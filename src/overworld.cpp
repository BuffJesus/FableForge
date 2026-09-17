#include "overworld.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

#include "forge/bwd.hpp"
#include "forge/lev.hpp"
#include "forge/stb.hpp"
#include "forge/stbbake.hpp"
#include "forge/stbheightbake.hpp"
#include "forge/stbinfo.hpp"
#include "forge/wad.hpp"
#include "forge/wld.hpp"
#include "stbrelocate.hpp"

namespace albion::editor {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string stemOf(const std::string& levelName) {
    return fs::path(levelName).stem().string();
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

void writeFile(const fs::path& p, const void* data, size_t size) {
    std::ofstream out(p, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + p.string());
    out.write(static_cast<const char*>(data), std::streamsize(size));
    if (!out) throw std::runtime_error("write to " + p.string() + " failed");
}

// A map's LEV as the game loads it (loose wins over the WAD), opened through
// a temp copy because lev::File is path based.
std::unique_ptr<forge::lev::File> openLev(const fs::path& gameRoot, forge::wad::Archive& wad, const std::string& stem) {
    fs::path levFile = gameRoot / "data" / "Levels" / "FinalAlbion" / (stem + ".lev");
    if (!fs::exists(levFile)) {
        const std::string want = lower(stem + ".lev");
        levFile.clear();
        for (const auto& e : wad.entries())
            if (lower(fs::path(e.name).filename().string()) == want) {
                const auto bytes = wad.read(e);
                const fs::path tmp = fs::temp_directory_path() / "Albion Atlas" / "overworld";
                fs::create_directories(tmp);
                levFile = tmp / (stem + ".lev");
                writeFile(levFile, bytes.data(), bytes.size());
                break;
            }
        if (levFile.empty()) throw std::runtime_error(stem + ".lev is neither loose nor in FinalAlbion.wad");
    }
    return std::make_unique<forge::lev::File>(forge::lev::File::open(levFile));
}

bool boxesTouch(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    // adjacent (sharing an edge or corner) or overlapping
    return bx <= ax + aw && ax <= bx + bw && by <= ay + ah && ay <= by + bh;
}
bool boxesOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return bx < ax + aw && ax < bx + bw && by < ay + ah && ay < by + bh;
}

} // namespace

bool WorldRegion::sees_(const std::string& stem) const {
    const std::string w = lower(stem);
    for (const auto& m : sees) if (lower(m) == w) return true;
    return false;
}
bool WorldRegion::owns(const std::string& stem) const {
    const std::string w = lower(stem);
    for (const auto& m : contains) if (lower(m) == w) return true;
    return false;
}
const WorldRegion* WorldLayout::region(const std::string& name) const {
    const std::string w = lower(name);
    for (const auto& r : regionInfo) if (lower(r.name) == w) return &r;
    return nullptr;
}

const WorldMapBox* WorldLayout::find(const std::string& name) const {
    const std::string want = lower(name);
    for (const auto& m : maps) if (lower(m.name) == want) return &m;
    return nullptr;
}

std::vector<const WorldMapBox*> WorldLayout::touching(const WorldMapBox& box, int x, int y) const {
    std::vector<const WorldMapBox*> out;
    for (const auto& m : maps) {
        if (&m == &box || m.slot == box.slot) continue;
        if (boxesTouch(x, y, box.w, box.h, m.x, m.y, m.w, m.h)) out.push_back(&m);
    }
    return out;
}

bool loadWorldLayout(const fs::path& gameRoot, WorldLayout& out, std::string& error) {
    try {
        out = WorldLayout{};
        const fs::path levels = gameRoot / "data" / "Levels";
        const auto bwd = forge::bwd::File::parse(levels / "FinalAlbion.bwd");
        const auto wld = forge::wld::File::parse(levels / "FinalAlbion.wld");
        std::map<std::string, std::pair<int, int>> stbOrigins;   // lower stem -> baked origin
        if (fs::exists(levels / "FinalAlbion_RT.stb")) {
            const auto stb = forge::stb::Archive::open(levels / "FinalAlbion_RT.stb");
            for (const auto& m : stb.staticMaps()) {
                const auto record = stb.readStaticMapRecord(m);
                if (record.size() < forge::stbinfo::kInfoBlockSize) continue;
                const auto info = forge::stbinfo::readInfoBlock(record.data());
                stbOrigins[lower(stemOf(m.levelName))] = {info.worldX, info.worldY};
            }
        }
        for (const auto& r : wld.regions()) {
            out.regions.push_back(r.regionName);
            WorldRegion info; info.slot = int(out.regions.size()); info.name = r.regionName;
            for (const auto& n : r.containsMaps) info.contains.push_back(stemOf(n));
            for (const auto& n : r.seesMaps) info.sees.push_back(stemOf(n));
            out.regionInfo.push_back(std::move(info));
        }
        int slot = 0;
        for (const auto& m : bwd.maps()) {
            ++slot;
            if (!m.used) continue;
            WorldMapBox b;
            b.slot = slot;
            // key on the LEV stem: ten retail maps carry a different script name
            // (BowerstoneSlumsWarehouses.lev is scripted as "BowerstoneSlums" ...)
            b.name = stemOf(m.levelName);
            b.scriptName = m.scriptName;
            b.x = m.left; b.y = m.top; b.w = m.right - m.left; b.h = m.bottom - m.top;
            b.isSea = m.isSea != 0; b.loadedOnProximity = m.loadedOnProximity != 0;
            const std::string key = lower(b.name);
            for (const auto& wm : wld.maps())
                if (lower(stemOf(wm.levelName)) == key) {
                    b.levelName = wm.levelName;
                    b.x = wm.mapX; b.y = wm.mapY;   // the text placement is the one the editor writes
                    break;
                }
            if (b.levelName.empty()) b.levelName = "FinalAlbion\\" + b.name + ".lev";
            for (size_t ri = 0; ri < wld.regions().size() && b.region.empty(); ++ri)
                for (const auto& n : wld.regions()[ri].containsMaps)
                    if (lower(stemOf(n)) == key) { b.region = wld.regions()[ri].regionName; b.regionSlot = int(ri) + 1; break; }
            if (auto it = stbOrigins.find(key); it != stbOrigins.end()) { b.inStb = true; b.stbX = it->second.first; b.stbY = it->second.second; }
            out.maps.push_back(std::move(b));
        }
        if (out.maps.empty()) { error = "no maps in FinalAlbion.bwd"; return false; }
        out.minX = out.minY = 1 << 30; out.maxX = out.maxY = -(1 << 30);
        for (const auto& b : out.maps) {
            out.minX = std::min(out.minX, b.x); out.minY = std::min(out.minY, b.y);
            out.maxX = std::max(out.maxX, b.x + b.w); out.maxY = std::max(out.maxY, b.y + b.h);
        }
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

// Add or drop one `SeesMap "<level>";` line in a region block of the WLD text.
// Line-precise: the file is otherwise untouched (its own EOL kept).
std::string editSeesLine(const std::string& text, const forge::wld::File& wld, const std::string& regionName,
                         const std::string& levelName, bool sees) {
    const forge::wld::Region* region = wld.findRegion(regionName);
    if (!region) throw std::runtime_error("unknown region " + regionName);
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    size_t p = 0;
    while (p < text.size()) {
        size_t q = text.find('\n', p);
        if (q == std::string::npos) q = text.size() - 1;
        lines.push_back(text.substr(p, q - p + 1));
        p = q + 1;
    }
    auto trimmed = [](const std::string& l) {
        size_t a = l.find_first_not_of(" \t\r\n"), b = l.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : l.substr(a, b - a + 1);
    };
    const std::string wantLine = "SeesMap \"" + levelName + "\";";
    int active = -1;
    size_t lastSees = SIZE_MAX;   // retail keeps the SeesMap lines together, last in the block
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trimmed(lines[i]);
        if (t.rfind("NewRegion", 0) == 0) { active = std::atoi(t.c_str() + 9); continue; }
        if (active != region->index) continue;
        if (lower(t) == lower(wantLine)) {
            if (!sees) lines.erase(lines.begin() + std::ptrdiff_t(i));
            break;   // present: dropped, or nothing to add
        }
        if (t.rfind("SeesMap", 0) == 0) lastSees = i;
        if (t == "EndRegion;") {
            if (sees) lines.insert(lines.begin() + std::ptrdiff_t(lastSees == SIZE_MAX ? i : lastSees + 1), wantLine + eol);
            break;
        }
    }
    std::string out;
    for (const auto& l : lines) out += l;
    return out;
}

bool checkMove(const WorldLayout& layout, const std::vector<MapMove>& moves, const MapMove& move, std::string& why) {
    const WorldMapBox* box = layout.find(move.name);
    if (!box) { why = move.name + " is not in the world"; return false; }
    if (move.x % 32 || move.y % 32) { why = "origin must be 32-aligned"; return false; }
    // the engine's placement grid is (0,0)-(8192,8192): CWorld::Init 0x4a6e30
    // constructs CWorldMap with that box and SetMapPlacement 0x4fc9c0 writes
    // the slot into a 32-unit cell grid with no bounds check (a map at y=9024
    // crashed the region transition)
    if (move.x < 0 || move.y < 0 || move.x + box->w > kWorldExtent || move.y + box->h > kWorldExtent) { why = "outside the engine's world grid (0..8192)"; return false; }
    std::set<int> movedSlots;
    for (const auto& mv : moves) if (const auto* b = layout.find(mv.name)) movedSlots.insert(b->slot);
    movedSlots.insert(box->slot);
    for (const auto& m : layout.maps) {
        if (movedSlots.count(m.slot)) continue;
        if (boxesOverlap(move.x, move.y, box->w, box->h, m.x, m.y, m.w, m.h)) { why = "overlaps " + m.name; return false; }
    }
    // moved boxes against each other at their new places
    for (const auto& mv : moves) {
        const auto* b = layout.find(mv.name);
        if (!b || b->slot == box->slot) continue;
        if (boxesOverlap(move.x, move.y, box->w, box->h, mv.x, mv.y, b->w, b->h)) { why = "overlaps " + b->name + " (also moved)"; return false; }
    }
    return true;
}

bool applyWorldEdits(const fs::path& gameRoot, const std::vector<MapMove>& moves,
                     const std::vector<OwnerEdit>& owners, const std::vector<SeesEdit>& seesEdits,
                     std::vector<std::string>& notes, std::string& error) {
    try {
        if (moves.empty() && owners.empty() && seesEdits.empty()) { error = "nothing to do"; return false; }
        WorldLayout before;
        if (!loadWorldLayout(gameRoot, before, error)) return false;
        for (const auto& mv : moves) {
            std::string why;
            if (!checkMove(before, moves, mv, why)) { error = mv.name + ": " + why; return false; }
        }
        // the layout after the moves (what neighbours are judged against)
        WorldLayout after = before;
        std::map<int, MapMove> moveOf;   // slot -> move
        for (const auto& mv : moves) {
            for (auto& b : after.maps)
                if (lower(b.name) == lower(mv.name)) { b.x = mv.x; b.y = mv.y; moveOf[b.slot] = mv; }
        }
        // every map whose bake changes: the moved ones plus everything that touched
        // them before or touches them after (their shared-edge samples changed)
        std::set<int> rebake;
        for (const auto& [slot, mv] : moveOf) {
            rebake.insert(slot);
            const WorldMapBox* oldBox = nullptr; const WorldMapBox* newBox = nullptr;
            for (const auto& b : before.maps) if (b.slot == slot) oldBox = &b;
            for (const auto& b : after.maps) if (b.slot == slot) newBox = &b;
            if (oldBox->x == newBox->x && oldBox->y == newBox->y) continue;
            for (const auto* n : before.touching(*oldBox, oldBox->x, oldBox->y)) rebake.insert(n->slot);
            for (const auto* n : after.touching(*newBox, newBox->x, newBox->y)) rebake.insert(n->slot);
        }

        const fs::path levels = gameRoot / "data" / "Levels";
        const fs::path wldPath = levels / "FinalAlbion.wld", bwdPath = levels / "FinalAlbion.bwd";
        const fs::path stbPath = levels / "FinalAlbion_RT.stb", wadPath = levels / "FinalAlbion.wad";
        for (const fs::path& p : {wldPath, bwdPath, stbPath, wadPath}) if (!backupOnce(p, error)) return false;
        const fs::path mirrors[] = {gameRoot / "FinalAlbion.bwd", levels / "FinalAlbion" / "FinalAlbion.bwd"};
        for (const auto& m : mirrors) if (fs::exists(m) && !backupOnce(m, error)) return false;

        // 1. STB first (the slow part; nothing is written until it succeeds)
        std::vector<forge::stb::StaticMapAppend> batch;
        bool sameSize = true;
        if (fs::exists(stbPath)) {
            auto wad = forge::wad::Archive::open(wadPath);
            const auto archive = forge::stb::Archive::open(stbPath);
            auto wld = forge::wld::File::parse(wldPath);
            std::map<int, std::unique_ptr<forge::lev::File>> levs;
            auto levOf = [&](const WorldMapBox& b) -> forge::lev::File& {
                auto it = levs.find(b.slot);
                if (it == levs.end()) it = levs.emplace(b.slot, openLev(gameRoot, wad, b.name)).first;
                return *it->second;
            };
            for (int slot : rebake) {
                const WorldMapBox* box = nullptr;
                for (const auto& b : after.maps) if (b.slot == slot) box = &b;
                if (!box->inStb) { notes.push_back(box->name + ": no terrain chunk, placement only"); continue; }
                const forge::stb::StaticMap* map = nullptr;
                for (const auto& m : archive.staticMaps())
                    if (lower(stemOf(m.levelName)) == lower(box->name)) { map = &m; break; }
                const auto record = archive.readStaticMapRecord(*map);
                uint32_t bankIndex = 0; std::memcpy(&bankIndex, record.data() + 4, 4);
                const forge::stb::Entry* entry = nullptr;
                for (const auto& e : archive.entries()) if (e.id == bankIndex) { entry = &e; break; }
                if (!entry) { error = box->name + ": static-map bank entry " + std::to_string(bankIndex) + " not found"; return false; }
                const auto chunk = archive.read(*entry);
                forge::lev::File& lev = levOf(*box);
                if (lev.width() != box->w || lev.height() != box->h) {
                    error = box->name + ": LEV is " + std::to_string(lev.width()) + "x" + std::to_string(lev.height()) + " but the world box is " + std::to_string(box->w) + "x" + std::to_string(box->h);
                    return false;
                }
                // neighbours the retail bake would have used: maps owned or seen by
                // a region that owns this one, touching it at the new placement
                forge::stbbake::HeightfieldBakeOptions opt;
                opt.requireCanonicalSize = false;
                std::set<std::string> candidates;
                const std::string mine = lower(box->levelName);
                for (const auto& region : wld.regions()) {
                    bool owns = false;
                    for (const auto& n : region.containsMaps) owns = owns || lower(n) == mine;
                    if (!owns) continue;
                    for (const auto& n : region.containsMaps) candidates.insert(lower(stemOf(n)));
                    for (const auto& n : region.seesMaps) candidates.insert(lower(stemOf(n)));
                }
                candidates.erase(lower(box->name));
                std::string neighbourList;
                for (const auto& nb : after.maps) {
                    if (nb.slot == box->slot || !candidates.count(lower(nb.name))) continue;
                    if (!boxesTouch(box->x, box->y, box->w, box->h, nb.x, nb.y, nb.w, nb.h)) continue;
                    try {
                        forge::lev::File& nl = levOf(nb);
                        opt.neighbors.push_back({&nl, nb.x, nb.y});
                        neighbourList += (neighbourList.empty() ? "" : ", ") + nb.name;
                    } catch (const std::exception&) {}
                }
                const bool moved = moveOf.count(slot) > 0;
                std::vector<uint8_t> newChunk, newRecord = record;
                if (moved) {
                    // a moved map keeps its terrain: every absolute coordinate in the
                    // chunk (vertices, edge strips, water, LOD tree, foliage) and the
                    // record's foliage root are translated by the move
                    const WorldMapBox* was = nullptr;
                    for (const auto& b : before.maps) if (b.slot == slot) was = &b;
                    newChunk = chunk;
                    RelocateReport rr;
                    if (!relocateChunk(newChunk, newRecord, box->x - was->x, box->y - was->y, rr, error)) { error = box->name + ": " + error; return false; }
                    auto info = forge::stbinfo::readInfoBlock(newRecord.data());
                    info.worldX = box->x; info.worldY = box->y;
                    info.cameraMapBounds[0] = float(box->x); info.cameraMapBounds[1] = float(box->y);
                    info.cameraMapBounds[3] = float(box->x + box->w); info.cameraMapBounds[4] = float(box->y + box->h);
                    const auto header = forge::stbinfo::writeInfoBlock(info);
                    std::copy(header.begin(), header.end(), newRecord.begin());
                    for (const auto& n : rr.notes) notes.push_back(box->name + ": " + n);
                    notes.push_back("moved " + box->name + " -> (" + std::to_string(box->x) + "," + std::to_string(box->y) + "): chunk translated (" +
                                    std::to_string(rr.foregroundFrames) + " foreground, " + std::to_string(rr.patchFrames) + " patches, " + std::to_string(rr.groupFrames) + " foliage groups), " + std::to_string(newChunk.size()) + " bytes");
                } else {
                    // a neighbour only re-bakes for its shared-edge samples: when its
                    // chunk is one the baker cannot rebuild (some fillers carry a reduced
                    // frame set) it keeps the retail bake, which is what it had before
                    forge::stbbake::HeightfieldBakeResult baked;
                    try { baked = forge::stbbake::bakeHeightfield(chunk, lev, box->x, box->y, opt); }
                    catch (const std::exception& e) {
                        notes.push_back("neighbour " + box->name + " kept its retail bake (" + e.what() + ")");
                        continue;
                    }
                    newChunk = std::move(baked.chunk);
                    notes.push_back("neighbour " + box->name + ": chunk re-baked, " + std::to_string(newChunk.size()) + " bytes" + (neighbourList.empty() ? "" : ", edges from " + neighbourList));
                }
                if (newChunk.size() != chunk.size()) sameSize = false;
                batch.push_back({map->levelName, entry->name, newChunk, newRecord});
            }
        }

        // 2. WLD: placement, owners, visibility (line-precise edits)
        // 3. BWD: the on-disk records with the boxes patched for the moves and
        //    the contains/sees lists taken from the edited WLD (forgecore's
        //    compileFromWld resolves them to slots). Every other BWD field is
        //    kept from disk: an install can carry regions whose def/minimap in
        //    the BWD differ from the WLD text (FableForge wrote them that way),
        //    and the engine reads the BWD.
        {
            auto wld = forge::wld::File::parse(wldPath);
            auto bwd = forge::bwd::File::parse(bwdPath);
            const forge::bwd::DimSource dims = [&](const std::string& levelName, int& w, int& h) {
                for (const auto& b : before.maps)
                    if (lower(b.name) == lower(stemOf(levelName))) { w = b.w; h = b.h; return true; }
                return false;
            };
            if (!owners.empty() || !seesEdits.empty()) {
                // the WLD must describe this BWD's ownership before it is trusted to rewrite it
                forge::bwd::File check;
                try { check = forge::bwd::compileFromWld(wld, dims); }
                catch (const std::exception& e) { error = std::string("the WLD does not compile: ") + e.what(); return false; }
                if (check.regions().size() != bwd.regions().size() || check.maps().size() != bwd.maps().size()) { error = "FinalAlbion.wld and FinalAlbion.bwd disagree on the map/region count"; return false; }
                for (size_t i = 0; i < bwd.regions().size(); ++i)
                    if (check.regions()[i].contains != bwd.regions()[i].contains || check.regions()[i].sees != bwd.regions()[i].sees) {
                        error = "FinalAlbion.wld and FinalAlbion.bwd disagree on region " + bwd.regions()[i].name + "'s maps; region edits refused";
                        return false;
                    }
            }
            for (const auto& [slot, mv] : moveOf) {
                const WorldMapBox* box = nullptr;
                for (const auto& b : after.maps) if (b.slot == slot) box = &b;
                wld.relocateMap(box->levelName, mv.x, mv.y);
            }
            for (const auto& o : owners) {
                const WorldMapBox* box = before.find(o.map);
                if (!box) { error = "unknown map " + o.map; return false; }
                if (!wld.findRegion(o.region)) { error = "unknown region " + o.region; return false; }
                wld.setMapOwner(o.region, box->levelName);
                notes.push_back(o.map + " is now owned by " + o.region);
            }
            std::string text = wld.serialize();
            for (const auto& se : seesEdits) {
                const WorldMapBox* box = before.find(se.map);
                if (!box) { error = "unknown map " + se.map; return false; }
                text = editSeesLine(text, wld, se.region, box->levelName, se.sees);
                notes.push_back(se.region + (se.sees ? " now sees " : " no longer sees ") + se.map);
            }
            writeFile(wldPath, text.data(), text.size());
            for (const auto& [slot, mv] : moveOf) {
                auto& m = bwd.maps().at(size_t(slot - 1));
                const int w = m.right - m.left, h = m.bottom - m.top;
                m.left = mv.x; m.top = mv.y; m.right = mv.x + w; m.bottom = mv.y + h;
            }
            if (!owners.empty() || !seesEdits.empty()) {
                const auto edited = forge::wld::File::parse(wldPath);
                const auto compiled = forge::bwd::compileFromWld(edited, dims);
                for (size_t i = 0; i < bwd.regions().size(); ++i) {
                    bwd.regions()[i].contains = compiled.regions()[i].contains;
                    bwd.regions()[i].sees = compiled.regions()[i].sees;
                }
            }
            bwd.write(bwdPath);
            for (const auto& m : mirrors) if (fs::exists(m)) bwd.write(m);
        }
        // 4. STB
        if (!batch.empty()) {
            const fs::path tmp = stbPath.string() + ".atlas-tmp";
            if (sameSize) forge::stb::replaceStaticMaps(stbPath, tmp, batch);
            else forge::stb::replaceStaticMapsRelayout(stbPath, tmp, batch);
            fs::rename(tmp, stbPath);
            notes.push_back(std::string("FinalAlbion_RT.stb: ") + std::to_string(batch.size()) + " chunk(s) " + (sameSize ? "replaced in place" : "re-laid (sizes changed)"));
        }
        notes.push_back("FinalAlbion.wld + FinalAlbion.bwd (" + std::to_string(1 + std::count_if(std::begin(mirrors), std::end(mirrors), [](const fs::path& p) { return fs::exists(p); })) + " copies) updated: " + std::to_string(moveOf.size()) + " move(s), " + std::to_string(owners.size()) + " owner change(s), " + std::to_string(seesEdits.size()) + " visibility change(s)");
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

} // namespace albion::editor
