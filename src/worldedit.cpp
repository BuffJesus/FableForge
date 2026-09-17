#include "worldedit.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "forge/bwd.hpp"
#include "forge/lev.hpp"
#include "forge/stb.hpp"
#include "forge/stbbake.hpp"
#include "forge/stbheightbake.hpp"
#include "forge/stbinfo.hpp"
#include "forge/wad.hpp"
#include "forge/wld.hpp"
#include "forge/worldinstall.hpp"

namespace albion::editor {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool backupOnce(const fs::path& p, std::string& error) {
    const fs::path b = p.string() + ".atlas-orig";
    try { if (fs::exists(p) && !fs::exists(b)) fs::copy_file(p, b); return true; }
    catch (const std::exception& e) { error = e.what(); return false; }
}

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The donor's .lev / .tng bytes as the game would load them: loose file first,
// else the WAD entry.
std::vector<uint8_t> levelBytes(const fs::path& gameRoot, const forge::wad::Archive& wad, const std::string& stem, const char* ext) {
    const fs::path loose = gameRoot / "data" / "Levels" / "FinalAlbion" / (stem + ext);
    if (fs::exists(loose)) return readFile(loose);
    const std::string want = lower(stem + ext);
    for (const auto& e : wad.entries())
        if (lower(fs::path(e.name).filename().string()) == want) return wad.read(e);
    throw std::runtime_error(stem + ext + " is neither loose nor in FinalAlbion.wad");
}

} // namespace

bool donorInfo(const fs::path& gameRoot, const std::string& donor, DonorInfo& out, std::string& error) {
    try {
        const fs::path levels = gameRoot / "data" / "Levels";
        const auto world = forge::wld::File::parse(levels / "FinalAlbion.wld");
        const std::string wantLev = lower(donor + ".lev");
        const forge::wld::Map* wm = nullptr;
        for (const auto& m : world.maps())
            if (lower(fs::path(m.levelName).filename().string()) == wantLev) { wm = &m; break; }
        if (!wm) { error = donor + " is not placed in FinalAlbion.wld"; return false; }
        out.worldX = wm->mapX; out.worldY = wm->mapY;
        out.regions.clear();
        for (const auto& r : world.regions()) {
            out.regions.push_back(r.regionName);
            for (const auto& n : r.containsMaps)
                if (lower(fs::path(n).filename().string()) == wantLev && out.owningRegion.empty()) out.owningRegion = r.regionName;
        }
        const auto bwd = forge::bwd::File::parse(levels / "FinalAlbion.bwd");
        const forge::bwd::MapInfo* bm = bwd.findMap(donor);
        if (!bm) { error = donor + " is not in FinalAlbion.bwd"; return false; }
        out.width = bm->right - bm->left; out.height = bm->bottom - bm->top;
        const auto origin = forge::worldinstall::suggestOrigin(gameRoot, donor);
        out.suggestedX = origin.x; out.suggestedY = origin.y;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool createLevelFromDonor(const fs::path& gameRoot, const NewLevelRequest& req, NewLevelResult& out, std::string& error) {
    try {
        const fs::path levels = gameRoot / "data" / "Levels";
        const fs::path wadPath = levels / "FinalAlbion.wad";
        const fs::path stbPath = levels / "FinalAlbion_RT.stb";
        forge::worldinstall::Request ir;
        ir.gameRoot = gameRoot;
        ir.donorLevelName = req.donor;
        ir.newLevelName = req.name;
        ir.worldX = req.worldX; ir.worldY = req.worldY;
        ir.hostRegion = req.hostRegion;
        ir.backupSuffix.clear();   // Atlas keeps its own .atlas-orig copies

        // the donor's current bytes (loose edits included) become the new level's
        const auto wad = forge::wad::Archive::open(wadPath);
        ir.levBytes = levelBytes(gameRoot, wad, req.donor, ".lev");
        ir.tngBytes = levelBytes(gameRoot, wad, req.donor, ".tng");

        if (req.rebakeChunk) {
            // re-bake the donor's terrain chunk for the new origin: same LEV, no
            // neighbours (the copy stands alone), every shared-edge sample clamps locally
            const auto archive = forge::stb::Archive::open(stbPath);
            const forge::stb::StaticMap* map = nullptr;
            const std::string wantLev = lower(req.donor + ".lev");
            for (const auto& m : archive.staticMaps())
                if (lower(fs::path(m.levelName).filename().string()) == wantLev) { map = &m; break; }
            if (!map) { error = req.donor + " has no static map in FinalAlbion_RT.stb"; return false; }
            const auto record = archive.readStaticMapRecord(*map);
            if (record.size() < forge::stbinfo::kInfoBlockSize) { error = "static-map record too short"; return false; }
            uint32_t bankIndex = 0; std::memcpy(&bankIndex, record.data() + 4, 4);
            const forge::stb::Entry* entry = nullptr;
            for (const auto& e : archive.entries()) if (e.id == bankIndex) { entry = &e; break; }
            if (!entry) { error = "static-map bank entry " + std::to_string(bankIndex) + " not found"; return false; }
            const auto chunk = archive.read(*entry);
            const fs::path tmp = fs::temp_directory_path() / "Albion Atlas" / "newlevel";
            fs::create_directories(tmp);
            const fs::path levTmp = tmp / (req.donor + ".lev");
            std::ofstream(levTmp, std::ios::binary).write(reinterpret_cast<const char*>(ir.levBytes.data()), std::streamsize(ir.levBytes.size()));
            const auto lev = forge::lev::File::open(levTmp);
            forge::stbbake::HeightfieldBakeOptions opt;
            opt.requireCanonicalSize = false;
            const auto baked = forge::stbbake::bakeHeightfield(chunk, lev, req.worldX, req.worldY, opt);
            for (const auto& n : baked.notes) if (n.rfind("foreground frame", 0) != 0) out.notes.push_back("bake: " + n);
            ir.chunkBytes = baked.chunk;
            out.notes.push_back("terrain chunk re-baked for origin (" + std::to_string(req.worldX) + "," + std::to_string(req.worldY) + "): " + std::to_string(baked.chunk.size()) + " bytes");
        }

        for (const char* f : {"FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion.wad", "FinalAlbion_RT.stb"})
            if (!backupOnce(levels / f, error)) return false;
        const auto r = forge::worldinstall::installLevel(ir);
        out.mapSlot = r.mapSlot;
        out.worldX = r.left; out.worldY = r.top; out.width = r.right - r.left; out.height = r.bottom - r.top;
        for (const auto& n : r.notes) out.notes.push_back(n);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

} // namespace albion::editor
