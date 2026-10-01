#include <cstdio>
#include <cstring>
#include <cmath>
#include "forge/worldinstall.hpp"

#include "forge/bwd.hpp"
#include "forge/levelstore.hpp"
#include "forge/stb.hpp"
#include "forge/stbinfo.hpp"
#include "forge/wad.hpp"
#include "forge/wld.hpp"
#include "forge/temporarydirectory.hpp"

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

bool iequalsName(const std::string& a, const std::string& b) { return lowered(a) == lowered(b); }

std::vector<Box> mapBoxes(const bwd::File& bwd) {
    std::vector<Box> boxes;
    for (const auto& m : bwd.maps()) {
        if (!m.used) continue;
        boxes.push_back({m.left, m.top, m.right, m.bottom, m.scriptName});
    }
    return boxes;
}

} // namespace

void validatePlacement(int x, int y, int64_t width, int64_t height) {
    if (width <= 0 || height <= 0 || width > kWorldExtent || height > kWorldExtent)
        throw std::runtime_error("worldinstall: map dimensions must fit the world grid (1..8192)");
    if (x % 32 || y % 32)
        throw std::runtime_error("worldinstall: origin must be on the 32-unit terrain grid");
    if (x < 0 || y < 0 || int64_t(x) + width > kWorldExtent || int64_t(y) + height > kWorldExtent)
        throw std::runtime_error("worldinstall: map box is outside the engine's world grid (0..8192)");
}

Origin suggestOrigin(const fs::path& gameRoot, const std::string& donorLevelName) {
    const fs::path levelsDir = gameRoot / "data" / "Levels";
    const auto bwd = bwd::File::parse(levelsDir / "FinalAlbion.bwd");
    const bwd::MapInfo* donor = bwd.findMap(donorLevelName);
    if (!donor) throw std::runtime_error("worldinstall: donor '" + donorLevelName + "' is not in FinalAlbion.bwd");
    const int64_t wideW = int64_t(donor->right) - donor->left, wideH = int64_t(donor->bottom) - donor->top;
    validatePlacement(0, 0, wideW, wideH);
    const int w = int(wideW), h = int(wideH);
    const auto boxes = mapBoxes(bwd);
    int maxRight = 0, maxBottom = 0;
    for (const auto& b : boxes) { maxRight = std::max(maxRight, b.right); maxBottom = std::max(maxBottom, b.bottom); }
    auto free = [&](int x, int y) {
        for (const auto& b : boxes) if (overlaps(b, x, y, x + w, y + h)) return false;
        return true;
    };
    // Preserve the preferred rightward row-band search, bounded by the engine grid.
    const int64_t firstX = std::max<int64_t>(0, ((int64_t(donor->left) + w) / 32 + 1) * 32);
    const int64_t firstY = std::max<int64_t>(0, ((int64_t(donor->top) + 31) / 32) * 32);
    const int64_t lastX = std::min<int64_t>(kWorldExtent - w, int64_t(maxRight) + 2 * w);
    const int64_t lastY = std::min<int64_t>(kWorldExtent - h, int64_t(maxBottom) + h);
    for (int64_t y = firstY; y <= lastY; y += 32)
        for (int64_t x = firstX; x <= lastX; x += 32)
            if (free(int(x), int(y))) return {int(x), int(y)};
    // A donor at the right/bottom edge may still have room elsewhere in the grid.
    for (int y = 0; y <= kWorldExtent - h; y += 32)
        for (int x = 0; x <= kWorldExtent - w; x += 32)
            if (free(x, y)) return {x, y};
    throw std::runtime_error("worldinstall: no free 32-aligned placement inside the world grid");
}

static Result installLevelImpl(const Request& req, bool validationOnly) {
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
    // A loose-level install (no FinalAlbion.wad, levels extracted to FinalAlbion\)
    // gets the new level as loose files; creating a WAD there would override
    // every loose level the install relies on.
    const auto layout = levelstore::detect(req.gameRoot);
    if (!layout.valid()) throw std::runtime_error("worldinstall: " + layout.describe());
    const bool loose = layout.looseOnly();
    for (const auto& p : {bwdPath, wldPath, stbPath})
        if (!fs::exists(p)) throw std::runtime_error("worldinstall: " + p.string() + " not found");
    const fs::path looseLev = levelstore::loosePath(layout, req.newLevelName + ".lev");
    const fs::path looseTng = levelstore::loosePath(layout, req.newLevelName + ".tng");

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
    validatePlacement(req.worldX, req.worldY, donorW, donorH);
    const int right = req.worldX + donorW, bottom = req.worldY + donorH;
    result.left = req.worldX; result.top = req.worldY; result.right = right; result.bottom = bottom;

    // ---- world containers (in memory)
    bwd::File bwd = bwd::File::parse(bwdPath);
    wld::File wld = wld::File::parse(wldPath);
    if (bwd.findMap(req.newLevelName) || wld.findMap(wldLevel))
        throw std::runtime_error("worldinstall: a map named '" + req.newLevelName + "' already exists in the world");
    if (loose) {
        for (const auto& p : {looseLev, looseTng})
            if (fs::exists(p)) throw std::runtime_error("worldinstall: " + p.string() + " already exists");
        for (const char* ext : {".lev", ".tng"})
            if (!fs::exists(levelstore::loosePath(layout, req.donorLevelName + ext)))
                throw std::runtime_error("worldinstall: the donor's " + req.donorLevelName + ext + " is not in " + layout.looseDir.string());
    } else {
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
    if (!req.takeOverRegion.empty()) {
        bwd::Region* victim = bwd.findRegion(req.takeOverRegion);
        const wld::Region* wv = wld.findRegion(req.takeOverRegion);
        if (!victim || !wv) throw std::runtime_error("worldinstall: region '" + req.takeOverRegion + "' not found in the BWD/WLD");
        if (req.mergeMapsInto.empty() && !victim->contains.empty())
            throw std::runtime_error("worldinstall: region '" + req.takeOverRegion + "' owns maps; name a region to merge them into");
        bwd::Region* sink = req.mergeMapsInto.empty() ? nullptr : bwd.findRegion(req.mergeMapsInto);
        const wld::Region* ws = req.mergeMapsInto.empty() ? nullptr : wld.findRegion(req.mergeMapsInto);
        if (!req.mergeMapsInto.empty() && (!sink || !ws)) throw std::runtime_error("worldinstall: merge region '" + req.mergeMapsInto + "' not found");
        if (sink == victim) throw std::runtime_error("worldinstall: cannot merge a region into itself");
        const std::string newName = req.regionName.empty() ? req.newLevelName : req.regionName;
        if (!iequalsName(newName, req.takeOverRegion) && (bwd.findRegion(newName) || wld.findRegion(newName)))
            throw std::runtime_error("worldinstall: a region named '" + newName + "' already exists");
        int slot = 0;
        for (size_t i = 0; i < bwd.regions().size(); ++i) if (&bwd.regions()[i] == victim) slot = int(i + 1);
        // the victim's maps move to the sink (ownership only; sees lists elsewhere stay)
        std::vector<std::string> moved;
        for (int32_t m : victim->contains) {
            if (sink && std::find(sink->contains.begin(), sink->contains.end(), m) == sink->contains.end()) sink->contains.push_back(m);
            if (m >= 1 && size_t(m) <= bwd.maps().size()) {
                const std::string lv = "FinalAlbion\\" + bwd.maps()[size_t(m - 1)].scriptName + ".lev";
                if (wld.findMap(lv)) {
                    wld.removeMapFromRegion(req.takeOverRegion, lv, true);
                    if (ws) wld.addMapToRegion(req.mergeMapsInto, lv, false);
                    moved.push_back(bwd.maps()[size_t(m - 1)].scriptName);
                }
            }
        }
        victim->contains.clear(); victim->sees.clear();
        // repurpose in place
        bwd::MapInfo m;
        m.levelName = newLev; m.scriptName = req.newLevelName;
        m.used = 1; m.loadedOnProximity = req.loadedOnProximity ? 1 : 0; m.isSea = req.isSea ? 1 : 0;
        m.left = req.worldX; m.top = req.worldY; m.right = right; m.bottom = bottom;
        m.flag2 = 1; m.mapUid = uid;
        result.mapSlot = bwd.addMap(std::move(m));
        victim = bwd.findRegion(req.takeOverRegion);
        victim->name = newName;
        victim->displayName = req.regionDisplayName.empty() ? newName : req.regionDisplayName;
        victim->regionDef = req.regionDef;
        victim->minimapGraphic = req.minimapGraphic;
        victim->contains = {result.mapSlot};
        victim->sees = {result.mapSlot};
        wld::Map wm;
        wm.index = result.mapSlot; wm.mapX = req.worldX; wm.mapY = req.worldY;
        wm.levelName = wldLevel; wm.levelScriptName = req.newLevelName;
        wm.mapUid = static_cast<uint32_t>(uid); wm.isSea = req.isSea; wm.loadedOnPlayerProximity = req.loadedOnProximity;
        wld.addMap(wm);
        wld.setRegionText(req.takeOverRegion, "NewDisplayName", victim->displayName);
        wld.setRegionText(req.takeOverRegion, "RegionDef", req.regionDef);
        wld.setRegionText(req.takeOverRegion, "MiniMapGraphic", req.minimapGraphic);
        if (req.minimapFraming) {
            const auto& f = *req.minimapFraming;
            std::memcpy(victim->minimapScale, &f.scale, 4);
            victim->mmOffX = static_cast<int32_t>(std::lround(f.offsetX));
            victim->mmOffY = static_cast<int32_t>(std::lround(f.offsetY));
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.1f", double(f.scale)); wld.setRegionText(req.takeOverRegion, "MiniMapScale", buf);
            std::snprintf(buf, sizeof buf, "%ld.0", std::lround(f.offsetX)); wld.setRegionText(req.takeOverRegion, "MiniMapOffsetX", buf);
            std::snprintf(buf, sizeof buf, "%ld.0", std::lround(f.offsetY)); wld.setRegionText(req.takeOverRegion, "MiniMapOffsetY", buf);
        }
        wld.setRegionText(req.takeOverRegion, "RegionName", newName);   // last: the name is the key
        wld.addMapToRegion(newName, wldLevel, true);
        result.regionSlot = slot;
        result.notes.push_back("map slot " + std::to_string(result.mapSlot) + " owned by region '" + newName + "' (slot " + std::to_string(slot) + ", taken over from '" + req.takeOverRegion + "'" +
                               (moved.empty() ? "" : ", its " + std::to_string(moved.size()) + " map(s) now owned by '" + req.mergeMapsInto + "'") + ")" +
                               (req.minimapGraphic.empty() ? "" : ", minimap " + req.minimapGraphic));
    } else if (!req.hostRegion.empty()) {
        bwd::Region* host = bwd.findRegion(req.hostRegion);
        const wld::Region* wr = wld.findRegion(req.hostRegion);
        if (!host || !wr) throw std::runtime_error("worldinstall: host region '" + req.hostRegion + "' not found in the BWD/WLD");
        int hostSlot = 0;
        for (size_t i = 0; i < bwd.regions().size(); ++i) if (&bwd.regions()[i] == host) hostSlot = int(i + 1);
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
        result.notes.push_back("new dedicated regions need a new game or a save made after installation to display correctly");
    }

    if (validationOnly) return result;

    // ---- prepare all replacements inside an exclusively owned directory.
    TemporaryDirectory scratch(req.gameRoot, ".forge-world-install-");
    struct PreparedFile { fs::path target, prepared, previous; bool saved=false, installed=false; };
    std::vector<PreparedFile> files;
    const auto root = fs::absolute(req.gameRoot).lexically_normal();
    const auto prepare = [&](const fs::path& target) {
        const auto relative = fs::absolute(target).lexically_normal().lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("worldinstall: target escaped install root: " + target.string());
        PreparedFile file{target, scratch.path() / "new" / relative, scratch.path() / "previous" / relative};
        fs::create_directories(file.prepared.parent_path());
        fs::create_directories(file.previous.parent_path());
        files.push_back(file);
        return file.prepared;
    };
    const fs::path bwdTmp = prepare(bwdPath);
    std::vector<fs::path> mirrors;
    for (const fs::path mirror : {req.gameRoot / "FinalAlbion.bwd", levelsDir / "FinalAlbion" / "FinalAlbion.bwd"})
        if (fs::exists(mirror)) mirrors.push_back(mirror);
    const fs::path wldTmp = prepare(wldPath);
    const fs::path wadTmp = scratch.path() / "cloned.wad";
    const fs::path wadTmp2 = loose ? fs::path{} : prepare(wadPath);
    const fs::path levTmp = loose ? prepare(looseLev) : fs::path{};
    const fs::path tngTmp = loose ? prepare(looseTng) : fs::path{};
    // Mirrors follow the main world files, with the STB last so a late failure
    // exercises rollback across both new loose files and all BWD copies.
    std::vector<fs::path> mirrorTemps;
    for (const auto& mirror : mirrors) mirrorTemps.push_back(prepare(mirror));
    const fs::path stbTmp = prepare(stbPath);
    try {
        const auto binary = bwd.serialize();
        {
            std::ofstream out(bwdTmp, std::ios::binary);
            out.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
            out.close();
            if (!out || bwd::File::parse(bwdTmp).serialize() != binary)
                throw std::runtime_error("prepared BWD failed read-back verification");
        }
        for (const auto& mirrorTmp : mirrorTemps) fs::copy_file(bwdTmp, mirrorTmp);
        {
            const std::string text = wld.serialize();
            std::ofstream out(wldTmp, std::ios::binary);
            if (!out) throw std::runtime_error("cannot write " + wldTmp.string());
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.close();
            if (!out || wld::File::parse(wldTmp).serialize() != text)
                throw std::runtime_error("prepared WLD failed read-back verification");
        }
        if (loose) {
            auto writeLoose = [&](const fs::path& tmp, const std::vector<uint8_t>& custom, const char* ext) {
                const std::vector<uint8_t> bytes = custom.empty() ? levelstore::requireFile(layout, req.donorLevelName + ext) : custom;
                std::ofstream out(tmp, std::ios::binary);
                if (!out) throw std::runtime_error("cannot write " + tmp.string());
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                out.close();
                if (!out) throw std::runtime_error("write failed for " + tmp.string());
            };
            writeLoose(levTmp, req.levBytes, ".lev");
            writeLoose(tngTmp, req.tngBytes, ".tng");
        } else {
            wad::appendClonedEntries(wadPath, {{donorLev, newLev}, {donorTng, newTng}}, wadTmp);
            std::map<std::string, std::vector<uint8_t>> repl;
            if (!req.levBytes.empty()) repl[newLev] = req.levBytes;
            if (!req.tngBytes.empty()) repl[newTng] = req.tngBytes;
            if (!repl.empty()) wad::repack(wadTmp, repl, wadTmp2);
            else fs::rename(wadTmp, wadTmp2);
        }

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
        const auto verifiedStb = stb::Archive::open(stbTmp);
        const auto* savedChunk = verifiedStb.findEntry(newLev);
        if (!savedChunk || verifiedStb.read(*savedChunk) != (result.chunkRetargeted ? req.chunkBytes : donorChunk))
            throw std::runtime_error("prepared STB chunk failed read-back verification");
        if (!loose) {
            const auto verifiedWad = wad::Archive::open(wadTmp2);
            for (const auto& item : {std::pair{newLev, &req.levBytes}, std::pair{newTng, &req.tngBytes}}) {
                const auto found = std::find_if(verifiedWad.entries().begin(), verifiedWad.entries().end(),
                    [&](const wad::Entry& entry) { return iequalsName(entry.name, item.first); });
                if (found == verifiedWad.entries().end() || (!item.second->empty() && verifiedWad.read(*found) != *item.second))
                    throw std::runtime_error("prepared WAD entry failed read-back verification: " + item.first);
            }
        }
        for (const auto& file : files) {
            if (!fs::is_regular_file(file.prepared)) throw std::runtime_error("prepared file missing: " + file.prepared.string());
            if (fs::exists(file.target) && !fs::is_regular_file(file.target))
                throw std::runtime_error("destination is not a file: " + file.target.string());
            if (!req.backupSuffix.empty() && fs::exists(file.target)) {
                const fs::path backup = file.target.string() + req.backupSuffix;
                if (fs::exists(backup)) {
                    if (!fs::is_regular_file(backup)) throw std::runtime_error("backup is not a file: " + backup.string());
                } else fs::copy_file(file.target, backup);
            }
        }
        if (loose && req.prepareCreatedFile) {
            req.prepareCreatedFile(looseLev);
            req.prepareCreatedFile(looseTng);
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("worldinstall: preparation failed, target files untouched: ") + e.what());
    }

    // ---- commit, keeping exact previous files available until the group succeeds.
    try {
        for (auto& file : files) {
            if (fs::exists(file.target)) { fs::rename(file.target, file.previous); file.saved=true; }
            fs::rename(file.prepared, file.target); file.installed=true;
        }
    } catch (const std::exception& e) {
        std::string error = std::string("worldinstall: commit failed: ") + e.what();
        bool failedRollback=false;
        for (auto it=files.rbegin(); it!=files.rend(); ++it) {
            try {
                if (it->installed) fs::rename(it->target, it->prepared);
                if (it->saved) fs::rename(it->previous, it->target);
            } catch (const std::exception& restore) {
                failedRollback=true;
                error += "; rollback failed: " + std::string(restore.what());
            }
        }
        if (failedRollback) { scratch.retain(); error += "; recovery files retained in " + scratch.path().string(); }
        else error += "; target files rolled back";
        throw std::runtime_error(error);
    }
    for (const auto& mirror : mirrors) result.notes.push_back("BWD mirrored to " + mirror.string());
    result.notes.push_back(std::string(loose ? "loose FinalAlbion\\ files (no FinalAlbion.wad in this install)" : "WAD") + ": cloned " + req.donorLevelName + ".lev/.tng as " + req.newLevelName + (req.levBytes.empty() && req.tngBytes.empty() ? " (donor bytes)" : " (custom bytes)"));
    if (loose) { result.createdFiles.push_back(looseLev); result.createdFiles.push_back(looseTng); }
    result.notes.push_back(std::string("STB: chunk appended ") + (!req.commonRecord.empty() ? "(authored from scratch)" : result.chunkRetargeted ? "(re-baked for the new origin)" : "(DONOR geometry: re-bake it for the new origin before playing)"));
    return result;
}

void validateRequest(const Request& request) { (void)installLevelImpl(request, true); }
Result installLevel(const Request& request) { return installLevelImpl(request, false); }

} // namespace forge::worldinstall
