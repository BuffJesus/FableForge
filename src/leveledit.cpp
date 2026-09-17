#include "leveledit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
#include "lodbake.hpp"
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

bool Document::open(const fs::path& gameRoot, const std::string& mapName, const fs::path& levPath, std::string& error) {
    std::string text;
    fromWad_ = false;
    loosePath_ = gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName + ".tng");
    if (fs::exists(loosePath_)) {
        text = readFile(loosePath_);
    } else {
        try {
            const auto wad = forge::wad::Archive::open(gameRoot / "data" / "Levels" / "FinalAlbion.wad");
            const std::string want = lower(mapName) + ".tng";
            for (const auto& e : wad.entries())
                if (lower(fs::path(e.name).filename().string()) == want) {
                    const auto bytes = wad.read(e);
                    text.assign(bytes.begin(), bytes.end());
                    fromWad_ = true;
                    break;
                }
        } catch (const std::exception& e) { error = e.what(); return false; }
        if (text.empty()) { error = "no " + mapName + ".tng loose or in FinalAlbion.wad"; return false; }
    }
    if (!openText(mapName, std::move(text), error)) return false;
    if (!levPath.empty()) loadLevel(levPath, error);
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
        t->themeIndex.resize(size_t(cx) * cy);
        t->themeStrength.resize(size_t(cx) * cy);
        for (int y = 0; y < cy; ++y)
            for (int x = 0; x < cx; ++x) {
                const size_t i = size_t(y) * cx + x;
                t->heights[i] = level_->heightAt(x, y);
                t->walkable[i] = level_->walkableAt(x, y) ? 1 : 0;
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
    undo_.push_back(snapshot());
    if (undo_.size() > kUndoDepth) undo_.erase(undo_.begin());
    redo_.clear();
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
    const int cx = level_->cellsX(), cy = level_->cellsY();
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) {
            const size_t i = size_t(y) * cx + x;
            if (level_->heightAt(x, y) != terrain_->heights[i]) level_->setHeightAt(x, y, terrain_->heights[i]);
            if (level_->walkableAt(x, y) != (terrain_->walkable[i] != 0)) level_->setWalkableAt(x, y, terrain_->walkable[i] != 0);
            bool sameTheme = true;
            for (int k = 0; k < 3; ++k) sameTheme = sameTheme && level_->themeIndexAt(x, y, k) == terrain_->themeIndex[i][k] && level_->themeStrengthAt(x, y, k) == terrain_->themeStrength[i][k];
            if (!sameTheme) level_->setThemeBlendAt(x, y, terrain_->themeIndex[i], terrain_->themeStrength[i]);
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

bool Document::themesDirty() const {
    if (!terrain_ || !savedTerrain_ || terrain_ == savedTerrain_) return false;
    return terrain_->themeIndex != savedTerrain_->themeIndex || terrain_->themeStrength != savedTerrain_->themeStrength;
}

bool Document::terrainDirty() const {
    if (!terrain_ || !savedTerrain_) return false;
    if (terrain_ == savedTerrain_) return false;
    return terrain_->heights != savedTerrain_->heights || terrain_->walkable != savedTerrain_->walkable ||
           terrain_->themeIndex != savedTerrain_->themeIndex || terrain_->themeStrength != savedTerrain_->themeStrength;
}

std::optional<float> Document::terrainHeight(float x, float y) const {
    const TerrainState* t = stroke_ && working_ ? working_.get() : terrain_.get();
    if (!t || !level_) return std::nullopt;
    const int cx = level_->cellsX(), cy = level_->cellsY();
    if (!(x >= 0 && y >= 0) || x > float(cx - 1) || y > float(cy - 1)) return std::nullopt;
    const int x0 = std::min(int(x), cx - 1), y0 = std::min(int(y), cy - 1);
    const int x1 = std::min(x0 + 1, cx - 1), y1 = std::min(y0 + 1, cy - 1);
    const float fx = x - float(x0), fy = y - float(y0);
    auto h = [&](int xx, int yy) { return t->heights[size_t(yy) * cx + xx]; };
    return (h(x0, y0) * (1 - fx) + h(x1, y0) * fx) * (1 - fy) + (h(x0, y1) * (1 - fx) + h(x1, y1) * fx) * fy;
}

namespace {
bool backupOnce(const fs::path& p, std::string& error) {
    const fs::path b = p.string() + ".atlas-orig";
    try { if (fs::exists(p) && !fs::exists(b)) fs::copy_file(p, b); return true; }
    catch (const std::exception& e) { error = e.what(); return false; }
}
} // namespace

bool Document::saveTerrainLoose(const fs::path& gameRoot, std::string& error, std::vector<std::string>* notes) {
    if (!hasTerrain()) { error = "no terrain loaded"; return false; }
    const fs::path path = gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName_ + ".lev");
    try {
        fs::create_directories(path.parent_path());
        if (!backupOnce(path, error)) return false;
        // a loose file that did not exist before gets a marker so tooling can
        // tell it apart from the user's own loose levels
        if (!fs::exists(path)) std::ofstream(path.string() + ".atlas-created") << "created by Albion Atlas\n";
        level_->save(path);
        savedTerrain_ = terrain_;

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
                             const forge::terraintex::ThemeLibrary* library) {
    if (!hasTerrain()) { error = "no terrain loaded"; return false; }
    const bool themesChanged = themesDirty();
    if (themesChanged && !library) { error = "ground themes were painted but the ENGINE_THEME library is not loaded (textures not ready)"; return false; }
    try {
        // 1. loose .lev (also the bytes for the WAD)
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
            const fs::path tmp = fs::temp_directory_path() / "Albion Atlas" / "neighbours";
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
        std::fstream io(stb, std::ios::binary | std::ios::in | std::ios::out);
        if (!io) { error = "cannot open " + stb.string() + " for writing"; return false; }
        io.seekp(std::streamoff(entry->offset));
        io.write(reinterpret_cast<const char*>(baked.chunk.data()), std::streamsize(baked.chunk.size()));
        io.seekp(std::streamoff(map->absoluteOffset));
        io.write(reinterpret_cast<const char*>(encoded.data()), std::streamsize(encoded.size()));
        if (!io) { error = "write to " + stb.string() + " failed"; return false; }
        notes.push_back("re-baked terrain chunk " + std::to_string(baked.chunk.size()) + " bytes (" + std::to_string(baked.patches) + " patches, " + std::to_string(baked.foregroundFrames) + " layer frames) into FinalAlbion_RT.stb");
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
        return r.thingIndex;
    } catch (...) {
        restore(undo_.back()); undo_.pop_back();
        throw;
    }
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
    const fs::path path = gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName_ + ".tng");
    try {
        fs::create_directories(path.parent_path());
        const std::string text = file_.serialize();
        if (fs::exists(path) && !fs::exists(path.string() + ".atlas-orig")) fs::copy_file(path, path.string() + ".atlas-orig");
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { error = "cannot write " + path.string(); return false; }
        f.write(text.data(), std::streamsize(text.size()));
        loosePath_ = path;
        original_ = text; dirtyRev_ = ~0ull;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool Document::deployWad(const fs::path& gameRoot, std::string& error) {
    const fs::path wad = gameRoot / "data" / "Levels" / "FinalAlbion.wad";
    const fs::path backup = wad.string() + ".atlas-orig";
    const fs::path temp = wad.string() + ".atlas-tmp";
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
            if (!fs::exists(loose.string() + ".atlas-orig")) fs::copy_file(loose, loose.string() + ".atlas-orig");
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
