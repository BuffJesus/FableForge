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

bool App::placeDefinition(const std::string& def) {
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
        const size_t n = doc_.place(p);
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
    theme::segmented("##gizmo", gizmoOp_, {"Select  Q", "Move  W", "Rotate  E", "Scale  R"}, cardInner);
    auto_.registerWidget("seg_gizmo");
    theme::toggle("Snap (0.5 units / 15 deg / 0.1x)", &gizmoSnap_);
    auto_.registerWidget("toggle_snap");
    ImGui::PushFont(fontSmall_);
    theme::hint("Click an object to select it. Drag the gizmo, or type values below. Del removes, Ctrl+D duplicates, Ctrl+Z/Y undo/redo, F frames, End drops to the ground.");
    ImGui::PopFont();
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

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
    if (defList_.empty() && ctx_.ready()) defList_ = ctx_.definitions({"OBJECT", "BUILDING"});
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##defsearch", "Search definitions (OBJECT_..., BUILDING_...)", defSearch_, sizeof defSearch_);
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

    // ---- changes / save
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##changes", inner);
    const auto changes = doc_.changes();
    char head[64];
    std::snprintf(head, sizeof head, "Changes  (%zu)", changes.size());
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
