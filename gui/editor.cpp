// Albion Atlas editor: selection, gizmo, edit panel and document plumbing.
// The App methods that make the viewer an editor live here; app.cpp keeps the
// layout, explorer, export and automation.

#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "ImGuizmo.h"
#include "theme.hpp"

namespace fs = std::filesystem;
namespace te = albion::terrainexport;

namespace albion::gui {

namespace {

// Row-vector axis swap Fable (x, y, z-up) -> render (x, z, -y), and back.
const float kToRender[16] = {1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1};
const float kToFable[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1};

std::string lowerCopy(std::string s) { for (auto& c : s) c = char(std::tolower(uint8_t(c))); return s; }

bool contains(const std::string& hay, const char* needle) {
    if (!needle || !*needle) return true;
    return lowerCopy(hay).find(lowerCopy(needle)) != std::string::npos;
}

float yawDegrees(const editor::Frame& f) {
    return std::atan2(f.forward[1], f.forward[0]) * 180.0f / 3.14159265f;
}

} // namespace

// ------------------------------------------------------------ document

void App::openDocument() {
    docLoadedFor_.clear();
    selectedThing_ = -1; selectedUid_ = 0;
    renderer_.selectedThing = -1;
    instLocal_.clear(); instUids_.clear();
    const MapEntry* e = findEntry(selectedName_);
    if (!e || !installValid_) return;
    std::string err;
    const std::string lev = resolveLevPath(*e, err);
    std::string derr;
    if (!doc_.open(installPath_, e->name, lev, derr)) {
        if (!derr.empty()) pushLog("editor: " + derr, 1);
        return;
    }
    if (!derr.empty()) pushLog("editor: " + derr, 1);
    docLoadedFor_ = selectedName_;
    syncedRevision_ = doc_.revision();
}

void App::setEditMode(bool on) {
    editMode_ = on;
    if (on && !documentLoaded()) openDocument();
    if (on && previewThings_ == false) setPreviewThings(true);
    if (!on) { selectedThing_ = -1; selectedUid_ = 0; renderer_.selectedThing = -1; }
}

void App::bindInstances(const foliageexport::Scene& things) {
    instLocal_.clear();
    instUids_.clear();
    if (!documentLoaded()) return;
    for (size_t i = 0; i < doc_.thingCount(); ++i) instUids_.push_back(doc_.uidOf(i));
    instLocal_.reserve(things.instances.size());
    for (const auto& inst : things.instances) {
        if (inst.mesh < 0) continue;   // the renderer skipped it too
        float col[3][3]; foliageexport::instanceBasis(inst, col);
        std::array<float, 16> w = {col[0][0], col[0][1], col[0][2], 0,
                                   col[1][0], col[1][1], col[1][2], 0,
                                   col[2][0], col[2][1], col[2][2], 0,
                                   inst.x, inst.y, inst.z, 1};
        editor::Frame f;
        if (inst.thing >= 0 && doc_.frameOf(size_t(inst.thing), f)) {
            float t[16], inv[16];
            editor::frameToMatrix(f, t);
            if (editor::invert(t, inv)) editor::multiply(w.data(), inv, w.data());
        }
        instLocal_.push_back(w);
    }
    syncedRevision_ = doc_.revision();
    // keep the selection across a reload
    if (selectedUid_) {
        const auto idx = doc_.indexOfUid(selectedUid_);
        selectedThing_ = idx ? int(*idx) : -1;
        if (!idx) selectedUid_ = 0;
        renderer_.selectedThing = selectedThing_;
    }
}

void App::applyFrame(int thing, const editor::Frame& f) {
    if (thing < 0 || instLocal_.size() != renderer_.instanceCount()) return;
    float t[16]; editor::frameToMatrix(f, t);
    for (size_t i = 0; i < instLocal_.size(); ++i) {
        if (renderer_.instance(i).thing != thing) continue;
        float w[16]; editor::multiply(instLocal_[i].data(), t, w);
        renderer_.setInstanceWorld(i, w);
    }
}

void App::syncInstances() {
    if (!documentLoaded() || doc_.revision() == syncedRevision_) return;
    bool sameThings = instUids_.size() == doc_.thingCount() && instLocal_.size() == renderer_.instanceCount();
    for (size_t i = 0; sameThings && i < instUids_.size(); ++i) sameThings = instUids_[i] == doc_.uidOf(i);
    if (sameThings) {
        for (size_t i = 0; i < doc_.thingCount(); ++i) {
            editor::Frame f;
            if (doc_.frameOf(i, f)) applyFrame(int(i), f);
        }
        syncedRevision_ = doc_.revision();
    } else {
        startThingsReload();
    }
    if (selectedUid_) {
        const auto idx = doc_.indexOfUid(selectedUid_);
        selectedThing_ = idx ? int(*idx) : -1;
        renderer_.selectedThing = selectedThing_;
    }
}

void App::startThingsReload() {
    if (!documentLoaded() || !ctx_.ready()) return;
    if (foliageFuture_.valid()) { thingsReloadPending_ = true; return; }
    thingsReloadPending_ = false;
    syncedRevision_ = doc_.revision();
    const MapEntry* found = findEntry(selectedName_);
    if (!found) return;
    const MapEntry entry = *found;
    const te::Context* ctx = &ctx_;
    const std::string root = installPath_;
    const std::string text = doc_.text();
    foliageFuture_ = std::async(std::launch::async, [entry, ctx, root, text]() {
        FoliageResult r; r.name = entry.key; r.thingsOnly = true;
        thingsexport::Options to;
        to.gameRoot = root;
        to.textures = true;
        to.up = te::UpAxis::Y;
        to.tngText = text;
        try { r.things = thingsexport::load(entry.name, to, *ctx, &r.thingStats); } catch (const std::exception& e) { r.things.warnings.push_back(e.what()); }
        return r;
    });
}

// ------------------------------------------------------------ selection

void App::selectThing(int index) {
    if (!documentLoaded() || index < 0 || size_t(index) >= doc_.thingCount()) { selectedThing_ = -1; selectedUid_ = 0; renderer_.selectedThing = -1; return; }
    selectedThing_ = index;
    selectedUid_ = doc_.uidOf(size_t(index));
    renderer_.selectedThing = index;
}

int App::selectByDefinition(const std::string& def) {
    if (!documentLoaded()) return -1;
    for (size_t i = 0; i < doc_.thingCount(); ++i)
        if (doc_.summary(i).definition == def) { selectThing(int(i)); return int(i); }
    selectThing(-1);
    return -1;
}

bool App::selectedPivotScreen(float& x, float& y) const {
    editor::Frame f;
    if (!frameOfSelected(f) || viewportSize_.x <= 0) return false;
    const float p[4] = {f.pos[0], f.pos[2], -f.pos[1], 1.0f};   // render space
    const float* v = renderer_.viewMatrix();
    const float* pr = renderer_.projMatrix();
    float e[4], c[4];
    for (int j = 0; j < 4; ++j) e[j] = p[0] * v[j] + p[1] * v[4 + j] + p[2] * v[8 + j] + p[3] * v[12 + j];
    for (int j = 0; j < 4; ++j) c[j] = e[0] * pr[j] + e[1] * pr[4 + j] + e[2] * pr[8 + j] + e[3] * pr[12 + j];
    if (c[3] <= 1e-6f) return false;
    x = viewportOrigin_.x + (c[0] / c[3] * 0.5f + 0.5f) * viewportSize_.x;
    y = viewportOrigin_.y + (0.5f - c[1] / c[3] * 0.5f) * viewportSize_.y;
    return true;
}

int App::pickAt(float u, float v) {
    float o[3], d[3];
    renderer_.screenRay(u, v, o, d);
    float t;
    const int inst = renderer_.pick(o, d, t);
    if (inst < 0) { selectThing(-1); return -1; }
    const int thing = renderer_.instance(size_t(inst)).thing;
    selectThing(thing);
    return thing;
}

bool App::frameOfSelected(editor::Frame& f) const {
    return documentLoaded() && selectedThing_ >= 0 && doc_.frameOf(size_t(selectedThing_), f);
}

void App::commitFrame(const editor::Frame& f) {
    if (!documentLoaded() || selectedThing_ < 0) return;
    try { doc_.setFrame(size_t(selectedThing_), f); }
    catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); }
}

void App::moveSelected(float dx, float dy, float dz) {
    editor::Frame f;
    if (!frameOfSelected(f)) return;
    f.pos[0] += dx; f.pos[1] += dy; f.pos[2] += dz;
    commitFrame(f);
}

void App::rotateSelected(float degrees) {
    editor::Frame f;
    if (!frameOfSelected(f)) return;
    // rotate forward about the up axis (Rodrigues)
    const float a = degrees * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
    float u[3] = {f.up[0], f.up[1], f.up[2]};
    const float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (ul > 1e-9f) for (float& x : u) x /= ul; else { u[0] = 0; u[1] = 0; u[2] = 1; }
    const float* w = f.forward;
    const float dot = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
    const float cr[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
    float r[3];
    for (int k = 0; k < 3; ++k) r[k] = w[k] * c + cr[k] * s + u[k] * dot * (1 - c);
    f.forward[0] = r[0]; f.forward[1] = r[1]; f.forward[2] = r[2];
    commitFrame(f);
}

void App::scaleSelected(float factor) {
    editor::Frame f;
    if (!frameOfSelected(f)) return;
    f.scale = std::clamp(f.scale * factor, 0.01f, 100.0f);
    commitFrame(f);
}

bool App::createCustomTheme(const std::string& png, const std::string& name, const std::string& donor, const std::string& cliffPng) {
    if (!documentLoaded() || !doc_.hasTerrain()) { pushLog("editor: no terrain loaded", 1); return false; }
    if (ctxFuture_.valid()) { pushLog("editor: textures are still loading, try again in a moment", 1); return false; }
    editor::CustomThemeRequest req;
    req.png = png; req.name = name; req.donor = donor.empty() ? "GROUND_GRASS" : donor;
    if (!cliffPng.empty()) req.cliffPng = cliffPng;
    editor::CustomThemeResult out; std::string err;
    pushLog("custom theme " + name + ": importing " + png + " into textures.big and appending the ENGINE_THEME...", 0);
    if (!editor::createCustomTheme(saveRoot(), req, out, err)) { pushLog("editor: custom theme failed: " + err, 2); return false; }
    for (const auto& n : out.notes) pushLog("custom theme: " + n, 0);
    const int slot = doc_.addGroundTheme(name, out.defIndex);
    if (slot < 0) { pushLog("editor: the palette has no free slot for " + name, 1); return false; }
    paintTheme_ = slot;
    pushLog("ground theme " + name + " in palette slot " + std::to_string(slot) + " (def " + std::to_string(out.defIndex) + ", texture " + std::to_string(out.baseTexture) + "); reloading textures", 3);
    // the theme library and texture cache must see the new entries before the
    // preview bake and the deploy (deploy is refused while they reload); they
    // live wherever the save root points (a scratch tree in tests)
    startContextLoad(saveRoot());
    return true;
}

bool App::addPaintTheme(const std::string& name) {
    if (!documentLoaded() || !doc_.hasTerrain()) { pushLog("editor: no terrain loaded", 1); return false; }
    const forge::terraintex::ThemeLibrary* lib = ctx_.themeLibrary();
    const auto* th = lib ? lib->byName(name) : nullptr;
    if (!th) { pushLog("editor: no ENGINE_THEME named " + name + " (library not loaded?)", 1); return false; }
    const int slot = doc_.addGroundTheme(th->name, th->defIndex);
    if (slot < 0) { pushLog("editor: the palette has no free slot for " + name, 1); return false; }
    paintTheme_ = slot;
    pushLog("ground theme " + name + " in palette slot " + std::to_string(slot), 0);
    return true;
}

void App::reseatThings() {
    if (!documentLoaded() || !doc_.hasTerrain()) return;
    const size_t n = doc_.reseatThingsSinceSave();
    if (!n) { pushLog("editor: no object stood on ground that changed", 0); return; }
    pushLog("editor: " + std::to_string(n) + " object(s) re-seated on the sculpted ground", 0);
}

void App::snapSelectedToGround() {
    editor::Frame f;
    if (!frameOfSelected(f)) return;
    const auto h = doc_.groundHeight(f.pos[0], f.pos[1]);
    if (!h) { pushLog("editor: no terrain height under the object", 1); return; }
    f.pos[2] = *h;
    commitFrame(f);
}

void App::duplicateSelected() {
    if (!documentLoaded() || selectedThing_ < 0) return;
    try {
        const size_t n = doc_.duplicate(size_t(selectedThing_));
        selectedUid_ = doc_.uidOf(n);
        selectedThing_ = int(n);
        renderer_.selectedThing = selectedThing_;
        pushLog("duplicated " + doc_.summary(n).definition, 0);
    } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); }
}

void App::deleteSelected() {
    if (!documentLoaded() || selectedThing_ < 0) return;
    const std::string what = doc_.summary(size_t(selectedThing_)).definition;
    try { doc_.remove(size_t(selectedThing_)); } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); return; }
    selectedThing_ = -1; selectedUid_ = 0; renderer_.selectedThing = -1;
    pushLog("removed " + what, 0);
}

void App::editUndo() { if (documentLoaded()) doc_.undo(); }
void App::editRedo() { if (documentLoaded()) doc_.redo(); }

void App::frameSelected() {
    if (selectedThing_ < 0) { frameMap(); return; }
    float c[3] = {0, 0, 0}, r = 0; int n = 0;
    for (size_t i = 0; i < renderer_.instanceCount(); ++i) {
        if (renderer_.instance(i).thing != selectedThing_) continue;
        float ic[3], ir;
        if (!renderer_.instanceBounds(i, ic, ir)) continue;
        c[0] += ic[0]; c[1] += ic[1]; c[2] += ic[2]; r = std::max(r, ir); ++n;
    }
    if (!n) { frameMap(); return; }
    c[0] /= n; c[1] /= n; c[2] /= n;
    camera_.lookAt(c[0], c[1], c[2], camera_.yaw, std::max(camera_.pitch, 0.35f), std::max(r * 4.0f, 8.0f));
}

bool App::placeDefinition(const std::string& def, const std::string& scriptName) {
    if (!documentLoaded()) { pushLog("editor: no level document", 1); return false; }
    uint32_t modelId = 0;
    const int code = ctx_.graphicModelId(def, modelId);
    if (code == 0) { pushLog("editor: " + def + " is not in game.bin", 2); return false; }
    if (code < 0 || modelId == 0) pushLog("editor: " + def + " has no mesh; it will be placed but not drawn", 1);
    float focus[3]; camera_.focus(focus);
    forge::thingplacer::Placement p;
    p.definitionType = def;
    p.thingType = def.rfind("BUILDING_", 0) == 0 ? "Building" : "Object";
    p.position = {focus[0], -focus[2], focus[1]};
    if (const auto h = doc_.groundHeight(p.position.x, p.position.y)) p.position.z = *h;
    // face the camera
    float d[3]; camera_.dir(d);
    const float fx = -d[0], fy = d[2];
    const float fl = std::sqrt(fx * fx + fy * fy);
    if (fl > 1e-6f) p.forward = {fx / fl, fy / fl, 0.0f};
    try {
        const bool creature = def.rfind("CREATURE_", 0) == 0;
        const float pos[3] = {p.position.x, p.position.y, p.position.z};
        const float fwd[2] = {p.forward.x, p.forward.y};
        const size_t n = creature ? doc_.placeCreature(pos, fwd, def, scriptName) : doc_.place(p);
        selectedUid_ = doc_.uidOf(n);
        selectedThing_ = int(n);
        renderer_.selectedThing = selectedThing_;
        pushLog("placed " + def, 0);
        return true;
    } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); return false; }
}

bool App::saveDocument() {
    if (!documentLoaded()) return false;
    std::string err;
    if (!doc_.saveLoose(saveRoot(), err)) { pushLog("save failed: " + err, 2); return false; }
    pushLog("saved " + doc_.loosePath().string(), 3);
    return true;
}

bool App::deployDocument() {
    if (!documentLoaded()) return false;
    std::string err;
    if (!doc_.deployWad(saveRoot(), err)) { pushLog("deploy failed: " + err, 2); return false; }
    pushLog("wrote " + doc_.mapName() + ".tng into FinalAlbion.wad (backup FinalAlbion.wad.atlas-orig)", 3);
    return true;
}

void App::revertDocument() {
    if (!documentLoaded()) return;
    while (doc_.undo()) {}
}

// ------------------------------------------------------------ terrain tool

void App::startThemeRebake() {
    if (!documentLoaded() || !doc_.hasTerrain() || !ctx_.ready()) return;
    if (previewFuture_.valid()) { rebakePending_ = true; return; }
    rebakePending_ = false;
    const MapEntry* found = findEntry(selectedName_);
    if (!found) return;
    const MapEntry entry = *found;
    const te::Context* ctx = &ctx_;
    const int texels = previewTexels_;
    const float gain = settings_.gain;
    auto level = std::make_shared<forge::lev::File>(*doc_.level());   // snapshot: strokes may continue meanwhile
    previewFuture_ = std::async(std::launch::async, [entry, ctx, texels, gain, level]() {
        PreviewResult r; r.name = entry.key; r.textured = true;
        try {
            te::Options o;
            o.textures = true; o.texelsPerCell = texels; o.gain = gain; o.up = te::UpAxis::Y;
            o.gameRoot = ctx->gameRoot();
            o.engineLayers = false;   // painted themes only exist in the LEV; bake from it
            r.scene = te::buildScene(*level, o, ctx);
        } catch (const std::exception& e) { r.error = e.what(); }
        return r;
    });
}

void App::syncTerrain() {
    if (!documentLoaded() || !doc_.hasTerrain()) return;
    if (doc_.themeRevision() != syncedThemeRev_) { syncedThemeRev_ = doc_.themeRevision(); if (syncedThemeRev_ > 1 || doc_.themesDirty()) startThemeRebake(); }
    if (rebakePending_ && !previewFuture_.valid()) startThemeRebake();
    if (doc_.terrainRevision() == syncedTerrainRev_) return;
    const auto& t = doc_.liveTerrain();
    if (renderer_.updateTerrain(t.heights.data(), t.walkable.data(), doc_.cellsX(), doc_.cellsY()))
        syncedTerrainRev_ = doc_.terrainRevision();
}

void App::terrainInput(const ImVec2& origin, const ImVec2& size) {
    brushHit_ = false;
    if (!editMode_ || gizmoOp_ != 4 || !documentLoaded() || !doc_.hasTerrain()) { if (doc_.strokeActive()) doc_.endStroke(); return; }
    ImGuiIO& io = ImGui::GetIO();
    if (size.x <= 0 || size.y <= 0) return;
    const float u = (io.MousePos.x - origin.x) / size.x, v = (io.MousePos.y - origin.y) / size.y;
    if (viewportHovered_ || doc_.strokeActive()) {
        float o[3], d[3], hit[3];
        renderer_.screenRay(u, v, o, d);
        if (renderer_.rayTerrain(o, d, hit)) {
            brushHit_ = true;
            brushFable_[0] = hit[0]; brushFable_[1] = -hit[2];
        }
    }
    editor::TerrainBrush b;
    using M = editor::TerrainBrush::Mode;
    int mode = terrainMode_;
    if (io.KeyShift && (mode == 0 || mode == 1)) mode = 1 - mode;
    if (io.KeyShift && (mode == 4 || mode == 5)) mode = 9 - mode;
    b.mode = mode == 0 ? M::Raise : mode == 1 ? M::Lower : mode == 2 ? M::Flatten : mode == 3 ? M::Smooth : mode == 4 ? M::Walkable : mode == 5 ? M::Blocked : M::Theme;
    b.x = brushFable_[0]; b.y = brushFable_[1];
    b.radius = brushRadius_; b.strength = brushStrength_;
    b.themeIndex = uint8_t(paintTheme_);
    const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (!doc_.strokeActive() && lmb && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && viewportHovered_ && brushHit_ && !io.KeyAlt) {
        doc_.beginStroke(b);
        clickArmed_ = false;
    }
    if (doc_.strokeActive()) {
        if (lmb) { if (brushHit_) doc_.applyBrush(b, std::min(io.DeltaTime, 0.1f)); }
        else doc_.endStroke();
    }
}

void App::terrainStroke(float x, float y, float seconds) {
    if (!documentLoaded() || !doc_.hasTerrain()) return;
    editor::TerrainBrush b;
    using M = editor::TerrainBrush::Mode;
    b.mode = terrainMode_ == 0 ? M::Raise : terrainMode_ == 1 ? M::Lower : terrainMode_ == 2 ? M::Flatten : terrainMode_ == 3 ? M::Smooth : terrainMode_ == 4 ? M::Walkable : terrainMode_ == 5 ? M::Blocked : M::Theme;
    b.x = x; b.y = y; b.radius = brushRadius_; b.strength = brushStrength_;
    b.themeIndex = uint8_t(paintTheme_);
    doc_.beginStroke(b);
    doc_.applyBrush(b, seconds);
    doc_.endStroke();
}

void App::drawBrushCursor(const ImVec2& origin, const ImVec2& size) {
    if (!brushHit_ || !editMode_ || gizmoOp_ != 4) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int n = 48;
    ImVec2 pts[n];
    int got = 0;
    for (int i = 0; i < n; ++i) {
        const float a = float(i) / n * 6.2831853f;
        const float fx = brushFable_[0] + brushRadius_ * std::cos(a), fy = brushFable_[1] + brushRadius_ * std::sin(a);
        const auto h = doc_.terrainHeight(fx, fy);
        const float p[3] = {fx, h.value_or(0.0f) + 0.05f, -fy};
        float u, v;
        if (!renderer_.project(p, u, v)) return;
        pts[got++] = ImVec2(origin.x + u * size.x, origin.y + v * size.y);
    }
    const ImU32 col = terrainMode_ == 6 ? IM_COL32(240, 200, 80, 230) : terrainMode_ >= 4 ? (terrainMode_ == 4 ? IM_COL32(80, 220, 140, 230) : IM_COL32(230, 80, 90, 230)) : theme::col(theme::Accent);
    dl->AddPolyline(pts, got, col, ImDrawFlags_Closed, theme::S(2.0f));
    // centre dot
    const auto hc = doc_.terrainHeight(brushFable_[0], brushFable_[1]);
    const float c[3] = {brushFable_[0], hc.value_or(0.0f) + 0.05f, -brushFable_[1]};
    float cu, cv;
    if (renderer_.project(c, cu, cv)) dl->AddCircleFilled(ImVec2(origin.x + cu * size.x, origin.y + cv * size.y), theme::S(3.0f), col);
}

// ---- new level from the selected map

bool App::placeSpawner(const std::vector<std::string>& families, float radius, int limit, const std::string& scriptName) {
    if (!documentLoaded()) { pushLog("editor: no level document", 1); return false; }
    if (families.empty()) { pushLog("editor: pick at least one creature family", 1); return false; }
    float focus[3]; camera_.focus(focus);
    float pos[3] = {focus[0], -focus[2], focus[1]};
    if (const auto h = doc_.groundHeight(pos[0], pos[1])) pos[2] = *h;
    try {
        const size_t n = doc_.placeCreatureGenerator(pos, families, radius, limit, scriptName);
        selectedUid_ = doc_.uidOf(n);
        selectedThing_ = int(n);
        renderer_.selectedThing = selectedThing_;
        std::string list;
        for (const auto& f : families) list += (list.empty() ? "" : ", ") + f;
        pushLog("placed an enemy spawner (" + list + ", radius " + std::to_string(int(radius)) + ", limit " + std::to_string(limit) + ")", 0);
        return true;
    } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); return false; }
}

void App::drawSpawnerCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!documentLoaded()) return;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##spawner", inner);
    theme::label("Enemy spawner");
    ImGui::PushFont(fontSmall_);
    theme::hint("A MARKER_CREATURE_GENERATOR that spawns creatures from the chosen families when the hero comes within the radius (the retail self-triggering generator). Families are the game's CREATURE_GENERATION_FAMILY defs.");
    ImGui::PopFont();
    if (familyList_.empty()) { std::string err; familyList_ = editor::creatureFamilies(saveRoot(), err); if (familyList_.empty()) familyList_ = editor::creatureFamilies(installPath_, err); }
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##familysearch", "Search families (HOBBES, BANDITS, BALVERINES...)", familySearch_, sizeof familySearch_);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_familysearch");
    if (familySearch_[0]) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
        ImGui::BeginChild("##familylist", ImVec2(cardInner, S(110)), ImGuiChildFlags_None);
        ImGui::PopStyleColor();
        ImGui::PushFont(fontSmall_);
        int shown = 0;
        for (const auto& name : familyList_) {
            if (!contains(name, familySearch_)) continue;
            const bool on = std::find(spawnerFamilies_.begin(), spawnerFamilies_.end(), name) != spawnerFamilies_.end();
            if (ImGui::Selectable(name.c_str(), on)) {
                if (on) spawnerFamilies_.erase(std::remove(spawnerFamilies_.begin(), spawnerFamilies_.end(), name), spawnerFamilies_.end());
                else spawnerFamilies_.push_back(name);
            }
            if (++shown >= 200) break;
        }
        if (!shown) ImGui::TextColored(theme::vec(theme::Faint), "no match");
        ImGui::PopFont();
        ImGui::EndChild();
    }
    if (!spawnerFamilies_.empty()) {
        ImGui::PushFont(fontSmall_);
        std::string list;
        for (const auto& f : spawnerFamilies_) list += (list.empty() ? "" : ", ") + f;
        ImGui::TextWrapped("%s", list.c_str());
        ImGui::PopFont();
        if (theme::ghostButton("Clear families", ImVec2(cardInner, S(24)))) spawnerFamilies_.clear();
    }
    char val[64];
    std::snprintf(val, sizeof val, "%.0f units", spawnerRadius_);
    theme::labelValue("Trigger radius", val, cardInner);
    ImGui::SetNextItemWidth(cardInner);
    ImGui::SliderFloat("##spawnradius", &spawnerRadius_, 4.0f, 40.0f, "");
    std::snprintf(val, sizeof val, spawnerLimit_ < 0 ? "unlimited" : "%d at once", spawnerLimit_);
    theme::labelValue("Creatures", val, cardInner);
    ImGui::SetNextItemWidth(cardInner);
    ImGui::SliderInt("##spawnlimit", &spawnerLimit_, -1, 12, "");
    if (theme::ghostButton("Place spawner at view centre", ImVec2(cardInner, S(30))) && !spawnerFamilies_.empty()) placeSpawner(spawnerFamilies_, spawnerRadius_, spawnerLimit_);
    auto_.registerWidget("btn_place_spawner");
    theme::endCard();
}

void App::drawNewLevelCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!documentLoaded()) return;
    const std::string donor = doc_.mapName();
    if (newLevelDonor_ != donor) {
        // refill the fields for this donor: a free origin and the owning region
        newLevelDonor_ = donor;
        std::string err;
        newLevelInfoOk_ = editor::donorInfo(saveRoot(), donor, newLevelInfo_, err);
        if (newLevelInfoOk_) {
            newLevelX_ = newLevelInfo_.suggestedX; newLevelY_ = newLevelInfo_.suggestedY;
            newLevelRegion_ = newLevelInfo_.owningRegion;
            if (newLevelName_[0] == 0) std::snprintf(newLevelName_, sizeof newLevelName_, "%s_Copy", donor.c_str());
            // blank levels come in the retail sizes; the palette/header come from a retail map of
            // that size -- this map when the size matches, else the first one of that size
            std::string serr;
            blankSizes_ = editor::retailMapSizes(saveRoot(), serr);
            selectBlankSize(newLevelInfo_.width, newLevelInfo_.height);
            reusableRegions_ = editor::reusableRegions(saveRoot(), serr);
        } else {
            pushLog("new level: " + err, 1);
        }
    }
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##newlevel", inner);
    theme::label("New level");
    theme::segmented("##newlevelmode", newLevelMode_, {"Copy of this map", "Blank"}, cardInner);
    auto_.registerWidget("seg_new_level_mode");
    ImGui::PushFont(fontSmall_);
    if (newLevelMode_ == 0) theme::hint("Clones the map (current .lev/.tng, terrain chunk translated to the new origin: ground, LOD, water, trees and grass) into the world as a new level owned by an existing region. One-time .atlas-orig backups of the .bwd/.wld/.wad/.stb.");
    else theme::hint(("A flat level authored from scratch (terrain chunk built by forgecore, renders in-game): one ground theme from " + blankTemplate_ + "'s palette, every cell walkable, empty .tng. Sculpt, paint and place on it afterwards.").c_str());
    ImGui::PopFont();
    if (newLevelMode_ == 1) {
        ImGui::SetNextItemWidth(cardInner);
        char szLabel[64] = "(size)";
        if (blankSize_ >= 0 && blankSize_ < int(blankSizes_.size())) std::snprintf(szLabel, sizeof szLabel, "%d x %d cells", blankSizes_[size_t(blankSize_)].width, blankSizes_[size_t(blankSize_)].height);
        if (ImGui::BeginCombo("##blanksize", szLabel)) {
            for (size_t i = 0; i < blankSizes_.size(); ++i) {
                char lbl[96]; std::snprintf(lbl, sizeof lbl, "%d x %d   (%d retail map%s)", blankSizes_[i].width, blankSizes_[i].height, blankSizes_[i].count, blankSizes_[i].count == 1 ? "" : "s");
                if (ImGui::Selectable(lbl, int(i) == blankSize_)) selectBlankSize(blankSizes_[i].width, blankSizes_[i].height);
            }
            ImGui::EndCombo();
        }
        auto_.registerWidget("combo_blank_size");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Any size a retail map has (the LEV header and palette are taken from one of them).");
        ImGui::SetNextItemWidth(cardInner);
        const char* cur = (blankTheme_ >= 0 && blankTheme_ < int(blankPalette_.size())) ? blankPalette_[size_t(blankTheme_)].c_str() : "(ground theme)";
        if (ImGui::BeginCombo("##blanktheme", cur)) {
            for (size_t i = 0; i < blankPalette_.size(); ++i) {
                if (blankPalette_[i].empty()) continue;
                char lbl[160]; std::snprintf(lbl, sizeof lbl, "%zu  %s", i, blankPalette_[i].c_str());
                if (ImGui::Selectable(lbl, int(i) == blankTheme_)) blankTheme_ = int(i);
            }
            ImGui::EndCombo();
        }
        auto_.registerWidget("combo_blank_theme");
        ImGui::SetNextItemWidth(cardInner);
        ImGui::InputFloat("##blankheight", &blankHeight_, 1.0f, 10.0f, "ground height %.1f");
    }
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##newlevelname", "Level name (letters, digits, _)", newLevelName_, sizeof newLevelName_);
    auto_.registerWidget("input_new_level_name");
    const float half = (cardInner - S(6)) * 0.5f;
    ImGui::SetNextItemWidth(half);
    ImGui::InputInt("##newlevelx", &newLevelX_, 32, 128);
    ImGui::SameLine(0, S(6));
    ImGui::SetNextItemWidth(half);
    ImGui::InputInt("##newlevely", &newLevelY_, 32, 128);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("World origin (32-unit grid). The suggestion is the first free spot right of the existing maps.");
    ImGui::SetNextItemWidth(cardInner);
    if (ImGui::BeginCombo("##newlevelregion", newLevelRegion_.empty() ? "(owning region)" : newLevelRegion_.c_str())) {
        for (const auto& r : newLevelInfo_.regions)
            if (ImGui::Selectable(r.c_str(), r == newLevelRegion_)) newLevelRegion_ = r;
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_new_level_region");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The region that owns the new map (the game only reaches maps owned by one of the first 141 regions, so a copy joins an existing region).");
    theme::toggle("Own region + minimap", &newLevelOwnRegion_);
    auto_.registerWidget("toggle_new_level_own_region");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game keeps only 141 regions, so a new region takes over a retail filler slot\n(a region owning only decorative maps; they move to another filler). The level gets its own\nname on the map screen and a minimap baked from its terrain (replaces an unreferenced\nMINIMAP_* texture in textures.big; one-time .atlas-orig backup).");
    if (newLevelOwnRegion_) {
        ImGui::PushFont(fontSmall_);
        if (reusableRegions_.size() >= 2)
            theme::hint(("Takes over " + reusableRegions_.front().name + " (slot " + std::to_string(reusableRegions_.front().slot) + ", " + std::to_string(reusableRegions_.front().maps) + " map(s) -> " + reusableRegions_.back().name + ").").c_str());
        else
            ImGui::TextColored(theme::vec(theme::Warn), "No filler region slot is free to take over.");
        ImGui::PopFont();
        ImGui::SetNextItemWidth(cardInner);
        ImGui::InputTextWithHint("##newleveldisplay", "Display name on the map screen (default: level name)", newLevelDisplay_, sizeof newLevelDisplay_);
    }
    const bool gridOk = newLevelX_ % 32 == 0 && newLevelY_ % 32 == 0;
    const bool can = newLevelInfoOk_ && newLevelName_[0] != 0 && gridOk && (!newLevelRegion_.empty() || newLevelOwnRegion_) && !newLevelFuture_.valid() &&
                     (!newLevelOwnRegion_ || reusableRegions_.size() >= 2) &&
                     (newLevelMode_ == 0 || (blankTheme_ >= 0 && ctx_.themeLibrary()));
    if (newLevelMode_ == 1 && !ctx_.themeLibrary()) { ImGui::PushFont(fontSmall_); ImGui::TextColored(theme::vec(theme::Warn), "Waiting for the ENGINE_THEME library (textures loading)."); ImGui::PopFont(); }
    if (!gridOk) { ImGui::PushFont(fontSmall_); ImGui::TextColored(theme::vec(theme::Warn), "Origin must be a multiple of 32."); ImGui::PopFont(); }
    if (theme::primaryButton(newLevelFuture_.valid() ? "Installing..." : "Create level in the game", ImVec2(cardInner, S(32)), can)) startNewLevel();
    auto_.registerWidget("btn_new_level");
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));
}

void App::selectBlankSize(int w, int h) {
    blankSize_ = -1;
    for (size_t i = 0; i < blankSizes_.size(); ++i) if (blankSizes_[i].width == w && blankSizes_[i].height == h) blankSize_ = int(i);
    if (blankSize_ < 0) for (size_t i = 0; i < blankSizes_.size(); ++i) if (blankSizes_[i].width == 64 && blankSizes_[i].height == 64) blankSize_ = int(i);
    if (blankSize_ < 0 && !blankSizes_.empty()) blankSize_ = 0;
    if (blankSize_ < 0) return;
    const auto& sz = blankSizes_[size_t(blankSize_)];
    const std::string donor = documentLoaded() ? doc_.mapName() : std::string();
    blankTemplate_ = (newLevelInfo_.width == sz.width && newLevelInfo_.height == sz.height && !donor.empty()) ? donor : sz.templateLevel;
    std::string perr;
    if (!editor::templatePalette(saveRoot(), blankTemplate_, blankPalette_, perr)) { blankPalette_.clear(); pushLog("new level: " + perr, 1); }
    if (blankTheme_ < 0 || blankTheme_ >= int(blankPalette_.size()) || blankPalette_[size_t(blankTheme_)].empty()) {
        blankTheme_ = -1;
        for (size_t i = 0; i < blankPalette_.size(); ++i) if (blankPalette_[i].rfind("GROUND_", 0) == 0) { blankTheme_ = int(i); break; }
    }
}

void App::startNewLevel() {
    if (!documentLoaded() || newLevelFuture_.valid()) return;
    const std::string root = saveRoot();
    if (newLevelMode_ == 1) {
        editor::BlankLevelRequest req;
        req.name = newLevelName_;
        req.hostRegion = newLevelRegion_;
        req.worldX = newLevelX_; req.worldY = newLevelY_;
        req.templateLevel = blankTemplate_;
        req.ownRegion.wanted = newLevelOwnRegion_; req.ownRegion.displayName = newLevelDisplay_;
        if (blankSize_ >= 0 && blankSize_ < int(blankSizes_.size())) { req.width = blankSizes_[size_t(blankSize_)].width; req.height = blankSizes_[size_t(blankSize_)].height; }
        req.themeSlot = blankTheme_;
        req.groundHeight = blankHeight_;
        const forge::terraintex::ThemeLibrary* lib = ctx_.themeLibrary();
        if (!lib) return;
        pushLog("new level: authoring blank " + req.name + " at (" + std::to_string(req.worldX) + "," + std::to_string(req.worldY) + "), region " + req.hostRegion + "...", 0);
        newLevelFuture_ = std::async(std::launch::async, [req, root, lib]() {
            NewLevelJob j; j.name = req.name;
            j.ok = editor::createBlankLevel(root, req, *lib, j.result, j.error);
            return j;
        });
        return;
    }
    editor::NewLevelRequest req;
    req.donor = doc_.mapName();
    req.name = newLevelName_;
    req.hostRegion = newLevelRegion_;
    req.ownRegion.wanted = newLevelOwnRegion_; req.ownRegion.displayName = newLevelDisplay_;
    req.worldX = newLevelX_; req.worldY = newLevelY_;
    pushLog("new level: cloning " + req.donor + " as " + req.name + " at (" + std::to_string(req.worldX) + "," + std::to_string(req.worldY) + "), region " + req.hostRegion + "...", 0);
    newLevelFuture_ = std::async(std::launch::async, [req, root]() {
        NewLevelJob j; j.name = req.name;
        j.ok = editor::createLevelFromDonor(root, req, j.result, j.error);
        return j;
    });
}

void App::startTerrainDeploy() {
    if (!documentLoaded() || !doc_.hasTerrain() || terrainDeployFuture_.valid()) return;
    if (ctxFuture_.valid()) { pushLog("terrain: textures and themes are still loading (a custom theme was just added); deploy again in a moment", 1); return; }
    if (doc_.strokeActive()) doc_.endStroke();
    editor::Document* doc = &doc_;
    const std::string root = saveRoot();
    const forge::terraintex::ThemeLibrary* lib = ctx_.themeLibrary();
    pushLog("terrain: writing .lev, FinalAlbion.wad and re-baking the FinalAlbion_RT.stb chunk...", 0);
    terrainDeployFuture_ = std::async(std::launch::async, [doc, root, lib]() {
        TerrainDeployResult r;
        r.ok = doc->deployTerrain(root, r.notes, r.error, lib);
        return r;
    });
}

// ------------------------------------------------------------ viewport

void App::editorShortcuts() {
    if (!editMode_ || !documentLoaded()) return;
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsAnyItemActive() || io.WantTextInput) return;
    const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (!rmb) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) gizmoOp_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_W)) gizmoOp_ = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) gizmoOp_ = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmoOp_ = 3;
        if (ImGui::IsKeyPressed(ImGuiKey_T) && doc_.hasTerrain()) gizmoOp_ = 4;
        if (gizmoOp_ == 4) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) brushRadius_ = std::max(1.0f, brushRadius_ - 1.0f);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) brushRadius_ = std::min(60.0f, brushRadius_ + 1.0f);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedThing_ >= 0) deleteSelected();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) editUndo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) editRedo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selectedThing_ >= 0) duplicateSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && selectedThing_ >= 0) selectThing(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_End) && selectedThing_ >= 0) snapSelectedToGround();
}

void App::drawGizmo(const ImVec2& origin, const ImVec2& size) {
    if (!editMode_ || !documentLoaded() || selectedThing_ < 0 || gizmoOp_ == 0) { gizmoWasUsing_ = false; return; }
    editor::Frame f;
    if (!frameOfSelected(f)) return;
    if (!gizmoWasUsing_) gizmoFrame_ = f;
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    ImGuizmo::AllowAxisFlip(false);
    // unit-scale frame so the handles sit on the object's own axes
    editor::Frame unit = gizmoFrame_; unit.scale = 100.0f;
    float m[16], g[16];
    editor::frameToMatrix(unit, m);
    editor::multiply(m, kToRender, g);
    const ImGuizmo::OPERATION op = gizmoOp_ == 1 ? ImGuizmo::TRANSLATE : gizmoOp_ == 2 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
    const ImGuizmo::MODE mode = gizmoOp_ == 1 ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    const float snapT[3] = {0.5f, 0.5f, 0.5f}, snapR[3] = {15.0f, 0, 0}, snapS[3] = {0.1f, 0.1f, 0.1f};
    const float* snap = !gizmoSnap_ ? nullptr : gizmoOp_ == 1 ? snapT : gizmoOp_ == 2 ? snapR : snapS;
    ImGuizmo::Manipulate(renderer_.viewMatrix(), renderer_.projMatrix(), op, mode, g, nullptr, snap);
    const bool using_ = ImGuizmo::IsUsing();
    if (using_) {
        float back[16]; editor::multiply(g, kToFable, back);
        editor::Frame nf;
        if (editor::matrixToFrame(back, nf)) {
            const float rel = nf.scale / 100.0f;
            nf.scale = std::clamp(gizmoFrame_.scale * rel, 0.01f, 100.0f);
            if (gizmoOp_ != 3) nf.scale = gizmoFrame_.scale;
            if (gizmoOp_ == 3) { nf.pos[0] = gizmoFrame_.pos[0]; nf.pos[1] = gizmoFrame_.pos[1]; nf.pos[2] = gizmoFrame_.pos[2]; }
            gizmoFrame_ = nf;
            applyFrame(selectedThing_, gizmoFrame_);
        }
    } else if (gizmoWasUsing_) {
        commitFrame(gizmoFrame_);   // one undo step per drag
    }
    gizmoWasUsing_ = using_;
}

// ------------------------------------------------------------ unsaved-changes prompt

void App::drawUnsavedPrompt() {
    if (pendingSelect_.empty()) return;
    using theme::S;
    ImGui::OpenPopup("Unsaved changes");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(420), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(16)));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        const MapEntry* cur = findEntry(selectedName_);
        ImGui::PushFont(fontBold_);
        ImGui::TextUnformatted("Unsaved changes");
        ImGui::PopFont();
        ImGui::PushTextWrapPos(S(390));
        ImGui::TextColored(theme::vec(theme::Muted), "%s has edits that have not been written (%s). Switching maps drops them.",
                           cur ? cur->name.c_str() : selectedName_.c_str(),
                           doc_.dirty() && doc_.hasTerrain() && doc_.terrainDirty() ? "objects and terrain" : doc_.dirty() ? "objects" : "terrain");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(10)));
        const float w = (S(390) - 2 * S(6)) / 3.0f;
        if (theme::primaryButton("Save .tng", ImVec2(w, S(32)), doc_.dirty())) { saveDocument(); if (!hasUnsavedEdits()) { const std::string t = pendingSelect_; pendingSelect_.clear(); discardEdits_ = true; selectMap(t); } }
        auto_.registerWidget("btn_unsaved_save");
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Discard", ImVec2(w, S(32)))) { const std::string t = pendingSelect_; pendingSelect_.clear(); discardEdits_ = true; selectMap(t); }
        auto_.registerWidget("btn_unsaved_discard");
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Cancel", ImVec2(w, S(32)))) pendingSelect_.clear();
        auto_.registerWidget("btn_unsaved_cancel");
        if (doc_.hasTerrain() && doc_.terrainDirty()) {
            ImGui::PushFont(fontSmall_);
            theme::hint("Terrain edits are written with 'Save terrain into the game' in the Edit panel.");
            ImGui::PopFont();
        }
        if (pendingSelect_.empty()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

// ------------------------------------------------------------ panel

void App::drawEditPanel(float pad, float inner, float cardInner) {
    using theme::S;
    ImGui::SetCursorPosX(pad);
    if (!installValid_) { theme::hint("Editing needs a Fable install."); return; }
    if (selectedName_.empty()) { theme::hint("Pick a map on the left to edit its placed objects."); return; }
    if (!documentLoaded()) { theme::hint("No .tng for this map."); return; }
    const bool thingsReady = foliageLoaded() && !foliageFuture_.valid();

    // ---- tool
    theme::beginCard("##tool", inner);
    theme::label("Tool");
    if (doc_.hasTerrain()) theme::segmented("##gizmo", gizmoOp_, {"Select  Q", "Move  W", "Rotate  E", "Scale  R", "Terrain  T"}, cardInner);
    else theme::segmented("##gizmo", gizmoOp_, {"Select  Q", "Move  W", "Rotate  E", "Scale  R"}, cardInner);
    auto_.registerWidget("seg_gizmo");
    theme::toggle("Snap (0.5 units / 15 deg / 0.1x)", &gizmoSnap_);
    auto_.registerWidget("toggle_snap");
    ImGui::PushFont(fontSmall_);
    theme::hint("Click an object to select it. Drag the gizmo, or type values below. Del removes, Ctrl+D duplicates, Ctrl+Z/Y undo/redo, F frames, End drops to the ground.");
    ImGui::PopFont();
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

    // ---- terrain brush
    if (gizmoOp_ == 4 && doc_.hasTerrain()) {
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##terrain", inner);
        theme::label("Terrain brush");
        int shape = terrainMode_ < 4 ? terrainMode_ : -1;
        if (theme::segmented("##tmode", shape, {"Raise", "Lower", "Flatten", "Smooth"}, cardInner) && shape >= 0) terrainMode_ = shape;
        auto_.registerWidget("seg_terrain_mode");
        int walk = (terrainMode_ >= 4 && terrainMode_ <= 6) ? terrainMode_ - 4 : -1;
        if (theme::segmented("##twalk", walk, {"Paint walkable", "Paint blocked", "Paint ground"}, cardInner) && walk >= 0) terrainMode_ = 4 + walk;
        auto_.registerWidget("seg_terrain_walk");
        if (terrainMode_ == 6) {
            // ground theme picker: the map's LEV palette (slot -> ENGINE_THEME name), named slots
            const forge::lev::File* lev = doc_.level();
            const char* current = "(pick a ground theme)";
            if (lev && paintTheme_ >= 0 && size_t(paintTheme_) < lev->groundThemes().size() && !lev->groundThemes()[size_t(paintTheme_)].name.empty())
                current = lev->groundThemes()[size_t(paintTheme_)].name.c_str();
            ImGui::SetNextItemWidth(cardInner);
            if (ImGui::BeginCombo("##paintTheme", current)) {
                if (lev)
                    for (size_t i = 0; i < lev->groundThemes().size(); ++i) {
                        const auto& g = lev->groundThemes()[i];
                        if (g.name.empty()) continue;
                        char lbl[160]; std::snprintf(lbl, sizeof lbl, "%zu  %s", i, g.name.c_str());
                        if (ImGui::Selectable(lbl, int(i) == paintTheme_)) paintTheme_ = int(i);
                    }
                ImGui::EndCombo();
            }
            auto_.registerWidget("combo_paint_theme");
            // any ENGINE_THEME of the game can join the palette (a free slot of the 256)
            ImGui::SetNextItemWidth(cardInner);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
            ImGui::InputTextWithHint("##themesearch", "Add a ground theme from the game (GRASS, COBBLES, SNOW...)", themeSearch_, sizeof themeSearch_);
            ImGui::PopStyleVar();
            auto_.registerWidget("input_themesearch");
            if (themeSearch_[0]) {
                const forge::terraintex::ThemeLibrary* lib = ctx_.themeLibrary();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
                ImGui::BeginChild("##themelist", ImVec2(cardInner, S(110)), ImGuiChildFlags_None);
                ImGui::PopStyleColor();
                ImGui::PushFont(fontSmall_);
                int shown = 0;
                if (!lib) ImGui::TextColored(theme::vec(theme::Faint), "ENGINE_THEME library not loaded yet");
                else
                    for (const auto& th : lib->themes()) {
                        if (!th.decoded || !contains(th.name, themeSearch_)) continue;
                        const int have = doc_.paletteSlotOf(th.name);
                        char lbl[200]; std::snprintf(lbl, sizeof lbl, have >= 0 ? "%s  (slot %d)" : "%s", th.name.c_str(), have);
                        if (ImGui::Selectable(lbl, have >= 0 && have == paintTheme_)) addPaintTheme(th.name);
                        if (++shown >= 200) break;
                    }
                if (lib && !shown) ImGui::TextColored(theme::vec(theme::Faint), "no match");
                ImGui::PopFont();
                ImGui::EndChild();
            }
            ImGui::PushFont(fontSmall_);
            theme::hint("Paints the theme into the LEV's three blend slots; the preview re-bakes from the LEV after each stroke. Saving rebuilds the map's layer meshes so the game draws the new material. A theme added from the game takes a free palette slot and is written with the next terrain save.");
            ImGui::PopFont();
            // your own texture as a ground theme
            if (theme::ghostButton(customThemeOpen_ ? "Hide custom texture" : "Custom texture from a PNG...", ImVec2(cardInner, S(26)))) customThemeOpen_ = !customThemeOpen_;
            auto_.registerWidget("btn_custom_theme_toggle");
            if (customThemeOpen_) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
                ImGui::SetNextItemWidth(cardInner);
                ImGui::InputTextWithHint("##custompng", "Path to a square power-of-two PNG (512x512 like retail)", customPng_, sizeof customPng_);
                auto_.registerWidget("input_custom_png");
                ImGui::SetNextItemWidth(cardInner);
                ImGui::InputTextWithHint("##customname", "Theme name, e.g. GROUND_MY_MOSS", customName_, sizeof customName_, ImGuiInputTextFlags_CharsUppercase);
                auto_.registerWidget("input_custom_name");
                ImGui::PopStyleVar();
                ImGui::PushFont(fontSmall_);
                std::string donor = "GROUND_GRASS";
                if (lev && paintTheme_ >= 0 && size_t(paintTheme_) < lev->groundThemes().size() && !lev->groundThemes()[size_t(paintTheme_)].name.empty()) donor = lev->groundThemes()[size_t(paintTheme_)].name;
                theme::hint(("The PNG is appended to textures.big and a new ENGINE_THEME (a copy of " + donor + ", the selected theme, with your texture) to game.bin; nothing retail is replaced. One-time backups.").c_str());
                ImGui::PopFont();
                const bool can = customPng_[0] && customName_[0] && !ctxFuture_.valid();
                if (theme::ghostButton(ctxFuture_.valid() ? "Textures reloading..." : "Create theme and select it", ImVec2(cardInner, S(28))) && can) {
                    std::string nm = customName_;
                    for (auto& c : nm) { c = char(std::toupper(static_cast<unsigned char>(c))); if (!std::isalnum(static_cast<unsigned char>(c))) c = '_'; }
                    createCustomTheme(customPng_, nm, donor);
                }
                auto_.registerWidget("btn_custom_theme_create");
            }
        }
        char val[48];
        std::snprintf(val, sizeof val, "%.0f cells", brushRadius_);
        theme::labelValue("Radius   ( [ ] )", val, cardInner);
        ImGui::SetNextItemWidth(cardInner);
        ImGui::SliderFloat("##radius", &brushRadius_, 1.0f, 60.0f, "");
        auto_.registerWidget("slider_radius");
        std::snprintf(val, sizeof val, "%.1f", brushStrength_);
        theme::labelValue("Strength", val, cardInner);
        ImGui::SetNextItemWidth(cardInner);
        ImGui::SliderFloat("##strength", &brushStrength_, 0.5f, 20.0f, "");
        auto_.registerWidget("slider_strength");
        ImGui::PushFont(fontSmall_);
        theme::hint("Hold LMB on the ground to paint. Shift inverts (lower / walkable). Switch the view to Walkable to see the paint. Each stroke is one undo step.");
        ImGui::PopFont();
        theme::endCard();
        ImGui::Dummy(ImVec2(0, S(8)));
    }

    // ---- selection
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##sel", inner);
    editor::Frame f;
    if (selectedThing_ < 0 || !frameOfSelected(f)) {
        theme::label("Selection");
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Faint), "%s", thingsReady ? "Nothing selected" : "Objects loading...");
        ImGui::PopFont();
    } else {
        const auto s = doc_.summary(size_t(selectedThing_));
        ImGui::PushFont(fontBold_);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
        ImGui::TextUnformatted(s.definition.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Muted), "%s%s%s   uid %llu", s.type.c_str(), s.scriptName.empty() ? "" : "   ", s.scriptName.c_str(), (unsigned long long)s.uid);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(4)));
        bool changed = false;
        const float third = (cardInner - 2 * S(6)) / 3.0f;
        theme::label("Position (map-local)");
        ImGui::PushItemWidth(third);
        ImGui::DragFloat("##px", &f.pos[0], 0.05f, -1e6f, 1e6f, "X %.3f"); changed |= ImGui::IsItemDeactivatedAfterEdit(); auto_.registerWidget("drag_px");
        ImGui::SameLine(0, S(6));
        ImGui::DragFloat("##py", &f.pos[1], 0.05f, -1e6f, 1e6f, "Y %.3f"); changed |= ImGui::IsItemDeactivatedAfterEdit(); auto_.registerWidget("drag_py");
        ImGui::SameLine(0, S(6));
        ImGui::DragFloat("##pz", &f.pos[2], 0.05f, -1e6f, 1e6f, "Z %.3f"); changed |= ImGui::IsItemDeactivatedAfterEdit(); auto_.registerWidget("drag_pz");
        ImGui::PopItemWidth();
        float yaw = yawDegrees(f);
        const float yaw0 = yaw;
        theme::label("Yaw / scale");
        ImGui::PushItemWidth((cardInner - S(6)) * 0.5f);
        ImGui::DragFloat("##yaw", &yaw, 0.5f, -360.0f, 360.0f, "%.1f deg");
        const bool yawDone = ImGui::IsItemDeactivatedAfterEdit();
        auto_.registerWidget("drag_yaw");
        ImGui::SameLine(0, S(6));
        ImGui::DragFloat("##scale", &f.scale, 0.01f, 0.01f, 100.0f, "x %.3f"); changed |= ImGui::IsItemDeactivatedAfterEdit(); auto_.registerWidget("drag_scale");
        ImGui::PopItemWidth();
        if (std::fabs(yaw - yaw0) > 1e-6f || yawDone) {
            // keep the tilt: rotate the current forward about up by the delta
            const float a = (yaw - yaw0) * 3.14159265f / 180.0f, c = std::cos(a), sn = std::sin(a);
            float u[3] = {f.up[0], f.up[1], f.up[2]};
            const float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
            if (ul > 1e-9f) for (float& x : u) x /= ul;
            const float* w = f.forward;
            const float dot = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
            const float cr[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
            float r[3]; for (int k = 0; k < 3; ++k) r[k] = w[k] * c + cr[k] * sn + u[k] * dot * (1 - c);
            f.forward[0] = r[0]; f.forward[1] = r[1]; f.forward[2] = r[2];
            changed |= yawDone;
        }
        // live preview while a field is being dragged, commit when released
        if (ImGui::IsAnyItemActive()) applyFrame(selectedThing_, f);
        if (changed) commitFrame(f);
        ImGui::Dummy(ImVec2(0, S(4)));
        const float half = (cardInner - S(6)) * 0.5f;
        if (theme::ghostButton("Drop to ground  (End)", ImVec2(half, S(28)))) snapSelectedToGround();
        auto_.registerWidget("btn_ground");
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Focus  (F)", ImVec2(half, S(28)))) frameSelected();
        auto_.registerWidget("btn_focus");
        if (theme::ghostButton("Duplicate  (Ctrl+D)", ImVec2(half, S(28)))) duplicateSelected();
        auto_.registerWidget("btn_duplicate");
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Delete  (Del)", ImVec2(half, S(28)))) deleteSelected();
        auto_.registerWidget("btn_delete");
    }
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

    // ---- objects in this map
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##objs", inner);
    theme::label("Objects in this map");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##thingsearch", "Filter by definition or script name", thingSearch_, sizeof thingSearch_);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_thingsearch");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
    ImGui::BeginChild("##thinglist", ImVec2(cardInner, S(150)), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    ImGui::PushFont(fontSmall_);
    std::vector<int> rows;
    rows.reserve(doc_.thingCount());
    for (size_t i = 0; i < doc_.thingCount(); ++i) {
        const auto s = doc_.summary(i);
        if (!s.hasFrame || s.type == "Marker" || s.type == "TrackNode") continue;
        if (thingSearch_[0] && !contains(s.definition, thingSearch_) && !contains(s.scriptName, thingSearch_)) continue;
        rows.push_back(int(i));
    }
    ImGuiListClipper clipper;
    clipper.Begin(int(rows.size()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const int i = rows[size_t(r)];
            const auto s = doc_.summary(size_t(i));
            std::string labelText = s.definition;
            if (!s.scriptName.empty()) labelText += "  (" + s.scriptName + ")";
            labelText += "##t" + std::to_string(i);
            if (ImGui::Selectable(labelText.c_str(), i == selectedThing_)) { selectThing(i); }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { selectThing(i); frameSelected(); }
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

    // ---- add
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##add", inner);
    theme::label("Add an object");
    if (defList_.empty() && ctx_.ready()) defList_ = ctx_.definitions({"OBJECT", "BUILDING", "CREATURE"});
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##defsearch", "Search definitions (OBJECT_..., BUILDING_..., CREATURE_...)", defSearch_, sizeof defSearch_);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_defsearch");
    static std::string placeDef;
    if (defSearch_[0]) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
        ImGui::BeginChild("##deflist", ImVec2(cardInner, S(120)), ImGuiChildFlags_None);
        ImGui::PopStyleColor();
        ImGui::PushFont(fontSmall_);
        int shown = 0;
        for (const auto& [name, type] : defList_) {
            if (!contains(name, defSearch_)) continue;
            if (ImGui::Selectable(name.c_str(), name == placeDef)) placeDef = name;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { placeDef = name; placeDefinition(name); }
            if (++shown >= 300) { ImGui::TextColored(theme::vec(theme::Faint), "...type more to narrow down"); break; }
        }
        if (!shown) ImGui::TextColored(theme::vec(theme::Faint), "no match");
        ImGui::PopFont();
        ImGui::EndChild();
    }
    const std::string placeLabel = placeDef.empty() ? "Place at view centre" : "Place " + placeDef;
    if (theme::ghostButton(placeLabel.c_str(), ImVec2(cardInner, S(30))) && !placeDef.empty()) placeDefinition(placeDef);
    auto_.registerWidget("btn_place");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Placed where the camera looks, dropped onto the terrain, facing the camera.");
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

    drawSpawnerCard(pad, inner, cardInner);
    ImGui::Dummy(ImVec2(0, S(8)));
    drawNewLevelCard(pad, inner, cardInner);

    // ---- changes / save
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##changes", inner);
    const auto changes = doc_.changes();
    char head[64];
    std::snprintf(head, sizeof head, "Changes  (%zu%s)", changes.size(), doc_.terrainDirty() ? " + terrain" : "");
    theme::label(head);
    const float half = (cardInner - S(6)) * 0.5f;
    if (theme::ghostButton(doc_.canUndo() ? "Undo  (Ctrl+Z)" : "Undo", ImVec2(half, S(28))) && doc_.canUndo()) editUndo();
    auto_.registerWidget("btn_undo");
    ImGui::SameLine(0, S(6));
    if (theme::ghostButton(doc_.canRedo() ? "Redo  (Ctrl+Y)" : "Redo", ImVec2(half, S(28))) && doc_.canRedo()) editRedo();
    auto_.registerWidget("btn_redo");
    if (!changes.empty()) {
        ImGui::PushFont(fontSmall_);
        int n = 0;
        for (const auto& c : changes) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
            ImGui::TextColored(theme::vec(theme::Muted), "%s", c.c_str());
            ImGui::PopTextWrapPos();
            if (++n >= 12) { ImGui::TextColored(theme::vec(theme::Faint), "...and %zu more", changes.size() - 12); break; }
        }
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(4)));
    }
    theme::endCard();
}

void App::drawEditFooter(float pad, float inner) {
    using theme::S;
    if (!documentLoaded()) {
        ImGui::SetCursorPosX(pad);
        theme::primaryButton("No level document", ImVec2(inner, S(42)), false);
        return;
    }
    const bool dirty = doc_.dirty();
    if (doc_.hasTerrain() && doc_.terrainDirty() && !terrainDeployFuture_.valid()) {
        // objects standing on sculpted ground follow it (their offset kept); one undo step
        ImGui::SetCursorPosX(pad);
        if (theme::ghostButton("Re-seat objects on the new ground", ImVec2(inner, S(28)))) reseatThings();
        auto_.registerWidget("btn_reseat_things");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every placed object that stood on the ground before this sculpt session\n(within 1 unit) moves with it, keeping its offset. Buried or floating objects stay.");
    }
    if (doc_.hasTerrain() && (doc_.terrainDirty() || terrainDeployFuture_.valid())) {
        ImGui::SetCursorPosX(pad);
        if (terrainDeployFuture_.valid()) {
            theme::primaryButton("Baking terrain...", ImVec2(inner, S(36)), false);
        } else if (!confirmTerrainDeploy_) {
            if (theme::primaryButton("Save terrain into the game", ImVec2(inner, S(36)))) confirmTerrainDeploy_ = true;
            auto_.registerWidget("btn_terrain_deploy");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes the loose .lev, replaces it in FinalAlbion.wad and re-bakes this map's\nterrain chunk inside FinalAlbion_RT.stb from the edited heights (same size, patched in place).\nOne-time .atlas-orig backups of all three files.");
        } else {
            ImGui::PushFont(fontSmall_);
            ImGui::TextColored(theme::vec(theme::Warn), "Rewrite %s's terrain in the .lev, .wad and .stb?", doc_.mapName().c_str());
            ImGui::PopFont();
            const float half2 = (inner - S(6)) * 0.5f;
            if (theme::primaryButton("Yes, bake it", ImVec2(half2, S(30)))) { confirmTerrainDeploy_ = false; startTerrainDeploy(); }
            auto_.registerWidget("btn_terrain_deploy_confirm");
            ImGui::SameLine(0, S(6));
            if (theme::ghostButton("Cancel", ImVec2(half2, S(30)))) confirmTerrainDeploy_ = false;
        }
    }
    ImGui::SetCursorPosX(pad);
    if (theme::primaryButton(dirty ? "Save .tng (loose file)" : "Saved", ImVec2(inner, S(42)), dirty)) saveDocument();
    auto_.registerWidget("btn_save");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes data/Levels/FinalAlbion/%s.tng. A one-time backup of any existing file is kept as .atlas-orig.", doc_.mapName().c_str());
    ImGui::SetCursorPosX(pad);
    const float half = (inner - S(6)) * 0.5f;
    if (!confirmDeploy_) {
        if (theme::ghostButton("Write into FinalAlbion.wad", ImVec2(dirty ? half : inner, S(32)))) confirmDeploy_ = true;
        auto_.registerWidget("btn_deploy");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game loads levels from the WAD, so this is what makes the edit show up in-game.\nThe original archive is backed up once as FinalAlbion.wad.atlas-orig.");
        if (dirty) {
            ImGui::SameLine(0, S(6));
            if (theme::ghostButton("Revert all", ImVec2(half, S(32)))) revertDocument();
            auto_.registerWidget("btn_revert");
        }
    } else {
        if (theme::primaryButton("Yes, write into the WAD", ImVec2(half, S(32)))) { confirmDeploy_ = false; deployDocument(); }
        auto_.registerWidget("btn_deploy_confirm");
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Cancel", ImVec2(half, S(32)))) confirmDeploy_ = false;
    }
}

} // namespace albion::gui
