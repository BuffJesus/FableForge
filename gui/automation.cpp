// The automation runner: `FableForge.exe --auto <script.txt>` drives the app one command
// per frame (docs/AUTOMATION.md lists the commands and the state keys). This is the
// dispatcher; the app-side helpers it calls live in app.cpp / editor.cpp / world.cpp /
// textures.cpp.
#include "app.hpp"
#include "dialogueaudio.hpp"
#include "profile.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
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
    if (!revealTarget_.empty() && revealTarget_ == id) {
        // cards are child windows inside the scrolling panel: bring the item into view in each window up
        // the chain, moving its rect by every scroll already requested (ScrollToItem alone stops at the first)
        // the first window up the chain that can scroll: centre the item in it
        for (ImGuiWindow* w = ImGui::GetCurrentWindow(); w; w = (w->Flags & ImGuiWindowFlags_ChildWindow) ? w->ParentWindow : nullptr) {
            if (w->ScrollMax.y < 1.0f) continue;   // an auto-sized card reports a float crumb
            const float centre = (a.y + b.y) * 0.5f;
            ImGui::SetScrollY(w, std::clamp(w->Scroll.y + centre - (w->Pos.y + w->Size.y * 0.5f), 0.0f, w->ScrollMax.y));
            break;
        }
        revealTarget_.clear();
    }
}

bool Automation::takeScreenshot(std::string& path) {
    if (!captureShot_.empty()) { path = std::move(captureShot_); captureShot_.clear(); return true; }
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
    if (!worldCoverageWatch_.empty()) {
        float detail=0;
        if (const auto found=app.worldDetailShown_.find(worldCoverageWatch_);found!=app.worldDetailShown_.end()) {
            const float progress=app.worldDetailFadeOverride_.value_or(found->second.fade);
            detail=app.worldSmoothObjects_ ? worldview::DetailFade::objects(progress) : progress;
        }
        const float coarse=app.worldScenery_.preparedCoverage(worldCoverageWatch_);
        ++worldCoverageWatchFrames_;
        if (detail+(1-detail)*coarse < 0.999f) {
            fail("scenery handoff coverage gap for " + worldCoverageWatch_ + ": detail=" + std::to_string(detail) + " coarse=" + std::to_string(coarse));
            worldCoverageWatch_.clear();
        }
    }
    // App::frame has already rendered: record its pose before this tick changes it.
    if (!capturePrefix_.empty()) {
        char suffix[32];
        std::snprintf(suffix, sizeof suffix, "-%05d.png", captureFrame_++);
        captureShot_ = capturePrefix_ + suffix;
        note("capture " + captureShot_);
        for (const auto& kv : app.stateDump())
            if (kv.starts_with("world_")) note("     " + kv);
        if (captureFrame_ >= 2000) { fail("capture exceeded 2000 frames"); capturePrefix_.clear(); }
    }
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
    if (deadline_ == 0) deadline_ = now + (cmd == "wait_world_tiles" ? 900.0 : cmd == "wait_mods" ? 600.0 : cmd == "wait_file_job" ? 180.0 : 60.0);   // whole-install mod builds can outlast ordinary preview loads
    auto waitOn = [&](bool done, const char* what) {
        if (done) { note("ok   " + line); ++pc_; deadline_ = 0; }
        else if (now > deadline_) { fail(std::string("timeout waiting for ") + what + " (" + line + ")"); ++pc_; deadline_ = 0; }
    };

    if (cmd.rfind("wait_", 0) != 0) deadline_ = 0;
    if (cmd == "wait_ready") waitOn(app.mapsReady() && !app.contextBusy(), "install + textures");
    else if (cmd == "wait_maps") waitOn(app.mapsReady(), "map list");
    else if (cmd == "wait_loaded") waitOn(app.previewLoaded() && !app.previewBusy(), "preview");
    else if (cmd == "wait_neighbours") waitOn(app.showNeighbours_ && !app.neighbourFuture_.valid() &&
        app.neighboursFor_ == app.selectedName_, "neighbour previews");
    else if (cmd == "wait_export") waitOn(!app.exportBusy(), "export");
    else if (cmd == "wait_foliage") waitOn(app.foliageLoaded() && !app.foliageBusy(), "foliage");
    else if (cmd == "frames") { waitFrames_ = std::max(1, std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }
    else if (cmd == "select") { app.selectMap(rest); note("ok   " + line); ++pc_; }
    else if (cmd == "filter") { app.setFilter(rest); note("ok   " + line); ++pc_; }
    else if (cmd == "reveal") { revealTarget_ = rest; note("ok   " + line); ++pc_; waitFrames_ = 2; }   // scroll the widget's panel to it (for screenshots)
    else if (cmd == "click") { clickTarget_ = rest; clickPhase_ = 0; note("..   " + line); ++pc_; }
    else if (cmd == "orbit") { float a = 0, b = 0; std::istringstream(rest) >> a >> b; app.camera().orbit(a, b); note("ok   " + line); ++pc_; }
    else if (cmd == "zoom") { app.camera().dolly(float(std::atof(rest.c_str()))); note("ok   " + line); ++pc_; }
    else if (cmd == "fly") {   // fly <forward> <strafe> <rise> <seconds>
        float f = 0, st = 0, r = 0, secs = 1; std::istringstream(rest) >> f >> st >> r >> secs;
        app.camera().fly(f, st, r, secs); note("ok   " + line); ++pc_;
    }
    else if (cmd == "mouse_move") {   // mouse_move <x> <y>  (window pixels) | mouse_move viewport
        ImGuiIO& io = ImGui::GetIO();
        if (rest=="selected_pivot") {
            float x=0,y=0;
            if (!app.selectedPivotScreen(x,y)) { fail("mouse_move: selected pivot is off screen"); ++pc_; return true; }
            setVirtualMouse(x,y);
        } else if (rest=="selected_glyph") {
            float x=0,y=0;
            if (!app.selectedGlyphScreen(x,y)) { fail("mouse_move: selected glyph is off screen"); ++pc_; return true; }
            setVirtualMouse(x,y);
        } else if (widgets_.count(rest)) {   // a registered widget (or "viewport"): its centre
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
    else if (cmd == "input_text") {
        ImGui::GetIO().AddInputCharactersUTF8(rest.c_str());
        note("ok   " + line); ++pc_; waitFrames_ = 2;
    }
    else if (cmd == "key_down" || cmd == "key_up") {
        static const std::map<std::string, ImGuiKey> keys = {
            {"W", ImGuiKey_W}, {"A", ImGuiKey_A}, {"S", ImGuiKey_S}, {"D", ImGuiKey_D}, {"Q", ImGuiKey_Q},
            {"E", ImGuiKey_E}, {"F", ImGuiKey_F}, {"H", ImGuiKey_H}, {"Shift", ImGuiKey_LeftShift}, {"Alt", ImGuiKey_LeftAlt},
            {"Ctrl", ImGuiKey_LeftCtrl}, {"L", ImGuiKey_L}, {"Enter", ImGuiKey_Enter}, {"Escape", ImGuiKey_Escape}, {"Minus", ImGuiKey_Minus}, {"Equal", ImGuiKey_Equal},
            {"Left",ImGuiKey_LeftArrow}, {"Right",ImGuiKey_RightArrow}, {"Up",ImGuiKey_UpArrow}, {"Down",ImGuiKey_DownArrow},
            {"Comma",ImGuiKey_Comma}, {"Period",ImGuiKey_Period}, {"PageUp",ImGuiKey_PageUp}, {"PageDown",ImGuiKey_PageDown},
            {"LBracket",ImGuiKey_LeftBracket}, {"RBracket",ImGuiKey_RightBracket}, {"Delete",ImGuiKey_Delete}};
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
    else if (cmd == "wait_profiler") {
#ifdef TRACY_ENABLE
        waitOn(TracyIsConnected, "local Tracy capture connection");
#else
        fail("wait_profiler requires a FABLEFORGE_PROFILE=ON build"); ++pc_;
#endif
    }
    else if (cmd == "profile_mark") { FORGE_MESSAGE(rest); note("ok   " + line); ++pc_; }
    else if (cmd == "wait_texture_cleanup") waitOn(app.renderer_.retiredTextureBytes() == 0, "retired texture CPU pixels");
    else if (cmd == "wait_world_detail_held") waitOn(app.worldDetailWork_ && app.worldDetailWork_->held.load(), "held world preparation");
    else if (cmd == "wait_world_scenery") waitOn(app.worldScenery_.settled, "visible distant scenery");
    else if (cmd == "watch_world_coverage") {
        if (rest=="-") { note("coverage watched " + std::to_string(worldCoverageWatchFrames_) + " frames"); worldCoverageWatch_.clear(); }
        else { worldCoverageWatch_=rest; worldCoverageWatchFrames_=0; }
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "wait_world_detail_idle") waitOn(!app.worldDetailFuture_.valid() && !app.worldDetailUpload_ && app.worldDetailRelease_.idle(), "idle world preparation and retirement");
    else if (cmd == "wait_world_detail" || cmd == "wait_world_detail_settled") {
        // High-altitude and empty-space views legitimately request zero maps.
        // Refresh demand once before accepting an empty, settled working set.
        if (!worldDetailWaitStarted_) { worldDetailWaitStarted_ = true; app.worldDetailNext_ = 0; return true; }
        const auto before = pc_;
        waitOn(!app.worldDetailFuture_.valid() && !app.worldDetailUpload_ && app.worldDetailWanting_ == 0 &&
            (cmd == "wait_world_detail_settled" || app.worldDetailDeferred_ == 0) &&
            std::all_of(app.worldDetailShown_.begin(), app.worldDetailShown_.end(), [](const auto& entry) { return entry.second.wanted && entry.second.fade == 1.0f; }), "world detail near the camera");
        if (pc_ != before) worldDetailWaitStarted_ = false;
    }
    else if (cmd == "wait_world_tiles") waitOn(app.worldTileTotal_ > 0 && app.worldTileWorkers_.empty() &&
        app.worldTileTex_.size() == app.worldTiles_.size() && (!app.world3D_ || app.worldLayerAt_.size() == app.worldTiles_.size()), "the world map tiles");
    else if (cmd == "assert_world_ground") {   // <world x> <world y> <map name or ->
        float x = 0, y = 0; std::string expected;
        std::istringstream(rest) >> x >> y >> expected;
        bool inside = false; std::string actual;
        app.worldGroundAt(x, y, inside, &actual);
        if (expected == "-") expected.clear();
        if (actual != expected || inside != !expected.empty()) fail("world ground: expected " + expected + ", got " + actual);
        else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "assert_world_ray") { // Fable origin/direction, then map, * (any hit), or -
        float o[3]{},d[3]{}; std::string expected;
        std::istringstream(rest) >> o[0] >> o[1] >> o[2] >> d[0] >> d[1] >> d[2] >> expected;
        const float renderO[]={o[0],o[2],-o[1]}, renderD[]={d[0],d[2],-d[1]};
        float hit[3]{}; std::string actual;
        const bool found=app.worldPickTile(renderO,renderD,actual,hit);
        if (expected=="-") expected.clear();
        if ((expected=="*" && !found) || (expected!="*" && actual!=expected))
            fail("world ray: expected " + expected + ", got " + actual);
        else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "world_camera") {   // world_camera <fableX> <fableY> <height> <yaw> <pitch> <distance>: the 3D world view's camera
        float fx = 0, fy = 0, fz = 0, yaw = 0.6f, pitch = 0.9f, dist = 800; std::istringstream(rest) >> fx >> fy >> fz >> yaw >> pitch >> dist;
        app.worldCamera_.lookAt(fx, fz, -fy, yaw, pitch, dist); app.worldCameraSet_ = true;
        app.worldDetailNext_ = 0;
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "world_look") {
        float yaw = 0, pitch = 0;
        std::istringstream(rest) >> yaw >> pitch;
        app.worldCamera_.look(yaw - app.worldCamera_.yaw, pitch - app.worldCamera_.pitch);
        app.worldDetailNext_ = 0;
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "world_eye" || cmd == "world_pose") {
        float x = 0, y = 0, height = 0;
        std::istringstream pose(rest);
        pose >> x >> y >> height;
        app.worldCamera_.posX = x; app.worldCamera_.posY = height; app.worldCamera_.posZ = -y;
        if (cmd == "world_eye") app.worldDetailNext_ = 0;
        float yaw = 0, pitch = 0;
        if (pose >> yaw >> pitch)
            app.worldCamera_.look(yaw - app.worldCamera_.yaw, pitch - app.worldCamera_.pitch);
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "world_eye_ground") {
        float x = 0, y = 0, clearance = 0;
        std::istringstream pose(rest);
        pose >> x >> y >> clearance;
        bool inside = false;
        const float ground = app.worldGroundAt(x, y, inside, nullptr);
        if (!inside) fail("world_eye_ground: no terrain under route point");
        app.worldCamera_.posX = x; app.worldCamera_.posY = ground + clearance; app.worldCamera_.posZ = -y;
        float yaw = 0, pitch = 0;
        if (pose >> yaw >> pitch)
            app.worldCamera_.look(yaw - app.worldCamera_.yaw, pitch - app.worldCamera_.pitch);
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "world_memory_sample") {
        if (rest == "auto") app.worldVideoMemoryOverride_.reset();
        else if (rest == "unavailable") app.worldVideoMemoryOverride_ = Renderer::VideoMemoryInfo{};
        else {
            Renderer::VideoMemoryInfo sample;
            std::istringstream input(rest);
            std::string extra;
            if (rest.find('-') != std::string::npos || !(input >> sample.budget >> sample.usage) || (input >> extra)) {
                fail("world_memory_sample: expected budget and usage in bytes"); ++pc_; return true;
            }
            sample.valid = true;
            app.worldVideoMemoryOverride_ = sample;
        }
        app.worldVideoMemoryNext_ = 0;
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "world_transition_hold") {
        if (rest == "auto") app.worldDetailFadeOverride_.reset();
        else app.worldDetailFadeOverride_ = std::clamp(float(std::atof(rest.c_str())), 0.0f, 1.0f);
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "world_transition") {
        const float fraction = std::clamp(float(std::atof(rest.c_str())), 0.0f, 1.0f);
        for (auto& [name, state] : app.worldDetailShown_) state.fade = fraction;
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "world_open_3d") { app.openFromWorld3D(rest); note("ok   " + line); ++pc_; }   // the 3D view's double-click on a map
    else if (cmd == "clear_toasts") { app.toasts_.clear(); note("ok   " + line); ++pc_; }   // clean screenshots for docs
    else if (cmd == "open_world") {   // open_world <path.wld>: File > Open world without the dialog
        if (!app.openWorld(rest)) fail("open_world failed: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "track_preview") {   // track_preview <eye track #> <look track #> <seconds>: the Tracks card's Play preview
        int e = 0, l = 1; float sec = 10; std::istringstream(rest) >> e >> l >> sec;
        if (!app.startTrackPreview(e, l, sec)) fail("track_preview refused: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "track_preview_refused") {   // the same track twice (or a short one) must be refused
        int e = 0, l = 0; std::istringstream(rest) >> e >> l;
        if (app.startTrackPreview(e, l, 1.0f)) { app.stopTrackPreview(); fail("track_preview was not refused: " + rest); } else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "wait_track_preview") waitOn(!app.trackPreview_.active, "the track preview");
    else if (cmd == "assert_camera_back") {   // the camera is where snapshot_camera left it
        const Camera& c = app.camera();
        const float d = std::fabs(c.posX - camSnap_[0]) + std::fabs(c.posY - camSnap_[1]) + std::fabs(c.posZ - camSnap_[2]);
        if (d > 1e-3f) fail("camera not back: off by " + std::to_string(d)); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "assert_initial_position") {
        editor::Frame frame;
        if (app.selectedThing_<0 || !app.doc_.frameOf(size_t(app.selectedThing_),frame)) fail("assert_initial_position: no selected frame");
        else {
            const auto rows=app.doc_.propertiesOf(size_t(app.selectedThing_));
            const char* keys[]={"InitialPosX","InitialPosY","InitialPosZ"};
            const float expected[]={frame.pos[0]+app.doc_.worldX(),frame.pos[1]+app.doc_.worldY(),frame.pos[2]};
            bool valid=true;
            for (int axis=0;axis<3;++axis) {
                const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& r) { return r.ctc.empty() && r.key==keys[axis]; });
                valid=valid && row!=rows.end() && std::abs(std::atof(row->value.c_str())-expected[axis])<.001;
            }
            if (valid) note("ok   "+line); else fail("assert_initial_position: saved position differs from world-space physics position");
        }
        ++pc_;
    }
    else if (cmd == "assert_selected_ground") {
        editor::Frame frame;
        if (app.selectedThing_ < 0 || !app.doc_.frameOf(size_t(app.selectedThing_),frame) || !app.doc_.hasTerrain())
            fail("assert_selected_ground: no terrain or selected frame");
        else {
            const auto expected = app.doc_.groundHeight(frame.pos[0],frame.pos[1]);
            if (!expected || std::abs(frame.pos[2]-*expected)>0.01f) fail("selected object is not on the ground");
            else note("ok   " + line);
        }
        ++pc_;
    }
    else if (cmd == "camera_above_selected") {   // camera_above_selected [distance]: look straight down at the selected thing
        editor::Frame f;
        float dist = 8;
        if (!rest.empty()) dist = float(std::atof(rest.c_str()));
        if (app.selectedThing_ < 0 || !app.doc_.frameOf(size_t(app.selectedThing_), f)) fail("camera_above_selected: nothing selected");
        else { app.camera().lookAt(f.pos[0], f.pos[2], -f.pos[1], 0.0f, 1.55f, dist); note("ok   " + line); }
        ++pc_;
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
        else if (key == "budget_scope") { app.budgetScope_ = std::clamp(std::atoi(val.c_str()), 0, 2); app.budgetDirty_ = true; }
        else if (key == "budget_include") { app.budgetInclude_ = unsigned(std::strtoul(val.c_str(), nullptr, 0)); app.budgetDirty_ = true; }
        else if (key == "budget_copies") { app.budgetAllDuplicates_ = val == "1"; app.budgetDirty_ = true; }
        else if (key == "budget_view") app.budgetView_ = std::clamp(std::atoi(val.c_str()), 0, 2);
        else if (key == "activity") app.activityOpen_ = val == "1" ? 1 : 0;
        else if (key == "world_3d") app.setWorld3D(val == "1");
        else if (key == "world_auto_detail") app.worldAutoDetail_ = val == "1";
        else if (key == "world_detail_upload_ms") {
            const int ms=std::atoi(val.c_str()); app.worldDetailUploadBudgetMs_ = ms==2 || ms==4 ? ms : 0;
        }
        else if (key == "world_overview_batch_limit") app.worldOverviewBatchLimit_ = std::clamp(std::atoi(val.c_str()), 1, 32);
        else if (key == "world_detail_fail_prepare") app.worldDetailFailPrepare_ = val == "-" ? "" : val;
        else if (key == "world_detail_fail_upload") app.worldDetailFailUpload_ = val == "-" ? "" : val;
        else if (key == "world_detail_hold_prepare") {
            app.worldDetailHoldPrepare_ = val == "-" ? "" : val;
            if (val == "-" && app.worldDetailWork_) app.worldDetailWork_->hold = false;
        }
        else if (key == "world_objects" || key == "world_creatures" || key == "world_plants") {
            bool& setting = key == "world_objects" ? app.worldDetailThings_ : key == "world_creatures" ? app.worldDetailCreatures_ : app.worldDetailFoliage_;
            if (setting != (val == "1")) { setting = val == "1"; app.clearWorldDetail(); }
        }
        else if (key == "world_culling") app.renderer_.worldCulling = val == "1";
        else if (key == "world_smooth_objects") app.worldSmoothObjects_ = val == "1";
        else if (key == "world_aa") { int n = std::atoi(val.c_str()); app.renderer_.worldAaMode = n == 2 || n == 4 ? n : n == 1 ? 1 : 0; }
        else if (key == "world_aa_test_limit") { int n = std::atoi(val.c_str()); app.renderer_.worldAaTestLimit = n == 2 || n == 4 ? n : 1; }
        else if (key == "world_cutout_mask") app.renderer_.worldCutoutMask = val == "1";
        else if (key == "world_cutout_aa") app.renderer_.worldCutoutAa = val == "1";
        else if (key == "world_cutout_mips") app.renderer_.worldCutoutMips = val == "1";
        else if (key == "world_cutout_cache") app.worldCutoutCacheOn_ = val == "1";
        else if (key == "world_texture_mips") app.renderer_.worldTextureMips = val == "1";
        else if (key == "world_texture_sharing") app.renderer_.worldTextureSharing = val == "1";
        else if (key == "world_normal_blend") app.renderer_.worldNormalBlend = val == "1";
        else if (key == "world_material_blend") app.renderer_.worldMaterialBlend = val == "1";
        else if (key == "world_terrain") app.worldTerrain2D_ = val == "1";
        else if (key == "world_detail") { app.worldDetailOn_ = val == "1"; if (!app.worldDetailOn_) app.clearWorldDetail(); }
        else if (key == "world_detail_limit") { app.worldDetailMaps_ = app.worldDetailAutoMaps_ = std::clamp(std::atoi(val.c_str()), 1, 32); }
        else if (key == "world_detail_radius") app.worldDetailRadius_ = std::clamp(float(std::atof(val.c_str())), 100.0f, 1000.0f);
        else if (key == "world_scenery") app.worldSceneryOn_ = val == "1";
        else if (key == "world_object_lods") app.renderer_.worldObjectLods = val == "1";
        else if (key == "world_terrain_lods") app.renderer_.worldTerrainLods = val == "1";
        else if (key == "things_script_only") app.thingsScriptOnly_ = val == "1";
        else if (key == "things_nearest") app.thingsNearest_ = val == "1";
        else if (key == "pen_exact") app.penExactStep_ = val == "1";
        else if (key == "pen_step") app.penStep_ = float(std::atof(val.c_str()));
        else if (key == "pen_target") { app.penTarget_ = float(std::atof(val.c_str())); app.penTargetFromStroke_ = false; }
        else if (key == "pen_target_from_stroke") app.penTargetFromStroke_ = val == "1";
        else if (key == "pen_speed") app.penSpeed_ = float(std::atof(val.c_str()));
        else if (key == "pen_smoothness") app.penSmoothness_ = float(std::atof(val.c_str()));
        else if (key == "pen_spiky") app.penSpikyness_ = float(std::atof(val.c_str()));
        else if (key == "pen_magnifier") app.penMagnifier_ = float(std::atof(val.c_str()));
        else if (key == "pen_spray") app.penSpray_ = val == "1";
        else if (key == "place_owner") app.placeOwner_ = val == "auto" ? -1 : val == "neutral" ? 4 : std::clamp(std::atoi(val.c_str()), 0, 4);
        else if (key == "section_day") { app.showDayOnly_ = val == "1"; app.sectionsDirty_ = true; }
        else if (key == "section_night") { app.showNightOnly_ = val == "1"; app.sectionsDirty_ = true; }
        else if (key == "section_hidden") {   // set section_hidden <NAME> 0|1
            std::istringstream vs(val); std::string name; int on = 1; vs >> name >> on;
            std::string k = name; for (auto& c : k) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (on) app.hiddenSections_.insert(k); else app.hiddenSections_.erase(k);
            app.sectionsDirty_ = true;
        }
        else if (key == "place_facing") app.placeFacing_ = std::clamp(std::atoi(val.c_str()), 0, 2);
        else if (key == "place_def") app.placeDef_ = val;
        else if (key == "place_angle") app.placeAngleDeg_ = float(std::atof(val.c_str()));
        else if (key == "place_height") { app.placeFixedHeight_ = !val.empty() && val != "off"; if (app.placeFixedHeight_) app.placeHeight_ = float(std::atof(val.c_str())); }
        else fail("set: unknown key " + key);
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "edit") { app.setEditMode(rest == "1" || rest == "on"); note("ok   " + line); ++pc_; }
    else if (cmd == "textures_tab") { app.setTexturesMode(rest == "1"); note("ok   " + line); ++pc_; }
    else if (cmd == "pack_dest") { app.setPackDest(rest == "-" ? std::string() : std::filesystem::absolute(rest).string()); note("ok   " + line); ++pc_; }   // a pack folder, or - = the game
    else if (cmd == "assets_tab") { app.setTexturesMode(true); app.setAssetsTab(std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }   // 0 textures, 1 models, 2 ground themes, 3 effects, 4 dialogue
    else if (cmd == "dialogue_select") {
        std::istringstream rs(rest); int bank=-1,id=0; rs >> bank >> id;
        if(!rs || bank<0 || bank>3 || id<=0) fail("dialogue_select: expected bank 0..3 and positive Sound ID");
        else { app.dialogueAudio_.reset();app.dialogueMotionPlaying_=false;
            app.dialogueError_.clear();
            app.dialogueBank_=bank; app.dialogueId_=id; app.dialogueLoaded_=false; note("ok   " + line); }
        ++pc_;
    }
    else if (cmd == "dialogue_search") {
        if(rest.size()>=app.dialogueSearchQuery_.size()) fail("dialogue_search: query too long");
        else {
            std::snprintf(app.dialogueSearchQuery_.data(),app.dialogueSearchQuery_.size(),
                          "%s",rest.c_str());
            app.dialogueSearchCacheKey_.clear();app.dialogueError_.clear();
            note("ok   " + line);
        }
        ++pc_;
    }
    else if (cmd == "dialogue_preset") {
        const int preset=std::atoi(rest.c_str());
        if(preset<0 || preset>=int(forge::lipsync::headPresets().size())) fail("dialogue_preset: index out of range");
        else {app.dialoguePreset_=preset;app.dialoguePresetChecked_=false;note("ok   " + line);}
        ++pc_;
    }
    else if (cmd == "dialogue_view") {
        std::istringstream rs(rest);
        float yaw=0,pitch=0,zoom=0;
        rs >> yaw >> pitch >> zoom;
        if(!rs || !std::isfinite(yaw) || !std::isfinite(pitch) ||
           !std::isfinite(zoom) || pitch < -1.5f || pitch > 1.5f ||
           zoom < 0.5f || zoom > 8.0f)
            fail("dialogue_view: expected finite yaw, pitch -1.5..1.5, zoom 0.5..8");
        else {
            app.dialogueHeadYaw_=yaw;app.dialogueHeadPitch_=pitch;
            app.dialogueHeadZoom_=zoom;note("ok   " + line);
        }
        ++pc_;
    }
    else if (cmd == "dialogue_export_path") {
        const auto absolute=fs::absolute(rest).lexically_normal().string();
        if(rest.empty() || absolute.size()>=app.dialogueScratchPath_.size())
            fail("dialogue_export_path: missing or excessive path");
        else {
            std::snprintf(app.dialogueScratchPath_.data(),app.dialogueScratchPath_.size(),
                          "%s",absolute.c_str());
            app.dialogueExportMessage_.clear();
            note("ok   " + line);
        }
        ++pc_;
    }
    else if (cmd == "effect_select") {
        if (!app.selectEffect(rest)) fail("effect_select: no effect " + rest);
        else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "effect_preview_play") {
        app.effectPlaying_ = rest=="1"; note("ok   " + line); ++pc_;
    }
    else if (cmd == "effect_preview_restart") {
        app.effectSimulation_.reset(app.effectBrowserSelection_);app.effectLoopCount_=0;
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "effect_preview_seek") {
        std::istringstream rs(rest);double seconds=-1;rs>>seconds;
        if(!rs || !std::isfinite(seconds) || seconds<0 || seconds>app.effectDuration_)
            fail("effect_preview_seek: time outside duration");
        else {app.effectPlaying_=false;app.effectSimulation_.seek(app.effectBrowserSelection_,seconds);note("ok   "+line);}
        ++pc_;
    }
    else if (cmd == "effect_preview_speed") {
        const int index=std::atoi(rest.c_str());
        if(index<0 || index>3) fail("effect_preview_speed: index out of range");
        else {app.effectSpeedIndex_=index;note("ok   "+line);}
        ++pc_;
    }
    else if (cmd == "effect_preview_duration") {
        std::istringstream rs(rest);float seconds=0;rs>>seconds;
        if(!rs || !std::isfinite(seconds) || seconds<float(particlepreview::Simulation::TickSeconds) || seconds>300.f)
            fail("effect_preview_duration: expected 1/30..300 seconds");
        else {app.effectDuration_=seconds;app.effectAutoDuration_=false;note("ok   "+line);}
        ++pc_;
    }
    else if (cmd == "effect_preview_loop") {
        if(rest!="0" && rest!="1") fail("effect_preview_loop: expected 0 or 1");
        else {app.effectLoop_=rest=="1";note("ok   "+line);}
        ++pc_;
    }
    else if (cmd == "effect_preview_advance_frames") {
        const int frames=std::atoi(rest.c_str());
        if(frames<1 || frames>9000) fail("effect_preview_advance_frames: expected 1..9000");
        else {
            const bool playing=app.effectPlaying_;
            app.effectPlaying_=true;
            for(int i=0;i<frames;++i) app.advanceEffectPlayback(particlepreview::Simulation::TickSeconds);
            if(!playing) app.effectPlaying_=false;
            note("ok   "+line);
        }
        ++pc_;
    }
    else if (cmd == "effect_preview_step") {
        app.effectPlaying_=false;
        const int ticks=std::clamp(std::atoi(rest.c_str()),1,600);
        for (int i=0;i<ticks;++i) app.effectSimulation_.step();
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "effect_search") {
        std::snprintf(app.effectBrowserSearch_, sizeof app.effectBrowserSearch_, "%s", rest.c_str());
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "model_select") { if (!app.selectModel(rest)) fail("model_select: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "model_search") { std::snprintf(app.modelSearch_, sizeof app.modelSearch_, "%s", rest == "-" ? "" : rest.c_str()); note("ok   " + line); ++pc_; }
    else if (cmd == "model_orbit") { std::istringstream rs(rest); rs >> app.modelYaw_ >> app.modelPitch_ >> app.modelZoom_; app.modelPitch_ = std::clamp(app.modelPitch_, -1.5f, 1.5f); app.modelZoom_ = std::clamp(app.modelZoom_, 0.5f, 8.0f); note("ok   " + line); ++pc_; }
    else if (cmd == "texture_select") { if (!app.selectTexture(rest)) fail("texture_select: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "texture_export") { if (!app.exportSelectedTexture(rest)) fail("texture_export failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "texture_replace") { if (!app.replaceSelectedTexture(rest)) fail("texture_replace failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "texture_add") {   // texture_add <NAME> <image> [dxt1|dxt3|argb8888]
        std::istringstream rs(rest); std::string n, img, fmt; rs >> n >> img >> fmt;
        if (!app.addTexture(n, img, "GBANK_MAIN_PC", fmt)) fail("texture_add failed: " + rest); else note("ok   " + line); ++pc_;
    }
    else if (cmd == "world_tab") { app.setWorldMode(rest == "1" || rest == "on"); note("ok   " + line); ++pc_; }
    else if (cmd == "mods_tab") { app.setModsMode(rest == "1" || rest == "on"); note("ok   " + line); ++pc_; }
    else if (cmd == "mod_add") {   // mod_add <path> [name]  (the name is the rest after the first space)
        std::string path = rest, name;
        const size_t sp = rest.find(' ');
        if (sp != std::string::npos) { path = rest.substr(0, sp); name = rest.substr(sp + 1); }
        if (!app.modAdd(path, name)) fail("mod_add failed: " + rest); else note("ok   " + line); ++pc_;
    }
    else if (cmd == "mod_requires") { std::istringstream rs(rest); std::string m, master; int on = 1; rs >> m >> master >> on; if (!app.modSetRequires(m, master, on != 0)) fail("mod_requires failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "assert_mod_problems") {   // assert_mod_problems <substring> | -  (- = none)
        const std::string got = app.modProblems();
        if (rest == "-" ? !got.empty() : got.find(rest) == std::string::npos) fail("mod problems: \"" + got + "\" (wanted " + rest + ")"); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "fit_open") { app.setFitOpen(rest != "0"); note("ok   " + line); ++pc_; }
    else if (cmd == "fractal_open") { app.setFractalOpen(rest != "0"); note("ok   " + line); ++pc_; }
    else if (cmd == "owner_apply") { app.applyOwnerToSelection(); note("ok   " + line); ++pc_; }   // vanilla O on the selection
    else if (cmd == "daynight") {   // daynight 0|1|2: the selected creature's Day and night / Day only / Night only
        const int mode = std::atoi(rest.c_str());
        if (app.selectedThing_ < 0) fail("daynight: nothing selected");
        else if (const auto n = app.doc_.setDayNight(size_t(app.selectedThing_), mode)) { app.selectThing(int(*n)); app.sectionsDirty_ = true; note("ok   " + line); }
        else fail("daynight failed: " + rest);
        ++pc_;
    }
    else if (cmd == "clip_copy") {   // clip_copy x0 y0 x1 y1 [things 0|1]: mode 14's drag
        std::istringstream rs(rest); int x0 = 0, y0 = 0, x1 = 0, y1 = 0, th = 1; rs >> x0 >> y0 >> x1 >> y1 >> th;
        app.clipThings_ = th != 0;
        if (!app.copyRegion(x0, y0, x1, y1)) fail("clip_copy failed: " + rest);
        else note("ok   " + line + "  -> " + std::to_string(app.terrainClip_.w) + "x" + std::to_string(app.terrainClip_.h) + ", " + std::to_string(app.terrainClip_.things.size()) + " things");
        ++pc_;
    }
    else if (cmd == "clip_paste") {   // clip_paste x y [turns]: mode 15's click
        std::istringstream rs(rest); int x = 0, y = 0, t = 0; rs >> x >> y >> t;
        app.clipTurns_ = t;
        const size_t n = app.pasteRegion(x, y);
        if (!n) fail("clip_paste changed nothing: " + rest); else note("ok   " + line + "  -> " + std::to_string(n) + " changes");
        ++pc_;
    }
    else if (cmd == "brush_save") { if (!app.saveBrush(rest)) fail("brush_save failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "brush_load") { if (!app.loadBrush(rest)) fail("brush_load failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "budget_open") { app.setBudgetOpen(rest != "0"); note("ok   " + line); ++pc_; }
    else if (cmd == "budget_run") {   // budget_run [min things]: survey with the window's options, log the totals
        app.runBudgetSurvey();
        const auto& r = app.budgetReport();
        char buf[160];
        std::snprintf(buf, sizeof buf, "budget: %llu things, %llu triangles, %llu vertices, %llu texture bytes, %zu problems",
                      (unsigned long long)r.things, (unsigned long long)r.triangles, (unsigned long long)r.vertices,
                      (unsigned long long)r.textureBytes, r.problems.size());
        const unsigned long long want = rest.empty() ? 0ull : std::strtoull(rest.c_str(), nullptr, 10);
        if (r.things < want || (r.things > 0 && (r.triangles == 0 || r.textureBytes == 0))) fail(std::string(buf));
        else note("ok   " + line + "  -> " + buf);
        ++pc_;
    }
    else if (cmd == "wait_fit") waitOn(!app.fitBusy(), "fit neighbours");
    else if (cmd == "fit_apply") { if (!app.fitApply()) fail("fit_apply changed nothing"); else note("ok   " + line); ++pc_; }
    else if (cmd == "mod_remove") { if (!app.modRemove(rest)) fail("mod_remove failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "mod_move") { std::istringstream rs(rest); std::string n; int to = 0; rs >> n >> to; if (!app.modMove(n, to)) fail("mod_move failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "mod_enable") { std::istringstream rs(rest); std::string n; int on = 1; rs >> n >> on; if (!app.modEnable(n, on != 0)) fail("mod_enable failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "mods_deploy") { if (!app.runModsTool("deploy")) fail("mods_deploy refused"); else note("..   " + line); ++pc_; }
    else if (cmd == "mods_deploy_refused") { if (app.runModsTool("deploy")) fail("mods_deploy unexpectedly started"); else note("ok   " + line); ++pc_; }
    else if (cmd == "mods_undeploy") { if (!app.runModsTool("undeploy")) fail("mods_undeploy refused"); else note("..   " + line); ++pc_; }
    else if (cmd == "mods_conflicts") { if (!app.runModsTool("conflicts")) fail("mods_conflicts refused"); else note("..   " + line); ++pc_; }
    else if (cmd == "wait_mods") { app.pollModsTool(); waitOn(!app.modsBusy(), "mods tool"); }
    else if (cmd == "wait_file_job") waitOn(app.activeFileJob() == nullptr, "file operation and refresh");
    else if (cmd == "mesh_import" || cmd == "mesh_import_refused") {   // mesh_import <model> <NAME> [png]
        std::istringstream rs(rest); std::string model, nm, png; rs >> model >> nm >> png;
        if (app.importMesh(model, nm, png) != (cmd == "mesh_import")) fail(cmd + " returned an unexpected result: " + rest); else note("ok   " + line); ++pc_;
    }
    else if (cmd == "wait_mesh_import") { app.pollMeshImport(); waitOn(!app.meshImportBusy(), "mesh import"); }
    else if (cmd == "mod_pick" || cmd == "mod_pick_refused") {   // mod_pick <key|*> <winner|->  (the winner is the rest after the first space; "*" = the first conflict row)
        std::string key = rest, winner;
        const size_t sp = rest.find(' ');
        if (sp != std::string::npos) { key = rest.substr(0, sp); winner = rest.substr(sp + 1); }
        if (app.modPick(key, winner) != (cmd == "mod_pick")) fail(cmd + " returned an unexpected result: " + rest); else note("ok   " + line); ++pc_;
    }
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
    else if (cmd == "pick_script") {
        int target=-1;
        for (size_t i=0;i<app.doc_.thingCount();++i)
            if (app.doc_.summary(i).scriptName==rest) { target=int(i); break; }
        const auto glyph=std::find_if(app.thingGlyphs_.begin(),app.thingGlyphs_.end(),
                                      [&](const App::ThingGlyph& item){return item.thing==target;});
        if (target<0 || glyph==app.thingGlyphs_.end() || app.viewportSize_.x<=0 || app.viewportSize_.y<=0)
            fail("pick_script: named thing has no visible glyph: "+rest);
        else {
            const float u=(glyph->screen.x-app.viewportOrigin_.x)/app.viewportSize_.x;
            const float v=(glyph->screen.y-app.viewportOrigin_.y)/app.viewportSize_.y;
            if (app.pickAt(u,v)!=target) fail("pick_script: viewport selected a different thing: "+rest);
            else note("ok   "+line);
        }
        ++pc_;
    }
    else if (cmd == "place_at") {
        float u = 0, v = 0, point[3]; std::istringstream(rest) >> u >> v;
        if (app.placeDef_.empty() || !app.groundUnderCursor(u, v, point) || !app.placeDefinitionAt(app.placeDef_, point)) fail("place_at: no definition or ground at " + rest);
        else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "set_thing_prop") { std::istringstream rs(rest); std::string key, val; rs >> key; std::getline(rs, val); while (!val.empty() && val.front() == ' ') val.erase(val.begin()); if (app.selectedThing() >= 0) { app.document().setProperty(size_t(app.selectedThing()), key, val); note("ok   " + line); } else fail("set_thing_prop: nothing selected"); ++pc_; }
    else if (cmd == "select_def") { const int t = app.selectByDefinition(rest); if (t < 0) fail("select_def: not found " + rest); else note("ok   " + line + " -> " + std::to_string(t)); ++pc_; }
    else if (cmd == "select_script") {
        int found=-1;
        for (size_t i=0;i<app.doc_.thingCount();++i)
            if (app.doc_.summary(i).scriptName==rest) {found=int(i);break;}
        if (found<0) fail("select_script: not found "+rest);
        else {app.selectThing(found);note("ok   "+line+" -> "+std::to_string(found));}
        ++pc_;
    }
    else if (cmd == "assert_link_script") {
        std::istringstream rs(rest); std::string sourceName,field,targetName;
        rs>>sourceName>>field>>targetName;
        int source=-1,target=-1;
        for (size_t i=0;i<app.doc_.thingCount();++i) {
            const auto name=app.doc_.summary(i).scriptName;
            if(name==sourceName) source=int(i);
            if(name==targetName) target=int(i);
        }
        bool matched=source>=0 && (targetName=="-" || target>=0);
        bool found=false;
        if(matched) for(const auto& link:app.doc_.linksOf(size_t(source)))
            if(link.field==field) {found=true;matched=targetName=="-" ? link.target==0 : link.target==app.doc_.uidOf(size_t(target));break;}
        if(targetName=="-" && !found) matched=source>=0;
        if(!matched) fail("assert_link_script: unexpected "+field+" on "+sourceName);
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "select_owned_parent") {
        int found=-1;
        for (size_t i=0;i<app.doc_.thingCount();++i) {
            editor::Frame parent,child;
            if (app.doc_.isLocked(i) || !app.doc_.frameOf(i,parent)) continue;
            for (size_t owned:app.doc_.ownedDescendants({i})) if (app.doc_.frameOf(owned,child)) { found=int(i); break; }
            if (found>=0) break;
        }
        if (found<0) fail("select_owned_parent: no movable owner with framed child");
        else { app.selectThing(found); note("ok   "+line+" -> "+std::to_string(found)); }
        ++pc_;
    }
    else if (cmd == "snapshot_owned_frame") {
        ownedFrameSnap_.reset(); ownedSnapUid_=0;
        ownedSnapParentUid_=app.selectedThing_>=0?app.selectedUid_:0;
        if (app.selectedThing_>=0) for (size_t child:app.doc_.ownedDescendants({size_t(app.selectedThing_)})) {
            editor::Frame frame;
            if (!app.doc_.frameOf(child,frame)) continue;
            ownedSnapUid_=app.doc_.uidOf(child); ownedFrameSnap_=frame; break;
        }
        if (!ownedFrameSnap_) fail("snapshot_owned_frame: selected thing has no framed child");
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "toggle_snapshotted_owned" || cmd == "select_snapshotted_owned") {
        const auto index=app.doc_.indexOfUid(ownedSnapUid_);
        if (!index) fail(cmd+": owned snapshot missing");
        else {
            if (cmd == "toggle_snapshotted_owned") app.toggleSelect(int(*index));
            else app.selectThing(int(*index));
            note("ok   "+line);
        }
        ++pc_;
    }
    else if (cmd == "assert_copied_owned_pair") {
        const auto selected=app.selectionIndices();
        bool okay=ownedSnapParentUid_ && ownedSnapUid_ && selected.size()==2 &&
            app.doc_.uidOf(size_t(selected.front()))!=ownedSnapParentUid_ &&
            app.doc_.uidOf(size_t(selected.back()))!=ownedSnapUid_;
        bool copyLinked=false,originalLinked=false;
        if (okay) {
            const uint64_t copyOwner=app.doc_.uidOf(size_t(selected.front()));
            for (const auto& link:app.doc_.linksOf(size_t(selected.back())))
                if (link.field=="OwnerUID") copyLinked=link.target==copyOwner;
            const auto original=app.doc_.indexOfUid(ownedSnapUid_);
            if (original) for (const auto& link:app.doc_.linksOf(*original))
                if (link.field=="OwnerUID") originalLinked=link.target==ownedSnapParentUid_;
        }
        if (!okay || !copyLinked || !originalLinked) fail("assert_copied_owned_pair: copied ownership mismatch");
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_selected_owner") {
        bool okay=app.selectedThing_>=0;
        bool matched=false;
        if (okay) for (const auto& link:app.doc_.linksOf(size_t(app.selectedThing_)))
            if (link.field=="OwnerUID") matched=link.target==std::strtoull(rest.c_str(),nullptr,10);
        if (!matched) fail("assert_selected_owner: OwnerUID mismatch");
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_owned_section") {
        bool okay=app.selectedThing_>=0;
        size_t count=0;
        if (okay) for (size_t child:app.doc_.ownedDescendants({size_t(app.selectedThing_)})) {
            ++count;
            okay=okay && app.doc_.sectionOf(child)==rest;
        }
        if (!okay || count==0) fail("assert_owned_section: no owned children or section mismatch: "+rest);
        else note("ok   "+line+" ("+std::to_string(count)+" owned)");
        ++pc_;
    }
    else if (cmd == "assert_selected_sections") {
        const auto selected=app.selectionIndices();
        bool okay=!selected.empty();
        for (int i:selected) okay=okay && app.doc_.sectionOf(size_t(i))==rest;
        if (!okay) fail("assert_selected_sections: selection section mismatch: "+rest);
        else note("ok   "+line+" ("+std::to_string(selected.size())+" selected)");
        ++pc_;
    }
    else if (cmd == "assert_owned_moved" || cmd == "assert_owned_frame_same") {
        editor::Frame frame;
        const auto index=app.doc_.indexOfUid(ownedSnapUid_);
        bool okay=ownedFrameSnap_ && index && app.doc_.frameOf(*index,frame);
        float delta[3]={};
        if (cmd == "assert_owned_moved") std::istringstream(rest)>>delta[0]>>delta[1]>>delta[2];
        if (okay) {
            for (int k=0;k<3;++k)
                okay=okay && std::abs(frame.pos[k]-ownedFrameSnap_->pos[k]-delta[k])<.005f &&
                    std::abs(frame.forward[k]-ownedFrameSnap_->forward[k])<.005f &&
                    std::abs(frame.up[k]-ownedFrameSnap_->up[k])<.005f;
            okay=okay && std::abs(frame.scale-ownedFrameSnap_->scale)<.005f;
        }
        if (!okay) fail(cmd+": owned child's frame differs from expected parent delta");
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_owned_snapshot_exists" || cmd == "assert_owned_snapshot_detached") {
        const auto index=app.doc_.indexOfUid(ownedSnapUid_);
        bool okay=ownedFrameSnap_.has_value();
        if (cmd == "assert_owned_snapshot_exists") okay=okay && bool(index)==(rest=="1");
        else {
            okay=okay && bool(index);
            bool detached=false;
            if (index) for (const auto& link:app.doc_.linksOf(*index))
                if (link.ctc=="CTCOwnedEntity" && link.field=="OwnerUID") detached=link.target==0;
            okay=okay && detached;
        }
        if (!okay) fail(cmd+": owned snapshot differs from expected state");
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_selected_link_def") {
        std::istringstream rs(rest);
        std::string field, definition;
        rs >> field >> definition;
        uint64_t target = 0;
        for (size_t i = 0; i < app.doc_.thingCount(); ++i)
            if (app.doc_.summary(i).definition == definition) { target = app.doc_.uidOf(i); break; }
        bool matched = false;
        if (app.selectedThing_ >= 0 && target)
            for (const auto& link : app.doc_.linksOf(size_t(app.selectedThing_)))
                if (link.field == field && link.target == target) matched = true;
        if (!matched) fail("selected link " + field + " does not target " + definition);
        else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "assert_exit_serialized") {
        uint64_t target=0;
        for(size_t i=0;i<app.doc_.thingCount();++i)
            if(app.doc_.summary(i).definition==rest) {target=app.doc_.uidOf(i);break;}
        bool matched=app.selectedThing_>=0 && target!=0;
        bool found=false;
        if(matched) {
            const auto& thing=app.doc_.file().things()[size_t(app.selectedThing_)];
            for(const char* ctc:{"CTCDRegionExit","CTCActionUseScriptedHook"}) {
                const auto* block=thing.findCtc(ctc);
                if(!block) continue;
                found=true;
                bool copyMatches=false;
                for(const auto& property:block->properties)
                    if(property.key=="EntranceConnectedToUID")
                        copyMatches=property.value==std::to_string(target);
                matched=matched && copyMatches;
            }
        }
        if(!matched || !found) fail("assert_exit_serialized: present entrance fields must target "+rest);
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "select_toggle_def") {
        int found=-1;
        for (size_t i=0;i<app.doc_.thingCount();++i) if (app.doc_.summary(i).definition==rest) { found=int(i); break; }
        if (found<0) fail("select_toggle_def: not found "+rest); else { app.toggleSelect(found); note("ok   "+line); }
        ++pc_;
    }
    else if (cmd == "selection_lock") { app.setSelectedLocked(rest=="1"); note("ok   "+line); ++pc_; }
    else if (cmd == "snapshot_selected") {
        if (app.selectedThing_<0) fail("snapshot_selected: no selection");
        else { thingSnapUid_=app.selectedUid_; thingSnap_=app.doc_.file().thingBlockText(size_t(app.selectedThing_)); note("ok   "+line); }
        ++pc_;
    }
    else if (cmd == "assert_snapshot_thing_same") {
        const auto index=app.doc_.indexOfUid(thingSnapUid_);
        if (!index || thingSnap_!=app.doc_.file().thingBlockText(*index)) fail("snapshotted object changed or was removed"); else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "snapshot_document") { documentSnap_=app.doc_.text(); note("ok   "+line); ++pc_; }
    else if (cmd == "assert_document_same") {
        if (documentSnap_!=app.doc_.text()) fail("document differs from snapshot"); else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "select_thing") { app.selectThing(std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }
    else if (cmd == "toggle_thing") { app.toggleSelect(std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }
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
    else if (cmd == "rotate_world") { std::istringstream rs(rest);float deg=0;int axis=-1;rs>>deg>>axis;
        if(!rs || axis<0 || axis>2) fail("rotate_world: expected degrees and axis 0..2");
        else {app.rotateSelectedWorld(deg,axis);note("ok   "+line);} ++pc_; }
    else if (cmd == "scale_thing") { app.scaleSelected(float(std::atof(rest.c_str()))); note("ok   " + line); ++pc_; }
    else if (cmd == "ground_thing") { app.snapSelectedToGround(); note("ok   " + line); ++pc_; }
    else if (cmd == "surface_thing") { app.cycleSelectedSurfaces(); note("ok   "+line); ++pc_; }
    else if (cmd == "snapshot_frame") {
        editor::Frame frame;
        if (!app.frameOfSelected(frame)) fail("no selected frame");
        else { frameSnap_=frame; note("ok   "+line); }
        ++pc_;
    }
    else if (cmd == "assert_selected_delta") {
        std::istringstream rs(rest);float dx=0,dy=0,dz=0;rs>>dx>>dy>>dz;
        editor::Frame frame;
        bool okay=bool(rs) && frameSnap_ && app.frameOfSelected(frame);
        if(okay) for(int k=0;k<3;++k) {
            const float expected=(k==0?dx:k==1?dy:dz);
            okay=okay && std::abs(frame.pos[k]-frameSnap_->pos[k]-expected)<0.002f;
        }
        if(!okay) fail("assert_selected_delta: position differs from snapshot + "+rest);
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_selected_forward") {
        std::istringstream rs(rest);float x=0,y=0,z=0;rs>>x>>y>>z;
        editor::Frame frame;
        bool okay=bool(rs) && app.frameOfSelected(frame);
        if(okay) for(int k=0;k<3;++k)
            okay=okay && std::abs(frame.forward[k]-(k==0?x:k==1?y:z))<0.003f;
        if(!okay) fail("assert_selected_forward: direction differs from "+rest);
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_selected_up") {
        std::istringstream rs(rest);float x=0,y=0,z=0;rs>>x>>y>>z;
        editor::Frame frame;
        bool okay=bool(rs) && app.frameOfSelected(frame);
        if(okay) for(int k=0;k<3;++k)
            okay=okay && std::abs(frame.up[k]-(k==0?x:k==1?y:z))<0.003f;
        if(!okay) fail("assert_selected_up: direction differs from "+rest);
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_selected_facing_changed") {
        editor::Frame frame;
        bool okay=frameSnap_ && app.frameOfSelected(frame);
        if(okay) okay=std::hypot(frame.forward[0]-frameSnap_->forward[0],
                                frame.forward[1]-frameSnap_->forward[1],
                                frame.forward[2]-frameSnap_->forward[2])>0.05f;
        if(!okay) fail("assert_selected_facing_changed: direction did not move");
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_selected_moved_xy" || cmd == "assert_selected_frame_same") {
        editor::Frame frame;
        bool okay = frameSnap_ && app.frameOfSelected(frame);
        if (okay) {
            const auto& before = *frameSnap_;
            const float xy = std::hypot(frame.pos[0] - before.pos[0], frame.pos[1] - before.pos[1]);
            okay = cmd == "assert_selected_moved_xy" ? xy > 0.05f : xy < 0.0001f && std::abs(frame.pos[2] - before.pos[2]) < 0.0001f;
            for (int k = 0; k < 3; ++k)
                okay = okay && std::abs(frame.forward[k] - before.forward[k]) < 0.0001f &&
                    std::abs(frame.up[k] - before.up[k]) < 0.0001f;
            okay = okay && std::abs(frame.scale - before.scale) < 0.0001f;
        }
        if (!okay) {
            std::string detail;
            if (frameSnap_ && app.frameOfSelected(frame))
                detail = " (before " + std::to_string(frameSnap_->pos[0]) + "," + std::to_string(frameSnap_->pos[1]) + "," + std::to_string(frameSnap_->pos[2]) +
                    "; after " + std::to_string(frame.pos[0]) + "," + std::to_string(frame.pos[1]) + "," + std::to_string(frame.pos[2]) + ")";
            fail(cmd + ": selected frame does not match the snapshot expectation" + detail);
        } else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "assert_ground_offset_same") {
        editor::Frame frame;
        bool okay = frameSnap_ && app.frameOfSelected(frame);
        if (okay) {
            const auto a = app.doc_.groundHeight(frameSnap_->pos[0], frameSnap_->pos[1]);
            const auto b = app.doc_.groundHeight(frame.pos[0], frame.pos[1]);
            okay = a && b && std::abs((frameSnap_->pos[2] - *a) - (frame.pos[2] - *b)) < 0.005f;
        }
        if (!okay) fail("selected thing did not keep its height above ground"); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "assert_selected_lower") {
        editor::Frame frame; bool okay=frameSnap_ && app.frameOfSelected(frame);
        if (okay) {
            const auto& before=*frameSnap_;
            okay=std::isfinite(frame.pos[2]) && frame.pos[2]<before.pos[2]-.001f && std::abs(frame.scale-before.scale)<.00001f;
            for (int k=0;k<3;++k) {
                if (k<2) okay=okay && std::abs(frame.pos[k]-before.pos[k])<.00001f;
                okay=okay && std::abs(frame.forward[k]-before.forward[k])<.00001f && std::abs(frame.up[k]-before.up[k])<.00001f;
            }
        }
        if (!okay) fail("selected object did not move only downward"); else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_selected_height") {
        editor::Frame frame;
        const float expected=float(std::atof(rest.c_str()));
        if (!app.frameOfSelected(frame) || !std::isfinite(frame.pos[2]) || std::abs(frame.pos[2]-expected)>.001f)
            fail("selected height differs from "+rest);
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_selected_above_ground") {
        editor::Frame frame;
        if (!app.frameOfSelected(frame)) fail("no selected frame");
        else {
            const auto ground=app.doc_.groundHeight(frame.pos[0],frame.pos[1]);
            if (!ground || !std::isfinite(frame.pos[2]) || frame.pos[2]<=*ground+.05f) fail("selected object is not supported above terrain");
            else note("ok   "+line);
        }
        ++pc_;
    }
    else if (cmd == "reseat_things") { app.reseatThings(); note("ok   " + line); ++pc_; }
    else if (cmd == "add_theme") { if (!app.addPaintTheme(rest)) fail("add_theme failed: " + rest); else note("ok   " + line); ++pc_; }
    else if (cmd == "custom_theme" || cmd == "custom_theme_refused") {   // custom_theme <png> <NAME> [donor] [cliffPng]
        std::istringstream rs(rest); std::string png, nm, donor, cliff; rs >> png >> nm >> donor >> cliff;
        const bool okay = app.createCustomTheme(png, nm, donor, cliff);
        if (okay != (cmd == "custom_theme")) fail(cmd + " returned an unexpected result: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "text_input") {
        ImGui::GetIO().AddInputCharactersUTF8(rest.c_str());
        note("ok   "+line); ++pc_; waitFrames_=2;
    }
    else if (cmd == "snapshot_mesh_orientation") {
        const auto& meshes=app.effectSimulation_.meshes();
        if (meshes.empty()) fail("no mesh particle to snapshot");
        else { meshOrientationId_=meshes.front().mesh; std::copy_n(meshes.front().orientation,4,meshOrientationSnap_.begin()); note("ok   "+line); }
        ++pc_;
    }
    else if (cmd == "assert_mesh_rotated") {
        const auto& meshes=app.effectSimulation_.meshes();
        float dot=0;
        if (!meshes.empty()) for (int k=0;k<4;++k) dot+=meshOrientationSnap_[k]*meshes.front().orientation[k];
        if (meshes.empty() || meshOrientationId_<0 || meshes.front().mesh!=meshOrientationId_ || std::abs(dot)>.99999f)
            fail("mesh particle orientation did not change");
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_effect_meshes") {
        if (app.effectSimulation_.meshes().empty() || !app.effectRenderer_.drawnMeshes() || !app.effectRenderer_.meshTriangles())
            fail("effect mesh preview has no simulated or submitted mesh geometry");
        else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_component") {
        std::istringstream input(rest); std::string ctc,key,expected; input>>ctc>>key>>expected;
        bool found=false,matched=false;
        if (app.selectedThing_>=0) for (const auto& field:app.doc_.knownComponentProperties(size_t(app.selectedThing_)))
            if (field.row.ctc==ctc && field.row.key==key) { found=true; matched=expected=="-"?!field.present:field.present && field.row.value==expected; }
        if (!found || !matched) fail("component override mismatch: "+rest); else note("ok   "+line);
        ++pc_;
    }
    else if (cmd == "assert_list") {
        // assert_list <CTC> <base> <count> [slot expected-definition]
        std::istringstream rs(rest); std::string ctc,base,expected; int count=-1,slot=-1;
        rs>>ctc>>base>>count;
        const auto entries=app.selectedThing_<0?std::vector<std::string>{}:app.doc_.listEntries(size_t(app.selectedThing_),ctc,base);
        if (app.selectedThing_<0 || count<0 || entries.size()!=size_t(count)) fail("assert_list count: "+rest+" actual="+std::to_string(entries.size()));
        else if (rs>>slot>>expected) {
            if (slot<0 || size_t(slot)>=entries.size() || entries[size_t(slot)]!="\""+expected+"\"") fail("assert_list entry: "+rest);
            else note("ok   "+line);
        } else note("ok   "+line);
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
    else if (cmd == "compact_stb_refused") { if (app.compactBank()) fail("compact_stb unexpectedly started"); else note("ok   " + line); ++pc_; }
    else if (cmd == "wait_compact") waitOn(!app.compactBusy(), "compaction");
    else if (cmd == "restore_all") { if (!app.restoreAllBackups()) fail("restore failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "setup") { app.setupOpen_ = std::atoi(rest.c_str()) != 0; note("ok   " + line); ++pc_; }
    else if (cmd == "scan_install") { app.scanInstall(rest); note("ok   " + line); ++pc_; }   // exercise folder changes without a native picker
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
    else if (cmd == "save_level_refused") { if (app.saveDocument()) fail("save unexpectedly succeeded"); else note("ok   " + line); ++pc_; }
    else if (cmd == "deploy_level") { if (!app.deployDocument()) fail("deploy failed"); else note("ok   " + line); ++pc_; }
    else if (cmd == "deploy_level_refused") { if (app.deployDocument()) fail("deploy unexpectedly succeeded"); else note("ok   " + line); ++pc_; }
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
    else if (cmd == "snapshot_heights") { heightSnap_ = app.doc_.hasTerrain() ? app.doc_.terrain().heights : std::vector<float>{}; note("ok   " + line); ++pc_; }
    else if (cmd == "assert_heights_changed") {   // 1: some vertex differs from snapshot_heights, 0: none does
        size_t diff = 0;
        if (app.doc_.hasTerrain() && app.doc_.terrain().heights.size() == heightSnap_.size())
            for (size_t i = 0; i < heightSnap_.size(); ++i) diff += app.doc_.terrain().heights[i] != heightSnap_[i];
        const bool want = rest == "1";
        if ((diff > 0) != want) fail("assert_heights_changed " + rest + " (" + std::to_string(diff) + " vertices differ)");
        else note("ok   " + line + " (" + std::to_string(diff) + " vertices differ)");
        ++pc_;
    }
    else if (cmd == "assert_height") {   // assert_height <x> <y> <expected> [tolerance]: the ground at a map-local point
        float x = 0, y = 0, want = 0, tol = 1e-3f;
        std::istringstream(rest) >> x >> y >> want >> tol;
        const auto h = app.doc_.terrainHeight(x, y);
        char got[48]; std::snprintf(got, sizeof got, "%.4f", h ? *h : -1.0f);
        if (!h || std::fabs(*h - want) > tol) fail("assert_height " + rest + " (got " + got + ")"); else note("ok   " + line + " (" + got + ")");
        ++pc_;
    }
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
    else if (cmd == "capture_begin") { capturePrefix_ = rest; captureFrame_ = 0; note("ok   " + line); ++pc_; }
    else if (cmd == "capture_end") { capturePrefix_.clear(); note("ok   " + line); ++pc_; }
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
    else if (cmd == "dump_widget") {
        const auto it = widgets_.find(rest);
        if (it == widgets_.end()) fail("widget not on screen: " + rest);
        else {
            const auto& r = it->second;
            note("widget_rect " + rest + " " + std::to_string(r.x) + " " +
                 std::to_string(r.y) + " " + std::to_string(r.z) + " " + std::to_string(r.w));
        }
        ++pc_;
    }
    else if (cmd == "dump_state") { for (const auto& kv : app.stateDump()) note("     " + kv); ++pc_; }
    else if (cmd == "dump_log") { for (const auto& [lvl, ln] : app.log_) note("     log: " + ln); ++pc_; }
    else if (cmd == "close") { app.requestClose(); note("ok   close"); ++pc_; }
    else if (cmd == "quit") { quit_ = true; note("ok   quit"); return false; }
    else { fail("unknown command: " + line); ++pc_; }
    return true;
}

} // namespace albion::gui
