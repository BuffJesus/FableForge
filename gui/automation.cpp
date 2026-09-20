// The automation runner: `FableForge.exe --auto <script.txt>` drives the app one command
// per frame (docs/AUTOMATION.md lists the commands and the state keys). This is the
// dispatcher; the app-side helpers it calls live in app.cpp / editor.cpp / world.cpp /
// textures.cpp.
#include "app.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "theme.hpp"

namespace fs = std::filesystem;

namespace albion::gui {
namespace {
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

void Automation::load(const std::string& scriptPath) {
    std::ifstream f(scriptPath);
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        lines_.push_back(line);
    }
    active_ = true;
    logPath_ = scriptPath + ".log";
    deadline_ = 0;
    t0_ = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Automation::registerWidget(const char* id) {
    if (!active_) return;
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    widgets_[id] = ImVec4(a.x, a.y, b.x, b.y);
}

bool Automation::takeScreenshot(std::string& path) {
    if (pendingShot_.empty()) return false;
    path = pendingShot_;
    pendingShot_.clear();
    return true;
}

void Automation::fail(const std::string& why) { failures_.push_back(why); note("FAIL " + why); }

void Automation::note(const std::string& what) {
    log_.push_back(what);
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    char stamp[16];
    std::snprintf(stamp, sizeof stamp, "%7.2f ", now - t0_);
    std::ofstream(logPath_, std::ios::app) << stamp << what << "\n";
}

bool Automation::tick(App& app) {
    if (!active_ || quit_) return !quit_;
    // Pending synthetic click: move, press, release over three frames.
    if (!clickTarget_.empty()) {
        auto it = widgets_.find(clickTarget_);
        if (it == widgets_.end()) { fail("click: widget not on screen: " + clickTarget_); clickTarget_.clear(); return true; }
        ImGuiIO& io = ImGui::GetIO();
        const ImVec4 r = it->second;
        setVirtualMouse((r.x + r.z) * 0.5f, (r.y + r.w) * 0.5f);
        io.AddMousePosEvent(vmX_, vmY_);
        if (clickPhase_ == 1) io.AddMouseButtonEvent(0, true);
        if (clickPhase_ == 2) io.AddMouseButtonEvent(0, false);
        if (++clickPhase_ > 3) { clickTarget_.clear(); clickPhase_ = 0; }
        return true;
    }
    if (dragPhase_ > 0) {
        // drag_gizmo: press on the selected pivot, glide by (dx, dy) over a few frames, release
        ImGuiIO& io = ImGui::GetIO();
        const int glide = 8;
        if (dragPhase_ == 1) { if (!app.selectedPivotScreen(dragX0_, dragY0_)) { fail("drag_gizmo: no selected pivot on screen"); dragPhase_ = 0; return true; } setVirtualMouse(dragX0_, dragY0_); }
        else if (dragPhase_ == 2) io.AddMouseButtonEvent(0, true);
        else if (dragPhase_ <= 2 + glide) { const float t = float(dragPhase_ - 2) / float(glide); setVirtualMouse(dragX0_ + dragDx_ * t, dragY0_ + dragDy_ * t); }
        else if (dragPhase_ == 3 + glide) io.AddMouseButtonEvent(0, false);
        io.AddMousePosEvent(vmX_, vmY_);
        if (++dragPhase_ > 4 + glide) dragPhase_ = 0;
        return true;
    }
    if (waitFrames_ > 0) { --waitFrames_; return true; }
    if (pc_ >= lines_.size()) { quit_ = true; return false; }

    std::string line = lines_[pc_];
    for (const char* var : {"TEMP", "USERPROFILE"}) {
        const std::string key = std::string("${") + var + "}";
        const char* val = std::getenv(var);
        for (size_t at; (at = line.find(key)) != std::string::npos;) line.replace(at, key.size(), val ? val : "");
    }
    std::istringstream ss(line);
    std::string cmd; ss >> cmd;
    std::string rest; std::getline(ss, rest);
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (deadline_ == 0) deadline_ = now + 60.0;   // generous: cold texture loads on slow disks
    auto waitOn = [&](bool done, const char* what) {
        if (done) { note("ok   " + line); ++pc_; deadline_ = 0; }
        else if (now > deadline_) { fail(std::string("timeout waiting for ") + what + " (" + line + ")"); ++pc_; deadline_ = 0; }
    };

    if (cmd.rfind("wait_", 0) != 0) deadline_ = 0;
    if (cmd == "wait_ready") waitOn(app.mapsReady() && !app.contextBusy(), "install + textures");
    else if (cmd == "wait_maps") waitOn(app.mapsReady(), "map list");
    else if (cmd == "wait_loaded") waitOn(app.previewLoaded() && !app.previewBusy(), "preview");
    else if (cmd == "wait_export") waitOn(!app.exportBusy(), "export");
    else if (cmd == "wait_foliage") waitOn(app.foliageLoaded() && !app.foliageBusy(), "foliage");
    else if (cmd == "frames") { waitFrames_ = std::max(1, std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }
    else if (cmd == "select") { app.selectMap(rest); note("ok   " + line); ++pc_; }
    else if (cmd == "filter") { app.setFilter(rest); note("ok   " + line); ++pc_; }
    else if (cmd == "click") { clickTarget_ = rest; clickPhase_ = 0; note("..   " + line); ++pc_; }
    else if (cmd == "orbit") { float a = 0, b = 0; std::istringstream(rest) >> a >> b; app.camera().orbit(a, b); note("ok   " + line); ++pc_; }
    else if (cmd == "zoom") { app.camera().dolly(float(std::atof(rest.c_str()))); note("ok   " + line); ++pc_; }
    else if (cmd == "fly") {   // fly <forward> <strafe> <rise> <seconds>
        float f = 0, st = 0, r = 0, secs = 1; std::istringstream(rest) >> f >> st >> r >> secs;
        app.camera().fly(f, st, r, secs); note("ok   " + line); ++pc_;
    }
    else if (cmd == "mouse_move") {   // mouse_move <x> <y>  (window pixels) | mouse_move viewport
        ImGuiIO& io = ImGui::GetIO();
        if (widgets_.count(rest)) {   // a registered widget (or "viewport"): its centre
            const ImVec4 r = widgets_[rest];
            setVirtualMouse((r.x + r.z) * 0.5f, (r.y + r.w) * 0.5f);
        } else if (rest.empty() || !std::isdigit(static_cast<unsigned char>(rest[0]))) { fail("mouse_move: widget not on screen: " + rest); ++pc_; return true; }
        else { float x = 0, y = 0; std::istringstream(rest) >> x >> y; setVirtualMouse(x, y); }
        io.AddMousePosEvent(vmX_, vmY_);
        note("ok   " + line); ++pc_; waitFrames_ = 1;
    }
    else if (cmd == "wheel") {   // wheel <dy>: mouse wheel at the scripted mouse position
        ImGui::GetIO().AddMouseWheelEvent(0.0f, float(std::atof(rest.c_str())));
        note("ok   " + line); ++pc_; waitFrames_ = 1;
    }
    else if (cmd == "mouse_down" || cmd == "mouse_up") {
        const int btn = rest == "right" ? 1 : rest == "middle" ? 2 : 0;
        ImGui::GetIO().AddMouseButtonEvent(btn, cmd == "mouse_down");
        note("ok   " + line); ++pc_; waitFrames_ = 1;
    }
    else if (cmd == "mouse_delta") {   // mouse_delta <dx> <dy>: move relative to the current position
        ImGuiIO& io = ImGui::GetIO();
        float x = 0, y = 0; std::istringstream(rest) >> x >> y;
        setVirtualMouse(vmX_ + x, vmY_ + y);
        io.AddMousePosEvent(vmX_, vmY_);
        note("ok   " + line); ++pc_; waitFrames_ = 1;
    }
    else if (cmd == "key_down" || cmd == "key_up") {
        static const std::map<std::string, ImGuiKey> keys = {
            {"W", ImGuiKey_W}, {"A", ImGuiKey_A}, {"S", ImGuiKey_S}, {"D", ImGuiKey_D}, {"Q", ImGuiKey_Q},
            {"E", ImGuiKey_E}, {"F", ImGuiKey_F}, {"Shift", ImGuiKey_LeftShift}, {"Alt", ImGuiKey_LeftAlt},
            {"Ctrl", ImGuiKey_LeftCtrl}, {"Escape", ImGuiKey_Escape}};
        auto it = keys.find(rest);
        if (it == keys.end()) fail("unknown key " + rest);
        else {
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(it->second, cmd == "key_down");
            if (it->second == ImGuiKey_LeftShift) io.AddKeyEvent(ImGuiMod_Shift, cmd == "key_down");
            if (it->second == ImGuiKey_LeftAlt) io.AddKeyEvent(ImGuiMod_Alt, cmd == "key_down");
            if (it->second == ImGuiKey_LeftCtrl) io.AddKeyEvent(ImGuiMod_Ctrl, cmd == "key_down");
            note("ok   " + line);
        }
        ++pc_; waitFrames_ = 1;
    }
    else if (cmd == "snapshot_camera") { const Camera& c = app.camera(); camSnap_[0] = c.posX; camSnap_[1] = c.posY; camSnap_[2] = c.posZ; note("ok   " + line); ++pc_; }
    else if (cmd == "assert_camera_moved") {
        const Camera& c = app.camera();
        const float d = std::sqrt((c.posX - camSnap_[0]) * (c.posX - camSnap_[0]) + (c.posY - camSnap_[1]) * (c.posY - camSnap_[1]) + (c.posZ - camSnap_[2]) * (c.posZ - camSnap_[2]));
        const float minD = rest.empty() ? 0.01f : float(std::atof(rest.c_str()));
        if (d < minD) fail("camera did not move enough: " + std::to_string(d)); else note("ok   " + line + "  (moved " + std::to_string(d) + ")");
        ++pc_;
    }
    else if (cmd == "look") { float a = 0, b = 0; std::istringstream(rest) >> a >> b; app.camera().look(a, b); note("ok   " + line); ++pc_; }
    else if (cmd == "camera") {   // camera <fableX> <fableY> <fableZ> <yaw> <pitch> <distance>  (target in Fable map-local coords)
        float fx = 0, fy = 0, fz = 0, yaw = 0.8f, pitch = 0.6f, dist = 30;
        std::istringstream(rest) >> fx >> fy >> fz >> yaw >> pitch >> dist;
        app.camera().lookAt(fx, fz, -fy, yaw, pitch, dist);
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "mode") {
        const std::string m = lower(rest);
        app.setMode(m == "wireframe" ? ViewMode::Wireframe : m == "walkable" ? ViewMode::Walkable : m == "height" ? ViewMode::Height : ViewMode::Textured);
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "set") {
        std::istringstream rs(rest); std::string key, val; rs >> key; std::getline(rs, val);
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());
        ExportSettings& s = app.settings();
        if (key == "format") s.format = lower(val) == "obj" ? 1 : 0;
        else if (key == "textures") s.textures = val == "1";
        else if (key == "layers") s.layers = val == "1";
        else if (key == "walkable") s.walkable = val == "1";
        else if (key == "foliage") s.foliage = val == "1";
        else if (key == "preview_foliage") app.setPreviewFoliage(val == "1");
        else if (key == "things") s.things = val == "1";
        else if (key == "water") s.water = val == "1";
        else if (key == "creatures") s.creatures = val == "1";
        else if (key == "uiscale") s.uiScale = std::clamp(float(std::atof(val.c_str())), 0.8f, 1.5f);
        else if (key == "texsize") s.texSize = std::atoi(val.c_str());
        else if (key == "world") s.world = val == "1";
        else if (key == "preview_things") app.setPreviewThings(val == "1");
        else if (key == "preview_water") app.setPreviewWater(val == "1");
        else if (key == "texels") s.texels = std::atoi(val.c_str());
        else if (key == "tile") s.tile = float(std::atof(val.c_str()));
        else if (key == "gain") { s.gain = float(std::atof(val.c_str())); app.previewLoadedFor_.clear(); app.startPreviewLoad(); }
        else if (key == "up") s.up = lower(val) == "z" ? 1 : 0;
        else if (key == "outdir") { s.outDir = val; std::snprintf(app.outDirBuf_, sizeof app.outDirBuf_, "%s", val.c_str()); }
        else if (key == "saveroot") app.setSaveRoot(val);
        else if (key == "unsaved_prompt") app.promptInAuto_ = val == "1";
        else fail("set: unknown key " + key);
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "edit") { app.setEditMode(rest == "1" || rest == "on"); note("ok   " + line); ++pc_; }
    else if (cmd == "textures_tab") { app.setTexturesMode(rest == "1"); note("ok   " + line); ++pc_; }
    else if (cmd == "texture_select") { if (!app.selectTexture(rest)) fail("texture_select: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "texture_export") { if (!app.exportSelectedTexture(rest)) fail("texture_export failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "texture_replace") { if (!app.replaceSelectedTexture(rest)) fail("texture_replace failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "texture_add") {   // texture_add <NAME> <image> [dxt1|dxt3|argb8888]
        std::istringstream rs(rest); std::string n, img, fmt; rs >> n >> img >> fmt;
        if (!app.addTexture(n, img, "GBANK_MAIN_PC", fmt)) fail("texture_add failed: " + rest); else note("ok   " + line); ++pc_;
    }
    else if (cmd == "world_tab") { app.setWorldMode(rest == "1" || rest == "on"); note("ok   " + line); ++pc_; }
    else if (cmd == "world_select") { app.worldSelect(rest); if (app.worldSelected().empty()) fail("world_select: no map " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "world_move") {   // world_move <map> <x> <y>: queue a move (refused moves fail the script)
        std::istringstream rs(rest); std::string m; int x = 0, y = 0; rs >> m >> x >> y;
        if (!app.worldMove(m, x, y)) fail("world_move refused: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "world_move_refused") {   // expects the move to be refused
        std::istringstream rs(rest); std::string m; int x = 0, y = 0; rs >> m >> x >> y;
        if (app.worldMove(m, x, y)) fail("world_move_refused: the move was accepted: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "world_owner") { std::istringstream rs(rest); std::string m, r; rs >> m >> r; if (!app.worldSetOwner(m, r)) fail("world_owner refused: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "world_sees") { std::istringstream rs(rest); std::string r, m; int v = 1; rs >> r >> m >> v; if (!app.worldSetSees(r, m, v != 0)) fail("world_sees refused: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "world_revert") { app.worldRevert(); note("ok   " + line); ++pc_; }
    else if (cmd == "world_undo") { if (!app.worldUndo()) fail("world_undo: nothing to undo"); else note("ok   " + line); ++pc_; }
    else if (cmd == "world_redo") { if (!app.worldRedo()) fail("world_redo: nothing to redo"); else note("ok   " + line); ++pc_; }
    else if (cmd == "world_stitch") {   // world_stitch <0|1> [feather]: stitch seams after the next apply
        std::istringstream rs(rest); int on = 1, feather = -1; rs >> on >> feather;
        app.setWorldStitch(on != 0, feather); note("ok   " + line); ++pc_;
    }
    else if (cmd == "world_apply") { app.worldApply(); note("..   " + line); ++pc_; }
    else if (cmd == "wait_world") waitOn(!app.worldBusy(), "world move");
    else if (cmd == "gizmo") { app.setGizmoOp(std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }
    else if (cmd == "pick") { float u = 0, v = 0; std::istringstream(rest) >> u >> v; const int t = app.pickAt(u, v); note("ok   " + line + " -> thing " + std::to_string(t)); ++pc_; }
    else if (cmd == "set_thing_prop") { std::istringstream rs(rest); std::string key, val; rs >> key; std::getline(rs, val); while (!val.empty() && val.front() == ' ') val.erase(val.begin()); if (app.selectedThing() >= 0) { app.document().setProperty(size_t(app.selectedThing()), key, val); note("ok   " + line); } else fail("set_thing_prop: nothing selected"); ++pc_; }
    else if (cmd == "select_def") { const int t = app.selectByDefinition(rest); if (t < 0) fail("select_def: not found " + rest); else note("ok   " + line + " -> " + std::to_string(t)); ++pc_; }
    else if (cmd == "select_thing") { app.selectThing(std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }
    else if (cmd == "select_added") {   // select the first thing the Changes list reports as added (uid from "... (uid N)")
        int hit = -1;
        for (const auto& c : app.document().changes()) {
            if (c.rfind("added ", 0) != 0) continue;
            const auto p = c.rfind("(uid ");
            if (p == std::string::npos) continue;
            const uint64_t uid = std::strtoull(c.c_str() + p + 5, nullptr, 10);
            if (const auto i = app.document().indexOfUid(uid)) { hit = int(*i); break; }
        }
        if (hit < 0) fail("select_added: nothing added"); else { app.selectThing(hit); note("ok   " + line); }
        ++pc_;
    }
    else if (cmd == "set_entrance") { if (!app.setEntranceHere()) fail("set_entrance failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "place_emitter") { std::istringstream rs(rest); std::string fx, sn; rs >> fx >> sn; if (!app.placeEmitter(fx, sn)) fail("place_emitter failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "preset_place") { if (!app.placePreset(rest)) fail("preset_place failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "preset_save") { if (!app.savePresetFromSelection(rest, "")) fail("preset_save failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "select_toggle") { app.toggleSelect(std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }   // Ctrl+click on a thing index
    else if (cmd == "copy") { app.copySelection(); note("ok   " + line); ++pc_; }
    else if (cmd == "paste") { app.pasteClipboard(); note("ok   " + line); ++pc_; }
    else if (cmd == "move_thing") { float x = 0, y = 0, z = 0; std::istringstream(rest) >> x >> y >> z; app.moveSelected(x, y, z); note("ok   " + line); ++pc_; }
    else if (cmd == "rotate_thing") { app.rotateSelected(float(std::atof(rest.c_str()))); note("ok   " + line); ++pc_; }
    else if (cmd == "scale_thing") { app.scaleSelected(float(std::atof(rest.c_str()))); note("ok   " + line); ++pc_; }
    else if (cmd == "ground_thing") { app.snapSelectedToGround(); note("ok   " + line); ++pc_; }
    else if (cmd == "reseat_things") { app.reseatThings(); note("ok   " + line); ++pc_; }
    else if (cmd == "add_theme") { if (!app.addPaintTheme(rest)) fail("add_theme failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "custom_theme") {   // custom_theme <png> <NAME> [donor] [cliffPng]
        std::istringstream rs(rest); std::string png, nm, donor, cliff; rs >> png >> nm >> donor >> cliff;
        if (!app.createCustomTheme(png, nm, donor, cliff)) fail("custom_theme failed: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "duplicate_thing") { app.duplicateSelected(); note("ok   " + line); ++pc_; }
    else if (cmd == "delete_thing") { app.deleteSelected(); note("ok   " + line); ++pc_; }
    else if (cmd == "undo") { app.editUndo(); note("ok   " + line); ++pc_; }
    else if (cmd == "tour") { app.setTourStep(std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }   // tour <0..2 | -1>
    else if (cmd == "help") { app.setHelpOpen(rest == "1"); note("ok   " + line); ++pc_; }
    else if (cmd == "def_search") { app.setDefSearch(rest); note("ok   " + line); ++pc_; }   // the "Add an object" search box
    else if (cmd == "theme_search") { app.setThemeSearch(rest); note("ok   " + line); ++pc_; }   // the "Add a ground theme from the game" box
    else if (cmd == "edit_tab") { app.setEditTab(std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }   // 0 Objects 1 Terrain 2 Actors 3 Level
    else if (cmd == "dismiss_rule") { app.dismissRule(rest); note("ok   " + line); ++pc_; }   // what the notice's "Got it" does (the notice may sit below the panel fold)
    else if (cmd == "redo") { app.editRedo(); note("ok   " + line); ++pc_; }
    else if (cmd == "place") {   // place <DEFINITION> [scriptname]
        std::istringstream rs(rest); std::string def, sn; rs >> def >> sn;
        if (!app.placeDefinition(def, sn)) fail("place failed: " + rest); else note("ok   " + line); ++pc_;
    }
    else if (cmd == "compact_stb") { if (!app.compactBank()) fail("compact_stb failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "wait_compact") waitOn(!app.compactBusy(), "compaction");
    else if (cmd == "restore_all") { if (!app.restoreAllBackups()) fail("restore failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "setup") { app.setupOpen_ = std::atoi(rest.c_str()) != 0; note("ok   " + line); ++pc_; }
    else if (cmd == "link_install") { if (!app.linkInstall()) fail("link_install failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "link_remove") { if (!app.linkRemove()) fail("link_remove failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "link_go") { if (!app.linkGoHere()) fail("link_go failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "link_spawn") { if (!app.linkSpawnSelected()) fail("link_spawn failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "link_reload") { if (!app.linkReload()) fail("link_reload failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "link_ping") { if (!app.linkPing()) fail("link_ping failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "link_poll") { app.linkPoll(true); note("ok   " + line); ++pc_; }
    else if (cmd == "place_village") {   // place_village <VILLAGE_DEF> [scriptname]
        std::istringstream rs(rest); std::string def, sn; rs >> def >> sn;
        if (!app.placeVillage(def, sn)) fail("place_village failed: " + rest); else note("ok   " + line); ++pc_;
    }
    else if (cmd == "village_member") {   // village_member <uid|scriptname|0>: the selected thing joins/leaves that village
        uint64_t uid = std::strtoull(rest.c_str(), nullptr, 10);
        if (!uid && rest != "0") for (const auto& v : app.doc_.villages()) if (v.scriptName == rest || v.definition == rest) uid = v.uid;
        if ((!uid && rest != "0") || !app.setSelectedVillage(uid)) fail("village_member failed: " + rest); else note("ok   " + line); ++pc_;
    }
    else if (cmd == "place_fishing_spot") {   // place_fishing_spot [OBJECT_DEF|-] [scriptname]  (the first catch; empty or - = the game's table)
        std::istringstream rs(rest); std::string reward, sn; rs >> reward >> sn; if (reward == "-") reward.clear();
        if (!app.placeFishingSpot(reward, sn)) fail("place_fishing_spot failed: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "place_spawner") {   // place_spawner <radius> <limit> <FAMILY[,FAMILY...]> [scriptname]
        std::istringstream rs(rest); float radius = 12; int limit = 3; std::string fams, sn; rs >> radius >> limit >> fams >> sn;
        std::vector<std::string> families; std::string cur;
        for (char c : fams) { if (c == ',') { if (!cur.empty()) families.push_back(cur); cur.clear(); } else cur += c; }
        if (!cur.empty()) families.push_back(cur);
        if (!app.placeSpawner(families, radius, limit, sn)) fail("place_spawner failed: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "save_level") { if (!app.saveDocument()) fail("save failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "deploy_level") { if (!app.deployDocument()) fail("deploy failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "drag_gizmo") { std::istringstream(rest) >> dragDx_ >> dragDy_; dragPhase_ = 1; note("..   " + line); ++pc_; }
    else if (cmd == "terrain_mode") { app.setTerrainMode(std::atoi(rest.c_str())); app.setGizmoOp(4); note("ok   " + line); ++pc_; }
    else if (cmd == "paint_theme") { app.setPaintTheme(std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }
    else if (cmd == "new_level") {   // new_level <name> <x> <y> [region]: fill the card and start the install
        std::istringstream rs(rest); std::string name, region; int x = 0, y = 0; rs >> name >> x >> y >> region;
        app.setNewLevel(name, x, y, region); app.startNewLevel(); note("..   " + line); ++pc_; }
    else if (cmd == "new_level_own_region") { app.setNewLevelOwnRegion(std::atoi(rest.c_str()) != 0); note("ok   " + line); ++pc_; }
    else if (cmd == "wait_new_level") { if (!app.newLevelBusy()) { note("ok   " + line); ++pc_; } }
    else if (cmd == "new_level_blank") {   // new_level_blank <theme slot> <height>: the next new_level makes a blank level
        std::istringstream rs(rest); int theme = -1; float h = 20; int w = 0, hh = 0; rs >> theme >> h >> w >> hh;
        app.setNewLevelBlank(theme, h, w, hh); note("ok   " + line); ++pc_; }
    else if (cmd == "brush") { float r = 6, s = 4; std::istringstream(rest) >> r >> s; app.setBrush(r, s); note("ok   " + line); ++pc_; }
    else if (cmd == "terrain_stroke") { float x = 0, y = 0, sec = 1; std::istringstream(rest) >> x >> y >> sec; app.terrainStroke(x, y, sec); note("ok   " + line); ++pc_; }
    else if (cmd == "deploy_terrain") { app.deployTerrain(); note("..   " + line); ++pc_; }
    else if (cmd == "wait_terrain") waitOn(!app.terrainDeployBusy(), "terrain deploy");
    else if (cmd == "frame_selected") { app.frameSelected(); note("ok   " + line); ++pc_; }
    else if (cmd == "export") { app.startExport(); note("ok   " + line); ++pc_; }
    else if (cmd == "export_all") { app.startBatchExport(app.visibleMapNames()); note("ok   " + line); ++pc_; }
    else if (cmd == "export_region") {
        const MapEntry* e = app.findEntry(app.selectedName());
        if (!e) fail("export_region: nothing selected");
        else { app.settings().world = true; app.startBatchExport(app.regionMapKeys(e->group)); note("ok   " + line); }
        ++pc_;
    }
    else if (cmd == "wait_batch") waitOn(!app.batchActive() && !app.exportBusy(), "batch export");
    else if (cmd == "drop") { if (!app.openDropped(rest)) fail("drop failed: " + rest); else note("ok   " + line); ++pc_; }   // what a file dropped on the window does
    else if (cmd == "open") { if (!app.openLooseLev(rest)) fail("open failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "screenshot") { pendingShot_ = rest; note("ok   " + line); ++pc_; waitFrames_ = 1; }
    else if (cmd == "assert_file") {
        std::error_code ec;
        if (!fs::exists(rest, ec) || fs::file_size(rest, ec) == 0) fail("missing or empty file: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "assert_file_contains") {   // assert_file_contains <path> <text...>  (the text is the rest of the line)
        std::istringstream rs(rest); std::string path; rs >> path;
        std::string text; std::getline(rs, text);
        if (!text.empty() && text.front() == ' ') text.erase(0, 1);
        std::ifstream in(path, std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!in && body.empty()) fail("cannot read " + path);
        else if (body.find(text) == std::string::npos) fail("file " + path + " does not contain: " + text);
        else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "assert_state") {
        std::istringstream rs(rest); std::string key, val; rs >> key >> val;
        bool found = false, match = false;
        for (const auto& kv : app.stateDump()) {
            if (kv.rfind(key + "=", 0) == 0) { found = true; match = kv.substr(key.size() + 1) == val; }
        }
        if (!found) fail("assert_state: unknown key " + key);
        else if (!match) { std::string cur; for (const auto& kv : app.stateDump()) if (kv.rfind(key + "=", 0) == 0) cur = kv; fail("assert_state: " + cur + " != " + val); }
        else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "assert_log") {   // assert_log <text>: some app log line contains the text
        if (!app.logContains(rest)) fail("assert_log: no log line contains '" + rest + "'"); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "assert_widget") {
        if (!widgets_.count(rest)) fail("widget not on screen: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "dump_state") { for (const auto& kv : app.stateDump()) note("     " + kv); ++pc_; }
    else if (cmd == "dump_log") { for (const auto& [lvl, ln] : app.log_) note("     log: " + ln); ++pc_; }
    else if (cmd == "quit") { quit_ = true; note("ok   quit"); return false; }
    else { fail("unknown command: " + line); ++pc_; }
    return true;
}

} // namespace albion::gui
