#include "forge/worldinstall.hpp"

#include "forge/bwd.hpp"
#include "forge/stb.hpp"
#include "forge/stbinfo.hpp"
#include "forge/wad.hpp"
#include "forge/wld.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <stdexcept>
#include <system_error>

namespace forge::worldinstall {
namespace {

namespace fs = std::filesystem;

std::string lowered(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool validStem(const std::string& name) {
    if (name.empty() || name.size() > 60) return false;
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

struct Box { int left, top, right, bottom; std::string name; };

bool overlaps(const Box& a, int left, int top, int right, int bottom) {
    return a.left < right && left < a.right && a.top < bottom && top < a.bottom;
}

std::vector<Box> mapBoxes(const bwd::File& bwd) {
    std::vector<Box> boxes;
    for (const auto& m : bwd.maps()) {
        if (!m.used) continue;
        boxes.push_back({m.left, m.top, m.right, m.bottom, m.scriptName});
    }
    return boxes;
}

} // namespace

Origin suggestOrigin(const fs::path& gameRoot, const std::string& donorLevelName) {
    const fs::path levelsDir = gameRoot / "data" / "Levels";
    const auto bwd = bwd::File::parse(levelsDir / "FinalAlbion.bwd");
    const bwd::MapInfo* donor = bwd.findMap(donorLevelName);
    if (!donor) throw std::runtime_error("worldinstall: donor '" + donorLevelName + "' is not in FinalAlbion.bwd");
    const int w = donor->right - donor->left, h = donor->bottom - donor->top;
    const auto boxes = mapBoxes(bwd);
    int maxRight = 0, maxBottom = 0;
    for (const auto& b : boxes) { maxRight = std::max(maxRight, b.right); maxBottom = std::max(maxBottom, b.bottom); }
    // scan the donor's row band first (rightwards, one map-width gap), then rows below
    auto free = [&](int x, int y) {
        for (const auto& b : boxes) if (overlaps(b, x, y, x + w, y + h)) return false;
        return true;
    };
    for (int y = donor->top; y <= maxBottom + h; y += 32)
        for (int x = donor->left; x <= maxRight + 2 * w; x += 32)
            if (x > donor->left + w && free(x, y)) return {x, y};
    return {((maxRight + 63) / 32) * 32, donor->top};
}

Result installLevel(const Request& req) {
    Result result;
    if (!validStem(req.newLevelName))
        throw std::runtime_error("worldinstall: level name must be letters, digits or '_' (got '" + req.newLevelName + "')");
    if (req.worldX % 32 != 0 || req.worldY % 32 != 0)
        throw std::runtime_error("worldinstall: origin must be on the 32-unit terrain grid (retail maps all are; off-grid patches do not rebuild exactly)");
    if (req.donorLevelName.empty()) throw std::runtime_error("worldinstall: a donor level is required");

    const fs::path levelsDir = req.gameRoot / "data" / "Levels";
    const fs::path bwdPath = levelsDir / "FinalAlbion.bwd";
    const fs::path wldPath = levelsDir / "FinalAlbion.wld";
    const fs::path wadPath = levelsDir / "FinalAlbion.wad";
    const fs::path stbPath = levelsDir / "FinalAlbion_RT.stb";
    for (const auto& p : {bwdPath, wldPath, wadPath, stbPath})
        if (!fs::exists(p)) throw std::runtime_error("worldinstall: " + p.string() + " not found");

    const std::string donorLev = "Data\\Levels\\FinalAlbion\\" + req.donorLevelName + ".lev";
    const std::string donorTng = "Data\\Levels\\FinalAlbion\\" + req.donorLevelName + ".tng";
    const std::string newLev = "Data\\Levels\\FinalAlbion\\" + req.newLevelName + ".lev";
    const std::string newTng = "Data\\Levels\\FinalAlbion\\" + req.newLevelName + ".tng";
    const std::string wldLevel = "FinalAlbion\\" + req.newLevelName + ".lev";

    // ---- donor dimensions + chunk from the STB
    int donorW = 0, donorH = 0;
    std::vector<uint8_t> donorChunk, commonRecord;
    {
        const auto stb = stb::Archive::open(stbPath);
        const stb::StaticMap* sm = nullptr;
        for (const auto& m : stb.staticMaps())
            if (lowered(m.levelName) == lowered(donorLev)) { sm = &m; break; }
        if (!sm) throw std::runtime_error("worldinstall: donor '" + req.donorLevelName + "' has no static map in FinalAlbion_RT.stb");
        for (const auto& m : stb.staticMaps())
            if (lowered(m.levelName) == lowered(newLev))
                throw std::runtime_error("worldinstall: '" + req.newLevelName + "' already has a static map");
        commonRecord = req.commonRecord.empty() ? stb.readStaticMapRecord(*sm) : req.commonRecord;
        if (commonRecord.size() < stbinfo::kInfoBlockSize) throw std::runtime_error("worldinstall: common record too small");
        if (!req.commonRecord.empty() && req.chunkBytes.empty()) throw std::runtime_error("worldinstall: a custom common record needs its chunk");
        const auto ib = stbinfo::readInfoBlock(commonRecord.data());
        donorW = ib.mapWidth; donorH = ib.mapHeight;
        const stb::Entry* chunkEntry = stb.findEntry(donorLev);
        if (!chunkEntry) throw std::runtime_error("worldinstall: donor chunk entry missing from the STB");
        if (req.chunkBytes.empty()) donorChunk = stb.read(*chunkEntry);
    }
    if (donorW <= 0 || donorH <= 0) throw std::runtime_error("worldinstall: donor has no map size");
    const int right = req.worldX + donorW, bottom = req.worldY + donorH;
    result.left = req.worldX; result.top = req.worldY; result.right = right; result.bottom = bottom;

    // ---- world containers (in memory)
    bwd::File bwd = bwd::File::parse(bwdPath);
    wld::File wld = wld::File::parse(wldPath);
    if (bwd.findMap(req.newLevelName) || wld.findMap(wldLevel))
        throw std::runtime_error("worldinstall: a map named '" + req.newLevelName + "' already exists in the world");
    {
        const auto archive = wad::Archive::open(wadPath);
        for (const auto& e : archive.entries())
            if (lowered(e.name) == lowered(newLev) || lowered(e.name) == lowered(newTng))
                throw std::runtime_error("worldinstall: FinalAlbion.wad already has " + e.name);
        bool lev = false, tng = false;
        for (const auto& e : archive.entries()) { lev = lev || lowered(e.name) == lowered(donorLev); tng = tng || lowered(e.name) == lowered(donorTng); }
        if (!lev || !tng) throw std::runtime_error("worldinstall: the donor's .lev/.tng are not both in FinalAlbion.wad");
    }
    if (!req.allowOverlap)
        for (const auto& b : mapBoxes(bwd))
            if (overlaps(b, req.worldX, req.worldY, right, bottom))
                throw std::runtime_error("worldinstall: the new box (" + std::to_string(req.worldX) + "," + std::to_string(req.worldY) + ")-(" +
                                         std::to_string(right) + "," + std::to_string(bottom) + ") overlaps map '" + b.name + "'");

    const uint64_t uid = [&]() { uint64_t m = 0; for (const auto& x : bwd.maps()) m = std::max(m, x.mapUid); return m + 1; }();
    result.mapUid = uid;
    if (!req.hostRegion.empty()) {
        bwd::Region* host = bwd.findRegion(req.hostRegion);
        const wld::Region* wr = wld.findRegion(req.hostRegion);
        if (!host || !wr) throw std::runtime_error("worldinstall: host region '" + req.hostRegion + "' not found in the BWD/WLD");
        int hostSlot = 0;
        for (size_t i = 0; i < bwd.regions().size(); ++i) if (&bwd.regions()[i] == host) hostSlot = int(i + 1);
        if (hostSlot > 141) result.notes.push_back("host region slot " + std::to_string(hostSlot) + " is past the engine's 141-region cap; the map may be unreachable");
        bwd::MapInfo m;
        m.levelName = newLev; m.scriptName = req.newLevelName;
        m.used = 1; m.loadedOnProximity = req.loadedOnProximity ? 1 : 0; m.isSea = req.isSea ? 1 : 0;
        m.left = req.worldX; m.top = req.worldY; m.right = right; m.bottom = bottom;
        m.flag2 = 1; m.mapUid = uid;
        result.mapSlot = bwd.addMap(std::move(m));
        host = bwd.findRegion(req.hostRegion);   // maps() growth does not move regions, but be safe
        if (std::find(host->contains.begin(), host->contains.end(), result.mapSlot) == host->contains.end()) host->contains.push_back(result.mapSlot);
        if (std::find(host->sees.begin(), host->sees.end(), result.mapSlot) == host->sees.end()) host->sees.push_back(result.mapSlot);
        wld::Map wm;
        wm.index = result.mapSlot; wm.mapX = req.worldX; wm.mapY = req.worldY;
        wm.levelName = wldLevel; wm.levelScriptName = req.newLevelName;
        wm.mapUid = static_cast<uint32_t>(uid); wm.isSea = req.isSea; wm.loadedOnPlayerProximity = req.loadedOnProximity;
        wld.addMap(wm);
        wld.addMapToRegion(req.hostRegion, wldLevel, true);
        result.notes.push_back("map slot " + std::to_string(result.mapSlot) + " owned by region '" + req.hostRegion + "' (slot " + std::to_string(hostSlot) + ")");
    } else {
        bwd::NewLevel spec;
        spec.levelName = req.newLevelName;
        spec.left = req.worldX; spec.top = req.worldY; spec.right = right; spec.bottom = bottom;
        spec.loadedOnProximity = req.loadedOnProximity; spec.isSea = req.isSea;
        spec.regionName = req.regionName; spec.regionDisplayName = req.regionDisplayName; spec.regionDef = req.regionDef;
        spec.mapUid = uid;
        const bwd::AssignedSlots slots = bwd.addLevel(spec);
        result.mapSlot = slots.mapSlot; result.regionSlot = slots.regionSlot;
        wld::Map wm;
        wm.index = slots.mapSlot; wm.mapX = req.worldX; wm.mapY = req.worldY;
        wm.levelName = wldLevel; wm.levelScriptName = req.newLevelName;
        wm.mapUid = static_cast<uint32_t>(uid); wm.isSea = req.isSea; wm.loadedOnPlayerProximity = req.loadedOnProximity;
        wld.addMap(wm);
        wld::Region wr;
        wr.index = slots.regionSlot;
        wr.regionName = req.regionName.empty() ? req.newLevelName : req.regionName;
        wr.displayName = req.regionDisplayName.empty() ? wr.regionName : req.regionDisplayName;
        wr.regionDef = req.regionDef;
        wr.containsMaps = {wldLevel}; wr.seesMaps = {wldLevel};
        wld.addRegion(wr);
        result.notes.push_back("map slot " + std::to_string(slots.mapSlot) + ", dedicated region slot " + std::to_string(slots.regionSlot));
        if (slots.regionSlot > 141) result.notes.push_back("region slot " + std::to_string(slots.regionSlot) + " is past the engine's 141-region cap: the level will not be reachable; attach it to a host region instead");
    }

    // ---- staged writes beside the originals (same volume: the commit renames are atomic)
    const fs::path bwdTmp = bwdPath.string() + ".forge-tmp";
    const fs::path wldTmp = wldPath.string() + ".forge-tmp";
    const fs::path wadTmp = wadPath.string() + ".forge-tmp";
    const fs::path wadTmp2 = wadPath.string() + ".forge-tmp2";
    const fs::path stbTmp = stbPath.string() + ".forge-tmp";
    auto cleanup = [&]() { std::error_code ec; for (const auto& t : {bwdTmp, wldTmp, wadTmp, wadTmp2, stbTmp}) fs::remove(t, ec); };
    fs::path wadFinal;
    try {
        bwd.write(bwdTmp);
        {
            const std::string text = wld.serialize();
            std::ofstream out(wldTmp, std::ios::binary);
            if (!out) throw std::runtime_error("cannot write " + wldTmp.string());
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!out) throw std::runtime_error("write failed for " + wldTmp.string());
        }
        wad::appendClonedEntries(wadPath, {{donorLev, newLev}, {donorTng, newTng}}, wadTmp);
        std::map<std::string, std::vector<uint8_t>> repl;
        if (!req.levBytes.empty()) repl[newLev] = req.levBytes;
        if (!req.tngBytes.empty()) repl[newTng] = req.tngBytes;
        if (!repl.empty()) { wad::repack(wadTmp, repl, wadTmp2); wadFinal = wadTmp2; }
        else wadFinal = wadTmp;

        auto ib = stbinfo::readInfoBlock(commonRecord.data());
        ib.worldX = req.worldX; ib.worldY = req.worldY;
        ib.cameraMapBounds[0] = static_cast<float>(req.worldX);
        ib.cameraMapBounds[1] = static_cast<float>(req.worldY);
        ib.cameraMapBounds[3] = static_cast<float>(right);
        ib.cameraMapBounds[4] = static_cast<float>(bottom);
        const auto patched = stbinfo::writeInfoBlock(ib);
        std::vector<uint8_t> record = commonRecord;
        std::copy(patched.begin(), patched.end(), record.begin());
        result.chunkRetargeted = !req.chunkBytes.empty();
        stb::appendStaticMap(stbPath, stbTmp, newLev, newLev, result.chunkRetargeted ? req.chunkBytes : donorChunk, record);
    } catch (const std::exception& e) {
        cleanup();
        throw std::runtime_error(std::string("worldinstall: staging failed, install untouched: ") + e.what());
    }

    // ---- commit
    try {
        if (!req.backupSuffix.empty())
            for (const auto& p : {bwdPath, wldPath, wadPath, stbPath}) {
                const fs::path bak = p.string() + req.backupSuffix;
                std::error_code ec;
                if (!fs::exists(bak)) fs::copy_file(p, bak, ec);
            }
        auto commit = [](const fs::path& tmp, const fs::path& orig) {
            std::error_code ec;
            fs::rename(tmp, orig, ec);
            if (ec) {
                fs::remove(orig, ec);
                fs::rename(tmp, orig, ec);
                if (ec) throw std::runtime_error("commit rename failed for " + orig.string() + ": " + ec.message());
            }
        };
        commit(bwdTmp, bwdPath);
        commit(wldTmp, wldPath);
        commit(wadFinal, wadPath);
        commit(stbTmp, stbPath);
        cleanup();
    } catch (const std::exception& e) {
        cleanup();
        throw std::runtime_error(std::string("worldinstall: commit failed after staging (restore from the ") + req.backupSuffix + " files): " + e.what());
    }
    result.notes.push_back("WAD: cloned " + req.donorLevelName + ".lev/.tng as " + req.newLevelName + (req.levBytes.empty() && req.tngBytes.empty() ? " (donor bytes)" : " (custom bytes)"));
    result.notes.push_back(std::string("STB: chunk appended ") + (!req.commonRecord.empty() ? "(authored from scratch)" : result.chunkRetargeted ? "(re-baked for the new origin)" : "(DONOR geometry: re-bake it for the new origin before playing)"));
    return result;
}

} // namespace forge::worldinstall
