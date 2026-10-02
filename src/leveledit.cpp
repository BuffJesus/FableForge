#include "nlohmann/json.hpp"
#include <fstream>
#include "backups.hpp"
#include "pendingbanks.hpp"
#include "leveledit.hpp"
#include "temporarydirectory.hpp"
#include "vanilla_props.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <limits>
#include <set>
#include <unordered_map>
#include <stdexcept>

#include "forge/heightpen.hpp"
#include "forge/navpatch.hpp"
#include "forge/stb.hpp"
#include "forge/stbbake.hpp"
#include "forge/stbheightbake.hpp"
#include "forge/stbinfo.hpp"
#include "forge/bin.hpp"
#include "forge/levelstore.hpp"
#include "lodbake.hpp"
#include "stbrelocate.hpp"
#include "forge/wad.hpp"
#include "forge/wld.hpp"

namespace fs = std::filesystem;

namespace albion::editor {

namespace {

constexpr size_t kUndoDepth = 128;

std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(uint8_t(c))); return s; }

bool placeholderTrackName(const std::string& name) {
    return name.empty() || name == "INVALID" || name == "NULL" ||
           name.rfind("TrackTempName", 0) == 0;
}

std::string trackNameOf(const forge::tng::Thing& thing) {
    std::string name;
    for (const auto& property:thing.properties)
        if (lower(property.key)=="scriptname") name=property.value;
    return name;
}

bool editBrushCopyable(const forge::tng::Thing& thing) {
    const std::string type = lower(thing.type);
    // CThingFilter_IsEditBrushCopyable rejects these five physical thing types.
    return type != "village" && type != "switch" && type != "physicalswitch" &&
           type != "marker" && type != "tracknode" && !thing.findCtc("CTCCreatedEntity");
}

std::string unquote(std::string v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
}

float propF(const forge::tng::CtcBlock& b, const char* key, float fallback) {
    for (const auto& p : b.properties)
        if (lower(p.key) == lower(key)) return float(std::atof(p.value.c_str()));
    return fallback;
}

const forge::tng::CtcBlock* physicsOf(const forge::tng::Thing& t) {
    const auto* phys = t.findCtc("CTCPhysicsStandard");
    if (!phys) phys = t.findCtc("CTCPhysicsNavigator");
    return phys;
}

namespace {
// Every tng::File edit re-indexes the whole file, so a frame is ~12 re-indexes: fine on one
// thing, slow when pasting dozens into a big map (Document::paste stages each block in its own
// one-thing file and inserts the finished text once).
void writeInitialPosition(forge::tng::File& file,size_t index,const float local[3],int worldX,int worldY) {
    // Preserve absent keys and synchronize existing copies only. Creature placement
    // serializes world-space XY, unlike the map-local physics frame.
    const char* keys[]={"InitialPosX","InitialPosY","InitialPosZ"};
    const float world[]={local[0]+float(worldX),local[1]+float(worldY),local[2]};
    for (int axis=0;axis<3;++axis)
        if (file.things()[index].find(keys[axis])) file.setThingPropertyAll(index,keys[axis],formatFloat(world[axis]));
}

void writeFrame(forge::tng::File& file, size_t index, const Frame& frame,int worldX,int worldY,bool rebaseInitial=false) {
    const auto* phys = physicsOf(file.things()[index]);
    if (!phys) throw std::runtime_error("setFrame: thing has no physics block");
    const std::string ctc = phys->name;
    const bool moved=propF(*phys,"PositionX",0)!=frame.pos[0] || propF(*phys,"PositionY",0)!=frame.pos[1] || propF(*phys,"PositionZ",0)!=frame.pos[2];
    file.setCtcProperty(index, ctc, "PositionX", formatFloat(frame.pos[0]));
    file.setCtcProperty(index, ctc, "PositionY", formatFloat(frame.pos[1]));
    file.setCtcProperty(index, ctc, "PositionZ", formatFloat(frame.pos[2]));
    file.setCtcProperty(index, ctc, "RHSetForwardX", formatFloat(frame.forward[0]));
    file.setCtcProperty(index, ctc, "RHSetForwardY", formatFloat(frame.forward[1]));
    file.setCtcProperty(index, ctc, "RHSetForwardZ", formatFloat(frame.forward[2]));
    file.setCtcProperty(index, ctc, "RHSetUpX", formatFloat(frame.up[0]));
    file.setCtcProperty(index, ctc, "RHSetUpY", formatFloat(frame.up[1]));
    file.setCtcProperty(index, ctc, "RHSetUpZ", formatFloat(frame.up[2]));
    const bool hasScale = file.things()[index].find("ObjectScale").has_value();
    if (std::fabs(frame.scale - 1.0f) > 1e-6f) file.setThingProperty(index, "ObjectScale", formatFloat(frame.scale));
    else if (hasScale) file.removeThingProperty(index, "ObjectScale");
    // FableWin drag02997650 calls SetInitialPos02997b50. Pure orientation/scale
    // edits preserve separately authored initial positions; pasted blocks rebase.
    if (moved || rebaseInitial) writeInitialPosition(file,index,frame.pos,worldX,worldY);
}
} // namespace

bool parseUid(const std::string& raw, uint64_t& out) {
    const std::string s = unquote(raw);
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtoull(s.c_str(), &end, 10);
    return end && *end == 0;
}

void normalise3(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-12f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

} // namespace

std::string formatFloat(float v) { return forge::thingplacer::formatFloat(v); }

void frameToMatrix(const Frame& f, float m[16]) {
    float fw[3] = {f.forward[0], f.forward[1], f.forward[2]};
    float up[3] = {f.up[0], f.up[1], f.up[2]};
    normalise3(fw); normalise3(up);
    if (fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2] < 0.5f) { fw[0] = 1; fw[1] = 0; fw[2] = 0; }
    if (up[0] * up[0] + up[1] * up[1] + up[2] * up[2] < 0.5f) { up[0] = 0; up[1] = 0; up[2] = 1; }
    float r[3] = {fw[1] * up[2] - fw[2] * up[1], fw[2] * up[0] - fw[0] * up[2], fw[0] * up[1] - fw[1] * up[0]};
    normalise3(r);
    const float s = 0.01f * (f.scale > 0 ? f.scale : 1.0f);
    // CalcObjectMatrix rows: { -(forward x up), -forward, up, pos } * scale
    m[0] = -r[0] * s;  m[1] = -r[1] * s;  m[2] = -r[2] * s;  m[3] = 0;
    m[4] = -fw[0] * s; m[5] = -fw[1] * s; m[6] = -fw[2] * s; m[7] = 0;
    m[8] = up[0] * s;  m[9] = up[1] * s;  m[10] = up[2] * s; m[11] = 0;
    m[12] = f.pos[0];  m[13] = f.pos[1];  m[14] = f.pos[2];  m[15] = 1;
}

bool matrixToFrame(const float m[16], Frame& f) {
    const float l1 = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    const float l2 = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    const float l0 = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    if (l1 < 1e-9f || l2 < 1e-9f || l0 < 1e-9f) return false;
    f.forward[0] = -m[4] / l1; f.forward[1] = -m[5] / l1; f.forward[2] = -m[6] / l1;
    f.up[0] = m[8] / l2; f.up[1] = m[9] / l2; f.up[2] = m[10] / l2;
    f.pos[0] = m[12]; f.pos[1] = m[13]; f.pos[2] = m[14];
    f.scale = ((l0 + l1 + l2) / 3.0f) / 0.01f;
    return true;
}

void multiply(const float a[16], const float b[16], float out[16]) {
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = s;
        }
    std::memcpy(out, r, sizeof r);
}

bool invert(const float m[16], float out[16]) {
    // general 4x4 inverse (cofactors); matrices here are affine but this keeps it simple
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-20f) return false;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] / det;
    return true;
}

// ---------------------------------------------------------------- Document

bool Document::open(const fs::path& gameRoot, const std::string& mapName, const fs::path& levPath, std::string& error,
                    const ExternalWorld* external) {
    std::string text;
    fromWad_ = false;
    externalWld_.clear(); externalLev_.clear();
    loosePath_ = external ? external->tng : gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName + ".tng");
    if (external) {
        if (!fs::exists(loosePath_)) { error = "no " + loosePath_.string() + " next to the level"; return false; }
        text = readFile(loosePath_);
    } else if (fs::exists(loosePath_)) {
        text = readFile(loosePath_);
    } else {
        const auto layout = forge::levelstore::detect(gameRoot);
        if (layout.hasWad()) try {
            const auto wad = forge::wad::Archive::open(layout.wad);
            const std::string want = lower(mapName) + ".tng";
            for (const auto& e : wad.entries())
                if (lower(fs::path(e.name).filename().string()) == want) {
                    const auto bytes = wad.read(e);
                    text.assign(bytes.begin(), bytes.end());
                    fromWad_ = true;
                    break;
                }
        } catch (const std::exception& e) { error = e.what(); return false; }
        if (text.empty()) { error = "no " + mapName + ".tng loose" + (layout.hasWad() ? " or in FinalAlbion.wad" : " (no FinalAlbion.wad in this install)"); return false; }
    }
    if (!openText(mapName, std::move(text), error)) return false;
    if (!levPath.empty()) loadLevel(levPath, error);
    if (external) { externalWld_ = external->wld; externalLev_ = levPath; }
    // the map's world origin (creatures carry world-space InitialPos)
    worldX_ = worldY_ = worldSlot_ = 0;
    try {
        const auto world = forge::wld::File::parse(external ? external->wld : gameRoot / "data" / "Levels" / "FinalAlbion.wld");
        const std::string want = lower(mapName) + ".lev";
        for (const auto& m : world.maps())
            if (lower(fs::path(m.levelName).filename().string()) == want) { worldX_ = m.mapX; worldY_ = m.mapY; worldSlot_ = m.index; break; }
    } catch (const std::exception&) {}
    return true;
}

bool Document::loadLevel(const fs::path& levPath, std::string& error) {
    terrainSession_ = std::make_shared<const uint8_t>(0);
    soundListGrew_ = false;
    try { level_ = std::make_shared<forge::lev::File>(forge::lev::File::open(levPath)); }
    catch (const std::exception& e) { level_.reset(); error = std::string("level heights unavailable: ") + e.what(); return false; }
    if (level_) {
        auto t = std::make_shared<TerrainState>();
        const int cx = level_->cellsX(), cy = level_->cellsY();
        t->heights.resize(size_t(cx) * cy);
        t->walkable.resize(size_t(cx) * cy);
        t->cameraPassable.resize(size_t(cx) * cy);
        t->themeIndex.resize(size_t(cx) * cy);
        t->themeStrength.resize(size_t(cx) * cy);
        t->palette = level_->groundThemes();
        if (level_->hasGameMap()) {
            const int gw = level_->gameMapWidth(), gh = level_->gameMapHeight();
            t->atmosIndex.resize(size_t(gw) * gh); t->atmosStrength.resize(size_t(gw) * gh); t->sound.resize(size_t(gw) * gh);
            for (int gy = 0; gy < gh; ++gy)
                for (int gx = 0; gx < gw; ++gx) {
                    const size_t g = size_t(gy) * gw + gx;
                    const auto a = level_->atmosAt(gx, gy);
                    t->atmosIndex[g] = a.indices; t->atmosStrength[g] = a.strengths;
                    t->sound[g] = level_->soundAt(gx, gy);
                }
            t->atmosPalette = level_->atmosThemes();
        }
        for (int y = 0; y < cy; ++y)
            for (int x = 0; x < cx; ++x) {
                const size_t i = size_t(y) * cx + x;
                t->heights[i] = level_->heightAt(x, y);
                t->walkable[i] = level_->walkableAt(x, y) ? 1 : 0;
                t->cameraPassable[i] = level_->cameraPassableAt(x, y) ? 1 : 0;
                for (int k = 0; k < 3; ++k) { t->themeIndex[i][k] = level_->themeIndexAt(x, y, k); t->themeStrength[i][k] = level_->themeStrengthAt(x, y, k); }
            }
        terrain_ = t;
        savedTerrain_ = t;
        navWalkable_ = t->walkable;
        ++terrainRev_;
    }
    return true;
}

bool Document::openText(const std::string& mapName, std::string tngText, std::string& error) {
    try { file_ = forge::tng::File::parseText(std::move(tngText), mapName + ".tng"); }
    catch (const std::exception& e) { error = std::string("cannot parse .tng: ") + e.what(); return false; }
    terrainSession_ = std::make_shared<const uint8_t>(0);
    mapName_ = mapName;
    worldX_=worldY_=worldSlot_=0; // open() resolves the new world's origin afterward
    original_ = file_.serialize();
    undo_.clear(); redo_.clear();
    placementSection_ = "NULL";   // per document: a quest section chosen on another map must not carry over
    trackTempCounter_ = 0;
    for (const auto& thing:file_.things()) {
        if (lower(thing.type)!="tracknode") continue;
        const std::string name=trackNameOf(thing);
        constexpr std::string_view prefix="TrackTempName";
        if (name.rfind("TrackTempName",0)!=0 || name.size()==prefix.size()) continue;
        uint64_t number=0;
        const char* first=name.data()+prefix.size();
        const auto parsed=std::from_chars(first,name.data()+name.size(),number);
        if (parsed.ec==std::errc{} && parsed.ptr==name.data()+name.size() && number<UINT64_MAX)
            trackTempCounter_=std::max(trackTempCounter_,number+1);
    }
    soundListGrew_ = false;
    ++revision_;
    return true;
}

ThingSummary Document::summary(size_t index) const {
    ThingSummary s;
    s.index = index;
    if (index >= file_.things().size()) return s;
    const auto& t = file_.things()[index];
    s.type = t.type;
    s.definition = t.definitionType();
    s.scriptName = lower(t.type)=="tracknode" ? trackNameOf(t) : t.scriptName();
    if (s.scriptName == "NULL") s.scriptName.clear();
    s.uid = uidOf(index);
    s.hasFrame = physicsOf(t) != nullptr;
    return s;
}

uint64_t Document::uidOf(size_t index) const {
    if (index >= file_.things().size()) return 0;
    uint64_t uid = 0;
    if (const auto raw = file_.things()[index].find("UID")) parseUid(*raw, uid);
    return uid;
}

std::optional<size_t> Document::indexOfUid(uint64_t uid) const {
    for (size_t i = 0; i < file_.things().size(); ++i)
        if (uidOf(i) == uid) return i;
    return std::nullopt;
}

namespace {
std::vector<std::optional<size_t>> validOwnedParents(const std::vector<forge::tng::Thing>& things) {
    auto strictUid=[](const std::string& raw)->std::optional<uint64_t> {
        const std::string value=unquote(raw);
        if (value.empty() || value.front()<'0' || value.front()>'9') return std::nullopt;
        uint64_t uid=0;
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),uid);
        if (parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || !uid) return std::nullopt;
        return uid;
    };
    std::vector<std::optional<uint64_t>> ids(things.size());
    std::map<uint64_t,size_t> declarations;
    for (size_t i=0;i<things.size();++i) {
        size_t count=0;
        for (const auto& property:things[i].properties) if (lower(property.key)=="uid") {
            ++count;
            ids[i]=strictUid(property.value);
            if (ids[i]) ++declarations[*ids[i]];
        }
        if (count!=1) ids[i].reset();
    }
    std::map<uint64_t,size_t> owners;
    for (size_t i=0;i<ids.size();++i) {
        if (ids[i] && declarations[*ids[i]]==1) owners[*ids[i]]=i;
        else ids[i].reset();
    }
    std::vector<std::optional<size_t>> parents(things.size());
    for (size_t i=0;i<things.size();++i) {
        if (!ids[i]) continue;
        size_t blocks=0,fields=0;
        std::optional<uint64_t> owner;
        for (const auto& block:things[i].ctcBlocks) if (lower(block.name)=="ctcownedentity") {
            ++blocks;
            for (const auto& property:block.properties) if (lower(property.key)=="owneruid") {
                ++fields; owner=strictUid(property.value);
            }
        }
        if (blocks!=1 || fields!=1 || !owner) continue;
        const auto found=owners.find(*owner);
        if (found!=owners.end()) parents[i]=found->second;
    }
    return parents;
}
} // namespace

std::vector<std::pair<size_t,size_t>> Document::ownedTree(const std::vector<size_t>& roots) const {
    const auto& things=file_.things();
    std::vector<std::vector<size_t>> children(things.size());
    const auto parents=validOwnedParents(things);
    for (size_t i=0;i<parents.size();++i) if (parents[i]) children[*parents[i]].push_back(i);
    std::vector<size_t> queue;
    std::vector<std::pair<size_t,size_t>> result;
    std::vector<bool> visited(things.size(),false);
    for (size_t root:roots) if (root<things.size() && !visited[root]) {
        visited[root]=true; queue.push_back(root);
    }
    for (size_t next=0;next<queue.size();++next) for (size_t child:children[queue[next]])
        if (!visited[child]) { visited[child]=true; queue.push_back(child); result.push_back({child,queue[next]}); }
    return result;
}

std::vector<size_t> Document::ownedDescendants(const std::vector<size_t>& roots) const {
    std::vector<size_t> result;
    for (const auto& [child,parent]:ownedTree(roots)) result.push_back(child);
    return result;
}

std::vector<std::pair<size_t,Frame>> Document::ownedFramesAfter(const std::vector<std::pair<size_t,Frame>>& roots) const {
    std::vector<size_t> indices;
    std::map<size_t,Frame> moved;
    for (const auto& [index,frame]:roots) if (index<thingCount() && !moved.count(index)) {
        Frame before;
        if (frameOf(index,before)) { indices.push_back(index); moved[index]=frame; }
    }
    std::vector<std::pair<size_t,Frame>> result;
    for (const auto& [child,parent]:ownedTree(indices)) {
        const auto now=moved.find(parent);
        Frame oldParent, oldChild;
        if (now==moved.end() || !frameOf(parent,oldParent) || !frameOf(child,oldChild)) continue;
        Frame a=oldParent,b=now->second,c=oldChild,after;
        a.scale=b.scale=c.scale=1.0f;
        float ma[16],mb[16],mc[16],inv[16],delta[16],matrix[16];
        frameToMatrix(a,ma); frameToMatrix(b,mb); frameToMatrix(c,mc);
        if (!invert(ma,inv)) continue;
        multiply(inv,mb,delta);
        multiply(mc,delta,matrix);
        if (!matrixToFrame(matrix,after)) continue;
        after.scale=oldChild.scale;
        moved[child]=after;
        result.push_back({child,after});
    }
    return result;
}

bool Document::frameOf(size_t index, Frame& out) const {
    if (index >= file_.things().size()) return false;
    const auto& t = file_.things()[index];
    const auto* phys = physicsOf(t);
    if (!phys) return false;
    out.pos[0] = propF(*phys, "PositionX", 0.0f);
    out.pos[1] = propF(*phys, "PositionY", 0.0f);
    out.pos[2] = propF(*phys, "PositionZ", 0.0f);
    out.forward[0] = propF(*phys, "RHSetForwardX", 1.0f);
    out.forward[1] = propF(*phys, "RHSetForwardY", 0.0f);
    out.forward[2] = propF(*phys, "RHSetForwardZ", 0.0f);
    out.up[0] = propF(*phys, "RHSetUpX", 0.0f);
    out.up[1] = propF(*phys, "RHSetUpY", 0.0f);
    out.up[2] = propF(*phys, "RHSetUpZ", 1.0f);
    out.scale = 1.0f;
    if (const auto sc = t.find("ObjectScale")) {
        const float v = float(std::atof(sc->c_str()));
        if (std::isfinite(v) && v > 0) out.scale = v;
    }
    return true;
}

std::optional<float> Document::groundHeight(float x, float y) const {
    if (!level_) return std::nullopt;
    try { return forge::thingplacer::terrainHeightAt(*level_, x, y); }
    catch (...) { return std::nullopt; }
}

Document::Snapshot Document::snapshot() const { return Snapshot{file_.serialize(), terrain_}; }

void Document::pushUndo() {
    if (batchDepth_ > 0) {
        if (batchPushed_) return;
        batchPushed_ = true;
    }
    undo_.push_back(snapshot());
    if (undo_.size() > kUndoDepth) undo_.erase(undo_.begin());
    redo_.clear();
}

void Document::beginBatch() {
    if (batchDepth_++ == 0) batchPushed_ = false;
}

void Document::endBatch() {
    if (batchDepth_ > 0 && --batchDepth_ == 0) batchPushed_ = false;
}

Document::Fragment Document::extract(const std::vector<size_t>& indices) const {
    Fragment f;
    int n = 0;
    for (size_t i : indices) {
        if (!isEditBrushCopyable(i)) continue;
        Fragment::Item item;
        item.block = file_.thingBlockText(i);
        item.hasFrame = frameOf(i, item.frame);
        if (item.hasFrame) { for (int k = 0; k < 3; ++k) f.centre[k] += item.frame.pos[k]; ++n; }
        f.items.push_back(std::move(item));
    }
    if (n) for (int k = 0; k < 3; ++k) f.centre[k] /= float(n);
    return f;
}

bool Document::isEditBrushCopyable(size_t index) const {
    return index < file_.things().size() && editBrushCopyable(file_.things()[index]);
}

std::vector<size_t> Document::paste(const Fragment& fragment, const float at[3], bool dropToGround) {
    std::vector<size_t> out;
    if (fragment.empty()) return out;
    beginBatch();
    try {
        // each block is edited in a one-thing staging file (cheap re-index) and inserted into the
        // map once: editing it in place re-indexed the whole .tng a dozen times per thing
        uint64_t uid = forge::thingplacer::nextUid(file_);
        for (const auto& item : fragment.items) {
            auto stage = forge::tng::File::parseText("Version 2;\r\n" + item.block, "paste");
            if (stage.things().empty() || !editBrushCopyable(stage.things()[0])) continue;
            if (out.empty()) pushUndo();
            while (!forge::thingplacer::uidIsFree(file_, uid)) ++uid;
            stage.setThingProperty(0, "UID", std::to_string(uid++));
            if (stage.things()[0].find("ScriptName")) stage.setThingProperty(0, "ScriptName", "NULL");
            if (item.hasFrame && physicsOf(stage.things()[0])) {
                Frame nf = item.frame;
                for (int k = 0; k < 3; ++k) nf.pos[k] = at[k] + (item.frame.pos[k] - fragment.centre[k]);
                if (dropToGround) if (const auto h = groundHeight(nf.pos[0], nf.pos[1])) nf.pos[2] = *h + (item.frame.pos[2] - fragment.centre[2]);
                writeFrame(stage, 0, nf,worldX_,worldY_,true);
            }
            const std::string block = stage.thingBlockText(0);
            const size_t idx = file_.sectionNames().empty() ? file_.insertThingBlockBefore(file_.things().size(), block)
                                                            : file_.insertThingBlock(targetSection(), block);   // where new things go
            ++revision_;
            out.push_back(idx);
        }
    } catch (...) { endBatch(); throw; }
    endBatch();
    return out;
}

void Document::restore(const Snapshot& s) {
    file_ = forge::tng::File::parseText(s.tng, mapName_ + ".tng");
    ++revision_;
    if (s.terrain && s.terrain != terrain_) {
        const bool themes = s.terrain->themeIndex != terrain_->themeIndex || s.terrain->themeStrength != terrain_->themeStrength;
        terrain_ = s.terrain;
        writeTerrainToLevel();
        ++terrainRev_;
        if (themes) ++themeRev_;
    }
}

void Document::writeTerrainToLevel() {
    if (!level_ || !terrain_) return;
    const auto& pal = level_->groundThemes();
    for (size_t i = 0; i < terrain_->palette.size() && i < pal.size(); ++i)
        if (pal[i].name != terrain_->palette[i].name || pal[i].value != terrain_->palette[i].value)
            level_->setGroundTheme(i, terrain_->palette[i].name, terrain_->palette[i].value);
    const int cx = level_->cellsX(), cy = level_->cellsY();
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) {
            const size_t i = size_t(y) * cx + x;
            if (level_->heightAt(x, y) != terrain_->heights[i]) level_->setHeightAt(x, y, terrain_->heights[i]);
            if (level_->walkableAt(x, y) != (terrain_->walkable[i] != 0)) level_->setWalkableAt(x, y, terrain_->walkable[i] != 0);
            if (!terrain_->cameraPassable.empty()) {
                const bool cam = terrain_->cameraPassable[i] != 0 || terrain_->walkable[i] != 0;   // the saver's OR
                if (level_->cameraPassableAt(x, y) != cam) level_->setCameraPassableAt(x, y, cam);
            }
            bool sameTheme = true;
            for (int k = 0; k < 3; ++k) sameTheme = sameTheme && level_->themeIndexAt(x, y, k) == terrain_->themeIndex[i][k] && level_->themeStrengthAt(x, y, k) == terrain_->themeStrength[i][k];
            if (!sameTheme) level_->setThemeBlendAt(x, y, terrain_->themeIndex[i], terrain_->themeStrength[i]);
        }
    if (level_->hasGameMap() && !terrain_->sound.empty()) {
        const auto& ap = level_->atmosThemes();
        for (size_t i = 0; i < terrain_->atmosPalette.size() && i < ap.size(); ++i)
            if (ap[i].name != terrain_->atmosPalette[i].name || ap[i].value != terrain_->atmosPalette[i].value)
                level_->setAtmosTheme(i, terrain_->atmosPalette[i].name, terrain_->atmosPalette[i].value);
        const int gw = level_->gameMapWidth(), gh = level_->gameMapHeight();
        for (int gy = 0; gy < gh; ++gy)
            for (int gx = 0; gx < gw; ++gx) {
                const size_t g = size_t(gy) * gw + gx;
                const auto a = level_->atmosAt(gx, gy);
                if (a.indices != terrain_->atmosIndex[g] || a.strengths != terrain_->atmosStrength[g])
                    level_->setAtmosAt(gx, gy, {terrain_->atmosIndex[g], terrain_->atmosStrength[g]});
                if (level_->soundAt(gx, gy) != terrain_->sound[g]) level_->setSoundAt(gx, gy, terrain_->sound[g]);
            }
    }
}

// ---------------------------------------------------------------- terrain

void Document::beginStroke(const TerrainBrush& brush) {
    if (!hasTerrain() || stroke_) return;
    pushUndo();
    working_ = std::make_unique<TerrainState>(*terrain_);
    hf_ = std::make_unique<forge::terrain::Heightfield>(forge::terrain::Heightfield::fromLev(*level_));
    stroke_ = true;
    flattenTarget_ = terrainHeight(brush.x, brush.y).value_or(0.0f);
    alteredByPen_.clear();   // vanilla ResetMapsAlteredByPenList on the press
}

void Document::applyBrush(const TerrainBrush& brush, float dt) {
    if (!stroke_ || !working_) return;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    using Mode = TerrainBrush::Mode;
    if (brush.mode == Mode::CameraPass || brush.mode == Mode::CameraBlock) {
        const int x0 = std::max(0, int(std::floor(brush.x - brush.radius))), x1 = std::min(cx - 1, int(std::ceil(brush.x + brush.radius)));
        const int y0 = std::max(0, int(std::floor(brush.y - brush.radius))), y1 = std::min(cy - 1, int(std::ceil(brush.y + brush.radius)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float dx = float(x) + 0.5f - brush.x, dy = float(y) + 0.5f - brush.y;
                if (dx * dx + dy * dy <= brush.radius * brush.radius)
                    working_->cameraPassable[size_t(y) * cx + x] = brush.mode == Mode::CameraPass ? 1 : 0;
            }
        ++terrainRev_;
        return;
    }
    if (brush.mode == Mode::Walkable || brush.mode == Mode::Blocked) {
        const int x0 = std::max(0, int(std::floor(brush.x - brush.radius))), x1 = std::min(cx - 1, int(std::ceil(brush.x + brush.radius)));
        const int y0 = std::max(0, int(std::floor(brush.y - brush.radius))), y1 = std::min(cy - 1, int(std::ceil(brush.y + brush.radius)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float dx = float(x) + 0.5f - brush.x, dy = float(y) + 0.5f - brush.y;
                if (dx * dx + dy * dy <= brush.radius * brush.radius)
                    working_->walkable[size_t(y) * cx + x] = brush.mode == Mode::Walkable ? 1 : 0;
            }
        ++terrainRev_;
        return;
    }
    if (brush.mode == Mode::Theme) {
        // paint straight into the level (the library brush keeps the 3-slot invariant), then mirror the touched cells
        forge::terrain::ThemeBrush tb;
        tb.centerX = brush.x; tb.centerY = brush.y; tb.radius = brush.radius;
        tb.opacity = std::clamp(brush.strength * dt, 0.0f, 1.0f);
        tb.themeIndex = brush.themeIndex;
        forge::terrain::applyThemeBrush(*level_, tb);
        const int x0 = std::max(0, int(std::floor(brush.x - brush.radius)) - 1), x1 = std::min(cx - 1, int(std::ceil(brush.x + brush.radius)) + 1);
        const int y0 = std::max(0, int(std::floor(brush.y - brush.radius)) - 1), y1 = std::min(cy - 1, int(std::ceil(brush.y + brush.radius)) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const size_t i = size_t(y) * cx + x;
                for (int k = 0; k < 3; ++k) { working_->themeIndex[i][k] = level_->themeIndexAt(x, y, k); working_->themeStrength[i][k] = level_->themeStrengthAt(x, y, k); }
            }
        ++terrainRev_;
        return;
    }
    if (brush.mode == Mode::Environment || brush.mode == Mode::Sound) {
        // game-map cells (4x4 height cells each) whose centre is under the brush
        if (!level_->hasGameMap() || working_->sound.empty()) return;
        const int gw = level_->gameMapWidth(), gh = level_->gameMapHeight();
        const float opacity = std::clamp(brush.strength * dt, 0.0f, 1.0f);
        for (int gy = 0; gy < gh; ++gy)
            for (int gx = 0; gx < gw; ++gx) {
                const float dx = float(gx) * 4.0f + 2.0f - brush.x, dy = float(gy) * 4.0f + 2.0f - brush.y;
                if (dx * dx + dy * dy > brush.radius * brush.radius) continue;
                const size_t g = size_t(gy) * gw + gx;
                if (brush.mode == Mode::Sound) {
                    if (brush.themeIndex <= level_->soundThemes().size()) working_->sound[g] = brush.themeIndex;
                    continue;
                }
                forge::terrain::ThemeBlend blend{working_->atmosIndex[g], working_->atmosStrength[g]};
                if (unsigned(blend.strengths[0]) + blend.strengths[1] + blend.strengths[2] != 255) blend = {{blend.indices[0], 0, 0}, {255, 0, 0}};
                const auto next = forge::terrain::paintThemeBlend(blend, brush.themeIndex, opacity);
                working_->atmosIndex[g] = next.indices; working_->atmosStrength[g] = next.strengths;
            }
        ++terrainRev_;
        return;
    }
    if (brush.mode == Mode::ReplaceTheme) {
        // a hard swap under the pen (cell centres inside the radius), mirrored into the level like Theme;
        // the slot match is replaceTheme's (index or water family, any strength)
        if (brush.themeIndex == 0) return;
        const auto match = replaceMatchSet(brush.replaceFrom);
        const int x0 = std::max(0, int(std::floor(brush.x - brush.radius))), x1 = std::min(cx - 1, int(std::ceil(brush.x + brush.radius)));
        const int y0 = std::max(0, int(std::floor(brush.y - brush.radius))), y1 = std::min(cy - 1, int(std::ceil(brush.y + brush.radius)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float dx = float(x) + 0.5f - brush.x, dy = float(y) + 0.5f - brush.y;
                if (dx * dx + dy * dy > brush.radius * brush.radius) continue;
                const size_t i = size_t(y) * cx + x;
                const forge::terrain::ThemeBlend blend{working_->themeIndex[i], working_->themeStrength[i]};
                const auto next = forge::terrain::replaceThemesInBlend(blend, match, brush.themeIndex);
                if (next.indices == blend.indices && next.strengths == blend.strengths) continue;
                working_->themeIndex[i] = next.indices; working_->themeStrength[i] = next.strengths;
                level_->setThemeBlendAt(x, y, next.indices, next.strengths);
            }
        ++terrainRev_;
        return;
    }
    if (brush.mode == Mode::Flatten || brush.mode == Mode::Smooth || brush.mode == Mode::Noise || brush.mode == Mode::HeightKey ||
        ((brush.mode == Mode::Raise || brush.mode == Mode::Lower) && brush.exactStep)) {
        // the vanilla Height Toolbox pens, one application per call
        const float target = brush.targetFromStroke ? flattenTarget_ : brush.target;
        size_t n = 0;
        switch (brush.mode) {
            case Mode::Raise: n = forge::heightpen::changeHeight(*hf_, brush.x, brush.y, brush.radius, brush.step, alteredByPen_); break;
            case Mode::Lower: n = forge::heightpen::changeHeight(*hf_, brush.x, brush.y, brush.radius, -brush.step, alteredByPen_); break;
            case Mode::Flatten: n = forge::heightpen::paintHeight(*hf_, brush.x, brush.y, brush.radius, target, forge::heightpen::speedToOpacity(brush.speed)); break;
            case Mode::Smooth: n = forge::heightpen::smear(*hf_, brush.x, brush.y, brush.radius, brush.smoothness, brush.spikyness); break;
            case Mode::Noise: n = forge::heightpen::noise(*hf_, brush.x, brush.y, brush.radius, brush.magnifier, penSeed_); break;
            case Mode::HeightKey: n = forge::heightpen::heightAddition(*hf_, brush.x, brush.y, brush.radius, brush.step); break;
            default: break;
        }
        if (n) {
            for (int y = 0; y < cy; ++y)
                for (int x = 0; x < cx; ++x) working_->heights[size_t(y) * cx + x] = hf_->at(x, y);
            ++terrainRev_;
        }
        return;
    }
    forge::terrain::Brush b;
    b.centerX = brush.x; b.centerY = brush.y; b.radius = brush.radius;
    switch (brush.mode) {
        case Mode::Raise:   b.mode = forge::terrain::BrushMode::RaiseLower; b.amount = brush.strength * dt; break;
        case Mode::Lower:   b.mode = forge::terrain::BrushMode::RaiseLower; b.amount = -brush.strength * dt; break;
        case Mode::Flatten: b.mode = forge::terrain::BrushMode::Flatten; b.amount = std::clamp(brush.strength * dt, 0.0f, 1.0f); b.targetHeight = flattenTarget_; break;
        case Mode::Smooth:  b.mode = forge::terrain::BrushMode::Smooth; b.amount = std::clamp(brush.strength * dt, 0.0f, 1.0f); break;
        default: break;
    }
    forge::terrain::applyBrush(*hf_, b);
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) working_->heights[size_t(y) * cx + x] = hf_->at(x, y);
    ++terrainRev_;
}

void Document::endStroke() {
    if (!stroke_) return;
    stroke_ = false;
    const auto& w = *working_;
    const auto& t = *terrain_;
    if (w.heights == t.heights && w.walkable == t.walkable && w.cameraPassable == t.cameraPassable && w.themeIndex == t.themeIndex &&
        w.themeStrength == t.themeStrength && w.atmosIndex == t.atmosIndex && w.atmosStrength == t.atmosStrength && w.sound == t.sound) {
        // nothing painted (an empty stroke, a no-op tool): drop the undo step beginStroke pushed
        working_.reset();
        hf_.reset();
        if (!undo_.empty()) undo_.pop_back();
        return;
    }
    const bool themes = working_->themeIndex != terrain_->themeIndex || working_->themeStrength != terrain_->themeStrength;
    const auto before = terrain_;
    terrain_ = std::shared_ptr<const TerrainState>(working_.release());
    hf_.reset();
    writeTerrainToLevel();
    if (before->heights != terrain_->heights) reseatGroundedThings(*before, 1.0f, false);
    ++revision_;
    ++terrainRev_;
    if (themes) ++themeRev_;
}

bool Document::setVertexHeights(const std::vector<VertexHeight>& edits) { return setVertexHeights(edits, true); }

bool Document::setVertexHeights(const std::vector<VertexHeight>& edits, bool followGround) {
    if (!hasTerrain() || stroke_) return false;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    auto next = std::make_unique<TerrainState>(*terrain_);
    bool changed = false;
    for (const auto& e : edits) {
        if (e.x < 0 || e.y < 0 || e.x >= cx || e.y >= cy) continue;
        float& dst = next->heights[size_t(e.y) * cx + e.x];
        if (dst != e.h) { dst = e.h; changed = true; }
    }
    if (!changed) return true;
    pushUndo();
    const auto before = terrain_;
    terrain_ = std::shared_ptr<const TerrainState>(next.release());
    hf_.reset();
    writeTerrainToLevel();
    if (followGround) reseatGroundedThings(*before, 1.0f, false);
    ++revision_;
    ++terrainRev_;
    return true;
}

namespace {
constexpr float kMaxVertexHeight = 2048.0f - 1e-4f;   // vanilla GFLimit(h, 0, 2048 - 1e-4)
}

size_t Document::raiseHeights(float d, bool moveThings) {
    if (!hasTerrain() || stroke_ || !std::isfinite(d) || d == 0.0f) return 0;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    std::vector<VertexHeight> edits;
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) {
            const float h = terrain_->heights[size_t(y) * cx + x], v = std::clamp(h + d, 0.0f, kMaxVertexHeight);
            if (v != h) edits.push_back({x, y, v});
        }
    if (edits.empty() || !setVertexHeights(edits, false)) return 0;
    if (moveThings)   // same undo step: setVertexHeights pushed it
        for (size_t i = 0; i < file_.things().size(); ++i) {
            Frame f;
            if (!frameOf(i, f)) continue;
            const auto* physics = physicsOf(file_.things()[i]);
            if (!physics) continue;
            f.pos[2] += d;
            file_.setCtcProperty(i, physics->name, "PositionZ", formatFloat(f.pos[2]));
            writeInitialPosition(file_, i, f.pos, worldX_, worldY_);
        }
    ++revision_;
    return edits.size();
}

size_t Document::resizeHeightsPercent(float pct) {
    if (!hasTerrain() || stroke_ || !std::isfinite(pct) || pct < 0.0f) return 0;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    std::vector<VertexHeight> edits;
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) {
            const float h = terrain_->heights[size_t(y) * cx + x], v = std::clamp(h * pct / 100.0f, 0.0f, kMaxVertexHeight);
            if (v != h) edits.push_back({x, y, v});
        }
    return !edits.empty() && setVertexHeights(edits) ? edits.size() : 0;
}

size_t Document::setAllHeights(float v) {
    if (!hasTerrain() || stroke_ || !std::isfinite(v)) return 0;
    const float target = std::clamp(v, 0.0f, kMaxVertexHeight);
    const int cx = level_->cellsX(), cy = level_->cellsY();
    std::vector<VertexHeight> edits;
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x)
            if (terrain_->heights[size_t(y) * cx + x] != target) edits.push_back({x, y, target});
    return !edits.empty() && setVertexHeights(edits) ? edits.size() : 0;
}

const std::vector<std::string>& Document::soundThemes() const {
    static const std::vector<std::string> none;
    return level_ ? level_->soundThemes() : none;
}

int Document::addEnvironmentTheme(const std::string& name, uint32_t defIndex) {
    if (!hasTerrain() || stroke_ || !hasGameMap() || terrain_->atmosPalette.empty() || name.empty() || name.size() >= 128 || defIndex == 0) return -1;
    for (size_t i = 0; i < terrain_->atmosPalette.size(); ++i)
        if (terrain_->atmosPalette[i].name == name) return int(i);
    // slot 0 stays the "no environment" entry retail maps paint most cells with
    for (size_t i = 1; i < terrain_->atmosPalette.size(); ++i)
        if (terrain_->atmosPalette[i].name.empty()) {
            auto next = std::make_unique<TerrainState>(*terrain_);
            next->atmosPalette[i] = {name, defIndex};
            pushUndo();
            terrain_ = std::shared_ptr<const TerrainState>(next.release());
            writeTerrainToLevel();
            ++revision_;
            ++terrainRev_;
            return int(i);
        }
    return -1;
}

int Document::addSoundTheme(const std::string& name, std::string& error) {
    if (!hasTerrain() || stroke_) { error = "no terrain, or a stroke is active"; return -1; }
    try {
        const int idx = level_->addSoundTheme(name);
        soundListGrew_ = true;
        ++terrainRev_;
        return idx;
    } catch (const std::exception& e) { error = e.what(); return -1; }
}

std::optional<std::pair<uint8_t, uint8_t>> Document::environmentAndSoundAt(float x, float y) const {
    if (!hasGameMap()) return std::nullopt;
    const TerrainState& t = liveTerrain();
    if (t.sound.empty()) return std::nullopt;
    const int gx = int(std::floor(x / 4.0f)), gy = int(std::floor(y / 4.0f));
    const int gw = level_->gameMapWidth(), gh = level_->gameMapHeight();
    if (gx < 0 || gy < 0 || gx >= gw || gy >= gh) return std::nullopt;
    const size_t g = size_t(gy) * gw + gx;
    int best = 0;
    for (int k = 1; k < 3; ++k)
        if (t.atmosStrength[g][k] > t.atmosStrength[g][best]) best = k;
    return std::make_pair(t.atmosIndex[g][best], t.sound[g]);
}

std::optional<uint8_t> Document::dominantThemeAt(float x, float y) const {
    if (!hasTerrain()) return std::nullopt;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    if (ix < 0 || iy < 0 || ix >= cx || iy >= cy) return std::nullopt;
    const TerrainState& t = liveTerrain();
    const size_t i = size_t(iy) * cx + ix;
    int best = 0;
    for (int k = 1; k < 3; ++k)
        if (t.themeStrength[i][k] > t.themeStrength[i][best]) best = k;
    return t.themeIndex[i][best];
}

size_t Document::fillSound(uint8_t index) {
    if (!hasTerrain() || stroke_ || !level_->hasGameMap() || terrain_->sound.empty()) return 0;
    if (index > level_->soundThemes().size()) return 0;
    auto next = std::make_unique<TerrainState>(*terrain_);
    size_t changed = 0;
    for (auto& s : next->sound) if (s != index) { s = index; ++changed; }
    if (!changed) return 0;
    pushUndo();
    terrain_ = std::shared_ptr<const TerrainState>(next.release());
    writeTerrainToLevel();   // mirrors the grid through level_->setSoundAt
    ++revision_;
    ++terrainRev_;
    return changed;
}

std::array<bool, 256> Document::replaceMatchSet(uint8_t from) const {
    std::array<bool, 256> match{};
    for (size_t s = 0; s < 256; ++s)
        match[s] = s == from || (themeFamily_[s] != 0 && themeFamily_[s] == themeFamily_[from]);
    return match;
}

size_t Document::replaceTheme(uint8_t from, uint8_t to, ReplaceScope scope, float x, float y) {
    if (!hasTerrain() || stroke_ || from == to || to == 0) return 0;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    auto next = std::make_unique<TerrainState>(*terrain_);
    const auto match = replaceMatchSet(from);
    auto holds = [&](size_t i) {
        for (int k = 0; k < 3; ++k)
            if (match[next->themeIndex[i][k]]) return true;
        return false;
    };
    std::vector<uint8_t> hit(size_t(cx) * cy, 0);
    if (scope == ReplaceScope::All) {
        for (size_t i = 0; i < hit.size(); ++i) hit[i] = holds(i) ? 1 : 0;
    } else {
        const int sx = int(std::floor(x)), sy = int(std::floor(y));
        if (sx < 0 || sy < 0 || sx >= cx || sy >= cy || !holds(size_t(sy) * cx + sx)) return 0;
        std::vector<std::pair<int, int>> todo{{sx, sy}};
        hit[size_t(sy) * cx + sx] = 1;
        while (!todo.empty()) {
            const auto [px, py] = todo.back(); todo.pop_back();
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = px + dx, ny = py + dy;
                    if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= cx || ny >= cy) continue;
                    const size_t n = size_t(ny) * cx + nx;
                    if (hit[n] || !holds(n)) continue;
                    hit[n] = 1;
                    todo.push_back({nx, ny});
                }
        }
    }
    size_t changed = 0;
    for (size_t i = 0; i < hit.size(); ++i) {
        if (!hit[i]) continue;
        const auto blend = forge::terrain::replaceThemesInBlend({next->themeIndex[i], next->themeStrength[i]}, match, to);
        if (blend.indices == next->themeIndex[i] && blend.strengths == next->themeStrength[i]) continue;   // already `to`
        next->themeIndex[i] = blend.indices; next->themeStrength[i] = blend.strengths;
        ++changed;
    }
    if (!changed) return 0;
    pushUndo();
    terrain_ = std::shared_ptr<const TerrainState>(next.release());
    hf_.reset();
    writeTerrainToLevel();
    ++revision_;
    ++terrainRev_;
    ++themeRev_;
    return changed;
}

size_t Document::applyFractal(const forge::fractal::Params& params) {
    if (!hasTerrain() || stroke_) return 0;
    const forge::fractal::Generator gen(params);
    const int cx = level_->cellsX(), cy = level_->cellsY();
    std::vector<VertexHeight> edits;
    edits.reserve(size_t(cx) * cy);
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) {
            const double h = double(gen.heightAt(double(x + worldX_), double(y + worldY_))) * params.scale;
            const float v = float(std::clamp(h, 0.0, 2047.9999));
            if (terrain_->heights[size_t(y) * cx + x] != v) edits.push_back({x, y, v});
        }
    if (edits.empty() || !setVertexHeights(edits)) return 0;
    return edits.size();
}

std::vector<float> Document::fittedHeights(const forge::fillerfit::Params& params, const std::vector<forge::fillerfit::Neighbour>& neighbours,
                                           forge::fillerfit::Report* report) const {
    if (!hasTerrain()) return {};
    std::vector<float> h = terrain_->heights;
    if (!forge::fillerfit::fit(h, worldX_, worldY_, level_->cellsX(), level_->cellsY(), neighbours, params, report)) return {};
    for (auto& v : h) v = std::clamp(v, 0.0f, 2047.9999f);
    return h;
}

size_t Document::fitToNeighbours(const forge::fillerfit::Params& params, const std::vector<forge::fillerfit::Neighbour>& neighbours,
                                 forge::fillerfit::Report* report) {
    if (!hasTerrain() || stroke_) return 0;
    const auto h = fittedHeights(params, neighbours, report);
    if (h.empty()) return 0;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    std::vector<VertexHeight> edits;
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) {
            const size_t i = size_t(y) * cx + x;
            if (terrain_->heights[i] != h[i]) edits.push_back({x, y, h[i]});
        }
    if (edits.empty() || !setVertexHeights(edits)) return 0;
    return edits.size();
}

TerrainClip Document::copyTerrain(int x0, int y0, int x1, int y1, bool withThings) const {
    TerrainClip c;
    if (!hasTerrain()) return c;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = std::clamp(x0, 0, cx - 1); x1 = std::clamp(x1, 0, cx - 1);
    y0 = std::clamp(y0, 0, cy - 1); y1 = std::clamp(y1, 0, cy - 1);
    c.w = x1 - x0 + 1; c.h = y1 - y0 + 1;
    std::map<uint8_t, uint8_t> slotOf;   // map palette slot -> clip slot
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const size_t i = size_t(y) * cx + x;
            c.heights.push_back(terrain_->heights[i]);
            std::array<uint8_t, 3> idx{};
            for (int k = 0; k < 3; ++k) {
                const uint8_t slot = terrain_->themeIndex[i][k];
                auto hit = slotOf.find(slot);
                if (hit == slotOf.end()) {
                    hit = slotOf.emplace(slot, uint8_t(c.themes.size())).first;
                    c.themes.push_back(slot < terrain_->palette.size() ? terrain_->palette[slot] : forge::lev::GroundTheme{});
                }
                idx[k] = hit->second;
            }
            c.themeIndex.push_back(idx);
            c.themeStrength.push_back(terrain_->themeStrength[i]);
        }
    if (withThings)
        for (size_t i : thingsInRect(x0, y0, x1, y1)) {
            Frame f;
            if (!isEditBrushCopyable(i) || !frameOf(i, f)) continue;
            TerrainClip::Thing t;
            t.block = file_.thingBlockText(i);
            t.dx = f.pos[0] - float(x0);
            t.dy = f.pos[1] - float(y0);
            t.aboveGround = f.pos[2] - groundHeight(f.pos[0], f.pos[1]).value_or(f.pos[2]);
            t.frame = f;
            c.things.push_back(std::move(t));
        }
    return c;
}

std::vector<size_t> Document::thingsInRect(int x0, int y0, int x1, int y1) const {
    std::vector<size_t> out;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (hasTerrain()) {
        const int cx = level_->cellsX(), cy = level_->cellsY();
        x0 = std::clamp(x0, 0, cx - 1); x1 = std::clamp(x1, 0, cx - 1);
        y0 = std::clamp(y0, 0, cy - 1); y1 = std::clamp(y1, 0, cy - 1);
    }
    for (size_t i = 0; i < file_.things().size(); ++i) {
        Frame f;
        if (!frameOf(i, f)) continue;
        if (f.pos[0] >= float(x0) && f.pos[0] <= float(x1) &&
            f.pos[1] >= float(y0) && f.pos[1] <= float(y1)) out.push_back(i);
    }
    return out;
}

size_t Document::removeThingsInRect(int x0, int y0, int x1, int y1,
                                    size_t* skipped, size_t* clearedLinks) {
    if (skipped) *skipped = 0;
    const auto inside = thingsInRect(x0, y0, x1, y1);
    std::vector<size_t> removable;
    for (size_t index : inside) {
        if (isLocked(index)) { if (skipped) ++*skipped; }
        else removable.push_back(index);
    }
    return removeWithOwned(removable, false, clearedLinks);
}

size_t Document::pasteTerrain(const TerrainClip& clip, int x, int y, int quarterTurns, bool heights, bool themes, bool relative,
                              bool withThings, size_t* thingsPlaced) {
    if (thingsPlaced) *thingsPlaced = 0;
    if (!hasTerrain() || stroke_ || clip.empty()) return 0;
    beginBatch();   // the ground and the things are one undo step
    size_t changed = 0;
    try {
        changed = (heights || themes) ? pasteTerrainCells(clip, x, y, quarterTurns, heights, themes, relative) : 0;
        if (withThings && !clip.things.empty()) {
            const int turns = ((quarterTurns % 4) + 4) % 4;
            Fragment frag;   // absolute frames, centre 0: Document::paste puts each where its frame says
            for (const auto& t : clip.things) {
                // the vertex grid's turn (pasteTerrainCells): output = f(source) for a point in the clip
                float ox = t.dx, oy = t.dy;
                if (turns == 1) { ox = float(clip.h - 1) - t.dy; oy = t.dx; }
                else if (turns == 2) { ox = float(clip.w - 1) - t.dx; oy = float(clip.h - 1) - t.dy; }
                else if (turns == 3) { ox = t.dy; oy = float(clip.w - 1) - t.dx; }
                auto turn = [&](const float v[3], float out[3]) {
                    out[2] = v[2];
                    if (turns == 0) { out[0] = v[0]; out[1] = v[1]; }
                    else if (turns == 1) { out[0] = -v[1]; out[1] = v[0]; }
                    else if (turns == 2) { out[0] = -v[0]; out[1] = -v[1]; }
                    else { out[0] = v[1]; out[1] = -v[0]; }
                };
                Fragment::Item item;
                item.block = t.block;
                item.hasFrame = true;
                item.frame = t.frame;
                turn(t.frame.forward, item.frame.forward);
                turn(t.frame.up, item.frame.up);
                item.frame.pos[0] = float(x) + ox;
                item.frame.pos[1] = float(y) + oy;
                item.frame.pos[2] = groundHeight(item.frame.pos[0], item.frame.pos[1]).value_or(t.frame.pos[2] - t.aboveGround) + t.aboveGround;
                frag.items.push_back(std::move(item));
            }
            const float origin[3] = {0, 0, 0};
            const size_t n = paste(frag, origin, false).size();
            if (thingsPlaced) *thingsPlaced = n;
            changed += n;
        }
    } catch (...) { endBatch(); throw; }
    endBatch();
    return changed;
}

size_t Document::pasteTerrainCells(const TerrainClip& clip, int x, int y, int quarterTurns, bool heights, bool themes, bool relative) {
    if (!hasTerrain() || stroke_ || clip.empty() || (!heights && !themes)) return 0;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    const int turns = ((quarterTurns % 4) + 4) % 4;
    const int ow = (turns % 2) ? clip.h : clip.w, oh = (turns % 2) ? clip.w : clip.h;
    auto next = std::make_unique<TerrainState>(*terrain_);
    // clip slot -> this palette's slot (by name; a missing theme takes a free slot)
    std::vector<int> slotFor(clip.themes.size(), -1);
    if (themes)
        for (size_t k = 0; k < clip.themes.size(); ++k) {
            const auto& t = clip.themes[k];
            for (size_t s = 0; s < next->palette.size() && slotFor[k] < 0; ++s)
                if (!t.name.empty() && next->palette[s].name == t.name) slotFor[k] = int(s);
            for (size_t s = 0; s < next->palette.size() && slotFor[k] < 0; ++s)
                if (next->palette[s].name.empty() && !t.name.empty()) { next->palette[s] = t; slotFor[k] = int(s); }
            if (slotFor[k] < 0) slotFor[k] = 0;
        }
    // relative: the vertex landing at the click (not source 0 once turned) sits on the ground there
    size_t anchor = 0;
    if (turns == 1) anchor = size_t(clip.h - 1) * clip.w;
    else if (turns == 2) anchor = size_t(clip.h - 1) * clip.w + size_t(clip.w - 1);
    else if (turns == 3) anchor = size_t(clip.w - 1);
    const float base = relative ? (sampleHeight(*terrain_, cx, cy, float(x), float(y)).value_or(0.0f) - clip.heights[anchor]) : 0.0f;
    size_t changed = 0;
    for (int oy = 0; oy < oh; ++oy)
        for (int ox = 0; ox < ow; ++ox) {
            // output (ox, oy) <- source vertex under the rotation
            int sx = ox, sy = oy;
            if (turns == 1) { sx = oy; sy = clip.h - 1 - ox; }
            else if (turns == 2) { sx = clip.w - 1 - ox; sy = clip.h - 1 - oy; }
            else if (turns == 3) { sx = clip.w - 1 - oy; sy = ox; }
            const int tx = x + ox, ty = y + oy;
            if (tx < 0 || ty < 0 || tx >= cx || ty >= cy) continue;
            const size_t si = size_t(sy) * clip.w + sx, ti = size_t(ty) * cx + tx;
            bool touched = false;
            if (heights) {
                const float v = std::clamp(clip.heights[si] + base, 0.0f, 2047.9999f);
                if (next->heights[ti] != v) { next->heights[ti] = v; touched = true; }
            }
            if (themes) {
                std::array<uint8_t, 3> idx{};
                for (int k = 0; k < 3; ++k) idx[k] = uint8_t(slotFor[clip.themeIndex[si][k]]);
                // two clip slots can land on one palette slot: merge like the paint brush does
                const auto blend = forge::terrain::replaceThemeInBlend({idx, clip.themeStrength[si]}, 255, 255);
                if (next->themeIndex[ti] != blend.indices || next->themeStrength[ti] != blend.strengths) {
                    next->themeIndex[ti] = blend.indices; next->themeStrength[ti] = blend.strengths; touched = true;
                }
            }
            changed += touched;
        }
    if (!changed) return 0;
    const bool themeChange = themes;
    pushUndo();
    terrain_ = std::shared_ptr<const TerrainState>(next.release());
    hf_.reset();
    writeTerrainToLevel();
    ++revision_;
    ++terrainRev_;
    if (themeChange) ++themeRev_;
    return changed;
}

// The vanilla Height Toolbox's Draw Paths (FableWin CEditWorldMap::EditDrawPathPenUndoable
// 0x02975ad0): a vertex is set when its perpendicular distance to the start->end line is
// within the radius AND its projection t lies in [0, 1] (GFGetShortestDistance2DSquared-
// BetweenPointAndLine returns t unclamped); height = start + t * (end - start). So the strip
// is a rectangle with square ends: nothing behind the start or past the end changes. A
// zero-length drag sets nothing (the vanilla helper asserts on equal points).
size_t Document::drawPath(float x0, float y0, float x1, float y1, float radius) {
    if (!hasTerrain() || stroke_ || !(radius > 0)) return 0;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    const auto h0 = sampleHeight(*terrain_, cx, cy, x0, y0), h1 = sampleHeight(*terrain_, cx, cy, x1, y1);
    if (!h0 || !h1) return 0;
    const float sx = x1 - x0, sy = y1 - y0, len2 = sx * sx + sy * sy;
    if (!(len2 > 0)) return 0;
    const int vx0 = std::max(0, int(std::floor(std::min(x0, x1) - radius))), vx1 = std::min(cx - 1, int(std::ceil(std::max(x0, x1) + radius)));
    const int vy0 = std::max(0, int(std::floor(std::min(y0, y1) - radius))), vy1 = std::min(cy - 1, int(std::ceil(std::max(y0, y1) + radius)));
    std::vector<VertexHeight> edits;
    for (int y = vy0; y <= vy1; ++y)
        for (int x = vx0; x <= vx1; ++x) {
            const float t = ((float(x) - x0) * sx + (float(y) - y0) * sy) / len2;
            if (t < 0.0f || t > 1.0f) continue;
            const float dx = float(x) - (x0 + sx * t), dy = float(y) - (y0 + sy * t);
            if (dx * dx + dy * dy > radius * radius) continue;
            const float h = *h0 + (*h1 - *h0) * t;
            if (terrain_->heights[size_t(y) * cx + x] != h) edits.push_back({x, y, h});
        }
    if (edits.empty() || !setVertexHeights(edits)) return 0;
    return edits.size();
}

bool Document::themesDirty() const {
    if (!terrain_ || !savedTerrain_ || terrain_ == savedTerrain_) return false;
    return terrain_->themeIndex != savedTerrain_->themeIndex || terrain_->themeStrength != savedTerrain_->themeStrength;
}

bool Document::terrainDirty() const {
    if (!terrain_ || !savedTerrain_) return false;
    if (soundListGrew_) return true;
    if (terrain_ == savedTerrain_) return false;
    if (terrain_->heights != savedTerrain_->heights || terrain_->walkable != savedTerrain_->walkable || terrain_->cameraPassable != savedTerrain_->cameraPassable ||
        terrain_->themeIndex != savedTerrain_->themeIndex || terrain_->themeStrength != savedTerrain_->themeStrength) return true;
    if (terrain_->atmosIndex != savedTerrain_->atmosIndex || terrain_->atmosStrength != savedTerrain_->atmosStrength ||
        terrain_->sound != savedTerrain_->sound) return true;
    if (terrain_->atmosPalette.size() != savedTerrain_->atmosPalette.size()) return true;
    for (size_t i = 0; i < terrain_->atmosPalette.size(); ++i)
        if (terrain_->atmosPalette[i].name != savedTerrain_->atmosPalette[i].name || terrain_->atmosPalette[i].value != savedTerrain_->atmosPalette[i].value) return true;
    if (terrain_->palette.size() != savedTerrain_->palette.size()) return true;
    for (size_t i = 0; i < terrain_->palette.size(); ++i)
        if (terrain_->palette[i].name != savedTerrain_->palette[i].name || terrain_->palette[i].value != savedTerrain_->palette[i].value) return true;
    return false;
}

std::optional<float> Document::sampleHeight(const TerrainState& t, int cx, int cy, float x, float y) {
    if (!(x >= 0 && y >= 0) || x > float(cx - 1) || y > float(cy - 1)) return std::nullopt;
    const int x0 = std::min(int(x), cx - 1), y0 = std::min(int(y), cy - 1);
    const int x1 = std::min(x0 + 1, cx - 1), y1 = std::min(y0 + 1, cy - 1);
    const float fx = x - float(x0), fy = y - float(y0);
    auto h = [&](int xx, int yy) { return t.heights[size_t(yy) * cx + xx]; };
    return (h(x0, y0) * (1 - fx) + h(x1, y0) * fx) * (1 - fy) + (h(x0, y1) * (1 - fx) + h(x1, y1) * fx) * fy;
}

std::optional<float> Document::terrainHeight(float x, float y) const {
    const TerrainState* t = stroke_ && working_ ? working_.get() : terrain_.get();
    if (!t || !level_) return std::nullopt;
    return sampleHeight(*t, level_->cellsX(), level_->cellsY(), x, y);
}

int Document::paletteSlotOf(const std::string& name) const {
    if (!level_) return -1;
    const auto& pal = level_->groundThemes();
    for (size_t i = 0; i < pal.size(); ++i) if (pal[i].name == name) return int(i);
    return -1;
}

int Document::addGroundTheme(const std::string& name, uint32_t defIndex) {
    if (!level_ || name.empty() || name.size() >= 128) return -1;
    if (const int have = paletteSlotOf(name); have >= 0) return have;
    const auto& pal = level_->groundThemes();
    // slot 0 is "no theme" and slot 1 INVALID_THEME_STANDIN on every retail map
    // (the debug editor's CMap::AddThemeDefIndexToPalette starts at 2; a theme
    // put in slot 0 does not draw in-game -- tried)
    for (size_t i = 2; i < pal.size(); ++i) {
        if (!pal[i].name.empty()) continue;
        if (terrain_ && !stroke_) {
            pushUndo();
            auto next = std::make_unique<TerrainState>(*terrain_);
            if (next->palette.size() != pal.size()) next->palette = pal;
            next->palette[i] = forge::lev::GroundTheme{name, defIndex};
            terrain_ = std::shared_ptr<const TerrainState>(next.release());
            ++terrainRev_;
        }
        level_->setGroundTheme(i, name, defIndex);
        ++revision_;
        return int(i);
    }
    return -1;
}

size_t Document::reseatThings(const TerrainState& before, float tolerance) {
    return reseatGroundedThings(before, tolerance, true);
}

size_t Document::reseatGroundedThings(const TerrainState& before, float tolerance, bool recordUndo) {
    if (!hasTerrain() || stroke_) return 0;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    if (before.heights.size() != terrain_->heights.size()) return 0;
    struct Move { size_t index; Frame frame; bool rotated; };
    std::vector<Move> moves;
    const auto& things=file_.things();
    std::vector<std::optional<float>> groundedDelta(things.size());
    using Rotation = std::array<float,9>;
    const Rotation identity{1,0,0,0,1,0,0,0,1};
    std::vector<Rotation> rotations(things.size(),identity);
    // Canonical terrain frames avoid accumulating yaw when a slope is edited
    // repeatedly. Their relative rotation preserves an authored lean/heading.
    // This is Forge's automatic prop-follow behavior, not a native editor port.
    auto slopeFrame=[&](const TerrainState& t,float x,float y) {
        const int ix=std::min(int(x),cx-2), iy=std::min(int(y),cy-2);
        const float fx=x-ix, fy=y-iy;
        auto h=[&](int xx,int yy) { return t.heights[size_t(yy)*cx+xx]; };
        float n[3]={-((h(ix+1,iy)-h(ix,iy))*(1-fy)+(h(ix+1,iy+1)-h(ix,iy+1))*fy),
                    -((h(ix,iy+1)-h(ix,iy))*(1-fx)+(h(ix+1,iy+1)-h(ix+1,iy))*fx),1};
        normalise3(n);
        const float d=1+n[2];
        return Rotation{1-n[0]*n[0]/d,-n[0]*n[1]/d,n[0],
                        -n[0]*n[1]/d,1-n[1]*n[1]/d,n[1],-n[0],-n[1],n[2]};
    };
    auto rotate=[](const Rotation& r,float v[3]) {
        const float p[3]={v[0],v[1],v[2]};
        for(int row=0;row<3;++row) v[row]=r[row*3]*p[0]+r[row*3+1]*p[1]+r[row*3+2]*p[2];
    };
    for (size_t i = 0; i < things.size(); ++i) {
        if (isLocked(i)) continue;
        Frame f;
        if (!frameOf(i, f)) continue;
        const auto was = sampleHeight(before, cx, cy, f.pos[0], f.pos[1]);
        const auto now = sampleHeight(*terrain_, cx, cy, f.pos[0], f.pos[1]);
        if (!was || !now) continue;
        if (std::fabs(f.pos[2] - *was) > tolerance) continue;   // was floating / sunk on purpose
        if (recordUndo && std::fabs(f.pos[2] - *now) <= std::fabs(f.pos[2] - *was)) continue;
        bool slopeChanged=false;
        // Repair uses a saved height baseline and must never apply the same
        // tilt twice. Automatic edits have an exact before/after pair.
        if (!recordUndo && cx>1 && cy>1 && things[i].type=="Object" &&
            !things[i].findCtc("CTCPhysicsNavigator") &&
            !things[i].definitionType().starts_with("BUILDING_") &&
            !things[i].definitionType().starts_with("CREATURE_")) {
            const auto a=slopeFrame(before,f.pos[0],f.pos[1]);
            const auto b=slopeFrame(*terrain_,f.pos[0],f.pos[1]);
            for(size_t k=0;k<a.size();++k) slopeChanged |= std::fabs(a[k]-b[k])>1e-6f;
            if(slopeChanged) for(int row=0;row<3;++row) for(int col=0;col<3;++col) {
                float value=0;
                for(int k=0;k<3;++k) value+=b[row*3+k]*a[col*3+k];
                rotations[i][row*3+col]=value;
            }
        }
        if (std::fabs(*was-*now)<1e-4f && !slopeChanged) continue;
        groundedDelta[i]=*now-*was;
    }
    // Ownership is a rigid placement relationship: a floating or locked child
    // follows its grounded parent, and a grounded child does not move twice.
    const auto parents=validOwnedParents(things);
    std::vector<std::vector<size_t>> children(things.size());
    for (size_t i=0;i<parents.size();++i) if (parents[i]) children[*parents[i]].push_back(i);
    std::vector<bool> visited(things.size(),false);
    auto moveTree=[&](size_t root) {
        const float delta=*groundedDelta[root];
        Frame pivot;
        frameOf(root,pivot);
        const auto& rotation=rotations[root];
        const bool rotated=rotation!=identity;
        std::vector<size_t> queue{root};
        visited[root]=true;
        for (size_t next=0;next<queue.size();++next) {
            const size_t i=queue[next];
            Frame f;
            if (frameOf(i,f)) {
                if(rotated) {
                    for(int k=0;k<3;++k) f.pos[k]-=pivot.pos[k];
                    rotate(rotation,f.pos);
                    for(int k=0;k<3;++k) f.pos[k]+=pivot.pos[k];
                    rotate(rotation,f.forward);
                    rotate(rotation,f.up);
                }
                f.pos[2]+=delta;
                moves.push_back({i,f,rotated});
            }
            for (size_t child:children[i]) if (!visited[child]) {
                visited[child]=true;
                queue.push_back(child);
            }
        }
    };
    std::vector<size_t> ancestorVisit(things.size(),0);
    size_t visitEpoch=0;
    for (size_t i=0;i<things.size();++i) if (groundedDelta[i]) {
        bool hasGroundedAncestor=false;
        ++visitEpoch;
        for (auto parent=parents[i];parent && ancestorVisit[*parent]!=visitEpoch;parent=parents[*parent]) {
            if (groundedDelta[*parent]) { hasGroundedAncestor=true; break; }
            ancestorVisit[*parent]=visitEpoch;
        }
        if (!hasGroundedAncestor && !visited[i]) moveTree(i);
    }
    for (size_t i=0;i<things.size();++i) if (groundedDelta[i] && !visited[i]) moveTree(i);
    if (moves.empty()) return 0;
    if (recordUndo) pushUndo();
    for (const auto& m : moves) {
        if(m.rotated) writeFrame(file_,m.index,m.frame,worldX_,worldY_);
        else {
            const auto ctc=physicsOf(file_.things()[m.index])->name;
            file_.setCtcProperty(m.index,ctc,"PositionZ",formatFloat(m.frame.pos[2]));
            writeInitialPosition(file_,m.index,m.frame.pos,worldX_,worldY_);
        }
    }
    ++revision_;
    return moves.size();
}

namespace {
bool backupOnce(const fs::path& p, std::string& error) { return albion::backups::backupOnce(p, error); }   // <file>.forge-orig, once
} // namespace

void Document::prepareTerrainLoose(const fs::path& prepared, std::shared_ptr<forge::lev::File>& nextLevel,
                                   bool& patchedNavigation, std::vector<std::string>& notes) const {
    level_->save(prepared);
    nextLevel = level_;
    patchedNavigation = false;

    // navigation: patch the retail quadtree for the cells whose walkable byte changed
    std::vector<std::pair<int, int>> changed;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    if (navWalkable_.size() == terrain_->walkable.size())
        for (int y = 0; y < cy; ++y)
            for (int x = 0; x < cx; ++x)
                if (navWalkable_[size_t(y) * cx + x] != terrain_->walkable[size_t(y) * cx + x]) changed.push_back({x, y});
    if (!changed.empty() && !level_->navSections().empty()) {
        const auto saved = forge::lev::File::open(prepared);
        auto nav = forge::navmesh::parseNavigation(saved);
        const auto st = forge::navmesh::patchWalkability(nav, saved, changed);
        const auto bytes = forge::navmesh::emitNavigation(saved, nav);
        std::ofstream out(prepared, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        out.close();
        if (!out) throw std::runtime_error("cannot write " + prepared.string());
        nextLevel = std::make_shared<forge::lev::File>(forge::lev::File::open(prepared));
        patchedNavigation = true;
        {
            char line[256];
            std::snprintf(line, sizeof line, "navigation: %zu cell(s) changed -> %zu leaves removed, %zu added, %zu split, %zu region(s) added, %zu merged (%zu section(s))",
                          changed.size(), st.leavesRemoved, st.leavesAdded, st.nodesSplit, st.regionsAdded, st.regionsMerged, nav.sections.size());
            notes.push_back(line);
            if (st.cellsSkipped) notes.push_back("navigation: " + std::to_string(st.cellsSkipped) + " opened cell(s) already had nav coverage");
        }
    }
}

bool Document::saveTerrainLoose(const fs::path& gameRoot, std::string& error, std::vector<std::string>* notes) {
    if (!hasTerrain()) { error = "no terrain loaded"; return false; }
    const fs::path path = external() && packOut_.empty() ? externalLev_ : gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName_ + ".lev");
    try {
        albion::detail::PendingBanks pending(path.parent_path(), ".forge-lev-save-");
        const auto prepared = pending.prepare(path.filename());
        std::shared_ptr<forge::lev::File> nextLevel;
        bool patchedNavigation = false;
        std::vector<std::string> preparedNotes;
        prepareTerrainLoose(prepared, nextLevel, patchedNavigation, preparedNotes);
        // Recovery metadata and document state follow successful serialization.
        if (packOut_.empty()) {
            if (!backupOnce(path, error)) return false;
            if (!fs::exists(path)) albion::backups::markCreated(path);
        }
        if (!pending.install(false, error)) return false;
        level_ = std::move(nextLevel);
        if (patchedNavigation) navWalkable_ = terrain_->walkable;
        savedTerrain_ = terrain_;
        soundListGrew_ = false;
        if (notes) notes->insert(notes->end(), preparedNotes.begin(), preparedNotes.end());
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

Document Document::terrainWriteSnapshot() const {
    if (stroke_) throw std::logic_error("finish the terrain stroke before taking a write snapshot");
    Document out;
    out.mapName_ = mapName_;
    out.externalWld_ = externalWld_;
    out.externalLev_ = externalLev_;
    out.terrain_ = terrain_;
    out.savedTerrain_ = savedTerrain_;
    out.level_ = level_ ? std::make_shared<forge::lev::File>(*level_) : nullptr;
    out.navWalkable_ = navWalkable_;
    out.soundListGrew_ = soundListGrew_;
    out.terrainSession_ = terrainSession_;
    return out;
}

bool Document::acceptTerrainWrite(const Document& written) {
    if (terrainSession_ != written.terrainSession_ || !level_ || !written.level_) return false;
    savedTerrain_ = written.savedTerrain_;
    soundListGrew_ = level_->soundThemes() != written.level_->soundThemes();
    // Keep the current LEV and its navWalkable_ together. If the worker patched
    // navigation, the next write can apply that patch again to this older nav
    // baseline. Replacing the LEV here would discard later edits/sound names.
    return true;
}

bool Document::deployTerrain(const fs::path& gameRoot, std::vector<std::string>& notes, std::string& error,
                             const forge::terraintex::ThemeLibrary* library,
                             const std::function<void(const std::string&)>& progress) {
    // another world's map: its .lev is the file the game loads; there is no WAD
    // entry, and its static map lives in that world's own .stb, which the vanilla
    // editor bakes (FableForge's STB writer is FinalAlbion_RT.stb only)
    if (external()) {
        if (!saveTerrainLoose(gameRoot, error, &notes)) return false;
        notes.push_back("wrote " + externalLev_.string() + "; the world's own .stb was not re-baked");
        return true;
    }
    return deployTerrainSteps(gameRoot, notes, error, library, progress);
}

bool Document::deployTerrainSteps(const fs::path& gameRoot, std::vector<std::string>& notes, std::string& error,
                                  const forge::terraintex::ThemeLibrary* library,
                                  const std::function<void(const std::string&)>& progress) {
    const auto stage = [&](const char* s) { if (progress) progress(s); };
    if (!hasTerrain()) { error = "no terrain loaded"; return false; }
    bool themesChanged = themesDirty();
    std::shared_ptr<const TerrainState> before = savedTerrain_;   // the ground the chunk's foliage sits on
    std::vector<std::string> preparedNotes;
    try {
        const bool inPlace = packOut_.empty();
        if (inPlace && backups::gameRunningIn(gameRoot)) { error = "Fable is running from this install; quit to the desktop first"; return false; }
        const fs::path outputRoot = inPlace ? gameRoot : packOut_;
        detail::PendingBanks pending(outputRoot, ".forge-terrain-deploy-");
        if (!packOut_.empty()) {
            // Each pack bake starts from gameRoot's STB, not the previous pack
            // chunk. Its height/theme baseline must come from that same root.
            const auto bytes = forge::levelstore::requireFile(forge::levelstore::detect(gameRoot), mapName_ + ".lev");
            const albion::detail::TemporaryDirectory scratch("terrain-baseline-");
            const fs::path path = scratch.path() / "baseline.lev";
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            out.close();
            if (!out) throw std::runtime_error("cannot write terrain baseline " + path.string());
            Document baseline;
            if (!baseline.loadLevel(path, error)) return false;
            if (baseline.cellsX() != cellsX() || baseline.cellsY() != cellsY()) {
                error = "pack terrain dimensions differ from the source bank";
                return false;
            }
            before = baseline.terrain_;
            themesChanged = terrain_->themeIndex != before->themeIndex || terrain_->themeStrength != before->themeStrength;
            for (size_t i = 0; i < terrain_->palette.size() && !themesChanged; ++i)
                themesChanged = i >= before->palette.size() || terrain_->palette[i].name != before->palette[i].name ||
                                terrain_->palette[i].value != before->palette[i].value;
        }
        if (themesChanged && !library) { error = "ground themes were painted but the ENGINE_THEME library is not loaded (textures not ready)"; return false; }
        // 1. prepare the loose .lev (also the bytes for the WAD).
        stage("preparing the .lev (navigation patch)");
        const fs::path levRelative = fs::path("data") / "Levels" / "FinalAlbion" / (mapName_ + ".lev");
        const fs::path loose = outputRoot / levRelative;
        const auto levPrepared = pending.prepare(levRelative);
        std::shared_ptr<forge::lev::File> nextLevel;
        bool patchedNavigation = false;
        prepareTerrainLoose(levPrepared, nextLevel, patchedNavigation, preparedNotes);
        const std::string levBytes = readFile(levPrepared);
        preparedNotes.push_back("wrote " + loose.string());
        const auto finish = [&]() {
            if (inPlace && backups::gameRunningIn(gameRoot)) { error = "Fable started during terrain deployment; quit to the desktop first"; return false; }
            if (inPlace && !fs::exists(loose)) {
                fs::create_directories(loose.parent_path());
                backups::markCreated(loose);
            }
            stage("installing prepared terrain files");
            if (!pending.install(inPlace, error)) return false;
            level_ = std::move(nextLevel);
            if (patchedNavigation) navWalkable_ = terrain_->walkable;
            savedTerrain_ = terrain_;
            soundListGrew_ = false;
            notes.insert(notes.end(), preparedNotes.begin(), preparedNotes.end());
            return true;
        };

        // 2. FinalAlbion.wad entry (patched in place when the size is unchanged,
        //    relocated to the end of the payload after a navigation patch)
        const fs::path wad = gameRoot / "data" / "Levels" / "FinalAlbion.wad";
        if (packOut_.empty() && fs::exists(wad)) {   // a pack's .lev is repacked by the composer
            const auto archive = forge::wad::Archive::open(wad);
            std::string entryName;
            const std::string want = lower(mapName_) + ".lev";
            for (const auto& e : archive.entries())
                if (lower(fs::path(e.name).filename().string()) == want) { entryName = e.name; break; }
            if (entryName.empty()) { error = mapName_ + ".lev is not in FinalAlbion.wad"; return false; }
            std::map<std::string, std::vector<uint8_t>> rep;
            rep[entryName] = std::vector<uint8_t>(levBytes.begin(), levBytes.end());
            const auto temp = pending.prepare(fs::path("data") / "Levels" / "FinalAlbion.wad");
            forge::wad::repack(wad, rep, temp);
            const auto verified = forge::wad::Archive::open(temp);
            const auto found = std::find_if(verified.entries().begin(), verified.entries().end(),
                [&](const forge::wad::Entry& e) { return e.name == entryName; });
            if (found == verified.entries().end() || verified.read(*found) != rep.at(entryName))
                throw std::runtime_error("prepared terrain WAD failed read-back verification");
            preparedNotes.push_back("replaced " + entryName + " in FinalAlbion.wad");
        }

        // 3. the terrain chunk in FinalAlbion_RT.stb, re-baked from the edited heights
        stage(themesChanged ? "re-baking the terrain chunk (heights + painted themes)" : "re-baking the terrain chunk");
        const fs::path stb = gameRoot / "data" / "Levels" / "FinalAlbion_RT.stb";
        if (!fs::exists(stb)) { error = "no " + stb.string(); return false; }
        const auto archive = forge::stb::Archive::open(stb);
        const forge::stb::StaticMap* map = nullptr;
        const std::string wantLev = lower(mapName_) + ".lev";
        for (const auto& m : archive.staticMaps())
            if (lower(fs::path(m.levelName).filename().string()) == wantLev) { map = &m; break; }
        if (!map) { error = mapName_ + " has no static map in FinalAlbion_RT.stb"; return false; }
        const auto record = archive.readStaticMapRecord(*map);
        if (record.size() < forge::stbinfo::kInfoBlockSize) { error = "static-map record too short"; return false; }
        uint32_t bankIndex = 0; std::memcpy(&bankIndex, record.data() + 4, 4);
        const forge::stb::Entry* entry = nullptr;
        for (const auto& e : archive.entries()) if (e.id == bankIndex) { entry = &e; break; }
        if (!entry) { error = "static-map bank entry " + std::to_string(bankIndex) + " not found"; return false; }
        const auto chunk = archive.read(*entry);
        // world placement from the WLD
        const auto world = forge::wld::File::parse(gameRoot / "data" / "Levels" / "FinalAlbion.wld");
        const forge::wld::Map* wm = nullptr;
        for (const auto& m : world.maps())
            if (lower(fs::path(m.levelName).filename().string()) == wantLev) { wm = &m; break; }
        if (!wm) { error = mapName_ + " is not placed in FinalAlbion.wld"; return false; }
        forge::stbbake::HeightfieldBakeOptions opt;
        opt.requireCanonicalSize = false;
        std::shared_ptr<LodAlbedo> lodAlbedo;
        if (themesChanged) {
            // regenerate every foreground layer mesh from the LEV themes (the
            // editor's ReadThemesAndCreateLayers): new material regions get
            // their own passes, direction masks come from the new normals
            opt.rebuildTopology = true;
            opt.rebuildDirectionMask = true;
            opt.themes = forge::terraintex::paletteMaterials(*level_, *library);
            opt.themes.resize(256);
            // the distant-LOD textures follow the painted themes (same size as the
            // ones they replace; retail chunks keep theirs where sizes differ)
            lodAlbedo = std::make_shared<LodAlbedo>(bakeLodAlbedo(gameRoot, *level_));
            opt.backgroundTextures = lodTextureProvider(*lodAlbedo);
            preparedNotes.push_back("ground themes painted: layer meshes rebuilt from the LEV palette, distant-LOD textures re-baked");
        }
        // Neighbouring maps (every map a region owning this one contains or
        // sees, whose placement touches ours) supply the shared-edge samples,
        // as the retail bake did. Their LEVs come from the WAD (loose copies
        // win, like everywhere else) via a temp folder because lev::File is
        // path based.
        std::vector<std::unique_ptr<forge::lev::File>> neighbourFiles;
        {
            std::set<std::string> candidates;
            const std::string mine = lower(wm->levelName);
            for (const auto& region : world.regions()) {
                bool owns = false;
                for (const auto& n : region.containsMaps) owns = owns || lower(n) == mine;
                if (!owns) continue;
                for (const auto& n : region.containsMaps) candidates.insert(lower(n));
                for (const auto& n : region.seesMaps) candidates.insert(lower(n));
            }
            candidates.erase(mine);
            detail::TemporaryDirectory neighbourScratch("terrain-neighbours-");
            const fs::path& tmp = neighbourScratch.path();
            std::unique_ptr<forge::wad::Archive> wadArchive;
            for (const auto& name : candidates) {
                const forge::wld::Map* nm = nullptr;
                for (const auto& m : world.maps()) if (lower(m.levelName) == name) { nm = &m; break; }
                if (!nm) continue;
                // quick placement test with the WLD alone (LEV sizes are unknown until loaded; use a generous window)
                if (nm->mapX > wm->mapX + level_->width() + 1 || nm->mapY > wm->mapY + level_->height() + 1) continue;
                if (nm->mapX + 512 < wm->mapX - 1 || nm->mapY + 512 < wm->mapY - 1) continue;
                const std::string leaf = fs::path(nm->levelName).filename().string();
                fs::path levFile = gameRoot / "data" / "Levels" / "FinalAlbion" / leaf;
                if (!fs::exists(levFile)) {
                    if (!wadArchive) wadArchive = std::make_unique<forge::wad::Archive>(forge::wad::Archive::open(wad));
                    const std::string wantLeaf = lower(leaf);
                    for (const auto& e : wadArchive->entries())
                        if (lower(fs::path(e.name).filename().string()) == wantLeaf) {
                            const auto bytes = wadArchive->read(e);
                            levFile = tmp / leaf;
                            std::ofstream(levFile, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                            break;
                        }
                }
                if (!fs::exists(levFile)) continue;
                try {
                    auto nf = std::make_unique<forge::lev::File>(forge::lev::File::open(levFile));
                    const int right = nm->mapX + nf->width() - 1, bottom = nm->mapY + nf->height() - 1;
                    if (right < wm->mapX - 1 || nm->mapX > wm->mapX + level_->width() || bottom < wm->mapY - 1 || nm->mapY > wm->mapY + level_->height()) continue;
                    opt.neighbors.push_back({nf.get(), nm->mapX, nm->mapY});
                    neighbourFiles.push_back(std::move(nf));
                    preparedNotes.push_back("neighbour " + leaf + " at (" + std::to_string(nm->mapX) + "," + std::to_string(nm->mapY) + ")");
                } catch (const std::exception&) {}
            }
        }
        opt.deferOversizedPatches = true;   // tall edits: those patches grow below instead of failing the bake
        opt.rebakeLodPatches = true;        // and the distant-view LOD patches follow the new ground
        const auto baked = forge::stbbake::bakeHeightfield(chunk, *level_, wm->mapX, wm->mapY, opt);
        for (const auto& n : baked.notes) if (n.rfind("foreground frame", 0) != 0) preparedNotes.push_back(n);
        if (baked.chunk.size() != chunk.size()) { error = "baked chunk changed size (" + std::to_string(baked.chunk.size()) + " vs " + std::to_string(chunk.size()) + ")"; return false; }
        // camera height bounds in the common record
        float minH = 1e30f, maxH = -1e30f;
        for (float h : terrain_->heights) { minH = std::min(minH, h); maxH = std::max(maxH, h); }
        auto info = forge::stbinfo::readInfoBlock(record.data());
        forge::stbbake::setRetailCameraHeightBounds(info, minH, maxH);
        const auto encoded = forge::stbinfo::writeInfoBlock(info);
        std::vector<uint8_t> outChunk = baked.chunk, outRecord = record;
        std::copy(encoded.begin(), encoded.end(), outRecord.begin());
        // patches whose new heights outgrew their fixed slot: written here, where
        // the frame may grow (its file block is re-laid or appended and every LOD
        // record rebased). Before the foliage pass: it keys on the bake's frame offsets.
        if (!baked.deferred.empty()) {
            RelocateReport rr;
            if (!replacePatchVertices(outChunk, outRecord, baked.deferred, rr, error)) { error = "grown terrain patches: " + error; return false; }
            RelocateReport check; std::string cerr;
            if (!auditChunk(outChunk, outRecord, wm->mapX, wm->mapY, level_->width(), level_->height(), check, cerr)) { error = "grown terrain patches produced a chunk that does not parse (" + cerr + ")"; return false; }
            const size_t grown = baked.deferred.size() - baked.lodPatches;
            if (baked.lodPatches) preparedNotes.push_back(std::to_string(baked.lodPatches) + " distant-view LOD patch(es) re-sampled from the new ground");
            if (grown) preparedNotes.push_back(std::to_string(grown) + " terrain patch(es) outgrew their slot and were re-laid");
            if (!rr.notes.empty()) preparedNotes.push_back(rr.notes.back());
        }
        // the chunk's trees and grass ride the ground change (bounds grow by the
        // largest change); a re-laid foliage section can grow the chunk
        bool foliageRode = false;
        if (before && before->heights.size() == terrain_->heights.size() && outRecord.size() > 0x79 && outRecord[0x79]) {
            float slack = 0;
            for (size_t i = 0; i < before->heights.size(); ++i) slack = std::max(slack, std::fabs(terrain_->heights[i] - before->heights[i]));
            if (slack > 1e-4f) {
                const int cx = level_->cellsX(), cy = level_->cellsY();
                const float ox = float(wm->mapX), oy = float(wm->mapY);
                const TerrainState& after = *terrain_;
                const TerrainState& was = *before;
                auto dz = [&](float wx, float wy) -> float {
                    const auto b = sampleHeight(was, cx, cy, wx - ox, wy - oy);
                    const auto a2 = sampleHeight(after, cx, cy, wx - ox, wy - oy);
                    return b && a2 ? *a2 - *b : 0.0f;
                };
                RelocateReport rr;
                if (!reseatFoliageZ(outChunk, outRecord, dz, slack, rr, error)) { error = "foliage re-seat: " + error; return false; }
                RelocateReport check; std::string cerr;
                if (!auditChunk(outChunk, outRecord, wm->mapX, wm->mapY, level_->width(), level_->height(), check, cerr)) { error = "foliage re-seat produced a chunk that does not parse (" + cerr + ")"; return false; }
                foliageRode = true;
                preparedNotes.push_back("foliage re-seated on the new ground (" + std::to_string(rr.groupFrames) + " cache groups, bounds grown by " + std::to_string(slack) + ")");
            }
        }
        if (!packOut_.empty()) {
            // the pack keeps the map's chunk + record; the composer writes them into the STB it builds
            const fs::path dir = "stb";
            auto put = [&](const fs::path& f, const std::vector<uint8_t>& b) {
                const auto prepared = pending.prepare(f);
                std::ofstream o(prepared, std::ios::binary | std::ios::trunc);
                o.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
                o.close();
                if (!o) throw std::runtime_error("cannot write " + prepared.string());
            };
            put(dir / (mapName_ + ".chunk"), outChunk);
            put(dir / (mapName_ + ".record"), outRecord);
            preparedNotes.push_back("re-baked terrain chunk " + std::to_string(outChunk.size()) + " bytes into the pack (stb/" + mapName_ + ".chunk + .record)");
            return finish();
        }
        const auto tmp = pending.prepare(fs::path("data") / "Levels" / "FinalAlbion_RT.stb");
        if (outChunk.size() == chunk.size()) {
            // Patch a copy, then replace the bank in one step. A failed or
            // interrupted write must not leave a half-updated retail STB.
            fs::copy_file(stb, tmp, fs::copy_options::overwrite_existing);
            std::fstream io(tmp, std::ios::binary | std::ios::in | std::ios::out);
            if (!io) { error = "cannot open " + tmp.string() + " for writing"; std::error_code ec; fs::remove(tmp,ec); return false; }
            io.seekp(std::streamoff(entry->offset));
            io.write(reinterpret_cast<const char*>(outChunk.data()), std::streamsize(outChunk.size()));
            io.seekp(std::streamoff(map->absoluteOffset));
            io.write(reinterpret_cast<const char*>(outRecord.data()), std::streamsize(outRecord.size()));
            io.close();
            if (!io) { error = "write to " + tmp.string() + " failed"; std::error_code ec; fs::remove(tmp,ec); return false; }
        } else {
            std::vector<forge::stb::StaticMapAppend> batch;
            batch.push_back({map->levelName, entry->name, outChunk, outRecord});
            forge::stb::replaceStaticMapsRelayout(stb, tmp, batch);
        }
        preparedNotes.push_back("re-baked terrain chunk " + std::to_string(outChunk.size()) + " bytes (" + std::to_string(baked.patches) + " patches, " + std::to_string(baked.foregroundFrames) + " layer frames) into FinalAlbion_RT.stb" + (outChunk.size() == chunk.size() ? "" : " (chunk re-laid)"));
        const auto verified = forge::stb::Archive::open(tmp);
        const auto* savedEntry = verified.findEntry(entry->name);
        if (!savedEntry || verified.read(*savedEntry) != outChunk)
            throw std::runtime_error("prepared terrain STB failed read-back verification");
        (void)foliageRode;
        return finish();
    } catch (const std::exception& e) {
        error = e.what(); return false;
    }
}


bool Document::isLocked(size_t index) const {
    if (index>=file_.things().size()) return false;
    const auto* editor=file_.things()[index].findCtc("CTCEditor");
    if (!editor) return false;
    for (const auto& property:editor->properties)
        if (lower(property.key)=="lockedinplace") return lower(property.value)=="true" || property.value=="1";
    return false;
}

bool Document::setLocked(size_t index,bool locked) {
    if (index>=file_.things().size() || !file_.things()[index].findCtc("CTCEditor")) return false;
    if (isLocked(index)==locked) return true;
    pushUndo();
    file_.setCtcProperty(index,"CTCEditor","LockedInPlace",locked?"TRUE":"FALSE");
    ++revision_;
    return true;
}

void Document::setFrame(size_t index, const Frame& frame) {
    if (index >= file_.things().size()) throw std::out_of_range("setFrame: bad thing index");
    if (isLocked(index)) return;
    if (!physicsOf(file_.things()[index])) throw std::runtime_error("setFrame: thing has no physics block");
    pushUndo();
    writeFrame(file_, index, frame,worldX_,worldY_);
    ++revision_;
}

void Document::setOwnedFrame(size_t index, const Frame& frame) {
    if (index >= file_.things().size()) throw std::out_of_range("setOwnedFrame: bad thing index");
    if (!physicsOf(file_.things()[index])) throw std::runtime_error("setOwnedFrame: thing has no physics block");
    pushUndo();
    writeFrame(file_, index, frame,worldX_,worldY_);
    ++revision_;
}

bool Document::setHeight(size_t index, float height) {
    Frame frame;
    if (!std::isfinite(height) || isLocked(index) || !frameOf(index,frame)) return false;
    if (!std::isfinite(frame.pos[0]) || !std::isfinite(frame.pos[1]) || !std::isfinite(frame.pos[2])) return false;
    const auto ground=groundHeight(frame.pos[0],frame.pos[1]);
    if (!ground || !std::isfinite(*ground)) return false;
    height=std::max(height,*ground);
    if (frame.pos[2]==height) return true;
    frame.pos[2]=height;
    setFrame(index,frame);
    return true;
}

void Document::setProperty(size_t index, const std::string& key, const std::string& value) {
    if (index >= file_.things().size()) throw std::out_of_range("setProperty: bad thing index");
    if (isLocked(index) && lower(key)=="objectscale") return;
    pushUndo();
    file_.setThingProperty(index, key, value);
    ++revision_;
}

namespace {
struct LinkKind { const char* ctc; const char* field; const char* label; const char* wants; const char* offeredOn = nullptr; };
// the UID links retail .tng files use (counted over the dev tree's FinalAlbion: OwnerUID 2434,
// VillageUID 2347, EntranceConnectedToUID 200, ReceptorUID 76, route 46, home/work/wife 4-6)
constexpr LinkKind kLinkKinds[] = {
    {"CTCVillageMember", "VillageUID", "Village", "a village"},
    {"CTCOwnedEntity", "OwnerUID", "Owned by", "any thing"},
    {"", "HomeBuildingUID", "Lives in", "a building", "aicreature"},
    {"", "WorkBuildingUID", "Works in", "a building", "aicreature"},
    {"", "FatherCreatureUID", "Father", "a creature", "aicreature"},
    {"", "MotherCreatureUID", "Mother", "a creature", "aicreature"},
    {"", "SpouseCreatureUID", "Spouse", "a creature", "aicreature"},
    {"CTCBuyableHouse", "WifeLivingHereUID", "Wife living here", "a creature"},
    {"CTCDRegionExit", "EntranceConnectedToUID", "Region exit to entrance", "a region entrance"},
    {"CTCActionUseScriptedHook", "EntranceConnectedToUID", "Region exit to entrance", "a region entrance"},
    {"CTCActivationTrigger", "ReceptorUID", "Activates", "an activation receptor"},
    {"CTCPreCalculatedNavigationRoute", "ThingToCalculateRouteToUID", "Route to", "any thing"},
    // the vanilla property dialog picks this one by script name (CTCActionUseScriptedHook "Camera Track")
    {"CTCActionUseScriptedHook", "CameraTrackUID", "Camera track", "a camera track"},
};
bool hasCtcPrefix(const forge::tng::Thing& t, std::string_view prefix) {
    for (const auto& b : t.ctcBlocks)
        if (b.name.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}
} // namespace

Document::PropertyRow::Kind Document::kindOf(const std::string& value) {
    using K = PropertyRow::Kind;
    if (value == "TRUE" || value == "FALSE") return K::Bool;
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') return K::String;
    if (value.empty()) return K::Raw;
    size_t i = (value[0] == '-' || value[0] == '+') ? 1 : 0;
    if (i >= value.size()) return K::Raw;
    bool digits = false, dot = false;
    for (; i < value.size(); ++i) {
        const char c = value[i];
        if (std::isdigit(static_cast<unsigned char>(c))) digits = true;
        else if (c == '.' && !dot) dot = true;
        else return K::Raw;
    }
    return !digits ? K::Raw : dot ? K::Float : K::Int;
}

namespace {
// fields the property grid leaves to other editors
bool hiddenProperty(const std::string& ctc, const std::string& key) {
    const std::string k = lower(key);
    if (k == "uid" || k == "definitiontype" || k == "versionnumber") return true;
    if (k.size() > 3 && k.compare(k.size() - 3, 3, "uid") == 0) return true;   // links
    if (ctc == "CTCPhysicsStandard") return true;                                // the frame: the gizmo
    return false;
}
} // namespace

std::vector<Document::PropertyRow> Document::propertiesOf(size_t index) const {
    std::vector<PropertyRow> out;
    if (index >= file_.things().size()) return out;
    const auto& t = file_.things()[index];
    const bool trackNode = lower(t.type) == "tracknode";
    for (const auto& p : t.properties) {
        if (hiddenProperty("", p.key)) continue;
        if (trackNode && lower(p.key) == "scriptname") continue;   // renamed through the Tracks card (the whole chain)
        // a key written twice (ScriptName on some things): one row, showing the copy the loader reads (the last)
        auto dup = std::find_if(out.begin(), out.end(), [&](const PropertyRow& r) { return r.ctc.empty() && lower(r.key) == lower(p.key); });
        if (dup != out.end()) { dup->value = p.value; dup->kind = kindOf(p.value); continue; }
        out.push_back({"", p.key, p.value, kindOf(p.value)});
    }
    for (const auto& b : t.ctcBlocks)
        for (const auto& p : b.properties)
            if (!hiddenProperty(b.name, p.key)) out.push_back({b.name, p.key, p.value, kindOf(p.value)});
    return out;
}

std::vector<Document::KnownProperty> Document::knownComponentProperties(size_t index) const {
    std::vector<KnownProperty> result;
    if(index>=file_.things().size()) return result;
    const auto& thing=file_.things()[index];
    // These fields have direct OnSerialise evidence in vanilla_property_fields.tsv.
    // Do not broaden from widget metadata alone: some rows are coordinated UI actions.
    static const std::set<std::string> components={"CTCDoor","CTCSearchableContainer","CTCDRegionExit",
        "CTCCreatureGenerator","CTCExplodingObject","CTCStockItem","CTCInfoDisplay"};
    for(const auto& field:vanillaFields()) {
        if(!components.contains(field.ctc) || std::string_view(field.confidence)!="H" || hiddenProperty(field.ctc,field.key)) continue;
        PropertyRow::Kind kind;
        const std::string_view type=field.kind;
        if(type=="bool") kind=PropertyRow::Kind::Bool;
        else if(type=="int") kind=PropertyRow::Kind::Int;
        else if(type=="float") kind=PropertyRow::Kind::Float;
        else continue;
        // The vanilla dialog used integer widgets for these fields, but their
        // OnSerialise transfers are float (TSV evidence 02514ffc/02515010/025a52fc).
        // Validate the serialized type so fractional authored values remain valid.
        if((std::string_view(field.ctc)=="CTCCreatureGenerator" &&
            (std::string_view(field.key)=="GenerationRadius" || std::string_view(field.key)=="SelfTriggerRadius")) ||
           (std::string_view(field.ctc)=="CTCExplodingObject" && std::string_view(field.key)=="Radius")) kind=PropertyRow::Kind::Float;
        const forge::tng::CtcBlock* block=nullptr; size_t blocks=0;
        for(const auto& candidate:thing.ctcBlocks) if(lower(candidate.name)==lower(field.ctc)) {block=&candidate;++blocks;}
        if(blocks!=1 || block->name!=field.ctc) continue;
        const forge::tng::Property* property=nullptr; size_t copies=0;
        for(const auto& candidate:block->properties) if(lower(candidate.key)==lower(field.key)) {property=&candidate;++copies;}
        if(copies>1) continue;
        if(std::any_of(result.begin(),result.end(),[&](const KnownProperty& row){return row.row.ctc==field.ctc && lower(row.row.key)==lower(field.key);})) continue;
        result.push_back({{field.ctc,field.key,property?property->value:std::string(),kind},property!=nullptr});
    }
    return result;
}

bool Document::setComponentOverride(size_t index,const std::string& ctc,const std::string& key,const std::string& value) {
    const auto rows=knownComponentProperties(index);
    const auto row=std::find_if(rows.begin(),rows.end(),[&](const KnownProperty& item){return item.row.ctc==ctc && lower(item.row.key)==lower(key);});
    if(row==rows.end() || value.empty()) return false;
    const auto* metadata=vanillaField(ctc,row->row.key);
    if(!metadata) return false;
    double number=0;
    std::string stored=value;
    if(row->row.kind==PropertyRow::Kind::Bool) {
        if(lower(value)!="true" && lower(value)!="false") return false;
        stored=lower(value)=="true"?"TRUE":"FALSE";
    } else if(row->row.kind==PropertyRow::Kind::Int) {
        int32_t parsed=0;
        const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),parsed);
        if(error!=std::errc() || end!=value.data()+value.size()) return false;
        number=parsed;
    } else {
        const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),number);
        if(error!=std::errc() || end!=value.data()+value.size() || !std::isfinite(number) || std::abs(number)>std::numeric_limits<float>::max()) return false;
    }
    if(metadata->hasRange && (number<metadata->min || number>metadata->max)) return false;
    if(row->present && row->row.value==stored) return true;
    pushUndo();
    file_.setCtcProperty(index,ctc,row->row.key,stored);
    ++revision_;
    return true;
}

bool Document::resetComponentOverride(size_t index,const std::string& ctc,const std::string& key) {
    const auto rows=knownComponentProperties(index);
    const auto row=std::find_if(rows.begin(),rows.end(),[&](const KnownProperty& item){return item.row.ctc==ctc && lower(item.row.key)==lower(key);});
    if(row==rows.end()) return false;
    if(!row->present) return true;
    pushUndo();
    file_.removeCtcProperty(index,ctc,row->row.key);
    ++revision_;
    return true;
}

bool Document::setPropertyValue(size_t index, const std::string& ctc, const std::string& key, const std::string& value) {
    if (index >= file_.things().size() || hiddenProperty(ctc, key)) return false;
    if (isLocked(index)) {
        const auto block=lower(ctc),field=lower(key);
        if (block.empty() && field=="objectscale") return false;
        if ((block=="ctcphysicsstandard" || block=="ctcphysicsnavigator") &&
            (field=="positionx" || field=="positiony" || field=="positionz" ||
             field=="rhsetforwardx" || field=="rhsetforwardy" || field=="rhsetforwardz" ||
             field=="rhsetupx" || field=="rhsetupy" || field=="rhsetupz")) return false;
    }
    std::optional<PropertyRow> row;
    for (const auto& r : propertiesOf(index))
        if (r.ctc == ctc && lower(r.key) == lower(key)) row = r;
    if (!row) return false;
    if (row->value == value) return true;
    using K = PropertyRow::Kind;
    const K k = kindOf(value);
    // keep the field's kind: an int field takes ints, a float field ints or floats, a string a quoted string
    const bool fits = row->kind == K::Raw ? (!value.empty() && value.find(';') == std::string::npos && value.find('\n') == std::string::npos)
                    : row->kind == K::Float ? (k == K::Float || k == K::Int)
                    : row->kind == K::String ? (k == K::String && value.find('"', 1) == value.size() - 1)
                    : k == row->kind;
    if (!fits) return false;
    pushUndo();
    if (ctc.empty()) { if (!file_.setThingPropertyAll(index, key, value)) file_.setThingProperty(index, key, value); }   // every copy of a doubled key
    else {
        file_.setCtcProperty(index, ctc, key, value);
        const std::string name=lower(key);
        if ((lower(ctc)=="ctcphysicsnavigator" || lower(ctc)=="ctcphysicsstandard") &&
            (name=="positionx" || name=="positiony" || name=="positionz") && float(std::atof(row->value.c_str()))!=float(std::atof(value.c_str()))) {
            Frame frame;
            if (frameOf(index,frame)) writeInitialPosition(file_,index,frame.pos,worldX_,worldY_);
        }
    }
    ++revision_;
    return true;
}

bool Document::isTrackNode(size_t index) const {
    return index < file_.things().size() && lower(file_.things()[index].type) == "tracknode";
}

uint64_t Document::trackLink(size_t node, int which) const {
    const auto v = file_.things()[node].find(which == 1 ? "LinkedToUID1" : "LinkedToUID2");
    if (!v) return 0;
    try { return std::stoull(*v); } catch (...) { return 0; }
}

void Document::setTrackField(size_t node, const std::string& key, const std::string& value) {
    // ScriptName appears twice on a track node (before the CTC blocks and at the end,
    // the loader reads the last): keep every copy equal
    if (!file_.setThingPropertyAll(node, key, value)) file_.setThingProperty(node, key, value);
}

std::vector<size_t> Document::trackChain(size_t node) const { return trackChain(node, nullptr); }

std::vector<size_t> Document::trackChain(size_t node, const std::unordered_map<uint64_t, size_t>* uidIndex) const {
    auto indexOf = [&](uint64_t uid) -> std::optional<size_t> {
        if (!uidIndex) return indexOfUid(uid);
        const auto hit = uidIndex->find(uid);
        return hit == uidIndex->end() ? std::nullopt : std::optional<size_t>(hit->second);
    };
    std::vector<size_t> back{node};
    std::set<size_t> seen{node};
    for (size_t cur = node;;) {   // walk to the head
        const uint64_t prev = trackLink(cur, 1);
        const auto p = prev ? indexOf(prev) : std::nullopt;
        if (!p || seen.count(*p)) break;
        seen.insert(*p); back.push_back(*p); cur = *p;
    }
    std::vector<size_t> chain(back.rbegin(), back.rend());
    for (size_t cur = node;;) {   // and to the tail
        const uint64_t next = trackLink(cur, 2);
        const auto n = next ? indexOf(next) : std::nullopt;
        if (!n || seen.count(*n)) break;
        seen.insert(*n); chain.push_back(*n); cur = *n;
    }
    return chain;
}

void Document::fixTrackEnds(const std::vector<size_t>& chain) {
    for (size_t i = 0; i < chain.size(); ++i) {
        setTrackField(chain[i], "Start", i == 0 ? "TRUE" : "FALSE");
        setTrackField(chain[i], "End", i + 1 == chain.size() ? "TRUE" : "FALSE");
    }
}

void Document::nameChain(const std::vector<size_t>& chain, const std::string& name) {
    for (const size_t n : chain) setTrackField(n, "ScriptName", name);
}

std::string Document::nextTrackTempName() {
    for (;;) {
        const std::string candidate="TrackTempName"+std::to_string(trackTempCounter_++);
        bool used=false;
        for (const auto& thing:file_.things())
            if (lower(thing.type)=="tracknode" && trackNameOf(thing)==candidate) { used=true; break; }
        if (!used) return candidate;
    }
}

std::vector<Document::Track> Document::tracks() const {
    std::vector<Track> out;
    std::set<size_t> done;
    std::unordered_map<uint64_t, size_t> uidIndex;
    for (size_t i = 0; i < file_.things().size(); ++i) uidIndex.emplace(uidOf(i), i);
    for (size_t i = 0; i < file_.things().size(); ++i) {
        if (!isTrackNode(i) || done.count(i)) continue;
        Track t;
        t.nodes = trackChain(i, &uidIndex);
        for (const size_t n : t.nodes) done.insert(n);
        t.name = trackNameOf(file_.things()[t.nodes.front()]);
        Frame a, b;
        for (size_t k = 0; k + 1 < t.nodes.size(); ++k)
            if (frameOf(t.nodes[k], a) && frameOf(t.nodes[k + 1], b))
                t.length += std::sqrt((b.pos[0] - a.pos[0]) * (b.pos[0] - a.pos[0]) + (b.pos[1] - a.pos[1]) * (b.pos[1] - a.pos[1]) + (b.pos[2] - a.pos[2]) * (b.pos[2] - a.pos[2]));
        out.push_back(std::move(t));
    }
    return out;
}

size_t Document::placeTrackNode(float x, float y, float z, const std::string& name) {
    pushUndo();
    const uint64_t uid = forge::thingplacer::nextUid(file_);
    char pos[160];
    std::snprintf(pos, sizeof pos, "PositionX %.6f;\r\nPositionY %.6f;\r\nPositionZ %.6f;\r\n", x, y, z);
    const std::string b = std::string("NewThing TrackNode;\r\nPlayer 0;\r\nUID ") + std::to_string(uid) + ";\r\n"
        "DefinitionType \"TRACK_NODE_BASIC\";\r\nScriptName " + name + ";\r\nScriptData \"NULL\";\r\n"
        "ThingGamePersistent FALSE;\r\nThingLevelPersistent FALSE;\r\nStartCTCPhysicsStandard;\r\n" + pos +
        "RHSetForwardX 0.000000;\r\nRHSetForwardY 0.000000;\r\nRHSetForwardZ 1.000000;\r\n"
        "RHSetUpX 0.000000;\r\nRHSetUpY -1.000000;\r\nRHSetUpZ 0.000000;\r\nEndCTCPhysicsStandard;\r\n"
        "StartCTCEditor;\r\nEndCTCEditor;\r\nStartCTCVillageMember;\r\nVillageUID 0;\r\nEndCTCVillageMember;\r\n"
        "Health 0.0;\r\nLinkedToUID1 0;\r\nLinkedToUID2 0;\r\nStart TRUE;\r\nEnd TRUE;\r\nScriptName " + name + ";\r\nEndThing;\r\n";
    try {
        const size_t n = file_.insertThingBlock(targetSection(), b);
        ++revision_;
        return n;
    } catch (...) {
        restore(undo_.back()); undo_.pop_back();
        throw;
    }
}

bool Document::linkTrackNodes(size_t a, size_t b, std::string& error) {
    if (!isTrackNode(a) || !isTrackNode(b) || a == b) { error = "pick two different track nodes"; return false; }
    auto chainA = trackChain(a), chainB = trackChain(b);
    if (std::find(chainA.begin(), chainA.end(), b) != chainA.end()) { error = "both nodes are on the same track (no loops)"; return false; }
    // a must end up the tail of its chain, b the head of its own: flip when that is possible
    if (chainA.back() != a && chainA.front() == a) std::reverse(chainA.begin(), chainA.end());
    if (chainB.front() != b && chainB.back() == b) std::reverse(chainB.begin(), chainB.end());
    if (chainA.back() != a || chainB.front() != b) { error = "a node inside a track already has two links"; return false; }
    pushUndo();
    std::vector<size_t> joined = chainA;
    joined.insert(joined.end(), chainB.begin(), chainB.end());
    std::string name=trackNameOf(file_.things()[joined.front()]);
    if (placeholderTrackName(name)) name=nextTrackTempName();
    // re-link the whole joined chain in order (a flipped part gets its links swapped)
    for (size_t i = 0; i < joined.size(); ++i) {
        setTrackField(joined[i], "LinkedToUID1", std::to_string(i ? uidOf(joined[i - 1]) : 0));
        setTrackField(joined[i], "LinkedToUID2", std::to_string(i + 1 < joined.size() ? uidOf(joined[i + 1]) : 0));
    }
    fixTrackEnds(joined);
    nameChain(joined, name);
    ++revision_;
    return true;
}

bool Document::flipTrack(size_t node) {
    if (!isTrackNode(node)) return false;
    auto chain = trackChain(node);
    if (chain.size() < 2) return false;
    pushUndo();
    std::reverse(chain.begin(), chain.end());
    for (size_t i = 0; i < chain.size(); ++i) {
        setTrackField(chain[i], "LinkedToUID1", std::to_string(i ? uidOf(chain[i - 1]) : 0));
        setTrackField(chain[i], "LinkedToUID2", std::to_string(i + 1 < chain.size() ? uidOf(chain[i + 1]) : 0));
    }
    fixTrackEnds(chain);
    ++revision_;
    return true;
}

bool Document::renameTrack(size_t node, const std::string& name) {
    if (!isTrackNode(node) || name.empty()) return false;
    for (const char c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    pushUndo();
    nameChain(trackChain(node), name);
    ++revision_;
    return true;
}

bool Document::unlinkTrackNode(size_t node) {
    if (!isTrackNode(node)) return false;
    const auto chain = trackChain(node);
    if (chain.size() < 2) return false;
    pushUndo();
    const auto at = size_t(std::find(chain.begin(), chain.end(), node) - chain.begin());
    const std::vector<size_t> before(chain.begin(), chain.begin() + at), after(chain.begin() + at + 1, chain.end());
    auto relink = [&](const std::vector<size_t>& part) {
        for (size_t i = 0; i < part.size(); ++i) {
            setTrackField(part[i], "LinkedToUID1", std::to_string(i ? uidOf(part[i - 1]) : 0));
            setTrackField(part[i], "LinkedToUID2", std::to_string(i + 1 < part.size() ? uidOf(part[i + 1]) : 0));
        }
        if (!part.empty()) fixTrackEnds(part);
    };
    relink(before);
    relink(after);
    setTrackField(node, "LinkedToUID1", "0");
    setTrackField(node, "LinkedToUID2", "0");
    fixTrackEnds({node});
    nameChain({node}, "NULL");
    if (before.size() == 1) nameChain(before, "INVALID");
    if (after.size() == 1) nameChain(after, "INVALID");
    else if (after.size() > 1) nameChain(after, nextTrackTempName());
    ++revision_;
    return true;
}

std::vector<std::string> Document::ctcBlocksOf(size_t index) const {
    std::vector<std::string> out;
    if (index < file_.things().size())
        for (const auto& block : file_.things()[index].ctcBlocks) out.push_back(block.name);
    return out;
}

std::vector<std::string> Document::listEntries(size_t index, const std::string& ctc, const std::string& base) const {
    std::vector<std::string> out;
    if (index >= file_.things().size()) return out;
    const auto* block = file_.things()[index].findCtc(ctc);
    if (!block) return out;
    for (int i = 0;; ++i) {
        const std::string key = lower(base + "[" + std::to_string(i) + "]");
        const auto hit = std::find_if(block->properties.begin(), block->properties.end(), [&](const forge::tng::Property& p) { return lower(p.key) == key; });
        if (hit == block->properties.end()) break;
        out.push_back(hit->value);
    }
    return out;
}

bool Document::listEditable(size_t index, const std::string& ctc, const std::string& base) const {
    if (index>=file_.things().size()) return false;
    const auto& thing=file_.things()[index];
    if (std::count_if(thing.ctcBlocks.begin(),thing.ctcBlocks.end(),[&](const auto& block) { return lower(block.name)==lower(ctc); })!=1) return false;
    const auto* block=thing.findCtc(ctc);
    const auto entries=listEntries(index,ctc,base);
    std::set<std::string> seen;
    const std::string prefix=lower(base)+"[";
    for (const auto& property:block->properties) {
        const std::string key=lower(property.key);
        if (key.rfind(prefix,0)!=0) continue;
        if (!seen.insert(key).second || kindOf(property.value)!=PropertyRow::Kind::String) return false;
        bool canonical=false;
        for (size_t i=0;i<entries.size();++i) if (key==prefix+std::to_string(i)+"]") { canonical=true; break; }
        if (!canonical) return false;
    }
    return seen.size()==entries.size();
}

bool Document::addListEntry(size_t index, const std::string& ctc, const std::string& base, const std::string& value) {
    if (!listEditable(index,ctc,base) || kindOf(value)!=PropertyRow::Kind::String || value.find_first_of(";\r\n")!=std::string::npos) return false;
    const auto cur = listEntries(index, ctc, base);
    pushUndo();
    const std::string after = cur.empty() ? std::string() : base + "[" + std::to_string(cur.size() - 1) + "]";
    // after the last entry; the first one leads the block, as retail writes it
    file_.insertCtcPropertyAfter(index, ctc, after, base + "[" + std::to_string(cur.size()) + "]", value);
    ++revision_;
    return true;
}

bool Document::removeListEntry(size_t index, const std::string& ctc, const std::string& base, int i) {
    if (!listEditable(index,ctc,base)) return false;
    const auto cur = listEntries(index, ctc, base);
    if (i < 0 || size_t(i) >= cur.size()) return false;
    pushUndo();
    for (size_t k = size_t(i); k + 1 < cur.size(); ++k)
        file_.setCtcProperty(index, ctc, base + "[" + std::to_string(k) + "]", cur[k + 1]);
    file_.removeCtcProperty(index, ctc, base + "[" + std::to_string(cur.size() - 1) + "]");
    ++revision_;
    return true;
}

std::vector<Document::Issue> Document::validate(const std::function<bool(const std::string&)>& isFamily) const {
    std::vector<Issue> out;
    std::unordered_map<uint64_t, size_t> uidIndex;
    for (size_t i = 0; i < file_.things().size(); ++i) uidIndex.emplace(uidOf(i), i);
    auto unquote = [](std::string v) { if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2); return v; };
    for (size_t i = 0; i < file_.things().size(); ++i) {
        const auto& t = file_.things()[i];
        // vanilla CTCCreatureGenerator::Validate 0x025151d7: families present and real
        for (const char* gen : {"CTCCreatureGenerator", "CTCDCreatureGenerator"}) {
            if (!t.findCtc(gen)) continue;
            const auto fams = listEntries(i, gen, "CreatureFamilies");
            if (fams.empty()) out.push_back({i, "CG-1", "no creature families specified - what am I supposed to generate?", true});
            if (isFamily)
                for (const auto& f : fams)
                    if (!isFamily(unquote(f))) out.push_back({i, "CG-2", "creature family " + unquote(f) + " is not a CREATURE_GENERATION_FAMILY def", true});
        }
        // vanilla CTCActivationReceptorCreatureGenerator::Validate 0x02639950: exactly one flag
        if (const auto* ar = t.findCtc("CTCActivationReceptorCreatureGenerator")) {
            bool trig = true, act = false;   // the constructor's defaults
            for (const auto& p : ar->properties) {
                if (lower(p.key) == "triggeronactivate") trig = p.value == "TRUE";
                if (lower(p.key) == "activateonactivate") act = p.value == "TRUE";
            }
            if (trig == act) out.push_back({i, "AR-1", "must set one and only one creature generator flag, not both or none!!!", true});
        }
        // vanilla CTCCameraPointBuilding::Validate 0x025c4950 (the offline half): an owner
        if (t.definitionType() == "CAMERA_POINT_BUILDING" && !t.findCtc("CTCOwnedEntity"))
            out.push_back({i, "CB-1", "CAMERA_POINT_BUILDING is not attached to a building!", true});
        // FableForge: links that point at nothing on this map (vanilla does not check links)
        for (const auto& l : linksOf(i)) {
            if (!l.target || l.field == "EntranceConnectedToUID" || l.field == "CameraTrackUID") continue;   // cross-map / stale by design
            if (!uidIndex.count(l.target)) out.push_back({i, "LINK", l.label + " points at uid " + std::to_string(l.target) + ", which is not on this map", false});
        }
    }
    // FableForge: track chains keep the engine's invariants (symmetric links, ends marked)
    for (const auto& tr : tracks())
        for (size_t k = 0; k < tr.nodes.size(); ++k) {
            const size_t n = tr.nodes[k];
            const uint64_t prev = trackLink(n, 1), next = trackLink(n, 2);
            const bool okPrev = k == 0 ? prev == 0 : prev == uidOf(tr.nodes[k - 1]);
            const bool okNext = k + 1 == tr.nodes.size() ? next == 0 : next == uidOf(tr.nodes[k + 1]);
            // only what the engine asserts on: links that do not point back, and a head without
            // Start TRUE (PeekTrackStartNode). A tail's End flag is not required: retail's
            // StartOakValeWest TrackTempName30 ends on End FALSE.
            const auto start = file_.things()[n].find("Start");
            if (!okPrev || !okNext) out.push_back({n, "TRACK", "track " + tr.name + ": a link here does not point back (the engine asserts)", false});
            else if (k == 0 && (!start || *start != "TRUE")) out.push_back({n, "TRACK", "track " + tr.name + ": the first node is not marked Start (the engine asserts)", false});
        }
    return out;
}

std::vector<Document::Link> Document::linksOf(size_t index) const {
    std::vector<Link> out;
    if (index >= file_.things().size()) return out;
    const auto& t = file_.things()[index];
    for (const auto& k : kLinkKinds) {
        // A region exit can carry both components; show one control and use the
        // dedicated region-exit value as the editor's source of truth.
        if (std::string_view(k.ctc) == "CTCActionUseScriptedHook" &&
            std::string_view(k.field) == "EntranceConnectedToUID" && t.findCtc("CTCDRegionExit")) continue;
        std::optional<std::string> value;
        if (k.ctc[0]) {
            const auto* block = t.findCtc(k.ctc);
            if (!block) continue;
            value = std::string("0");
            for (const auto& p : block->properties)
                if (lower(p.key) == lower(k.field)) { value = p.value; break; }
        } else {
            value = t.find(k.field);
            if (!value && k.offeredOn && lower(t.type)==k.offeredOn) value=std::string("0");
            if (!value) continue;
        }
        Link l{k.ctc, k.field, k.label, k.wants, 0, std::nullopt};
        try { l.target = std::stoull(*value); } catch (...) { l.target = 0; }
        if (l.target) l.targetIndex = indexOfUid(l.target);
        out.push_back(std::move(l));
    }
    return out;
}

bool Document::linkTargetFits(const Link& link, size_t target) const {
    if (target >= file_.things().size()) return false;
    const auto& t = file_.things()[target];
    if (link.field == "VillageUID") return t.findCtc("CTCVillage") != nullptr;
    if (link.field == "EntranceConnectedToUID") return t.findCtc("CTCDRegionEntrance") != nullptr;
    if (link.field == "ReceptorUID") return hasCtcPrefix(t, "CTCActivationReceptor");
    if (link.field == "HomeBuildingUID" || link.field == "WorkBuildingUID") return lower(t.type) == "building";
    if (link.field == "FatherCreatureUID" || link.field == "MotherCreatureUID") {
        if (lower(t.type)!="aicreature") return false;
        if (creatureSexLookup_) {
            const auto sex=creatureSexLookup_(t.definitionType());
            if (sex && *sex!=(link.field=="FatherCreatureUID" ? 1 : 2)) return false;
        }
        return true;
    }
    if (link.field == "SpouseCreatureUID") return lower(t.type) == "aicreature";
    if (link.field == "WifeLivingHereUID") return lower(t.type) == "aicreature" || lower(t.type) == "creature";
    return true;
}

bool Document::setLink(size_t index, const std::string& ctc, const std::string& field, uint64_t targetUid) {
    if (index >= file_.things().size()) return false;
    const auto links = linksOf(index);
    const auto hit = std::find_if(links.begin(), links.end(), [&](const Link& l) { return l.ctc == ctc && l.field == field; });
    if (hit == links.end()) return false;
    constexpr std::array<std::string_view,5> creatureFields={
        "HomeBuildingUID","WorkBuildingUID","FatherCreatureUID","MotherCreatureUID","SpouseCreatureUID"};
    const auto creatureField=std::find(creatureFields.begin(),creatureFields.end(),field);
    if (ctc.empty() && creatureField!=creatureFields.end()) {
        const auto target=targetUid ? indexOfUid(targetUid) : std::nullopt;
        if (targetUid && (!target || *target==index || !linkTargetFits(*hit,*target))) return false;
        const uint64_t sourceUid=uidOf(index);
        const bool spouse=field=="SpouseCreatureUID";
        const auto mate=spouse && hit->target ? indexOfUid(hit->target) : std::nullopt;
        if (spouse && targetUid) {
            if (hit->target && hit->target!=targetUid) return false;
            const auto theirField=file_.things()[*target].find(field);
            uint64_t theirUid=0;
            if (theirField && !parseUid(*theirField,theirUid)) return false;
            if (theirUid && theirUid!=sourceUid) return false;
        }
        const bool present=file_.things()[index].find(field).has_value();
        bool mateLinked=false, targetLinked=false;
        if (mate) {
            if (const auto v=file_.things()[*mate].find(field)) {
                uint64_t u=0;
                mateLinked=parseUid(*v,u) && u==sourceUid;
            }
        }
        if (spouse && target) {
            if (const auto v=file_.things()[*target].find(field)) {
                uint64_t u=0;
                targetLinked=parseUid(*v,u) && u==sourceUid;
            }
        }
        if (targetUid==hit->target && ((targetUid && (!spouse || targetLinked)) || (!targetUid && !present))) return true;
        pushUndo();
        auto writeCreatureField=[&](size_t thingIndex,std::string_view key,uint64_t uid) {
            if (!uid) { file_.removeThingProperty(thingIndex,key); return; }
            if (file_.things()[thingIndex].find(key)) {
                file_.setThingProperty(thingIndex,key,std::to_string(uid));
                return;
            }
            const auto fieldPos=std::find(creatureFields.begin(),creatureFields.end(),key);
            std::string_view anchor="OverridingBrainName";
            for (auto next=fieldPos+1;next!=creatureFields.end();++next)
                if (file_.things()[thingIndex].find(*next)) { anchor=*next; break; }
            file_.insertThingPropertyBefore(thingIndex,key,std::to_string(uid),anchor);
        };
        writeCreatureField(index,field,targetUid);
        if (spouse) {
            if (mateLinked && (!target || *target!=*mate)) writeCreatureField(*mate,field,0);
            if (target && !targetLinked) writeCreatureField(*target,field,sourceUid);
        }
        ++revision_;
        return true;
    }
    if (field == "EntranceConnectedToUID") {
        const auto& thing = file_.things()[index];
        const bool hasExit = thing.findCtc("CTCDRegionExit") != nullptr;
        const bool hasHook = thing.findCtc("CTCActionUseScriptedHook") != nullptr;
        const std::string value = std::to_string(targetUid);
        auto matches = [&](const char* name) {
            const auto* block = thing.findCtc(name);
            if (!block) return true;
            for (const auto& property : block->properties)
                if (lower(property.key) == lower(field)) return property.value == value;
            return targetUid == 0;
        };
        if (matches("CTCDRegionExit") && matches("CTCActionUseScriptedHook")) return true;
        pushUndo();
        if (hasExit) file_.setCtcProperty(index, "CTCDRegionExit", field, value);
        if (hasHook) file_.setCtcProperty(index, "CTCActionUseScriptedHook", field, value);
        ++revision_;
        return true;
    }
    if (hit->target == targetUid) return true;
    pushUndo();
    if (ctc.empty()) file_.setThingProperty(index, field, std::to_string(targetUid));
    else file_.setCtcProperty(index, ctc, field, std::to_string(targetUid));
    ++revision_;
    return true;
}

std::vector<Document::IncomingLink> Document::linksInto(size_t target) const {
    if (target>=file_.things().size()) return {};
    const uint64_t uid=uidOf(target);
    if (!uid) return {};
    size_t matches=0;
    for (size_t i=0;i<file_.things().size();++i) matches+=uidOf(i)==uid;
    if (matches!=1) return {}; // ambiguous target UID
    if (incomingCacheRevision_!=revision_) {
        incomingCache_.clear();
        for (size_t i=0;i<file_.things().size();++i)
            for (auto link:linksOf(i))
                if (link.target) incomingCache_[link.target].push_back({i,std::move(link)});
        incomingCacheRevision_=revision_;
    }
    const auto it=incomingCache_.find(uid);
    return it==incomingCache_.end() ? std::vector<IncomingLink>{} : it->second;
}

std::vector<Document::AttachOption> Document::viableAttachModes(size_t anchor) const {
    std::vector<AttachOption> out;
    if (anchor>=file_.things().size()) return out;
    const auto& thing=file_.things()[anchor];
    const std::string type=lower(thing.type);
    auto add=[&](AttachMode mode,const char* ctc,const char* field,const char* caption) {
        out.push_back({mode,field,ctc,caption});
    };
    if (type=="object" || type=="building" || thing.findCtc("CTCThingOwner"))
        add(AttachMode::Owned,"CTCOwnedEntity","OwnerUID","Attach objects");
    if (type=="building") {
        add(AttachMode::LivesIn,"","HomeBuildingUID","Attach people who live here");
        add(AttachMode::WorksIn,"","WorkBuildingUID","Attach people who work here");
    }
    if (type=="village" && thing.findCtc("CTCVillage"))
        add(AttachMode::Village,"CTCVillageMember","VillageUID","Attach things to village");
    if (type=="aicreature" && thing.findCtc("CTCVillageMember")) {
        add(AttachMode::Spouse,"","SpouseCreatureUID","Select creature's spouse");
        add(AttachMode::Father,"","FatherCreatureUID","Select creature's father");
        add(AttachMode::Mother,"","MotherCreatureUID","Select creature's mother");
    }
    if (hasCtcPrefix(thing,"CTCActivationReceptor"))
        add(AttachMode::Receptor,"CTCActivationTrigger","ReceptorUID","Attach triggers to this receptor");
    const auto anchorLinks=linksOf(anchor);
    if (std::any_of(anchorLinks.begin(),anchorLinks.end(),
                    [](const Link& link){return link.field=="EntranceConnectedToUID";}))
        add(AttachMode::RegionEntrance,"CTCDRegionExit","EntranceConnectedToUID","Select region entrance to connect to");
    if (thing.findCtc("CTCPreCalculatedNavigationRoute"))
        add(AttachMode::RouteTarget,"CTCPreCalculatedNavigationRoute","ThingToCalculateRouteToUID","Select target to calculate route to");
    return out;
}

bool Document::canAttach(size_t anchor, AttachMode mode, size_t clicked, std::string* reason) const {
    auto reject=[&](const char* message){if(reason)*reason=message;return false;};
    if (anchor>=file_.things().size() || clicked>=file_.things().size() || anchor==clicked)
        return reject("pick a different thing");
    const auto modes=viableAttachModes(anchor);
    const auto option=std::find_if(modes.begin(),modes.end(),
                                   [&](const AttachOption& item){return item.mode==mode;});
    if (option==modes.end()) return reject("this attachment mode is unavailable");
    const bool anchorSource=mode==AttachMode::RegionEntrance || mode==AttachMode::RouteTarget ||
                            mode==AttachMode::Spouse || mode==AttachMode::Father || mode==AttachMode::Mother;
    const size_t source=anchorSource?anchor:clicked, target=anchorSource?clicked:anchor;
    if (mode==AttachMode::Owned) {
        const std::string type=lower(file_.things()[clicked].type);
        if (type=="building" || type=="tracknode") return reject("buildings and track nodes cannot be owned this way");
        for (size_t child:ownedDescendants({clicked}))
            if (child==anchor) return reject("an owner link cannot form a cycle");
        if (!file_.things()[clicked].findCtc("CTCOwnedEntity")) return true;
    }
    if (mode==AttachMode::Village && isTrackNode(clicked) &&
        file_.things()[clicked].find("Start").value_or("")!="TRUE")
        return reject("only a track's start node can join a village");
    const auto links=linksOf(source);
    const auto link=std::find_if(links.begin(),links.end(),
                                 [&](const Link& item){return item.field==option->field &&
                                     (mode==AttachMode::RegionEntrance || item.ctc==option->ctc);});
    if (link==links.end()) return reject("the clicked thing lacks the required component");
    if (!linkTargetFits(*link,target)) return reject("the clicked thing has the wrong type");
    if ((mode==AttachMode::LivesIn || mode==AttachMode::WorksIn ||
         mode==AttachMode::Village || mode==AttachMode::Receptor ||
         mode==AttachMode::Owned) && link->target && link->target!=uidOf(target))
        return reject("the clicked thing is already attached elsewhere");
    if (mode==AttachMode::Spouse) {
        if (link->target && link->target!=uidOf(target)) return reject("the selected creature already has a spouse");
        for (const auto& reciprocal:linksOf(target))
            if (reciprocal.field=="SpouseCreatureUID" && reciprocal.target &&
                reciprocal.target!=uidOf(source)) return reject("the clicked creature already has a spouse");
    }
    return true;
}

bool Document::toggleAttachment(size_t anchor, AttachMode mode, size_t clicked, std::string& error) {
    if (!canAttach(anchor,mode,clicked,&error)) return false;
    if (mode==AttachMode::Owned && !file_.things()[clicked].findCtc("CTCOwnedEntity")) {
        pushUndo();
        file_.addCtcBlock(clicked,"CTCOwnedEntity",{{"VersionNumber","1"},{"OwnerUID",std::to_string(uidOf(anchor))}});
        ++revision_;
        return true;
    }
    const bool anchorSource=mode==AttachMode::RegionEntrance || mode==AttachMode::RouteTarget ||
                            mode==AttachMode::Spouse || mode==AttachMode::Father || mode==AttachMode::Mother;
    const size_t source=anchorSource?anchor:clicked, target=anchorSource?clicked:anchor;
    const auto options=viableAttachModes(anchor);
    const auto found=std::find_if(options.begin(),options.end(),
                                  [&](const AttachOption& item){return item.mode==mode;});
    if (found==options.end()) {error="attachment mode disappeared";return false;}
    const auto links=linksOf(source);
    const auto link=std::find_if(links.begin(),links.end(),
                                 [&](const Link& item){return item.field==found->field &&
                                     (mode==AttachMode::RegionEntrance || item.ctc==found->ctc);});
    if (link==links.end()) {error="attachment link disappeared";return false;}
    if (mode==AttachMode::Owned && link->target==uidOf(anchor)) {
        pushUndo();
        file_.removeCtcBlock(clicked,"CTCOwnedEntity");
        ++revision_;
        return true;
    }
    if (!setLink(source,link->ctc,link->field,link->target==uidOf(target)?0:uidOf(target))) {
        error="the attachment could not be changed";
        return false;
    }
    return true;
}

std::string Document::targetSection() const {
    const auto names = file_.sectionNames();
    for (const auto& n : names)
        if (lower(n) == lower(placementSection_)) return n;
    for (const auto& n : names)
        if (lower(n) == "null") return n;
    // no NULL section (a quest-only file): the first one, never a name the file lacks
    return names.empty() ? std::string("NULL") : names.front();
}

size_t Document::intoPlacementSection(size_t index) {
    const std::string want = targetSection();
    if (index >= file_.things().size() || lower(file_.sectionOf(index)) == lower(want)) return index;
    if (file_.sectionNames().empty()) return index;   // a file without sections has only the implicit NULL
    const std::string block = file_.thingBlockText(index);
    file_.removeThing(index);
    return file_.insertThingBlock(want, block);
}

bool Document::addSection(const std::string& name) {
    if (name.empty() || name.size() > 128) return false;
    for (const char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    for (const auto& n : file_.sectionNames())
        if (lower(n) == lower(name)) return false;
    pushUndo();
    if (file_.sectionNames().empty()) file_.addSection("NULL");   // keep the main section first
    file_.addSection(name);
    ++revision_;
    return true;
}

std::pair<std::string, int> Document::splitDayNight(const std::string& section) {
    const size_t p = section.find('%');
    if (p == std::string::npos) return {section, 0};
    const std::string suffix = section.substr(p + 1);
    return {section.substr(0, p), suffix == "DayOnly" ? 1 : suffix == "NightOnly" ? 2 : 0};
}

std::optional<size_t> Document::setDayNight(size_t index, int mode) {
    if (index >= file_.things().size() || mode < 0 || mode > 2) return std::nullopt;
    std::string section = file_.sectionOf(index);
    if (section.empty()) section = "NULL";
    const auto split = splitDayNight(section);
    if (split.second == mode) return index;
    const std::string target = split.first + (mode == 1 ? "%DayOnly" : mode == 2 ? "%NightOnly" : "");
    beginBatch();   // the new section and the move are one undo step
    try {
        bool have = false;
        for (const auto& n : file_.sectionNames()) {
            if (lower(n) != lower(target)) continue;
            // the engine matches the suffix exactly; a wrong-case twin (hand edit) would swallow the move
            if (n != target) { endBatch(); return std::nullopt; }
            have = true;
        }
        if (!have) {
            pushUndo();
            if (file_.sectionNames().empty()) file_.addSection("NULL");   // keep the main section first
            file_.addSection(target);   // '%' only through here: addSection() keeps names to A-Z, 0-9, _
            ++revision_;
        }
        const auto moved = moveToSection(index, target);
        endBatch();
        return moved;
    } catch (...) { endBatch(); throw; }
}

std::optional<size_t> Document::moveToSection(size_t index, const std::string& name) {
    if (index >= file_.things().size()) return std::nullopt;
    std::string target;
    for (const auto& n : file_.sectionNames())
        if (lower(n) == lower(name)) target = n;
    if (target.empty()) return std::nullopt;
    // The native owner component assigns its children the owner's serialization
    // section. Snapshot strict UID edges before any block relocation changes indices.
    const uint64_t rootUid=uidOf(index);
    std::vector<uint64_t> childUids;
    for (size_t child:ownedDescendants({index})) childUids.push_back(uidOf(child));
    const auto moveOne=[&](size_t i) {
        if (lower(file_.sectionOf(i)) == lower(target)) return i;
        pushUndo();
        const std::string block=file_.thingBlockText(i);
        file_.removeThing(i);
        const size_t moved=file_.insertThingBlock(target,block);
        ++revision_;
        return moved;
    };
    beginBatch();
    try {
        const size_t movedRoot=moveOne(index);
        for (uint64_t uid:childUids)
            if (const auto child=indexOfUid(uid)) moveOne(*child);
        const auto result=childUids.empty()?std::optional<size_t>(movedRoot):indexOfUid(rootUid);
        endBatch();
        return result;
    } catch (...) { endBatch(); throw; }
}

size_t Document::duplicate(size_t index) {
    if (index >= file_.things().size()) throw std::out_of_range("duplicate: bad thing index");
    if (lower(file_.things()[index].type) == "village")
        throw std::invalid_argument("duplicate: villages cannot be copied as verbatim blocks");
    bool hasOwnerLink=false;
    if (const auto* owned=file_.things()[index].findCtc("CTCOwnedEntity"))
        for (const auto& property:owned->properties)
            if (lower(property.key)=="owneruid") hasOwnerLink=true;
    pushUndo();
    std::string block = file_.thingBlockText(index);
    const size_t newIndex = file_.insertThingBlockBefore(index + 1, block);
    file_.setThingProperty(newIndex, "UID", std::to_string(forge::thingplacer::nextUid(file_)));
    if (file_.things()[newIndex].find("ScriptName")) file_.setThingProperty(newIndex, "ScriptName", "NULL");
    if (hasOwnerLink) file_.setCtcProperty(newIndex,"CTCOwnedEntity","OwnerUID","0");
    if (isTrackNode(newIndex)) {
        // Native clone placement creates a new track node; copied links would
        // make the original chain asymmetric and can assert in the engine.
        setTrackField(newIndex,"LinkedToUID1","0");
        setTrackField(newIndex,"LinkedToUID2","0");
        setTrackField(newIndex,"Start","TRUE");
        setTrackField(newIndex,"End","TRUE");
        setTrackField(newIndex,"ScriptName","NULL");
    }
    ++revision_;
    return newIndex;
}

std::vector<size_t> Document::duplicateGroup(const std::vector<size_t>& indices) {
    std::vector<size_t> sources;
    std::set<size_t> selected;
    for (size_t index:indices)
        if (index<file_.things().size() && selected.insert(index).second) sources.push_back(index);
    if (sources.empty()) return {};
    for (size_t source:sources)
        if (lower(file_.things()[source].type) == "village")
            throw std::invalid_argument("duplicate: villages cannot be copied as verbatim blocks");
    // Only a unique source UID can identify which selected thing a saved link
    // meant. Snapshot the links before insertion shifts thing indices.
    std::map<uint64_t,size_t> uidCounts, selectedUids;
    for (size_t i=0;i<file_.things().size();++i)
        if (const uint64_t uid=uidOf(i)) ++uidCounts[uid];
    for (size_t source:sources)
        if (const uint64_t uid=uidOf(source); uid && uidCounts[uid]==1)
            selectedUids[uid]=source;
    struct CopyLink { size_t source, target; std::string ctc, field; };
    std::vector<CopyLink> internalLinks;
    for (size_t source:sources) {
        const auto& thing=file_.things()[source];
        for (const auto& link:linksOf(source)) {
            if (!link.target || link.field=="OwnerUID") continue;
            const auto target=selectedUids.find(link.target);
            if (target==selectedUids.end()) continue;
            // Duplicate declarations are ambiguous even when their first
            // values agree; leave the copied text alone in that case.
            const auto matches=[&](const auto& properties) {
                return std::count_if(properties.begin(),properties.end(),
                    [&](const auto& p){return lower(p.key)==lower(link.field);});
            };
            size_t declarations=0;
            if (link.ctc.empty()) declarations=matches(thing.properties);
            else for (const auto& block:thing.ctcBlocks)
                if (lower(block.name)==lower(link.ctc)) declarations+=matches(block.properties);
            if (declarations!=1) continue;
            internalLinks.push_back({source,target->second,link.ctc,link.field});
        }
    }
    // Only strict serialized ownership edges within the selected set are
    // reconnected. Each single copy starts detached from its source's owner.
    std::map<size_t,size_t> selectedParents;
    for (size_t parent:sources)
        for (const auto& [child,directParent]:ownedTree({parent}))
            if (directParent==parent && selected.count(child)) selectedParents[child]=parent;
    std::vector<size_t> descending=sources;
    std::sort(descending.begin(),descending.end(),std::greater<size_t>());
    std::map<size_t,uint64_t> copyUids;
    beginBatch();
    try {
        for (size_t source:descending) copyUids[source]=uidOf(duplicate(source));
        for (const auto& [child,parent]:selectedParents) {
            const auto copy=indexOfUid(copyUids.at(child));
            if (!copy || !setLink(*copy,"CTCOwnedEntity","OwnerUID",copyUids.at(parent)))
                throw std::runtime_error("duplicateGroup: failed to link copied owner");
        }
        for (const auto& link:internalLinks) {
            if (link.field=="SpouseCreatureUID") continue;
            const auto copy=indexOfUid(copyUids.at(link.source));
            if (!copy || !setLink(*copy,link.ctc,link.field,copyUids.at(link.target)))
                throw std::runtime_error("duplicateGroup: failed to remap copied link");
        }
        // Spouse links are reciprocal. Clear only paired copies first; otherwise
        // setLink would see the copied partner's old target and refuse the new
        // pair. Originals remain untouched because neither points at a copy.
        for (const auto& link:internalLinks) {
            if (link.field!="SpouseCreatureUID" || link.source>=link.target) continue;
            const bool reciprocal=std::any_of(internalLinks.begin(),internalLinks.end(),
                [&](const CopyLink& other){return other.field==link.field &&
                    other.source==link.target && other.target==link.source;});
            if (!reciprocal) continue;
            const auto a=indexOfUid(copyUids.at(link.source));
            const auto b=indexOfUid(copyUids.at(link.target));
            if (!a || !b || !setLink(*a,"",link.field,0) ||
                !setLink(*b,"",link.field,0) ||
                !setLink(*a,"",link.field,copyUids.at(link.target)))
                throw std::runtime_error("duplicateGroup: failed to remap copied spouses");
        }
        std::vector<size_t> result;
        for (size_t source:sources) {
            const auto copy=indexOfUid(copyUids.at(source));
            if (!copy) throw std::runtime_error("duplicateGroup: copied UID missing");
            result.push_back(*copy);
        }
        endBatch();
        return result;
    } catch (...) { endBatch(); throw; }
}

size_t Document::place(forge::thingplacer::Placement placement) {
    pushUndo();
    try {
        const auto r = forge::thingplacer::place(file_, std::move(placement));
        ++revision_;
        return intoPlacementSection(r.thingIndex);
    } catch (...) {
        restore(undo_.back()); undo_.pop_back();
        throw;
    }
}

size_t Document::placeFishingSpot(const float pos[3], const std::string& reward, const std::string& scriptName) {
    pushUndo();
    try {
        // the retail block: 32 MARKER_FISHING_SPOT things across FinalAlbion share it,
        // one (BarrowFields) adds the reward container for a named catch
        const std::string eol = "\r\n";
        std::string b;
        b += "NewThing Marker;" + eol;
        b += "Player -1;" + eol;
        b += "UID " + std::to_string(forge::thingplacer::nextUid(file_)) + ";" + eol;
        b += "DefinitionType \"MARKER_FISHING_SPOT\";" + eol;
        b += "ScriptName " + (scriptName.empty() ? std::string("NULL") : scriptName) + ";" + eol;
        b += "ScriptData \"NULL\";" + eol;
        b += "ThingGamePersistent TRUE;" + eol;
        b += "ThingLevelPersistent TRUE;" + eol;
        b += "StartCTCPhysicsStandard;" + eol;
        b += "PositionX " + formatFloat(pos[0]) + ";" + eol;
        b += "PositionY " + formatFloat(pos[1]) + ";" + eol;
        b += "PositionZ " + formatFloat(pos[2]) + ";" + eol;
        b += "RHSetForwardX 0.0;" + eol + "RHSetForwardY 0.999994;" + eol + "RHSetForwardZ 0.0;" + eol;
        b += "RHSetUpX 0.0;" + eol + "RHSetUpY 0.0;" + eol + "RHSetUpZ 0.999994;" + eol;
        b += "EndCTCPhysicsStandard;" + eol;
        b += "StartCTCEditor;" + eol + "EndCTCEditor;" + eol;
        if (!reward.empty()) {
            b += "StartCTCContainerRewardHero;" + eol;
            b += "ContainerContents[0] \"" + reward + "\";" + eol;
            b += "EndCTCContainerRewardHero;" + eol;
        }
        b += "StartCTCFishingSpot;" + eol + "EndCTCFishingSpot;" + eol;
        b += "Health 1.0;" + eol;
        b += "EndThing;" + eol;
        const size_t n = file_.insertThingBlock(targetSection(), b);
        ++revision_;
        return n;
    } catch (...) {
        restore(undo_.back()); undo_.pop_back();
        throw;
    }
}

size_t Document::placeCreatureGenerator(const float pos[3], const std::vector<std::string>& families,
                                        float radius, int activeLimit, const std::string& scriptName) {
    if (families.empty()) throw std::invalid_argument("a spawner needs at least one creature family");
    pushUndo();
    try {
        // the retail block (Darkwood_8 / Graveyard_1 self-triggering generators),
        // retail float spelling, per-file UID namespace
        const std::string eol = "\r\n";
        std::string b;
        b += "NewThing Marker;" + eol;
        b += "Player -1;" + eol;
        b += "UID " + std::to_string(forge::thingplacer::nextUid(file_)) + ";" + eol;
        b += "DefinitionType \"MARKER_CREATURE_GENERATOR\";" + eol;
        b += "ScriptName " + (scriptName.empty() ? std::string("NULL") : scriptName) + ";" + eol;
        b += "ScriptData \"NULL\";" + eol;
        b += "ThingGamePersistent FALSE;" + eol;
        b += "ThingLevelPersistent TRUE;" + eol;
        b += "StartCTCPhysicsStandard;" + eol;
        b += "PositionX " + formatFloat(pos[0]) + ";" + eol;
        b += "PositionY " + formatFloat(pos[1]) + ";" + eol;
        b += "PositionZ " + formatFloat(pos[2]) + ";" + eol;
        b += "RHSetForwardX 0.0;" + eol + "RHSetForwardY 0.999994;" + eol + "RHSetForwardZ 0.0;" + eol;
        b += "RHSetUpX 0.0;" + eol + "RHSetUpY 0.0;" + eol + "RHSetUpZ 0.999994;" + eol;
        b += "EndCTCPhysicsStandard;" + eol;
        b += "StartCTCEditor;" + eol + "EndCTCEditor;" + eol;
        b += "StartCTCCreatureGenerator;" + eol;
        for (size_t i = 0; i < families.size(); ++i) b += "CreatureFamilies[" + std::to_string(i) + "] \"" + families[i] + "\";" + eol;
        b += "GenerationRadius " + formatFloat(radius) + ";" + eol;
        b += "SelfTriggerRadius " + formatFloat(radius) + ";" + eol;
        b += "SelfTrigger TRUE;" + eol;
        b += "SelfTriggerResetInterval 0;" + eol;
        b += "TriggerOnActivate FALSE;" + eol;
        b += "ActiveCreatureLimit " + std::to_string(activeLimit) + ";" + eol;
        b += "TotalGenerationLimit -1;" + eol;
        b += "NumTriggers -1;" + eol;
        b += "ScriptNameOfAllGeneratedCreatures \"\";" + eol;
        b += "EndCTCCreatureGenerator;" + eol;
        b += "StartCTCActivationReceptorCreatureGenerator;" + eol;
        b += "DeactivateAfterSetTime TRUE;" + eol;
        b += "FramesAfterActivationToDeactivate 150;" + eol;
        b += "ActivateOnActivate FALSE;" + eol;
        b += "TriggerOnActivate TRUE;" + eol;
        b += "EndCTCActivationReceptorCreatureGenerator;" + eol;
        b += "StartCTCActivationTrigger;" + eol + "ReceptorUID 0;" + eol + "EndCTCActivationTrigger;" + eol;
        b += "StartCTCCreatureGeneratorCreator;" + eol + "EndCTCCreatureGeneratorCreator;" + eol;
        b += "Health 1.0;" + eol;
        b += "EndThing;" + eol;
        // into the NULL section (insertThingBlock places it there); appending after the
        // last thing would land in a quest-loaded section that the engine never loads
        const size_t n = file_.insertThingBlock(targetSection(), b);
        ++revision_;
        return n;
    } catch (...) {
        restore(undo_.back()); undo_.pop_back();
        throw;
    }
}

size_t Document::placeCreature(const float pos[3], const float forward[2], const std::string& definition, const std::string& scriptName, int player) {
    if (definition.empty()) throw std::invalid_argument("a creature needs a definition");
    pushUndo();
    try {
        // the retail block (StartOakValeWest chickens / villagers): navigator
        // physics, targetable, talk, editor, the AI flags, world-space InitialPos
        const std::string eol = "\r\n";
        float fx = forward[0], fy = forward[1];
        const float fl = std::sqrt(fx * fx + fy * fy);
        if (fl < 1e-6f) { fx = 0.0f; fy = 1.0f; } else { fx /= fl; fy /= fl; }
        const bool villager = definition.find("VILLAGER") != std::string::npos;
        std::string b;
        b += "NewThing AICreature;" + eol;
        b += "Player " + std::to_string(player) + ";" + eol;
        b += "UID " + std::to_string(forge::thingplacer::nextUid(file_)) + ";" + eol;
        b += "DefinitionType \"" + definition + "\";" + eol;
        b += "ScriptName " + (scriptName.empty() ? std::string("NULL") : scriptName) + ";" + eol;
        b += "ScriptData \"NULL\";" + eol;
        b += "ThingGamePersistent FALSE;" + eol;
        b += "ThingLevelPersistent FALSE;" + eol;
        b += "StartCTCPhysicsNavigator;" + eol;
        b += "PositionX " + formatFloat(pos[0]) + ";" + eol;
        b += "PositionY " + formatFloat(pos[1]) + ";" + eol;
        b += "PositionZ " + formatFloat(pos[2]) + ";" + eol;
        b += "RHSetForwardX " + formatFloat(fx) + ";" + eol;
        b += "RHSetForwardY " + formatFloat(fy) + ";" + eol;
        b += "RHSetForwardZ 0.0;" + eol;
        b += "RHSetUpX 0.0;" + eol + "RHSetUpY 0.0;" + eol + "RHSetUpZ 0.999994;" + eol;
        b += "EndCTCPhysicsNavigator;" + eol;
        b += "StartCTCTargeted;" + eol + "Targetable TRUE;" + eol + "EndCTCTargeted;" + eol;
        b += "StartCTCTalk;" + eol + "EndCTCTalk;" + eol;
        b += "StartCTCEditor;" + eol + "EndCTCEditor;" + eol;
        if (villager) b += "StartCTCVillageMember;" + eol + "VillageUID 0;" + eol + "EndCTCVillageMember;" + eol;
        b += "Health 1.0;" + eol;
        b += "OverridingBrainName NULL;" + eol;
        b += "HasInformation FALSE;" + eol;
        b += "WanderWithInformation FALSE;" + eol;
        b += "WaveWithInformation FALSE;" + eol;
        b += "ContinueAIWithInformation FALSE;" + eol;
        b += "EnableCreatureAutoPlacing FALSE;" + eol;
        b += "AllowedToFollowHero FALSE;" + eol;
        b += "RegionFollowingOverriddenFromScript FALSE;" + eol;
        b += "RespondingToFollowAndWait TRUE;" + eol;
        b += "CanBeCourted FALSE;" + eol;
        b += "CanBeMarried FALSE;" + eol;
        b += "InitialPosX " + formatFloat(pos[0] + float(worldX_)) + ";" + eol;
        b += "InitialPosY " + formatFloat(pos[1] + float(worldY_)) + ";" + eol;
        b += "InitialPosZ " + formatFloat(pos[2]) + ";" + eol;
        b += "EndThing;" + eol;
        const size_t n = file_.insertThingBlock(targetSection(), b);
        ++revision_;
        return n;
    } catch (...) {
        restore(undo_.back()); undo_.pop_back();
        throw;
    }
}

size_t Document::placeVillage(const float pos[3], const std::string& definition, const std::string& scriptName, int player) {
    if (definition.empty()) throw std::invalid_argument("a village needs a definition");
    pushUndo();
    try {
        // the retail block (StartOakValeWest V_OakVale)
        const std::string eol = "\r\n";
        std::string b;
        b += "NewThing Village;" + eol;
        b += "Player " + std::to_string(player) + ";" + eol;
        b += "UID " + std::to_string(forge::thingplacer::nextUid(file_)) + ";" + eol;
        b += "DefinitionType \"" + definition + "\";" + eol;
        b += "ScriptName " + (scriptName.empty() ? std::string("NULL") : scriptName) + ";" + eol;
        b += "ScriptData \"NULL\";" + eol;
        b += "ThingGamePersistent TRUE;" + eol;
        b += "ThingLevelPersistent TRUE;" + eol;
        b += "StartCTCPhysicsStandard;" + eol;
        b += "PositionX " + formatFloat(pos[0]) + ";" + eol;
        b += "PositionY " + formatFloat(pos[1]) + ";" + eol;
        b += "PositionZ " + formatFloat(pos[2]) + ";" + eol;
        b += "RHSetForwardX 0.0;" + eol + "RHSetForwardY 0.999994;" + eol + "RHSetForwardZ 0.0;" + eol;
        b += "RHSetUpX 0.0;" + eol + "RHSetUpY 0.0;" + eol + "RHSetUpZ 0.999994;" + eol;
        b += "EndCTCPhysicsStandard;" + eol;
        b += "StartCTCEditor;" + eol + "EndCTCEditor;" + eol;
        b += "StartCTCVillage;" + eol;
        b += "HasBeenInitiallyPopulated FALSE;" + eol;
        b += "FramePlayerLastSeenByGuard 0;" + eol;
        b += "Limbo FALSE;" + eol;
        b += "IsEnemyBecauseOfCrime FALSE;" + eol;
        b += "EndCTCVillage;" + eol;
        b += "StartCTCEnemy;" + eol;
        b += "FriendsWithEverythingFlag FALSE;" + eol;
        b += "EnableFollowersEnemyProxy TRUE;" + eol;
        b += "FactionName \"\";" + eol;
        b += "EndCTCEnemy;" + eol;
        b += "StartCTCCreatureOpinionOfHero;" + eol;
        b += "InteractedFlag FALSE;" + eol;
        b += "GreetedFlag FALSE;" + eol;
        b += "LastOpinionReactionFrame 0;" + eol;
        b += "NumberOfTimesHit 0.0;" + eol;
        b += "ToleranceToBeingHitOverride -1.0;" + eol;
        b += "FrameToDecayNumberOfTimesHit 2147483647;" + eol;
        b += "ForcedAttitude 18;" + eol;
        b += "HeroOpinionEnemy FALSE;" + eol;
        b += "EndCTCCreatureOpinionOfHero;" + eol;
        b += "Health 0.0;" + eol;
        b += "EndThing;" + eol;
        const size_t n = file_.insertThingBlock(targetSection(), b);
        ++revision_;
        return n;
    } catch (...) {
        restore(undo_.back()); undo_.pop_back();
        throw;
    }
}

size_t Document::placeEmitter(const float pos[3], const std::string& effectName, const std::string& scriptName) {
    if (effectName.empty()) throw std::invalid_argument("an emitter needs an effect name");
    pushUndo();
    try {
        // the retail block (StartOakValeWest BUTTERFLY_BLUE)
        const std::string eol = "\r\n";
        std::string b;
        b += "NewThing Thing;" + eol;
        b += "Player 4;" + eol;
        b += "UID " + std::to_string(forge::thingplacer::nextUid(file_)) + ";" + eol;
        b += "DefinitionType \"PARTICLE_EMITTER_PLACEABLE\";" + eol;
        b += "ScriptName " + (scriptName.empty() ? std::string("NULL") : scriptName) + ";" + eol;
        b += "ScriptData \"NULL\";" + eol;
        b += "ThingGamePersistent FALSE;" + eol;
        b += "ThingLevelPersistent FALSE;" + eol;
        b += "StartCTCPhysicsStandard;" + eol;
        b += "PositionX " + formatFloat(pos[0]) + ";" + eol;
        b += "PositionY " + formatFloat(pos[1]) + ";" + eol;
        b += "PositionZ " + formatFloat(pos[2]) + ";" + eol;
        b += "RHSetForwardX 0.0;" + eol + "RHSetForwardY 0.999994;" + eol + "RHSetForwardZ 0.0;" + eol;
        b += "RHSetUpX 0.0;" + eol + "RHSetUpY 0.0;" + eol + "RHSetUpZ 0.999994;" + eol;
        b += "EndCTCPhysicsStandard;" + eol;
        b += "StartCTCEditor;" + eol + "EndCTCEditor;" + eol;
        b += "StartCTCDParticleEmitter;" + eol;
        b += "IndependantObject TRUE;" + eol;
        b += "ParticleTypeName \"" + effectName + "\";" + eol;
        b += "EndCTCDParticleEmitter;" + eol;
        b += "EndThing;" + eol;
        const size_t n = file_.insertThingBlock(targetSection(), b);
        ++revision_;
        return n;
    } catch (...) {
        restore(undo_.back()); undo_.pop_back();
        throw;
    }
}

std::vector<ThingSummary> Document::villages() const {
    std::vector<ThingSummary> out;
    for (size_t i = 0; i < file_.things().size(); ++i)
        if (file_.things()[i].findCtc("CTCVillage")) out.push_back(summary(i));
    return out;
}

uint64_t Document::villageOf(size_t index) const {
    if (index >= file_.things().size()) return 0;
    const auto* m = file_.things()[index].findCtc("CTCVillageMember");
    if (!m) return 0;
    for (const auto& pr : m->properties)
        if (pr.key == "VillageUID") return std::strtoull(pr.value.c_str(), nullptr, 10);
    return 0;
}

void Document::setVillageMember(size_t index, uint64_t villageUid) {
    if (index >= file_.things().size()) throw std::out_of_range("setVillageMember: bad thing index");
    pushUndo();
    try {
        if (file_.things()[index].findCtc("CTCVillageMember")) {
            file_.setCtcProperty(index, "CTCVillageMember", "VillageUID", std::to_string(villageUid));
        } else {
            // insert the block after CTCEditor (retail order), else before the
            // first top-level line after the CTC blocks, else before EndThing
            std::string text = file_.thingBlockText(index);
            const std::string block = "StartCTCVillageMember;\r\nVillageUID " + std::to_string(villageUid) + ";\r\nEndCTCVillageMember;\r\n";
            size_t at = text.find("EndCTCEditor;");
            if (at != std::string::npos) {
                at = text.find('\n', at);
                at = at == std::string::npos ? text.size() : at + 1;
            } else {
                at = text.rfind("EndThing;");
                if (at == std::string::npos) throw std::runtime_error("thing block without EndThing");
            }
            text.insert(at, block);
            const std::string section = file_.sectionOf(index);
            file_.removeThing(index);
            file_.insertThingBlockBefore(index, text);
        }
        ++revision_;
    } catch (...) {
        restore(undo_.back()); undo_.pop_back();
        throw;
    }
}

std::vector<std::string> creatureFamilies(const fs::path& gameRoot, std::string& error) {
    std::vector<std::string> out;
    try {
        const fs::path defs = gameRoot / "data" / "CompiledDefs";
        const auto file = forge::bin::File::open(defs / "names.bin", defs / "game.bin");
        for (const auto& e : file.entries())
            if (e.definition == "CREATURE_GENERATION_FAMILY" && !e.name.empty() && e.name.rfind("NULLDEF", 0) != 0) out.push_back(e.name);
        std::sort(out.begin(), out.end());
    } catch (const std::exception& e) { error = e.what(); }
    return out;
}

size_t Document::clearLinksTo(uint64_t uid, size_t except, const std::set<size_t>* pending) {
    if (!uid) return 0;
    size_t declarations=0;
    for (size_t i=0;i<file_.things().size();++i) declarations+=uidOf(i)==uid;
    if (declarations!=1) return 0; // a duplicate UID still has a surviving target
    auto exactUid=[&](const std::string& raw) {
        const std::string value=unquote(raw);
        uint64_t parsed=0;
        const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
        return result.ec==std::errc{} && result.ptr==value.data()+value.size() && parsed==uid;
    };
    size_t cleared=0;
    for (size_t i=0;i<file_.things().size();++i) {
        if (i==except || (pending && pending->count(i))) continue;
        std::vector<const LinkKind*> hits;
        for (const auto& kind:kLinkKinds) {
            const auto& thing=file_.things()[i];
            size_t fields=0,blocks=0;
            std::string value;
            if (kind.ctc[0]) {
                for (const auto& block:thing.ctcBlocks) if (lower(block.name)==lower(kind.ctc)) {
                    ++blocks;
                    for (const auto& property:block.properties) if (lower(property.key)==lower(kind.field)) {
                        ++fields; value=property.value;
                    }
                }
            } else {
                blocks=1;
                for (const auto& property:thing.properties) if (lower(property.key)==lower(kind.field)) {
                    ++fields; value=property.value;
                }
            }
            if (blocks==1 && fields==1 && exactUid(value)) hits.push_back(&kind);
        }
        for (const auto* kind:hits) {
            if (kind->ctc[0]) file_.setCtcProperty(i,kind->ctc,kind->field,"0");
            else if (kind->offeredOn) file_.removeThingProperty(i,kind->field);
            else file_.setThingProperty(i,kind->field,"0");
            ++cleared;
        }
    }
    return cleared;
}

size_t Document::removeOne(size_t index, const std::set<size_t>* pending) {
    pushUndo();
    if (isTrackNode(index)) unlinkTrackNode(index);
    const size_t cleared=clearLinksTo(uidOf(index),index,pending);
    file_.removeThing(index);
    ++revision_;
    return cleared;
}

size_t Document::remove(size_t index) {
    if (index >= file_.things().size()) throw std::out_of_range("remove: bad thing index");
    if (isLocked(index)) return 0;
    beginBatch();
    try { const size_t cleared=removeOne(index); endBatch(); return cleared; }
    catch (...) { endBatch(); throw; }
}

size_t Document::removeWithOwned(const std::vector<size_t>& roots, bool includeOwned,
                                  size_t* clearedLinks) {
    if (clearedLinks) *clearedLinks=0;
    std::set<size_t> selected;
    for (size_t index:roots)
        if (index<file_.things().size() && !isLocked(index)) selected.insert(index);
    if (selected.empty()) return 0;
    const std::vector<size_t> validRoots(selected.begin(),selected.end());
    const auto tree=ownedTree(validRoots);
    std::vector<size_t> detach;
    if (includeOwned) {
        for (const auto& [child,parent]:tree) selected.insert(child);
    } else {
        for (const auto& [child,parent]:tree)
            if (selected.count(parent) && !selected.count(child)) detach.push_back(child);
    }
    beginBatch();
    try {
        for (size_t child:detach)
            if (!setLink(child,"CTCOwnedEntity","OwnerUID",0))
                throw std::runtime_error("removeWithOwned: failed to detach child");
        size_t cleared=0;
        std::set<size_t> pending=selected;
        for (auto it=selected.rbegin();it!=selected.rend();++it) {
            pending.erase(*it); // only lower indices still await removal
            cleared+=removeOne(*it,&pending);
        }
        endBatch();
        if (clearedLinks) *clearedLinks=cleared;
        return selected.size();
    } catch (...) { endBatch(); throw; }
}

bool Document::undo() {
    if (undo_.empty() || stroke_) return false;
    redo_.push_back(snapshot());
    restore(undo_.back());
    undo_.pop_back();
    return true;
}

bool Document::redo() {
    if (redo_.empty() || stroke_) return false;
    undo_.push_back(snapshot());
    restore(redo_.back());
    redo_.pop_back();
    return true;
}

bool Document::dirty() const {
    if (dirtyRev_ != revision_) { dirtyValue_ = file_.serialize() != original_; dirtyRev_ = revision_; }
    return dirtyValue_;
}

std::vector<std::string> Document::changes() const {
    std::vector<std::string> out;
    forge::tng::File before;
    try { before = forge::tng::File::parseText(original_, mapName_ + ".tng"); } catch (...) { return out; }
    auto label = [](const forge::tng::Thing& t, uint64_t uid) {
        std::string s = t.definitionType();
        const std::string sn = t.scriptName();
        if (!sn.empty() && sn != "NULL") s += " " + sn;
        return s + " (uid " + std::to_string(uid) + ")";
    };
    std::map<uint64_t, const forge::tng::Thing*> was, now;
    auto uidOfThing = [](const forge::tng::Thing& t) { uint64_t u = 0; if (const auto r = t.find("UID")) parseUid(*r, u); return u; };
    for (const auto& t : before.things()) was[uidOfThing(t)] = &t;
    for (const auto& t : file_.things()) now[uidOfThing(t)] = &t;
    for (const auto& [uid, t] : was)
        if (!now.count(uid)) out.push_back("removed " + label(*t, uid));
    for (const auto& [uid, t] : now) {
        auto hit = was.find(uid);
        if (hit == was.end()) { out.push_back("added " + label(*t, uid)); continue; }
        const auto* a = physicsOf(*hit->second);
        const auto* b = physicsOf(*t);
        bool moved = false;
        if (a && b) {
            static const char* keys[] = {"PositionX", "PositionY", "PositionZ", "RHSetForwardX", "RHSetForwardY", "RHSetForwardZ", "RHSetUpX", "RHSetUpY", "RHSetUpZ"};
            for (const char* k : keys) if (std::fabs(propF(*a, k, 0) - propF(*b, k, 0)) > 1e-6f) { moved = true; break; }
        }
        // any other line difference
        std::string ta, tb;
        for (const auto& p : hit->second->properties) ta += p.key + "=" + p.value + ";";
        for (const auto& p : t->properties) tb += p.key + "=" + p.value + ";";
        for (const auto& c : hit->second->ctcBlocks) { ta += c.name + "{"; for (const auto& p : c.properties) ta += p.key + "=" + p.value + ";"; ta += "}"; }
        for (const auto& c : t->ctcBlocks) { tb += c.name + "{"; for (const auto& p : c.properties) tb += p.key + "=" + p.value + ";"; tb += "}"; }
        if (moved) out.push_back("moved " + label(*t, uid));
        else if (ta != tb) out.push_back("changed " + label(*t, uid));
    }
    return out;
}

bool Document::saveLoose(const fs::path& gameRoot, std::string& error) {
    const fs::path path = external() ? loosePath_ : gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName_ + ".tng");
    try {
        fs::create_directories(path.parent_path());
        const std::string text = file_.serialize();
        if (fs::exists(path)) { if (!backupOnce(path, error)) return false; }
        else albion::backups::markCreated(path);   // the backup manager deletes it on restore
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { error = "cannot write " + path.string(); return false; }
        f.write(text.data(), std::streamsize(text.size()));
        loosePath_ = path;
        original_ = text; dirtyRev_ = ~0ull;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool Document::saveToPack(const fs::path& pack, std::string& error) {
    if (external()) { error = "a map of another world cannot go into a FinalAlbion pack"; return false; }
    const fs::path path = pack / "data" / "Levels" / "FinalAlbion" / (mapName_ + ".tng");
    try {
        fs::create_directories(path.parent_path());
        const std::string text = file_.serialize();
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { error = "cannot write " + path.string(); return false; }
        f.write(text.data(), std::streamsize(text.size()));
        original_ = text; dirtyRev_ = ~0ull;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool Document::deployTerrainToPack(const fs::path& gameRoot, const fs::path& pack, std::vector<std::string>& notes, std::string& error,
                                   const forge::terraintex::ThemeLibrary* library, const std::function<void(const std::string&)>& progress) {
    if (external()) { error = "a map of another world cannot go into a FinalAlbion pack"; return false; }
    packOut_ = pack;
    const bool ok = deployTerrainSteps(gameRoot, notes, error, library, progress);
    packOut_.clear();
    return ok;
}

bool Document::deployWad(const fs::path& gameRoot, std::string& error) {
    // a loose-level install (no FinalAlbion.wad, the levels extracted to
    // FinalAlbion\*): the loose .tng is what the game reads, so it is the deploy.
    // Never recreate the WAD there: it would override every loose file.
    // another world's .tng beside its .lev is likewise the file the game reads
    if (external() || forge::levelstore::detect(gameRoot).looseOnly()) return saveLoose(gameRoot, error);
    const fs::path wad = gameRoot / "data" / "Levels" / "FinalAlbion.wad";
    try {
        if (!fs::exists(wad)) { error = "no " + wad.string(); return false; }
        const auto archive = forge::wad::Archive::open(wad);
        std::string entryName;
        const std::string want = lower(mapName_) + ".tng";
        for (const auto& e : archive.entries())
            if (lower(fs::path(e.name).filename().string()) == want) { entryName = e.name; break; }
        if (entryName.empty()) { error = mapName_ + ".tng is not in FinalAlbion.wad"; return false; }
        const std::string text = file_.serialize();
        std::map<std::string, std::vector<uint8_t>> rep;
        rep[entryName] = std::vector<uint8_t>(text.begin(), text.end());
        detail::PendingBanks pending(gameRoot, ".forge-tng-deploy-");
        forge::wad::repack(wad, rep, pending.prepare(fs::path("data") / "Levels" / "FinalAlbion.wad"));
        // a loose copy (the user's, or ours) would otherwise go stale and shadow the WAD on read
        const fs::path loose = fs::path("data") / "Levels" / "FinalAlbion" / (mapName_ + ".tng");
        if (fs::exists(gameRoot / loose)) {
            const auto prepared = pending.prepare(loose);
            std::ofstream f(prepared, std::ios::binary);
            f.write(text.data(), std::streamsize(text.size()));
            f.close();
            if (!f) throw std::runtime_error("cannot prepare loose TNG: " + prepared.string());
        }
        if (!pending.install(true, error)) return false;
        original_ = text; dirtyRev_ = ~0ull;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

// ---------------------------------------------------------------- brush library files

bool saveTerrainClip(const TerrainClip& clip, const std::filesystem::path& file, std::string& error) {
    try {
        nlohmann::json j;
        j["format"] = "fableforge-brush";
        j["version"] = 1;
        j["w"] = clip.w;
        j["h"] = clip.h;
        j["heights"] = clip.heights;
        auto& themes = j["themes"] = nlohmann::json::array();
        for (const auto& t : clip.themes) themes.push_back({{"name", t.name}, {"value", t.value}});
        auto& cells = j["cells"] = nlohmann::json::array();   // per vertex: 3 theme slots + 3 strengths
        for (size_t i = 0; i < clip.themeIndex.size(); ++i)
            cells.push_back({clip.themeIndex[i][0], clip.themeIndex[i][1], clip.themeIndex[i][2],
                             clip.themeStrength[i][0], clip.themeStrength[i][1], clip.themeStrength[i][2]});
        auto& things = j["things"] = nlohmann::json::array();
        for (const auto& t : clip.things)
            things.push_back({{"block", t.block}, {"dx", t.dx}, {"dy", t.dy}, {"aboveGround", t.aboveGround},
                              {"forward", {t.frame.forward[0], t.frame.forward[1], t.frame.forward[2]}},
                              {"up", {t.frame.up[0], t.frame.up[1], t.frame.up[2]}}, {"scale", t.frame.scale}});
        std::error_code ec;
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
        std::ofstream out(file, std::ios::binary);
        out << j.dump(1);
        if (!out) { error = "cannot write " + file.string(); return false; }
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool loadTerrainClip(const std::filesystem::path& file, TerrainClip& clip, std::string& error) {
    try {
        std::ifstream in(file, std::ios::binary);
        if (!in) { error = "cannot read " + file.string(); return false; }
        const nlohmann::json j = nlohmann::json::parse(in);
        if (j.value("format", "") != "fableforge-brush") { error = file.filename().string() + " is not a FableForge brush"; return false; }
        TerrainClip c;
        c.w = j.at("w").get<int>();
        c.h = j.at("h").get<int>();
        c.heights = j.at("heights").get<std::vector<float>>();
        for (const auto& t : j.at("themes")) c.themes.push_back({t.value("name", ""), t.value("value", 0u)});
        for (const auto& cell : j.at("cells")) {
            c.themeIndex.push_back({cell[0].get<uint8_t>(), cell[1].get<uint8_t>(), cell[2].get<uint8_t>()});
            c.themeStrength.push_back({cell[3].get<uint8_t>(), cell[4].get<uint8_t>(), cell[5].get<uint8_t>()});
        }
        const size_t n = size_t(c.w) * size_t(c.h);
        if (c.w <= 0 || c.h <= 0 || c.heights.size() != n || c.themeIndex.size() != n) { error = file.filename().string() + ": sizes do not match"; return false; }
        for (const auto& idx : c.themeIndex)
            for (uint8_t k : idx) if (k >= c.themes.size()) { error = file.filename().string() + ": theme slot out of range"; return false; }
        for (const auto& t : j.value("things", nlohmann::json::array())) {
            TerrainClip::Thing th;
            th.block = t.at("block").get<std::string>();
            th.dx = t.value("dx", 0.0f); th.dy = t.value("dy", 0.0f); th.aboveGround = t.value("aboveGround", 0.0f);
            for (int k = 0; k < 3; ++k) { th.frame.forward[k] = t.at("forward")[k].get<float>(); th.frame.up[k] = t.at("up")[k].get<float>(); }
            th.frame.scale = t.value("scale", 1.0f);
            c.things.push_back(std::move(th));
        }
        clip = std::move(c);
        return true;
    } catch (const std::exception& e) { error = file.filename().string() + ": " + e.what(); return false; }
}

} // namespace albion::editor
