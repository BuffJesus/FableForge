#include "texturebrowse.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <numeric>

#include "backups.hpp"
#include "pendingbanks.hpp"
#include "forge/big.hpp"
#include "forge/terraintex.hpp"
#include "forge/texturewrite.hpp"

namespace albion::texbrowse {
namespace fs = std::filesystem;

namespace {
fs::path bigPath(const fs::path& gameRoot) { return gameRoot / "data" / "graphics" / "pc" / "textures.big"; }

std::string formatLabel(uint32_t f) {
    switch (f & 0xFF) {
        case forge::texturewrite::kFormatDXT1: return "DXT1";
        case forge::texturewrite::kFormatDXT3: return "DXT3";
        case forge::texturewrite::kFormatARGB: return "ARGB8888";
        default: return "fmt " + std::to_string(f & 0xFF);
    }
}

// What the list shows for an entry: the symbol as is, or for the retail `[\DEV\...\NAME.TGA]`
// path symbols the file stem (BARREL_BRACED_1_24).
std::string entryLabel(const std::string& symbol) {
    if (symbol.empty() || symbol.front() != '[') return symbol;
    const size_t slash = symbol.find_last_of("\\/");
    std::string stem = symbol.substr(slash == std::string::npos ? 1 : slash + 1);
    if (!stem.empty() && stem.back() == ']') stem.pop_back();
    const size_t dot = stem.rfind('.');
    if (dot != std::string::npos) stem.resize(dot);
    return stem.empty() ? symbol : stem;
}

// by the label the list prints, the raw symbol, or the numeric id
const forge::big::Entry* findEntry(const forge::big::File& file, const std::string& name, std::string* bank = nullptr) {
    uint32_t id = 0; bool byId = !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (byId) id = uint32_t(std::strtoul(name.c_str(), nullptr, 10));
    for (const auto& b : file.banks())
        for (const auto& e : b.entries)
            if (e.name == name || entryLabel(e.name) == name || (byId && e.id == id)) { if (bank) *bank = b.name; return &e; }
    return nullptr;
}

// Prepare and validate an owned output before backing up and replacing the bank.
bool runImport(const fs::path& gameRoot, forge::terraintex::ImportRequest ir, uint32_t& idOut,
               std::vector<std::string>& notes, std::string& error) {
    try {
        if (backups::gameRunningIn(gameRoot)) { error = "Fable is running from this install; rewriting textures.big underneath it crashes it. Quit to the desktop first."; return false; }
        const fs::path big = bigPath(gameRoot);
        std::error_code ec;
        if (!fs::exists(big, ec)) { error = "no " + big.string(); return false; }
        if (!fs::exists(ir.png, ec)) { error = "no such image " + ir.png.string(); return false; }
        if (!ir.add) {   // the importer matches the raw symbol; the user names the label (or the id)
            try {
                const auto file = forge::big::File::open(big);
                const auto* e = findEntry(file, ir.entryName);
                if (!e) { error = "no texture named " + ir.entryName; return false; }
                ir.entryName = e->name;
            } catch (const std::exception& ex) { error = ex.what(); return false; }
        }
        detail::PendingBanks pending(gameRoot, ".forge-texture-import-");
        ir.srcBig = big;
        ir.outBig = pending.prepare("data/graphics/pc/textures.big");
        const auto r = forge::terraintex::importPng(ir);
        if (!r.ok || !r.validation.ok) {
            error = r.output.empty() ? r.command : r.output;
            for (const auto& e : r.validation.errors) error += (error.empty() ? "" : "; ") + e;
            if (error.empty()) error = "texture import failed";
            return false;
        }
        if (backups::gameRunningIn(gameRoot)) { error = "Fable started during texture import; quit the game before replacing textures.big"; return false; }
        if (!pending.install(true, error)) return false;
        idOut = r.entryId;
        const auto& v = r.validation;
        notes.push_back((ir.add ? "appended " : "replaced ") + entryLabel(ir.entryName) + " (id " + std::to_string(r.entryId) + ", " + std::to_string(v.info.frameWidth) + "x" +
                        std::to_string(v.info.frameHeight) + " " + formatLabel(v.info.pixelFormat) + ", " + std::to_string(int(v.info.mipLevels)) + " mips) in textures.big from " + ir.png.filename().string());
        for (const auto& w : v.warnings) notes.push_back("warning: " + w);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
}  // namespace

std::vector<TextureRow> listTextures(const fs::path& texturesBig, std::string& error) {
    std::vector<TextureRow> out;
    try {
        const auto file = forge::big::File::open(texturesBig);
        for (const auto& b : file.banks())
            for (const auto& e : b.entries) {
                TextureRow r;
                r.bank = b.name; r.name = e.name; r.id = e.id; r.bytes = e.length;
                r.label = entryLabel(e.name);
                forge::terraintex::TextureInfo info; std::string ierr;
                if (forge::terraintex::parseTextureInfo(e.subHeader.data(), e.subHeader.size(), info, ierr)) {
                    r.width = info.frameWidth; r.height = info.frameHeight;
                    r.allocWidth = info.allocWidth; r.allocHeight = info.allocHeight;
                    r.format = formatLabel(info.pixelFormat); r.mips = info.mipLevels;
                } else r.format = "?";
                out.push_back(std::move(r));
            }
    } catch (const std::exception& e) { error = e.what(); }
    return out;
}

bool decodeTexture(const fs::path& texturesBig, const std::string& entryName, terrainexport::Image& out, std::string& error) {
    try {
        return decodeTexture(forge::big::File::open(texturesBig), entryName, out, error);
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool decodeTexture(const forge::big::File& file, const std::string& entryName, terrainexport::Image& out, std::string& error) {
    try {
        const auto* e = findEntry(file, entryName);
        if (!e) { error = "no texture named " + entryName; return false; }
        const auto mip = forge::terraintex::decodeMip0(e->subHeader, file.entryData(*e));
        if (!mip.ok) { error = mip.error; return false; }
        using F = forge::terraintex::PreviewFormat;
        std::vector<uint8_t> rgba;
        if (mip.format == F::BC1) rgba = terrainexport::decodeBc1ToRgba(mip.bytes.data(), mip.width, mip.height);
        else if (mip.format == F::BC2) rgba = terrainexport::decodeBc2ToRgba(mip.bytes.data(), mip.width, mip.height);
        else rgba = terrainexport::bgra8ToRgba(mip.bytes.data(), mip.width, mip.height);
        out = terrainexport::Image{};
        out.width = mip.width; out.height = mip.height; out.rgba = std::move(rgba); out.name = e->name;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool decodeSpriteFrames(const std::vector<uint8_t>& info, const std::vector<uint8_t>& payload,
                        SpriteTexture& out, std::string& error) {
    out = {}; error.clear();
    try {
        forge::terraintex::TextureInfo header;
        if (!forge::terraintex::parseTextureInfo(info.data(), info.size(), header, error)) return false;
        const uint32_t frames = std::max(1u, uint32_t(header.frameCount));
        constexpr uint64_t maxPixels = 16ull * 1024 * 1024;
        auto rounded = [](uint32_t v) { uint32_t n=1; while(n<v) n<<=1; return n; };
        if (!header.frameWidth || !header.frameHeight || !header.allocWidth || !header.allocHeight ||
            header.allocWidth > 8192 || header.allocHeight > 8192 || header.frameWidth > 8192 ||
            uint64_t(header.frameHeight) * frames > 8192 ||
            uint64_t(header.frameWidth) * header.frameHeight * frames > maxPixels ||
            uint64_t(rounded(header.allocWidth)) * rounded(header.allocHeight) > maxPixels) {
            error = "Invalid or oversized sprite frame/atlas dimensions"; return false;
        }
        if (header.depth > 1) { error = "Volume textures are not sprite frame arrays"; return false; }
        const auto validation = forge::terraintex::validateTextureEntry(info, payload);
        if (!validation.ok) {
            error = validation.errors.empty() ? "Invalid sprite texture" : validation.errors.front(); return false;
        }
        const size_t rawChain = std::accumulate(validation.mipRawSizes.begin(), validation.mipRawSizes.end(), size_t(0));
        const size_t diskChain = rawChain - validation.mipRawSizes.front() + validation.mip0RegionSize;
        const bool repeated = frames > 1 && header.mipSize0 == 0 && payload.size() == rawChain * frames;
        if (header.mipSize0 && frames > 1 && payload.size() != diskChain) {
            error = "Compressed multi-frame texture arrays are not supported by the preview"; return false;
        }
        const uint32_t columns = header.allocWidth / header.frameWidth;
        const uint32_t rows = header.allocHeight / header.frameHeight;
        if (!repeated && (payload.size() != diskChain || uint64_t(columns) * rows < frames)) {
            error = "Declared sprite frames are missing from the stored texture"; return false;
        }
        auto singleInfo = info; singleInfo[10] = 1; singleInfo[11] = 0;
        SpriteTexture result; result.frames = frames;
        result.image.width = header.frameWidth; result.image.height = header.frameHeight * frames;
        result.image.rgba.resize(size_t(result.image.width) * result.image.height * 4);
        std::vector<uint8_t> pixels;
        uint32_t surfaceWidth = 0, surfaceHeight = 0;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            if (frame == 0 || repeated) {
                const size_t begin = repeated ? size_t(frame) * rawChain : 0;
                const size_t length = repeated ? rawChain : diskChain;
                const std::vector<uint8_t> chain(payload.begin() + begin, payload.begin() + begin + length);
                const auto mip = forge::terraintex::decodeMip0(singleInfo, chain);
                if (!mip.ok) { error = mip.error; return false; }
                surfaceWidth = mip.width; surfaceHeight = mip.height;
                // Non-power-of-two allocation fields are rounded by the native
                // decoder. Bound that actual allocation before RGBA expansion.
                if (uint64_t(surfaceWidth) * surfaceHeight > maxPixels) {
                    error = "Rounded sprite surface exceeds the preview pixel limit"; return false;
                }
                using F = forge::terraintex::PreviewFormat;
                if (mip.format == F::BC1) pixels = terrainexport::decodeBc1ToRgba(mip.bytes.data(), mip.width, mip.height);
                else if (mip.format == F::BC2) pixels = terrainexport::decodeBc2ToRgba(mip.bytes.data(), mip.width, mip.height);
                else pixels = terrainexport::bgra8ToRgba(mip.bytes.data(), mip.width, mip.height);
            }
            const uint32_t x = repeated ? 0 : (frame % columns) * header.frameWidth;
            const uint32_t y = repeated ? 0 : (frame / columns) * header.frameHeight;
            if (x + header.frameWidth > surfaceWidth || y + header.frameHeight > surfaceHeight) {
                error = "Sprite frame exceeds its decoded surface"; return false;
            }
            for (uint32_t row = 0; row < header.frameHeight; ++row) {
                const size_t source = (size_t(y + row) * surfaceWidth + x) * 4;
                const size_t dest = (size_t(frame) * header.frameHeight + row) * header.frameWidth * 4;
                std::copy_n(pixels.data() + source, size_t(header.frameWidth) * 4, result.image.rgba.data() + dest);
            }
        }
        out = std::move(result); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool decodeSpriteTexture(const fs::path& texturesBig, const std::string& entryName,
                         SpriteTexture& out, std::string& error) {
    out = {}; error.clear();
    try {
        const auto file = forge::big::File::open(texturesBig);
        const auto* e = findEntry(file, entryName);
        if (!e) { error = "no texture named " + entryName; return false; }
        if (!decodeSpriteFrames(e->subHeader, file.entryData(*e), out, error)) return false;
        out.image.name = e->name; return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool exportPng(const fs::path& texturesBig, const std::string& entryName, const fs::path& png, std::string& error) {
    terrainexport::Image img;
    if (!decodeTexture(texturesBig, entryName, img, error)) return false;
    return terrainexport::writePng(img, png, error);
}

bool replaceTexture(const fs::path& gameRoot, const std::string& entryName, const fs::path& image,
                    std::vector<std::string>& notes, std::string& error) {
    forge::terraintex::ImportRequest ir;
    ir.png = image; ir.entryName = entryName; ir.add = false; ir.format.clear();   // the slot's own format
    uint32_t id = 0;
    return runImport(gameRoot, ir, id, notes, error);
}

bool addTexture(const fs::path& gameRoot, const std::string& bank, const std::string& entryName, const fs::path& image,
                const std::string& format, uint32_t& idOut, std::vector<std::string>& notes, std::string& error) {
    if (entryName.empty()) { error = "the new texture needs a name"; return false; }
    forge::terraintex::ImportRequest ir;
    ir.png = image; ir.entryName = entryName; ir.add = true;
    ir.subBank = bank.empty() ? "GBANK_MAIN_PC" : bank;
    ir.format = format.empty() ? "dxt1" : format;
    return runImport(gameRoot, ir, idOut, notes, error);
}

}  // namespace albion::texbrowse
