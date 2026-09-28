// FableForge editor: selection, gizmo, edit panel and document plumbing.
// The App methods that make the viewer an editor live here; app.cpp keeps the
// layout, explorer, export and automation.

#include "app.hpp"
#include "vanilla_props.hpp"

#include "meshimport.hpp"

#include "nlohmann/json.hpp"
#include "effects.hpp"

#include <fstream>
#include <algorithm>
#include <functional>
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
    const editor::Document::ExternalWorld ext{e->worldFile, e->tngPath};
    if (!doc_.open(installPath_, e->name, lev, derr, e->worldFile.empty() ? nullptr : &ext)) {
        if (!derr.empty()) pushLog("editor: " + derr, 1);
        return;
    }
    if (!derr.empty()) pushLog("editor: " + derr, 1);
    docLoadedFor_ = selectedName_;
    syncedRevision_ = doc_.revision();
    hiddenSections_.clear(); sectionsDirty_ = true; sectionsCardRev_ = ~0ull;
    tracksCacheRev_ = ~0ull; linkPick_.active = false; trackLinkPick_ = false;
    loadThingOrigins();
}

// forge_mods_provenance.json next to forge_mods.json (the save root): the mod that placed or
// last changed each thing of this map, by "uid:<n>". Absent = no mod deploy = no badges.
void App::loadThingOrigins() {
    thingOrigin_.clear(); originMods_.clear(); originFilter_.clear();
    std::error_code ec;
    const fs::path path = fs::path(saveRoot()) / "forge_mods_provenance.json";
    if (!fs::exists(path, ec) || !documentLoaded()) return;
    try {
        std::ifstream in(path);
        const auto j = nlohmann::json::parse(in);
        const std::string key = "FinalAlbion/" + doc_.mapName() + ".tng";
        const auto& levels = j.value("levels", nlohmann::json::object());
        for (auto it = levels.begin(); it != levels.end(); ++it) {
            std::string k = it.key(), want = key;
            std::transform(k.begin(), k.end(), k.begin(), ::tolower); std::transform(want.begin(), want.end(), want.begin(), ::tolower);
            if (k != want) continue;
            for (auto t = it.value().begin(); t != it.value().end(); ++t) {
                thingOrigin_[t.key()] = t.value().get<std::string>();
                if (std::find(originMods_.begin(), originMods_.end(), t.value().get<std::string>()) == originMods_.end()) originMods_.push_back(t.value().get<std::string>());
            }
        }
    } catch (const std::exception& e) { pushLog(std::string("editor: forge_mods_provenance.json: ") + e.what(), 1); }
}

const char* App::originOf(uint64_t uid) const {
    if (thingOrigin_.empty()) return nullptr;
    const auto it = thingOrigin_.find("uid:" + std::to_string(uid));
    return it == thingOrigin_.end() ? nullptr : it->second.c_str();
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
    meshTextures_.clear();
    for (const auto& m : things.meshes) {
        std::vector<uint32_t> ids;
        for (const auto& p : m.parts) if (p.diffuseTexture) ids.push_back(p.diffuseTexture);
        meshTextures_.push_back(std::move(ids));
    }
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
    syncExtraSelection();
}

void App::startThingsReload() {
    if (!documentLoaded() || !ctx_.ready()) return;
    if (foliageFuture_.valid()) { thingsReloadPending_ = true; return; }
    thingsReloadPending_ = false;
    syncedRevision_ = doc_.revision();
    const MapEntry* found = findEntry(selectedName_);
    if (!found) return;
    const MapEntry entry = *found;
    // the worker holds its own reference: a context reload on the UI thread
    // (ctx_ = *ctxPending_) must not free what it is reading
    const auto ctxHold = std::make_shared<const te::Context>(ctx_);
    const te::Context* ctx = ctxHold.get();
    const std::string root = installPath_;
    const std::string text = doc_.text();
    foliageFuture_ = std::async(std::launch::async, [ctxHold, entry, ctx, root, text]() {
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
    extraUids_.clear();
    syncExtraSelection();
    if (!documentLoaded() || index < 0 || size_t(index) >= doc_.thingCount()) { selectedThing_ = -1; selectedUid_ = 0; renderer_.selectedThing = -1; return; }
    selectedThing_ = index;
    selectedUid_ = doc_.uidOf(size_t(index));
    renderer_.selectedThing = index;
}

void App::toggleSelect(int index) {
    if (!documentLoaded() || index < 0 || size_t(index) >= doc_.thingCount()) return;
    const uint64_t uid = doc_.uidOf(size_t(index));
    if (index == selectedThing_) {
        // the primary leaves: the first extra takes over
        if (extraUids_.empty()) { selectThing(-1); return; }
        const uint64_t next = extraUids_.front();
        extraUids_.erase(extraUids_.begin());
        if (const auto i = doc_.indexOfUid(next)) { selectedThing_ = int(*i); selectedUid_ = next; renderer_.selectedThing = selectedThing_; }
        syncExtraSelection();
        return;
    }
    if (selectedThing_ < 0) { selectThing(index); return; }
    const auto it = std::find(extraUids_.begin(), extraUids_.end(), uid);
    if (it != extraUids_.end()) extraUids_.erase(it); else extraUids_.push_back(uid);
    syncExtraSelection();
}

std::vector<int> App::selectionIndices() const {
    std::vector<int> v;
    if (!documentLoaded()) return v;
    if (selectedThing_ >= 0) v.push_back(selectedThing_);
    for (const uint64_t uid : extraUids_)
        if (const auto i = doc_.indexOfUid(uid)) if (int(*i) != selectedThing_) v.push_back(int(*i));
    return v;
}

void App::syncExtraSelection() {
    renderer_.alsoSelected.clear();
    if (!documentLoaded()) return;
    for (const uint64_t uid : extraUids_)
        if (const auto i = doc_.indexOfUid(uid)) renderer_.alsoSelected.push_back(int(*i));
}

void App::copySelection() {
    const auto sel = selectionIndices();
    if (sel.empty()) { pushLog("copy: nothing selected", 1); return; }
    std::vector<size_t> idx(sel.begin(), sel.end());
    clipboard_ = doc_.extract(idx);
    pushLog("copied " + std::to_string(sel.size()) + " object" + (sel.size() == 1 ? "" : "s"), 0);
}

void App::pasteClipboard() {
    if (!documentLoaded()) return;
    if (clipboard_.empty()) { pushLog("paste: the clipboard is empty (Ctrl+C a selection first)", 1); return; }
    float focus[3]; camera_.focus(focus);
    const float at[3] = {focus[0], -focus[2], focus[1]};
    try {
        const auto pasted = doc_.paste(clipboard_, at, true);
        if (pasted.empty()) return;
        extraUids_.clear();
        selectedThing_ = int(pasted.front()); selectedUid_ = doc_.uidOf(pasted.front()); renderer_.selectedThing = selectedThing_;
        for (size_t i = 1; i < pasted.size(); ++i) extraUids_.push_back(doc_.uidOf(pasted[i]));
        syncExtraSelection();
        pushLog("pasted " + std::to_string(pasted.size()) + " object" + (pasted.size() == 1 ? "" : "s") + " at the view centre", 0);
        if (editTab_ != 2) setEditTab(0);
    } catch (const std::exception& e) { pushLog(std::string("paste: ") + e.what(), 2); }
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

// The vanilla Quests dialog: which sections are drawn, where new things go, and a
// thing moved between sections. A quest section loads only with its quest in-game.
void App::drawSectionsCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!documentLoaded()) return;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##sections", inner);
    theme::label("Quest sections");
    // the section list and per-section counts, rebuilt only when the document changes
    if (sectionsCardRev_ != doc_.revision()) {
        sectionsCardRev_ = doc_.revision();
        sectionNamesCache_ = doc_.sections();
        sectionCountsCache_.clear();
        for (const auto& n : doc_.thingSections()) ++sectionCountsCache_[lowerCopy(n)];
    }
    const auto& names = sectionNamesCache_;
    auto& counts = sectionCountsCache_;
    std::vector<std::string> shown = names;
    if (shown.empty()) shown.push_back("NULL");
    const std::string current = lowerCopy(doc_.placementSection());
    ImGui::PushFont(fontSmall_);
    for (const auto& n : shown) {
        ImGui::PushID(n.c_str());
        const std::string key = lowerCopy(n);
        bool vis = !hiddenSections_.count(key);
        if (ImGui::Checkbox("##vis", &vis)) { if (vis) hiddenSections_.erase(key); else hiddenSections_.insert(key); sectionsDirty_ = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show / hide this section's things in the view");
        ImGui::SameLine();
        const bool isCurrent = key == current || (current.empty() && key == "null");
        char lbl[160]; std::snprintf(lbl, sizeof lbl, "%s  (%zu)%s", n.c_str(), counts[key], isCurrent ? "   <- new things go here" : "");
        if (ImGui::Selectable(lbl, isCurrent)) doc_.setPlacementSection(n);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(key == "null" ? "The main section: always loaded." : "Loaded with its quest. Click: new things go here.");
        ImGui::PopID();
    }
    ImGui::PopFont();
    ImGui::SetNextItemWidth(cardInner - S(70));
    ImGui::InputTextWithHint("##newsection", "New section (quest name, e.g. Q_MY_QUEST)", newSection_, sizeof newSection_, ImGuiInputTextFlags_CharsUppercase);
    auto_.registerWidget("input_new_section");
    ImGui::SameLine(0, S(6));
    if (theme::ghostButton("Add", ImVec2(S(64), S(26))) && newSection_[0]) {
        if (doc_.addSection(newSection_)) { doc_.setPlacementSection(newSection_); pushLog(std::string("section ") + newSection_ + " added; new things go there", 0); newSection_[0] = 0; }
        else pushLog("section: letters, digits and _ only, and not a name already there", 1);
    }
    auto_.registerWidget("btn_add_section");
    if (selectedThing_ >= 0) {
        const std::string target = doc_.placementSection().empty() ? std::string("NULL") : doc_.placementSection();
        const std::string mine = doc_.sectionOf(size_t(selectedThing_));
        if (lowerCopy(mine) != lowerCopy(target)) {
            const std::string l = "Move selection to " + target + "  (from " + mine + ")";
            if (theme::ghostButton(l.c_str(), ImVec2(cardInner, S(26)))) {
                if (const auto n = doc_.moveToSection(size_t(selectedThing_), target)) { selectThing(int(*n)); sectionsDirty_ = true; }
            }
            auto_.registerWidget("btn_move_section");
        }
    }
    theme::endCard();
}

// Every other field of the selected thing, one collapsible group per component
// (CTCDoor, CTCChest, CTCLight ...) like the vanilla Thing Properties tabs. TRUE /
// FALSE fields are checkboxes; the rest are text fields committed on Enter or when
// focus leaves, refused (and logged) when the value does not fit the field.
void App::drawPropertyGrid(float cardInner) {
    using theme::S;
    if (selectedThing_ < 0) return;
    const size_t idx = size_t(selectedThing_);
    const auto rows = doc_.propertiesOf(idx);
    if (rows.empty()) return;
    ImGui::Dummy(ImVec2(0, S(4)));
    theme::label("Properties");
    ImGui::PushFont(fontSmall_);
    using K = editor::Document::PropertyRow::Kind;
    std::string group = "\x01";
    bool open = false;
    const float keyW = cardInner * 0.45f;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r.ctc != group) {
            group = r.ctc;
            const std::string title = (group.empty() ? std::string("General") : group) + "##pg" + group;
            open = ImGui::CollapsingHeader(title.c_str(), group.empty() || group == "CTCDoor" || group == "CTCChest" ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        }
        if (!open) continue;
        ImGui::PushID(int(i));
        // the vanilla dialog's caption and widget for this key, when recovered
        const editor::VanillaField* vf = editor::vanillaField(r.ctc, r.key);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(vf ? theme::Text : theme::Muted), "%s", vf ? vf->label : r.key.c_str());
        if (ImGui::IsItemHovered()) {
            if (vf) ImGui::SetTooltip("%s  (.tng key %s)\nvanilla tab: %s%s", vf->label, r.key.c_str(), vf->category, vf->hasRange ? "" : "");
            else ImGui::SetTooltip(".tng key %s (no vanilla dialog entry recovered)", r.key.c_str());
        }
        ImGui::SameLine(keyW);
        ImGui::SetNextItemWidth(cardInner - keyW);
        std::string next;
        bool commit = false;
        const std::string vkind = vf ? vf->kind : "";
        int rgba[4];
        const bool isColour = std::sscanf(r.value.c_str(), "CRGBColour(%d,%d,%d,%d)", &rgba[0], &rgba[1], &rgba[2], &rgba[3]) == 4;
        const bool isFamily = r.key.rfind("CreatureFamilies[", 0) == 0 && r.kind == K::String;
        if (isColour) {
            // CTCLight / CTCSpotLight Colour: the dialog's ColourRed/Green/Blue spins as one RGBA editor,
            // committed once the edit ends (one undo step, not one per frame of a drag)
            const std::string id = std::to_string(idx) + "/" + r.ctc + "/" + r.key;
            float col[4] = {rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, rgba[3] / 255.0f};
            if (pendingColour_.live && pendingColour_.id == id) std::copy(pendingColour_.rgba, pendingColour_.rgba + 4, col);
            if (ImGui::ColorEdit4("##v", col, ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_AlphaBar)) {
                pendingColour_.id = id; pendingColour_.live = true;
                std::copy(col, col + 4, pendingColour_.rgba);
            }
            if (pendingColour_.live && pendingColour_.id == id && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "CRGBColour(%d,%d,%d,%d)", int(std::lround(col[0] * 255)), int(std::lround(col[1] * 255)), int(std::lround(col[2] * 255)), int(std::lround(col[3] * 255)));
                next = buf; commit = true;
                pendingColour_.live = false;
            }
        } else if (isFamily && ctx_.ready()) {
            // a spawner's creature family slot: the CREATURE_GENERATION_FAMILY defs (vanilla DefIndexList)
            const std::string cur = r.value.size() >= 2 ? r.value.substr(1, r.value.size() - 2) : r.value;
            ImGui::SetNextItemWidth(cardInner - keyW - S(28));
            if (ImGui::BeginCombo("##v", cur.c_str())) {
                for (const auto& [name, type] : ctx_.definitions(std::vector<std::string>{"CREATURE_GENERATION_FAMILY"}))
                    if (ImGui::Selectable(name.c_str(), name == cur)) { next = "\"" + name + "\""; commit = true; }
                ImGui::EndCombo();
            }
            ImGui::SameLine(0, S(4));
            const int slot = std::atoi(r.key.c_str() + std::strlen("CreatureFamilies["));
            if (theme::ghostButton("x##famx", ImVec2(S(24), S(22)))) {
                if (doc_.removeListEntry(idx, r.ctc, "CreatureFamilies", slot)) { pushLog("spawner: family " + cur + " removed", 0); ImGui::PopID(); break; }
            }
        } else if (vkind == "def" && vf->defType[0] && ctx_.ready() && r.kind != K::Bool) {
            // a def picker; the value keeps the file's spelling (quoted or bare), NULL = none
            const bool quoted = r.kind == K::String;
            std::string cur = r.value;
            if (quoted && cur.size() >= 2) cur = cur.substr(1, cur.size() - 2);
            if (ImGui::BeginCombo("##v", cur.c_str())) {
                if (!quoted && ImGui::Selectable("NULL", cur == "NULL")) { next = "NULL"; commit = true; }
                for (const auto& [name, type] : ctx_.definitions(std::vector<std::string>{vf->defType}))
                    if (ImGui::Selectable(name.c_str(), name == cur)) { next = quoted ? "\"" + name + "\"" : name; commit = true; }
                ImGui::EndCombo();
            }
        } else if (vkind == "enum" && vf->enumPairs[0] && r.kind == K::Int) {
            // NAME=value pairs from the dialog's option list
            std::vector<std::pair<std::string, std::string>> opts;
            std::string pairs = vf->enumPairs;
            for (size_t a = 0; a < pairs.size();) {
                size_t b = pairs.find(';', a); if (b == std::string::npos) b = pairs.size();
                const std::string item = pairs.substr(a, b - a);
                const size_t eq = item.find('=');
                if (eq != std::string::npos) opts.push_back({item.substr(0, eq), item.substr(eq + 1)});
                a = b + 1;
            }
            std::string curName = r.value;
            for (const auto& [n, v] : opts) if (v == r.value) curName = n;
            if (ImGui::BeginCombo("##v", curName.c_str())) {
                for (const auto& [n, v] : opts)
                    if (ImGui::Selectable(n.c_str(), v == r.value)) { next = v; commit = true; }
                ImGui::EndCombo();
            }
        } else if (r.kind == K::Bool) {
            bool v = r.value == "TRUE";
            if (ImGui::Checkbox("##v", &v)) { next = v ? "TRUE" : "FALSE"; commit = true; }
        } else {
            char buf[256];
            std::snprintf(buf, sizeof buf, "%s", r.value.c_str());
            ImGui::InputText("##v", buf, sizeof buf);
            if (ImGui::IsItemDeactivatedAfterEdit()) { next = buf; commit = true; }
        }
        // the vanilla dialog's range for numbers (its spin controls stop there)
        if (commit && vf && vf->hasRange && (r.kind == K::Int || r.kind == K::Float)) {
            char* end = nullptr;
            const double v = std::strtod(next.c_str(), &end);
            if (end && *end == 0 && (v < vf->min || v > vf->max)) {
                char msg[160]; std::snprintf(msg, sizeof msg, "property: %s must be %g .. %g (the vanilla dialog's range)", vf->label, vf->min, vf->max);
                pushLog(msg, 1);
                commit = false;
            }
        }
        // a spawner group ends with "add a family" (the list has no count field; entries stay contiguous)
        const bool lastOfGroup = i + 1 == rows.size() || rows[i + 1].ctc != r.ctc;
        if (lastOfGroup && ctx_.ready() && (r.ctc == "CTCCreatureGenerator" || r.ctc == "CTCDCreatureGenerator")) {
            ImGui::SetNextItemWidth(cardInner);
            if (ImGui::BeginCombo("##addfam", "+ add a creature family")) {
                for (const auto& [name, type] : ctx_.definitions(std::vector<std::string>{"CREATURE_GENERATION_FAMILY"}))
                    if (ImGui::Selectable(name.c_str())) {
                        if (doc_.addListEntry(idx, r.ctc, "CreatureFamilies", "\"" + name + "\"")) pushLog("spawner: family " + name + " added", 0);
                    }
                ImGui::EndCombo();
            }
        }
        if (commit && next != r.value) {
            if (doc_.setPropertyValue(idx, r.ctc, r.key, next)) pushLog((r.ctc.empty() ? "" : r.ctc + ".") + r.key + " = " + next, 0);
            else pushLog("property: " + next + " does not fit " + r.key + (r.kind == K::String ? " (a quoted \"text\")" : r.kind == K::Int ? " (a whole number)" : r.kind == K::Float ? " (a number)" : ""), 1);
        }
        ImGui::PopID();
    }
    ImGui::PopFont();
}

// The vanilla Tracks dialog: tracks are doubly-linked TrackNode chains sharing a
// name (guard patrols, camera paths). Place / link / flip / rename / unlink.
void App::drawTracksCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!documentLoaded()) return;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##tracks", inner);
    const auto& trs = cachedTracks();
    char head[64]; std::snprintf(head, sizeof head, "Tracks  (%zu)", trs.size());
    theme::label(head);
    ImGui::PushFont(fontSmall_);
    const bool selIsNode = selectedThing_ >= 0 && doc_.isTrackNode(size_t(selectedThing_));
    for (size_t i = 0; i < trs.size() && i < 60; ++i) {
        const auto& t = trs[i];
        const bool mine = selIsNode && std::find(t.nodes.begin(), t.nodes.end(), size_t(selectedThing_)) != t.nodes.end();
        char row[160]; std::snprintf(row, sizeof row, "%s   %zu nodes, %.1f long##trk%zu", t.name.c_str(), t.nodes.size(), t.length, i);
        if (ImGui::Selectable(row, mine)) { selectThing(int(t.nodes.front())); frameSelected(); }
    }
    if (trs.empty()) ImGui::TextColored(theme::vec(theme::Faint), "No tracks on this map.");
    ImGui::PopFont();
    const float half = (cardInner - S(6)) * 0.5f;
    if (theme::ghostButton("Place node", ImVec2(half, S(26)))) {
        float focus[3]; camera_.focus(focus);
        const float x = focus[0], y = -focus[2];
        const size_t n = doc_.placeTrackNode(x, y, doc_.groundHeight(x, y).value_or(focus[1]));
        selectThing(int(n));
        pushLog("track: node placed at the view centre (INVALID until linked)", 0);
    }
    auto_.registerWidget("btn_track_place");
    ImGui::SameLine(0, S(6));
    if (theme::ghostButton(trackLinkPick_ ? "Click a node..." : "Link to...", ImVec2(half, S(26))) && selIsNode) trackLinkPick_ = !trackLinkPick_;
    auto_.registerWidget("btn_track_link");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select a track node, press this, then click the node it leads to (Esc cancels).\nThe joined track runs from the selected node to the clicked one and keeps the selected one's name.");
    if (selIsNode) {
        if (theme::ghostButton("Flip", ImVec2(half, S(26)))) doc_.flipTrack(size_t(selectedThing_));
        auto_.registerWidget("btn_track_flip");
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Unlink node", ImVec2(half, S(26)))) doc_.unlinkTrackNode(size_t(selectedThing_));
        auto_.registerWidget("btn_track_unlink");
        ImGui::SetNextItemWidth(cardInner - S(76));
        ImGui::InputTextWithHint("##trackname", "Track name (letters, digits, _)", trackName_, sizeof trackName_);
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Rename", ImVec2(S(70), S(24))) && trackName_[0]) {
            if (doc_.renameTrack(size_t(selectedThing_), trackName_)) pushLog(std::string("track renamed ") + trackName_, 0);
            else pushLog("track: letters, digits and _ only", 1);
        }
        auto_.registerWidget("btn_track_rename");
    }
    ImGui::PushFont(fontSmall_);
    theme::hint("A track is a chain of TRACK_NODE_BASIC things sharing one name: village guards patrol them (GuardTrack) and cut-scene cameras can follow them. Links run head -> tail; the ends carry Start / End; no branches or loops (the engine asserts on them).");
    ImGui::PopFont();
    theme::endCard();
}

void App::drawTrackLines(const ImVec2& origin, const ImVec2& size) {
    if (!documentLoaded()) return;
    if (!editMode_ || !documentLoaded()) return;
    const bool selIsNode = selectedThing_ >= 0 && doc_.isTrackNode(size_t(selectedThing_));
    if (editTab_ != 3 && !selIsNode) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto screen = [&](size_t i, ImVec2& out) {
        editor::Frame f;
        if (!doc_.frameOf(i, f)) return false;
        const float p[3] = {f.pos[0], f.pos[2] + 0.3f, -f.pos[1]};
        float u, v;
        if (!renderer_.project(p, u, v)) return false;
        out = ImVec2(origin.x + u * size.x, origin.y + v * size.y);
        return true;
    };
    for (const auto& t : cachedTracks()) {
        const bool mine = selIsNode && std::find(t.nodes.begin(), t.nodes.end(), size_t(selectedThing_)) != t.nodes.end();
        const ImU32 col = mine ? IM_COL32(255, 220, 90, 240) : IM_COL32(120, 200, 255, 170);
        ImVec2 prev;
        bool havePrev = false;
        for (size_t k = 0; k < t.nodes.size(); ++k) {
            ImVec2 p;
            if (!screen(t.nodes[k], p)) { havePrev = false; continue; }
            dl->AddCircleFilled(p, theme::S(k == 0 ? 5.0f : 3.5f), col);
            if (havePrev) {
                dl->AddLine(prev, p, col, theme::S(2.0f));
                // an arrow head mid-segment: the direction the track runs
                const ImVec2 m((prev.x + p.x) * 0.5f, (prev.y + p.y) * 0.5f), d(p.x - prev.x, p.y - prev.y);
                const float len = std::sqrt(d.x * d.x + d.y * d.y);
                if (len > theme::S(20)) {
                    const ImVec2 u(d.x / len, d.y / len), n(-u.y, u.x);
                    const float a = theme::S(6);
                    dl->AddTriangleFilled(ImVec2(m.x + u.x * a, m.y + u.y * a), ImVec2(m.x - u.x * a + n.x * a * 0.7f, m.y - u.y * a + n.y * a * 0.7f),
                                          ImVec2(m.x - u.x * a - n.x * a * 0.7f, m.y - u.y * a - n.y * a * 0.7f), col);
                }
            }
            prev = p; havePrev = true;
        }
        if (mine || editTab_ == 3) {
            ImVec2 p;
            if (screen(t.nodes.front(), p)) dl->AddText(ImVec2(p.x + theme::S(6), p.y - theme::S(14)), col, t.name.c_str());
        }
    }
}

void App::applySectionVisibility() {
    if (!documentLoaded()) return;
    const size_t n = renderer_.instanceCount();
    if (!sectionsDirty_ && sectionsAppliedRev_ == doc_.revision() && sectionsAppliedInstances_ == n) return;
    sectionsDirty_ = false; sectionsAppliedRev_ = doc_.revision(); sectionsAppliedInstances_ = n;
    const auto per = doc_.thingSections();
    for (size_t i = 0; i < n; ++i) {
        const int t = renderer_.instance(i).thing;
        const bool hidden = t >= 0 && size_t(t) < per.size() && hiddenSections_.count(lowerCopy(per[size_t(t)]));
        renderer_.setInstanceVisible(i, !hidden);
    }
}

const std::vector<editor::Document::Track>& App::cachedTracks() {
    if (tracksCacheRev_ != doc_.revision()) { tracksCache_ = doc_.tracks(); tracksCacheRev_ = doc_.revision(); }
    return tracksCache_;
}

bool App::thingsStale() const {
    return foliageFuture_.valid() || syncedRevision_ != doc_.revision();
}

int App::trackNodeAt(float px, float py) const {
    int best = -1;
    float bestD = theme::S(12.0f) * theme::S(12.0f);
    for (const auto& t : tracksCache_)
        for (const size_t n : t.nodes) {
            editor::Frame f;
            if (!doc_.frameOf(n, f)) continue;
            const float p[3] = {f.pos[0], f.pos[2] + 0.3f, -f.pos[1]};
            float u, v;
            if (!renderer_.project(p, u, v)) continue;
            const float dx = viewportOrigin_.x + u * viewportSize_.x - px, dy = viewportOrigin_.y + v * viewportSize_.y - py;
            if (dx * dx + dy * dy < bestD) { bestD = dx * dx + dy * dy; best = int(n); }
        }
    return best;
}

std::string App::thingLabel(size_t index) const {
    const auto s = doc_.summary(index);
    return s.scriptName.empty() ? s.definition : s.scriptName + " (" + s.definition + ")";
}

// Lines from the selected thing to the things it links to (the vanilla editor's
// DrawAttachModeLines): owner, village, home, exit -> entrance ...
void App::drawLinkLines(const ImVec2& origin, const ImVec2& size) {
    if (!editMode_ || !documentLoaded() || selectedThing_ < 0) return;
    editor::Frame from;
    if (!doc_.frameOf(size_t(selectedThing_), from)) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto screen = [&](const editor::Frame& f, ImVec2& out) {
        const float p[3] = {f.pos[0], f.pos[2] + 1.0f, -f.pos[1]};
        float u, v;
        if (!renderer_.project(p, u, v)) return false;
        out = ImVec2(origin.x + u * size.x, origin.y + v * size.y);
        return true;
    };
    ImVec2 a;
    if (!screen(from, a)) return;
    for (const auto& l : doc_.linksOf(size_t(selectedThing_))) {
        editor::Frame to;
        ImVec2 b;
        if (!l.targetIndex || !doc_.frameOf(*l.targetIndex, to) || !screen(to, b)) continue;
        const ImU32 col = l.field == "VillageUID" ? IM_COL32(120, 200, 255, 220) : l.field == "EntranceConnectedToUID" ? IM_COL32(255, 170, 60, 220) : IM_COL32(200, 160, 255, 220);
        dl->AddLine(a, b, col, theme::S(2.0f));
        dl->AddCircleFilled(b, theme::S(4.0f), col);
        dl->AddText(ImVec2((a.x + b.x) * 0.5f + theme::S(4), (a.y + b.y) * 0.5f), col, l.label.c_str());
    }
}

int App::pickAt(float u, float v) {
    float o[3], d[3];
    renderer_.screenRay(u, v, o, d);
    float t;
    const int inst = renderer_.pick(o, d, t);
    if (trackLinkPick_) {
        trackLinkPick_ = false;
        // the drawn node dots (a track node may have no mesh to pick), else a mesh hit
        const int dot = trackNodeAt(viewportOrigin_.x + u * viewportSize_.x, viewportOrigin_.y + v * viewportSize_.y);
        const int target = dot >= 0 ? dot : (inst < 0 || thingsStale()) ? -1 : renderer_.instance(size_t(inst)).thing;
        if (target < 0 || selectedThing_ < 0) { pushLog("track: no node picked", 1); return -1; }
        std::string err;
        if (doc_.linkTrackNodes(size_t(selectedThing_), size_t(target), err)) { pushLog("track: linked", 0); selectThing(target); }
        else pushLog("track: " + err, 1);
        return target;
    }
    if (linkPick_.active) {
        // link pick: the clicked thing becomes the target; the selection stays
        if (thingsStale()) { pushLog("link: the objects are reloading after an edit; click again in a moment", 1); return -1; }
        linkPick_.active = false;
        const int target = inst < 0 ? -1 : renderer_.instance(size_t(inst)).thing;
        if (target < 0 || selectedThing_ < 0 || target == selectedThing_) { pushLog("link: no target picked", 1); return -1; }
        editor::Document::Link link;
        for (const auto& l : doc_.linksOf(size_t(selectedThing_))) if (l.ctc == linkPick_.ctc && l.field == linkPick_.field) link = l;
        if (!doc_.linkTargetFits(link, size_t(target))) { pushLog("link: " + linkPick_.label + " wants " + link.wants + "; " + thingLabel(size_t(target)) + " is not one", 1); return -1; }
        if (doc_.setLink(size_t(selectedThing_), linkPick_.ctc, linkPick_.field, doc_.uidOf(size_t(target))))
            pushLog("link: " + linkPick_.label + " -> " + thingLabel(size_t(target)), 0);
        return target;
    }
    if (inst < 0) { if (!ImGui::GetIO().KeyCtrl) selectThing(-1); return -1; }
    const int thing = renderer_.instance(size_t(inst)).thing;
    if (ImGui::GetIO().KeyCtrl) toggleSelect(thing); else selectThing(thing);
    if (editTab_ == 1 || editTab_ == 3) setEditTab(0);
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
    doc_.beginBatch();
    for (const int i : selectionIndices()) {
        editor::Frame g;
        if (!doc_.frameOf(size_t(i), g)) continue;
        g.pos[0] += dx; g.pos[1] += dy; g.pos[2] += dz;
        try { doc_.setFrame(size_t(i), g); } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); }
    }
    doc_.endBatch();
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

bool App::importMesh(const std::string& model, const std::string& name, const std::string& texturePng) {
    if (meshImportFuture_.valid()) { pushLog("import model: still busy", 1); return false; }
    if (ctxFuture_.valid()) { pushLog("import model: textures are still loading, try again in a moment", 1); return false; }
    meshimport::ImportRequest req;
    req.model = model; req.name = name; req.texturePng = texturePng;
    pushLog("import model " + name + ": composing " + model + " into graphics.big (a copy of the bank is rewritten; a few seconds)...", 0);
    const std::string root = saveRoot();
    meshImportFuture_ = std::async(std::launch::async, [root, req]() {
        MeshImportJob job;
        meshimport::ImportResult out;
        job.ok = meshimport::importModel(root, req, out, job.error);
        job.notes = out.notes; job.objectName = out.objectName;
        return job;
    });
    return true;
}

void App::pollMeshImport() {
    if (!meshImportFuture_.valid() || meshImportFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const MeshImportJob job = meshImportFuture_.get();
    if (!job.ok) { pushLog("import model failed: " + job.error, 2); return; }
    for (const auto& n : job.notes) pushLog("import model: " + n, 0);
    pushLog(job.objectName + " ready: find it under Add an object (with a collision hull from its own triangles; not yet seen in-game)", 3);
    // the def list, the thumbnails and the texture context must see the new entries
    foliageexport::closeMeshBank(); thumbBankOpen_ = false; defThumbs_.clear(); defList_.clear(); themeGroupOf_.clear(); envDefs_.clear(); soundDefs_.clear();
    meshModelPath_[0] = 0; meshName_[0] = 0; meshTexturePng_[0] = 0;
    startContextLoad(saveRoot());
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
    auto sel = selectionIndices();
    if (sel.empty()) return;
    try {
        // highest index first so the insert-after-original does not shift the rest;
        // the copies become the new selection (the primary's copy stays primary)
        std::vector<int> order = sel;
        std::sort(order.begin(), order.end(), std::greater<int>());
        std::vector<uint64_t> copies;
        uint64_t primaryCopy = 0;
        doc_.beginBatch();
        for (const int i : order) {
            const size_t n = doc_.duplicate(size_t(i));
            const uint64_t uid = doc_.uidOf(n);
            if (i == selectedThing_) primaryCopy = uid; else copies.push_back(uid);
        }
        doc_.endBatch();
        extraUids_ = copies;
        if (const auto p = doc_.indexOfUid(primaryCopy)) { selectedThing_ = int(*p); selectedUid_ = primaryCopy; renderer_.selectedThing = selectedThing_; }
        syncExtraSelection();
        pushLog(sel.size() == 1 ? "duplicated " + doc_.summary(size_t(selectedThing_)).definition : "duplicated " + std::to_string(sel.size()) + " objects", 0);
    } catch (const std::exception& e) { doc_.endBatch(); pushLog(std::string("editor: ") + e.what(), 2); }
}

void App::deleteSelected() {
    if (!documentLoaded() || selectedThing_ < 0) return;
    auto sel = selectionIndices();
    if (sel.empty()) return;
    const std::string what = sel.size() == 1 ? doc_.summary(size_t(selectedThing_)).definition : std::to_string(sel.size()) + " objects";
    std::sort(sel.begin(), sel.end(), std::greater<int>());
    try {
        doc_.beginBatch();
        for (const int i : sel) doc_.remove(size_t(i));
        doc_.endBatch();
    } catch (const std::exception& e) { doc_.endBatch(); pushLog(std::string("editor: ") + e.what(), 2); return; }
    selectedThing_ = -1; selectedUid_ = 0; renderer_.selectedThing = -1;
    extraUids_.clear(); syncExtraSelection();
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

// The placement palette: with an empty search, a tree of type -> THING_GROUP -> def
// (groups named by their def, G_CREATURES_BANDIT shown as "CREATURES BANDIT");
// with a search, a flat list of matches with their group. Click selects, double-click places.
void App::drawDefPalette(const char* id, const std::vector<std::string>& types, float width, float height) {
    using theme::S;
    if (defList_.empty() && ctx_.ready()) defList_ = ctx_.groupedDefinitions({"OBJECT", "BUILDING", "CREATURE"});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
    ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    ImGui::PushFont(fontSmall_);
    const float rowH = S(34), thumb = S(30);
    thumbBudget_ = 1;   // one mesh decode per frame across the visible rows
    auto row = [&](const terrainexport::Context::GroupedDefinition& d, bool showGroup) {
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable((std::string("##def") + d.name).c_str(), d.name == placeDef_, 0, ImVec2(0, rowH))) placeDef_ = d.name;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { placeDef_ = d.name; placeDefinition(d.name); }
        // thumbnail + name drawn over the row; rows off screen are not decoded
        if (ImGui::IsItemVisible()) {
            bool pending = false;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ID3D11ShaderResourceView* srv = defThumbnail(d.name, pending);
            const ImVec2 p(rowPos.x + S(2), rowPos.y + (rowH - thumb) * 0.5f);
            if (srv) dl->AddImageRounded((ImTextureID)(intptr_t)srv, p, ImVec2(p.x + thumb, p.y + thumb), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, S(4));
            else dl->AddRectFilled(p, ImVec2(p.x + thumb, p.y + thumb), theme::col(pending ? theme::Bg2 : theme::Bg3), S(4));
            const float ty = rowPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            dl->AddText(ImVec2(p.x + thumb + S(8), ty), theme::col(theme::Text), d.name.c_str());
            if (showGroup && !d.group.empty()) {
                const float nx = p.x + thumb + S(8) + ImGui::CalcTextSize(d.name.c_str()).x + S(10);
                dl->AddText(ImVec2(nx, ty), theme::col(theme::Faint), d.group.c_str());
            }
        }
    };
    auto wanted = [&](const std::string& type) { return std::find(types.begin(), types.end(), type) != types.end(); };
    if (!ctx_.ready()) ImGui::TextColored(theme::vec(theme::Faint), "Definitions load with the textures...");
    else if (defSearch_[0]) {
        int shown = 0;
        for (const auto& d : defList_) {
            if (!wanted(d.type) || !(contains(d.name, defSearch_) || contains(d.group, defSearch_))) continue;
            row(d, true);
            if (++shown >= 300) { ImGui::TextColored(theme::vec(theme::Faint), "...type more to narrow down"); break; }
        }
        if (!shown) ImGui::TextColored(theme::vec(theme::Faint), "no match");
    } else {
        // defList_ is sorted type / group / name: walk it in runs
        auto groupLabel = [](const std::string& g) {
            if (g.empty()) return std::string("(no group)");
            std::string s = g.rfind("GO_", 0) == 0 ? g.substr(3) : g.rfind("G_", 0) == 0 ? g.substr(2) : g;
            for (auto& c : s) if (c == '_') c = ' ';
            return s;
        };
        const bool oneType = types.size() == 1;
        for (size_t i = 0; i < defList_.size();) {
            const std::string& type = defList_[i].type;
            size_t typeEnd = i;
            while (typeEnd < defList_.size() && defList_[typeEnd].type == type) ++typeEnd;
            if (!wanted(type)) { i = typeEnd; continue; }
            char head[96]; std::snprintf(head, sizeof head, "%s  (%zu)", type.c_str(), typeEnd - i);
            const bool typeOpen = oneType || ImGui::TreeNodeEx((std::string(head) + "##t" + type).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
            if (typeOpen) {
                for (size_t j = i; j < typeEnd;) {
                    const std::string& group = defList_[j].group;
                    size_t groupEnd = j;
                    while (groupEnd < typeEnd && defList_[groupEnd].group == group) ++groupEnd;
                    char gh[128]; std::snprintf(gh, sizeof gh, "%s  (%zu)##g%s%s", groupLabel(group).c_str(), groupEnd - j, type.c_str(), group.c_str());
                    const bool old = group.find("DO_NOT_USE") != std::string::npos;
                    if (old) ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::Faint));
                    const bool open = ImGui::TreeNodeEx(gh, ImGuiTreeNodeFlags_SpanAvailWidth);
                    if (old) ImGui::PopStyleColor();
                    if (open) {
                        for (size_t k = j; k < groupEnd; ++k) row(defList_[k], false);
                        ImGui::TreePop();
                    }
                    j = groupEnd;
                }
                if (!oneType) ImGui::TreePop();
            }
            i = typeEnd;
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
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
    p.scriptName = scriptName;
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
        if (editTab_ != 2) setEditTab(0);
        if (creature) raiseRule("creature");
        return true;
    } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); return false; }
}

bool App::saveDocument() {
    if (!documentLoaded()) return false;
    if (doc_.external() && saveRoot() != installPath_) { pushLog("save: this map belongs to another world and writes its own files; a redirected save root does not apply to it", 2); return false; }
    std::string err;
    if (!doc_.saveLoose(saveRoot(), err)) { pushLog("save failed: " + err, 2); return false; }
    pushLog("saved " + doc_.loosePath().string(), 3);
    return true;
}

bool App::gameWriteBlocked(const char* what) {
    linkPoll(true);
    if (link_.heartbeatAge >= 0 && link_.heartbeatAge < 5.0) { pushLog(std::string(what) + ": the game is running (live link heartbeat) -- rewriting its files underneath it crashes it; quit to the desktop first", 1); return true; }
    if (backups::gameRunningIn(saveRoot())) { pushLog(std::string(what) + ": Fable.exe is running from this install -- it holds the WAD/STB/textures/defs open and rewriting them crashes it; quit to the desktop first", 1); return true; }
    return false;
}

bool App::deployDocument() {
    if (!documentLoaded()) return false;
    if (gameWriteBlocked("deploy")) return false;
    if (doc_.external() && saveRoot() != installPath_) { pushLog("deploy: this map belongs to another world and writes its own files; a redirected save root does not apply to it", 2); return false; }
    std::string err;
    if (!doc_.deployWad(saveRoot(), err)) { pushLog("deploy failed: " + err, 2); return false; }
    if (writesLoose()) pushLog("wrote " + doc_.loosePath().string() + " (loose-level install: the game reads this file)", 3);
    else pushLog("wrote " + doc_.mapName() + ".tng into FinalAlbion.wad (backup FinalAlbion.wad.forge-orig)", 3);
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
    // the worker holds its own reference: a context reload on the UI thread
    // (ctx_ = *ctxPending_) must not free what it is reading
    const auto ctxHold = std::make_shared<const te::Context>(ctx_);
    const te::Context* ctx = ctxHold.get();
    const float gain = settings_.gain;
    auto level = std::make_shared<forge::lev::File>(*doc_.level());   // snapshot: strokes may continue meanwhile
    const int texels = previewTexelsFor(level->cellsX(), level->cellsY());
    previewFuture_ = std::async(std::launch::async, [ctxHold, entry, ctx, texels, gain, level]() {
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

// A picker over the map's LEV ground-theme palette (named slots only).
void App::paletteCombo(const char* id, int& slot, float width) {
    using theme::S;
    const forge::lev::File* lev = doc_.level();
    const char* current = "(pick a ground theme)";
    if (lev && slot >= 0 && size_t(slot) < lev->groundThemes().size() && !lev->groundThemes()[size_t(slot)].name.empty())
        current = lev->groundThemes()[size_t(slot)].name.c_str();
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, current)) {
        if (lev)
            for (size_t i = 0; i < lev->groundThemes().size(); ++i) {
                const auto& g = lev->groundThemes()[i];
                if (g.name.empty()) continue;
                char lbl[160]; std::snprintf(lbl, sizeof lbl, "%zu  %s", i, g.name.c_str());
                const float rowH = S(24);
                const ImVec2 rowPos = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable((std::string("##pal") + std::to_string(i)).c_str(), int(i) == slot, 0, ImVec2(0, rowH))) slot = int(i);
                themeRow(g.name, rowPos, rowH, lbl);
            }
        ImGui::EndCombo();
    }
}

namespace {
editor::TerrainBrush::Mode brushModeFor(int mode) {
    using M = editor::TerrainBrush::Mode;
    switch (mode) {
        case 0: return M::Raise;
        case 1: return M::Lower;
        case 2: return M::Flatten;
        case 3: return M::Smooth;
        case 4: return M::Walkable;
        case 5: return M::Blocked;
        case 7: return M::ReplaceTheme;
        case 10: return M::Environment;
        case 11: return M::Sound;
        case 12: return M::CameraPass;
        case 14: case 15: return M::Theme;   // not brushes: handled before any stroke starts
        case 13: return M::CameraBlock;
        default: return M::Theme;
    }
}
}  // namespace

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
    int mode = terrainMode_;
    if (io.KeyShift && (mode == 0 || mode == 1)) mode = 1 - mode;
    if (io.KeyShift && (mode == 4 || mode == 5)) mode = 9 - mode;
    b.mode = brushModeFor(mode);
    b.x = brushFable_[0]; b.y = brushFable_[1];
    b.radius = brushRadius_; b.strength = brushStrength_;
    b.themeIndex = uint8_t(terrainMode_ == 10 ? envSlot_ : terrainMode_ == 11 ? soundIndex_ : paintTheme_);
    b.replaceFrom = uint8_t(std::max(replaceFrom_, 0));
    const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool press = lmb && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && viewportHovered_ && brushHit_ && !io.KeyAlt;
    // Ctrl+click in the theme tools: the vanilla eyedropper (PaintInputPickupTheme) --
    // the cell's strongest theme becomes the one painted, Ctrl+Shift the one replaced
    if (press && io.KeyCtrl && terrainMode_ >= 6 && terrainMode_ <= 8) {
        if (const auto t = doc_.dominantThemeAt(brushFable_[0], brushFable_[1])) {
            if (io.KeyShift) replaceFrom_ = *t; else paintTheme_ = *t;
            const auto& pal = doc_.level()->groundThemes();
            pushLog(std::string("theme picked for ") + (io.KeyShift ? "replace: " : "paint: ") + (size_t(*t) < pal.size() ? pal[*t].name : std::to_string(*t)), 0);
        }
        return;
    }
    if (press && io.KeyCtrl && (terrainMode_ == 10 || terrainMode_ == 11)) {
        if (const auto es = doc_.environmentAndSoundAt(brushFable_[0], brushFable_[1])) {
            if (terrainMode_ == 10) envSlot_ = es->first; else soundIndex_ = es->second;
            pushLog(terrainMode_ == 10 ? "environment picked" : "sound picked", 0);
        }
        return;
    }
    if (terrainMode_ == 8) {
        if (press) {
            if (replaceFrom_ < 0) { pushLog("flood replace: pick the theme to replace (Ctrl+Shift+click the ground or the list)", 1); return; }
            const size_t n = doc_.replaceTheme(uint8_t(replaceFrom_), uint8_t(paintTheme_), editor::Document::ReplaceScope::Connected, brushFable_[0], brushFable_[1]);
            pushLog(n ? "flood replace: " + std::to_string(n) + " cells" : std::string("flood replace: the clicked cell does not hold the theme to replace"), n ? 0 : 1);
        }
        return;
    }
    if (terrainMode_ == 14) {
        if (press) { clipDrag_ = true; clipStart_[0] = brushFable_[0]; clipStart_[1] = brushFable_[1]; }
        else if (clipDrag_ && !lmb) {
            clipDrag_ = false;
            if (brushHit_) {
                terrainClip_ = doc_.copyTerrain(int(std::lround(clipStart_[0])), int(std::lround(clipStart_[1])), int(std::lround(brushFable_[0])), int(std::lround(brushFable_[1])));
                clipTurns_ = 0;
                pushLog("copied " + std::to_string(terrainClip_.w) + " x " + std::to_string(terrainClip_.h) + " vertices of ground; Paste region places it", 0);
            }
        }
        return;
    }
    if (terrainMode_ == 15) {
        if (ImGui::IsKeyPressed(ImGuiKey_R) && !io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive()) clipTurns_ = (clipTurns_ + 1) % 4;
        if (press) {
            if (terrainClip_.empty()) { pushLog("paste: copy a region first (Copy region, drag on the ground)", 1); return; }
            const size_t n = doc_.pasteTerrain(terrainClip_, int(std::lround(brushFable_[0])), int(std::lround(brushFable_[1])), clipTurns_, clipHeights_, clipThemes_, clipRelative_);
            pushLog("paste: " + std::to_string(n) + " vertices (one undo step)", n ? 0 : 1);
        }
        return;
    }
    if (terrainMode_ == 9) {
        if (press) { pathDrag_ = true; pathStart_[0] = brushFable_[0]; pathStart_[1] = brushFable_[1]; }
        else if (pathDrag_ && !lmb) {
            pathDrag_ = false;
            const size_t n = brushHit_ ? doc_.drawPath(pathStart_[0], pathStart_[1], brushFable_[0], brushFable_[1], brushRadius_) : 0;
            pushLog(n ? "path: " + std::to_string(n) + " vertices levelled along the drag" : std::string("path: release on the ground to draw it"), n ? 0 : 1);
        }
        return;
    }
    if ((terrainMode_ == 10 || terrainMode_ == 11) && !doc_.hasGameMap()) return;   // an older .lev: no grid to paint
    if (terrainMode_ == 7 && replaceFrom_ == paintTheme_) { if (press) pushLog("replace: the two themes are the same", 1); return; }
    if (terrainMode_ == 7 && replaceFrom_ < 0) {
        if (press) pushLog("replace: pick the theme to replace (Ctrl+Shift+click the ground or the list)", 1);
        return;
    }
    if (!doc_.strokeActive() && press) {
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
    b.mode = brushModeFor(terrainMode_);
    b.x = x; b.y = y; b.radius = brushRadius_; b.strength = brushStrength_;
    b.themeIndex = uint8_t(terrainMode_ == 10 ? envSlot_ : terrainMode_ == 11 ? soundIndex_ : paintTheme_);
    b.replaceFrom = uint8_t(std::max(replaceFrom_, 0));
    doc_.beginStroke(b);
    doc_.applyBrush(b, seconds);
    doc_.endStroke();
}

// A map-local rectangle outlined on the current ground.
void App::drawGroundRect(const ImVec2& origin, const ImVec2& size, float x0, float y0, float x1, float y1, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float xs[5] = {x0, x1, x1, x0, x0}, ys[5] = {y0, y0, y1, y1, y0};
    ImVec2 pts[4 * 16 + 1];
    int m = 0;
    for (int e = 0; e < 4; ++e)
        for (int i = 0; i < 16; ++i) {
            const float t = float(i) / 16.0f;
            const float fx = xs[e] + (xs[e + 1] - xs[e]) * t, fy = ys[e] + (ys[e + 1] - ys[e]) * t;
            const float p[3] = {fx, doc_.terrainHeight(fx, fy).value_or(0.0f) + 0.1f, -fy};
            float u, v;
            if (renderer_.project(p, u, v)) pts[m++] = ImVec2(origin.x + u * size.x, origin.y + v * size.y);
        }
    if (m > 2) dl->AddPolyline(pts, m, col, ImDrawFlags_Closed, theme::S(2.0f));
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
    const ImU32 col = terrainMode_ == 10 ? IM_COL32(120, 220, 200, 230) : terrainMode_ == 11 ? IM_COL32(120, 170, 255, 230)
                    : terrainMode_ >= 6 && terrainMode_ <= 8 ? IM_COL32(240, 200, 80, 230)
                    : terrainMode_ == 4 ? IM_COL32(80, 220, 140, 230) : terrainMode_ == 5 ? IM_COL32(230, 80, 90, 230) : theme::col(theme::Accent);
    dl->AddPolyline(pts, got, col, ImDrawFlags_Closed, theme::S(2.0f));
    if (terrainMode_ == 14 && clipDrag_)
        drawGroundRect(origin, size, clipStart_[0], clipStart_[1], brushFable_[0], brushFable_[1], IM_COL32(255, 255, 255, 220));
    if (terrainMode_ == 15 && !terrainClip_.empty()) {
        const float px = std::round(brushFable_[0]), py = std::round(brushFable_[1]);
        const int ow = (clipTurns_ % 2) ? terrainClip_.h : terrainClip_.w, oh = (clipTurns_ % 2) ? terrainClip_.w : terrainClip_.h;
        drawGroundRect(origin, size, px, py, px + float(ow - 1), py + float(oh - 1), IM_COL32(255, 210, 90, 230));
    }
    if (terrainMode_ == 9 && pathDrag_) {
        // the path being dragged, draped on the current ground
        ImVec2 line[25];
        int m = 0;
        for (int i = 0; i <= 24; ++i) {
            const float t = float(i) / 24.0f;
            const float fx = pathStart_[0] + (brushFable_[0] - pathStart_[0]) * t, fy = pathStart_[1] + (brushFable_[1] - pathStart_[1]) * t;
            const float p[3] = {fx, doc_.terrainHeight(fx, fy).value_or(0.0f) + 0.1f, -fy};
            float lu, lv;
            if (renderer_.project(p, lu, lv)) line[m++] = ImVec2(origin.x + lu * size.x, origin.y + lv * size.y);
        }
        if (m > 1) dl->AddPolyline(line, m, col, ImDrawFlags_None, theme::S(3.0f));
    }
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
        setEditTab(2);
        raiseRule("spawner");
        return true;
    } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); return false; }
}

bool App::placeVillage(const std::string& def, const std::string& scriptName) {
    if (!documentLoaded()) { pushLog("editor: no level document", 1); return false; }
    if (def.rfind("VILLAGE_", 0) != 0) { pushLog("editor: " + def + " is not a VILLAGE_ definition", 1); return false; }
    float focus[3]; camera_.focus(focus);
    float pos[3] = {focus[0], -focus[2], focus[1]};
    if (const auto h = doc_.groundHeight(pos[0], pos[1])) pos[2] = *h;
    try {
        const size_t n = doc_.placeVillage(pos, def, scriptName);
        selectedUid_ = doc_.uidOf(n);
        selectedThing_ = int(n);
        renderer_.selectedThing = selectedThing_;
        pushLog("placed village " + def + " (uid " + std::to_string(selectedUid_) + "); join buildings and creatures to it from their Selection card", 0);
        setEditTab(2);
        return true;
    } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); return false; }
}

bool App::setSelectedVillage(uint64_t villageUid) {
    if (!documentLoaded() || selectedThing_ < 0) return false;
    try {
        doc_.setVillageMember(size_t(selectedThing_), villageUid);
        // the block edit re-inserts the thing: keep the selection by uid
        if (const auto i = doc_.indexOfUid(selectedUid_)) { selectedThing_ = int(*i); renderer_.selectedThing = selectedThing_; }
        pushLog(villageUid ? "joined village " + std::to_string(villageUid) : std::string("left the village"), 0);
        return true;
    } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); return false; }
}

void App::drawVillageCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!documentLoaded()) return;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##village", inner);
    theme::label("Village");
    ImGui::PushFont(fontSmall_);
    theme::hint("A VILLAGE_* thing (the retail CTCVillage block: guards, crime, homes). Place it, then pick it in the Village box of each building, marker and creature that belongs to it.");
    ImGui::PopFont();
    if (villageList_.empty() && ctx_.ready()) villageList_ = ctx_.definitions({"VILLAGE"});
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##villagesearch", "Search village definitions (OAKVALE, BOWERSTONE...)", villageSearch_, sizeof villageSearch_);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_villagesearch");
    if (villageSearch_[0]) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
        ImGui::BeginChild("##villagelist", ImVec2(cardInner, S(90)), ImGuiChildFlags_None);
        ImGui::PopStyleColor();
        ImGui::PushFont(fontSmall_);
        int shown = 0;
        for (const auto& [name, type] : villageList_) {
            if (!contains(name, villageSearch_)) continue;
            if (ImGui::Selectable(name.c_str(), false)) placeVillage(name);
            if (++shown >= 100) break;
        }
        if (!shown) ImGui::TextColored(theme::vec(theme::Faint), "no match");
        ImGui::PopFont();
        ImGui::EndChild();
    }
    const auto vills = doc_.villages();
    if (!vills.empty()) {
        ImGui::PushFont(fontSmall_);
        for (const auto& v : vills) {
            size_t members = 0;
            for (size_t i = 0; i < doc_.thingCount(); ++i) members += doc_.villageOf(i) == v.uid;
            ImGui::TextColored(theme::vec(theme::Muted), "%s  %s   %zu member(s)", v.definition.c_str(), v.scriptName.c_str(), members);
        }
        ImGui::PopFont();
    }
    theme::endCard();
}

// ------------------------------------------------------------ live link

void App::linkPoll(bool force) {
    const double now = ImGui::GetTime();
    if (!force && linkPolledAt_ >= 0 && now - linkPolledAt_ < 1.0) return;
    linkPolledAt_ = now;
    link_ = livelink::poll(installPath_);
    if (linkFollow_ && documentLoaded() && link_.heartbeatAge >= 0 && link_.heartbeatAge < 5.0 && link_.heroMap == doc_.mapName()) {
        const float lx = link_.heroX - float(doc_.worldX()), ly = link_.heroY - float(doc_.worldY());
        camera_.lookAt(lx, link_.heroZ, -ly, camera_.yaw, camera_.pitch, camera_.distance);
    }
}

bool App::linkInstall() {
    std::string err;
    if (!livelink::install(installPath_, err)) { pushLog("live link: " + err, 2); return false; }
    pushLog("live link installed (FSE/AtlasLink + a Main hook in PartyMode.lua); start the game through ForgeFSE", 3);
    return true;
}

bool App::linkRemove() {
    std::string err;
    if (!livelink::remove(installPath_, err)) { pushLog("live link: " + err, 2); return false; }
    pushLog("live link removed from PartyMode.lua", 0);
    return true;
}

bool App::linkGoHere() {
    if (!documentLoaded()) { pushLog("live link: no level document", 1); return false; }
    if (!doc_.worldSlot()) { pushLog("live link: " + doc_.mapName() + " is not placed in FinalAlbion.wld", 1); return false; }
    float focus[3]; camera_.focus(focus);
    const float wx = focus[0] + float(doc_.worldX()), wy = -focus[2] + float(doc_.worldY());
    std::string err;
    linkLastSent_ = livelink::sendTeleport(installPath_, doc_.worldSlot(), doc_.mapName(), wx, wy, err);
    if (!linkLastSent_) { pushLog("live link: " + err, 2); return false; }
    pushLog("live link: hero -> " + doc_.mapName() + " (" + std::to_string(int(focus[0])) + ", " + std::to_string(int(-focus[2])) + ")", 0);
    return true;
}

bool App::linkSpawnSelected() {
    editor::Frame f;
    if (!frameOfSelected(f)) { pushLog("live link: select a creature first", 1); return false; }
    const auto s = doc_.summary(size_t(selectedThing_));
    if (s.definition.rfind("CREATURE_", 0) != 0) { pushLog("live link: " + s.definition + " is not a creature (spawning places creatures only)", 1); return false; }
    std::string err;
    linkLastSent_ = livelink::sendSpawn(installPath_, s.definition, f.pos[0] + float(doc_.worldX()), f.pos[1] + float(doc_.worldY()), s.scriptName, err);
    if (!linkLastSent_) { pushLog("live link: " + err, 2); return false; }
    pushLog("live link: spawn " + s.definition + " at the selected spot", 0);
    return true;
}

bool App::linkReload() {
    if (!documentLoaded()) { pushLog("live link: no level document", 1); return false; }
    linkPoll(true);
    if (link_.heartbeatAge < 0 || link_.heartbeatAge > 5.0) { pushLog("live link: the game is not live", 1); return false; }
    if (link_.heroMap != doc_.mapName()) { pushLog("live link: the hero is in " + link_.heroMap + ", not " + doc_.mapName() + " -- use Go here first", 1); return false; }
    if (!doc_.worldSlot()) { pushLog("live link: " + doc_.mapName() + " is not placed in FinalAlbion.wld", 1); return false; }
    std::string err;
    linkLastSent_ = livelink::sendReload(installPath_, doc_.worldSlot(), link_.heroX, link_.heroY, err);
    if (!linkLastSent_) { pushLog("live link: " + err, 2); return false; }
    pushLog("live link: reloading " + doc_.mapName() + " around the hero", 0);
    return true;
}

bool App::linkPing() {
    std::string err;
    linkLastSent_ = livelink::sendPing(installPath_, err);
    if (!linkLastSent_) { pushLog("live link: " + err, 2); return false; }
    return true;
}

void App::drawLiveLinkCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!installValid_) return;
    linkPoll();
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##livelink", inner);
    theme::label("Live link (ForgeFSE)");
    const bool installed = livelink::isInstalled(installPath_);
    ImGui::PushFont(fontSmall_);
    theme::hint("Talks to the running game through a small Lua thread in ForgeFSE's PartyMode quest: jump the hero to the spot you are looking at (a real region transition when he is elsewhere), spawn the selected creature where it stands, follow him with the camera.");
    if (!installed) ImGui::TextColored(theme::vec(theme::Muted), "Not installed.");
    else if (link_.heartbeatAge < 0 || link_.heartbeatAge > 5.0) ImGui::TextColored(theme::vec(theme::Muted), "%s", link_.ready ? "Installed; the game is not running (or no hero yet)." : "Installed; waiting for the game (start it through FSE_Launcher).");
    else ImGui::TextColored(theme::vec(theme::Text), "Live: hero in %s at (%.0f, %.0f, %.1f)", link_.heroMap.c_str(), link_.heroX, link_.heroY, link_.heroZ);
    if (link_.lastAckId && link_.lastAckId == linkLastSent_) ImGui::TextColored(theme::vec(link_.lastAckOk ? theme::Muted : theme::Warn), "last command: %s", link_.lastAckMessage.c_str());
    ImGui::PopFont();
    const float half = (cardInner - S(6)) * 0.5f;
    if (!installed) {
        if (theme::ghostButton("Install into ForgeFSE", ImVec2(cardInner, S(28)))) linkInstall();
        auto_.registerWidget("btn_link_install");
    } else {
        if (theme::ghostButton("Go here in game", ImVec2(half, S(28)))) linkGoHere();
        auto_.registerWidget("btn_link_go");
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Spawn selected creature", ImVec2(half, S(28)))) linkSpawnSelected();
        auto_.registerWidget("btn_link_spawn");
        ImGui::Checkbox("Camera follows the hero", &linkFollow_);
        auto_.registerWidget("chk_link_follow");
        if (theme::ghostButton("Remove the hook", ImVec2(cardInner, S(24)))) linkRemove();
        auto_.registerWidget("btn_link_remove");
    }
    theme::endCard();
}

bool App::placeFishingSpot(const std::string& reward, const std::string& scriptName) {
    if (!documentLoaded()) { pushLog("editor: no level document", 1); return false; }
    float focus[3]; camera_.focus(focus);
    float pos[3] = {focus[0], -focus[2], focus[1]};
    if (const auto h = doc_.groundHeight(pos[0], pos[1])) pos[2] = *h;
    try {
        const size_t n = doc_.placeFishingSpot(pos, reward, scriptName);
        selectedUid_ = doc_.uidOf(n);
        selectedThing_ = int(n);
        renderer_.selectedThing = selectedThing_;
        pushLog("placed a fishing spot" + (reward.empty() ? std::string() : " (first catch " + reward + ")"), 0);
        setEditTab(2);
        return true;
    } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); return false; }
}

void App::drawFishingSpotCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!documentLoaded()) return;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##fishingspot", inner);
    theme::label("Fishing spot");
    ImGui::PushFont(fontSmall_);
    theme::hint("A MARKER_FISHING_SPOT where the hero can cast a fishing rod (the retail marker; put it on a shore or pier). Optionally name an OBJECT_* def as the first catch there, the way Barrow Fields hands out OBJECT_MOONFISH; leave it empty for the game's normal fish table.");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##fishingreward", "First catch (optional, e.g. OBJECT_MOONFISH)", fishingReward_, sizeof fishingReward_);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_fishingreward");
    if (theme::ghostButton("Place fishing spot at view centre", ImVec2(cardInner, S(30)))) placeFishingSpot(fishingReward_);
    auto_.registerWidget("btn_place_fishing_spot");
    theme::endCard();
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
    drawRuleNotice("spawner", cardInner);
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
    if (newLevelMode_ == 0) theme::hint("Clones the map (current .lev/.tng, terrain chunk translated to the new origin: ground, LOD, water, trees and grass) into the world as a new level owned by an existing region. One-time .forge-orig backups of the .bwd/.wld/.wad/.stb.");
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The region that owns the new map when it does not get its own (existing saves see it at once).");
    theme::toggle("Own region + minimap", &newLevelOwnRegion_);
    auto_.registerWidget("toggle_new_level_own_region");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The level gets its own region: its own name on the map screen and a minimap baked from its\nterrain (appended to textures.big and registered; one-time .forge-orig backups).");
    if (newLevelOwnRegion_) {
        int mode = newLevelDedicated_ ? 0 : 1;
        if (theme::segmented("##ownmode", mode, {"New region slot", "Take over a filler"}, cardInner)) newLevelDedicated_ = mode == 0;
        auto_.registerWidget("seg_new_level_own_mode");
        ImGui::PushFont(fontSmall_);
        if (newLevelDedicated_)
            theme::hint("A brand-new region slot (no engine cap). Saves cache the region table, so start a new game -- or make your save after adding it -- to see it named and drawn.");
        else if (reusableRegions_.size() >= 2)
            theme::hint(("Takes over " + reusableRegions_.front().name + " (slot " + std::to_string(reusableRegions_.front().slot) + ", " + std::to_string(reusableRegions_.front().maps) + " map(s) -> " + reusableRegions_.back().name + "); existing saves see it.").c_str());
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
    if (theme::primaryButton(newLevelFuture_.valid() ? jobLabel("Installing").c_str() : "Create level in the game", ImVec2(cardInner, S(32)), can)) startNewLevel();
    auto_.registerWidget("btn_new_level");
    drawRuleNotice("region", cardInner);
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
    if (gameWriteBlocked("new level")) return;
    const std::string root = saveRoot();
    if (newLevelMode_ == 1) {
        editor::BlankLevelRequest req;
        req.name = newLevelName_;
        req.hostRegion = newLevelRegion_;
        req.worldX = newLevelX_; req.worldY = newLevelY_;
        req.templateLevel = blankTemplate_;
        req.ownRegion.wanted = newLevelOwnRegion_; req.ownRegion.dedicated = newLevelDedicated_; req.ownRegion.displayName = newLevelDisplay_;
        if (blankSize_ >= 0 && blankSize_ < int(blankSizes_.size())) { req.width = blankSizes_[size_t(blankSize_)].width; req.height = blankSizes_[size_t(blankSize_)].height; }
        req.themeSlot = blankTheme_;
        req.groundHeight = blankHeight_;
        const forge::terraintex::ThemeLibrary* lib = ctx_.themeLibrary();
        if (!lib) return;
        pushLog("new level: authoring blank " + req.name + " at (" + std::to_string(req.worldX) + "," + std::to_string(req.worldY) + "), region " + req.hostRegion + "...", 0);
        beginJob(); req.progress = jobProgress();
        newLevelFuture_ = std::async(std::launch::async, [req, root, lib]() {
            NewLevelJob j; j.name = req.name; j.ownRegion = req.ownRegion.wanted && req.ownRegion.dedicated;
            j.ok = editor::createBlankLevel(root, req, *lib, j.result, j.error);
            return j;
        });
        return;
    }
    editor::NewLevelRequest req;
    req.donor = doc_.mapName();
    req.name = newLevelName_;
    req.hostRegion = newLevelRegion_;
    req.ownRegion.wanted = newLevelOwnRegion_; req.ownRegion.dedicated = newLevelDedicated_; req.ownRegion.displayName = newLevelDisplay_;
    req.worldX = newLevelX_; req.worldY = newLevelY_;
    pushLog("new level: cloning " + req.donor + " as " + req.name + " at (" + std::to_string(req.worldX) + "," + std::to_string(req.worldY) + "), region " + req.hostRegion + "...", 0);
    beginJob(); req.progress = jobProgress();
    newLevelFuture_ = std::async(std::launch::async, [req, root]() {
        NewLevelJob j; j.name = req.name; j.ownRegion = req.ownRegion.wanted && req.ownRegion.dedicated;
        j.ok = editor::createLevelFromDonor(root, req, j.result, j.error);
        return j;
    });
}

void App::startTerrainDeploy() {
    if (!documentLoaded() || !doc_.hasTerrain() || terrainDeployFuture_.valid()) return;
    linkPoll(true);
    if (gameWriteBlocked("terrain")) return;
    if (doc_.external() && saveRoot() != installPath_) { pushLog("terrain: this map belongs to another world and writes its own files; a redirected save root does not apply to it", 2); return; }
    if (ctxFuture_.valid()) { pushLog("terrain: textures and themes are still loading (a custom theme was just added); deploy again in a moment", 1); return; }
    if (doc_.strokeActive()) doc_.endStroke();
    editor::Document* doc = &doc_;
    const std::string root = saveRoot();
    const auto ctxHold = std::make_shared<const te::Context>(ctx_);   // the library lives in it; a reload must not free it
    const forge::terraintex::ThemeLibrary* lib = ctxHold->themeLibrary();
    pushLog(std::string("terrain: writing .lev") + (writesLoose() ? "" : ", FinalAlbion.wad") + " and re-baking the FinalAlbion_RT.stb chunk...", 0);
    beginJob();
    const editor::ProgressFn progress = jobProgress();
    terrainDeployFuture_ = std::async(std::launch::async, [ctxHold, doc, root, lib, progress]() {
        TerrainDeployResult r;
        r.ok = doc->deployTerrain(root, r.notes, r.error, lib, progress);
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
        if (ImGui::IsKeyPressed(ImGuiKey_R) && !(gizmoOp_ == 4 && terrainMode_ == 15)) gizmoOp_ = 3;   // R turns the paste there
        if (ImGui::IsKeyPressed(ImGuiKey_T) && doc_.hasTerrain()) gizmoOp_ = 4;
        // the vanilla editor switches modes with the number keys
        if (!io.KeyCtrl) {
            const ImGuiKey tabs[4] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4};
            for (int k = 0; k < 4; ++k)
                if (ImGui::IsKeyPressed(tabs[k]) && (k != 1 || doc_.hasTerrain())) setEditTab(k);
        }
        if (gizmoOp_ == 4 && !io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) brushRadius_ = std::max(1.0f, brushRadius_ - 1.0f);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) brushRadius_ = std::min(60.0f, brushRadius_ + 1.0f);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedThing_ >= 0) deleteSelected();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) editUndo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) editRedo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selectedThing_ >= 0) duplicateSelected();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && selectedThing_ >= 0) copySelection();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) pasteClipboard();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && (linkPick_.active || trackLinkPick_)) { linkPick_.active = false; trackLinkPick_ = false; }
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape) && selectedThing_ >= 0) selectThing(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_End) && selectedThing_ >= 0) snapSelectedToGround();
}

void App::drawGizmo(const ImVec2& origin, const ImVec2& size) {
    if (!editMode_ || !documentLoaded() || selectedThing_ < 0 || gizmoOp_ == 0) { gizmoWasUsing_ = false; return; }
    editor::Frame f;
    if (!frameOfSelected(f)) return;
    if (!gizmoWasUsing_) {
        gizmoFrame_ = f;
        gizmoStart_ = f;
        groupStart_.clear();
        for (const int i : selectionIndices()) {
            if (i == selectedThing_) continue;
            editor::Frame g;
            if (doc_.frameOf(size_t(i), g)) groupStart_.push_back({i, g});
        }
    }
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
            // ImGuizmo's scale is the cumulative ratio since the drag began, so it
            // multiplies the start scale; multiplying the per-frame scale compounded it
            const float rel = nf.scale / 100.0f;
            nf.scale = std::clamp(gizmoStart_.scale * rel, 0.01f, 100.0f);
            if (gizmoOp_ != 3) nf.scale = gizmoFrame_.scale;
            if (gizmoOp_ == 3) { nf.pos[0] = gizmoFrame_.pos[0]; nf.pos[1] = gizmoFrame_.pos[1]; nf.pos[2] = gizmoFrame_.pos[2]; }
            gizmoFrame_ = nf;
            applyFrame(selectedThing_, gizmoFrame_);
            for (const auto& [i, g] : groupStart_) applyFrame(i, groupFrame(g));
        }
    } else if (gizmoWasUsing_) {
        // one undo step per drag, the whole group
        doc_.beginBatch();
        commitFrame(gizmoFrame_);
        for (const auto& [i, g] : groupStart_) {
            try { doc_.setFrame(size_t(i), groupFrame(g)); }
            catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); }
        }
        doc_.endBatch();
    }
    gizmoWasUsing_ = using_;
}

// An extra's frame under the primary's drag: the rigid transform that took the primary
// from its start frame to the current one (about its own pivot, unit scale) applied to
// the extra's start frame; a scale drag multiplies the extra's own scale instead.
editor::Frame App::groupFrame(const editor::Frame& start) const {
    editor::Frame out = start;
    if (gizmoOp_ == 3) { out.scale = std::clamp(start.scale * (gizmoFrame_.scale / std::max(gizmoStart_.scale, 1e-4f)), 0.01f, 100.0f); return out; }
    editor::Frame a = gizmoStart_, b = gizmoFrame_, s = start;
    a.scale = b.scale = s.scale = 1.0f;
    float ma[16], mb[16], ms[16], inv[16], t[16], m[16];
    editor::frameToMatrix(a, ma); editor::frameToMatrix(b, mb); editor::frameToMatrix(s, ms);
    if (!editor::invert(ma, inv)) return out;
    editor::multiply(inv, mb, t);       // start -> now
    editor::multiply(ms, t, m);         // the extra, moved the same way
    if (editor::matrixToFrame(m, out)) out.scale = start.scale;
    return out;
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
        if (theme::primaryButton("Save draft", ImVec2(w, S(32)), doc_.dirty())) { saveDocument(); if (!hasUnsavedEdits()) { const std::string t = pendingSelect_; pendingSelect_.clear(); discardEdits_ = true; selectMap(t); } }
        auto_.registerWidget("btn_unsaved_save");
        ImGui::SameLine(0, S(6));
        if (theme::dangerButton("Discard", ImVec2(w, S(32)))) { const std::string t = pendingSelect_; pendingSelect_.clear(); discardEdits_ = true; selectMap(t); }
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

    // ---- sub-tabs: the tool and the Terrain tab follow each other
    if (gizmoOp_ != lastGizmoOp_) {
        if (gizmoOp_ == 4) editTab_ = 1;
        else if (lastGizmoOp_ == 4 && editTab_ == 1) editTab_ = 0;
        lastGizmoOp_ = gizmoOp_;
    }
    ImGui::SetCursorPosX(pad);
    {
        int tab = editTab_;
        if (theme::segmented("##edittab", tab, {"Objects", "Terrain", "Actors", "Level"}, inner)) setEditTab(tab);
        auto_.registerWidget("seg_edit_tab");
    }
    ImGui::Dummy(ImVec2(0, S(8)));

    // ---- terrain brush
    if (editTab_ == 1 && !doc_.hasTerrain()) {
        ImGui::SetCursorPosX(pad);
        theme::hint("This map has no .lev, so there is no terrain to sculpt or paint.");
    }
    if (editTab_ == 1 && doc_.hasTerrain()) {
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##terrain", inner);
        theme::label("Terrain tool");
        {
            // tools grouped like the vanilla Height / Themes / Survey / Copy-and-paste dialogs:
            // a category, then its tools (each tool keeps its terrainMode_ id)
            struct Tool { int mode; const char* label; };
            static const std::vector<Tool> sculpt = {{0, "Raise"}, {1, "Lower"}, {2, "Flatten"}, {3, "Smooth"}, {9, "Path"}};
            static const std::vector<Tool> paint = {{6, "Ground"}, {7, "Replace"}, {8, "Flood"}, {10, "Environ."}, {11, "Sound"}};
            static const std::vector<Tool> pass = {{4, "Walkable"}, {5, "Blocked"}, {12, "Camera ok"}, {13, "Camera no"}};
            static const std::vector<Tool> region = {{14, "Copy"}, {15, "Paste"}};
            const std::vector<Tool>* groups[4] = {&sculpt, &paint, &pass, &region};
            int cat = 0;
            for (int g = 0; g < 4; ++g)
                for (const auto& t : *groups[g]) if (t.mode == terrainMode_) cat = g;
            if (theme::segmented("##tcat", cat, {"Sculpt", "Paint", "Passability", "Region"}, cardInner) && cat >= 0) {
                bool inside = false;
                for (const auto& t : *groups[cat]) inside = inside || t.mode == terrainMode_;
                if (!inside) terrainMode_ = (*groups[cat])[0].mode;
            }
            auto_.registerWidget("seg_terrain_category");
            std::vector<const char*> labels;
            std::vector<int> modes;
            for (const auto& t : *groups[cat]) {
                if ((t.mode == 10 || t.mode == 11) && !doc_.hasGameMap()) continue;   // older .lev: no game-map grid
                labels.push_back(t.label); modes.push_back(t.mode);
            }
            int pick = -1;
            for (size_t i = 0; i < modes.size(); ++i) if (modes[i] == terrainMode_) pick = int(i);
            if (theme::segmented("##tmode", pick, labels, cardInner) && pick >= 0) terrainMode_ = modes[size_t(pick)];
            auto_.registerWidget("seg_terrain_mode");
        }
        {
            if (terrainMode_ == 14 || terrainMode_ == 15) {
                if (terrainMode_ == 15) {
                    ImGui::Checkbox("Heights##clh", &clipHeights_); ImGui::SameLine();
                    ImGui::Checkbox("Themes##clt", &clipThemes_); ImGui::SameLine();
                    ImGui::Checkbox("Relative##clr", &clipRelative_);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: the copied shape sits on the ground where you click.\nOff: the copied heights are placed as they were.");
                }
                ImGui::PushFont(fontSmall_);
                if (!terrainClip_.empty()) ImGui::TextColored(theme::vec(theme::Muted), "clipboard: %d x %d vertices, turned %d deg", terrainClip_.w, terrainClip_.h, clipTurns_ * 90);
                theme::hint(terrainMode_ == 14 ? "Drag a rectangle on the ground to copy its heights and ground themes (the vanilla Copy and paste dialog). The copy survives switching maps."
                                               : "Click to paste with the copy's first corner there; R turns it 90 degrees. Themes are matched by name (a missing one takes a free palette slot). One undo step per paste.");
                ImGui::PopFont();
            }
        }
        {
            if (terrainMode_ == 12 || terrainMode_ == 13) {
                ImGui::PushFont(fontSmall_);
                theme::hint("Where the camera may pass (the .lev's camera-passability byte; vanilla Survey > Passability). Walkable cells are always camera-passable -- the vanilla saver ORs them -- so this matters on blocked ground: cliffs, walls, water edges.");
                ImGui::PopFont();
            }
        }
        if (terrainMode_ == 10 || terrainMode_ == 11) {
            const auto& atm = doc_.terrain().atmosPalette;
            const auto& snd = doc_.soundThemes();
            auto atmosName = [&](int i) { return i >= 0 && size_t(i) < atm.size() && !atm[size_t(i)].name.empty() ? atm[size_t(i)].name : std::string("(no environment)"); };
            auto soundName = [&](int i) { return i > 0 && size_t(i) <= snd.size() ? snd[size_t(i) - 1] : std::string("(no sound)"); };
            if (brushHit_) if (const auto here = doc_.environmentAndSoundAt(brushFable_[0], brushFable_[1])) {
                ImGui::PushFont(fontSmall_);
                ImGui::TextColored(theme::vec(theme::Muted), "under the cursor: %s, %s", atmosName(here->first).c_str(), soundName(here->second).c_str());
                ImGui::PopFont();
            }
            if (terrainMode_ == 10) {
                theme::label("Environment theme");
                ImGui::SetNextItemWidth(cardInner);
                if (ImGui::BeginCombo("##envslot", atmosName(envSlot_).c_str())) {
                    for (size_t i = 0; i < atm.size(); ++i) {
                        if (i != 0 && atm[i].name.empty()) continue;
                        if (ImGui::Selectable((atmosName(int(i)) + "##env" + std::to_string(i)).c_str(), int(i) == envSlot_)) envSlot_ = int(i);
                    }
                    ImGui::EndCombo();
                }
                auto_.registerWidget("combo_env_slot");
                ImGui::SetNextItemWidth(cardInner);
                ImGui::InputTextWithHint("##envsearch", "Add an environment from the game (UNDERTREES, HAUNTED...)", envSearch_, sizeof envSearch_, ImGuiInputTextFlags_CharsUppercase);
                auto_.registerWidget("input_env_search");
                if (envSearch_[0] && ctx_.ready()) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
                    ImGui::BeginChild("##envlist", ImVec2(cardInner, S(100)), ImGuiChildFlags_None);
                    ImGui::PopStyleColor();
                    ImGui::PushFont(fontSmall_);
                    if (envDefs_.empty()) envDefs_ = ctx_.definitions({"ENVIRONMENT_THEME_DAY"});
                    for (const auto& [name, type] : envDefs_) {
                        if (!contains(name, envSearch_)) continue;
                        if (ImGui::Selectable(name.c_str())) {
                            const auto idx = ctx_.definitionIndex(name);
                            const int slot = idx ? doc_.addEnvironmentTheme(name, *idx) : -1;
                            if (slot >= 0) { envSlot_ = slot; pushLog("environment " + name + " in atmos slot " + std::to_string(slot), 0); }
                            else pushLog("environment: no free atmos slot (or no game-map grid)", 1);
                        }
                    }
                    ImGui::PopFont();
                    ImGui::EndChild();
                }
            } else {
                theme::label("Sound theme");
                ImGui::SetNextItemWidth(cardInner);
                if (ImGui::BeginCombo("##soundidx", soundName(soundIndex_).c_str())) {
                    for (size_t i = 0; i <= snd.size(); ++i)
                        if (ImGui::Selectable((soundName(int(i)) + "##snd" + std::to_string(i)).c_str(), int(i) == soundIndex_)) soundIndex_ = int(i);
                    ImGui::EndCombo();
                }
                auto_.registerWidget("combo_sound");
                ImGui::SetNextItemWidth(cardInner);
                ImGui::InputTextWithHint("##soundsearch", "Add a sound from the game (WOODLAND, OCEAN...)", envSearch_, sizeof envSearch_, ImGuiInputTextFlags_CharsUppercase);
                auto_.registerWidget("input_sound_search");
                if (envSearch_[0] && ctx_.ready()) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
                    ImGui::BeginChild("##soundlist", ImVec2(cardInner, S(100)), ImGuiChildFlags_None);
                    ImGui::PopStyleColor();
                    ImGui::PushFont(fontSmall_);
                    if (soundDefs_.empty()) soundDefs_ = ctx_.definitions({"SOUND_THEME"});
                    for (const auto& [name, type] : soundDefs_) {
                        if (!contains(name, envSearch_)) continue;
                        if (ImGui::Selectable(name.c_str())) {
                            std::string err;
                            const int idx = doc_.addSoundTheme(name, err);
                            if (idx > 0) { soundIndex_ = idx; pushLog("sound " + name + " is index " + std::to_string(idx) + " (saved with the terrain)", 0); }
                            else pushLog("sound: " + err, 1);
                        }
                    }
                    ImGui::PopFont();
                    ImGui::EndChild();
                }
            }
            ImGui::PushFont(fontSmall_);
            theme::hint(terrainMode_ == 10
                ? "Paints the environment theme (an ENVIRONMENT_THEME_DAY def) on the map's 4x4-cell grid, blended like ground themes. Ctrl+click samples it. Saved with the terrain."
                : "Paints the background sound (birds, sea, village) on the map's 4x4-cell grid. The list is the sounds this map names; one added from the game joins it (the .lev after the list is re-laid, every offset shifted). Ctrl+click samples it. Saved with the terrain.");
            ImGui::PopFont();
        }
        if (terrainMode_ == 7 || terrainMode_ == 8) {
            // the vanilla Themes dialog's "Theme to Replace" / "Theme to Place" pair
            theme::label("Replace this theme");
            paletteCombo("##replaceFrom", replaceFrom_, cardInner);
            auto_.registerWidget("combo_replace_from");
            theme::label("with this theme");
            paletteCombo("##replaceTo", paintTheme_, cardInner);
            auto_.registerWidget("combo_replace_to");
            const bool can = replaceFrom_ >= 0 && replaceFrom_ != paintTheme_;
            if (theme::ghostButton("Replace all on this map", ImVec2(cardInner, S(28))) && can) {
                const size_t n = doc_.replaceTheme(uint8_t(replaceFrom_), uint8_t(paintTheme_), editor::Document::ReplaceScope::All);
                pushLog("replace all: " + std::to_string(n) + " cells", n ? 0 : 1);
            }
            auto_.registerWidget("btn_replace_all");
            ImGui::PushFont(fontSmall_);
            theme::hint(terrainMode_ == 7 ? "Hold LMB: under the brush, the first theme becomes the second in every blend slot. Ctrl+click samples the theme to paint, Ctrl+Shift+click the theme to replace. One undo step per stroke."
                                          : "Click the ground: the connected patch holding the first theme (8-neighbour flood, like the vanilla editor) takes the second. Ctrl+click samples the theme to paint, Ctrl+Shift+click the theme to replace. One undo step.");
            ImGui::PopFont();
        }
        if (terrainMode_ == 9) {
            ImGui::PushFont(fontSmall_);
            theme::hint("Drag on the ground from the start of the path to its end and release: every vertex within the radius of the line takes the height interpolated between the ground at the two ends (the vanilla Height Toolbox's Draw Paths). One undo step.");
            ImGui::PopFont();
        }
        if (terrainMode_ == 6) {
            // ground theme picker: the map's LEV palette (slot -> ENGINE_THEME name), named slots
            const forge::lev::File* lev = doc_.level();
            const char* current = "(pick a ground theme)";
            if (lev && paintTheme_ >= 0 && size_t(paintTheme_) < lev->groundThemes().size() && !lev->groundThemes()[size_t(paintTheme_)].name.empty())
                current = lev->groundThemes()[size_t(paintTheme_)].name.c_str();
            if (current[0] != '(') { const ImVec2 at = ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(cardInner, S(28))); themeRow(current, at, S(28), nullptr); ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + S(36), at.y + (S(28) - ImGui::GetTextLineHeight()) * 0.5f), theme::col(theme::Muted), "painting with this theme"); }
            paletteCombo("##paintTheme", paintTheme_, cardInner);
            auto_.registerWidget("combo_paint_theme");
            // any ENGINE_THEME of the game can join the palette (a free slot of the 256)
            ImGui::SetNextItemWidth(cardInner);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
            ImGui::InputTextWithHint("##themesearch", "Add a ground theme from the game (GRASS, COBBLES, SNOW...)", themeSearch_, sizeof themeSearch_);
            ImGui::PopStyleVar();
            auto_.registerWidget("input_themesearch");
            {
                // every ENGINE_THEME of the game: grouped by ENGINE_THEME_GROUP like the vanilla
                // Themes dialog (TG_SNOWSPIRE, TG_HOOKCOAST ...), or the search's matches
                const forge::terraintex::ThemeLibrary* lib = ctx_.themeLibrary();
                if (themeGroupOf_.empty() && ctx_.ready())
                    for (const auto& g : ctx_.groupedDefinitions({"ENGINE_THEME"})) themeGroupOf_[g.name] = g.group;
                ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
                ImGui::BeginChild("##themelist", ImVec2(cardInner, S(themeSearch_[0] ? 110 : 180)), ImGuiChildFlags_None);
                ImGui::PopStyleColor();
                ImGui::PushFont(fontSmall_);
                auto themeLine = [&](const std::string& name) {
                    const int have = doc_.paletteSlotOf(name);
                    char lbl[200]; std::snprintf(lbl, sizeof lbl, have >= 0 ? "%s  (slot %d)" : "%s", name.c_str(), have);
                    const float rowH = S(22);
                    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable((std::string("##lib") + name).c_str(), have >= 0 && have == paintTheme_, 0, ImVec2(0, rowH))) addPaintTheme(name);
                    themeRow(name, rowPos, rowH, lbl);
                };
                int shown = 0;
                if (!lib) ImGui::TextColored(theme::vec(theme::Faint), "ENGINE_THEME library not loaded yet");
                else if (themeSearch_[0]) {
                    for (const auto& th : lib->themes()) {
                        if (!th.decoded || !(contains(th.name, themeSearch_) || contains(themeGroupOf_[th.name], themeSearch_))) continue;
                        themeLine(th.name);
                        if (++shown >= 200) break;
                    }
                    if (!shown) ImGui::TextColored(theme::vec(theme::Faint), "no match");
                } else {
                    std::map<std::string, std::vector<std::string>> byGroup;
                    for (const auto& th : lib->themes())
                        if (th.decoded) {
                            const auto g = themeGroupOf_.find(th.name);
                            byGroup[g == themeGroupOf_.end() || g->second.empty() ? std::string("(no group)") : g->second].push_back(th.name);
                        }
                    for (const auto& [group, names] : byGroup) {
                        char gh[128]; std::snprintf(gh, sizeof gh, "%s  (%zu)##tg%s", group.c_str(), names.size(), group.c_str());
                        if (ImGui::TreeNodeEx(gh, ImGuiTreeNodeFlags_SpanAvailWidth)) {
                            for (const auto& n : names) themeLine(n);
                            ImGui::TreePop();
                        }
                    }
                }
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

        // ---- the vanilla Fractals dialog (CFractalDialog), ported generator
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##fractal", inner);
        if (theme::ghostButton(fractalOpen_ ? "Fractal terrain  (hide)" : "Fractal terrain...", ImVec2(cardInner, S(26)))) fractalOpen_ = !fractalOpen_;
        auto_.registerWidget("btn_fractal_toggle");
        if (fractalOpen_) {
            auto& f = fractal_;
            ImGui::PushItemWidth(cardInner * 0.5f);
            auto field = [&](const char* label, const char* id, double& v, double step, double lo, double hi, const char* fmt) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::vec(theme::Muted), "%s", label);
                ImGui::SameLine(cardInner * 0.5f);
                ImGui::InputDouble(id, &v, step, step * 10.0, fmt);
                v = std::clamp(v, lo, hi);
            };
            field("Lacunarity", "##flac", f.lacunarity, 0.1, 0.001, 100.0, "%.3f");
            field("Fractal dimension", "##fdim", f.dimension, 0.01, 0.001, 10.0, "%.3f");
            field("Octaves", "##foct", f.octaves, 1.0, 1.0, 100.0, "%.1f");
            field("Map pos X", "##fmx", f.mapX, 250.0, -1e6, 1e6, "%.0f");
            field("Map pos Y", "##fmy", f.mapY, 250.0, -1e6, 1e6, "%.0f");
            field("World scaler", "##fws", f.worldScaler, 0.05, 0.001, 10.0, "%.3f");
            field("Scale (height)", "##fsc", f.scale, 5.0, 0.001, 2048.0, "%.1f");
            ImGui::Checkbox("Use falloff##ffo", &f.useFalloff);
            if (f.useFalloff) {
                field("Start falloff", "##fst", f.startFalloff, 100.0, 0.0, 10000.0, "%.0f");
                field("End falloff", "##fen", f.endFalloff, 100.0, 0.0, 10000.0, "%.0f");
                if (f.endFalloff <= f.startFalloff) f.endFalloff = f.startFalloff + 1.0;   // vanilla keeps start < end
            }
            ImGui::PopItemWidth();
            // preview: the fractal over this map (world coordinates), 128 x 128, redrawn when a field changes
            {
                char key[256];
                std::snprintf(key, sizeof key, "%s|%g|%g|%g|%g|%g|%g|%d|%g|%g|%d|%d", doc_.mapName().c_str(), f.lacunarity, f.dimension, f.octaves, f.mapX, f.mapY,
                              f.worldScaler, int(f.useFalloff), f.startFalloff, f.endFalloff, doc_.worldX(), doc_.worldY());
                if (fractalPreviewKey_ != key) {
                    fractalPreviewKey_ = key;
                    const forge::fractal::Generator gen(f);
                    terrainexport::Image img;
                    img.width = img.height = 128;
                    img.rgba.resize(128 * 128 * 4);
                    const float sx = float(doc_.cellsX() - 1) / 127.0f, sy = float(doc_.cellsY() - 1) / 127.0f;
                    for (int y = 0; y < 128; ++y)
                        for (int x = 0; x < 128; ++x) {
                            const float h = gen.heightAt(double(doc_.worldX()) + x * sx, double(doc_.worldY()) + y * sy);
                            const uint8_t g = uint8_t(std::clamp(h, 0.0f, 1.0f) * 255.0f);
                            uint8_t* px = &img.rgba[size_t(y * 128 + x) * 4];
                            px[0] = g; px[1] = g; px[2] = g; px[3] = 255;
                        }
                    fractalPreview_ = renderer_.uiTexture("fractal", img);
                }
                if (fractalPreview_) {
                    const float side = std::min(cardInner, S(160));
                    ImGui::Image((ImTextureID)(intptr_t)fractalPreview_, ImVec2(side, side));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The fractal over this map, dark = 0, white = Scale. Apply sets the ground to it.");
                }
            }
            if (theme::ghostButton("Apply fractal to this map", ImVec2(cardInner, S(28)))) {
                const size_t n = doc_.applyFractal(f);
                pushLog("fractal: " + std::to_string(n) + " vertices set (one undo step)", n ? 0 : 1);
            }
            auto_.registerWidget("btn_fractal_apply");
            if (theme::ghostButton("Vanilla defaults", ImVec2(cardInner, S(24)))) f = forge::fractal::Params{};
            ImGui::PushFont(fontSmall_);
            theme::hint("The vanilla editor's generator, ported from its code: a hybrid multifractal over Perlin noise sampled at WORLD positions, so neighbouring maps done one after another meet at their seams. It SETS every height to fractal x Scale world units (it does not add); retail ground spans about 0..70, the vanilla default Scale is 1000. Falloff fades to 0 away from the world centre (2048, 2048). One undo step.");
            ImGui::PopFont();
        }
        theme::endCard();
        ImGui::Dummy(ImVec2(0, S(8)));
    }

    if (editTab_ == 0 || editTab_ == 2) { drawSectionsCard(pad, inner, cardInner); ImGui::Dummy(ImVec2(0, S(8))); }

    // ---- selection (Objects and Actors: both tabs place things)
    if (editTab_ == 0 || editTab_ == 2) {
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
        const size_t nSel = selectionCount();
        ImGui::PushFont(fontBold_);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
        ImGui::TextUnformatted(s.definition.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        if (const char* o = originOf(s.uid)) {
            ImGui::PushFont(fontSmall_);
            ImGui::TextColored(theme::vec(theme::Accent), "placed or changed by %s", o);
            ImGui::PopFont();
            ImGui::SameLine();
            if (theme::ghostButton("Back to retail", ImVec2(S(110), S(22)))) {
                setModPick("tng:FinalAlbion/" + doc_.mapName() + ".tng|uid:" + std::to_string(s.uid), "vanilla");
                pushLog("mods: " + s.definition + " picked back to retail (forge_mods_picks.txt; deploy again from the Mods tab)", 0);
            }
            auto_.registerWidget("btn_thing_retail");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes a vanilla pick for this thing; the next Build and deploy on the Mods tab leaves it as retail has it.");
        }
        if (nSel > 1) {
            ImGui::PushFont(fontSmall_);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
            ImGui::TextColored(theme::vec(theme::Accent), "+ %zu more selected: the gizmo moves them together; Del, Ctrl+D and Ctrl+C act on all of them.", nSel - 1);
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
        }
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
        if (theme::dangerButton("Delete  (Del)", ImVec2(half, S(28)))) deleteSelected();
        auto_.registerWidget("btn_delete");
        // village membership: buildings, markers and creatures belong to a Village thing by uid
        if (s.type != "Village") {
            const auto vills = doc_.villages();
            if (!vills.empty()) {
                const uint64_t mine = doc_.villageOf(size_t(selectedThing_));
                std::string cur = "none";
                for (const auto& v : vills) if (v.uid == mine) cur = v.scriptName.empty() ? v.definition : v.scriptName;
                ImGui::Dummy(ImVec2(0, S(4)));
                theme::label("Village");
                ImGui::SetNextItemWidth(cardInner);
                if (ImGui::BeginCombo("##village", cur.c_str())) {
                    if (ImGui::Selectable("none", mine == 0)) setSelectedVillage(0);
                    for (const auto& v : vills) {
                        const std::string lbl = (v.scriptName.empty() ? v.definition : v.scriptName) + "  (uid " + std::to_string(v.uid) + ")";
                        if (ImGui::Selectable(lbl.c_str(), v.uid == mine)) setSelectedVillage(v.uid);
                    }
                    ImGui::EndCombo();
                }
                auto_.registerWidget("combo_village");
            }
        }
        // the other links (owner, home, work, exit -> entrance, trigger -> receptor ...)
        const auto links = doc_.linksOf(size_t(selectedThing_));
        bool header = false;
        for (const auto& l : links) {
            if (l.field == "VillageUID") continue;   // the combo above
            if (!header) { ImGui::Dummy(ImVec2(0, S(4))); theme::label("Links"); header = true; }
            ImGui::PushID(l.field.c_str());
            const bool picking = linkPick_.active && linkPick_.field == l.field && linkPick_.ctc == l.ctc;
            const std::string target = picking ? std::string("click a thing in the view  (Esc cancels)")
                                     : l.target == 0 ? std::string("none")
                                     : l.targetIndex ? thingLabel(*l.targetIndex)
                                     : "uid " + std::to_string(l.target) + " (not on this map)";
            const float btn = S(44), x = S(22);
            ImGui::TextColored(theme::vec(theme::Muted), "%s", l.label.c_str());
            ImGui::PushFont(fontSmall_);
            if (ImGui::Selectable(target.c_str(), false, 0, ImVec2(cardInner - btn - x - S(12), 0)) && l.targetIndex) selectThing(int(*l.targetIndex));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s%s%s)\nwants %s%s", l.label.c_str(), l.ctc.empty() ? "" : l.ctc.c_str(), l.ctc.empty() ? "" : ".", l.field.c_str(), l.wants.c_str(), l.targetIndex ? "\nclick to select the target" : "");
            ImGui::PopFont();
            ImGui::SameLine(cardInner - btn - x - S(4));
            if (theme::ghostButton(picking ? "..." : "Pick", ImVec2(btn, S(22)))) { linkPick_ = {l.ctc, l.field, l.label, !picking}; }
            auto_.registerWidget(("btn_link_pick_" + l.field).c_str());
            ImGui::SameLine(0, S(4));
            if (theme::ghostButton("x", ImVec2(x, S(22))) && l.target) {
                doc_.setLink(size_t(selectedThing_), l.ctc, l.field, 0);
                pushLog("link: " + l.label + " cleared", 0);
            }
            ImGui::PopID();
        }
        drawPropertyGrid(cardInner);
    }
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));
    }

    // ---- objects in this map
    if (editTab_ == 0) {
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##objs", inner);
    theme::label("Objects in this map");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##thingsearch", "Filter by definition or script name", thingSearch_, sizeof thingSearch_);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_thingsearch");
    if (!originMods_.empty()) {   // a mod deploy is on this install: filter the list by who placed what
        ImGui::SetNextItemWidth(cardInner);
        const std::string shown = originFilter_.empty() ? "Placed by: everyone (retail + mods)" : originFilter_ == "retail" ? "Placed by: retail only" : "Placed by: " + originFilter_;
        if (ImGui::BeginCombo("##originfilter", shown.c_str())) {
            if (ImGui::Selectable("everyone (retail + mods)", originFilter_.empty())) originFilter_.clear();
            if (ImGui::Selectable("retail only", originFilter_ == "retail")) originFilter_ = "retail";
            for (const auto& m : originMods_) if (ImGui::Selectable(m.c_str(), originFilter_ == m)) originFilter_ = m;
            ImGui::EndCombo();
        }
        auto_.registerWidget("combo_origin");
    }
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
        if (!originFilter_.empty()) {
            const char* o = originOf(s.uid);
            if (originFilter_ == "retail" ? o != nullptr : (!o || originFilter_ != o)) continue;
        }
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
            const char* origin = originOf(s.uid);
            if (origin) {   // leave the badge its room: ellipsise the label
                const float room = cardInner - ImGui::CalcTextSize(origin).x - S(28);
                if (ImGui::CalcTextSize(labelText.c_str()).x > room) {
                    while (labelText.size() > 4 && ImGui::CalcTextSize((labelText + "...").c_str()).x > room) labelText.pop_back();
                    labelText += "...";
                }
            }
            labelText += "##t" + std::to_string(i);
            const bool inSel = i == selectedThing_ || std::find(renderer_.alsoSelected.begin(), renderer_.alsoSelected.end(), i) != renderer_.alsoSelected.end();
            if (ImGui::Selectable(labelText.c_str(), inSel)) { if (ImGui::GetIO().KeyCtrl) toggleSelect(i); else selectThing(i); }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { selectThing(i); frameSelected(); }
            if (origin) {   // the mod badge, right-aligned on the row
                const float w = ImGui::CalcTextSize(origin).x;
                ImGui::SameLine(std::max(0.0f, cardInner - w - S(12)));
                ImGui::TextColored(theme::vec(theme::Accent), "%s", origin);
            }
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
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##defsearch", "Search definitions (OBJECT_..., BUILDING_..., CREATURE_...)", defSearch_, sizeof defSearch_);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_defsearch");
    drawDefPalette("##deflist", {"OBJECT", "BUILDING", "CREATURE"}, cardInner, S(defSearch_[0] ? 180 : 240));
    const std::string placeLabel = placeDef_.empty() ? "Place at view centre" : "Place " + placeDef_;
    if (theme::ghostButton(placeLabel.c_str(), ImVec2(cardInner, S(30))) && !placeDef_.empty()) placeDefinition(placeDef_);
    auto_.registerWidget("btn_place");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Placed where the camera looks, dropped onto the terrain, facing the camera.");
    drawRuleNotice("creature", cardInner);
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

    // ---- import model
    pollMeshImport();
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##importmodel", inner);
    theme::label("Import model");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##meshmodel", "A .glb, .gltf or .obj (Y up, 1 unit = 1 metre)", meshModelPath_, sizeof meshModelPath_);
    auto_.registerWidget("input_mesh_model");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##meshname", "Name (becomes OBJECT_<NAME>)", meshName_, sizeof meshName_);
    auto_.registerWidget("input_mesh_name");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##meshtex", "Diffuse texture PNG (optional)", meshTexturePng_, sizeof meshTexturePng_);
    auto_.registerWidget("input_mesh_texture");
    ImGui::PopStyleVar();
    const bool meshBusy = meshImportFuture_.valid();
    const bool meshCan = meshModelPath_[0] && meshName_[0] && !meshBusy && !ctxFuture_.valid();
    if (theme::ghostButton(meshBusy ? "Importing..." : "Import into the game", ImVec2(cardInner, S(28))) && meshCan) {
        std::string nm = meshName_;
        for (auto& c : nm) { c = char(std::toupper(static_cast<unsigned char>(c))); if (!std::isalnum(static_cast<unsigned char>(c))) c = '_'; }
        importMesh(meshModelPath_, nm, meshTexturePng_);
    }
    auto_.registerWidget("btn_mesh_import");
    ImGui::PushFont(fontSmall_);
    theme::hint("The model becomes MESH_<NAME> in graphics.big, the PNG <NAME>_DIFFUSE in textures.big and OBJECT_<NAME> in game.bin (a copy of the barrel's def with the new mesh); nothing retail is replaced, one-time backups. A collision hull is written from the model's own triangles (EgoCore's 3DMF physics entry). It then shows under Add an object. Not yet seen in-game.");
    ImGui::PopFont();
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));
    }

    if (editTab_ == 2) {
        // creatures by their GroupDef (G_CREATURES_BANDIT, _FAE, _HOSTILE ...), like the vanilla Things tree
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##actordefs", inner);
        theme::label("Creatures by group");
        ImGui::SetNextItemWidth(cardInner);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
        ImGui::InputTextWithHint("##actorsearch", "Search creatures (CREATURE_...)", defSearch_, sizeof defSearch_);
        ImGui::PopStyleVar();
        auto_.registerWidget("input_actorsearch");
        drawDefPalette("##actorlist", {"CREATURE"}, cardInner, S(260));
        const std::string actorLabel = placeDef_.rfind("CREATURE_", 0) == 0 ? "Place " + placeDef_ : std::string("Pick a creature above");
        if (theme::ghostButton(actorLabel.c_str(), ImVec2(cardInner, S(30))) && placeDef_.rfind("CREATURE_", 0) == 0) placeDefinition(placeDef_);
        auto_.registerWidget("btn_place_actor");
        drawRuleNotice("creature", cardInner);
        theme::endCard();
        ImGui::Dummy(ImVec2(0, S(8)));
        drawPresetsCard(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(8)));
        drawVillageCard(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(8)));
        drawSpawnerCard(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(8)));
        drawFishingSpotCard(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(8)));
        drawEffectsCard(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(8)));
        drawLiveLinkCard(pad, inner, cardInner);
    }
    if (editTab_ == 3) {
        drawTracksCard(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(8)));
        drawNewLevelCard(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(8)));
        drawEntranceCard(pad, inner, cardInner);
    }

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
            theme::primaryButton(jobLabel("Saving terrain").c_str(), ImVec2(inner, S(36)), false);
        } else if (!confirmTerrainDeploy_) {
            if (theme::primaryButton("Write terrain into the game", ImVec2(inner, S(36)))) confirmTerrainDeploy_ = true;
            auto_.registerWidget("btn_terrain_deploy");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", writesLoose()
                ? "Writes the loose .lev (this install has no FinalAlbion.wad, so the game reads it) and re-bakes this map's\nterrain chunk inside FinalAlbion_RT.stb from the edited heights (same size, patched in place).\nOne-time .forge-orig backups of both files."
                : "Writes the loose .lev, replaces it in FinalAlbion.wad and re-bakes this map's\nterrain chunk inside FinalAlbion_RT.stb from the edited heights (same size, patched in place).\nOne-time .forge-orig backups of all three files.");
        } else {
            const std::string q = "Rewrite " + doc_.mapName() + "'s terrain in the .lev" + (writesLoose() ? "" : ", FinalAlbion.wad") + " and FinalAlbion_RT.stb? (one-time .forge-orig backups)";
            const int r = confirmRow(q.c_str(), "Yes, write it", inner, S(36), "btn_terrain_deploy_confirm");
            if (r != 0) confirmTerrainDeploy_ = false;
            if (r > 0) startTerrainDeploy();
        }
    }
    // The WAD write is the primary action: the engine only ever reads the archive. The
    // loose .tng is the editor's working copy, so it is a "draft" (0.15 #4).
    ImGui::SetCursorPosX(pad);
    const float half = (inner - S(6)) * 0.5f;
    if (!confirmDeploy_) {
        if (theme::primaryButton(writesLoose() ? "Write into the game (loose .tng)" : "Write into FinalAlbion.wad", ImVec2(inner, S(42)))) confirmDeploy_ = true;
        auto_.registerWidget("btn_deploy");
        if (ImGui::IsItemHovered()) {
            if (writesLoose()) ImGui::SetTooltip("This install has no FinalAlbion.wad: the game reads the loose files in data/Levels/FinalAlbion,\nso this writes %s.tng there. The original file is backed up once as .forge-orig.", doc_.mapName().c_str());
            else ImGui::SetTooltip("The game loads levels from the WAD, so this is what makes the edit show up in-game.\nThe original archive is backed up once as FinalAlbion.wad.forge-orig.");
        }
    } else {
        const std::string q = writesLoose()
            ? "Write " + doc_.mapName() + ".tng into data/Levels/FinalAlbion? The game reads it on the next visit (one-time .forge-orig backup)."
            : "Replace " + doc_.mapName() + ".tng inside FinalAlbion.wad? The game reads it on the next visit (one-time .forge-orig backup).";
        const int r = confirmRow(q.c_str(), "Yes, write it", inner, S(42), "btn_deploy_confirm");
        if (r != 0) confirmDeploy_ = false;
        if (r > 0) deployDocument();
    }
    ImGui::SetCursorPosX(pad);
    if (theme::ghostButton(dirty ? "Save draft" : "Draft saved", ImVec2(dirty ? half : inner, S(32))) && dirty) saveDocument();
    auto_.registerWidget("btn_save");
    if (ImGui::IsItemHovered()) {
        if (writesLoose()) ImGui::SetTooltip("Writes data/Levels/FinalAlbion/%s.tng. This install has no FinalAlbion.wad, so the game READS this file:\na draft saved here is live on the next visit. A one-time backup of any existing file is kept as .forge-orig.", doc_.mapName().c_str());
        else ImGui::SetTooltip("Keeps a working copy as data/Levels/FinalAlbion/%s.tng (the game never reads it; FableForge reopens it).\nA one-time backup of any existing file is kept as .forge-orig.", doc_.mapName().c_str());
    }
    if (dirty) {
        ImGui::SameLine(0, S(6));
        if (theme::dangerButton("Revert all", ImVec2(half, S(32)))) revertDocument();
        auto_.registerWidget("btn_revert");
    }
}

// ---- region entrance --------------------------------------------------------------------
std::optional<editor::RegionEntrance> App::currentEntrance() const {
    if (!documentLoaded() || !doc_.worldSlot()) return std::nullopt;
    std::string err;
    return editor::entranceOf(saveRoot(), doc_.worldSlot(), err);
}

bool App::setEntranceHere() {
    if (!documentLoaded()) { pushLog("entrance: no level document", 1); return false; }
    if (!doc_.worldSlot()) { pushLog("entrance: " + doc_.mapName() + " is not placed in FinalAlbion.wld", 1); return false; }
    if (gameWriteBlocked("entrance")) return false;
    float focus[3]; camera_.focus(focus);
    float pos[3] = {focus[0], -focus[2], focus[1]};
    if (const auto h = doc_.groundHeight(pos[0], pos[1])) pos[2] = *h;
    float d[3]; camera_.dir(d);
    const float fwd[2] = {-d[0], d[2]};   // face the camera, like a placed object
    std::vector<std::string> notes; std::string err;
    if (!editor::setRegionEntrance(saveRoot(), doc_.worldSlot(), doc_.mapName(), pos, fwd, notes, err)) { pushLog("entrance: " + err, 2); return false; }
    for (const auto& n : notes) pushLog(n, 3);
    raiseRule("region");
    return true;
}

void App::drawEntranceCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!documentLoaded()) return;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##entrance", inner);
    theme::label("Region entrance");
    ImGui::PushFont(fontSmall_);
    theme::hint("Where the map screen and quest teleports put the hero when he travels to this map (FinalAlbion.gtg, a REGION_ENTRANCE_POINT + a <Map>HSP start). A level installed with its own region gets one at its centre; move it here to choose the spot.");
    const auto e = currentEntrance();
    if (!doc_.worldSlot()) ImGui::TextColored(theme::vec(theme::Warn), "This map is not placed in FinalAlbion.wld.");
    else if (e) ImGui::TextColored(theme::vec(theme::Muted), "Slot %d: entrance at %.1f, %.1f (h %.1f)%s%s", doc_.worldSlot(), e->pos[0], e->pos[1], e->pos[2], e->startScript.empty() ? "" : "   start ", e->startScript.c_str());
    else ImGui::TextColored(theme::vec(theme::Warn), "Slot %d has no region entrance yet: the map screen cannot travel here.", doc_.worldSlot());
    ImGui::PopFont();
    if (theme::ghostButton(e ? "Move the entrance to the view centre" : "Set the entrance at the view centre", ImVec2(cardInner, S(28))) && doc_.worldSlot()) setEntranceHere();
    auto_.registerWidget("btn_set_entrance");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes FinalAlbion.gtg (one-time .forge-orig backup). Retail entrances are left in place; a second one is added for this map.");
    theme::endCard();
}

// ---- particle emitters -----------------------------------------------------------------
bool App::placeEmitter(const std::string& effectName, const std::string& scriptName) {
    if (!documentLoaded()) { pushLog("editor: no level document", 1); return false; }
    if (effectName.empty()) { pushLog("editor: pick an effect first", 1); return false; }
    float focus[3]; camera_.focus(focus);
    float pos[3] = {focus[0], -focus[2], focus[1]};
    if (const auto h = doc_.groundHeight(pos[0], pos[1])) pos[2] = *h + 0.5f;   // half a unit up, where retail puts most flames
    try {
        const size_t n = doc_.placeEmitter(pos, effectName, scriptName);
        selectedUid_ = doc_.uidOf(n);
        selectedThing_ = int(n);
        renderer_.selectedThing = selectedThing_;
        extraUids_.clear(); syncExtraSelection();
        setEditTab(2);
        pushLog("placed a particle emitter playing " + effectName + " (the preview shows a tinted proxy when Objects are on; the real effect needs the game)", 0);
        return true;
    } catch (const std::exception& e) { pushLog(std::string("editor: ") + e.what(), 2); return false; }
}

void App::drawEffectsCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!effectsLoaded_) {
        std::string err;
        if (effects::bankOpen() || effects::openBank(installPath_, err)) effectNames_ = effects::entryNames();   // read-only: the install, not a scratch save root
        else pushLog("effects: " + err, 1);
        effectsLoaded_ = true;
    }
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##effects", inner);
    theme::label("Particle effect");
    ImGui::PushFont(fontSmall_);
    theme::hint("A PARTICLE_EMITTER_PLACEABLE thing playing one of the game's effects (effects.big: fires, smoke, butterflies, sparkles...). Placed half a unit above the ground at the view centre.");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##effectsearch", "Search effects (FIRE, SMOKE, BUTTERFLY...)", effectSearch_, sizeof effectSearch_, ImGuiInputTextFlags_CharsUppercase);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_effectsearch");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
    ImGui::BeginChild("##effectlist", ImVec2(cardInner, S(110)), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    ImGui::PushFont(fontSmall_);
    std::vector<const std::string*> rows;
    for (const auto& n : effectNames_) if (!effectSearch_[0] || n.find(effectSearch_) != std::string::npos) rows.push_back(&n);
    if (effectNames_.empty()) ImGui::TextColored(theme::vec(theme::Faint), "effects.big not loaded");
    else if (rows.empty()) ImGui::TextColored(theme::vec(theme::Faint), "no match");
    ImGuiListClipper clipper;
    clipper.Begin(int(rows.size()));
    while (clipper.Step())
        for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k)
            if (ImGui::Selectable(rows[size_t(k)]->c_str(), *rows[size_t(k)] == effectPick_)) effectPick_ = *rows[size_t(k)];
    ImGui::PopFont();
    ImGui::EndChild();
    auto_.registerWidget("list_effects");
    const std::string lbl = effectPick_.empty() ? "Place an emitter at view centre" : "Place " + effectPick_ + " at view centre";
    if (theme::ghostButton(lbl.c_str(), ImVec2(cardInner, S(28))) && !effectPick_.empty()) placeEmitter(effectPick_);
    auto_.registerWidget("btn_place_emitter");
    theme::endCard();
}

// ---- presets --------------------------------------------------------------------------
std::vector<std::filesystem::path> App::presetFolders() const {
    std::vector<std::filesystem::path> v;
    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) v.push_back(std::filesystem::path(exe).parent_path() / "presets");
    v.push_back(std::filesystem::path(settingsPath()).parent_path() / "presets");
    return v;
}

void App::refreshPresets() {
    presets_ = editor::listPresets(presetFolders());
    presetsLoaded_ = true;
}

bool App::placePreset(const std::string& name) {
    if (!documentLoaded()) { pushLog("preset: no level document", 1); return false; }
    if (!presetsLoaded_) refreshPresets();
    const editor::PresetInfo* hit = nullptr;
    for (const auto& p : presets_) if (p.name == name || p.file.filename().string() == name) { hit = &p; break; }
    if (!hit) { pushLog("preset: no preset named " + name, 1); return false; }
    editor::Document::Fragment frag;
    std::string err;
    if (!editor::loadPreset(hit->file, frag, err)) { pushLog("preset: " + err, 2); return false; }
    float focus[3]; camera_.focus(focus);
    const float at[3] = {focus[0], -focus[2], focus[1]};
    try {
        const auto placed = doc_.paste(frag, at, true);
        if (placed.empty()) return false;
        extraUids_.clear();
        selectedThing_ = int(placed.front()); selectedUid_ = doc_.uidOf(placed.front()); renderer_.selectedThing = selectedThing_;
        for (size_t i = 1; i < placed.size(); ++i) extraUids_.push_back(doc_.uidOf(placed[i]));
        syncExtraSelection();
        pushLog("placed preset " + hit->name + " (" + std::to_string(placed.size()) + " objects) at the view centre; they stay selected, drag the gizmo to move them together", 0);
        return true;
    } catch (const std::exception& e) { pushLog(std::string("preset: ") + e.what(), 2); return false; }
}

bool App::savePresetFromSelection(const std::string& name, const std::string& description) {
    const auto sel = selectionIndices();
    if (sel.empty()) { pushLog("preset: select the objects to save first", 1); return false; }
    if (name.empty()) { pushLog("preset: give it a name", 1); return false; }
    std::vector<size_t> idx(sel.begin(), sel.end());
    const auto frag = doc_.extract(idx);
    const auto folders = presetFolders();
    const std::filesystem::path file = folders.back() / (editor::presetSlug(name) + ".preset.tng");
    std::string err;
    if (!editor::savePreset(file, name, description, frag, err)) { pushLog("preset: " + err, 2); return false; }
    refreshPresets();
    pushLog("saved preset " + name + " (" + std::to_string(sel.size()) + " objects) to " + file.string(), 3);
    return true;
}

void App::drawPresetsCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!presetsLoaded_) refreshPresets();
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##presets", inner);
    theme::label("Presets");
    ImGui::PushFont(fontSmall_);
    theme::hint("A saved group of objects placed with one click at the view centre (positions kept relative, fresh UIDs, dropped on the ground). Shipped ones come from retail maps; yours go to %APPDATA%\\FableForge\\presets.");
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
    ImGui::BeginChild("##presetlist", ImVec2(cardInner, S(std::min(150.0f, 26.0f * float(std::max<size_t>(presets_.size(), 1)) + 8.0f))), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    ImGui::PushFont(fontSmall_);
    if (presets_.empty()) ImGui::TextColored(theme::vec(theme::Faint), "No presets found (presets/ next to the exe, or your own folder).");
    for (const auto& p : presets_) {
        char lbl[200]; std::snprintf(lbl, sizeof lbl, "%s   (%zu)%s##%s", p.name.c_str(), p.things, p.user ? "  *" : "", p.file.string().c_str());
        if (ImGui::Selectable(lbl, false, 0, ImVec2(0, S(22)))) placePreset(p.name);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nClick to place at the view centre.%s", p.description.c_str(), p.user ? "\n* your own preset" : "");
    }
    ImGui::PopFont();
    ImGui::EndChild();
    auto_.registerWidget("list_presets");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##presetname", "Name for the selection as a preset", presetName_, sizeof presetName_);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_preset_name");
    const size_t n = selectionCount();
    char btn[96]; std::snprintf(btn, sizeof btn, n ? "Save %zu selected object%s as a preset" : "Save selection as a preset", n, n == 1 ? "" : "s");
    if (theme::ghostButton(btn, ImVec2(cardInner, S(28))) && n && presetName_[0]) { if (savePresetFromSelection(presetName_, "")) presetName_[0] = 0; }
    auto_.registerWidget("btn_preset_save");
    theme::endCard();
}

// ---- object thumbnails ----------------------------------------------------------------
ID3D11ShaderResourceView* App::defThumbnail(const std::string& def, bool& pending) {
    pending = false;
    auto it = defThumbs_.find(def);
    if (it != defThumbs_.end()) return it->second;
    if (!ctx_.ready()) { pending = true; return nullptr; }
    if (thumbBudget_ <= 0) { pending = true; return nullptr; }   // one decode per frame
    --thumbBudget_;
    uint32_t modelId = 0;
    const int code = ctx_.graphicModelId(def, modelId);
    if (code <= 0 || !modelId) { defThumbs_[def] = nullptr; return nullptr; }
    if (!thumbBankOpen_) {
        std::string err;
        std::filesystem::path graphics = std::filesystem::path(installPath_) / "data" / "graphics" / "graphics.big";
        if (!std::filesystem::exists(graphics)) graphics = std::filesystem::path(installPath_) / "data" / "graphics" / "pc" / "graphics.big";
        thumbBankOpen_ = foliageexport::openMeshBank(graphics, err);
        if (!thumbBankOpen_) { pushLog("thumbnails: " + err, 1); defThumbs_[def] = nullptr; return nullptr; }
    }
    std::string merr;
    const auto* geo = foliageexport::cachedMesh(modelId, merr);
    if (!geo) { defThumbs_[def] = nullptr; return nullptr; }
    std::vector<std::string> warnings;
    const foliageexport::Mesh m = foliageexport::makeMesh(modelId, foliageexport::meshName(modelId), def, *geo, true, ctx_, thumbImages_, thumbTextureToImage_, warnings);
    ID3D11ShaderResourceView* srv = renderer_.thumbnail(def, m, thumbImages_, 96);
    defThumbs_[def] = srv;
    return srv;
}

// ---- theme swatches -----------------------------------------------------------------
ID3D11ShaderResourceView* App::themeSwatch(const std::string& themeName) {
    if (!ctx_.ready()) return nullptr;
    const forge::terraintex::ThemeLibrary* lib = ctx_.themeLibrary();
    const auto* th = lib ? lib->byName(themeName) : nullptr;
    if (!th || !th->textures.base[0]) return nullptr;
    std::string warning;
    const terrainexport::Image* img = ctx_.texture(th->textures.base[0], warning);
    if (!img) return nullptr;
    return renderer_.swatch(th->textures.base[0], *img);
}

void App::themeRow(const std::string& themeName, const ImVec2& p, float size, const char* label) {
    using theme::S;
    ID3D11ShaderResourceView* srv = themeSwatch(themeName);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (srv) dl->AddImageRounded((ImTextureID)(intptr_t)srv, p, ImVec2(p.x + size, p.y + size), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, S(4));
    else dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), theme::col(theme::Bg3), S(4));
    if (label) dl->AddText(ImVec2(p.x + size + S(8), p.y + (size - ImGui::GetTextLineHeight()) * 0.5f), theme::col(theme::Text), label);
}

// ---- engine-rule notices -------------------------------------------------------------
// The rules the engine imposes are listed once in the Setup panel; these repeat the one
// that applies right where the user just acted, so it is read at the moment it matters.
namespace {
const char* ruleText(const std::string& key) {
    if (key == "creature") return "Engine rule: existing saves will not show this creature. It appears in a new game, or on the first visit to this map in a save that has never loaded it.";
    if (key == "spawner")  return "Engine rule: enemy spawners only run once the hero is past childhood, and existing saves will not show this one. Test from a fresh game with an adult hero.";
    if (key == "region")   return "Engine rule: saves cache the region table, so this region is only named and drawn in a game started after it was added (or a save made after adding it).";
    return "";
}
} // namespace

void App::raiseRule(const std::string& key) {
    if (rulesDismissed_.count(key)) return;
    ruleKey_ = key;
}

void App::drawRuleNotice(const char* key, float width) {
    using theme::S;
    if (ruleKey_ != key) return;
    const char* text = ruleText(key);
    if (!*text) { ruleKey_.clear(); return; }
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::AccentSoft));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(8)));
    ImGui::BeginChild((std::string("##rule_") + key).c_str(), ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(fontSmall_);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - S(20));
    ImGui::TextColored(theme::vec(theme::Text), "%s", text);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(2)));
    if (theme::ghostButton("Got it", ImVec2(S(90), S(24)))) dismissRule(key);
    auto_.registerWidget((std::string("btn_rule_") + key).c_str());
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace albion::gui
