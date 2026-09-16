#include "forge/terraintex.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "forge/big.hpp"
#include "forge/defdecode.hpp"
#include "forge/lzo.hpp"

namespace fs = std::filesystem;

namespace forge::terraintex {
namespace {

// CPixelFormatInit tails, empirically constant per format across the retail
// banks (FableTLC docs/TEXTURE_WRITER.md; matches EgoCore TextureBuilder).
constexpr uint8_t kTailDXT1[6] = {0x03, 0x04, 0, 0, 0, 0};
constexpr uint8_t kTailDXT3[6] = {0x02, 0x08, 0, 0, 0, 0};
constexpr uint8_t kTailARGB[6] = {0x01, 0x20, 8, 8, 8, 8};

uint16_t readU16(const uint8_t* p) {
    return uint16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}
uint32_t readU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = char(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool isPowerOfTwo(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }

uint32_t powerOfTwoUp(uint32_t v) {
    uint32_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

int32_t fieldInt32(const defdecode::Decoded& decoded, std::string_view name,
                   bool& found) {
    for (const auto& f : decoded.fields) {
        if (f.name == name && f.value.size() >= 4) {
            found = true;
            return int32_t(readU32(f.value.data()));
        }
    }
    found = false;
    return 0;
}

// Run a command, capturing combined stdout/stderr. Kept deliberately simple:
// the only external process forge launches here is the python texture writer.
int runCommand(const std::string& command, std::string& output) {
    fs::path logPath;
    {
        std::error_code ec;
        const fs::path base = fs::temp_directory_path(ec);
        logPath = (ec ? fs::path(".") : base) /
                  ("forge_texture_import_" + std::to_string(std::rand()) + ".log");
    }
    std::string full = command + " > \"" + logPath.string() + "\" 2>&1";
#ifdef _WIN32
    // cmd.exe strips the outer pair of quotes from the whole command line.
    full = "\"" + full + "\"";
#endif
    const int rc = std::system(full.c_str());
    std::ifstream log(logPath, std::ios::binary);
    if (log) {
        std::ostringstream ss;
        ss << log.rdbuf();
        output = ss.str();
    }
    log.close();
    std::error_code ec;
    fs::remove(logPath, ec);
    return rc;
}

std::string quoted(const fs::path& p) { return "\"" + p.string() + "\""; }

} // namespace

const uint8_t* pixelFormatTail(uint32_t format) {
    switch (format & 0xFF) {
        case kFormatDXT1: return kTailDXT1;
        case kFormatDXT3: return kTailDXT3;
        case kFormatARGB: return kTailARGB;
        default: return nullptr;
    }
}

const char* pixelFormatName(uint32_t format) {
    switch (format & 0xFF) {
        case kFormatDXT1: return "DXT1";
        case kFormatDXT3: return "DXT3";
        case kFormatARGB: return "A8R8G8B8";
        default: return "unknown";
    }
}

size_t mipRawLength(uint32_t format, uint32_t w, uint32_t h) {
    if ((format & 0xFF) == kFormatARGB) return size_t(w) * size_t(h) * 4;
    const size_t unit = (format & 0xFF) == kFormatDXT1 ? 8 : 16;
    return size_t((w + 3) / 4) * size_t((h + 3) / 4) * unit;
}

bool parseTextureInfo(const uint8_t* data, size_t len, TextureInfo& out,
                      std::string& error) {
    if (data == nullptr || len < 34) {
        error = "Info blob is " + std::to_string(len) + " bytes, need 34";
        return false;
    }
    out.allocWidth = readU16(data + 0);
    out.allocHeight = readU16(data + 2);
    out.depth = readU16(data + 4);
    out.frameWidth = readU16(data + 6);
    out.frameHeight = readU16(data + 8);
    out.frameCount = readU16(data + 10);
    out.pixelFormat = readU32(data + 12);
    out.transparency = data[16];
    out.mipLevels = data[17];
    out.flags = data[18];
    out.padding = data[19];
    out.frameDataSize = readU32(data + 20);
    out.mipSize0 = readU32(data + 24);
    std::memcpy(out.tail, data + 28, 6);
    return true;
}

TextureValidation validateTextureEntry(const std::vector<uint8_t>& info,
                                       const std::vector<uint8_t>& payload) {
    TextureValidation v;
    std::string parseError;
    if (!parseTextureInfo(info.data(), info.size(), v.info, parseError)) {
        v.errors.push_back(parseError);
        return v;
    }
    const TextureInfo& i = v.info;
    const uint32_t fmt = i.pixelFormat & 0xFF;

    const uint8_t* tail = pixelFormatTail(fmt);
    if (tail == nullptr) {
        v.errors.push_back("unsupported pixel format 0x" +
                           std::to_string(i.pixelFormat));
        return v;
    }
    if (std::memcmp(tail, i.tail, 6) != 0) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "CPixelFormatInit tail %02x %02x %02x %02x %02x %02x does "
                      "not match %s (%02x %02x %02x %02x %02x %02x)",
                      i.tail[0], i.tail[1], i.tail[2], i.tail[3], i.tail[4],
                      i.tail[5], pixelFormatName(fmt), tail[0], tail[1], tail[2],
                      tail[3], tail[4], tail[5]);
        v.errors.push_back(buf);
    }
    if (i.allocWidth == 0 || i.allocHeight == 0)
        v.errors.push_back("allocated surface has a zero dimension");
    if (i.frameWidth > i.allocWidth || i.frameHeight > i.allocHeight)
        v.errors.push_back("frame (authored) dimensions exceed the allocated surface");
    // The engine allocates a power-of-two surface; a non-pow2 "allocated" size
    // in Info is rounded up for every mip size (retail EDITORGUI_* icons are
    // 20x20 with a 32x32 = 512-byte mip 0).
    const uint32_t surfaceW = powerOfTwoUp(i.allocWidth);
    const uint32_t surfaceH = powerOfTwoUp(i.allocHeight);
    if (surfaceW != i.allocWidth || surfaceH != i.allocHeight)
        v.warnings.push_back("allocated surface is not power-of-two; sizes use "
                             "the rounded-up surface");
    if (i.padding != 0)
        v.warnings.push_back("Info padding byte is not zero");

    const uint32_t mips = i.mipLevels == 0 ? 1 : i.mipLevels;
    if (i.mipLevels == 0) v.warnings.push_back("MipmapLevels is 0, treated as 1");

    // Volume textures (Depth > 0) store Depth slices per level. Retail carries
    // four of them in textures.big (WEATHER_RAIN/MIST_ALPHA, Depth 8 and 64),
    // whose payloads are exactly depth * the 2D surface size.
    const uint32_t depth = i.depth == 0 ? 1 : i.depth;
    if (depth > 1 && i.mipLevels > 1)
        v.warnings.push_back("volume texture with a mip chain: slice count per "
                             "level is assumed to halve");

    const size_t mip0Raw = mipRawLength(fmt, surfaceW, surfaceH) * depth;
    if (i.frameDataSize != mip0Raw)
        v.errors.push_back("FrameDataSize " + std::to_string(i.frameDataSize) +
                           " != uncompressed mip-0 size " + std::to_string(mip0Raw));

    size_t pos = 0;
    uint32_t w = surfaceW, h = surfaceH;
    uint32_t d = depth;
    for (uint32_t level = 0; level < mips; ++level) {
        const size_t raw = mipRawLength(fmt, w, h) * d;
        v.mipRawSizes.push_back(raw);
        if (level == 0 && i.mipSize0 > 0) {
            // Fable chunked-LZO region: chunks cover raw-3 bytes, then 3 raw
            // tail bytes. MipSize0 is the whole on-disk region.
            const size_t start = pos;
            const size_t target = raw > 3 ? raw - 3 : 0;
            size_t produced = 0;
            bool bad = false;
            while (produced < target) {
                if (pos + 2 > payload.size()) {
                    v.errors.push_back("mip-0 chunk header runs past the payload");
                    bad = true;
                    break;
                }
                uint32_t clen = readU16(payload.data() + pos);
                pos += 2;
                if (clen == 0xFFFF) {
                    if (pos + 4 > payload.size()) {
                        v.errors.push_back("mip-0 u32 chunk-length escape truncated");
                        bad = true;
                        break;
                    }
                    clen = readU32(payload.data() + pos);
                    pos += 4;
                }
                ++v.chunkCount;
                if (clen == 0) {  // stored chunk: the rest is copied verbatim
                    const size_t take = target - produced;
                    if (pos + take > payload.size()) {
                        v.errors.push_back("stored mip-0 chunk runs past the payload");
                        bad = true;
                        break;
                    }
                    pos += take;
                    produced = target;
                    break;
                }
                if (pos + clen > payload.size()) {
                    v.errors.push_back("compressed mip-0 chunk runs past the payload");
                    bad = true;
                    break;
                }
                std::vector<uint8_t> chunk;
                try {
                    chunk = lzo::decompressBounded(payload.data() + pos, clen,
                                                   target - produced);
                } catch (const std::exception& e) {
                    v.errors.push_back(std::string("mip-0 LZO chunk ") +
                                       std::to_string(v.chunkCount) +
                                       " failed to decode: " + e.what());
                    bad = true;
                    break;
                }
                produced += chunk.size();
                pos += clen;
            }
            if (bad) return v;
            if (produced != target)
                v.errors.push_back("mip-0 chunks produced " +
                                   std::to_string(produced) + " bytes, expected " +
                                   std::to_string(target));
            if (raw >= 3) {
                if (pos + 3 > payload.size()) {
                    v.errors.push_back("mip-0 3-byte raw tail runs past the payload");
                    return v;
                }
                pos += 3;
            }
            v.mip0RegionSize = pos - start;
            if (v.mip0RegionSize != i.mipSize0)
                v.errors.push_back("measured mip-0 region " +
                                   std::to_string(v.mip0RegionSize) +
                                   " != MipSize0 " + std::to_string(i.mipSize0));
        } else {
            if (pos + raw > payload.size()) {
                v.errors.push_back("mip " + std::to_string(level) +
                                   " raw surface runs past the payload (need " +
                                   std::to_string(raw) + ", have " +
                                   std::to_string(payload.size() - pos) + ")");
                return v;
            }
            pos += raw;
            if (level == 0) v.mip0RegionSize = raw;
        }
        w = std::max(1u, w >> 1);
        h = std::max(1u, h >> 1);
        d = std::max(1u, d >> 1);
    }
    // Multi-frame sprites repeat the whole mip chain once per frame (verified on
    // the retail 20x20 4-frame EDITORGUI_* icons: 4 * (512+128+32+8) = 2720 B).
    const size_t frames = i.frameCount == 0 ? 1 : i.frameCount;
    v.consumed = pos * frames;
    if (frames > 1 && i.mipSize0 > 0) {
        v.warnings.push_back("multi-frame entry with a compressed mip 0: only "
                             "frame 0 was checked");
        v.consumed = pos;
    } else if (frames > 1 && pos == payload.size()) {
        // Retail GRAPHIC_EFFECT_SHOOFLES_3 declares FrameCount 4 but stores a
        // single chain (its FrameCount-1 siblings are byte-identical in size).
        v.warnings.push_back("FrameCount is " + std::to_string(frames) +
                             " but only one mip chain is stored");
        v.consumed = pos;
    } else if (v.consumed != payload.size()) {
        const std::string msg = "payload has " + std::to_string(payload.size()) +
                                " bytes, the mip chain accounts for " +
                                std::to_string(v.consumed);
        v.errors.push_back(v.consumed < payload.size() ? msg + " (trailing bytes)"
                                                       : msg);
    }
    v.ok = v.errors.empty();
    return v;
}

DecodedMip0 decodeMip0(const std::vector<uint8_t>& info,
                       const std::vector<uint8_t>& payload) {
    DecodedMip0 out;
    const auto validation = validateTextureEntry(info, payload);
    if (!validation.ok) {
        out.error = validation.errors.empty() ? "invalid texture entry"
                                               : validation.errors.front();
        return out;
    }
    const TextureInfo& i = validation.info;
    out.width = powerOfTwoUp(i.allocWidth);
    out.height = powerOfTwoUp(i.allocHeight);
    const uint32_t fmt = i.pixelFormat & 0xFF;
    out.format = fmt == kFormatDXT1 ? PreviewFormat::BC1
                 : fmt == kFormatDXT3 ? PreviewFormat::BC2
                                      : PreviewFormat::BGRA8;
    const size_t raw = mipRawLength(fmt, out.width, out.height) *
                       (i.depth == 0 ? 1u : i.depth);
    out.bytes.reserve(raw);
    if (i.mipSize0 == 0) {
        out.bytes.assign(payload.begin(), payload.begin() + raw);
    } else {
        size_t pos = 0;
        const size_t target = raw > 3 ? raw - 3 : 0;
        while (out.bytes.size() < target) {
            uint32_t clen = readU16(payload.data() + pos);
            pos += 2;
            if (clen == 0xFFFF) {
                clen = readU32(payload.data() + pos);
                pos += 4;
            }
            if (clen == 0) {
                const size_t take = target - out.bytes.size();
                out.bytes.insert(out.bytes.end(), payload.begin() + pos,
                                 payload.begin() + pos + take);
                pos += take;
                break;
            }
            const auto chunk = lzo::decompressBounded(
                payload.data() + pos, clen, target - out.bytes.size());
            out.bytes.insert(out.bytes.end(), chunk.begin(), chunk.end());
            pos += clen;
        }
        if (raw >= 3)
            out.bytes.insert(out.bytes.end(), payload.begin() + pos,
                             payload.begin() + pos + 3);
    }
    if (out.bytes.size() != raw) {
        out.error = "decoded mip-0 length mismatch";
        out.bytes.clear();
        return out;
    }
    out.ok = true;
    return out;
}

// ----------------------------------------------------------------- resolver
ThemeLibrary ThemeLibrary::load(const bin::File& defs,
                                const defschema::Schema& schema) {
    ThemeLibrary lib;
    const auto& entries = defs.entries();
    for (size_t index = 0; index < entries.size(); ++index) {
        const auto& e = entries[index];
        if (e.definition != "ENGINE_THEME") continue;
        ThemeEntry theme;
        theme.defIndex = uint32_t(index);
        theme.name = e.name;
        const auto* type = defdecode::resolveType(schema, e.definition, e.data);
        if (type != nullptr) {
            const auto decoded = defdecode::decode(e.data, *type);
            static const char* kFields[6] = {
                "BaseTexture",      "BackgroundTexture",      "BaseBumpMap",
                "CliffBaseTexture", "CliffBackgroundTexture", "CliffBumpMap"};
            uint32_t* slots[6] = {&theme.textures.base[0], &theme.textures.base[1],
                                  &theme.textures.base[2], &theme.textures.cliff[0],
                                  &theme.textures.cliff[1], &theme.textures.cliff[2]};
            bool all = true;
            for (int f = 0; f < 6; ++f) {
                bool found = false;
                const int32_t value = fieldInt32(decoded, kFields[f], found);
                if (!found) { all = false; continue; }
                *slots[f] = uint32_t(value < 0 ? 0 : value);
            }
            theme.decoded = all;
        }
        lib.themes_.push_back(std::move(theme));
    }
    return lib;
}

ThemeLibrary ThemeLibrary::loadFromRoot(const fs::path& gameRoot,
                                        const fs::path& schemaPath,
                                        const std::string& binName) {
    const fs::path defsDir = gameRoot / "data" / "CompiledDefs";
    const auto defs = bin::File::open(defsDir / "names.bin",
                                      defsDir / (binName.empty() ? "game.bin" : binName));
    const auto schema = defschema::Schema::load(schemaPath);
    return load(defs, schema);
}

const ThemeEntry* ThemeLibrary::byDefIndex(uint32_t index) const {
    for (const auto& t : themes_)
        if (t.defIndex == index) return &t;
    return nullptr;
}

const ThemeEntry* ThemeLibrary::byName(std::string_view name) const {
    const std::string needle = upper(name);
    for (const auto& t : themes_)
        if (upper(t.name) == needle) return &t;
    return nullptr;
}

std::set<uint32_t> ThemeLibrary::referencedTextures() const {
    std::set<uint32_t> ids;
    for (const auto& t : themes_) {
        if (!t.decoded) continue;
        for (int f = 0; f < 3; ++f) {
            if (t.textures.base[f] != 0) ids.insert(t.textures.base[f]);
            if (t.textures.cliff[f] != 0) ids.insert(t.textures.cliff[f]);
        }
    }
    return ids;
}

std::vector<PaletteSlotResolution> resolvePalette(const lev::File& level,
                                                  const ThemeLibrary& library) {
    std::vector<PaletteSlotResolution> out;
    const auto& palette = level.groundThemes();
    std::vector<size_t> counts(palette.size(), 0);
    for (int y = 0; y < level.cellsY(); ++y) {
        for (int x = 0; x < level.cellsX(); ++x) {
            for (int slot = 0; slot < 3; ++slot) {
                const uint8_t index = level.themeIndexAt(x, y, slot);
                if (index < counts.size()) ++counts[index];
            }
        }
    }
    for (size_t slot = 0; slot < palette.size(); ++slot) {
        if (palette[slot].name.empty()) continue;
        PaletteSlotResolution r;
        r.slot = int(slot);
        r.paletteName = palette[slot].name;
        r.defIndex = palette[slot].value;
        r.cellCount = counts[slot];
        if (const ThemeEntry* theme = library.byDefIndex(r.defIndex)) {
            r.defName = theme->name;
            r.textures = theme->textures;
            r.resolved = theme->decoded;
            r.nameMatches = upper(theme->name) == upper(r.paletteName);
        }
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<terrain::TerrainThemeMaterial> paletteMaterials(
    const lev::File& level, const ThemeLibrary& library) {
    std::vector<terrain::TerrainThemeMaterial> materials(256);
    for (const auto& r : resolvePalette(level, library)) {
        if (!r.resolved || r.slot < 0 || r.slot >= 256) continue;
        auto& m = materials[size_t(r.slot)];
        m.available = true;
        for (int f = 0; f < 3; ++f) {
            m.base.textures[f] = r.textures.base[f];
            m.cliff.textures[f] = r.textures.cliff[f];
        }
    }
    return materials;
}

// ------------------------------------------------------------ import driver
fs::path findTextureBuilder(const fs::path& toolsDir) {
    std::vector<fs::path> candidates;
    if (!toolsDir.empty()) {
        candidates.push_back(toolsDir / "texture_build.py");
        candidates.push_back(toolsDir);
    }
    if (const char* env = std::getenv("FABLETLC_TOOLS"))
        candidates.push_back(fs::path(env) / "texture_build.py");
    if (const char* env = std::getenv("FABLETLC_ROOT"))
        candidates.push_back(fs::path(env) / "tools" / "texture_build.py");
    candidates.push_back(fs::path("D:/Documents/FableTLC/tools/texture_build.py"));
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c, ec)) return c;
    }
    return {};
}

TextureValidation validateBigEntry(const fs::path& bigPath,
                                   std::string_view entryName,
                                   std::string* bankOut, uint32_t* idOut) {
    const auto file = big::File::open(bigPath);
    const std::string needle = upper(entryName);
    for (const auto& bank : file.banks()) {
        for (const auto& entry : bank.entries) {
            if (upper(entry.name) != needle) continue;
            if (bankOut) *bankOut = bank.name;
            if (idOut) *idOut = entry.id;
            return validateTextureEntry(entry.subHeader, file.entryData(entry));
        }
    }
    TextureValidation v;
    v.errors.push_back("entry " + std::string(entryName) + " not found in " +
                       bigPath.string());
    return v;
}

ImportResult importPng(const ImportRequest& request) {
    ImportResult result;
    const fs::path builder = findTextureBuilder(request.toolsDir);
    if (builder.empty()) {
        result.validation.errors.push_back(
            "texture_build.py not found (pass --tools <dir> or set FABLETLC_TOOLS)");
        return result;
    }
    const fs::path python = request.python.empty() ? fs::path("python") : request.python;

    std::ostringstream cmd;
    cmd << quoted(python) << " " << quoted(builder) << " "
        << (request.add ? "add " : "replace ") << quoted(request.srcBig) << " "
        << quoted(request.outBig) << " ";
    if (request.add) cmd << request.subBank << " ";
    cmd << request.entryName << " " << quoted(request.png)
        << " --format " << request.format;
    if (request.add && !request.dims.empty()) cmd << " --dims " << request.dims;
    result.command = cmd.str();

    result.exitCode = runCommand(result.command, result.output);
    std::error_code ec;
    if (result.exitCode != 0 || !fs::is_regular_file(request.outBig, ec)) {
        result.validation.errors.push_back("texture_build.py failed (exit " +
                                           std::to_string(result.exitCode) + ")");
        return result;
    }
    result.validation = validateBigEntry(request.outBig, request.entryName,
                                         &result.bankName, &result.entryId);
    result.ok = result.validation.ok;
    return result;
}

} // namespace forge::terraintex
