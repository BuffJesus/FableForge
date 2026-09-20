#include "texturebrowse.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

#include "backups.hpp"
#include "forge/big.hpp"
#include "forge/terraintex.hpp"
#include "forge/texturewrite.hpp"

namespace albion::texbrowse {
namespace fs = std::filesystem;

namespace {
bool backupOnce(const fs::path& p, std::string& error) { return albion::backups::backupOnce(p, error); }   // <file>.forge-orig, once

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

// the importer writes a sibling file; swap it in, backup first, refuse while the game runs
bool runImport(const fs::path& gameRoot, forge::terraintex::ImportRequest ir, uint32_t& idOut,
               std::vector<std::string>& notes, std::string& error) {
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
    if (!backupOnce(big, error)) return false;
    ir.srcBig = big;
    ir.outBig = big.string() + ".atlas-tmp";
    const auto r = forge::terraintex::importPng(ir);
    if (!r.ok) {
        fs::remove(ir.outBig, ec);
        error = r.output.empty() ? r.command : r.output;
        for (const auto& e : r.validation.errors) error += (error.empty() ? "" : "; ") + e;
        if (error.empty()) error = "texture import failed";
        return false;
    }
    fs::rename(ir.outBig, big, ec);
    if (ec) { error = "cannot replace textures.big: " + ec.message(); return false; }
    idOut = r.entryId;
    const auto& v = r.validation;
    notes.push_back((ir.add ? "appended " : "replaced ") + entryLabel(ir.entryName) + " (id " + std::to_string(r.entryId) + ", " + std::to_string(v.info.frameWidth) + "x" +
                    std::to_string(v.info.frameHeight) + " " + formatLabel(v.info.pixelFormat) + ", " + std::to_string(int(v.info.mipLevels)) + " mips) in textures.big from " + ir.png.filename().string());
    for (const auto& w : v.warnings) notes.push_back("warning: " + w);
    if (!v.ok) { for (const auto& e : v.errors) notes.push_back("ERROR: " + e); error = "the written entry does not match the retail texture contract"; return false; }
    return true;
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
        const auto file = forge::big::File::open(texturesBig);
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

bool exportPng(const fs::path& texturesBig, const std::string& entryName, const fs::path& png, std::string& error) {
    terrainexport::Image img;
    if (!decodeTexture(texturesBig, entryName, img, error)) return false;
    const auto bytes = terrainexport::encodePng(img);
    std::error_code ec;
    fs::create_directories(png.parent_path(), ec);
    std::ofstream out(png, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot write " + png.string(); return false; }
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(out);
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
