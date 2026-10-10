#include "forge/terraintex.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

#include "forge/big.hpp"
#include "forge/defdecode.hpp"
#include "forge/lzo.hpp"
#include "forge/texturewrite.hpp"

namespace fs = std::filesystem;

namespace forge::terraintex {
namespace {

// CPixelFormatInit tails, empirically constant per format across the retail
// banks (FableTLC docs/TEXTURE_WRITER.md; matches EgoCore TextureBuilder).
constexpr uint8_t kTailDXT1[6] = {0x03, 0x04, 0, 0, 0, 0};
constexpr uint8_t kTailDXT3[6] = {0x02, 0x08, 0, 0, 0, 0};
constexpr uint8_t kTailDXT5[6] = {0x05, 0x08, 0, 0, 0, 0};
constexpr uint8_t kTailV8U8[6] = {0x04, 0x10, 8, 8, 0, 0};
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



} // namespace

const uint8_t* pixelFormatTail(uint32_t format) {
    switch (format & 0xFF) {
        case kFormatDXT1: return kTailDXT1;
        case kFormatDXT3: return kTailDXT3;
        case kFormatDXT5: return kTailDXT5;
        case kFormatV8U8: return kTailV8U8;
        case kFormatARGB: return kTailARGB;
        default: return nullptr;
    }
}

const char* pixelFormatName(uint32_t format) {
    switch (format & 0xFF) {
        case kFormatDXT1: return "DXT1";
        case kFormatDXT3: return "DXT3";
        case kFormatDXT5: return "DXT5";
        case kFormatV8U8: return "V8U8";
        case kFormatARGB: return "A8R8G8B8";
        default: return "unknown";
    }
}

size_t mipRawLength(uint32_t format, uint32_t w, uint32_t h) {
    if ((format & 0xFF) == kFormatARGB) return size_t(w) * size_t(h) * 4;
    if ((format & 0xFF) == kFormatV8U8) return size_t(w) * size_t(h) * 2;
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
        v.errors.push_back("unsupported pixel format " +
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

    // CGraphicDataBank::LoadGraphicFrame (031238F0) reads each surface's
    // GetByteLength (030C5F90): Width * Height * ColourDepth >> 3. Native
    // DXT5 therefore stores 4/1 bytes for 2x2/1x1 tails, not 16-byte blocks.
    // Keep the established DXT1/DXT3 writer contract separate from this path.
    const auto storedMipLength = [&](uint32_t width, uint32_t height) {
        return fmt == kFormatDXT5 ? size_t(width) * height
                                 : mipRawLength(fmt, width, height);
    };
    const size_t mip0Raw = storedMipLength(surfaceW, surfaceH) * depth;
    if (i.frameDataSize != mip0Raw)
        v.errors.push_back("FrameDataSize " + std::to_string(i.frameDataSize) +
                           " != uncompressed mip-0 size " + std::to_string(mip0Raw));

    size_t pos = 0;
    uint32_t w = surfaceW, h = surfaceH;
    uint32_t d = depth;
    for (uint32_t level = 0; level < mips; ++level) {
        const size_t raw = storedMipLength(w, h) * d;
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
    if ((i.pixelFormat & 0xFF) == kFormatDXT5 || (i.pixelFormat & 0xFF) == kFormatV8U8) {
        const std::string formatName=pixelFormatName(i.pixelFormat);
        out.error = formatName + " storage is valid but the texture preview decoder does not support " + formatName;
        return out;
    }
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
            bool found = false;
            const int32_t type = fieldInt32(decoded, "WaterType", found);
            if (found) theme.waterType = type;
            const int32_t family = fieldInt32(decoded, "NoWaterThemeDef", found);
            if (found && family > 0) theme.noWaterThemeDef = uint32_t(family);
            for (const auto& f : decoded.fields)
                if (f.name == "WaterHeight" && f.value.size() >= 4) { std::memcpy(&theme.waterHeight, f.value.data(), 4); break; }
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
        if (r.slot < 0 || r.slot >= 256) continue;
        // The LEV's def index goes stale when game.bin changes (retail LEVs are: Bowerstone
        // Bridge's "WATER_BWLAKE_8" slot indexes WATER_BWLAKE_1); the palette NAME is stable,
        // so when it names a theme that disagrees with the index, the name wins -- the same
        // rule the exporter applies. Without it a stale slot has no material and the layer
        // bake drops every pass that uses it.
        const ThemeEntry* theme = nullptr;
        if (!r.paletteName.empty()) {
            const ThemeEntry* byName = library.byName(r.paletteName);
            if (byName && byName->decoded && (!r.resolved || !r.nameMatches)) theme = byName;
        }
        if (!theme && r.resolved) theme = library.byDefIndex(r.defIndex);
        if (!theme) continue;
        auto& m = materials[size_t(r.slot)];
        m.available = true;
        for (int f = 0; f < 3; ++f) {
            m.base.textures[f] = theme->textures.base[f];
            m.cliff.textures[f] = theme->textures.cliff[f];
        }
    }
    return materials;
}


TextureValidation validateBigEntry(const fs::path& bigPath,
                                   std::string_view entryName,
                                   std::string* bankOut, uint32_t* idOut) {
    const auto file = big::File::open(bigPath);
    const std::string needle = upper(entryName);
    // A post-write check must resolve the exact entry just written. A unique
    // folded fallback remains useful for the read-only verification command.
    for (bool exact : {true,false}) {
        const big::Bank* matchedBank=nullptr;
        const big::Entry* matchedEntry=nullptr;
        for (const auto& bank : file.banks()) {
            for (const auto& entry : bank.entries) {
                if (exact ? entry.name!=entryName : upper(entry.name)!=needle) continue;
                if (matchedEntry) {
                    TextureValidation ambiguous;
                    ambiguous.errors.push_back("ambiguous texture entry name: " + std::string(entryName));
                    return ambiguous;
                }
                matchedBank=&bank;matchedEntry=&entry;
            }
        }
        if (matchedEntry) {
            if (bankOut) *bankOut=matchedBank->name;
            if (idOut) *idOut=matchedEntry->id;
            return validateTextureEntry(matchedEntry->subHeader,file.entryData(*matchedEntry));
        }
    }
    TextureValidation v;
    v.errors.push_back("entry " + std::string(entryName) + " not found in " +
                       bigPath.string());
    return v;
}

ImportResult importPng(const ImportRequest& request) {
    // Native path (2026-09-17): no Python. The Fable entry is built by
    // texturewrite (DXT1/DXT3/ARGB, mips, chunked-LZO mip 0, 34-byte Info) and
    // spliced into a copy of the bank through big::File::serialize().
    ImportResult result;
    auto fail = [&](const std::string& why) { result.validation.errors.push_back(why); return result; };
    std::string err;
    texturewrite::Image img = texturewrite::loadImage(request.png.string(), err);
    if (img.rgba.empty()) return fail(err);
    const int realW = img.width, realH = img.height;
    if (request.add && (realW > 65535 || realH > 65535))
        return fail("source image dimensions exceed the 16-bit texture header");
    std::error_code ec;
    if (!fs::is_regular_file(request.srcBig, ec)) return fail("no such bank " + request.srcBig.string());
    big::File file = big::File::open(request.srcBig);
    big::Bank* bank = nullptr;
    big::Entry* target = nullptr;
    for (auto& b : file.banks()) {
        for (auto& e : b.entries)
            if (e.name == request.entryName) {
                if (target) return fail("ambiguous texture entry name " + request.entryName);
                bank = &b; target = &e;
            }
    }
    uint32_t format = texturewrite::formatByName(request.format);
    if (!request.format.empty() && !format)
        return fail("unknown texture format '" + request.format + "' (dxt1 | dxt3 | argb8888)");
    uint32_t newId = 0;
    int retainedTransparency = -1;
    int allocW = 0, allocH = 0;
    int frameW = realW, frameH = realH;
    if (request.add) {
        if (target) return fail("entry " + request.entryName + " already exists in " + bank->name + " (replace it instead)");
        bank = file.findBank(request.subBank);
        if (!bank) return fail("bank " + request.subBank + " not found in " + request.srcBig.string());
        if (bank->entries.empty()) return fail("bank " + request.subBank + " is empty; nothing to model the new record on");
        for (const auto& entry : bank->entries) newId = std::max(newId, entry.id);
        if (newId == std::numeric_limits<uint32_t>::max()) return fail("no free texture entry ID");
        ++newId;
        if (!format) return fail("unknown texture format '" + request.format + "' (dxt1 | dxt3 | argb8888)");
        if (!request.dims.empty()) {
            const auto split = request.dims.find('x');
            auto dimension = [](std::string_view text, int& value) {
                const auto [end, error] = std::from_chars(text.data(), text.data()+text.size(), value);
                return !text.empty() && error == std::errc{} && end == text.data()+text.size()
                    && value > 0 && value <= 65535;
            };
            if (split == std::string::npos ||
                !dimension(std::string_view(request.dims).substr(0,split),allocW) ||
                !dimension(std::string_view(request.dims).substr(split+1),allocH))
                return fail("bad --dims " + request.dims + "; expected WxH with dimensions from 1 to 65535");
        } else {
            for (allocW = 1; allocW < realW; allocW <<= 1) {}
            for (allocH = 1; allocH < realH; allocH <<= 1) {}
            if (format == kFormatDXT1 || format == kFormatDXT3) {
                allocW = std::max(4,allocW); allocH = std::max(4,allocH);
            }
        }
        if (allocW > 65535 || allocH > 65535)
            return fail("allocated dimensions exceed the 16-bit texture header");
        if ((allocW & (allocW-1)) || (allocH & (allocH-1)))
            return fail("allocated texture dimensions must be powers of two");
    } else {
        if (!target) return fail("entry " + request.entryName + " not found in " + request.srcBig.string());
        TextureInfo info;
        if (!parseTextureInfo(target->subHeader.data(), target->subHeader.size(), info, err)) return fail(request.entryName + ": " + err);
        // CGraphicInfo::IsVolumeTexture (02D136E0) uses archive type 4,
        // including a volume with only one slice or a damaged zero depth.
        if (target->type == 4 || info.frameCount > 1 || info.depth != 0)
            return fail(request.entryName + ": image replacement supports only single-frame 2D textures");
        // Native IsBooleanAlpha (0263DE40) selects both DXT1 mode 2 and
        // non-DXT1 mode 4. Preserve the reviewed DXT1/DXT3 combinations.
        const uint32_t originalFormat = info.pixelFormat & 0xFF;
        const bool cutoutSlot = (originalFormat == kFormatDXT1 && info.transparency == 2) ||
                                (originalFormat == kFormatDXT3 && info.transparency == 4);
        if ((info.transparency > 1 && !cutoutSlot) ||
            (originalFormat == kFormatDXT1 && info.transparency != 0 && !cutoutSlot))
            return fail(request.entryName + ": image replacement cannot preserve transparency mode " +
                        std::to_string(info.transparency));
        if (cutoutSlot && format && format != originalFormat)
            return fail(request.entryName + ": cutout texture format conversion is not supported");
        if (!info.allocWidth || !info.allocHeight || !info.frameWidth || !info.frameHeight ||
            info.frameWidth > info.allocWidth || info.frameHeight > info.allocHeight)
            return fail(request.entryName + ": invalid texture slot dimensions");
        allocW = info.allocWidth; allocH = info.allocHeight;   // the slot keeps its allocated size
        frameW = info.frameWidth; frameH = info.frameHeight; // replacement keeps the slot's logical size too
        if (!format) format = info.pixelFormat & 0xFF;
        if (format != texturewrite::kFormatDXT1 && format != texturewrite::kFormatDXT3 && format != texturewrite::kFormatARGB)
            return fail(request.entryName + " uses pixel format " + std::to_string(format) + ", which this writer cannot encode");
        // IsAlpha (0263DC90) tests this byte independently of the pixel format.
        // An explicit format conversion uses the new format's default instead.
        if (format == (info.pixelFormat & 0xFF)) retainedTransparency = info.transparency;
    }
    if ((format == kFormatDXT1 || format == kFormatDXT3) && (allocW < 4 || allocH < 4))
        return fail("compressed texture allocation dimensions must be at least 4x4");
    img = texturewrite::resample(img, allocW, allocH);
    texturewrite::Entry built = texturewrite::buildEntry(img, format, frameW, frameH, !request.rawMip0, 0,
                                                       format == kFormatDXT1 && retainedTransparency == 2);
    if (retainedTransparency >= 0) built.info[16] = uint8_t(retainedTransparency);
    const auto builtValidation = validateTextureEntry(built.info, built.payload);
    if (!builtValidation.ok) {
        result.validation = builtValidation;
        return result;
    }
    if (request.add) {
        big::Entry e;
        const big::Entry& model = bank->entries.back();
        e.magic = file.magic() == "BIGB" ? 42 : model.magic;
        // This writer creates a single 2D image, never a sequence/bump/volume
        // resource. Native PrepareDataForInclusion (03118180) uses type 0 for it.
        e.type = 0; e.devFileType = model.devFileType;
        e.id = newId;
        e.name = request.entryName;
        e.subHeader = built.info;
        e.data = built.payload;
        e.length = uint32_t(built.payload.size());
        bank->entries.push_back(std::move(e));
        target = &bank->entries.back();
    } else {
        target->subHeader = built.info;
        target->data = built.payload;
        target->length = uint32_t(built.payload.size());
    }
    result.entryId = target->id;
    result.bankName = bank->name;
    const auto bytes = file.serialize();
    {
        std::ofstream out(request.outBig, std::ios::binary | std::ios::trunc);
        if (!out) return fail("cannot write " + request.outBig.string());
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        out.close();
        if (!out) return fail("write to " + request.outBig.string() + " failed");
    }
    result.command = std::string("native texturewrite: ") + (request.add ? "add " : "replace ") + request.entryName + " " +
                     std::to_string(allocW) + "x" + std::to_string(allocH) + " fmt " + std::to_string(format) + " mips " + std::to_string(built.mips);
    result.exitCode = 0;
    result.validation = validateBigEntry(request.outBig, request.entryName, &result.bankName, &result.entryId);
    result.ok = result.validation.ok;
    return result;
}

} // namespace forge::terraintex
