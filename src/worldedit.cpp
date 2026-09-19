#include "worldedit.hpp"
#include "gtg.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

#include "forge/big.hpp"
#include "forge/bwd.hpp"
#include "forge/lev.hpp"
#include "forge/stb.hpp"
#include "forge/stbbake.hpp"
#include "forge/stbheightbake.hpp"
#include "forge/navmesh.hpp"
#include "forge/terrain.hpp"
#include "forge/terraintex.hpp"
#include "forge/themepalette.hpp"
#include "forge/stbinfo.hpp"
#include "forge/wad.hpp"
#include "forge/wld.hpp"
#include "forge/worldinstall.hpp"
#include "terrainexport.hpp"
#include "forge/bin.hpp"
#include "forge/defedit.hpp"
#include "forge/defschema.hpp"
#include "../vendor/embedded_schema.hpp"
#include "overworld.hpp"
#include "stbrelocate.hpp"
#include "lodbake.hpp"

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

bool applyOwnRegion(const fs::path& gameRoot, const OwnRegion& own, const std::string& levelName, const std::string& hostRegion,
                    forge::worldinstall::Request& ir, std::string& error);
bool finishDedicatedRegion(const fs::path& gameRoot, const OwnRegion& own, const std::string& levelName, const std::string& minimap,
                           std::vector<std::string>& notes, std::string& error);
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

// The region entrance a new own-region level gets by default: the map centre, on the
// ground, facing north. The map screen / quest teleports drop the hero there.
bool defaultEntrance(const fs::path& gameRoot, int slot, const std::string& levelName, const std::vector<uint8_t>& levBytes,
                     std::vector<std::string>& notes, std::string& error) {
    if (!fs::exists(gameRoot / "data" / "Levels" / "FinalAlbion.gtg")) { notes.push_back("no FinalAlbion.gtg in this tree: region entrance skipped (set it later from the Level tab)"); return true; }
    try {
        const fs::path tmp = fs::temp_directory_path() / "FableForge" / "entrance";
        fs::create_directories(tmp);
        const fs::path levTmp = tmp / (levelName + ".lev");
        std::ofstream(levTmp, std::ios::binary).write(reinterpret_cast<const char*>(levBytes.data()), std::streamsize(levBytes.size()));
        const auto lev = forge::lev::File::open(levTmp);
        const int cx = lev.cellsX() / 2, cy = lev.cellsY() / 2;
        const float pos[3] = {float(cx), float(cy), lev.heightAt(cx, cy)};
        const float fwd[2] = {0.0f, 1.0f};
        return setRegionEntrance(gameRoot, slot, levelName, pos, fwd, notes, error);
    } catch (const std::exception& e) { error = std::string("region entrance: ") + e.what(); return false; }
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
        if (!applyOwnRegion(gameRoot, req.ownRegion, req.name, req.hostRegion, ir, error)) return false;
        ir.backupSuffix.clear();   // FableForge keeps its own .atlas-orig copies
        const auto stage = [&](const std::string& s) { if (req.progress) req.progress(s); };
        stage("reading the donor level");

        // the donor's current bytes (loose edits included) become the new level's
        const auto wad = forge::wad::Archive::open(wadPath);
        ir.levBytes = levelBytes(gameRoot, wad, req.donor, ".lev");
        ir.tngBytes = levelBytes(gameRoot, wad, req.donor, ".tng");
        if (req.ownRegion.wanted && req.ownRegion.minimap) {
            std::string entry;
            stage("baking the minimap into textures.big");
            if (!bakeMinimapTexture(gameRoot, req.name, ir.levBytes, nullptr, entry, out.notes, error)) return false;
            ir.minimapGraphic = entry;
        }

        if (req.rebakeChunk) {
            stage("translating the terrain chunk");
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
            // every coordinate in the chunk is absolute (vertices, edge strips,
            // water, LOD tree, foliage): translate the whole thing to the new
            // origin, the same path a world move takes. The donor's edited loose
            // heights, if any, are already in this chunk only when the donor was
            // deployed; a plain copy carries the donor's shipped terrain.
            auto chunk = archive.read(*entry);
            auto rec = record;
            const auto info = forge::stbinfo::readInfoBlock(record.data());
            RelocateReport rr;
            if (!relocateChunk(chunk, rec, req.worldX - info.worldX, req.worldY - info.worldY, rr, error)) { error = "terrain chunk: " + error; return false; }
            for (const auto& n : rr.notes) out.notes.push_back("chunk: " + n);
            ir.chunkBytes = std::move(chunk);
            ir.commonRecord = std::move(rec);
            out.notes.push_back("terrain chunk translated from (" + std::to_string(info.worldX) + "," + std::to_string(info.worldY) + ") to (" + std::to_string(req.worldX) + "," + std::to_string(req.worldY) + "): " +
                                std::to_string(rr.foregroundFrames) + " foreground, " + std::to_string(rr.patchFrames) + " patches, " + std::to_string(rr.groupFrames) + " foliage groups, " + std::to_string(ir.chunkBytes.size()) + " bytes");
        }

        for (const char* f : {"FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion.wad", "FinalAlbion_RT.stb"})
            if (!backupOnce(levels / f, error)) return false;
        for (const fs::path mirror : {gameRoot / "FinalAlbion.bwd", levels / "FinalAlbion" / "FinalAlbion.bwd"})
            if (fs::exists(mirror) && !backupOnce(mirror, error)) return false;
        stage("installing: FinalAlbion.wad / .wld / .bwd / _RT.stb");
        const auto r = forge::worldinstall::installLevel(ir);
        out.mapSlot = r.mapSlot;
        out.worldX = r.left; out.worldY = r.top; out.width = r.right - r.left; out.height = r.bottom - r.top;
        for (const auto& n : r.notes) if (n.find("141-region cap") == std::string::npos) out.notes.push_back(n);
        stage("registering the region");
        if (!finishDedicatedRegion(gameRoot, req.ownRegion, req.name, ir.minimapGraphic, out.notes, error)) return false;
        if (req.ownRegion.wanted && !defaultEntrance(gameRoot, r.mapSlot, req.name, ir.levBytes, out.notes, error)) return false;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

std::vector<MapSize> retailMapSizes(const fs::path& gameRoot, std::string& error) {
    std::vector<MapSize> out;
    try {
        const auto bwd = forge::bwd::File::parse(gameRoot / "data" / "Levels" / "FinalAlbion.bwd");
        const auto wld = forge::wld::File::parse(gameRoot / "data" / "Levels" / "FinalAlbion.wld");
        for (const auto& m : bwd.maps()) {
            if (!m.used) continue;
            if (!wld.findMap("FinalAlbion\\" + m.scriptName + ".lev")) continue;   // a template must be placed in the WLD too
            const int w = m.right - m.left, h = m.bottom - m.top;
            if (w < 16 || h < 16 || w % 16 || h % 16) continue;
            auto it = std::find_if(out.begin(), out.end(), [&](const MapSize& s) { return s.width == w && s.height == h; });
            if (it == out.end()) out.push_back({w, h, m.scriptName, 1});
            else ++it->count;
        }
        std::sort(out.begin(), out.end(), [](const MapSize& a, const MapSize& b) { return a.width * a.height != b.width * b.height ? a.width * a.height < b.width * b.height : a.width < b.width; });
    } catch (const std::exception& e) { error = e.what(); }
    return out;
}

std::vector<ReusableRegion> reusableRegions(const fs::path& gameRoot, std::string& error) {
    std::vector<ReusableRegion> out;
    try {
        const auto bwd = forge::bwd::File::parse(gameRoot / "data" / "Levels" / "FinalAlbion.bwd");
        const auto wld = forge::wld::File::parse(gameRoot / "data" / "Levels" / "FinalAlbion.wld");
        for (size_t i = 0; i < bwd.regions().size() && i < 141; ++i) {
            const auto& r = bwd.regions()[i];
            if (r.name.rfind("Filler", 0) != 0 || !r.regionDef.empty() || !r.minimapGraphic.empty()) continue;
            if (!wld.findRegion(r.name)) continue;
            out.push_back({int(i + 1), r.name, int(r.contains.size())});
        }
        std::sort(out.begin(), out.end(), [](const ReusableRegion& a, const ReusableRegion& b) { return a.maps != b.maps ? a.maps < b.maps : a.slot < b.slot; });
    } catch (const std::exception& e) { error = e.what(); }
    return out;
}

namespace {
// Fill the worldinstall request's region fields from the FableForge request.
// A dedicated region: forgecore's install writes name/display/def but not the
// minimap graphic; set it (WLD + the three BWD copies) and say what the region
// needs from the player.
bool finishDedicatedRegion(const fs::path& gameRoot, const OwnRegion& own, const std::string& levelName, const std::string& minimap,
                           std::vector<std::string>& notes, std::string& error) {
    if (!own.wanted || !own.dedicated) return true;
    if (!minimap.empty()) {
        RegionProps props; props.minimapGraphic = minimap;
        if (!setRegionProperties(gameRoot, levelName, props, notes, error)) return false;
    }
    notes.push_back("region " + levelName + " is a new slot: saves cache the region table, so start a new game (or make a save after this) to see it named and drawn");
    return true;
}

bool applyOwnRegion(const fs::path& gameRoot, const OwnRegion& own, const std::string& levelName, const std::string& hostRegion,
                    forge::worldinstall::Request& ir, std::string& error) {
    ir.hostRegion = hostRegion;
    if (!own.wanted) return true;
    if (own.dedicated) {
        ir.hostRegion.clear();
        ir.regionName = levelName;
        ir.regionDisplayName = own.displayName.empty() ? levelName : own.displayName;
        ir.regionDef = own.regionDef;
        return true;
    }
    std::string rerr;
    const auto regions = reusableRegions(gameRoot, rerr);
    if (regions.size() < 2) { error = "no filler region slots left to take over (" + rerr + ")"; return false; }
    ir.takeOverRegion = own.takeOver.empty() ? regions.front().name : own.takeOver;
    ir.mergeMapsInto = own.mergeInto;
    if (ir.mergeMapsInto.empty()) for (auto it = regions.rbegin(); it != regions.rend(); ++it) if (it->name != ir.takeOverRegion) { ir.mergeMapsInto = it->name; break; }
    if (ir.mergeMapsInto == ir.takeOverRegion) { error = "the merge region must differ from the one taken over"; return false; }
    ir.hostRegion.clear();
    ir.regionName = levelName;
    ir.regionDisplayName = own.displayName.empty() ? levelName : own.displayName;
    ir.regionDef = own.regionDef;
    return true;
}
} // namespace

bool bakeMinimapTexture(const fs::path& gameRoot, const std::string& levelName, const std::vector<uint8_t>& levBytes,
                        const forge::terraintex::ThemeLibrary* library, std::string& entryName,
                        std::vector<std::string>& notes, std::string& error) {
    try {
        (void)library;
        const fs::path tmp = fs::temp_directory_path() / "FableForge" / "minimap";
        fs::create_directories(tmp);
        const fs::path levTmp = tmp / (levelName + ".lev");
        std::ofstream(levTmp, std::ios::binary).write(reinterpret_cast<const char*>(levBytes.data()), std::streamsize(levBytes.size()));
        const auto lev = forge::lev::File::open(levTmp);
        // top-down albedo from the LEV themes (no STB needed), lit by a simple hillshade
        albion::terrainexport::Context ctx;
        std::string cerr;
        const bool textured = ctx.load(gameRoot, gameRoot / "data" / "graphics" / "pc" / "textures.big", cerr);
        albion::terrainexport::Options o;
        o.textures = textured; o.texelsPerCell = 4; o.gain = 2.0f; o.engineLayers = false; o.gameRoot = gameRoot;
        const auto scene = albion::terrainexport::buildScene(lev, o, textured ? &ctx : nullptr);
        const int size = 256;
        albion::terrainexport::Image img;
        img.width = img.height = uint32_t(size);
        img.rgba.assign(size_t(size) * size * 4, 0);
        const int cx = lev.cellsX(), cy = lev.cellsY();
        auto heightAt = [&](float fx, float fy) {
            const int x0 = std::clamp(int(fx), 0, cx - 1), y0 = std::clamp(int(fy), 0, cy - 1);
            return lev.heightAt(x0, y0);
        };
        for (int py = 0; py < size; ++py)
            for (int px = 0; px < size; ++px) {
                // texture row 0 is the map's north edge (max Y); the box is stretched onto the square like retail
                const float u = (px + 0.5f) / size, v = 1.0f - (py + 0.5f) / size;
                const float mx = u * lev.width(), my = v * lev.height();
                float r = 120, g = 130, b = 80;
                if (scene.hasAlbedo && scene.albedo.width && scene.albedo.height) {
                    const uint32_t ax = std::min(uint32_t(u * scene.albedo.width), scene.albedo.width - 1);
                    const uint32_t ay = std::min(uint32_t(v * scene.albedo.height), scene.albedo.height - 1);
                    const uint8_t* p = &scene.albedo.rgba[(size_t(ay) * scene.albedo.width + ax) * 4];
                    r = p[0]; g = p[1]; b = p[2];
                }
                // hillshade from the LEV (light from the north-west)
                const float d = 2.0f;
                const float hx = heightAt(mx + d, my) - heightAt(mx - d, my);
                const float hy = heightAt(mx, my + d) - heightAt(mx, my - d);
                const float shade = std::clamp(1.0f + 0.06f * (-hx + hy), 0.55f, 1.35f);
                // the retail vignette: an opaque disc that fades out at the corners
                const float dx = u - 0.5f, dy = v - 0.5f;
                const float rad = std::sqrt(dx * dx + dy * dy);
                const float alpha = std::clamp((0.5f - rad) / 0.06f, 0.0f, 1.0f);
                uint8_t* q = &img.rgba[(size_t(py) * size + px) * 4];
                q[0] = uint8_t(std::clamp(r * shade, 0.0f, 255.0f));
                q[1] = uint8_t(std::clamp(g * shade, 0.0f, 255.0f));
                q[2] = uint8_t(std::clamp(b * shade, 0.0f, 255.0f));
                q[3] = uint8_t(alpha * 255.0f);
            }
        const fs::path png = tmp / (levelName + "_minimap.png");
        {
            const auto bytes = albion::terrainexport::encodePng(img);
            std::ofstream(png, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        }
        // Retail resolves a region's MiniMapGraphic through PLAYER_GUI.MiniMapGraphics
        // (name -> GBANK_MAIN_PC id), so a new texture is appended under its own
        // name and registered there; nothing retail is touched. (A 256x256 mip 0
        // needs the 0xFFFF+u32 chunk header the importer now writes for >= 64 KiB
        // raw chunks; with the short header the engine dropped the texture.)
        const fs::path big = gameRoot / "data" / "graphics" / "pc" / "textures.big";
        std::string upper = levelName;
        for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (entryName.empty()) entryName = "MINIMAP_" + upper;
        bool exists = false;
        {
            const auto tex = forge::big::File::open(big);
            const auto* bank = tex.findBank("GBANK_MAIN_PC");
            if (!bank) { error = "textures.big has no GBANK_MAIN_PC"; return false; }
            for (const auto& e : bank->entries) exists = exists || e.name == entryName;
        }
        if (!backupOnce(big, error)) return false;
        forge::terraintex::ImportRequest ir;
        ir.png = png; ir.srcBig = big; ir.outBig = big.string() + ".atlas-tmp";
        ir.entryName = entryName; ir.subBank = "GBANK_MAIN_PC"; ir.format = "dxt3"; ir.add = !exists;
        const auto r = forge::terraintex::importPng(ir);
        if (!r.ok) { error = "minimap texture import failed: " + r.output + " (" + r.command + ")"; std::error_code ec; fs::remove(ir.outBig, ec); return false; }
        fs::rename(ir.outBig, big);
        notes.push_back(std::string("minimap: ") + (exists ? "replaced " : "appended ") + entryName + " (id " + std::to_string(r.entryId) + ", 256x256 DXT3) in textures.big from " + png.string());
        if (!registerMinimapGraphic(gameRoot, entryName, uint32_t(r.entryId), notes, error)) return false;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool createCustomTheme(const fs::path& gameRoot, const CustomThemeRequest& req, CustomThemeResult& out, std::string& error) {
    out = CustomThemeResult{};
    if (req.name.empty() || req.name.size() >= 100) { error = "theme name missing or too long"; return false; }
    for (char c : req.name)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) { error = "theme name must be A-Z, 0-9 and _ (got '" + req.name + "')"; return false; }
    if (!fs::exists(req.png)) { error = "no such PNG: " + req.png.string(); return false; }
    if (!req.cliffPng.empty() && !fs::exists(req.cliffPng)) { error = "no such PNG: " + req.cliffPng.string(); return false; }
    try {
        const fs::path defsDir = gameRoot / "data" / "CompiledDefs";
        const fs::path namesBin = defsDir / "names.bin", gameBin = defsDir / "game.bin";
        const fs::path big = gameRoot / "data" / "graphics" / "pc" / "textures.big";
        if (!fs::exists(big)) { error = "no " + big.string(); return false; }
        auto file = forge::bin::File::open(namesBin, gameBin);
        if (file.find(req.name)) { error = "game.bin already has a def named " + req.name; return false; }
        const auto* donor = file.find(req.donor);
        if (!donor) { error = "no ENGINE_THEME named " + req.donor + " to copy from"; return false; }
        if (donor->definition != "ENGINE_THEME") { error = req.donor + " is a " + donor->definition + ", not an ENGINE_THEME"; return false; }
        // 1. the texture(s): appended GBANK_MAIN_PC entries (the layer mesh
        //    references the global id; retail chunks resolve ids directly)
        auto addTexture = [&](const fs::path& png, const std::string& symbol, uint32_t& id) {
            {
                const auto tex = forge::big::File::open(big);
                const auto* bank = tex.findBank("GBANK_MAIN_PC");
                if (!bank) { error = "textures.big has no GBANK_MAIN_PC"; return false; }
                for (const auto& e : bank->entries) if (e.name == symbol) { error = "textures.big already has an entry named " + symbol; return false; }
            }
            if (!backupOnce(big, error)) return false;
            forge::terraintex::ImportRequest ir;
            ir.png = png; ir.srcBig = big; ir.outBig = big.string() + ".atlas-tmp";
            ir.entryName = symbol; ir.subBank = "GBANK_MAIN_PC"; ir.format = "dxt1"; ir.add = true;
            const auto r = forge::terraintex::importPng(ir);
            if (!r.ok) { error = "texture import failed: " + r.output + " (" + r.command + ")"; std::error_code ec; fs::remove(ir.outBig, ec); return false; }
            fs::rename(ir.outBig, big);
            id = uint32_t(r.entryId);
            out.notes.push_back("textures.big: appended " + symbol + " (id " + std::to_string(id) + ", DXT1) from " + png.string());
            return true;
        };
        if (!addTexture(req.png, req.name + "_BASE", out.baseTexture)) return false;
        out.cliffTexture = out.baseTexture;
        if (!req.cliffPng.empty() && !addTexture(req.cliffPng, req.name + "_CLIFF", out.cliffTexture)) return false;
        // 2. the ENGINE_THEME def: the donor's bytes with the texture fields repointed
        const size_t index = file.addEntry("ENGINE_THEME", req.name, donor->data);
        const auto schema = forge::defschema::Schema::loadText(kEmbeddedDefSchema, "embedded");
        auto set = [&](const char* field, uint32_t value) {
            try { forge::defedit::setField(file, schema, req.name, field, std::to_string(value)); }
            catch (const std::exception& e) { error = std::string("game.bin: could not set ") + field + ": " + e.what(); return false; }
            return true;
        };
        if (!set("BaseTexture", out.baseTexture) || !set("BackgroundTexture", out.baseTexture) ||
            !set("CliffBaseTexture", out.cliffTexture) || !set("CliffBackgroundTexture", out.cliffTexture) ||
            !set("BaseBumpMap", 0) || !set("CliffBumpMap", 0)) return false;
        if (!backupOnce(namesBin, error) || !backupOnce(gameBin, error)) return false;
        file.save(namesBin, gameBin);
        out.defIndex = uint32_t(index);
        out.notes.push_back("game.bin: appended ENGINE_THEME " + req.name + " (def index " + std::to_string(index) + ", a copy of " + req.donor + " with textures " + std::to_string(out.baseTexture) + "/" + std::to_string(out.cliffTexture) + ")");
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool registerMinimapGraphic(const fs::path& gameRoot, const std::string& name, uint32_t id,
                            std::vector<std::string>& notes, std::string& error) {
    try {
        const fs::path defs = gameRoot / "data" / "CompiledDefs";
        const fs::path namesBin = defs / "names.bin", gameBin = defs / "game.bin";
        auto file = forge::bin::File::open(namesBin, gameBin);
        const auto schema = forge::defschema::Schema::loadText(kEmbeddedDefSchema, "embedded");
        int edited = 0;
        for (const char* entryName : {"PLAYER_GUI_PC", "PLAYER_GUI_DEFAULT"}) {
            if (!file.find(entryName)) continue;
            // Map_JVCCharString__: [u32 count] then count x [NUL-terminated name][u32 id]
            std::vector<uint8_t> raw = forge::defedit::getFieldBytes(file, schema, entryName, "MiniMapGraphics");
            if (raw.size() < 4) { error = std::string(entryName) + ".MiniMapGraphics is too short"; return false; }
            uint32_t count = 0; std::memcpy(&count, raw.data(), 4);
            std::vector<std::pair<std::string, uint32_t>> entries;
            size_t p = 4;
            for (uint32_t i = 0; i < count; ++i) {
                const size_t z = std::find(raw.begin() + std::ptrdiff_t(p), raw.end(), uint8_t(0)) - raw.begin();
                if (z + 5 > raw.size()) { error = std::string(entryName) + ".MiniMapGraphics is malformed"; return false; }
                std::string key(raw.begin() + std::ptrdiff_t(p), raw.begin() + std::ptrdiff_t(z));
                uint32_t value = 0; std::memcpy(&value, raw.data() + z + 1, 4);
                entries.emplace_back(std::move(key), value);
                p = z + 5;
            }
            if (p != raw.size()) { error = std::string(entryName) + ".MiniMapGraphics has trailing bytes"; return false; }
            bool replaced = false;
            for (auto& e : entries) if (e.first == name) { e.second = id; replaced = true; }
            if (!replaced) entries.emplace_back(name, id);
            // the engine's std::map<CCharString, long> serialises in key order; keep it sorted
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<uint8_t> out;
            const uint32_t n = uint32_t(entries.size());
            out.insert(out.end(), reinterpret_cast<const uint8_t*>(&n), reinterpret_cast<const uint8_t*>(&n) + 4);
            for (const auto& e : entries) {
                out.insert(out.end(), e.first.begin(), e.first.end());
                out.push_back(0);
                out.insert(out.end(), reinterpret_cast<const uint8_t*>(&e.second), reinterpret_cast<const uint8_t*>(&e.second) + 4);
            }
            forge::defedit::setFieldBytes(file, schema, entryName, "MiniMapGraphics", std::move(out));
            notes.push_back(std::string(entryName) + ".MiniMapGraphics: " + (replaced ? "updated " : "added ") + name + " -> " + std::to_string(id) + " (" + std::to_string(entries.size()) + " entries)");
            ++edited;
        }
        if (!edited) { error = "game.bin has no PLAYER_GUI_PC / PLAYER_GUI_DEFAULT entry"; return false; }
        if (!backupOnce(namesBin, error) || !backupOnce(gameBin, error)) return false;
        file.save(namesBin, gameBin);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool templatePalette(const fs::path& gameRoot, const std::string& level, std::vector<std::string>& names, std::string& error) {
    try {
        const auto wad = forge::wad::Archive::open(gameRoot / "data" / "Levels" / "FinalAlbion.wad");
        const auto bytes = levelBytes(gameRoot, wad, level, ".lev");
        const fs::path tmp = fs::temp_directory_path() / "FableForge" / "newlevel";
        fs::create_directories(tmp);
        const fs::path levTmp = tmp / (level + ".palette.lev");
        std::ofstream(levTmp, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        const auto lev = forge::lev::File::open(levTmp);
        names.assign(256, "");
        for (size_t i = 0; i < lev.groundThemes().size() && i < 256; ++i) names[i] = lev.groundThemes()[i].name;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool createBlankLevel(const fs::path& gameRoot, const BlankLevelRequest& req,
                      const forge::terraintex::ThemeLibrary& library, NewLevelResult& out, std::string& error) {
    try {
        const fs::path levels = gameRoot / "data" / "Levels";
        if (req.width < 16 || req.height < 16 || req.width % 16 || req.height % 16) { error = "level sides must be multiples of 16"; return false; }
        std::string templateLevel = req.templateLevel;
        if (templateLevel.empty()) {
            // a retail LEV of exactly this size supplies the header/palette skeleton
            std::string serr;
            for (const auto& s : retailMapSizes(gameRoot, serr))
                if (s.width == req.width && s.height == req.height) { templateLevel = s.templateLevel; break; }
            if (templateLevel.empty()) { error = "no retail map is " + std::to_string(req.width) + "x" + std::to_string(req.height) + " (a template LEV of that size is needed)"; return false; }
        }
        const auto stage = [&](const std::string& s) { if (req.progress) req.progress(s); };
        stage("authoring the level from the " + templateLevel + " skeleton");
        const auto wad = forge::wad::Archive::open(levels / "FinalAlbion.wad");
        const auto templateBytes = levelBytes(gameRoot, wad, templateLevel, ".lev");
        const fs::path tmp = fs::temp_directory_path() / "FableForge" / "newlevel";
        fs::create_directories(tmp);
        const fs::path levTmp = tmp / (req.name + ".lev");
        std::ofstream(levTmp, std::ios::binary).write(reinterpret_cast<const char*>(templateBytes.data()), std::streamsize(templateBytes.size()));
        auto lev = forge::lev::File::open(levTmp);
        if (lev.width() != req.width || lev.height() != req.height) { error = "template " + templateLevel + " is " + std::to_string(lev.width()) + "x" + std::to_string(lev.height()) + ", not " + std::to_string(req.width) + "x" + std::to_string(req.height); return false; }
        out.notes.push_back("template " + templateLevel + " (" + std::to_string(lev.width()) + "x" + std::to_string(lev.height()) + ")");

        // ground theme: the requested slot, else the template's most used
        int slot = req.themeSlot;
        if (slot < 0) {
            std::vector<size_t> use(256, 0);
            for (int y = 0; y < lev.cellsY(); ++y)
                for (int x = 0; x < lev.cellsX(); ++x) use[lev.themeIndexAt(x, y, lev.dominantLayerAt(x, y))]++;
            slot = int(std::max_element(use.begin(), use.end()) - use.begin());
        }
        if (slot < 0 || slot > 255 || lev.groundThemes()[size_t(slot)].name.empty()) { error = "palette slot " + std::to_string(slot) + " has no ground theme"; return false; }
        for (int y = 0; y < lev.cellsY(); ++y)
            for (int x = 0; x < lev.cellsX(); ++x) {
                lev.setHeightAt(x, y, req.groundHeight);
                lev.setWalkableAt(x, y, true);
                lev.setPreferredPathAt(x, y, false);
                lev.setThemeBlendAt(x, y, {uint8_t(slot), uint8_t(slot), uint8_t(slot)}, {255, 0, 0});
            }
        // the template's palette carries the def indices of whatever game.bin it
        // was authored against (retail ones are stale); a new level is rebased to
        // this install by name so the theme materials resolve
        {
            const auto defs = forge::themepalette::openDefBank(gameRoot);
            const auto table = forge::themepalette::makeDefIndexTable(defs);
            const size_t rebased = lev.rebaseThemePalette(table);
            if (rebased) out.notes.push_back("palette: " + std::to_string(rebased) + " theme slot(s) rebased to this install's game.bin");
        }
        lev.save(levTmp);
        stage("building the navigation tree");
        // navigation: a fresh single-layer tree over the (all walkable) cells
        {
            const auto saved = forge::lev::File::open(levTmp);
            const auto nav = forge::navmesh::generateTerrain(saved);
            std::ofstream(levTmp, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(nav.levBytes.data()), std::streamsize(nav.levBytes.size()));
            out.notes.push_back("navigation: " + std::to_string(nav.navigableLeaves) + " leaves over " + std::to_string(nav.walkableCells) + " walkable cells");
        }
        lev = forge::lev::File::open(levTmp);

        stage("baking the terrain chunk");
        // terrain chunk from scratch: LEV heights + palette materials, a solid distant-LOD colour
        const auto heightfield = forge::terrain::Heightfield::fromLev(lev);
        auto themeMaterials = forge::terraintex::paletteMaterials(lev, library);
        themeMaterials.resize(256);
        if (!themeMaterials[size_t(slot)].available) { error = "ground theme " + lev.groundThemes()[size_t(slot)].name + " has no textures in this install"; return false; }
        forge::stbbake::InlineTexture background;
        background.width = background.height = 4; background.levels = 1; background.pixelFormat0 = 3;
        background.mipData.assign(8, 0);
        background.mipData[0] = background.mipData[2] = uint8_t(req.backgroundRgb565);
        background.mipData[1] = background.mipData[3] = uint8_t(req.backgroundRgb565 >> 8);
        forge::terrain::TerrainMaterialTuple material = themeMaterials[size_t(slot)].base;
        // distant-LOD textures: the level's own top-down albedo per background
        // node (64x64 DXT1 like retail) instead of the solid colour, so a later
        // theme paint can re-bake them in place
        const auto lodAlbedo = bakeLodAlbedo(gameRoot, lev);
        const auto lodProvider = lodTextureProvider(lodAlbedo);
        const auto built = forge::stbbake::buildTerrainChunk64(
            heightfield, req.worldX, req.worldY, 0, material, {}, background, 2048, {}, {}, false, {}, true,
            themeMaterials,
            [&](int x, int y) {
                forge::terrain::ThemeBlend blend;
                for (int k = 0; k < 3; ++k) { blend.indices[size_t(k)] = lev.themeIndexAt(x, y, k); blend.strengths[size_t(k)] = lev.themeStrengthAt(x, y, k); }
                return blend;
            }, lodProvider);
        const auto record = forge::stbbake::buildTerrainCommonRecord(built.info, built.chunk);
        out.notes.push_back("terrain chunk authored from scratch: " + std::to_string(built.chunk.size()) + " bytes, theme " + lev.groundThemes()[size_t(slot)].name);

        forge::worldinstall::Request ir;
        ir.gameRoot = gameRoot;
        ir.donorLevelName = templateLevel;   // supplies the WAD clone base and the STB name; every payload is ours
        ir.newLevelName = req.name;
        ir.worldX = req.worldX; ir.worldY = req.worldY;
        if (!applyOwnRegion(gameRoot, req.ownRegion, req.name, req.hostRegion, ir, error)) return false;
        ir.backupSuffix.clear();
        ir.levBytes = readFile(levTmp);
        if (req.ownRegion.wanted && req.ownRegion.minimap) {
            std::string entry;
            if (!bakeMinimapTexture(gameRoot, req.name, ir.levBytes, &library, entry, out.notes, error)) return false;
            ir.minimapGraphic = entry;
        }
        const std::string tng = "Version 2;\r\nXXXSectionStart NULL;\r\nXXXSectionEnd;\r\n";
        ir.tngBytes.assign(tng.begin(), tng.end());
        ir.chunkBytes = built.chunk;
        ir.commonRecord = record;
        for (const char* f : {"FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion.wad", "FinalAlbion_RT.stb"})
            if (!backupOnce(levels / f, error)) return false;
        for (const fs::path mirror : {gameRoot / "FinalAlbion.bwd", levels / "FinalAlbion" / "FinalAlbion.bwd"})
            if (fs::exists(mirror) && !backupOnce(mirror, error)) return false;
        stage("installing: FinalAlbion.wad / .wld / .bwd / _RT.stb");
        const auto r = forge::worldinstall::installLevel(ir);
        out.mapSlot = r.mapSlot;
        out.worldX = r.left; out.worldY = r.top; out.width = r.right - r.left; out.height = r.bottom - r.top;
        for (const auto& n : r.notes) if (n.find("141-region cap") == std::string::npos) out.notes.push_back(n);
        if (!finishDedicatedRegion(gameRoot, req.ownRegion, req.name, ir.minimapGraphic, out.notes, error)) return false;
        if (req.ownRegion.wanted && !defaultEntrance(gameRoot, r.mapSlot, req.name, ir.levBytes, out.notes, error)) return false;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

} // namespace albion::editor
