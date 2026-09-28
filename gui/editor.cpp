// FableForge editor: selection, gizmo, edit panel and document plumbing.
// The App methods that make the viewer an editor live here; app.cpp keeps the
// layout, explorer, export and automation.

#include <chrono>
#include "app.hpp"
#include "vanilla_props.hpp"
#include "modpack.hpp"

#include "meshimport.hpp"

#include "nlohmann/json.hpp"
#include "effects.hpp"
#include "forge/heightpen.hpp"

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
    // "<quest>%DayOnly" / "%NightOnly" rows fold into their quest (the vanilla dialog lists quests;
    // the Day only / Night only toggles below decide whether the variants are drawn)
    std::vector<std::string> shown;
    std::map<std::string, size_t> counts, dayCounts, nightCounts;
    bool anyDayNight = false;
    for (const auto& n : sectionNamesCache_) {
        const auto split = editor::Document::splitDayNight(n);
        const std::string base = split.first.empty() ? std::string("NULL") : split.first;
        if (std::none_of(shown.begin(), shown.end(), [&](const std::string& x) { return lowerCopy(x) == lowerCopy(base); })) shown.push_back(base);
        const size_t c = sectionCountsCache_.count(lowerCopy(n)) ? sectionCountsCache_[lowerCopy(n)] : 0;
        counts[lowerCopy(base)] += c;
        if (split.second == 1) { dayCounts[lowerCopy(base)] += c; anyDayNight = true; }
        if (split.second == 2) { nightCounts[lowerCopy(base)] += c; anyDayNight = true; }
    }
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
        char dn[64] = "";
        if (dayCounts[key] || nightCounts[key]) std::snprintf(dn, sizeof dn, ", %zu day, %zu night", dayCounts[key], nightCounts[key]);
        char lbl[200]; std::snprintf(lbl, sizeof lbl, "%s  (%zu%s)%s", n.c_str(), counts[key], dn, isCurrent ? "   <- new things go here" : "");
        // a folded row whose plain section the file lacks (only "<quest>%NightOnly", say) cannot take new things
        const bool exists = std::any_of(sectionNamesCache_.begin(), sectionNamesCache_.end(), [&](const std::string& x) { return lowerCopy(x) == key; });
        if (ImGui::Selectable(lbl, isCurrent, exists ? 0 : ImGuiSelectableFlags_Disabled)) doc_.setPlacementSection(n);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(key == "null" ? "The main section: always loaded." : "Loaded with its quest. Click: new things go here.");
        ImGui::PopID();
    }
    // vanilla Quests dialog: Day only / Night only (CQuestDialog::GetQuestsToDisplay adds the
    // shown quests' %DayOnly / %NightOnly sections)
    if (ImGui::Checkbox("Day-only creatures##secday", &showDayOnly_)) sectionsDirty_ = true;
    auto_.registerWidget("check_section_day");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draw the creatures set to appear only by day (their quest's %%DayOnly section).");
    ImGui::SameLine();
    if (ImGui::Checkbox("Night-only##secnight", &showNightOnly_)) sectionsDirty_ = true;
    auto_.registerWidget("check_section_night");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draw the creatures set to appear only by night (their quest's %%NightOnly section).");
    if (!anyDayNight) theme::hint("No day- or night-only creatures on this map (a creature's Properties set it).");
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
    if (doc_.summary(idx).type == "AICreature") {
        // vanilla CTCDayOrNightOnlySupport "DayNightExclusive": the creature's section carries it
        int mode = editor::Document::splitDayNight(doc_.sectionOf(idx)).second;
        const int before = mode;
        theme::segmented("##daynight", mode, {"Day and night", "Day only", "Night only"}, cardInner);
        auto_.registerWidget("seg_daynight");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("When the creature is in the world: it moves to its quest's %%DayOnly / %%NightOnly section,\nwhich the game streams in only by day / by night (vanilla DayNightExclusive).");
        if (mode != before) {
            if (const auto n = doc_.setDayNight(idx, mode)) { selectThing(int(*n)); sectionsDirty_ = true; }
            return;   // indices moved: draw the grid next frame
        }
    }
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
        {
            // the caption fits its column: long keys end in "..." (the tooltip has the whole name)
            std::string cap = vf ? vf->label : r.key;
            const float room = keyW - S(10);
            if (ImGui::CalcTextSize(cap.c_str()).x > room) {
                while (cap.size() > 3 && ImGui::CalcTextSize((cap + "...").c_str()).x > room) cap.pop_back();
                cap += "...";
            }
            ImGui::TextColored(theme::vec(vf ? theme::Text : theme::Muted), "%s", cap.c_str());
        }
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
        } else if (r.ctc.empty() && r.key == "Player" && r.kind == K::Int && isOwnerType(doc_.summary(idx).type)) {
            // the owner, as the vanilla PLAYER_LIST_BOX names it (Auto is a placement-time choice, not a stored value)
            static const char* names[] = {"Player 0", "Player 1", "Player 2", "Player 3", "Neutral"};
            const int cur = std::atoi(r.value.c_str());
            const std::string curName = cur >= 0 && cur <= 4 ? names[cur] : "Player " + r.value;
            if (ImGui::BeginCombo("##v", curName.c_str())) {
                for (int p = 0; p <= 4; ++p)
                    if (ImGui::Selectable(names[p], p == cur)) { next = std::to_string(p); commit = true; }
                ImGui::EndCombo();
            }
            auto_.registerWidget("combo_prop_player");
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

std::vector<editor::Document::Issue> App::validateMap() {
    if (!documentLoaded()) return {};
    if (issuesRev_ != doc_.revision()) {
        if (familyNames_.empty() && ctx_.ready())
            for (const auto& [n, t] : ctx_.definitions({"CREATURE_GENERATION_FAMILY"})) familyNames_.insert(n);
        std::function<bool(const std::string&)> isFamily;
        if (!familyNames_.empty()) isFamily = [this](const std::string& n) { return familyNames_.count(n) != 0; };
        issuesCache_ = doc_.validate(isFamily);
        issuesRev_ = doc_.revision();
    }
    return issuesCache_;
}

void App::showFirstInvalid() {
    const auto issues = validateMap();
    if (issues.empty()) { pushLog("There are no invalid things on this map", 3); return; }
    selectThing(int(issues.front().thing));
    frameSelected();
    pushLog("This thing is invalid. Reason: " + issues.front().message + "  (" + std::to_string(issues.size()) + " issue(s); Level tab lists them)", 1);
}

// Vanilla's V key / SaveLevel warning: the five CTC Validate rules it has (offline
// ones) plus FableForge's link and track checks. Warns; never blocks a save.
void App::drawCheckCard(float pad, float inner, float cardInner) {
    using theme::S;
    if (!documentLoaded()) return;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##check", inner);
    const auto issues = validateMap();
    char head[64]; std::snprintf(head, sizeof head, issues.empty() ? "Check the map  (clean)" : "Check the map  (%zu)", issues.size());
    theme::label(head);
    ImGui::PushFont(fontSmall_);
    if (issues.empty()) ImGui::TextColored(theme::vec(theme::Faint), "No invalid things (vanilla rules + links + tracks).");
    for (size_t i = 0; i < issues.size() && i < 40; ++i) {
        const auto& is = issues[i];
        char row[320]; std::snprintf(row, sizeof row, "%s %s: %s##iss%zu", is.vanilla ? "[vanilla]" : "[forge]", thingLabel(is.thing).c_str(), is.message.c_str(), i);
        if (ImGui::Selectable(row, selectedThing_ == int(is.thing))) { selectThing(int(is.thing)); frameSelected(); }
    }
    ImGui::PopFont();
    ImGui::PushFont(fontSmall_);
    theme::hint("V jumps to the first one. [vanilla] = the rule the Lionhead editor warns about on save (spawners without / with unknown families, receptors with both or neither flag, building camera points without an owner); [forge] = links to things missing from this map and broken track chains, which vanilla does not check.");
    ImGui::PopFont();
    theme::endCard();
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
        const bool hidden = t >= 0 && thingHiddenBySection(per, size_t(t));
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
    const auto per = doc_.thingSections();
    for (const auto& t : tracksCache_)
        for (const size_t n : t.nodes) {
            if (thingHiddenBySection(per, n)) continue;   // a hidden section's nodes are not pickable either
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

// The destination of an Assets import: a FableForge pack (default: the first one in the
// load order) or the game directly. A new pack goes under <root>/FableForgeMods/<Name>/.
std::vector<std::pair<std::string, std::string>> App::packChoices() {
    if (modOrder_.mods.empty()) refreshModOrder();
    std::vector<std::pair<std::string, std::string>> packs;   // (label, folder)
    for (const auto& m : modOrder_.mods)
        if (m.kind == forge::modorder::Kind::Forge) {
            fs::path src(m.source);
            if (src.is_relative()) src = fs::path(saveRoot()) / src;
            packs.push_back({m.name, src.string()});
        }
    if (!packDestChosen_) { packDest_ = packs.empty() ? std::string() : packs.front().second; packDestChosen_ = true; }
    return packs;
}

std::string App::packLabel(const std::string& folder) {
    for (const auto& [l, f] : packChoices()) if (f == folder) return l;
    return fs::path(folder).filename().string();
}

void App::drawPackPicker(float width) {
    const auto packs = packChoices();
    const std::string cur = packDest_.empty() ? std::string("the game directly") : "mod pack " + packLabel(packDest_);
    ImGui::PushFont(fontSmall_);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(theme::Muted), "Writes go into");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width - ImGui::GetCursorPosX() + ImGui::GetStyle().WindowPadding.x);
    if (ImGui::BeginCombo("##packpick", cur.c_str())) {
        for (const auto& [l, f] : packs)
            if (ImGui::Selectable(("mod pack " + l).c_str(), f == packDest_)) packDest_ = f;
        if (ImGui::Selectable("the game directly", packDest_.empty())) packDest_.clear();
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_pack_pick");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A mod pack: the map's objects / terrain go into the pack; Mods > Deploy builds them into the game with every\nother mod (switchable, shareable, ordered). The game directly: FableForge's classic writes (one-time backups).\nMake a pack on the Assets tab (Models / Ground themes: New pack).");
    ImGui::PopFont();
}

void App::drawPackDestination(float cardInner) {
    using theme::S;
    const auto packs = packChoices();
    std::string cur = "Directly into the game (advanced)";
    for (const auto& [l, f] : packs) if (f == packDest_) cur = "Mod pack: " + l;
    theme::label("Goes into");
    ImGui::SetNextItemWidth(cardInner);
    if (ImGui::BeginCombo("##packdest", cur.c_str())) {
        for (const auto& [l, f] : packs)
            if (ImGui::Selectable(("Mod pack: " + l).c_str(), f == packDest_)) packDest_ = f;
        if (ImGui::Selectable("Directly into the game (advanced)", packDest_.empty())) packDest_.clear();
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_pack_dest");
    ImGui::SetNextItemWidth(cardInner - S(96));
    ImGui::InputTextWithHint("##newpack", "New pack name", newPackName_, sizeof newPackName_);
    auto_.registerWidget("input_new_pack");
    ImGui::SameLine(0, S(6));
    if (theme::ghostButton("New pack", ImVec2(S(90), S(26))) && newPackName_[0]) {
        std::string leaf = newPackName_;
        for (auto& c : leaf) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) c = '_';
        const fs::path folder = fs::path(saveRoot()) / "FableForgeMods" / leaf;
        std::string err;
        if (!modpack::create(folder, newPackName_, err)) pushLog("pack: " + err, 2);
        else if (modAdd(folder.string(), newPackName_)) { packDest_ = folder.string(); pushLog("pack " + std::string(newPackName_) + " created in " + folder.string() + " and added to the load order", 3); newPackName_[0] = 0; }
    }
    auto_.registerWidget("btn_new_pack");
    ImGui::PushFont(fontSmall_);
    theme::hint(packDest_.empty() ? "Directly: the game's banks are rewritten now (one-time backups). The import is not a mod -- it cannot be switched off, shared or ordered."
                                  : "Into the pack: the files and a recipe go in the pack; Mods > Deploy builds it into the game with every other mod, so it can be switched off, shared and ordered.");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(4)));
}

// Import into the chosen pack (a recipe) or directly (the in-place import). model: the Models page.
bool App::addToPackOrGame(bool model) {
    auto upper = [](std::string nm) { for (auto& c : nm) { c = char(std::toupper(static_cast<unsigned char>(c))); if (!std::isalnum(static_cast<unsigned char>(c))) c = '_'; } return nm; };
    if (packDest_.empty()) {
        if (model) return importMesh(meshModelPath_, upper(meshName_), meshTexturePng_);
        return createCustomTheme(customPng_, upper(customName_), customDonor_);
    }
    std::string err;
    bool ok;
    if (model) {
        modpack::ModelRecipe r; r.name = upper(meshName_); r.model = meshModelPath_; r.texture = meshTexturePng_;
        ok = modpack::addModel(packDest_, r, err);
        if (ok) { pushLog("pack: model " + r.name + " added (OBJECT_" + r.name + " after the next Mods > Deploy)", 3); meshModelPath_[0] = 0; meshName_[0] = 0; meshTexturePng_[0] = 0; }
    } else {
        modpack::GroundThemeRecipe r; r.name = upper(customName_); r.png = customPng_; r.donor = customDonor_;
        ok = modpack::addGroundTheme(packDest_, r, err);
        if (ok) { pushLog("pack: ground theme " + r.name + " added (paintable after the next Mods > Deploy)", 3); customPng_[0] = 0; customName_[0] = 0; }
    }
    if (!ok) pushLog("pack: " + err, 2);
    return ok;
}

// ---- Assets > Models: a model into graphics.big + an OBJECT def (meshimport)
void App::drawModelImportCard(float pad, float inner, float cardInner) {
    using theme::S;
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
    drawPackDestination(cardInner);
    if (theme::primaryButton(meshBusy ? "Importing..." : packDest_.empty() ? "Import into the game" : "Add to the pack", ImVec2(cardInner, S(30)), meshCan))
        addToPackOrGame(true);
    auto_.registerWidget("btn_mesh_import");
    ImGui::PushFont(fontSmall_);
    theme::hint(packDest_.empty()
        ? "The model becomes MESH_<NAME> in graphics.big, the PNG <NAME>_DIFFUSE in textures.big and OBJECT_<NAME> in game.bin (a copy of the barrel's def with the new mesh), with a collision hull from the model's own triangles; nothing retail is replaced, one-time backups. It then shows under Add an object."
        : "At deploy the pack's recipe makes MESH_<NAME> (graphics.big), <NAME>_DIFFUSE (textures.big) and OBJECT_<NAME> (game.bin, a copy of the barrel's def) with a collision hull from the model's own triangles, taking the ids the other mods leave free. After Mods > Deploy it shows under Add an object.");
    ImGui::PopFont();
    theme::endCard();
}

// ---- Assets > Ground themes: a PNG into textures.big + a new ENGINE_THEME (a copy of a donor)
void App::drawGroundThemeCard(float pad, float inner, float cardInner) {
    using theme::S;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##groundtheme", inner);
    theme::label("New ground theme from a PNG");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##custompng", "Path to a square power-of-two PNG (512x512 like retail)", customPng_, sizeof customPng_);
    auto_.registerWidget("input_custom_png");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##customname", "Theme name, e.g. GROUND_MY_MOSS", customName_, sizeof customName_, ImGuiInputTextFlags_CharsUppercase);
    auto_.registerWidget("input_custom_name");
    ImGui::PopStyleVar();
    // the donor: the theme being painted when a map is open, else one picked here
    const forge::lev::File* lev = documentLoaded() ? doc_.level() : nullptr;
    if (customDonor_.empty() && lev && paintTheme_ >= 0 && size_t(paintTheme_) < lev->groundThemes().size()) customDonor_ = lev->groundThemes()[size_t(paintTheme_)].name;
    if (customDonor_.empty()) customDonor_ = "GROUND_GRASS";
    theme::label("Copies the settings of");
    ImGui::SetNextItemWidth(cardInner);
    if (ImGui::BeginCombo("##donor", customDonor_.c_str())) {
        if (const auto* lib = ctx_.themeLibrary())
            for (const auto& th : lib->themes())
                if (th.decoded && ImGui::Selectable(th.name.c_str(), th.name == customDonor_)) customDonor_ = th.name;
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_custom_donor");
    ImGui::PushFont(fontSmall_);
    theme::hint(("The PNG is appended to textures.big and a new ENGINE_THEME (a copy of " + customDonor_ + " with your texture) to game.bin; nothing retail is replaced. One-time backups. With a map open it joins that map's palette, ready to paint; otherwise add it from Terrain > Paint > Ground.").c_str());
    ImGui::PopFont();
    drawPackDestination(cardInner);
    const bool can = customPng_[0] && customName_[0] && !ctxFuture_.valid();
    if (theme::primaryButton(ctxFuture_.valid() ? "Textures reloading..." : packDest_.empty() ? "Create ground theme" : "Add to the pack", ImVec2(cardInner, S(32)), can))
        addToPackOrGame(false);
    auto_.registerWidget("btn_custom_theme_create");
    theme::endCard();
}

bool App::createCustomTheme(const std::string& png, const std::string& name, const std::string& donor, const std::string& cliffPng) {
    if (ctxFuture_.valid()) { pushLog("editor: textures are still loading, try again in a moment", 1); return false; }
    if (!installValid_) { pushLog("editor: a custom theme needs a Fable install", 1); return false; }
    editor::CustomThemeRequest req;
    req.png = png; req.name = name; req.donor = donor.empty() ? "GROUND_GRASS" : donor;
    if (!cliffPng.empty()) req.cliffPng = cliffPng;
    editor::CustomThemeResult out; std::string err;
    pushLog("custom theme " + name + ": importing " + png + " into textures.big and appending the ENGINE_THEME...", 0);
    if (!editor::createCustomTheme(saveRoot(), req, out, err)) { pushLog("editor: custom theme failed: " + err, 2); return false; }
    for (const auto& n : out.notes) pushLog("custom theme: " + n, 0);
    if (documentLoaded() && doc_.hasTerrain()) {
        const int slot = doc_.addGroundTheme(name, out.defIndex);
        if (slot < 0) { pushLog("editor: the palette has no free slot for " + name, 1); return false; }
        paintTheme_ = slot;
        pushLog("ground theme " + name + " in palette slot " + std::to_string(slot) + " (def " + std::to_string(out.defIndex) + ", texture " + std::to_string(out.baseTexture) + "); reloading textures", 3);
    } else {
        pushLog("ground theme " + name + " created (def " + std::to_string(out.defIndex) + ", texture " + std::to_string(out.baseTexture) + "); open a map and add it from Terrain > Paint > Ground", 3);
    }
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
    foliageexport::closeMeshBank(); thumbBankOpen_ = false; defThumbs_.clear(); defList_.clear(); themeGroupOf_.clear(); envDefs_.clear(); soundDefs_.clear(); familyNames_.clear(); issuesRev_ = ~0ull;
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

// The vanilla Things dialog's placement options (Random placement angle / Place at constant
// angle / Place at constant height), as one facing choice and one height switch.
void App::drawPlacementOptions(float width) {
    using theme::S;
    ImGui::Dummy(ImVec2(0, S(4)));
    theme::label("Facing");
    theme::segmented("##place_facing", placeFacing_, {"Toward camera", "Random", "Fixed angle"}, width);
    auto_.registerWidget("seg_place_facing");
    if (placeFacing_ == 1) theme::hint("Each new thing gets its own turn, like the vanilla editor's random placement angle.");
    if (placeFacing_ == 2) {
        ImGui::SetNextItemWidth(width);
        ImGui::SliderFloat("##place_angle", &placeAngleDeg_, 0.0f, 360.0f, "%.0f deg (0 = +Y, clockwise)");
        auto_.registerWidget("slider_place_angle");
    }
    ImGui::Dummy(ImVec2(0, S(2)));
    {
        // vanilla "Player Auto" combo: Auto, Player 0-3, Neutral
        const char* owners[] = {"Auto", "Player 0", "Player 1", "Player 2", "Player 3", "Neutral"};
        int pick = placeOwner_ < 0 ? 0 : placeOwner_ >= 4 ? 5 : placeOwner_ + 1;
        const float labelW = S(58);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(theme::Muted), "Owner");
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(width - labelW);
        if (ImGui::Combo("##place_owner", &pick, owners, 6)) placeOwner_ = pick == 0 ? -1 : pick == 5 ? 4 : pick - 1;
        auto_.registerWidget("combo_place_owner");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The Player field of new objects, buildings, creatures and villages.\n"
                              "Auto: a creature takes its definition's DefaultOwner (hostiles 2), everything else Neutral (4).\n"
                              "O applies it to the selection (Auto = Neutral there, as in the vanilla editor).");
    }
    ImGui::Dummy(ImVec2(0, S(2)));
    theme::toggle("Fixed height", &placeFixedHeight_);
    auto_.registerWidget("toggle_place_height");
    if (placeFixedHeight_) {
        const float btnW = S(96);
        ImGui::SetNextItemWidth(width - btnW - ImGui::GetStyle().ItemSpacing.x);
        ImGui::DragFloat("##place_height", &placeHeight_, 0.1f, -1000.0f, 5000.0f, "%.2f m");
        ImGui::SameLine();
        if (theme::ghostButton("Sample here", ImVec2(btnW, 0)) && documentLoaded()) {
            float focus[3]; camera_.focus(focus);
            if (const auto h = doc_.groundHeight(focus[0], -focus[2])) placeHeight_ = *h;
        }
        auto_.registerWidget("btn_place_sample_height");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Take the ground height where the camera looks.");
        theme::hint("A world height. Where the ground is higher, the thing sits on the ground.");
    }
}

int App::ownerFor(const std::string& def) const {
    if (placeOwner_ >= 0) return std::min(placeOwner_, 4);
    if (def.rfind("CREATURE_", 0) == 0) {
        if (const auto v = ctx_.defIntField(def, "DefaultOwner")) return int(*v);
        return 0;   // defs not readable: FableForge's old creature default
    }
    return 4;
}

// the thing types whose ConstructFromParams takes an owner (CThing clamps it, CThingAICreature may take DefaultOwner)
bool App::isOwnerType(const std::string& type) {
    static const std::set<std::string> owned{"AICreature", "Building", "Village", "Object", "HolySite", "PhysicalSwitch"};
    return owned.count(type) != 0;
}

void App::applyOwnerToSelection() {
    if (!documentLoaded() || selectedThing_ < 0) return;
    const int v = placeOwner_ < 0 ? 4 : std::min(placeOwner_, 4);
    std::vector<size_t> all{size_t(selectedThing_)};
    for (const uint64_t u : extraUids_) if (const auto i = doc_.indexOfUid(u)) all.push_back(*i);
    // markers (-1), track nodes and the hero keep theirs
    std::vector<size_t> targets;
    for (const size_t i : all) {
        if (!isOwnerType(doc_.summary(i).type)) continue;
        for (const auto& r : doc_.propertiesOf(i))
            if (r.ctc.empty() && r.key == "Player") { if (r.value != std::to_string(v)) targets.push_back(i); break; }
    }
    if (targets.empty()) { pushLog("owner: nothing to change (only creatures, buildings, villages, objects, holy sites and switches have an owner)", 1); return; }
    doc_.beginBatch();
    try {
        for (const size_t i : targets) doc_.setProperty(i, "Player", std::to_string(v));
    } catch (const std::exception& e) { doc_.endBatch(); pushLog(std::string("owner: ") + e.what(), 2); return; }
    doc_.endBatch();
    pushLog("owner: Player " + std::to_string(v) + " on " + std::to_string(targets.size()) + " thing(s) (one undo step)", 0);
}

bool App::thingHiddenBySection(const std::vector<std::string>& per, size_t t) const {
    if (t >= per.size()) return false;
    const auto split = editor::Document::splitDayNight(per[t]);
    const std::string base = split.first.empty() ? std::string("NULL") : split.first;
    if (hiddenSections_.count(lowerCopy(base))) return true;
    return (split.second == 1 && !showDayOnly_) || (split.second == 2 && !showNightOnly_);
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
    p.player = ownerFor(def);
    if (const auto h = doc_.groundHeight(p.position.x, p.position.y)) p.position.z = *h;
    if (placeFixedHeight_) p.position.z = forge::thingplacer::constantPlacementHeight(placeHeight_, p.position.z);
    if (placeFacing_ == 1) {
        if (placeSeed_ == 0) placeSeed_ = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) | 1u;
        p.forward = forge::thingplacer::forwardFromVanillaTurns(forge::thingplacer::vanillaFloatRandom(1.0f, placeSeed_));
    } else if (placeFacing_ == 2) {
        p.forward = forge::thingplacer::forwardFromVanillaTurns(placeAngleDeg_ / 360.0f);
    } else {   // face the camera
        float d[3]; camera_.dir(d);
        const float fx = -d[0], fy = d[2];
        const float fl = std::sqrt(fx * fx + fy * fy);
        if (fl > 1e-6f) p.forward = {fx / fl, fy / fl, 0.0f};
    }
    try {
        const bool creature = def.rfind("CREATURE_", 0) == 0;
        const float pos[3] = {p.position.x, p.position.y, p.position.z};
        const float fwd[2] = {p.forward.x, p.forward.y};
        const size_t n = creature ? doc_.placeCreature(pos, fwd, def, scriptName, p.player) : doc_.place(p);
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
    if (const auto n = validateMap().size()) pushLog("There are " + std::to_string(n) + " invalid thing(s) on this map. Press V to see the first (saved anyway, as vanilla does).", 1);
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
    if (!packDest_.empty()) {
        std::string err;
        if (!doc_.saveToPack(packDest_, err)) { pushLog("pack: " + err, 2); return false; }
        pushLog("wrote " + doc_.mapName() + ".tng into pack " + packLabel(packDest_) + " (Mods > Deploy puts it in the game)", 3);
        return true;
    }
    if (packDest_.empty() && gameWriteBlocked("deploy")) return false;
    if (doc_.external() && saveRoot() != installPath_) { pushLog("deploy: this map belongs to another world and writes its own files; a redirected save root does not apply to it", 2); return false; }
    std::string err;
    if (!doc_.deployWad(saveRoot(), err)) { pushLog("deploy failed: " + err, 2); return false; }
    if (writesLoose()) pushLog("wrote " + doc_.loosePath().string() + " (loose-level install: the game reads this file)", 3);
    else pushLog("wrote " + doc_.mapName() + ".tng into FinalAlbion.wad (backup FinalAlbion.wad.forge-orig)", 3);
    if (const auto n = validateMap().size()) pushLog("There are " + std::to_string(n) + " invalid thing(s) on this map. Press V to see the first.", 1);
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
        case 16: return M::Noise;
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
    fillPen(b);
    const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    // vanilla '-' / '=' (PaintInputPaintMapHeightAddition): lower / raise the ground under the cursor by the
    // Speed opacity on every frame the key is held, one undo step per key press; any terrain tool
    {
        const bool keysFree = !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !lmb;
        const bool down = keysFree && (ImGui::IsKeyDown(ImGuiKey_Minus) || ImGui::IsKeyDown(ImGuiKey_Equal));
        if (keyStroke_ && !down) { doc_.endStroke(); keyStroke_ = false; }
        if (down && brushHit_ && (keyStroke_ || (!doc_.strokeActive() && viewportHovered_))) {
            editor::TerrainBrush k = b;
            k.mode = editor::TerrainBrush::Mode::HeightKey;
            const float op = forge::heightpen::speedToOpacity(penSpeed_);
            k.step = ImGui::IsKeyDown(ImGuiKey_Equal) ? op : -op;
            if (!keyStroke_) { doc_.beginStroke(k); keyStroke_ = true; }
            doc_.applyBrush(k, 0.0f);
        }
        if (keyStroke_) return;
    }
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
    // Ctrl+click in Flatten: the vanilla height eyedropper (PaintInputPickupHeight) fills the target
    if (press && io.KeyCtrl && terrainMode_ == 2) {
        if (const auto h = doc_.terrainHeight(brushFable_[0], brushFable_[1])) {
            penTarget_ = *h; penTargetFromStroke_ = false;
            char msg[64]; std::snprintf(msg, sizeof msg, "flatten: target height %.2f", *h);
            pushLog(msg, 0);
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
                copyRegion(int(std::lround(clipStart_[0])), int(std::lround(clipStart_[1])), int(std::lround(brushFable_[0])), int(std::lround(brushFable_[1])));
            }
        }
        return;
    }
    if (terrainMode_ == 15) {
        if (ImGui::IsKeyPressed(ImGuiKey_R) && !io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive()) clipTurns_ = (clipTurns_ + 1) % 4;
        if (press) {
            pasteRegion(int(std::lround(brushFable_[0])), int(std::lround(brushFable_[1])));
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
        penApplied_ = false;
    }
    if (doc_.strokeActive()) {
        if (lmb) {
            // the vanilla pens apply once per click, or every frame with Spray on
            const bool gate = !isVanillaPen(terrainMode_) || penSpray_ || !penApplied_;
            if (brushHit_ && gate) { doc_.applyBrush(b, std::min(io.DeltaTime, 0.1f)); penApplied_ = true; }
        }
        else doc_.endStroke();
    }
}

void App::fillPen(editor::TerrainBrush& b) const {
    b.exactStep = penExactStep_; b.step = penStep_;
    b.targetFromStroke = penTargetFromStroke_; b.target = penTarget_; b.speed = penSpeed_;
    b.smoothness = penSmoothness_; b.spikyness = penSpikyness_; b.magnifier = penMagnifier_;
}

// the vanilla Height Toolbox options for the sculpt tool in use (Change Height step, Paint
// Height target + speed, Smear sliders, Noise magnifier) and the Spray can toggle
void App::drawPenControls(float cardInner) {
    using theme::S;
    const int m = terrainMode_;
    if (m != 0 && m != 1 && m != 2 && m != 3 && m != 16) return;
    const float labelW = S(110);
    auto row = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(theme::Muted), "%s", label);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(cardInner - labelW);
    };
    if (m == 0 || m == 1) {
        ImGui::Checkbox("Exact step##pstep", &penExactStep_);
        auto_.registerWidget("check_pen_step");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vanilla Change Height: every block under the brush moves by exactly the step,\nonce per stroke, with no soft edge. Off: FableForge's smooth brush.");
        if (penExactStep_) {
            row("Step (m)");
            ImGui::InputFloat("##pstepv", &penStep_, 0.5f, 1.0f, "%.2f");
            penStep_ = std::clamp(penStep_, 0.0f, 2048.0f);
            auto_.registerWidget("input_pen_step");
        }
    } else if (m == 2) {
        ImGui::Checkbox("Level to where the stroke starts##ptfs", &penTargetFromStroke_);
        auto_.registerWidget("check_pen_target_from_stroke");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: the ground under the first click is the target.\nOff: the target height below (vanilla Paint Height); Ctrl+click the ground to sample it.");
        if (!penTargetFromStroke_) {
            row("Target (m)");
            ImGui::InputFloat("##ptarget", &penTarget_, 0.5f, 5.0f, "%.2f");
            penTarget_ = std::clamp(penTarget_, 0.0f, 2048.0f);
            auto_.registerWidget("input_pen_target");
        }
        row("Speed");
        ImGui::SliderFloat("##pspeed", &penSpeed_, 0.0f, 1.0f, "%.1f");
        auto_.registerWidget("slider_pen_speed");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far toward the target each application goes: 1 - cos(90 deg x speed)\n(0.5 = 29%%, 1 = all the way). Vanilla's Speed slider.\nIt is also the step of the - / = keys, which lower / raise the ground under the cursor while held.");
    } else if (m == 3) {
        row("Smoothness");
        float pct = penSmoothness_ * 100.0f;
        if (ImGui::SliderFloat("##psmooth", &pct, 0.0f, 100.0f, "%.0f %%")) penSmoothness_ = pct / 100.0f;
        auto_.registerWidget("slider_pen_smoothness");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far a block moves toward its neighbours' average each application (vanilla \"Smear: Smoothness %%\").");
        row("Spikes allowed");
        ImGui::SliderFloat("##pspiky", &penSpikyness_, 0.0f, 1.0f, "%.1f");
        auto_.registerWidget("slider_pen_spikyness");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0: smooth every bump. 1: only real spikes and pits (a block outside its neighbours' range).\nStraight slopes are never flattened (vanilla \"Smear: Spikyness Allowed\").");
    } else if (m == 16) {
        row("Amount");
        ImGui::SliderFloat("##pmag", &penMagnifier_, 1.0f, 50.0f, "%.1f");
        auto_.registerWidget("slider_pen_magnifier");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each jump is up to 0.01 + 0.05 x amount world units (vanilla noise magnifier, 1 - 50).");
    }
    if (isVanillaPen(m)) {
        ImGui::Checkbox("Repeat while held##pspray", &penSpray_);
        auto_.registerWidget("check_pen_spray");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vanilla Spray can. On: applies every frame while the button is held. Off: once per click.");
    }
}

void App::terrainStroke(float x, float y, float seconds) {
    if (!documentLoaded() || !doc_.hasTerrain()) return;
    editor::TerrainBrush b;
    b.mode = brushModeFor(terrainMode_);
    b.x = x; b.y = y; b.radius = brushRadius_; b.strength = brushStrength_;
    b.themeIndex = uint8_t(terrainMode_ == 10 ? envSlot_ : terrainMode_ == 11 ? soundIndex_ : paintTheme_);
    b.replaceFrom = uint8_t(std::max(replaceFrom_, 0));
    fillPen(b);
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
        const size_t n = doc_.placeVillage(pos, def, scriptName, ownerFor(def));
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
    if (packDest_.empty() && gameWriteBlocked("new level")) return;
    const std::string root = saveRoot();
    const std::string pack = packDest_;   // "" = the game directly
    // into a pack the level, its world records and static map go in; its minimap would be
    // a textures.big + game.bin write, which a pack does not carry yet
    const bool minimap = pack.empty();
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
        if (!minimap) req.ownRegion.minimap = false;
        const forge::terraintex::ThemeLibrary* lib = ctx_.themeLibrary();
        if (!lib) return;
        pushLog("new level: authoring blank " + req.name + " at (" + std::to_string(req.worldX) + "," + std::to_string(req.worldY) + "), region " + req.hostRegion + "...", 0);
        beginJob(); req.progress = jobProgress();
        newLevelFuture_ = std::async(std::launch::async, [req, root, lib, pack]() {
            NewLevelJob j; j.name = req.name; j.ownRegion = req.ownRegion.wanted && req.ownRegion.dedicated; j.pack = pack;
            if (pack.empty()) j.ok = editor::createBlankLevel(root, req, *lib, j.result, j.error);
            else j.ok = albion::modpack::intoPack(root, pack, [&](const std::filesystem::path& shadow, std::string& err) {
                return editor::createBlankLevel(shadow, req, *lib, j.result, err);
            }, j.result.notes, j.error);
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
    if (!minimap) req.ownRegion.minimap = false;
    pushLog("new level: cloning " + req.donor + " as " + req.name + " at (" + std::to_string(req.worldX) + "," + std::to_string(req.worldY) + "), region " + req.hostRegion + "...", 0);
    beginJob(); req.progress = jobProgress();
    newLevelFuture_ = std::async(std::launch::async, [req, root, pack]() {
        NewLevelJob j; j.name = req.name; j.ownRegion = req.ownRegion.wanted && req.ownRegion.dedicated; j.pack = pack;
        if (pack.empty()) j.ok = editor::createLevelFromDonor(root, req, j.result, j.error);
        else j.ok = albion::modpack::intoPack(root, pack, [&](const std::filesystem::path& shadow, std::string& err) {
            return editor::createLevelFromDonor(shadow, req, j.result, err);
        }, j.result.notes, j.error);
        return j;
    });
}

void App::startTerrainDeploy() {
    if (!documentLoaded() || !doc_.hasTerrain() || terrainDeployFuture_.valid()) return;
    linkPoll(true);
    if (packDest_.empty() && gameWriteBlocked("terrain")) return;
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
    const std::string pack = packDest_;   // "" = the game directly
    terrainDeployFuture_ = std::async(std::launch::async, [ctxHold, doc, root, lib, progress, pack]() {
        TerrainDeployResult r;
        r.pack = pack;
        r.ok = pack.empty() ? doc->deployTerrain(root, r.notes, r.error, lib, progress)
                            : doc->deployTerrainToPack(root, pack, r.notes, r.error, lib, progress);
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
    if (ImGui::IsKeyPressed(ImGuiKey_V) && !io.KeyCtrl) showFirstInvalid();   // vanilla: find the invalid thing
    // vanilla saves with Ctrl+S / F6: here the draft (the loose .tng); writing into the game stays a confirmed button
    if (((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) || ImGui::IsKeyPressed(ImGuiKey_F6)) && doc_.dirty()) saveDocument();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) editUndo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) editRedo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selectedThing_ >= 0) duplicateSelected();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && selectedThing_ >= 0) copySelection();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) pasteClipboard();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && (linkPick_.active || trackLinkPick_)) { linkPick_.active = false; trackLinkPick_ = false; }
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape) && selectedThing_ >= 0) selectThing(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_End) && selectedThing_ >= 0) snapSelectedToGround();
    // vanilla O: the selection takes the owner picked in Add an object (Auto = Neutral)
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_O, false) && selectedThing_ >= 0) applyOwnerToSelection();
}

void App::drawGizmo(const ImVec2& origin, const ImVec2& size) {
    if (!editMode_ || !documentLoaded() || selectedThing_ < 0 || gizmoOp_ == 0 || gizmoOp_ == 4) { gizmoWasUsing_ = false; return; }   // 4 = the terrain tool: no gizmo
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
            static const std::vector<Tool> sculpt = {{0, "Raise"}, {1, "Lower"}, {2, "Flatten"}, {3, "Smooth"}, {16, "Noise"}, {9, "Path"}};
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
                    ImGui::SameLine();
                }
                ImGui::Checkbox("Things##clth", &clipThings_);
                auto_.registerWidget("check_clip_things");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(terrainMode_ == 14 ? "Also copy the objects standing inside the rectangle." : "Also place the copied objects (turned with the ground, at their old height above it).");
                ImGui::PushFont(fontSmall_);
                if (!terrainClip_.empty()) ImGui::TextColored(theme::vec(theme::Muted), "clipboard: %d x %d vertices, %zu object%s, turned %d deg", terrainClip_.w, terrainClip_.h,
                                                              terrainClip_.things.size(), terrainClip_.things.size() == 1 ? "" : "s", clipTurns_ * 90);
                theme::hint(terrainMode_ == 14 ? "Drag a rectangle on the ground to copy its heights, ground themes and objects (the vanilla Copy and paste dialog). The copy survives switching maps."
                                               : "Click to paste with the copy's first corner there; R turns it 90 degrees. Themes are matched by name (a missing one takes a free palette slot). One undo step per paste.");
                ImGui::PopFont();
                drawBrushLibrary(cardInner);
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
            // your own texture: the Assets tab makes a new ENGINE_THEME from a PNG
            if (theme::ghostButton("Your own ground texture...  (Assets tab)", ImVec2(cardInner, S(26)))) { setTexturesMode(true); assetsTab_ = 2; }
            auto_.registerWidget("btn_goto_ground_theme");
        }
        drawPenControls(cardInner);
        // the brush size where a tool has one (flood / copy / paste do not); strength where it blends
        const bool usesRadius = terrainMode_ != 8 && terrainMode_ != 14 && terrainMode_ != 15;
        const bool usesStrength = usesRadius && terrainMode_ != 9 && terrainMode_ != 11 && terrainMode_ != 4 && terrainMode_ != 5 &&
                                  terrainMode_ != 12 && terrainMode_ != 13 && terrainMode_ != 7 && !isVanillaPen(terrainMode_);
        char val[48];
        if (usesRadius) {
            std::snprintf(val, sizeof val, "%.0f cells", brushRadius_);
            theme::labelValue("Radius   ( [ ] )", val, cardInner);
            ImGui::SetNextItemWidth(cardInner);
            ImGui::SliderFloat("##radius", &brushRadius_, 1.0f, 60.0f, "");
            auto_.registerWidget("slider_radius");
        }
        if (usesStrength) {
            std::snprintf(val, sizeof val, "%.1f", brushStrength_);
            theme::labelValue("Strength", val, cardInner);
            ImGui::SetNextItemWidth(cardInner);
            ImGui::SliderFloat("##strength", &brushStrength_, 0.5f, 20.0f, "");
            auto_.registerWidget("slider_strength");
        }
        // how to use the brush, per tool (Shift only inverts raise/lower and walkable/blocked; Replace has its own hint above)
        const char* how = nullptr;
        if ((terrainMode_ == 0 || terrainMode_ == 1) && penExactStep_) how = "Click or drag: every block under the brush moves by the step once per stroke (a terrace). Shift swaps raise and lower. One undo step per stroke.";
        else if (terrainMode_ == 0 || terrainMode_ == 1) how = "Hold LMB on the ground to sculpt; Shift swaps raise and lower. Each stroke is one undo step.";
        else if (terrainMode_ == 2) how = "Hold LMB: the ground under the brush moves toward the target height, never past it. Ctrl+click samples the target. One undo step per stroke.";
        else if (terrainMode_ == 3) how = "Hold LMB: bumps and pits move toward their neighbours; straight slopes stay. One undo step per stroke.";
        else if (terrainMode_ == 16) how = "Hold LMB: random blocks under the brush jump up and down (nothing happens where the ground under the brush averages sea level; no block goes below 0). One undo step per stroke.";
        else if (terrainMode_ == 4 || terrainMode_ == 5) how = "Hold LMB on the ground to paint; Shift swaps walkable and blocked. Switch the view to Walkable to see it. Each stroke is one undo step.";
        else if (terrainMode_ == 6 || terrainMode_ == 10 || terrainMode_ == 11 || terrainMode_ == 12 || terrainMode_ == 13)
            how = "Hold LMB on the ground to paint. Each stroke is one undo step.";
        if (usesRadius && how) {
            ImGui::PushFont(fontSmall_);
            theme::hint(how);
            ImGui::PopFont();
        }
        theme::endCard();
        ImGui::Dummy(ImVec2(0, S(8)));

        // ---- the vanilla Fractals dialog (CFractalDialog), ported generator
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##fractal", inner);
        if (theme::ghostButton(fractalOpen_ ? "Fractal terrain  (open)" : "Fractal terrain...", ImVec2(cardInner, S(26)))) fractalOpen_ = !fractalOpen_;
        auto_.registerWidget("btn_fractal_toggle");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The vanilla editor's fractal generator: set this map's ground to a hybrid multifractal.");
        theme::endCard();
        ImGui::Dummy(ImVec2(0, S(8)));
        drawFitCard(pad, inner, cardInner);
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
    // the vanilla Scene Browser's two switches (CSceneDialog): script-named things only, nearest first
    ImGui::PushFont(fontSmall_);
    ImGui::Checkbox("Script-named only##tso", &thingsScriptOnly_);
    auto_.registerWidget("check_things_script_only");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only things with a ScriptName (the ones quests and cut-scenes refer to).");
    ImGui::SameLine();
    ImGui::Checkbox("Nearest first##tnf", &thingsNearest_);
    auto_.registerWidget("check_things_nearest");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sort by distance from the camera. Double-click a row to fly to it.");
    ImGui::PopFont();
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
        if (thingsScriptOnly_ && s.scriptName.empty()) continue;
        if (!originFilter_.empty()) {
            const char* o = originOf(s.uid);
            if (originFilter_ == "retail" ? o != nullptr : (!o || originFilter_ != o)) continue;
        }
        rows.push_back(int(i));
    }
    if (thingsNearest_) {
        // camera in map-local Fable units (render x, -render z, as the brush maps them)
        const float camX = camera_.posX, camY = -camera_.posZ;
        std::vector<std::pair<float, int>> keyed;
        keyed.reserve(rows.size());
        for (const int i : rows) {
            editor::Frame f;
            float d = 1e30f;
            if (doc_.frameOf(size_t(i), f)) {
                const float wx = f.pos[0], wy = f.pos[1];
                d = (wx - camX) * (wx - camX) + (wy - camY) * (wy - camY);
            }
            keyed.push_back({d, i});
        }
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t k = 0; k < keyed.size(); ++k) rows[k] = keyed[k].second;
    }
    thingsFirst_ = rows.empty() ? -1 : rows.front();
    thingsShown_ = rows.size();
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Placed where the camera looks, on the terrain (or at the fixed height below).");
    drawPlacementOptions(cardInner);
    drawRuleNotice("creature", cardInner);
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

    // model import lives in the Assets tab (it writes the game's banks, not this map)
    ImGui::SetCursorPosX(pad);
    if (theme::ghostButton("Import your own model...  (Assets tab)", ImVec2(inner, S(26)))) { setTexturesMode(true); assetsTab_ = 1; }
    auto_.registerWidget("btn_goto_model_import");
    ImGui::Dummy(ImVec2(0, S(8)));
    }

    if (editTab_ == 2) {
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
        drawCheckCard(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(8)));
        drawBudgetCard(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(8)));
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
    ImGui::SetCursorPosX(pad);
    drawPackPicker(inner);
    const bool toPack = !packDest_.empty();
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
        } else if (toPack) {
            // into a pack nothing in the game changes: no confirmation
            if (theme::primaryButton(("Write terrain into pack " + packLabel(packDest_)).c_str(), ImVec2(inner, S(36)))) startTerrainDeploy();
            auto_.registerWidget("btn_terrain_deploy");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The .lev and this map's re-baked static-map chunk go into the pack (data/Levels/FinalAlbion + stb/);\nMods > Deploy writes them into the game with the other mods. Nothing in the game changes now.");
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
    if (toPack) {
        if (theme::primaryButton(("Write into pack " + packLabel(packDest_)).c_str(), ImVec2(inner, S(42)))) deployDocument();
        auto_.registerWidget("btn_deploy");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes %s.tng into the pack (data/Levels/FinalAlbion); Mods > Deploy merges it thing by thing\nwith the other mods and writes it into the game. Nothing in the game changes now.", doc_.mapName().c_str());
    } else if (!confirmDeploy_) {
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


// ---------------------------------------------------------------- fit to neighbours

void App::startFitNeighbourLoad() {
    if (fitFuture_.valid() || !documentLoaded()) return;
    fitNeighboursFor_ = doc_.mapName();
    fitNeighbours_.clear(); fitNeighboursNote_.clear(); fitPreviewKey_.clear();
    editor::WorldLayout layout;
    std::string err;
    if (!editor::loadWorldLayout(installPath_, layout, err)) { fitNeighboursNote_ = err; return; }
    const editor::WorldMapBox* box = layout.find(doc_.mapName());
    if (!box) { fitNeighboursNote_ = doc_.mapName() + " is not placed on the world map"; return; }
    // .lev paths here (the WAD read uses app state), the parses on the worker
    struct Job { std::string lev, name; int x, y; };
    std::vector<Job> jobs;
    for (const auto* n : layout.touching(*box, box->x, box->y)) {
        if (n->name == box->name) continue;
        const MapEntry* e = findEntry(n->name);
        if (!e) continue;
        const std::string lev = resolveLevPath(*e, err);
        if (!lev.empty()) jobs.push_back({lev, n->name, n->x, n->y});
    }
    fitFuture_ = std::async(std::launch::async, [jobs]() {
        std::pair<std::vector<forge::fillerfit::Neighbour>, std::string> r;
        for (const auto& j : jobs) {
            try {
                const auto lev = forge::lev::File::open(j.lev);
                forge::fillerfit::Neighbour nb;
                nb.name = j.name; nb.x0 = j.x; nb.y0 = j.y; nb.cellsX = lev.cellsX(); nb.cellsY = lev.cellsY();
                nb.heights.resize(size_t(nb.cellsX) * nb.cellsY);
                for (int y = 0; y < nb.cellsY; ++y)
                    for (int x = 0; x < nb.cellsX; ++x) nb.heights[size_t(y) * nb.cellsX + x] = lev.heightAt(x, y);
                r.first.push_back(std::move(nb));
            } catch (const std::exception& e) { r.second = j.name + ": " + e.what(); }
        }
        return r;
    });
}

size_t App::fitApply() {
    if (!documentLoaded() || fitFuture_.valid() || fitNeighbours_.empty()) return 0;
    forge::fillerfit::Report rep;
    const size_t n = doc_.fitToNeighbours(fitParams_, fitNeighbours_, &rep);
    fitPreviewKey_.clear();   // "now" is the fitted ground from here on
    if (n) pushLog("fit to neighbours: " + std::to_string(n) + " vertices rebuilt to meet " + std::to_string(fitNeighbours_.size()) + " touching map(s) (one undo step; Write terrain saves it)", 3);
    else pushLog("fit to neighbours: nothing changed", 1);
    return n;
}

namespace {
// a small shaded relief: elevation tint (shared range) x a north-west light
terrainexport::Image reliefImage(const std::vector<float>& h, int cx, int cy, float lo, float hi, int longSide) {
    // the map's own proportions (fillers are often long and thin)
    const int iw = cx >= cy ? longSide : std::max(8, int(float(longSide) * float(cx) / float(cy)));
    const int ih = cy >= cx ? longSide : std::max(8, int(float(longSide) * float(cy) / float(cx)));
    terrainexport::Image img;
    img.width = iw; img.height = ih;
    img.rgba.resize(size_t(iw) * ih * 4);
    const float span = std::max(hi - lo, 1e-3f);
    auto at = [&](float fx, float fy) {
        const int x = std::clamp(int(fx), 0, cx - 1), y = std::clamp(int(fy), 0, cy - 1);
        return h[size_t(y) * cx + x];
    };
    const float sx = float(cx - 1) / float(iw - 1), sy = float(cy - 1) / float(ih - 1);
    for (int y = 0; y < ih; ++y)
        for (int x = 0; x < iw; ++x) {
            const float v = at(x * sx, y * sy);
            const float t = std::clamp((v - lo) / span, 0.0f, 1.0f);
            // low teal -> moss -> sand -> snow
            static const float stops[4][3] = {{38, 64, 78}, {84, 122, 82}, {186, 170, 128}, {238, 236, 228}};
            const float f = t * 3.0f;
            const int k = std::min(2, int(f));
            const float u = f - float(k);
            float c[3];
            for (int i = 0; i < 3; ++i) c[i] = stops[k][i] + (stops[k + 1][i] - stops[k][i]) * u;
            const float dzx = at(x * sx + 1, y * sy) - at(x * sx - 1, y * sy);
            const float dzy = at(x * sx, y * sy + 1) - at(x * sx, y * sy - 1);
            const float shade = std::clamp(0.78f - (dzx + dzy) * 0.06f, 0.45f, 1.15f);
            uint8_t* px = &img.rgba[(size_t(y) * iw + x) * 4];
            for (int i = 0; i < 3; ++i) px[i] = uint8_t(std::clamp(c[i] * shade, 0.0f, 255.0f));
            px[3] = 255;
        }
    return img;
}

// The vanilla fractal preview's colouring (CEditFractal::DrawFractal 0x029a76b0): the first
// HeightColours band (dynamic initializer 0x03ff9550) at or above the fractal height (0..1), flat,
// times 0.75 - atan2(dh, 0.01) for the rise to the next sample diagonally, 0.1 darker on the
// 256-unit world checkerboard, limited to 0..1.
terrainexport::Image fractalImage(const std::vector<float>& h, int cx, int cy, int worldX, int worldY, int longSide) {
    static const struct { float upTo; uint8_t r, g, b; } bands[7] = {
        {0.0001f, 0, 0, 0}, {0.05f, 0, 128, 255}, {0.2f, 0, 255, 255}, {0.4f, 255, 255, 0},
        {0.6f, 0, 255, 0}, {0.8f, 255, 180, 180}, {1.0f, 255, 255, 255}};
    const int iw = cx >= cy ? longSide : std::max(8, int(float(longSide) * float(cx) / float(cy)));
    const int ih = cy >= cx ? longSide : std::max(8, int(float(longSide) * float(cy) / float(cx)));
    terrainexport::Image img;
    img.width = iw; img.height = ih;
    img.rgba.resize(size_t(iw) * ih * 4);
    auto at = [&](int x, int y) { return h[size_t(std::clamp(y, 0, cy - 1)) * cx + std::clamp(x, 0, cx - 1)]; };
    const float sx = float(cx - 1) / float(std::max(iw - 1, 1)), sy = float(cy - 1) / float(std::max(ih - 1, 1));
    for (int y = 0; y < ih; ++y)
        for (int x = 0; x < iw; ++x) {
            const int mx = int(float(x) * sx), my = int(float(y) * sy);
            const float v = at(mx, my);
            uint8_t c[3] = {1, 1, 1};   // above every band: the dialog's (1,1,1)
            for (const auto& b : bands) if (v <= b.upTo) { c[0] = b.r; c[1] = b.g; c[2] = b.b; break; }
            const float dh = (mx + 1 < cx && my + 1 < cy) ? at(mx + 1, my + 1) - v : -v;   // vanilla: 0 past the edge
            float shade = 0.75f - float(std::atan2(double(dh), 0.01));
            if ((((worldX + mx) >> 8) + ((worldY + my) >> 8)) & 1) shade -= 0.1f;
            shade = std::clamp(shade, 0.0f, 1.0f);
            uint8_t* px = &img.rgba[(size_t(y) * iw + x) * 4];
            for (int i = 0; i < 3; ++i) px[i] = uint8_t(std::lround(float(c[i]) * shade));
            px[3] = 255;
        }
    return img;
}
} // namespace

void App::drawFractalWindow() {
    using theme::S;
    if (!beginToolWindow("##fractalwin", "Fractal terrain", "Set this map's ground to the vanilla editor's fractal - sampled at world positions, so maps done one after another meet.", &fractalOpen_, S(620))) return;
    const float inner = toolWindowInner_;
    auto& f = fractal_;
    const float gap = S(18);
    const float leftW = std::floor(inner * 0.52f);
    const float rightW = inner - leftW - gap;
    const float x0 = ImGui::GetCursorPosX();

    // left: the fields, friendly names (the vanilla ones in the tooltips)
    ImGui::BeginGroup();
    {
        const float fieldW = std::floor(leftW * 0.5f);
        auto field = [&](const char* label, const char* id, double& v, double step, double lo, double hi, const char* fmt, const char* tip) {
            ImGui::SetCursorPosX(x0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(theme::vec(theme::Muted), "%s", label);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            ImGui::SameLine(x0 + leftW - fieldW);
            ImGui::SetNextItemWidth(fieldW);
            ImGui::InputDouble(id, &v, step, step * 10.0, fmt);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            v = std::clamp(v, lo, hi);
        };
        field("Height", "##fsc", f.scale, 5.0, 0.001, 2048.0, "%.1f",
              "Scale: the ground is SET to fractal (0..1) x this, in world units.\nRetail ground spans about 0..70; the vanilla default is 1000.");
        field("Feature size", "##fws", f.worldScaler, 0.05, 0.001, 10.0, "%.3f",
              "World scaler: one noise unit spans 4096 x this world units. Bigger = broader hills.");
        field("Layers of detail", "##foct", f.octaves, 1.0, 1.0, 100.0, "%.1f",
              "Octaves: how many finer layers are stacked on the big shapes (fractions blend in).");
        field("Detail spacing", "##flac", f.lacunarity, 0.1, 0.001, 100.0, "%.3f",
              "Lacunarity: how much finer each layer is than the one before (2 = twice as fine).");
        field("Smoothness", "##fdim", f.dimension, 0.01, 0.001, 10.0, "%.3f",
              "Fractal dimension H: layer i weighs lacunarity^(-i x H). Higher = the fine layers count less = smoother.");
        field("Pattern offset X", "##fmx", f.mapX, 250.0, -1e6, 1e6, "%.0f", "Map pos X: slides the pattern; the same offset on every map keeps them continuous.");
        field("Pattern offset Y", "##fmy", f.mapY, 250.0, -1e6, 1e6, "%.0f", "Map pos Y: slides the pattern; the same offset on every map keeps them continuous.");
        ImGui::SetCursorPosX(x0);
        ImGui::Checkbox("Fade out away from the world centre##ffo", &f.useFalloff);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use falloff: full height near (2048, 2048), a cosine fade between the two radii, flat beyond.");
        if (f.useFalloff) {
            field("Fade starts at", "##fst", f.startFalloff, 100.0, 0.0, 10000.0, "%.0f", "Start falloff: world units from the centre where the fade begins.");
            field("Fade ends at", "##fen", f.endFalloff, 100.0, 0.0, 10000.0, "%.0f", "End falloff: world units from the centre where the ground reaches 0.");
            if (f.endFalloff <= f.startFalloff) f.endFalloff = f.startFalloff + 1.0;   // vanilla keeps start < end
        }
    }
    ImGui::EndGroup();

    // right: the preview (relief, the fitted maps' colours), then the actions
    ImGui::SameLine(x0 + leftW + gap);
    ImGui::BeginGroup();
    {
        const int cx = doc_.cellsX(), cy = doc_.cellsY();
        char key[256];
        std::snprintf(key, sizeof key, "%s|%g|%g|%g|%g|%g|%g|%d|%g|%g|%d|%d|%g", doc_.mapName().c_str(), f.lacunarity, f.dimension, f.octaves, f.mapX, f.mapY,
                      f.worldScaler, int(f.useFalloff), f.startFalloff, f.endFalloff, doc_.worldX(), doc_.worldY(), f.scale);
        if (fractalPreviewKey_ != key) {
            fractalPreviewKey_ = key;
            const forge::fractal::Generator gen(f);
            std::vector<float> h(size_t(cx) * cy);
            for (int y = 0; y < cy; ++y)
                for (int x = 0; x < cx; ++x) h[size_t(y) * cx + x] = gen.heightAt(double(doc_.worldX() + x), double(doc_.worldY() + y));
            fractalPreview_ = renderer_.uiTexture("fractal", fractalImage(h, cx, cy, int(doc_.worldX()), int(doc_.worldY()), 160));
        }
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Accent), "This map with it");
        ImGui::PopFont();
        if (fractalPreview_) {
            const float aspect = float(cx) / float(std::max(cy, 1));
            const ImVec2 side = aspect >= 1.0f ? ImVec2(rightW, rightW / aspect) : ImVec2(std::min(rightW, rightW * 1.3f * aspect), std::min(rightW * 1.3f, rightW / aspect));
            ImGui::Image((ImTextureID)(intptr_t)fractalPreview_, side);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The fractal over this map in the vanilla dialog's colours: black at 0, then blue, cyan, yellow, green, pink and white\nby height (steps of 0.2), shaded by slope, with its 256-unit checkerboard.");
        }
        ImGui::Dummy(ImVec2(0, S(6)));
        const std::string go = "Apply to " + doc_.mapName();
        if (theme::primaryButton(go.c_str(), ImVec2(rightW, S(34)))) {
            const size_t n = doc_.applyFractal(f);
            pushLog("fractal: " + std::to_string(n) + " vertices set (one undo step)", n ? 0 : 1);
        }
        auto_.registerWidget("btn_fractal_apply");
        if (theme::ghostButton("Vanilla defaults", ImVec2(rightW, S(24)))) f = forge::fractal::Params{};
    }
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::PushFont(fontSmall_);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
    ImGui::TextColored(theme::vec(theme::Faint), "Ported from the vanilla editor's code (a hybrid multifractal over Perlin noise). It SETS every height, it does not add. One undo step (Ctrl+Z); Write terrain saves it.");
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    endToolWindow();
}

void App::drawFitCard(float pad, float inner, float cardInner) {
    using theme::S;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##fitnb", inner);
    if (theme::ghostButton(fitOpen_ ? "Fit to neighbours  (open)" : "Fit to neighbours...", ImVec2(cardInner, S(26)))) setFitOpen(!fitOpen_);
    auto_.registerWidget("btn_fit_toggle");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rebuild this map as a ridge that meets every map touching it at the seam.\nThe vanilla editor's Fit Neighbours, made for the filler maps between areas.");
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));
}

void App::drawFitWindow() {
    using theme::S;
    const std::string sub = "Rebuild " + doc_.mapName() + " as a ridge that meets every map around it - the vanilla editor's tool for filler maps.";
    if (!beginToolWindow("##fitwin", "Fit to neighbours", sub.c_str(), &fitOpen_, S(640))) return;
    const float cardInner = toolWindowInner_;
    auto close = [&]() { endToolWindow(); };

    if (fitNeighboursFor_ != doc_.mapName() && !fitFuture_.valid()) startFitNeighbourLoad();
    if (fitFuture_.valid() && fitFuture_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        auto r = fitFuture_.get();
        fitNeighbours_ = std::move(r.first);
        if (!r.second.empty()) fitNeighboursNote_ = r.second;
        fitPreviewKey_.clear();
    }
    ImGui::PushFont(fontSmall_);
    if (fitFuture_.valid()) {
        ImGui::TextColored(theme::vec(theme::Muted), "Reading the maps around %s...", doc_.mapName().c_str());
        ImGui::PopFont(); close();
        return;
    }
    if (fitNeighbours_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
        ImGui::TextColored(theme::vec(theme::Faint), "%s", fitNeighboursNote_.empty() ? ("Nothing touches " + doc_.mapName() + " on the world map, so there is no edge to meet.").c_str() : fitNeighboursNote_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont(); close();
        return;
    }
    ImGui::PopFont();
    // the preview: re-fit on a copy whenever a field or the ground changes
    const auto* now = doc_.terrainHeights();
    const int cx = doc_.cellsX(), cy = doc_.cellsY();
    char key[256];
    std::snprintf(key, sizeof key, "%s|%g|%g|%g|%g|%g|%zu|%llu", doc_.mapName().c_str(), fitParams_.peakHeight, fitParams_.step, fitParams_.tension,
                  fitParams_.lowNoise, fitParams_.highNoise, fitNeighbours_.size(), (unsigned long long)doc_.terrainRevision());
    if (now && fitPreviewKey_ != key) {
        fitPreviewKey_ = key;
        fitReport_ = {};
        const auto after = doc_.fittedHeights(fitParams_, fitNeighbours_, &fitReport_);
        fitHasResult_ = !after.empty();
        float lo = 1e9f, hi = -1e9f;
        for (const float v : *now) { lo = std::min(lo, v); hi = std::max(hi, v); }
        for (const float v : after) { lo = std::min(lo, v); hi = std::max(hi, v); }
        fitPreviewNow_ = renderer_.uiTexture("fit_now", reliefImage(*now, cx, cy, lo, hi, 128));
        if (fitHasResult_) fitPreviewAfter_ = renderer_.uiTexture("fit_after", reliefImage(after, cx, cy, lo, hi, 128));
    }

    // two columns: the ground (Now | Fitted) on the left, what shapes it on the right
    const float gap = S(18);
    const float leftW = std::floor(cardInner * 0.48f);
    const float rightW = cardInner - leftW - gap;

    const float colX = ImGui::GetCursorPosX();
    ImGui::BeginGroup();
    {
        const float pgap = S(8);
        const float box = (leftW - pgap) * 0.5f;
        const float aspect = float(cx) / float(std::max(cy, 1));
        // wide maps fill the column; tall ones may stand up to 1.7x taller
        const ImVec2 side = aspect >= 1.0f ? ImVec2(box, box / aspect) : ImVec2(std::min(box, box * 1.7f * aspect), std::min(box * 1.7f, box / aspect));
        auto preview = [&](const char* caption, bool accent, ID3D11ShaderResourceView* tex) {
            ImGui::BeginGroup();
            ImGui::PushFont(fontSmall_);
            ImGui::TextColored(theme::vec(accent ? theme::Accent : theme::Muted), "%s", caption);
            ImGui::PopFont();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, side);
            else ImGui::Dummy(side);
            // the sides that meet a map, lit on the fitted relief
            if (accent && tex) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 q(p.x + side.x, p.y + side.y);
                const float t = S(3);
                if (!fitReport_.north.empty()) dl->AddLine(ImVec2(p.x, p.y + t * 0.5f), ImVec2(q.x, p.y + t * 0.5f), theme::col(theme::Accent), t);
                if (!fitReport_.south.empty()) dl->AddLine(ImVec2(p.x, q.y - t * 0.5f), ImVec2(q.x, q.y - t * 0.5f), theme::col(theme::Accent), t);
                if (!fitReport_.west.empty()) dl->AddLine(ImVec2(p.x + t * 0.5f, p.y), ImVec2(p.x + t * 0.5f, q.y), theme::col(theme::Accent), t);
                if (!fitReport_.east.empty()) dl->AddLine(ImVec2(q.x - t * 0.5f, p.y), ImVec2(q.x - t * 0.5f, q.y), theme::col(theme::Accent), t);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shaded relief, same colours as Now: teal low, snow high.\nThe lit edges meet the maps beside them exactly.");
            }
            ImGui::EndGroup();
        };
        preview("Now", false, fitPreviewNow_);
        ImGui::SameLine(0, pgap);
        preview("Fitted", true, fitHasResult_ ? fitPreviewAfter_ : nullptr);
    }
    ImGui::EndGroup();

    ImGui::SameLine(colX + leftW + gap);
    ImGui::BeginGroup();
    {
        // the four sides, in words
        ImGui::PushFont(fontSmall_);
        auto sideRow = [&](const char* dir, const std::vector<std::string>& names) {
            std::string s;
            for (const auto& n : names) s += (s.empty() ? "" : ", ") + n;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float r = S(3.5f);
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r,
                                                        names.empty() ? theme::col(theme::Border) : theme::col(theme::Accent));
            ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + S(8), p.y));
            ImGui::TextColored(theme::vec(theme::Muted), "%s", dir);
            ImGui::SameLine(S(58));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rightW - S(58));
            ImGui::TextColored(theme::vec(names.empty() ? theme::Faint : theme::Text), "%s", names.empty() ? "open - the spline shapes it" : s.c_str());
            ImGui::PopTextWrapPos();
        };
        const float x0 = ImGui::GetCursorPosX();
        auto row = [&](const char* d, const std::vector<std::string>& n) { ImGui::SetCursorPosX(x0); sideRow(d, n); };
        row("North", fitReport_.north);
        row("East", fitReport_.east);
        row("South", fitReport_.south);
        row("West", fitReport_.west);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(8)));

        auto slider = [&](const char* label, const char* id, float& v, float lo, float hi, const char* fmt, const char* tip) {
            char val[32];
            std::snprintf(val, sizeof val, fmt, v);
            ImGui::SetCursorPosX(x0);
            theme::labelValue(label, val, rightW);
            ImGui::SetCursorPosX(x0);
            ImGui::SetNextItemWidth(rightW);
            ImGui::SliderFloat(id, &v, lo, hi, "");
            auto_.registerWidget(id + 2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        };
        slider("Ridge height", "##fit_peak", fitParams_.peakHeight, 0.0f, 120.0f, "%.1f",
               "How far the ridge rises above the higher of the two edges it spans (world units).\n0 = a smooth slope from edge to edge.");
        slider("Shoulders", "##fit_step", fitParams_.step, 0.0f, 3.0f, "%.2f",
               "Where the slopes sit on the way up: each shoulder is edge + ridge height x this.\nAbove 1 the shoulders stand higher than the ridge line and the top flattens.");
        ImGui::SetCursorPosX(x0);
        ImGui::PushFont(fontSmall_);
        const bool fine = ImGui::TreeNodeEx("Fine-tune##fitfine", fitFineTune_ ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        ImGui::PopFont();
        if (fine) {
            fitFineTune_ = true;
            slider("Curve tension", "##fit_tension", fitParams_.tension, 0.0f, 1.0f, "%.2f",
                   "How the slopes bend between the points (cardinal spline). 0.5 is a classic smooth curve.");
            slider("Ridge bumps", "##fit_lo", fitParams_.lowNoise, 0.0f, 12.0f, "%.1f",
                   "Random whole-unit bumps on the ridge and its shoulders (0 .. value).\nBelow 1 adds none - the vanilla default 1.5 adds up to 1.");
            slider("Surface roughness", "##fit_hi", fitParams_.highNoise, 0.0f, 12.0f, "%.1f",
                   "Random whole-unit roughness on every inside vertex before the final smoothing pass.");
            ImGui::TreePop();
        } else fitFineTune_ = false;
        ImGui::Dummy(ImVec2(0, S(6)));
        const std::string go = "Fit " + doc_.mapName();
        ImGui::SetCursorPosX(x0);
        if (theme::primaryButton(go.c_str(), ImVec2(rightW, S(34)), fitHasResult_)) fitApply();
        auto_.registerWidget("btn_fit_apply");
        ImGui::SetCursorPosX(x0);
        if (theme::ghostButton("Vanilla defaults", ImVec2(rightW, S(24)))) fitParams_ = forge::fillerfit::Params{};
    }
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::PushFont(fontSmall_);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
    ImGui::TextColored(theme::vec(theme::Faint), "Rebuilds the whole map: the edges take the touching maps' heights, open stretches curve between them, and the inside rises to a ridge. Objects stay put (Re-seat them after). One undo step (Ctrl+Z); Write terrain saves it.");
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    close();
}

// ------------------------------------------------------------ budget survey
// The vanilla Surveys > Engine tab (inventory 11b; forge/budget.hpp has its rules):
// things, triangles, vertices and texture memory for an area. Vanilla surveys a
// dragged box or clicked things; here the area is the whole map, the selection, or a
// radius around where the camera looks.

void App::drawBudgetCard(float pad, float inner, float cardInner) {
    using theme::S;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##budgetcard", inner);
    if (theme::ghostButton(budgetOpen_ ? "Budget survey  (open)" : "Budget survey...", ImVec2(cardInner, S(26)))) setBudgetOpen(!budgetOpen_);
    auto_.registerWidget("btn_budget_toggle");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What this map (or part of it) costs to draw: things, triangles, vertices, texture memory.");
    theme::endCard();
}

void App::runBudgetSurvey() {
    namespace bg = forge::budget;
    budgetDirty_ = false;
    if (!documentLoaded()) return;
    float focus[3]; camera_.focus(focus);
    const float cx = focus[0], cy = -focus[2];
    std::set<int> sel;
    if (selectedThing_ >= 0) sel.insert(selectedThing_);
    for (int i : renderer_.alsoSelected) sel.insert(i);
    const float r2 = budgetRadius_ * budgetRadius_;
    auto inScope = [&](float x, float y, int thing) {
        if (budgetScope_ == 1) return thing >= 0 && sel.count(thing) > 0;
        if (budgetScope_ == 2) return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r2;
        return true;
    };
    std::vector<bg::Item> items;
    for (size_t i = 0; i < doc_.thingCount(); ++i) {
        editor::Frame f;
        if (!doc_.frameOf(i, f) || !inScope(f.pos[0], f.pos[1], int(i))) continue;
        const auto summary = doc_.summary(i);
        bg::Item item;
        item.name = summary.definition;
        item.kind = bg::kindOfThingType(summary.type);
        uint32_t id = 0;
        if (ctx_.graphicModelId(summary.definition, id) == 1) item.mesh = id;
        items.push_back(item);
    }
    if (budgetScope_ != 1)
        for (const auto& d : localDetail_)
            if (inScope(d.x, d.y, -1)) items.push_back({d.name, bg::kLocalDetail, d.mesh});
    auto mesh = [](uint32_t id) {
        bg::MeshCost m;
        m.name = foliageexport::meshName(id);
        std::string err;
        const auto* g = foliageexport::cachedMesh(id, err);
        if (!g) return m;
        m.ok = true;
        // what the renderer draws: the collision hull on texture-less materials is not
        // drawn (the rule the preview uses, foliageexport)
        bool anyTextured = false;
        for (const auto& mt : g->materials) if (mt.diffuseTexture > 0 || mt.textureFlags != 0) { anyTextured = true; break; }
        std::vector<char> used(g->vertices.size(), 0);
        for (const auto& t : g->triangles) {
            if (anyTextured && t.material >= 0 && size_t(t.material) < g->materials.size()) {
                const auto& mt = g->materials[size_t(t.material)];
                if (mt.diffuseTexture <= 0 && mt.textureFlags == 0 && mt.bumpTexture <= 0 && mt.reflectionTexture <= 0) continue;
            }
            ++m.triangles;
            for (uint32_t v : {t.a, t.b, t.c}) if (v < used.size() && !used[v]) { used[v] = 1; ++m.vertices; }
        }
        for (const auto& mt : g->materials)
            for (int32_t t : {mt.diffuseTexture, mt.bumpTexture, mt.reflectionTexture, mt.alphaMapTexture})
                if (t > 0) m.textures.push_back(uint32_t(t));
        return m;
    };
    auto texture = [this](uint32_t id) {
        bg::TextureCost t;
        forge::terraintex::TextureInfo info;
        if (!ctx_.textureInfo(id, t.name, info)) return t;
        t.ok = true;
        t.bytes = bg::textureMemory(info);
        t.width = info.allocWidth;
        t.height = info.allocHeight;
        t.format = forge::terraintex::pixelFormatName(info.pixelFormat);
        return t;
    };
    bg::Options o;
    o.include = budgetInclude_;
    o.countAllDuplications = budgetAllDuplicates_;
    budgetReport_ = bg::survey(items, mesh, texture, o);
    budgetHasReport_ = true;
    budgetSaved_.clear();
}

void App::drawBudgetWindow() {
    using theme::S;
    namespace bg = forge::budget;
    if (!beginToolWindow("##budgetwin", "Budget survey",
                         "What this area costs to draw: things, triangles, vertices and texture memory. The vanilla editor's Surveys > Engine tab.",
                         &budgetOpen_, S(560))) return;
    const float inner = toolWindowInner_;
    bool changed = false;

    theme::label("Area");
    changed |= theme::segmented("##budget_scope", budgetScope_, {"Whole map", "Selected", "Around the view"}, inner);
    auto_.registerWidget("seg_budget_scope");
    if (budgetScope_ == 1 && selectedThing_ < 0) theme::hint("Select things in the view (Ctrl+click adds more).");
    if (budgetScope_ == 2) {
        ImGui::SetNextItemWidth(inner - S(96));
        changed |= ImGui::SliderFloat("##budget_radius", &budgetRadius_, 2.0f, 200.0f, "%.0f units");
        ImGui::SameLine();
        if (theme::ghostButton("Update", ImVec2(S(88), 0))) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Survey again around where the camera now looks.");
        theme::hint("Everything within this distance of where the camera looks.");
    }

    ImGui::Dummy(ImVec2(0, S(4)));
    theme::label("Count");
    struct Chip { const char* label; unsigned bit; const char* tip; };
    const Chip chips[] = {{"Buildings", bg::kBuildings, "Buildings"},
                          {"Creatures", bg::kCreatures, "Creatures and villagers"},
                          {"Objects", bg::kObjects, "Objects (props, doors, chests...)"},
                          {"Other things", bg::kOthers, "Markers, holy sites, villages and the rest"},
                          {"Plants", bg::kLocalDetail, "Trees, bushes and grass baked into the map (vanilla: 'local detail')"}};
    for (size_t i = 0; i < sizeof chips / sizeof chips[0]; ++i) {
        if (i) ImGui::SameLine(0, S(6));
        if (theme::chip(chips[i].label, (budgetInclude_ & chips[i].bit) != 0)) { budgetInclude_ ^= chips[i].bit; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", chips[i].tip);
    }
    auto_.registerWidget("chips_budget_include");
    changed |= theme::toggle("Count every copy", &budgetAllDuplicates_);
    auto_.registerWidget("toggle_budget_copies");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: a model used many times counts once (it is loaded once).\nOn: every copy counts (what gets drawn). Vanilla: 'Count all duplications'.");

    if (changed || budgetDirty_ || !budgetHasReport_) runBudgetSurvey();
    const bg::Report& r = budgetReport_;

    // the four numbers
    ImGui::Dummy(ImVec2(0, S(8)));
    const float gap = S(8);
    const float tileW = std::floor((inner - gap * 3) / 4);
    auto tile = [&](const char* label, const std::string& value) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + tileW, a.y + S(58)), theme::col(theme::Bg2), S(8));
        ImGui::PushFont(fontSmall_);
        ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + S(10), a.y + S(8)), theme::col(theme::Muted), label);
        ImGui::PopFont();
        ImGui::PushFont(fontBold_);
        ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + S(10), a.y + S(28)), theme::col(theme::Text), value.c_str());
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(tileW, S(58)));
    };
    auto grouped = [](uint64_t v) {
        const std::string d = std::to_string(v);
        std::string out;
        for (size_t i = 0; i < d.size(); ++i) { if (i && (d.size() - i) % 3 == 0) out += ','; out += d[i]; }
        return out;
    };
    tile("Things", grouped(r.things));
    ImGui::SameLine(0, gap);
    tile("Triangles", grouped(r.triangles));
    ImGui::SameLine(0, gap);
    tile("Vertices", grouped(r.vertices));
    ImGui::SameLine(0, gap);
    tile("Texture memory", bg::formatBytes(r.textureBytes));
    auto_.registerWidget("budget_totals");

    // the breakdown, heaviest first
    ImGui::Dummy(ImVec2(0, S(8)));
    theme::segmented("##budget_view", budgetView_, {"By definition", "By model", "By texture"}, inner);
    auto_.registerWidget("seg_budget_view");
    const std::vector<bg::Line>& lines = budgetView_ == 0 ? r.definitions : budgetView_ == 1 ? r.meshes : r.textures;
    auto valueOf = [&](const bg::Line& l) { return budgetView_ == 0 ? l.count : budgetView_ == 1 ? l.triangles : l.bytes; };
    uint64_t top = 1;
    for (const auto& l : lines) top = std::max<uint64_t>(top, valueOf(l));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
    ImGui::BeginChild("##budget_lines", ImVec2(inner, S(220)), false);
    ImGui::PushFont(fontSmall_);
    const float rowH = ImGui::GetTextLineHeightWithSpacing() + S(2);
    const float valueW = S(120);
    ImGuiListClipper clip;
    clip.Begin(int(lines.size()), rowH);
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const bg::Line& l = lines[size_t(i)];
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const float bar = (w - valueW - S(8)) * float(double(valueOf(l)) / double(top));
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x, a.y + S(1)), ImVec2(a.x + bar, a.y + rowH - S(1)),
                                                      (theme::col(theme::Accent) & 0x00FFFFFFu) | 0x38000000u, S(3));
            // texture entries are named by their source path: show the file name
            std::string name = l.name;
            if (budgetView_ == 2) {
                const size_t slash = name.find_last_of("\\/");
                if (slash != std::string::npos) name = name.substr(slash + 1);
                if (!name.empty() && name.back() == ']') name.pop_back();   // entries read "[...\NAME.TGA]"
            }
            ImGui::GetWindowDrawList()->PushClipRect(a, ImVec2(a.x + w - valueW - S(8), a.y + rowH), true);
            ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + S(6), a.y + S(1)), theme::col(theme::Text), name.c_str());
            ImGui::GetWindowDrawList()->PopClipRect();
            if (name != l.name && ImGui::IsMouseHoveringRect(a, ImVec2(a.x + w, a.y + rowH))) ImGui::SetTooltip("%s", l.name.c_str());
            const std::string value = budgetView_ == 0 ? grouped(l.count) + (l.count == 1 ? " thing" : " things")
                                    : budgetView_ == 1 ? grouped(l.triangles) + " tris" + (l.count > 1 ? "  x" + std::to_string(l.count) : "")
                                                       : bg::formatBytes(l.bytes);
            ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + w - valueW, a.y + S(1)), theme::col(theme::Muted), value.c_str());
            ImGui::Dummy(ImVec2(w, rowH));
        }
    if (lines.empty()) ImGui::TextColored(theme::vec(theme::Faint), "  Nothing in this area.");
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (r.skippedNoGraphic || !r.problems.empty()) {
        std::string note;
        if (r.skippedNoGraphic) note += std::to_string(r.skippedNoGraphic) + " thing(s) have no model (markers, cameras...) and cost nothing to draw. ";
        if (!r.problems.empty()) note += std::to_string(r.problems.size()) + " model(s) or texture(s) could not be read; the saved report lists them.";
        theme::hint(note.c_str());
    }

    ImGui::Dummy(ImVec2(0, S(6)));
    if (theme::ghostButton("Save report", ImVec2(S(140), S(28)))) {
        std::error_code ec;
        const fs::path dir = settings_.outDir;
        fs::create_directories(dir, ec);
        const char* scopeName[] = {"whole map", "selection", "around the view"};
        const fs::path file = dir / (doc_.mapName() + "_budget.txt");
        std::ofstream out(file, std::ios::binary);
        bg::Options o;
        o.include = budgetInclude_;
        o.countAllDuplications = budgetAllDuplicates_;
        out << bg::toText(r, doc_.mapName() + " (" + scopeName[std::clamp(budgetScope_, 0, 2)] + ")", o);
        const bool ok = bool(out);
        budgetSaved_ = ok ? file.string() : std::string();
        pushLog(ok ? "budget report: " + file.string() : "budget report: cannot write " + file.string(), ok ? 3 : 2);
    }
    auto_.registerWidget("btn_budget_save");
    if (!budgetSaved_.empty()) {
        ImGui::SameLine();
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Muted), "Saved to %s", budgetSaved_.c_str());
        ImGui::PopFont();
    }
    endToolWindow();
}


// ------------------------------------------------------------ copy / paste + brush library
// Vanilla's Copy and paste mode (CEditInputProcessCopyPaste) and its Brush library dialog
// (CBrushLibraryDialog; SaveBrush / LoadBrush write CEditMapBrush files: cells with themes by
// name, and things). Here a brush is a small JSON file in the library folder.

bool App::copyRegion(int x0, int y0, int x1, int y1) {
    if (!documentLoaded() || !doc_.hasTerrain()) return false;
    terrainClip_ = doc_.copyTerrain(x0, y0, x1, y1, clipThings_);
    clipTurns_ = 0;
    pushLog("copied " + std::to_string(terrainClip_.w) + " x " + std::to_string(terrainClip_.h) + " vertices of ground" +
            (clipThings_ ? " and " + std::to_string(terrainClip_.things.size()) + " object(s)" : std::string()) + "; Paste region places it", 0);
    return !terrainClip_.empty();
}

size_t App::pasteRegion(int x, int y) {
    if (!documentLoaded()) return 0;
    if (terrainClip_.empty()) { pushLog("paste: copy a region first (Copy region, drag on the ground), or pick a saved brush", 1); return 0; }
    size_t things = 0;
    size_t n = 0;
    try {
        n = doc_.pasteTerrain(terrainClip_, x, y, clipTurns_, clipHeights_, clipThemes_, clipRelative_, clipThings_, &things);
    } catch (const std::exception& e) { pushLog(std::string("paste: ") + e.what(), 2); return 0; }
    pushLog("paste: " + std::to_string(n - things) + " vertices" + (things ? ", " + std::to_string(things) + " object(s)" : std::string()) + " (one undo step)", n ? 0 : 1);
    return n;
}

std::filesystem::path App::brushDir() const {
    return std::filesystem::path(settings_.outDir) / "brushes";
}

bool App::saveBrush(const std::string& rawName) {
    std::string name;
    for (char c : rawName) name += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' ') ? c : '_';
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) { pushLog("brush: give it a name", 1); return false; }
    if (terrainClip_.empty()) { pushLog("brush: copy a region first", 1); return false; }
    std::string err;
    const auto file = brushDir() / (name + ".brush.json");
    if (!editor::saveTerrainClip(terrainClip_, file, err)) { pushLog("brush: " + err, 2); return false; }
    brushListDirty_ = true;
    pushLog("brush saved: " + file.string(), 3);
    return true;
}

bool App::loadBrush(const std::string& name) {
    std::string err;
    editor::TerrainClip clip;
    if (!editor::loadTerrainClip(brushDir() / (name + ".brush.json"), clip, err)) { pushLog("brush: " + err, 2); return false; }
    terrainClip_ = std::move(clip);
    clipTurns_ = 0;
    terrainMode_ = 15;
    pushLog("brush '" + name + "' on the clipboard: click the ground to paste it (R turns it)", 0);
    return true;
}

void App::drawBrushLibrary(float cardInner) {
    using theme::S;
    if (brushListDirty_) {
        brushListDirty_ = false;
        brushList_.clear();
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(brushDir(), ec)) {
            const std::string f = e.path().filename().string();
            const std::string ext = ".brush.json";
            if (f.size() > ext.size() && f.compare(f.size() - ext.size(), ext.size(), ext) == 0) brushList_.push_back(f.substr(0, f.size() - ext.size()));
        }
        std::sort(brushList_.begin(), brushList_.end());
    }
    ImGui::Dummy(ImVec2(0, S(6)));
    theme::label("Brush library");
    const float btnW = S(72);
    ImGui::SetNextItemWidth(cardInner - btnW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##brushname", "Name for the current copy", brushName_, sizeof brushName_);
    auto_.registerWidget("input_brush_name");
    ImGui::SameLine();
    if (theme::ghostButton("Save##brushsave", ImVec2(btnW, 0))) saveBrush(brushName_);
    auto_.registerWidget("btn_brush_save");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(terrainClip_.empty() ? "Copy a region first." : "Keep the current copy as a brush you can paste on any map.");
    ImGui::PushFont(fontSmall_);
    if (brushList_.empty()) theme::hint("No saved brushes yet. Copy a region, name it, Save.");
    for (const auto& b : brushList_) {
        if (ImGui::Selectable((b + "##brush_" + b).c_str(), false)) loadBrush(b);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put this brush on the clipboard and switch to Paste region.");
    }
    ImGui::PopFont();
    auto_.registerWidget("list_brushes");
}

} // namespace albion::gui
