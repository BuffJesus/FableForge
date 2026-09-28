#include "backups.hpp"
#include "leveledit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

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
    mapName_ = mapName;
    original_ = file_.serialize();
    undo_.clear(); redo_.clear();
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
    s.scriptName = t.scriptName();
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
        if (i >= file_.things().size()) continue;
        Fragment::Item item;
        item.block = file_.thingBlockText(i);
        item.hasFrame = frameOf(i, item.frame);
        if (item.hasFrame) { for (int k = 0; k < 3; ++k) f.centre[k] += item.frame.pos[k]; ++n; }
        f.items.push_back(std::move(item));
    }
    if (n) for (int k = 0; k < 3; ++k) f.centre[k] /= float(n);
    return f;
}

std::vector<size_t> Document::paste(const Fragment& fragment, const float at[3], bool dropToGround) {
    std::vector<size_t> out;
    if (fragment.empty()) return out;
    beginBatch();
    pushUndo();
    try {
        for (const auto& item : fragment.items) {
            const size_t idx = file_.insertThingBlockBefore(file_.things().size(), item.block);
            file_.setThingProperty(idx, "UID", std::to_string(forge::thingplacer::nextUid(file_)));
            if (file_.things()[idx].find("ScriptName")) file_.setThingProperty(idx, "ScriptName", "NULL");
            ++revision_;
            if (item.hasFrame) {
                Frame nf = item.frame;
                for (int k = 0; k < 3; ++k) nf.pos[k] = at[k] + (item.frame.pos[k] - fragment.centre[k]);
                if (dropToGround) if (const auto h = groundHeight(nf.pos[0], nf.pos[1])) nf.pos[2] = *h + (item.frame.pos[2] - fragment.centre[2]);
                setFrame(idx, nf);
            }
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
        // a hard swap under the pen (cell centres inside the radius), mirrored into the level like Theme
        const int x0 = std::max(0, int(std::floor(brush.x - brush.radius))), x1 = std::min(cx - 1, int(std::ceil(brush.x + brush.radius)));
        const int y0 = std::max(0, int(std::floor(brush.y - brush.radius))), y1 = std::min(cy - 1, int(std::ceil(brush.y + brush.radius)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float dx = float(x) + 0.5f - brush.x, dy = float(y) + 0.5f - brush.y;
                if (dx * dx + dy * dy > brush.radius * brush.radius) continue;
                const size_t i = size_t(y) * cx + x;
                const forge::terrain::ThemeBlend blend{working_->themeIndex[i], working_->themeStrength[i]};
                const auto next = forge::terrain::replaceThemeInBlend(blend, brush.replaceFrom, brush.themeIndex);
                if (next.indices == blend.indices && next.strengths == blend.strengths) continue;
                working_->themeIndex[i] = next.indices; working_->themeStrength[i] = next.strengths;
                level_->setThemeBlendAt(x, y, next.indices, next.strengths);
            }
        ++terrainRev_;
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
    const bool themes = working_->themeIndex != terrain_->themeIndex || working_->themeStrength != terrain_->themeStrength;
    terrain_ = std::shared_ptr<const TerrainState>(working_.release());
    hf_.reset();
    writeTerrainToLevel();
    ++revision_;
    ++terrainRev_;
    if (themes) ++themeRev_;
}

bool Document::setVertexHeights(const std::vector<VertexHeight>& edits) {
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
    terrain_ = std::shared_ptr<const TerrainState>(next.release());
    hf_.reset();
    writeTerrainToLevel();
    ++revision_;
    ++terrainRev_;
    return true;
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

size_t Document::replaceTheme(uint8_t from, uint8_t to, ReplaceScope scope, float x, float y) {
    if (!hasTerrain() || stroke_ || from == to) return 0;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    auto next = std::make_unique<TerrainState>(*terrain_);
    auto holds = [&](size_t i) {
        for (int k = 0; k < 3; ++k)
            if (next->themeIndex[i][k] == from && next->themeStrength[i][k] > 0) return true;
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
        const auto blend = forge::terrain::replaceThemeInBlend({next->themeIndex[i], next->themeStrength[i]}, from, to);
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

size_t Document::drawPath(float x0, float y0, float x1, float y1, float radius) {
    if (!hasTerrain() || stroke_ || !(radius > 0)) return 0;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    const auto h0 = sampleHeight(*terrain_, cx, cy, x0, y0), h1 = sampleHeight(*terrain_, cx, cy, x1, y1);
    if (!h0 || !h1) return 0;
    const float sx = x1 - x0, sy = y1 - y0, len2 = sx * sx + sy * sy;
    const int vx0 = std::max(0, int(std::floor(std::min(x0, x1) - radius))), vx1 = std::min(cx - 1, int(std::ceil(std::max(x0, x1) + radius)));
    const int vy0 = std::max(0, int(std::floor(std::min(y0, y1) - radius))), vy1 = std::min(cy - 1, int(std::ceil(std::max(y0, y1) + radius)));
    std::vector<VertexHeight> edits;
    for (int y = vy0; y <= vy1; ++y)
        for (int x = vx0; x <= vx1; ++x) {
            const float t = len2 > 0 ? std::clamp(((float(x) - x0) * sx + (float(y) - y0) * sy) / len2, 0.0f, 1.0f) : 0.0f;
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
    if (!hasTerrain() || stroke_) return 0;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    if (before.heights.size() != terrain_->heights.size()) return 0;
    struct Move { size_t index; std::string ctc; float z; };
    std::vector<Move> moves;
    for (size_t i = 0; i < file_.things().size(); ++i) {
        Frame f;
        if (!frameOf(i, f)) continue;
        const auto was = sampleHeight(before, cx, cy, f.pos[0], f.pos[1]);
        const auto now = sampleHeight(*terrain_, cx, cy, f.pos[0], f.pos[1]);
        if (!was || !now || std::fabs(*was - *now) < 1e-4f) continue;
        if (std::fabs(f.pos[2] - *was) > tolerance) continue;   // was floating / sunk on purpose
        const auto* phys = physicsOf(file_.things()[i]);
        moves.push_back({i, phys->name, f.pos[2] + (*now - *was)});
    }
    if (moves.empty()) return 0;
    pushUndo();
    for (const auto& m : moves) file_.setCtcProperty(m.index, m.ctc, "PositionZ", formatFloat(m.z));
    ++revision_;
    return moves.size();
}

namespace {
bool backupOnce(const fs::path& p, std::string& error) { return albion::backups::backupOnce(p, error); }   // <file>.forge-orig, once
} // namespace

bool Document::saveTerrainLoose(const fs::path& gameRoot, std::string& error, std::vector<std::string>* notes) {
    if (!hasTerrain()) { error = "no terrain loaded"; return false; }
    const fs::path path = external() ? externalLev_ : gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName_ + ".lev");
    try {
        fs::create_directories(path.parent_path());
        if (!backupOnce(path, error)) return false;
        // a loose file that did not exist before gets a marker so tooling can
        // tell it apart from the user's own loose levels
        if (!fs::exists(path)) albion::backups::markCreated(path);
        level_->save(path);
        savedTerrain_ = terrain_;
        soundListGrew_ = false;

        // navigation: patch the retail quadtree for the cells whose walkable byte changed
        std::vector<std::pair<int, int>> changed;
        const int cx = level_->cellsX(), cy = level_->cellsY();
        if (navWalkable_.size() == terrain_->walkable.size())
            for (int y = 0; y < cy; ++y)
                for (int x = 0; x < cx; ++x)
                    if (navWalkable_[size_t(y) * cx + x] != terrain_->walkable[size_t(y) * cx + x]) changed.push_back({x, y});
        if (!changed.empty() && !level_->navSections().empty()) {
            const auto saved = forge::lev::File::open(path);
            auto nav = forge::navmesh::parseNavigation(saved);
            const auto st = forge::navmesh::patchWalkability(nav, saved, changed);
            const auto bytes = forge::navmesh::emitNavigation(saved, nav);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!out) throw std::runtime_error("cannot write " + path.string());
            out.close();
            *level_ = forge::lev::File::open(path);   // the nav suffix is part of the bytes later saves copy
            navWalkable_ = terrain_->walkable;
            if (notes) {
                char line[256];
                std::snprintf(line, sizeof line, "navigation: %zu cell(s) changed -> %zu leaves removed, %zu added, %zu split, %zu region(s) added, %zu merged (%zu section(s))",
                              changed.size(), st.leavesRemoved, st.leavesAdded, st.nodesSplit, st.regionsAdded, st.regionsMerged, nav.sections.size());
                notes->push_back(line);
                if (st.cellsSkipped) notes->push_back("navigation: " + std::to_string(st.cellsSkipped) + " opened cell(s) already had nav coverage");
            }
        }
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
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
    // what the .lev was before: the loose file (or its absence) and the WAD entry
    const fs::path loose = gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName_ + ".lev");
    const fs::path wad = gameRoot / "data" / "Levels" / "FinalAlbion.wad";
    const bool hadLoose = fs::exists(loose);
    std::string oldLoose, wadEntry;
    std::vector<uint8_t> oldWad;
    const auto oldSaved = savedTerrain_;
    try {
        if (hadLoose) oldLoose = readFile(loose);
        if (fs::exists(wad)) {
            const auto archive = forge::wad::Archive::open(wad);
            const std::string want = lower(mapName_) + ".lev";
            for (const auto& e : archive.entries())
                if (lower(fs::path(e.name).filename().string()) == want) { wadEntry = e.name; oldWad = archive.read(e); break; }
        }
    } catch (const std::exception& e) { error = e.what(); return false; }

    if (deployTerrainSteps(gameRoot, notes, error, library, progress)) return true;

    // roll the .lev back; the STB is only written as the last step
    std::string rollback;
    try {
        if (hadLoose) {
            std::ofstream out(loose, std::ios::binary | std::ios::trunc);
            out.write(oldLoose.data(), std::streamsize(oldLoose.size()));
            if (!out) throw std::runtime_error("cannot restore " + loose.string());
        } else if (fs::exists(loose)) {
            fs::remove(loose);
            std::error_code ec;
            fs::remove(loose.string() + albion::backups::kCreatedSuffix, ec);
        }
        if (!wadEntry.empty()) {
            const auto archive = forge::wad::Archive::open(wad);
            for (const auto& e : archive.entries())
                if (e.name == wadEntry && archive.read(e) != oldWad) {
                    std::map<std::string, std::vector<uint8_t>> rep{{wadEntry, oldWad}};
                    const fs::path temp = wad.string() + ".atlas-tmp";
                    forge::wad::repack(wad, rep, temp);
                    fs::rename(temp, wad);
                    break;
                }
        }
        // the edit is unsaved again. navWalkable_ stays: the in-memory level
        // already carries the patched navigation the next save writes out
        savedTerrain_ = oldSaved;
    } catch (const std::exception& e) { rollback = e.what(); }
    error += rollback.empty() ? " (the .lev was put back; nothing changed on disk)"
                              : " (and putting the .lev back failed: " + rollback + "; restore it from the .forge-orig backup)";
    return false;
}

bool Document::deployTerrainSteps(const fs::path& gameRoot, std::vector<std::string>& notes, std::string& error,
                                  const forge::terraintex::ThemeLibrary* library,
                                  const std::function<void(const std::string&)>& progress) {
    const auto stage = [&](const char* s) { if (progress) progress(s); };
    if (!hasTerrain()) { error = "no terrain loaded"; return false; }
    const bool themesChanged = themesDirty();
    if (themesChanged && !library) { error = "ground themes were painted but the ENGINE_THEME library is not loaded (textures not ready)"; return false; }
    const std::shared_ptr<const TerrainState> before = savedTerrain_;   // the ground the chunk's foliage sits on
    try {
        // 1. loose .lev (also the bytes for the WAD)
        stage("writing the .lev (navigation patch)");
        if (!saveTerrainLoose(gameRoot, error, &notes)) return false;
        const fs::path loose = gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName_ + ".lev");
        const std::string levBytes = readFile(loose);
        notes.push_back("wrote " + loose.string());

        // 2. FinalAlbion.wad entry (patched in place when the size is unchanged,
        //    relocated to the end of the payload after a navigation patch)
        const fs::path wad = gameRoot / "data" / "Levels" / "FinalAlbion.wad";
        if (fs::exists(wad)) {
            const auto archive = forge::wad::Archive::open(wad);
            std::string entryName;
            const std::string want = lower(mapName_) + ".lev";
            for (const auto& e : archive.entries())
                if (lower(fs::path(e.name).filename().string()) == want) { entryName = e.name; break; }
            if (entryName.empty()) { error = mapName_ + ".lev is not in FinalAlbion.wad"; return false; }
            if (!backupOnce(wad, error)) return false;
            std::map<std::string, std::vector<uint8_t>> rep;
            rep[entryName] = std::vector<uint8_t>(levBytes.begin(), levBytes.end());
            const fs::path temp = wad.string() + ".atlas-tmp";
            forge::wad::repack(wad, rep, temp);
            fs::rename(temp, wad);
            notes.push_back("replaced " + entryName + " in FinalAlbion.wad");
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
            notes.push_back("ground themes painted: layer meshes rebuilt from the LEV palette, distant-LOD textures re-baked");
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
            const fs::path tmp = fs::temp_directory_path() / "FableForge" / "neighbours";
            fs::create_directories(tmp);
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
                    notes.push_back("neighbour " + leaf + " at (" + std::to_string(nm->mapX) + "," + std::to_string(nm->mapY) + ")");
                } catch (const std::exception&) {}
            }
        }
        opt.deferOversizedPatches = true;   // tall edits: those patches grow below instead of failing the bake
        opt.rebakeLodPatches = true;        // and the distant-view LOD patches follow the new ground
        const auto baked = forge::stbbake::bakeHeightfield(chunk, *level_, wm->mapX, wm->mapY, opt);
        for (const auto& n : baked.notes) if (n.rfind("foreground frame", 0) != 0) notes.push_back(n);
        if (baked.chunk.size() != chunk.size()) { error = "baked chunk changed size (" + std::to_string(baked.chunk.size()) + " vs " + std::to_string(chunk.size()) + ")"; return false; }
        // camera height bounds in the common record
        float minH = 1e30f, maxH = -1e30f;
        for (float h : terrain_->heights) { minH = std::min(minH, h); maxH = std::max(maxH, h); }
        auto info = forge::stbinfo::readInfoBlock(record.data());
        forge::stbbake::setRetailCameraHeightBounds(info, minH, maxH);
        const auto encoded = forge::stbinfo::writeInfoBlock(info);
        if (!backupOnce(stb, error)) return false;
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
            if (baked.lodPatches) notes.push_back(std::to_string(baked.lodPatches) + " distant-view LOD patch(es) re-sampled from the new ground");
            if (grown) notes.push_back(std::to_string(grown) + " terrain patch(es) outgrew their slot and were re-laid");
            if (!rr.notes.empty()) notes.push_back(rr.notes.back());
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
                notes.push_back("foliage re-seated on the new ground (" + std::to_string(rr.groupFrames) + " cache groups, bounds grown by " + std::to_string(slack) + ")");
            }
        }
        if (outChunk.size() == chunk.size()) {
            std::fstream io(stb, std::ios::binary | std::ios::in | std::ios::out);
            if (!io) { error = "cannot open " + stb.string() + " for writing"; return false; }
            io.seekp(std::streamoff(entry->offset));
            io.write(reinterpret_cast<const char*>(outChunk.data()), std::streamsize(outChunk.size()));
            io.seekp(std::streamoff(map->absoluteOffset));
            io.write(reinterpret_cast<const char*>(outRecord.data()), std::streamsize(outRecord.size()));
            if (!io) { error = "write to " + stb.string() + " failed"; return false; }
        } else {
            std::vector<forge::stb::StaticMapAppend> batch;
            batch.push_back({map->levelName, entry->name, outChunk, outRecord});
            const fs::path tmp = stb.string() + ".atlas-tmp";
            forge::stb::replaceStaticMapsRelayout(stb, tmp, batch);
            fs::rename(tmp, stb);
        }
        notes.push_back("re-baked terrain chunk " + std::to_string(outChunk.size()) + " bytes (" + std::to_string(baked.patches) + " patches, " + std::to_string(baked.foregroundFrames) + " layer frames) into FinalAlbion_RT.stb" + (outChunk.size() == chunk.size() ? "" : " (chunk re-laid)"));
        (void)foliageRode;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

void Document::setFrame(size_t index, const Frame& frame) {
    if (index >= file_.things().size()) throw std::out_of_range("setFrame: bad thing index");
    const auto& t = file_.things()[index];
    const auto* phys = physicsOf(t);
    if (!phys) throw std::runtime_error("setFrame: thing has no physics block");
    const std::string ctc = phys->name;
    pushUndo();
    file_.setCtcProperty(index, ctc, "PositionX", formatFloat(frame.pos[0]));
    file_.setCtcProperty(index, ctc, "PositionY", formatFloat(frame.pos[1]));
    file_.setCtcProperty(index, ctc, "PositionZ", formatFloat(frame.pos[2]));
    file_.setCtcProperty(index, ctc, "RHSetForwardX", formatFloat(frame.forward[0]));
    file_.setCtcProperty(index, ctc, "RHSetForwardY", formatFloat(frame.forward[1]));
    file_.setCtcProperty(index, ctc, "RHSetForwardZ", formatFloat(frame.forward[2]));
    file_.setCtcProperty(index, ctc, "RHSetUpX", formatFloat(frame.up[0]));
    file_.setCtcProperty(index, ctc, "RHSetUpY", formatFloat(frame.up[1]));
    file_.setCtcProperty(index, ctc, "RHSetUpZ", formatFloat(frame.up[2]));
    const bool hasScale = file_.things()[index].find("ObjectScale").has_value();
    if (std::fabs(frame.scale - 1.0f) > 1e-6f) file_.setThingProperty(index, "ObjectScale", formatFloat(frame.scale));
    else if (hasScale) file_.removeThingProperty(index, "ObjectScale");
    ++revision_;
}

void Document::setProperty(size_t index, const std::string& key, const std::string& value) {
    if (index >= file_.things().size()) throw std::out_of_range("setProperty: bad thing index");
    pushUndo();
    file_.setThingProperty(index, key, value);
    ++revision_;
}

namespace {
struct LinkKind { const char* ctc; const char* field; const char* label; const char* wants; };
// the UID links retail .tng files use (counted over the dev tree's FinalAlbion: OwnerUID 2434,
// VillageUID 2347, EntranceConnectedToUID 200, ReceptorUID 76, route 46, home/work/wife 4-6)
constexpr LinkKind kLinkKinds[] = {
    {"CTCVillageMember", "VillageUID", "Village", "a village"},
    {"CTCOwnedEntity", "OwnerUID", "Owned by", "any thing"},
    {"", "HomeBuildingUID", "Lives in", "a building"},
    {"", "WorkBuildingUID", "Works in", "a building"},
    {"CTCBuyableHouse", "WifeLivingHereUID", "Wife living here", "a creature"},
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
    for (const auto& p : t.properties)
        if (!hiddenProperty("", p.key)) out.push_back({"", p.key, p.value, kindOf(p.value)});
    for (const auto& b : t.ctcBlocks)
        for (const auto& p : b.properties)
            if (!hiddenProperty(b.name, p.key)) out.push_back({b.name, p.key, p.value, kindOf(p.value)});
    return out;
}

bool Document::setPropertyValue(size_t index, const std::string& ctc, const std::string& key, const std::string& value) {
    if (index >= file_.things().size() || hiddenProperty(ctc, key)) return false;
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
    if (ctc.empty()) file_.setThingProperty(index, key, value);
    else file_.setCtcProperty(index, ctc, key, value);
    ++revision_;
    return true;
}

std::vector<Document::Link> Document::linksOf(size_t index) const {
    std::vector<Link> out;
    if (index >= file_.things().size()) return out;
    const auto& t = file_.things()[index];
    for (const auto& k : kLinkKinds) {
        std::optional<std::string> value;
        if (k.ctc[0]) {
            const auto* block = t.findCtc(k.ctc);
            if (!block) continue;
            value = std::string("0");
            for (const auto& p : block->properties)
                if (lower(p.key) == lower(k.field)) { value = p.value; break; }
        } else {
            value = t.find(k.field);
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
    if (link.field == "WifeLivingHereUID") return lower(t.type) == "aicreature" || lower(t.type) == "creature";
    return true;
}

bool Document::setLink(size_t index, const std::string& ctc, const std::string& field, uint64_t targetUid) {
    if (index >= file_.things().size()) return false;
    const auto links = linksOf(index);
    const auto hit = std::find_if(links.begin(), links.end(), [&](const Link& l) { return l.ctc == ctc && l.field == field; });
    if (hit == links.end()) return false;
    if (hit->target == targetUid) return true;
    pushUndo();
    if (ctc.empty()) file_.setThingProperty(index, field, std::to_string(targetUid));
    else file_.setCtcProperty(index, ctc, field, std::to_string(targetUid));
    ++revision_;
    return true;
}

std::string Document::targetSection() const {
    for (const auto& n : file_.sectionNames())
        if (lower(n) == lower(placementSection_)) return n;
    return "NULL";
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

std::optional<size_t> Document::moveToSection(size_t index, const std::string& name) {
    if (index >= file_.things().size()) return std::nullopt;
    std::string target;
    for (const auto& n : file_.sectionNames())
        if (lower(n) == lower(name)) target = n;
    if (target.empty()) return std::nullopt;
    if (lower(file_.sectionOf(index)) == lower(target)) return index;
    pushUndo();
    const std::string block = file_.thingBlockText(index);
    file_.removeThing(index);
    const size_t n = file_.insertThingBlock(target, block);
    ++revision_;
    return n;
}

size_t Document::duplicate(size_t index) {
    if (index >= file_.things().size()) throw std::out_of_range("duplicate: bad thing index");
    pushUndo();
    std::string block = file_.thingBlockText(index);
    const size_t newIndex = file_.insertThingBlockBefore(index + 1, block);
    file_.setThingProperty(newIndex, "UID", std::to_string(forge::thingplacer::nextUid(file_)));
    if (file_.things()[newIndex].find("ScriptName")) file_.setThingProperty(newIndex, "ScriptName", "NULL");
    ++revision_;
    return newIndex;
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

size_t Document::placeCreature(const float pos[3], const float forward[2], const std::string& definition, const std::string& scriptName) {
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
        b += "Player 0;" + eol;
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

size_t Document::placeVillage(const float pos[3], const std::string& definition, const std::string& scriptName) {
    if (definition.empty()) throw std::invalid_argument("a village needs a definition");
    pushUndo();
    try {
        // the retail block (StartOakValeWest V_OakVale)
        const std::string eol = "\r\n";
        std::string b;
        b += "NewThing Village;" + eol;
        b += "Player 4;" + eol;
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

void Document::remove(size_t index) {
    if (index >= file_.things().size()) throw std::out_of_range("remove: bad thing index");
    pushUndo();
    file_.removeThing(index);
    ++revision_;
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
        if (fs::exists(path)) { if (!albion::backups::hasOriginal(path) && !fs::exists(path.string() + ".forge-created") && !fs::exists(path.string() + ".atlas-created")) fs::copy_file(path, path.string() + albion::backups::kOrigSuffix); }
        else albion::backups::markCreated(path);   // the backup manager deletes it on restore
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { error = "cannot write " + path.string(); return false; }
        f.write(text.data(), std::streamsize(text.size()));
        loosePath_ = path;
        original_ = text; dirtyRev_ = ~0ull;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool Document::deployWad(const fs::path& gameRoot, std::string& error) {
    // a loose-level install (no FinalAlbion.wad, the levels extracted to
    // FinalAlbion\*): the loose .tng is what the game reads, so it is the deploy.
    // Never recreate the WAD there: it would override every loose file.
    // another world's .tng beside its .lev is likewise the file the game reads
    if (external() || forge::levelstore::detect(gameRoot).looseOnly()) return saveLoose(gameRoot, error);
    const fs::path wad = gameRoot / "data" / "Levels" / "FinalAlbion.wad";
    const fs::path backup = albion::backups::originalOf(wad);
    const fs::path temp = wad.string() + ".forge-tmp";
    try {
        if (!fs::exists(wad)) { error = "no " + wad.string(); return false; }
        const auto archive = forge::wad::Archive::open(wad);
        std::string entryName;
        const std::string want = lower(mapName_) + ".tng";
        for (const auto& e : archive.entries())
            if (lower(fs::path(e.name).filename().string()) == want) { entryName = e.name; break; }
        if (entryName.empty()) { error = mapName_ + ".tng is not in FinalAlbion.wad"; return false; }
        if (!fs::exists(backup)) fs::copy_file(wad, backup);
        const std::string text = file_.serialize();
        std::map<std::string, std::vector<uint8_t>> rep;
        rep[entryName] = std::vector<uint8_t>(text.begin(), text.end());
        forge::wad::repack(wad, rep, temp);
        fs::rename(temp, wad);
        // a loose copy (the user's, or ours) would otherwise go stale and shadow the WAD on read
        const fs::path loose = gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName_ + ".tng");
        if (fs::exists(loose)) {
            if (!albion::backups::hasOriginal(loose)) fs::copy_file(loose, loose.string() + albion::backups::kOrigSuffix);
            std::ofstream f(loose, std::ios::binary | std::ios::trunc);
            f.write(text.data(), std::streamsize(text.size()));
        }
        original_ = text; dirtyRev_ = ~0ull;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        std::error_code ec; fs::remove(temp, ec);
        return false;
    }
}

} // namespace albion::editor
