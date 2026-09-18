#include "app.hpp"

#include "ImGuizmo.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>

#include "forge/env.hpp"
#include "forge/lev.hpp"
#include "forge/wad.hpp"
#include "nlohmann/json.hpp"
#include "theme.hpp"

namespace fs = std::filesystem;
namespace te = albion::terrainexport;

namespace albion::gui {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

std::string groupOf(const std::string& name) {
    const size_t us = name.find('_');
    if (us == std::string::npos) {
        // "Greatwood" style names with no underscore: split off a trailing number
        size_t i = name.size();
        while (i > 0 && std::isdigit((unsigned char)name[i - 1])) --i;
        return i == 0 ? name : name.substr(0, i);
    }
    std::string head = name.substr(0, us);
    size_t i = head.size();
    while (i > 0 && std::isdigit((unsigned char)head[i - 1])) --i;
    return i == 0 ? head : head.substr(0, i);
}

std::string fmtBytes(uint64_t b) {
    char buf[32];
    if (b >= 1u << 20) std::snprintf(buf, sizeof buf, "%.1f MB", b / 1048576.0);
    else std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024.0);
    return buf;
}

std::string pickFolder(HWND owner, const std::string& initial) {
    std::string result;
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (!initial.empty()) {
        std::wstring w(initial.begin(), initial.end());
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(w.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            dlg->SetFolder(item);
            item->Release();
        }
    }
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                const int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                std::string s(size_t(std::max(n - 1, 0)), '\0');
                WideCharToMultiByte(CP_UTF8, 0, path, -1, s.data(), n, nullptr, nullptr);
                result = s;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

void openInExplorer(const std::string& path) {
    ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

const char* kModeNames[] = {"Textured", "Wireframe", "Walkable", "Height"};

} // namespace

// ------------------------------------------------------------------ App

void App::buildFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const char* fontsDir = "C:\\Windows\\Fonts\\";
    auto tryFont = [&](const char* file, float size) -> ImFont* {
        const std::string p = std::string(fontsDir) + file;
        if (!fs::exists(p)) return nullptr;
        return io.Fonts->AddFontFromFileTTF(p.c_str(), std::round(size * scale));
    };
    fontBody_ = tryFont("segoeui.ttf", 17.0f);
    fontBold_ = tryFont("seguisb.ttf", 17.0f);
    fontTitle_ = tryFont("seguisb.ttf", 24.0f);
    fontSmall_ = tryFont("segoeui.ttf", 14.0f);
    if (!fontBody_) fontBody_ = io.Fonts->AddFontDefault();
    if (!fontBold_) fontBold_ = fontBody_;
    if (!fontTitle_) fontTitle_ = fontBody_;
    if (!fontSmall_) fontSmall_ = fontBody_;
    io.FontDefault = fontBody_;
    io.Fonts->Build();
    uiScale_ = wantScale_ = scale;
    theme::setScale(scale);
    theme::applyTheme();
}

void App::rebuildFonts() { buildFonts(wantScale_); }

App::App() = default;
App::~App() = default;

bool App::init(ID3D11Device* device, ID3D11DeviceContext* context, HWND hwnd,
               const std::string& installOverride) {
    device_ = device; context_ = context; hwnd_ = hwnd;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    buildFonts(dpiScale_);

    if (!renderer_.init(device_, context_)) {
        pushLog(std::string("renderer: ") + renderer_.error(), 2);
    }

    settings_.outDir = (fs::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".") / "Documents" / "AlbionAtlas").string();
    std::string savedInstall;
    if (!auto_.active()) { firstRun_ = !fs::exists(settingsPath()); loadSettings(savedInstall); }   // scripted runs stay deterministic
    std::snprintf(outDirBuf_, sizeof outDirBuf_, "%s", settings_.outDir.c_str());

    std::string root = installOverride;
    installSource_ = "command line";
    if (root.empty() && !savedInstall.empty() && fs::exists(fs::path(savedInstall) / "data" / "Levels" / "FinalAlbion.wad")) {
        root = savedInstall; installSource_ = "remembered";
    }
    if (root.empty()) {
        try {
            const auto env = forge::env::Environment::detect();
            if (env.installValid) { root = env.installDir.string(); installSource_ = env.detectSource; }
        } catch (...) {}
    }
    if (!root.empty()) scanInstall(root);
    else pushLog("No Fable install found. Point me at your 'Fable The Lost Chapters' folder.", 1);
    return true;
}

App::InstallHealth App::installHealth() const {
    InstallHealth h;
    if (installPath_.empty()) return h;
    const fs::path r = installPath_;
    std::error_code ec;
    h.gameBin = fs::exists(r / "data" / "CompiledDefs" / "game.bin", ec);
    h.wad = fs::exists(r / "data" / "Levels" / "FinalAlbion.wad", ec);
    h.stb = fs::exists(r / "data" / "Levels" / "FinalAlbion_RT.stb", ec);
    h.texturesBig = fs::exists(r / "data" / "graphics" / "pc" / "textures.big", ec);
    h.fse = fs::exists(r / "FSE" / "FableScriptExtender.dll", ec) && fs::exists(r / "FSE" / "PartyMode" / "PartyMode.lua", ec);
    const char* home = std::getenv("USERPROFILE");
    h.saves = home && fs::exists(fs::path(home) / "Documents" / "My Games" / "Fable" / "Saves", ec);
    return h;
}

void App::drawTour() {
    if (tourStep_ < 0 || tourStep_ > 2) return;
    using theme::S;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    struct Step { const char* title; const char* text; float ax, ay; ImVec2 pos; };   // anchor: 0..1 of the window
    const float leftW = S(270), rightW = S(330);
    const Step steps[3] = {
        {"1 / 3  The maps", "Every map of the game is here, grouped by region. Click one to see it; Ctrl+F searches. Drop a .lev on the window to open a loose file.", 0, 0, ImVec2(vp->Pos.x + leftW + S(14), vp->Pos.y + S(120))},
        {"2 / 3  The tabs", "Export writes GLB / OBJ for Blender. Edit places objects and shapes the ground. World moves whole maps. Textures browses and replaces the game's textures. Everything you write into the game is backed up once.", 0, 0, ImVec2(vp->Pos.x + vp->Size.x - rightW - S(360), vp->Pos.y + S(110))},
        {"3 / 3  The viewport", "Right-drag + WASD to fly, left-drag to turn, wheel to zoom, F to frame. In Edit, click an object to select it and drag the gizmo. Press ? any time for the cheat-sheet.", 0, 0, ImVec2(vp->Pos.x + leftW + S(120), vp->Pos.y + vp->Size.y * 0.45f)},
    };
    const Step& st = steps[tourStep_];
    ImGui::SetNextWindowPos(st.pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(S(340), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(14)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::vec(theme::Bg1));
    ImGui::PushStyleColor(ImGuiCol_Border, theme::vec(theme::Accent));
    ImGui::Begin("##tour", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::BringWindowToFocusFront(ImGui::GetCurrentWindow());
    ImGui::PushFont(fontBold_);
    ImGui::TextColored(theme::vec(theme::Accent), "%s", st.title);
    ImGui::PopFont();
    ImGui::PushFont(fontSmall_);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(308));
    ImGui::TextColored(theme::vec(theme::Text), "%s", st.text);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(6)));
    const float half = (S(308) - S(6)) * 0.5f;
    if (theme::primaryButton(tourStep_ == 2 ? "Done" : "Next", ImVec2(half, S(28)))) tourStep_ = tourStep_ == 2 ? -1 : tourStep_ + 1;
    auto_.registerWidget("btn_tour_next");
    ImGui::SameLine(0, S(6));
    if (theme::ghostButton("Skip the tour", ImVec2(half, S(28)))) tourStep_ = -1;
    auto_.registerWidget("btn_tour_skip");
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void App::drawHelpOverlay() {
    if (!helpOpen_) return;
    using theme::S;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // dim everything (a full-screen window under the card), then the card; a click
    // outside or Escape closes it
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##helpdim", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::BringWindowToFocusFront(ImGui::GetCurrentWindow());
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(S(720), vp->Size.x - S(40)), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(18)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(12));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::vec(theme::Bg1));
    ImGui::PushStyleColor(ImGuiCol_Border, theme::vec(theme::Border));
    ImGui::Begin("##help", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::BringWindowToFocusFront(ImGui::GetCurrentWindow());
    ImGui::PushFont(fontBold_);
    ImGui::TextUnformatted("Keyboard and mouse");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushFont(fontSmall_);
    ImGui::TextColored(theme::vec(theme::Faint), "   ?  or  F1 toggles this   |   Esc closes");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(8)));
    struct Row { const char* keys; const char* what; };
    struct Group { const char* title; std::vector<Row> rows; };
    const Group groups[] = {
        {"Camera (viewport)", {{"RMB drag + W A S D", "look and fly (Q / E down / up)"}, {"LMB drag", "dolly / turn"}, {"Alt + LMB drag", "orbit the focus"}, {"MMB drag", "pan"}, {"Wheel", "zoom"}, {"F", "frame the map, or the selected object"}}},
        {"Edit: objects", {{"Q  W  E  R", "select / move / rotate / scale tool"}, {"Click", "select what you see (the real mesh)"}, {"Ctrl + click", "add to / remove from the selection"}, {"Ctrl + D", "duplicate"}, {"Ctrl + C  /  Ctrl + V", "copy / paste at the view centre"}, {"Del", "delete"}, {"End", "drop to the ground"}, {"Esc", "clear the selection"}, {"Ctrl + Z  /  Ctrl + Y", "undo / redo (also on the World tab)"}}},
        {"Edit: terrain", {{"T", "terrain tool (opens the Terrain tab)"}, {"LMB hold", "sculpt / paint"}, {"Shift", "invert: lower, or paint walkable"}, {"[  ]", "brush radius"}}},
        {"World tab", {{"Drag a map", "move it (snaps to 32)"}, {"Arrow keys", "nudge the selected map by 32"}, {"Wheel / right drag", "zoom / pan"}, {"F  or  Home", "fit the world"}}},
        {"Everywhere", {{"Ctrl + F", "search the map list"}, {"Ctrl + E", "export the selected map"}, {"Drop a .lev / .tng", "open a loose file"}}},
    };
    const float colW = S(190);
    ImGui::Columns(2, "##helpcols", false);
    ImGui::SetColumnWidth(0, std::min(S(720), vp->Size.x - S(40)) * 0.5f);
    int i = 0;
    for (const Group& g : groups) {
        if (i == 3) ImGui::NextColumn();
        ImGui::PushFont(fontBold_);
        ImGui::TextColored(theme::vec(theme::Accent), "%s", g.title);
        ImGui::PopFont();
        ImGui::PushFont(fontSmall_);
        for (const Row& r : g.rows) {
            ImGui::TextColored(theme::vec(theme::Text), "%s", r.keys);
            ImGui::SameLine(colW);
            ImGui::TextColored(theme::vec(theme::Muted), "%s", r.what);
        }
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(8)));
        ++i;
    }
    ImGui::Columns(1);
    ImGui::Dummy(ImVec2(0, S(4)));
    if (theme::ghostButton("Close", ImVec2(S(120), S(28)))) helpOpen_ = false;
    auto_.registerWidget("btn_help_close");
    const bool clickedOutside = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows);
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    if (clickedOutside) helpOpen_ = false;
}

void App::drawSetupPanel() {
    if (!setupOpen_) return;
    using theme::S;
    ImGui::OpenPopup("Setup");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(560), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(16)));
    if (ImGui::BeginPopupModal("Setup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        const InstallHealth h = installHealth();
        ImGui::PushFont(fontBold_);
        ImGui::TextUnformatted(installValid_ ? "Your Fable install" : "Point Albion Atlas at Fable: The Lost Chapters");
        ImGui::PopFont();
        ImGui::PushFont(fontSmall_);
        ImGui::PushTextWrapPos(S(530));
        ImGui::TextColored(theme::vec(theme::Muted), "%s", installValid_ ? installPath_.c_str() : "The Steam or GOG folder that holds Fable.exe (Steam: steamapps\\common\\Fable The Lost Chapters). Nothing in it is changed until you write something; every file touched gets a one-time .atlas-orig backup.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(8)));
        auto row = [&](bool ok, const char* what, const char* enables, const char* without) {
            ImGui::TextColored(theme::vec(ok ? theme::Success : theme::Warn), ok ? "  ok   " : "  --   ");
            ImGui::SameLine(0, 0);
            ImGui::TextColored(theme::vec(theme::Text), "%s", what);
            ImGui::SameLine(0, S(8));
            ImGui::PushTextWrapPos(S(530));
            ImGui::TextColored(theme::vec(theme::Muted), "%s", ok ? enables : without);
            ImGui::PopTextWrapPos();
        };
        row(h.gameBin && h.wad && h.stb, "game data", "levels, objects, terrain and the world editor", "no data/Levels + CompiledDefs here: this is not a Fable install");
        row(h.texturesBig, "textures.big", "textured preview, ground-theme paint, custom textures, minimaps", "no textured preview, no theme paint or custom textures (a trimmed install?)");
        row(h.fse, "ForgeFSE", "the live link to the running game (go here, spawn, follow)", "no live link: install ForgeFSE (FSE_Launcher.exe) to talk to the running game; everything else works");
        row(h.saves, "saves folder", "the in-game test harness can continue your profiles", "no My Games/Fable/Saves yet: start the game once");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(10)));
        ImGui::PushFont(fontSmall_);
        ImGui::PushTextWrapPos(S(530));
        ImGui::TextColored(theme::vec(theme::Faint), "Rules the engine imposes: a new region only shows in a game started after it was added (saves cache the region table); new objects and creatures need a fresh game or a first visit; enemy spawners only run once the hero is past childhood; never write while the game is running (the editor refuses when the live link sees a hero).");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        // backups: everything Atlas has touched, and the way back
        if (installValid_) {
            if (backupsScannedAt_ < 0 || ImGui::GetTime() - backupsScannedAt_ > 5.0) { backupList_ = backups::scan(installPath_); backupsScannedAt_ = ImGui::GetTime(); }
            size_t changed = 0; for (const auto& e : backupList_) changed += e.differs;
            ImGui::Dummy(ImVec2(0, S(10)));
            ImGui::PushFont(fontBold_);
            ImGui::Text("Backups: %zu file(s) touched, %zu differ from retail", backupList_.size(), changed);
            ImGui::PopFont();
            if (!backupList_.empty()) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
                ImGui::BeginChild("##backuplist", ImVec2(S(530), S(std::min(120.0f, 18.0f * float(backupList_.size()) + 8.0f))), ImGuiChildFlags_None);
                ImGui::PopStyleColor();
                ImGui::PushFont(fontSmall_);
                for (const auto& e : backupList_) {
                    ImGui::TextColored(theme::vec(e.differs ? theme::Warn : theme::Faint), "%s  %s", e.differs ? (e.created ? "new " : "edit") : "same", fs::relative(e.file, installPath_).string().c_str());
                }
                ImGui::PopFont();
                ImGui::EndChild();
                if (!confirmRestore_) {
                    if (theme::ghostButton(changed ? "Restore the retail files" : "Nothing to restore", ImVec2(S(530), S(28))) && changed) confirmRestore_ = true;
                    auto_.registerWidget("btn_restore_all");
                } else {
                    const int r = confirmRow("Put every backed-up file back and delete the files Atlas created? Your edits in the game are lost (loose .lev/.tng drafts stay).",
                                             "Yes, restore", S(530), S(28), "btn_restore_confirm");
                    if (r != 0) confirmRestore_ = false;
                    if (r > 0) restoreAllBackups();
                }
            }
        }
        ImGui::Dummy(ImVec2(0, S(10)));
        const float w = (S(530) - S(6)) * 0.5f;
        if (theme::ghostButton("Choose the install folder...", ImVec2(w, S(32)))) {
            const std::string picked = pickFolder(hwnd_, installPath_);
            if (!picked.empty()) { installSource_ = "manual"; scanInstall(picked); }
        }
        auto_.registerWidget("btn_setup_browse");
        ImGui::SameLine(0, S(6));
        if (theme::primaryButton(installValid_ ? "Continue" : "Continue without an install", ImVec2(w, S(32)))) { setupOpen_ = false; ImGui::CloseCurrentPopup(); if (tourPending_) { tourPending_ = false; tourStep_ = 0; } }
        auto_.registerWidget("btn_setup_continue");
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

bool App::restoreAllBackups() {
    if (!installValid_) return false;
    std::vector<std::string> notes; std::string err;
    const size_t n = backups::restoreAll(installPath_, true, notes, err);
    for (const auto& x : notes) pushLog("restore: " + x, 0);
    if (!err.empty()) pushLog("restore: " + err, 2);
    if (n) pushLog("restore: " + std::to_string(n) + " file(s) back to retail; reloading the map list", 3);
    rescanBackups();
    if (n) { scanInstall(installPath_); }
    return err.empty();
}

std::string App::settingsPath() const {
    const char* appdata = std::getenv("APPDATA");
    const fs::path dir = fs::path(appdata ? appdata : ".") / "AlbionAtlas";
    return (dir / "settings.json").string();
}

void App::loadSettings(std::string& savedInstall) {
    std::ifstream f(settingsPath());
    if (!f) return;
    try {
        const auto j = nlohmann::json::parse(f);
        savedInstall = j.value("install", "");
        settings_.outDir = j.value("out_dir", settings_.outDir);
        settings_.format = j.value("format", settings_.format);
        settings_.up = j.value("up", settings_.up);
        settings_.textures = j.value("textures", settings_.textures);
        settings_.texels = std::clamp(j.value("texels", settings_.texels), 2, 32);
        settings_.tile = std::clamp(j.value("tile", settings_.tile), 1.0f, 16.0f);
        settings_.gain = std::clamp(j.value("gain", settings_.gain), 0.5f, 3.0f);
        settings_.layers = j.value("layers", settings_.layers);
        settings_.walkable = j.value("walkable", settings_.walkable);
        settings_.foliage = j.value("foliage", settings_.foliage);
        settings_.things = j.value("things", settings_.things);
        settings_.water = j.value("water", settings_.water);
        settings_.creatures = j.value("creatures", settings_.creatures);
        settings_.texSize = std::clamp(j.value("texSize", settings_.texSize), 0, 2);
        settings_.world = j.value("world", settings_.world);
        editTab_ = std::clamp(j.value("editTab", editTab_), 0, 3);
        settings_.uiScale = std::clamp(j.value("uiScale", settings_.uiScale), 0.8f, 1.5f);
    } catch (...) {}
}

void App::saveSettings() const {
    if (auto_.active()) return;
    try {
        fs::create_directories(fs::path(settingsPath()).parent_path());
        nlohmann::json j = {
            {"install", installPath_}, {"out_dir", std::string(outDirBuf_)}, {"format", settings_.format},
            {"up", settings_.up}, {"textures", settings_.textures}, {"texels", settings_.texels},
            {"tile", settings_.tile}, {"gain", settings_.gain}, {"layers", settings_.layers}, {"walkable", settings_.walkable},
            {"foliage", settings_.foliage}, {"things", settings_.things}, {"water", settings_.water}, {"creatures", settings_.creatures}, {"texSize", settings_.texSize}, {"world", settings_.world},
            {"editTab", editTab_}, {"uiScale", settings_.uiScale},
        };
        std::ofstream(settingsPath()) << j.dump(2);
    } catch (...) {}
}

void App::scanInstall(const std::string& root) {
    maps_.clear();
    installPath_ = root;
    installValid_ = fs::exists(fs::path(root) / "data" / "CompiledDefs" / "game.bin");
    const fs::path wadPath = fs::path(root) / "data" / "Levels" / "FinalAlbion.wad";
    if (!fs::exists(wadPath)) {
        installValid_ = false;
        pushLog("Not a Fable TLC install: " + root, 2);
        return;
    }
    regions_ = te::loadRegionIndex(root);
    try {
        const auto wad = forge::wad::Archive::open(wadPath);
        for (const auto& e : wad.entries()) {
            const fs::path p(e.name);
            if (lower(p.extension().string()) != ".lev") continue;
            MapEntry m; m.name = p.stem().string(); m.key = m.name; m.size = e.size;
            auto rg = regions_.regionOfMap.find(lower(m.name));
            m.group = rg != regions_.regionOfMap.end() ? rg->second : groupOf(m.name);
            auto og = regions_.originOfMap.find(lower(m.name));
            if (og != regions_.originOfMap.end()) { m.worldX = og->second.first; m.worldY = og->second.second; m.hasWorld = true; }
            const fs::path loose = fs::path(root) / "data" / "Levels" / "FinalAlbion" / (m.name + ".lev");
            if (fs::exists(loose)) m.loosePath = loose.string();
            maps_.push_back(std::move(m));
        }
        std::sort(maps_.begin(), maps_.end(), [](const MapEntry& a, const MapEntry& b) {
            if (lower(a.group) != lower(b.group)) return lower(a.group) < lower(b.group);
            return lower(a.name) < lower(b.name);
        });
        pushLog(std::to_string(maps_.size()) + " maps in FinalAlbion.wad", 0);
    } catch (const std::exception& e) {
        pushLog(std::string("cannot read FinalAlbion.wad: ") + e.what(), 2);
        installValid_ = false;
        return;
    }
    startContextLoad();
}

void App::startContextLoad(const std::string& fromRoot) {
    if (!installValid_) return;
    ctxPending_ = std::make_shared<te::Context>();
    const fs::path root = fromRoot.empty() ? fs::path(installPath_) : fs::path(fromRoot);
    auto ctx = ctxPending_;
    ctxFuture_ = std::async(std::launch::async, [ctx, root]() {
        std::string err;
        const bool ok = ctx->load(root, root / "data" / "graphics" / "pc" / "textures.big", err);
        // no textures.big (a trimmed install): keep the ENGINE_THEME defs so theme
        // paint, navigation and blank levels still work without the albedo bake
        if (!ok) { std::string derr; ctx->loadDefs(root, derr); }
        return std::make_pair(ok, err);
    });
}

std::string App::resolveLevPath(const MapEntry& e, std::string& err) {
    if (!e.loosePath.empty()) return e.loosePath;
    try {
        const fs::path wadPath = fs::path(installPath_) / "data" / "Levels" / "FinalAlbion.wad";
        const auto wad = forge::wad::Archive::open(wadPath);
        const std::string want = lower(e.name) + ".lev";
        for (const auto& en : wad.entries()) {
            if (lower(fs::path(en.name).filename().string()) != want) continue;
            const auto bytes = wad.read(en);
            const fs::path dir = fs::temp_directory_path() / "AlbionAtlas";
            fs::create_directories(dir);
            const fs::path out = dir / (e.name + ".lev");
            std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            return out.string();
        }
        err = "map not found in FinalAlbion.wad";
    } catch (const std::exception& ex) {
        err = ex.what();
    }
    return {};
}

bool App::openDropped(const std::string& path) {
    const std::string ext = lower(fs::path(path).extension().string());
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
        if (!documentLoaded() || !doc_.hasTerrain()) { pushLog("drop: open a map with terrain first, then drop the image again to make a ground texture from it", 1); return false; }
        std::snprintf(customPng_, sizeof customPng_, "%s", fs::absolute(path).string().c_str());
        if (!customName_[0]) {
            std::string nm = "GROUND_" + fs::path(path).stem().string();
            for (auto& c : nm) { c = char(std::toupper(static_cast<unsigned char>(c))); if (!std::isalnum(static_cast<unsigned char>(c))) c = '_'; }
            std::snprintf(customName_, sizeof customName_, "%.*s", int(sizeof customName_ - 1), nm.c_str());
        }
        setEditMode(true);
        setEditTab(1);
        terrainMode_ = 6;
        customThemeOpen_ = true;
        pushLog("drop: " + fs::path(path).filename().string() + " is ready as a custom ground texture; name it and press Create theme", 0);
        return true;
    }
    return openLooseLev(path);
}

bool App::openLooseLev(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || lower(fs::path(path).extension().string()) != ".lev") {
        pushLog("Not a .lev file: " + path, 2);
        return false;
    }
    MapEntry m;
    m.name = fs::path(path).stem().string();
    m.key = "file:" + m.name;
    m.group = "Loose files";
    m.loosePath = fs::absolute(path).string();
    m.size = uint32_t(fs::file_size(path, ec));
    auto it = std::find_if(maps_.begin(), maps_.end(), [&](const MapEntry& e) { return e.loosePath == m.loosePath; });
    if (it == maps_.end()) maps_.insert(maps_.begin(), m);   // loose files first: visible without scrolling
    groupOpen_["Loose files"] = true;
    pushLog("Opened " + m.loosePath, 0);
    selectedName_.clear();
    selectMap(m.key);
    return true;
}

void App::selectMap(const std::string& nameOrKey) {
    if (nameOrKey == selectedName_) return;
    if (hasUnsavedEdits() && !discardEdits_ && (!auto_.active() || promptInAuto_) && pendingSelect_.empty()) { pendingSelect_ = nameOrKey; return; }
    pendingSelect_.clear();
    discardEdits_ = false;
    auto it = std::find_if(maps_.begin(), maps_.end(), [&](const MapEntry& m) { return m.key == nameOrKey; });
    if (it == maps_.end()) it = std::find_if(maps_.begin(), maps_.end(), [&](const MapEntry& m) { return m.name == nameOrKey; });
    if (it == maps_.end()) return;
    selectedName_ = it->key;
    scrollToSelected_ = true;
    groupOpen_[it->group] = true;
    renderer_.clearLayer(0);
    renderer_.clearThings();
    foliageLoadedFor_.clear();
    foliageInstances_ = 0;
    thingInstances_ = 0;
    foliageStatus_.clear();
    openDocument();
    startPreviewLoad();
}

void App::setPreviewFoliage(bool on) {
    previewFoliage_ = on;
    renderer_.showFoliage = on;
    if (on && !foliageLoaded() && !foliageFuture_.valid() && previewLoaded()) startFoliageLoad();
}

void App::setPreviewThings(bool on) {
    previewThings_ = on;
    renderer_.showThings = on;
    if (on && !foliageLoaded() && !foliageFuture_.valid() && previewLoaded()) startFoliageLoad();
}

void App::startFoliageLoad() {
    if (selectedName_.empty() || !ctx_.ready()) return;
    if (foliageFuture_.valid()) { foliagePendingName_ = selectedName_; return; }
    const MapEntry* found = findEntry(selectedName_);
    if (!found) return;
    const MapEntry entry = *found;
    const te::Context* ctx = &ctx_;
    const std::string root = installPath_;
    const std::string tngText = documentLoaded() ? doc_.text() : std::string();
    if (documentLoaded()) syncedRevision_ = doc_.revision();
    foliageFuture_ = std::async(std::launch::async, [entry, ctx, root, tngText]() {
        FoliageResult r; r.name = entry.key;
        foliageexport::Options fo;
        fo.gameRoot = root;
        fo.textures = true;
        fo.up = te::UpAxis::Y;
        fo.mapLocal = true;
        try { r.scene = foliageexport::load(entry.name, fo, *ctx); } catch (const std::exception& e) { r.scene.warnings.push_back(e.what()); }
        thingsexport::Options to;
        to.gameRoot = root;
        to.textures = true;
        to.up = te::UpAxis::Y;
        to.tngText = tngText;
        try { r.things = thingsexport::load(entry.name, to, *ctx, &r.thingStats); } catch (const std::exception& e) { r.things.warnings.push_back(e.what()); }
        return r;
    });
}

const MapEntry* App::findEntry(const std::string& key) const {
    auto it = std::find_if(maps_.begin(), maps_.end(), [&](const MapEntry& m) { return m.key == key; });
    return it == maps_.end() ? nullptr : &*it;
}

void App::startPreviewLoad() {
    if (selectedName_.empty()) return;
    if (previewFuture_.valid()) { previewPendingName_ = selectedName_; return; }
    const MapEntry* found = findEntry(selectedName_);
    if (!found) return;
    const MapEntry entry = *found;
    const bool textured = ctx_.ready();
    reloadWhenContextReady_ = !textured;
    const te::Context* ctx = textured ? &ctx_ : nullptr;
    const int texels = previewTexels_;
    const float gain = settings_.gain;
    previewFuture_ = std::async(std::launch::async, [this, entry, ctx, textured, texels, gain]() {
        PreviewResult r; r.name = entry.key; r.textured = textured;
        std::string err;
        const std::string lev = resolveLevPath(entry, err);
        if (lev.empty()) { r.error = err; return r; }
        try {
            const auto file = forge::lev::File::open(lev);
            te::Options o;
            o.textures = textured;
            o.texelsPerCell = texels;
            o.gain = gain;
            o.up = te::UpAxis::Y;
            if (ctx) { o.gameRoot = ctx->gameRoot(); o.mapName = entry.key.rfind("file:", 0) == 0 ? std::string() : entry.name; }
            r.scene = te::buildScene(file, o, ctx);
        } catch (const std::exception& e) {
            r.error = e.what();
        }
        return r;
    });
}

void App::startExport() {
    if (exportFuture_.valid() || selectedName_.empty()) return;
    if (const MapEntry* e = findEntry(selectedName_)) startExportOf(*e);
}

std::vector<std::string> App::regionMapKeys(const std::string& region) const {
    std::vector<std::string> out;
    for (const auto& m : maps_) if (m.group == region && m.loosePath.empty()) out.push_back(m.key);
    return out;
}

std::vector<std::string> App::visibleMapNames() const {
    std::vector<std::string> out;
    const std::string f = lower(filter_);
    for (const auto& m : maps_)
        if (f.empty() || lower(m.name).find(f) != std::string::npos) out.push_back(m.key);
    return out;
}

void App::startBatchExport(const std::vector<std::string>& names) {
    if (names.empty() || batchActive()) return;
    batchQueue_ = names;
    batchTotal_ = int(names.size());
    batchDone_ = batchFailed_ = 0;
    pushLog("Batch export: " + std::to_string(batchTotal_) + " maps -> " + std::string(outDirBuf_), 0);
    saveSettings();
}

void App::cancelBatch() {
    if (!batchActive()) return;
    pushLog("Batch cancelled after " + std::to_string(batchDone_) + " of " + std::to_string(batchTotal_), 1);
    batchQueue_.clear();
    batchTotal_ = 0;
}

void App::startExportOf(const MapEntry& entry) {
    settings_.outDir = outDirBuf_;
    const ExportSettings s = settings_;
    const te::Context* ctx = ctx_.ready() ? &ctx_ : nullptr;
    const std::string outPath = (fs::path(s.outDir) / (entry.name + (s.format == 0 ? ".glb" : ".obj"))).string();
    lastExportPath_ = outPath;
    lastExportOk_ = false;
    batchCurrent_ = entry.name;
    if (!batchActive()) { pushLog("Exporting " + entry.name + " ...", 0); saveSettings(); }
    exportFuture_ = std::async(std::launch::async, [this, entry, s, ctx, outPath]() {
        ExportResult r; r.path = outPath;
        const auto t0 = std::chrono::steady_clock::now();
        std::string err;
        const std::string lev = resolveLevPath(entry, err);
        if (lev.empty()) { r.log.push_back("error: " + err); return r; }
        try {
            fs::create_directories(fs::path(outPath).parent_path());
            const auto file = forge::lev::File::open(lev);
            te::Options o;
            o.textures = s.textures && ctx != nullptr;
            o.texelsPerCell = s.texels;
            o.tileSize = s.tile;
            o.gain = s.gain;
            o.layers = s.layers;
            o.walkableColor = s.walkable;
            o.water = s.water;
            o.up = s.up == 0 ? te::UpAxis::Y : te::UpAxis::Z;
            if (ctx) { o.gameRoot = ctx->gameRoot(); o.mapName = entry.key.rfind("file:", 0) == 0 ? std::string() : entry.name; }
            if (s.world && entry.hasWorld) { o.originX = entry.worldX; o.originY = entry.worldY; }
            else if (s.world) r.log.push_back("warning: no world placement known for " + entry.name + ", exporting map-local");
            o.log = [&](const std::string& m) { r.log.push_back(m); };
            if (s.textures && ctx == nullptr) r.log.push_back("warning: textures not loaded yet, exporting untextured");
            const auto scene = te::buildScene(file, o, ctx);
            foliageexport::Scene fol;
            albion::foliageexport::setTextureLimit(s.texSize == 1 ? 256 : s.texSize == 2 ? 128 : 0);
            if (s.foliage && ctx) {
                foliageexport::Options fo;
                fo.gameRoot = ctx->gameRoot();
                fo.textures = o.textures;
                fo.up = o.up;
                fo.mapLocal = !(s.world && entry.hasWorld);
                fo.log = o.log;
                fol = foliageexport::load(entry.name, fo, *ctx);
            }
            foliageexport::Scene thg;
            if (s.things && ctx) {
                thingsexport::Options to;
                to.gameRoot = ctx->gameRoot();
                to.textures = o.textures;
                to.up = o.up;
                to.originX = o.originX; to.originY = o.originY;
                to.creatures = s.creatures;
                to.log = o.log;
                thg = thingsexport::load(entry.name, to, *ctx);
            }
            std::vector<const foliageexport::Scene*> layers;
            if (s.foliage && ctx) layers.push_back(&fol);
            if (s.things && ctx) layers.push_back(&thg);
            const auto files = s.format == 0 ? foliageexport::writeGlbWith(scene, layers, outPath)
                                             : foliageexport::writeObjWith(scene, layers, outPath);
            for (const auto& f : files) r.files.push_back(f.string());
            r.ok = true;
        } catch (const std::exception& e) {
            r.log.push_back(std::string("error: ") + e.what());
        }
        albion::foliageexport::setTextureLimit(0);   // previews always load full-size textures
        r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return r;
    });
}

void App::pollWorkers() {
    using namespace std::chrono_literals;
    if (ctxFuture_.valid() && ctxFuture_.wait_for(0ms) == std::future_status::ready) {
        const auto [ok, err] = ctxFuture_.get();
        if (ok) {
            ctx_ = *ctxPending_;
            pushLog("Textures ready (game.bin themes + textures.big)", 3);
            if (reloadWhenContextReady_ && !selectedName_.empty()) startPreviewLoad();
            else if ((previewFoliage_ || previewThings_) && previewLoaded() && !foliageLoaded()) startFoliageLoad();
        } else {
            ctxError_ = err;
            if (ctxPending_->themeLibrary()) ctx_ = *ctxPending_;   // defs-only context
            pushLog("Textures unavailable: " + err, 1);
        }
        ctxPending_.reset();
    }
    if (previewFuture_.valid() && previewFuture_.wait_for(0ms) == std::future_status::ready) {
        PreviewResult r = previewFuture_.get();
        if (!r.error.empty()) {
            lastError_ = r.error;
            pushLog("Preview failed for " + r.name + ": " + r.error, 2);
        } else if (r.name == selectedName_) {
            // Re-bakes of the same map (gain change, textures arriving) keep the camera.
            renderer_.upload(r.scene, camera_, lastFramedFor_ != r.name);
            lastFramedFor_ = r.name;
            previewScene_ = std::move(r.scene);
            previewScene_.albedo = {};  // GPU owns it now; keep stats only
            previewLoadedFor_ = r.name;
            previewTextured_ = r.textured;
            if ((previewFoliage_ || previewThings_) && ctx_.ready() && !foliageLoaded()) startFoliageLoad();
        }
        if (!previewPendingName_.empty()) {
            previewPendingName_.clear();
            if (previewLoadedFor_ != selectedName_) startPreviewLoad();
        } else if (r.name == selectedName_ && !r.textured && ctx_.ready()) {
            startPreviewLoad();   // context arrived while we were baking untextured
        }
    }
    if (foliageFuture_.valid() && foliageFuture_.wait_for(0ms) == std::future_status::ready) {
        FoliageResult r = foliageFuture_.get();
        if (r.name == selectedName_ && r.thingsOnly) {
            thingInstances_ = r.things.instances.size();
            renderer_.uploadThings(r.things, te::UpAxis::Y);
            bindInstances(r.things);
            for (const auto& w : r.things.warnings) pushLog("objects: " + w, 1);
        } else if (r.name == selectedName_) {
            foliageLoadedFor_ = r.name;
            foliageInstances_ = r.scene.instances.size();
            thingInstances_ = r.things.instances.size();
            if (r.scene.found && !r.scene.instances.empty()) {
                renderer_.uploadLayer(0, r.scene, te::UpAxis::Y);
                foliageStatus_ = std::to_string(r.scene.instances.size()) + " plants (" + std::to_string(r.scene.treeInstances) + " trees)";
            } else {
                foliageStatus_ = r.scene.found ? "no baked foliage" : "no foliage bank entry";
            }
            if (!r.things.instances.empty()) {
                renderer_.uploadThings(r.things, te::UpAxis::Y);
                bindInstances(r.things);
                foliageStatus_ += ", " + std::to_string(r.things.instances.size()) + " objects";
            } else if (r.things.found) {
                foliageStatus_ += ", no placed objects";
            }
            for (const auto& w : r.things.warnings) pushLog("objects: " + w, 1);
            for (const auto& w : r.scene.warnings)
                if (w.rfind("mesh", 0) != 0) pushLog("foliage: " + w, 1);
        }
        if (!foliagePendingName_.empty()) { foliagePendingName_.clear(); if (!foliageLoaded()) startFoliageLoad(); }
        else if (thingsReloadPending_) startThingsReload();
    }
    if (exportFuture_.valid() && exportFuture_.wait_for(0ms) == std::future_status::ready) {
        ExportResult r = exportFuture_.get();
        for (const auto& l : r.log) pushLog(l, l.rfind("warning", 0) == 0 ? 1 : (l.rfind("error", 0) == 0 ? 2 : 0));
        if (r.ok) {
            lastExportOk_ = true;
            char buf[64]; std::snprintf(buf, sizeof buf, " (%.1fs)", r.seconds);
            pushLog("Wrote " + fs::path(r.path).filename().string() + " + " + std::to_string(r.files.size() - 1) + " file(s)" + buf, 3);
        } else {
            pushLog("Export failed: " + batchCurrent_, 2);
        }
        if (batchTotal_ > 0) {
            ++batchDone_;
            if (!r.ok) ++batchFailed_;
            if (batchQueue_.empty()) {
                pushLog("Batch done: " + std::to_string(batchDone_ - batchFailed_) + " ok, " + std::to_string(batchFailed_) + " failed", batchFailed_ ? 1 : 3);
                batchTotal_ = 0;
            }
        }
    }
    if (batchTotal_ > 0 && !batchQueue_.empty() && !exportFuture_.valid()) {
        const std::string next = batchQueue_.front();
        batchQueue_.erase(batchQueue_.begin());
        if (const MapEntry* e = findEntry(next)) startExportOf(*e);
        else { ++batchDone_; ++batchFailed_; }
    }
    std::lock_guard<std::mutex> lock(logMutex_);
    for (auto& l : logPending_) {
        if (l.first >= 1) {
            toasts_.push_back({l.first, l.second, time_});
            if (toasts_.size() > 4) toasts_.erase(toasts_.begin());
        }
        log_.push_back(std::move(l));
    }
    logPending_.clear();
    if (log_.size() > 400) log_.erase(log_.begin(), log_.begin() + long(log_.size() - 400));
}

bool App::logContains(const std::string& needle) const {
    for (const auto& l : log_) if (l.second.find(needle) != std::string::npos) return true;
    return false;
}

void App::drawViewportOverlays(const ImVec2& origin, const ImVec2& size) {
    using theme::S;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    cursorHit_ = false;
    if (!renderer_.hasMesh() || !previewLoaded() || size.x <= 0 || size.y <= 0) return;
    ImGuiIO& io = ImGui::GetIO();
    // ground under the cursor: map-local Fable x/y (y north) and the height there
    if (viewportHovered_) {
        const float u = (io.MousePos.x - origin.x) / size.x, v = (io.MousePos.y - origin.y) / size.y;
        float o[3], d[3], hit[3];
        renderer_.screenRay(u, v, o, d);
        if (renderer_.rayTerrain(o, d, hit)) { cursorHit_ = true; cursorFable_[0] = hit[0]; cursorFable_[1] = -hit[2]; cursorFable_[2] = hit[1]; }
    }
    ImGui::PushFont(fontSmall_);
    const float rowH = ImGui::GetFrameHeight() + S(2);
    const float yChips = origin.y + size.y - rowH - S(10);   // the view-mode chip row
    if (cursorHit_) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "x %.1f   y %.1f   h %.1f", cursorFable_[0], cursorFable_[1], cursorFable_[2]);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        const ImVec2 p0(origin.x + S(14), yChips - ts.y - S(22));
        dl->AddRectFilled(p0, ImVec2(p0.x + ts.x + S(16), p0.y + ts.y + S(10)), theme::col(theme::Bg1) | 0xD0000000, S(6));
        dl->AddText(ImVec2(p0.x + S(8), p0.y + S(5)), theme::col(theme::Muted), buf);
    }
    // compass: where Fable north (+y, rendered as -z) points on screen, from two projected
    // points around the camera focus; the needle keeps its length whatever the pitch
    {
        float f[3]; camera_.focus(f);
        const float a[3] = {f[0], f[1], f[2]}, b[3] = {f[0], f[1], f[2] - 1.0f};
        float au, av, bu, bv;
        if (renderer_.project(a, au, av) && renderer_.project(b, bu, bv)) {
            float dx = (bu - au) * size.x, dy = (bv - av) * size.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-3f) {
                dx /= len; dy /= len;
                const float r = S(16);
                const ImVec2 c(origin.x + size.x - S(14) - r, yChips - r - S(26));
                dl->AddCircleFilled(c, r + S(4), theme::col(theme::Bg1) | 0xD0000000, 32);
                dl->AddCircle(c, r + S(4), theme::col(theme::Border), 32, 1.0f);
                const ImVec2 tip(c.x + dx * r, c.y + dy * r), tail(c.x - dx * r * 0.6f, c.y - dy * r * 0.6f);
                const ImVec2 side(-dy * S(4), dx * S(4));
                dl->AddTriangleFilled(tip, ImVec2(c.x + side.x, c.y + side.y), ImVec2(c.x - side.x, c.y - side.y), theme::col(theme::Accent));
                dl->AddTriangleFilled(tail, ImVec2(c.x - side.x, c.y - side.y), ImVec2(c.x + side.x, c.y + side.y), theme::col(theme::Muted));
                const ImVec2 ns = ImGui::CalcTextSize("N");
                dl->AddText(ImVec2(c.x + dx * (r + S(11)) - ns.x * 0.5f, c.y + dy * (r + S(11)) - ns.y * 0.5f), theme::col(theme::Text), "N");
            }
        }
    }
    ImGui::PopFont();
}

int App::confirmRow(const char* question, const char* yes, float width, float height, const char* widget) {
    using theme::S;
    ImGui::PushFont(fontSmall_);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextColored(theme::vec(theme::Warn), "%s", question);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    const float half = (width - S(6)) * 0.5f;
    int r = 0;
    if (theme::primaryButton(yes, ImVec2(half, height))) r = 1;
    auto_.registerWidget(widget);
    ImGui::SameLine(0, S(6));
    if (theme::ghostButton("Cancel", ImVec2(half, height))) r = -1;
    return r;
}

std::string App::jobLabel(const char* verb) const {
    std::string stage;
    { std::lock_guard<std::mutex> l(jobMutex_); stage = jobStage_; }
    char buf[192];
    std::snprintf(buf, sizeof buf, "%s: %s  (%d s)", verb, stage.c_str(), int(time_ - jobStart_));
    return buf;
}

void App::drawToasts(const ImVec2& origin, const ImVec2& size) {
    using theme::S;
    constexpr float kLife = 6.0f, kFade = 1.0f;
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return time_ - t.at > kLife; }), toasts_.end());
    if (toasts_.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = std::min(S(360), size.x - S(32));
    float y = origin.y + S(14);
    ImGui::PushFont(fontSmall_);
    for (const Toast& t : toasts_) {
        const float age = time_ - t.at;
        const float alpha = age < kLife - kFade ? 1.0f : std::max(0.0f, (kLife - age) / kFade);
        const ImVec2 ts = ImGui::CalcTextSize(t.text.c_str(), nullptr, false, w - S(34));
        const float h = ts.y + S(16);
        const ImVec2 p0(origin.x + size.x - S(16) - w, y), p1(p0.x + w, y + h);
        const auto withAlpha = [alpha](ImU32 c) { return (c & 0x00FFFFFF) | (ImU32(alpha * float(c >> 24)) << 24); };
        const theme::Color stripe = t.level == 3 ? theme::Success : t.level == 2 ? theme::Error : theme::Warn;
        dl->AddRectFilled(p0, p1, withAlpha(theme::col(theme::Bg1) | 0xF0000000), S(8.0f));
        dl->AddRect(p0, p1, withAlpha(theme::col(theme::Border)), S(8.0f));
        dl->AddRectFilled(p0, ImVec2(p0.x + S(4), p1.y), withAlpha(theme::col(stripe)), S(8.0f), ImDrawFlags_RoundCornersLeft);
        dl->AddText(nullptr, 0.0f, ImVec2(p0.x + S(16), p0.y + S(8)), withAlpha(theme::col(theme::Text)), t.text.c_str(), nullptr, w - S(34));
        y = p1.y + S(6);
    }
    ImGui::PopFont();
}

void App::pushLog(const std::string& line, int level) {
    std::lock_guard<std::mutex> lock(logMutex_);
    logPending_.emplace_back(level, line);
}

void App::setFilter(const std::string& f) {
    filter_ = f;
    std::snprintf(filterBuf_, sizeof filterBuf_, "%s", f.c_str());
}

std::vector<std::string> App::stateDump() const {
    std::vector<std::string> v;
    v.push_back("install=" + installPath_);
    v.push_back("install_valid=" + std::string(installValid_ ? "1" : "0"));
    { const InstallHealth h = installHealth(); v.push_back("install_textures=" + std::string(h.texturesBig ? "1" : "0")); v.push_back("install_fse=" + std::string(h.fse ? "1" : "0")); v.push_back("setup_open=" + std::string(setupOpen_ ? "1" : "0")); }
    if (installValid_) { size_t d = 0; for (const auto& e : backupList_) d += e.differs; v.push_back("backups_differ=" + std::to_string(d)); }
    v.push_back("maps=" + std::to_string(maps_.size()));
    v.push_back("selected=" + selectedName_);
    v.push_back("settings_scroll=" + std::to_string(int(settingsScroll_)));
    v.push_back(std::string("settings_scrolled=") + (settingsScroll_ > 0.5f ? "1" : "0"));
    { ImGuiContext& g = *ImGui::GetCurrentContext(); v.push_back(std::string("hovered_window=") + (g.HoveredWindow ? g.HoveredWindow->Name : "-")); v.push_back("mouse=" + std::to_string(int(g.IO.MousePos.x)) + "," + std::to_string(int(g.IO.MousePos.y))); if (g.HoveredWindow) v.push_back("hovered_scrollmax=" + std::to_string(int(g.HoveredWindow->ScrollMax.y)));
      v.push_back(std::string("wheeling_window=") + (g.WheelingWindow ? g.WheelingWindow->Name : "-") + " scrolled_frame=" + std::to_string(g.WheelingWindowScrolledFrame) + " frame=" + std::to_string(g.FrameCount) + " hovered_flags=" + std::to_string(g.HoveredWindow ? g.HoveredWindow->Flags : 0) + " parent=" + (g.HoveredWindow && g.HoveredWindow->ParentWindow ? g.HoveredWindow->ParentWindow->Name : "-") + " parent_scrollmax=" + std::to_string(g.HoveredWindow && g.HoveredWindow->ParentWindow ? int(g.HoveredWindow->ParentWindow->ScrollMax.y) : -1)); }
    v.push_back("preview_loaded=" + std::string(previewLoaded() ? "1" : "0"));
    v.push_back("preview_textured=" + std::string(previewTextured_ ? "1" : "0"));
    v.push_back("context_ready=" + std::string(ctx_.ready() ? "1" : "0"));
    v.push_back("mode=" + std::string(kModeNames[int(mode_)]));
    v.push_back("rule_notice=" + (ruleKey_.empty() ? std::string("-") : ruleKey_));
    v.push_back("edit_tab=" + std::to_string(editTab_));
    v.push_back("toasts=" + std::to_string(toasts_.size()));
    v.push_back("help_open=" + std::string(helpOpen_ ? "1" : "0"));
    v.push_back("tour_step=" + std::to_string(tourStep_));
    { char b[16]; std::snprintf(b, sizeof b, "%.2f", settings_.uiScale); v.push_back(std::string("ui_scale=") + b); }
    v.push_back("grid=" + std::string(renderer_.showGrid ? "1" : "0"));
    v.push_back("selection_count=" + std::to_string(selectionCount()));
    v.push_back("presets=" + std::to_string(presets().size()));
    v.push_back("effects=" + std::to_string(effectNames_.size()));
    { const auto e = currentEntrance(); char b[64]; if (e) std::snprintf(b, sizeof b, "%.1f,%.1f,%.1f", e->pos[0], e->pos[1], e->pos[2]); v.push_back(std::string("entrance=") + (e ? b : "-")); }
    if (cursorHit_) { char b[64]; std::snprintf(b, sizeof b, "%.1f,%.1f,%.1f", cursorFable_[0], cursorFable_[1], cursorFable_[2]); v.push_back(std::string("cursor_ground=") + b); } else v.push_back("cursor_ground=-");
    v.push_back("export_ok=" + std::string(lastExportOk_ ? "1" : "0"));
    v.push_back("export_path=" + lastExportPath_);
    v.push_back("format=" + std::string(settings_.format == 0 ? "glb" : "obj"));
    v.push_back("textures=" + std::string(settings_.textures ? "1" : "0"));
    v.push_back("layers=" + std::string(settings_.layers ? "1" : "0"));
    v.push_back("walkable=" + std::string(settings_.walkable ? "1" : "0"));
    v.push_back("texels=" + std::to_string(settings_.texels));
    { char g[32]; std::snprintf(g, sizeof g, "%.2f", settings_.gain); v.push_back(std::string("gain=") + g); }
    v.push_back("mesh_vertices=" + std::to_string(previewScene_.vertices.size()));
    v.push_back("batch_active=" + std::string(batchActive() ? "1" : "0"));
    v.push_back("foliage_loaded=" + std::string(foliageLoaded() ? "1" : "0"));
    v.push_back("foliage_instances=" + std::to_string(foliageInstances_));
    v.push_back("preview_foliage=" + std::string(previewFoliage_ ? "1" : "0"));
    v.push_back("export_foliage=" + std::string(settings_.foliage ? "1" : "0"));
    v.push_back("thing_instances=" + std::to_string(thingInstances_));
    v.push_back("preview_things=" + std::string(previewThings_ ? "1" : "0"));
    v.push_back("preview_water=" + std::string(renderer_.showWater ? "1" : "0"));
    v.push_back("export_things=" + std::string(settings_.things ? "1" : "0"));
    v.push_back("world=" + std::string(settings_.world ? "1" : "0"));
    if (const MapEntry* e = findEntry(selectedName_)) v.push_back("region=" + e->group);
    v.push_back("batch_done=" + std::to_string(batchDone_));
    v.push_back("batch_failed=" + std::to_string(batchFailed_));
    v.push_back("filter=" + filter_);
    char cam[96];
    std::snprintf(cam, sizeof cam, "%.2f,%.2f,%.2f", camera_.posX, camera_.posY, camera_.posZ);
    v.push_back(std::string("camera=") + cam);
    v.push_back("edit_mode=" + std::string(editMode_ ? "1" : "0"));
    v.push_back("world_mode=" + std::string(worldMode_ ? "1" : "0"));
    v.push_back("textures_mode=" + std::string(texturesMode_ ? "1" : "0"));
    v.push_back("textures_count=" + std::to_string(texRows_.size()));
    { const auto* t = selectedTexture(); v.push_back("texture_selected=" + (t ? t->label : std::string("-"))); }
    v.push_back("world_loaded=" + std::string(worldLoaded_ ? "1" : "0"));
    v.push_back("world_maps=" + std::to_string(world_.maps.size()));
    v.push_back("world_selected=" + worldSelected_);
    v.push_back("world_pending=" + std::to_string(worldPending_.size()));
    v.push_back("world_pending_owners=" + std::to_string(worldOwnerEdits_.size()));
    v.push_back("world_can_undo=" + std::string(worldCanUndo() ? "1" : "0"));
    v.push_back("world_can_redo=" + std::string(worldCanRedo() ? "1" : "0"));
    v.push_back("world_pending_sees=" + std::to_string(worldSeesEdits_.size()));
    if (!worldSelected_.empty()) v.push_back("world_selected_owner=" + worldOwnerOf(worldSelected_));
    v.push_back("world_ok=" + std::string(worldLastOk_ ? "1" : "0"));
    v.push_back("world_stitch=" + std::string(worldStitch_ ? "1" : "0"));
    v.push_back("paint_theme=" + std::to_string(paintTheme_));
    v.push_back("link_installed=" + std::string(installValid_ && livelink::isInstalled(installPath_) ? "1" : "0"));
    v.push_back("link_ready=" + std::string(link_.ready ? "1" : "0"));
    v.push_back("link_hero_map=" + link_.heroMap);
    if (documentLoaded()) {
        v.push_back("villages=" + std::to_string(doc_.villages().size()));
        if (selectedThing_ >= 0) {
            const uint64_t vu = doc_.villageOf(size_t(selectedThing_));
            std::string vn = vu ? std::to_string(vu) : "0";
            for (const auto& vv : doc_.villages()) if (vv.uid == vu && !vv.scriptName.empty()) vn = vv.scriptName;
            v.push_back("selected_village=" + vn);
        }
        if (selectedThing_ >= 0) v.push_back("selected_uid=" + std::to_string(selectedUid_));
    }
    if (documentLoaded() && doc_.level()) { size_t named = 0; for (const auto& g : doc_.level()->groundThemes()) named += !g.name.empty(); v.push_back("palette_named=" + std::to_string(named)); }
    if (const auto* wb = world_.find(worldSelected_)) { int wx = 0, wy = 0; worldPlacement(wb->name, wx, wy); v.push_back("world_selected_pos=" + std::to_string(wx) + "," + std::to_string(wy)); }
    v.push_back("doc_loaded=" + std::string(documentLoaded() ? "1" : "0"));
    v.push_back("doc_things=" + std::to_string(documentLoaded() ? doc_.thingCount() : 0));
    v.push_back("doc_dirty=" + std::string(documentLoaded() && doc_.dirty() ? "1" : "0"));
    v.push_back("doc_changes=" + std::to_string(documentLoaded() ? doc_.changes().size() : 0));
    v.push_back("selected_thing=" + std::to_string(selectedThing_));
    if (documentLoaded() && selectedThing_ >= 0) {
        v.push_back("selected_def=" + doc_.summary(size_t(selectedThing_)).definition);
        editor::Frame f;
        if (doc_.frameOf(size_t(selectedThing_), f)) {
            char p[128];
            std::snprintf(p, sizeof p, "%.3f,%.3f,%.3f", f.pos[0], f.pos[1], f.pos[2]);
            v.push_back(std::string("selected_pos=") + p);
            std::snprintf(p, sizeof p, "%.3f", f.scale);
            v.push_back(std::string("selected_scale=") + p);
            if (const auto g = doc_.terrainHeight(f.pos[0], f.pos[1])) {   // z above the (live) ground
                std::snprintf(p, sizeof p, "%.2f", f.pos[2] - *g);
                v.push_back(std::string("selected_ground_delta=") + p);
            }
        }
    }
    v.push_back("gizmo=" + std::to_string(gizmoOp_));
    v.push_back("terrain_dirty=" + std::string(documentLoaded() && doc_.hasTerrain() && doc_.terrainDirty() ? "1" : "0"));
    if (documentLoaded() && doc_.hasTerrain()) {
        char h[64];
        const auto& t = doc_.terrain();
        const int cx = doc_.cellsX(), cy = doc_.cellsY();
        std::snprintf(h, sizeof h, "%.3f", t.heights[size_t(cy / 2) * cx + cx / 2]);
        v.push_back(std::string("terrain_center_height=") + h);
    }
    return v;
}

// ------------------------------------------------------------------ drawing

void App::frame(float dt) {
    time_ += dt;
    pollWorkers();
    ImGuizmo::BeginFrame();
    syncInstances();
    syncTerrain();
    editorShortcuts();
    if (terrainDeployFuture_.valid() && terrainDeployFuture_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        const TerrainDeployResult r = terrainDeployFuture_.get();
        for (const auto& n : r.notes) pushLog("terrain: " + n, 0);
        if (r.ok) pushLog("terrain saved into the game (start a new game or re-enter the region to see it)", 3);
        else pushLog("terrain save failed: " + r.error, 2);
    }
    if (worldFuture_.valid() && worldFuture_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        const WorldJob r = worldFuture_.get();
        for (const auto& n : r.notes) pushLog("world: " + n, 0);
        worldLastOk_ = r.ok;
        if (r.ok) pushLog("world: maps moved (start a new game to walk the new layout)", 3);
        else pushLog("world: move failed: " + r.error, 2);
        worldLoaded_ = false;
        loadWorld();
        if (r.ok && (saveRoot_.empty() || saveRoot_ == installPath_)) { const std::string root = installPath_; scanInstall(root); }
    }
    if (newLevelFuture_.valid() && newLevelFuture_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        const NewLevelJob r = newLevelFuture_.get();
        for (const auto& n : r.result.notes) pushLog("new level: " + n, 0);
        if (r.ok) {
            pushLog("new level " + r.name + " installed (map slot " + std::to_string(r.result.mapSlot) + ", origin " + std::to_string(r.result.worldX) + "," + std::to_string(r.result.worldY) + ")", 3);
            if (r.ownRegion) raiseRule("region");
            newLevelDonor_.clear();
            worldLoaded_ = false;   // the World tab re-reads the layout with the new map
            if (saveRoot_.empty() || saveRoot_ == installPath_) {
                // the WAD has a new entry: rescan, then open the copy (the texture context is unchanged)
                const std::string root = installPath_;
                scanInstall(root);
                discardEdits_ = true;
                selectMap(r.name);
            } else {
                pushLog("new level written under the save root " + saveRoot_ + " (not the explorer's install)", 1);
            }
        } else {
            pushLog("new level failed: " + r.error, 2);
        }
    }
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) focusFilter_ = true;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E) && !exportFuture_.valid() && !selectedName_.empty()) startExport();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !filter_.empty() && !ImGui::IsAnyItemActive()) setFilter("");
        // ? (shift+/ on most layouts) or F1: the shortcut cheat-sheet; Escape closes it
        if (!io.WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_F1) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Slash)))) helpOpen_ = !helpOpen_;
        if (helpOpen_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) helpOpen_ = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    {
        // Size factor: a 1200 px tall window is 1.0; 1080p runs at 0.9, 1440p at 1.2,
        // anything shorter than ~1000 px at 0.85. Snapped to 0.05 so a resize does not
        // rebuild fonts every frame.
        const float sizeFactor = std::clamp(vp->Size.y / 1200.0f, 0.85f, 1.25f);
        wantScale_ = std::round(dpiScale_ * sizeFactor * settings_.uiScale * 20.0f) / 20.0f;
    }
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    drawTitleBar();

    const float total = ImGui::GetContentRegionAvail().x;
    const float left = std::clamp(total * 0.22f, theme::S(230.0f), theme::S(320.0f));
    const float right = std::clamp(total * 0.26f, theme::S(300.0f), theme::S(400.0f));
    const float middle = std::max(total - left - right, theme::S(200.0f));

    drawExplorer(left);
    ImGui::SameLine(0, 0);
    drawViewport(middle);
    ImGui::SameLine(0, 0);
    drawActions(right);
    drawUnsavedPrompt();
    if (firstRun_ && !auto_.active()) { firstRun_ = false; setupOpen_ = true; tourPending_ = true; }
    drawSetupPanel();
    drawHelpOverlay();
    drawTour();

    ImGui::End();
}

void App::drawTitleBar() {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    using theme::S;
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = S(52.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::col(theme::Bg1));
    dl->AddLine(ImVec2(p.x, p.y + h), ImVec2(p.x + w, p.y + h), theme::col(theme::Border));
    // accent mark
    dl->AddRectFilled(ImVec2(p.x + S(18), p.y + S(14)), ImVec2(p.x + S(24), p.y + h - S(14)), theme::col(theme::Accent), S(3.0f));

    ImGui::SetCursorScreenPos(ImVec2(p.x + S(36), p.y + S(11)));
    ImGui::PushFont(fontTitle_);
    ImGui::TextUnformatted("Albion Atlas");
    const float titleEnd = ImGui::GetItemRectMax().x;
    ImGui::PopFont();
    const float btnW = S(92.0f);
    const float subtitleW = ImGui::CalcTextSize("v" ALBION_VERSION "   Fable: The Lost Chapters map exporter").x;
    if (w > S(760)) {
        ImGui::SameLine(0, S(12));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(8));
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Faint), "v" ALBION_VERSION "   Fable: The Lost Chapters map exporter");
        ImGui::PopFont();
    }

    // install status (right side), shortened from the left when there is no room
    ImGui::PushFont(fontSmall_);
    // a redirected save root (scripted runs, scratch trees) is the one thing a writer
    // must not miss: every write goes there, not into the install shown
    const bool redirected = !saveRoot_.empty() && saveRoot_ != installPath_;
    std::string status = !installValid_ ? "no install selected" : redirected ? "writes -> " + saveRoot_ : installPath_;
    const float statusMax = std::max(S(120.0f), p.x + w - btnW - S(108) - (titleEnd + (w > S(760) ? subtitleW + S(24) : S(12))));
    if (ImGui::CalcTextSize(status.c_str()).x > statusMax) {
        while (status.size() > 4 && ImGui::CalcTextSize(("..." + status).c_str()).x > statusMax) status.erase(0, 1);
        status = "..." + status;
    }
    const float statusW = ImGui::CalcTextSize(status.c_str()).x;
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - statusW - btnW - S(78), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f));
    const ImU32 dot = installValid_ ? (ctx_.ready() ? theme::col(theme::Success) : theme::col(theme::Warn))
                                    : theme::col(theme::Error);
    dl->AddCircleFilled(ImVec2(ImGui::GetCursorScreenPos().x - S(12), ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f), S(4.0f), dot);
    ImGui::TextColored(theme::vec(redirected ? theme::Warn : theme::Muted), "%s", status.c_str());
    if (ImGui::IsItemHovered()) {
        const InstallHealth h = installHealth();
        ImGui::SetTooltip("%s  (%s)\ntextures.big %s   ForgeFSE %s   saves %s%s%s\nclick for the setup check", installValid_ ? installPath_.c_str() : "no install", installSource_.c_str(),
                          h.texturesBig ? "yes" : "no", h.fse ? "yes" : "no", h.saves ? "yes" : "no",
                          redirected ? "\nwrites go to " : "", redirected ? saveRoot_.c_str() : "");
    }
    if (ImGui::IsItemClicked()) setupOpen_ = true;
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - btnW - S(18) - S(38), p.y + (h - S(30)) * 0.5f));
    if (theme::ghostButton("?", ImVec2(S(30), S(30)))) helpOpen_ = !helpOpen_;
    auto_.registerWidget("btn_help");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keyboard and mouse cheat-sheet  (? or F1)");
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - btnW - S(18), p.y + (h - S(30)) * 0.5f));
    if (theme::ghostButton("Change...", ImVec2(btnW, S(30)))) {
        const std::string picked = pickFolder(hwnd_, installPath_);
        if (!picked.empty()) { installSource_ = "manual"; scanInstall(picked); }
    }
    auto_.registerWidget("btn_change_install");
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 1));
}

void App::drawExplorer(float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg1));
    ImGui::BeginChild("##explorer", ImVec2(width, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    const ImVec2 p0 = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p0.x + width - 1, p0.y), ImVec2(p0.x + width - 1, p0.y + ImGui::GetWindowHeight()), theme::col(theme::Border));

    using theme::S;
    ImGui::SetCursorPos(ImVec2(S(16), S(14)));
    ImGui::PushFont(fontBold_);
    ImGui::TextColored(theme::vec(theme::Muted), "MAPS");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushFont(fontSmall_);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
    if (!filter_.empty()) {
        size_t shownCount = 0;
        const std::string lf = lower(filter_);
        for (const auto& m : maps_) if (lower(m.name).find(lf) != std::string::npos) ++shownCount;
        ImGui::TextColored(theme::vec(theme::Faint), "%zu / %zu", shownCount, maps_.size());
    } else {
        ImGui::TextColored(theme::vec(theme::Faint), "%zu", maps_.size());
    }
    ImGui::PopFont();

    ImGui::SetCursorPosX(S(16));
    ImGui::SetNextItemWidth(width - S(32));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(7)));
    if (focusFilter_) { ImGui::SetKeyboardFocusHere(); focusFilter_ = false; }
    if (ImGui::InputTextWithHint("##filter", "Search maps...   (Ctrl+F)", filterBuf_, sizeof filterBuf_)) filter_ = filterBuf_;
    ImGui::PopStyleVar();
    auto_.registerWidget("input_filter");
    ImGui::Dummy(ImVec2(0, S(6)));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg1));
    ImGui::BeginChild("##maplist", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    if (maps_.empty()) {
        ImGui::SetCursorPos(ImVec2(S(16), S(20)));
        ImGui::PushTextWrapPos(width - S(24));
        ImGui::TextColored(theme::vec(theme::Faint),
                           installValid_ ? "Reading FinalAlbion.wad..." :
                           "No install found.\n\nClick \"Change...\" and pick your\n\"Fable The Lost Chapters\" folder.");
        ImGui::PopTextWrapPos();
    } else {
        const std::string f = lower(filter_);
        std::map<std::string, int> groupCount;
        for (const auto& m : maps_)
            if (f.empty() || lower(m.name).find(f) != std::string::npos) ++groupCount[m.group];
        std::string currentGroup;
        bool groupVisible = true;
        int shown = 0;
        for (const auto& m : maps_) {
            if (!f.empty() && lower(m.name).find(f) == std::string::npos) continue;
            ++shown;
            const bool single = groupCount[m.group] <= 1 && m.group != "Loose files";
            if (!single && m.group != currentGroup) {
                currentGroup = m.group;
                bool& open = groupOpen_[m.group];
                if (!f.empty()) open = true;          // searching expands
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8), S(5)));
                ImGui::SetCursorPosX(S(8));
                ImGui::PushFont(fontBold_);
                ImGui::PushStyleColor(ImGuiCol_Header, theme::vec(theme::Bg1));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::vec(theme::Bg2));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme::vec(theme::Bg2));
                ImGui::SetNextItemOpen(open);
                const std::string title = m.group + "  ";
                groupVisible = ImGui::TreeNodeEx(m.group.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding, "%s", m.group.c_str());
                ImGui::SameLine();
                ImGui::PushFont(fontSmall_);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(3));
                ImGui::TextColored(theme::vec(theme::Faint), "%d", groupCount[m.group]);
                ImGui::PopFont();
                open = groupVisible;
                ImGui::PopStyleColor(3);
                ImGui::PopFont();
                ImGui::PopStyleVar();
                auto_.registerWidget(("group_" + m.group).c_str());
            } else if (single) {
                currentGroup.clear();
                groupVisible = true;
            }
            if (!groupVisible) continue;
            const bool selected = m.key == selectedName_;
            ImGui::SetCursorPosX(S(26));
            ImGui::PushID(m.key.c_str());
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8), S(5)));
            ImGui::PushStyleColor(ImGuiCol_Header, theme::vec(theme::AccentSoft));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::vec(theme::Bg2));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme::vec(theme::AccentSoft));
            if (selected) ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::AccentText));
            if (ImGui::Selectable(m.name.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(0, S(24)))) selectMap(m.key);
            if (selected) ImGui::PopStyleColor();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            if (selected) auto_.registerWidget("row_selected");
            if (selected && scrollToSelected_) { ImGui::SetScrollHereY(0.5f); scrollToSelected_ = false; }
            auto_.registerWidget(("row_" + m.key).c_str());
            if (!m.loosePath.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.loosePath.c_str());
            ImGui::PopID();
        }
        if (shown == 0) {
            ImGui::SetCursorPos(ImVec2(S(16), S(20)));
            ImGui::TextColored(theme::vec(theme::Faint), "No map matches \"%s\"", filter_.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

// Unreal-editor viewport grammar:
//   RMB hold      look around; WASD fly, Q/E down/up, Shift = 3x, wheel = fly speed
//   LMB drag      dolly forward/back (mouse Y) + turn (mouse X)
//   MMB drag      track (pan) in the view plane
//   Alt + LMB     orbit around the focus point
//   wheel         dolly towards the focus point
//   F             frame the whole map
void App::handleViewportInput(const ImVec2& origin, const ImVec2& size) {
    ImGuiIO& io = ImGui::GetIO();
    if (!renderer_.hasMesh()) return;
    const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    // Keep flying while RMB is held even if the cursor leaves the image.
    const bool active = viewportHovered_ || viewportCaptured_;
    viewportCaptured_ = active && (rmb || ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle));
    if (!active) return;
    const float dx = io.MouseDelta.x, dy = io.MouseDelta.y;
    const float panK = camera_.distance / std::max(size.y, 1.0f) * 1.6f;

    if (rmb) {
        camera_.look(-dx * 0.005f, dy * 0.005f);
        if (io.MouseWheel != 0) camera_.flySpeed = std::clamp(camera_.flySpeed * std::pow(1.25f, io.MouseWheel), 0.5f, 5000.0f);
        float fwd = 0, strafe = 0, rise = 0;
        if (ImGui::IsKeyDown(ImGuiKey_W)) fwd += 1; if (ImGui::IsKeyDown(ImGuiKey_S)) fwd -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_D)) strafe += 1; if (ImGui::IsKeyDown(ImGuiKey_A)) strafe -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_E)) rise += 1; if (ImGui::IsKeyDown(ImGuiKey_Q)) rise -= 1;
        const float boost = io.KeyShift ? 3.0f : 1.0f;
        if (fwd || strafe || rise) camera_.fly(fwd * boost, strafe * boost, rise * boost, std::min(io.DeltaTime, 0.1f));
    } else {
        const bool terrainTool = editMode_ && gizmoOp_ == 4 && documentLoaded() && doc_.hasTerrain();
        const bool gizmo = editMode_ && (ImGuizmo::IsOver() || ImGuizmo::IsUsing() || terrainTool);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && viewportHovered_ && !gizmo) { clickArmed_ = true; clickPos_ = io.MousePos; }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) && !gizmo) {
            if (io.KeyAlt) camera_.orbit(-dx * 0.008f, dy * 0.008f);
            else { camera_.turn(-dx * 0.005f); camera_.dolly(-dy * 0.02f); }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && clickArmed_) {
            clickArmed_ = false;
            const float mx = io.MousePos.x - clickPos_.x, my = io.MousePos.y - clickPos_.y;
            if (editMode_ && !gizmo && mx * mx + my * my < 16.0f && size.x > 0 && size.y > 0)
                pickAt((io.MousePos.x - origin.x) / size.x, (io.MousePos.y - origin.y) / size.y);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) camera_.pan(-dx * panK, dy * panK);
        if (io.MouseWheel != 0 && viewportHovered_ && !ImGuizmo::IsUsing()) camera_.dolly(io.MouseWheel);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl && !ImGui::IsAnyItemActive()) { if (editMode_ && selectedThing_ >= 0) frameSelected(); else frameMap(); }
}

void App::frameMap() {
    if (!renderer_.hasMesh()) return;
    const float w = float(previewScene_.mapWidth), h = float(previewScene_.mapHeight);
    const float span = std::max({w, h, 8.0f});
    camera_.lookAt(w * 0.5f, (previewScene_.minHeight + previewScene_.maxHeight) * 0.5f, -h * 0.5f, 0.8f, 0.62f, span * 0.95f);
}

void App::drawViewport(float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
    ImGui::BeginChild("##viewport", ImVec2(width, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (worldMode_) {
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##worldcanvas", ImVec2(std::max(size.x, 8.0f), std::max(size.y, 8.0f)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        viewportOrigin_ = origin; viewportSize_ = size;
        viewportHovered_ = ImGui::IsItemHovered();
        drawWorldCanvas(origin, size);
        auto_.registerWidget("viewport");
        drawToasts(origin, size);
        ImGui::EndChild();
        return;
    }
    ID3D11ShaderResourceView* srv = renderer_.render(uint32_t(std::max(size.x, 8.0f)), uint32_t(std::max(size.y, 8.0f)), camera_, mode_, time_);
    if (srv) {
        ImGui::SetCursorScreenPos(origin);
        ImGui::Image((ImTextureID)(intptr_t)srv, size);
        viewportOrigin_ = origin; viewportSize_ = size;
        viewportHovered_ = ImGui::IsItemHovered();
        handleViewportInput(origin, size);
        terrainInput(origin, size);
        drawGizmo(origin, size);
        drawBrushCursor(origin, size);
        drawViewportOverlays(origin, size);
    }
    auto_.registerWidget("viewport");
    drawToasts(origin, size);

    using theme::S;
    // Empty state / loading overlay.
    const bool loading = previewFuture_.valid();
    if (!renderer_.hasMesh() || loading) {
        ImGui::PushFont(fontBold_);
        const MapEntry* pend = findEntry(previewPendingName_.empty() ? selectedName_ : previewPendingName_);
        std::string msg = loading ? "Loading " + (pend ? pend->name : selectedName_) : "Pick a map on the left";
        if (loading) {
            const char* dots[] = {"", ".", "..", "..."};
            msg += dots[int(time_ * 3) % 4];
        }
        const ImVec2 ts = ImGui::CalcTextSize(msg.c_str());
        const ImVec2 c(origin.x + (size.x - ts.x) * 0.5f, origin.y + (size.y - ts.y) * 0.5f);
        if (!renderer_.hasMesh()) {
            // soft accent ring
            dl->AddCircle(ImVec2(origin.x + size.x * 0.5f, c.y - S(44)), S(26.0f), theme::col(theme::AccentSoft), 48, S(6.0f));
            dl->AddCircle(ImVec2(origin.x + size.x * 0.5f, c.y - S(44)), S(26.0f), theme::col(theme::Accent), 48, S(2.0f));
        }
        if (loading) dl->AddRectFilled(ImVec2(c.x - S(14), c.y - S(8)), ImVec2(c.x + ts.x + S(14), c.y + ts.y + S(8)), theme::col(theme::Bg1) | 0xE0000000, S(8.0f));
        dl->AddText(c, theme::col(loading ? theme::Text : theme::Muted), msg.c_str());
        ImGui::PopFont();
    }

    // HUD: map name + stats (top-left)
    if (renderer_.hasMesh() && previewLoaded()) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + S(16), origin.y + S(14)));
        ImGui::PushFont(fontTitle_);
        const MapEntry* cur = findEntry(previewLoadedFor_);
        ImGui::TextUnformatted(cur ? cur->name.c_str() : previewLoadedFor_.c_str());
        if (cur && !cur->loosePath.empty()) {
            ImGui::SameLine(0, S(10));
            ImGui::PushFont(fontSmall_);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(9));
            ImGui::TextColored(theme::vec(theme::Faint), "%s", cur->loosePath.c_str());
            ImGui::PopFont();
        }
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(origin.x + S(16), origin.y + S(44)));
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Muted), "%d x %d cells   |   %zu vertices   |   height %.1f .. %.1f%s",
                           previewScene_.mapWidth, previewScene_.mapHeight,
                           previewScene_.vertices.size(), previewScene_.minHeight, previewScene_.maxHeight,
                           previewTextured_ ? "" : "   |   textures loading...");
        if (previewFoliage_ || previewThings_) {
            ImGui::SameLine(0, 0);
            if (foliageFuture_.valid()) ImGui::TextColored(theme::vec(theme::Faint), "   |   plants + objects loading...");
            else if (!foliageStatus_.empty()) ImGui::TextColored(theme::vec(theme::Faint), "   |   %s", foliageStatus_.c_str());
        }
        if (previewTextured_ && previewScene_.unresolvedThemes > 0) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + S(16), origin.y + S(64)));
            ImGui::TextColored(theme::vec(theme::Warn), "%d of %zu ground themes have no texture in this install (shown grey)",
                               previewScene_.unresolvedThemes, previewScene_.themes.size());
        }
        ImGui::PopFont();
    }

    // Chips along the bottom: view modes on the left, layers on the right. When the
    // viewport is too narrow for one row the layer chips move up onto a second row.
    {
        ImGui::PushFont(fontSmall_);
        const float gap = S(6), rowH = ImGui::GetFrameHeight() + S(2);
        auto chipW = [&](const char* t) { return ImGui::CalcTextSize(t).x + S(24); };
        float modesW = 0; for (int i = 0; i < 4; ++i) modesW += chipW(kModeNames[i]) + gap;
        modesW += chipW("Frame  (F)") + S(8);
        const char* layerNames[4] = {"Foliage", "Objects", "Water", "Grid"};
        float layersW = 0; for (const char* n : layerNames) layersW += chipW(n) + gap;
        const bool twoRows = modesW + layersW + ImGui::CalcTextSize("Show:").x + S(40) > size.x;
        const float yModes = origin.y + size.y - rowH - S(10);
        const float yLayers = twoRows ? yModes - rowH - S(4) : yModes;
        float x = origin.x + S(14);
        for (int i = 0; i < 4; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(x, yModes));
            const bool on = int(mode_) == i;
            if (theme::chip(kModeNames[i], on)) mode_ = ViewMode(i);
            auto_.registerWidget((std::string("chip_") + lower(kModeNames[i])).c_str());
            x += ImGui::GetItemRectSize().x + gap;
        }
        ImGui::SetCursorScreenPos(ImVec2(x + S(8), yModes));
        if (theme::chip("Frame  (F)", false)) frameMap();
        auto_.registerWidget("chip_reset");
        const float modesEnd = ImGui::GetItemRectMax().x;
        // layer chips, right-aligned, with a "Show:" caption so nobody mistakes them
        // for export settings (those are the toggles in the right panel)
        float lx = origin.x + size.x - S(14) - layersW + gap;
        if (!twoRows && lx < modesEnd + S(16)) lx = modesEnd + S(16);
        {
            const ImVec2 cs = ImGui::CalcTextSize("Show:");
            dl->AddText(ImVec2(lx - cs.x - S(8), yLayers + (rowH - cs.y) * 0.5f), theme::col(theme::Faint), "Show:");
        }
        ImGui::SetCursorScreenPos(ImVec2(lx, yLayers));
        if (theme::chip("Foliage", previewFoliage_)) setPreviewFoliage(!previewFoliage_);
        auto_.registerWidget("chip_foliage");
        ImGui::SameLine(0, gap);
        if (theme::chip("Objects", previewThings_)) setPreviewThings(!previewThings_);
        auto_.registerWidget("chip_things");
        ImGui::SameLine(0, gap);
        if (theme::chip("Water", renderer_.showWater)) renderer_.showWater = !renderer_.showWater;
        auto_.registerWidget("chip_water");
        ImGui::SameLine(0, gap);
        if (theme::chip("Grid", renderer_.showGrid)) renderer_.showGrid = !renderer_.showGrid;
        auto_.registerWidget("chip_grid");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The LEV cell grid (1 unit), heavier every 8 cells (one terrain patch).");
        const char* hint = "RMB look + WASD fly   LMB dolly/turn   MMB pan   Alt+LMB orbit   Wheel zoom   F frame";
        const ImVec2 hs = ImGui::CalcTextSize(hint);
        const float hintRight = lx - ImGui::CalcTextSize("Show:").x - S(8) - S(28);   // clear of the "Show:" caption
        if (!twoRows && hintRight - hs.x > modesEnd + S(16))
            dl->AddText(ImVec2(hintRight - hs.x, yModes + (rowH - hs.y) * 0.5f), theme::col(theme::Faint), hint);
        else if (renderer_.hasMesh() && previewLoaded() && size.y > S(300))
            dl->AddText(ImVec2(origin.x + S(16), origin.y + S(64) + (previewTextured_ && previewScene_.unresolvedThemes > 0 ? ImGui::GetTextLineHeight() : 0)), theme::col(theme::Faint), hint);
        ImGui::PopFont();
    }
    ImGui::EndChild();
}

void App::drawActions(float width) {
    using theme::S;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg1));
    ImGui::BeginChild("##actions", ImVec2(width, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    const ImVec2 p0 = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p0.x, p0.y), ImVec2(p0.x, p0.y + ImGui::GetWindowHeight()), theme::col(theme::Border));
    const float pad = S(16), inner = width - 2 * pad;
    // Footer: primary Export + batch/open-folder rows. Always visible, never scrolls away.
    const bool showOpen = lastExportOk_ && !exportFuture_.valid() && !batchActive();
    const MapEntry* footerEntry = findEntry(selectedName_);
    const bool showRegion = footerEntry && regions_.loaded && regions_.mapsOfRegion.count(footerEntry->group) && regionMapKeys(footerEntry->group).size() > 1 && !batchActive();
    const float footerHeight = texturesMode_ ? S(60) : worldMode_ ? S(42 + 8 + 30 + 16) : editMode_ ? S(42 + 8 + 32 + 16) : S(42 + 8 + 32 + 16) + (showOpen ? S(40) : 0) + (showRegion ? S(40) : 0) + (batchActive() ? S(40) : 0);
    // The settings stack takes what it needs (measured last frame); the activity log
    // takes the rest, never less than a few lines. On a short window the settings
    // scroll instead of pushing the export button off screen.
    const float availH = ImGui::GetContentRegionAvail().y;
    const float logMin = S(72), logHeader = S(30);
    float settingsH = settingsContentH_ > 0 ? settingsContentH_ + S(8) : availH * 0.6f;
    settingsH = std::min(settingsH, availH - footerHeight - logHeader - logMin);
    settingsH = std::max(settingsH, S(120));
    const float logHeight = std::max(logMin, availH - settingsH - footerHeight - logHeader);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg1));
    ImGui::BeginChild("##settings", ImVec2(width, settingsH), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    settingsScroll_ = ImGui::GetScrollY();

    ImGui::SetCursorPos(ImVec2(pad, S(12)));
    {
        int tab = texturesMode_ ? 3 : worldMode_ ? 2 : editMode_ ? 1 : 0;
        if (theme::segmented("##paneltab", tab, {"Export", "Edit", "World", "Textures"}, inner)) {
            if (tab == 3) setTexturesMode(true);
            else if (tab == 2) { setTexturesMode(false); setWorldMode(true); }
            else { setTexturesMode(false); setWorldMode(false); if (editMode_ != (tab == 1)) setEditMode(tab == 1); }
        }
        auto_.registerWidget("seg_panel");
    }
    ImGui::Dummy(ImVec2(0, S(8)));

    const float cardInner = inner - S(24);
    if (texturesMode_) {
        drawTexturesPanel(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(6)));
        settingsContentH_ = ImGui::GetCursorPosY();
        ImGui::EndChild();  // ##settings
    } else if (worldMode_) {
        drawWorldPanel(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(6)));
        settingsContentH_ = ImGui::GetCursorPosY();
        ImGui::EndChild();  // ##settings
    } else if (editMode_) {
        drawEditPanel(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(6)));
        settingsContentH_ = ImGui::GetCursorPosY();
        ImGui::EndChild();  // ##settings
    } else {
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##fmt", inner);
    theme::label("Format");
    if (theme::segmented("##format", settings_.format, {"GLB", "OBJ"}, cardInner)) {}
    auto_.registerWidget("seg_format");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", settings_.format == 0 ? "One self-contained .glb, textures embedded. Blender, Unreal, three.js." : ".obj + .mtl + albedo .png next to it.");
    ImGui::Dummy(ImVec2(0, S(2)));
    theme::label("Up axis");
    theme::segmented("##up", settings_.up, {"Y up  (glTF)", "Z up  (Fable)"}, cardInner);
    auto_.registerWidget("seg_up");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", settings_.up == 0 ? "Blender, Unreal, three.js and most viewers expect Y up." : "Raw Fable coordinates; heights on Z.");
    ImGui::Dummy(ImVec2(0, S(2)));
    theme::toggle("World coordinates (maps line up)", &settings_.world);
    auto_.registerWidget("toggle_world");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", settings_.world ? "Placed at the map's WLD position: export a whole region and it assembles itself." : "Map-local: the map's corner sits at the origin.");
    theme::endCard();

    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##tex", inner);
    theme::toggle("Ground textures", &settings_.textures);
    auto_.registerWidget("toggle_textures");
    if (settings_.textures) {
        if (!ctx_.ready()) {
            ImGui::PushFont(fontSmall_);
            ImGui::TextColored(theme::vec(ctxError_.empty() && installValid_ ? theme::Warn : theme::Error), "%s",
                               !installValid_ ? "needs a Fable install (Change... above)"
                               : ctxError_.empty() ? "loading game.bin + textures.big..." : "unavailable in this install");
            ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(0, S(4)));
        // Sliders: label + value on one line, the bare slider under it (ImGui's own
        // centred value text collides with the grab).
        char val[64];
        const char* detail = settings_.texels <= 4 ? "fast" : settings_.texels <= 8 ? "balanced" : settings_.texels <= 16 ? "high" : "extreme";
        std::snprintf(val, sizeof val, "%d texels / cell  (%s)", settings_.texels, detail);
        theme::labelValue("Texture detail", val, cardInner);
        ImGui::SetNextItemWidth(cardInner);
        ImGui::SliderInt("##texels", &settings_.texels, 2, 32, "");
        auto_.registerWidget("slider_texels");
        std::snprintf(val, sizeof val, "every %.1f units", settings_.tile);
        theme::labelValue("Texture repeat", val, cardInner);
        ImGui::SetNextItemWidth(cardInner);
        ImGui::SliderFloat("##tile", &settings_.tile, 1.0f, 16.0f, "");
        auto_.registerWidget("slider_tile");
        std::snprintf(val, sizeof val, "x%.2f", settings_.gain);
        theme::labelValue("Brightness", val, cardInner);
        ImGui::SetNextItemWidth(cardInner);
        if (ImGui::SliderFloat("##gain", &settings_.gain, 0.5f, 3.0f, "")) gainDirty_ = true;
        if (gainDirty_ && !ImGui::IsItemActive()) { gainDirty_ = false; previewLoadedFor_.clear(); startPreviewLoad(); }
        auto_.registerWidget("slider_gain");
        ImGui::PushFont(fontSmall_);
        theme::hint("Fable's ground textures are authored dark; the game lights them up. 1.0 = raw texels.");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(4)));
        theme::toggle("Splat layers (per-theme PNGs + weights)", &settings_.layers);
        auto_.registerWidget("toggle_layers");
    }
    theme::toggle("Walkability as vertex colours", &settings_.walkable);
    auto_.registerWidget("toggle_walkable");
    theme::endCard();

    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##fol", inner);
    theme::toggle("Foliage (grass, plants, trees)", &settings_.foliage);
    auto_.registerWidget("toggle_foliage");
    theme::toggle("Placed objects (fences, walls, rocks, buildings)", &settings_.things);
    auto_.registerWidget("toggle_things");
    if (settings_.things) {
        theme::toggle("Creatures placed in the map (bind pose)", &settings_.creatures);
        auto_.registerWidget("toggle_creatures");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only creatures authored in the .tng (guards, bosses, the demon-door face).\nVillagers are spawned at runtime and are not in any file.");
    }
    theme::toggle("Water (lakes, rivers, sea)", &settings_.water);
    auto_.registerWidget("toggle_water");
    ImGui::Dummy(ImVec2(0, S(2)));
    theme::label("Object texture size");
    theme::segmented("##texsize", settings_.texSize, {"Full", "Half", "Quarter"}, cardInner);
    auto_.registerWidget("seg_texsize");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Most object textures are 512 px. Half = 256 px (files about a third smaller), Quarter = 128 px.");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The map's .tng, plus the doors, windows and building parts their meshes spawn.\nMesh instances with textures; creatures are skipped.");
    theme::endCard();

    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##out", inner);
    theme::label("Output folder");
    const float browseW = S(34);
    ImGui::SetNextItemWidth(cardInner - browseW - S(6));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(7)));
    ImGui::InputText("##outdir", outDirBuf_, sizeof outDirBuf_);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_outdir");
    ImGui::SameLine(0, S(6));
    if (theme::ghostButton("...", ImVec2(browseW, 0))) {
        const std::string picked = pickFolder(hwnd_, outDirBuf_);
        if (!picked.empty()) std::snprintf(outDirBuf_, sizeof outDirBuf_, "%s", picked.c_str());
    }
    theme::endCard();

    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##interface", inner);
    theme::label("Interface");
    {
        char val[32];
        std::snprintf(val, sizeof val, "%d %%", int(std::round(settings_.uiScale * 100.0f)));
        theme::labelValue("Text size", val, cardInner);
        ImGui::SetNextItemWidth(cardInner);
        if (ImGui::SliderFloat("##uiscale", &settings_.uiScale, 0.8f, 1.5f, "")) settings_.uiScale = std::round(settings_.uiScale * 20.0f) / 20.0f;
        auto_.registerWidget("slider_uiscale");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("On top of the display DPI and the window size. Fonts rebuild when you let go.");
    }
    theme::endCard();

    ImGui::Dummy(ImVec2(0, S(6)));
    settingsContentH_ = ImGui::GetCursorPosY();
    ImGui::EndChild();  // ##settings
    }

    // ---- footer
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p0.x + pad, ImGui::GetCursorScreenPos().y), ImVec2(p0.x + width - pad, ImGui::GetCursorScreenPos().y), theme::col(theme::Border));
    ImGui::Dummy(ImVec2(0, S(8)));
    if (texturesMode_) {
        ImGui::SetCursorPosX(pad);
        ImGui::PushFont(fontSmall_);
        theme::hint("Textures live in data/graphics/pc/textures.big. Replacing one changes every object that uses it; the original archive is backed up once as textures.big.atlas-orig (Setup > Restore puts it back).");
        ImGui::PopFont();
    } else if (worldMode_) {
        drawWorldFooter(pad, inner);
    } else if (editMode_) {
        drawEditFooter(pad, inner);
    } else {
    ImGui::SetCursorPosX(pad);
    const bool canExport = !selectedName_.empty() && !exportFuture_.valid() && installValid_;
    const MapEntry* selEntry = findEntry(selectedName_);
    const std::string label = exportFuture_.valid() ? "Exporting..." : !selEntry ? "Select a map to export" : "Export " + selEntry->name;
    if (theme::primaryButton(label.c_str(), ImVec2(inner, S(42)), canExport && !batchActive())) startExport();
    auto_.registerWidget("btn_export");
    if (ImGui::IsItemHovered() && canExport) ImGui::SetTooltip("Ctrl+E");
    ImGui::SetCursorPosX(pad);
    if (batchActive()) {
        char b[96];
        std::snprintf(b, sizeof b, "Exporting %d / %d  -  %s", batchDone_ + 1, batchTotal_, batchCurrent_.c_str());
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Muted), "%s", b);
        ImGui::PopFont();
        ImGui::SetCursorPosX(pad);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + inner, p.y + S(6)), theme::col(theme::Bg0), S(3.0f));
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + inner * float(batchDone_) / float(std::max(batchTotal_, 1)), p.y + S(6)), theme::col(theme::Accent), S(3.0f));
        ImGui::Dummy(ImVec2(inner, S(10)));
        ImGui::SetCursorPosX(pad);
        if (theme::ghostButton("Cancel batch", ImVec2(inner, S(30)))) cancelBatch();
        auto_.registerWidget("btn_cancel_batch");
    } else {
        const auto visible = visibleMapNames();
        char b[64];
        if (filter_.empty()) std::snprintf(b, sizeof b, "Export all %zu maps", visible.size());
        else std::snprintf(b, sizeof b, "Export %zu matching maps", visible.size());
        if (theme::ghostButton(b, ImVec2(inner, S(32))) && installValid_ && !exportFuture_.valid()) startBatchExport(visible);
        auto_.registerWidget("btn_export_all");
        if (selEntry && regions_.loaded && regions_.mapsOfRegion.count(selEntry->group)) {
            const auto keys = regionMapKeys(selEntry->group);
            if (keys.size() > 1) {
                ImGui::SetCursorPosX(pad);
                char rb[96];
                std::snprintf(rb, sizeof rb, "Export region %s (%zu maps)", selEntry->group.c_str(), keys.size());
                if (theme::ghostButton(rb, ImVec2(inner, S(32))) && !exportFuture_.valid()) { settings_.world = true; startBatchExport(keys); }
                auto_.registerWidget("btn_export_region");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Exports every map of the region in world coordinates");
            }
        }
        if (lastExportOk_ && !exportFuture_.valid()) {
            ImGui::SetCursorPosX(pad);
            if (theme::ghostButton("Open output folder", ImVec2(inner, S(32)))) openInExplorer(fs::path(lastExportPath_).parent_path().string());
            auto_.registerWidget("btn_open_folder");
        }
    }
    }

    // Activity log takes whatever height is left.
    ImGui::SetCursorPosX(pad);
    ImGui::PushFont(fontBold_);
    ImGui::TextColored(theme::vec(theme::Muted), "ACTIVITY");
    ImGui::PopFont();
    ImGui::SetCursorPosX(pad);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(8.0f));
    ImGui::BeginChild("##log", ImVec2(inner, std::max(ImGui::GetContentRegionAvail().y - S(10), logMin - S(10))), ImGuiChildFlags_None);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PushFont(fontSmall_);
    ImGui::Dummy(ImVec2(0, S(4)));
    for (const auto& [level, line] : log_) {
        const ImVec4 c = level == 1 ? theme::vec(theme::Warn) : level == 2 ? theme::vec(theme::Error)
                       : level == 3 ? theme::vec(theme::Success) : theme::vec(theme::Muted);
        ImGui::SetCursorPosX(S(10));
        ImGui::PushTextWrapPos(inner - S(10));
        ImGui::TextColored(c, "%s", line.c_str());
        ImGui::PopTextWrapPos();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - S(30)) ImGui::SetScrollHereY(1.0f);
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::EndChild();
}

// ------------------------------------------------------------------ Automation

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
    std::ofstream(logPath_, std::ios::app) << what << "\n";
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
    else if (cmd == "theme_search") { app.setThemeSearch(rest); note("ok   " + line); ++pc_; }   // the "Add a ground theme from the game" box
    else if (cmd == "edit_tab") { app.setEditTab(std::atoi(rest.c_str())); note("ok   " + line); ++pc_; }   // 0 Objects 1 Terrain 2 Actors 3 Level
    else if (cmd == "dismiss_rule") { app.dismissRule(rest); note("ok   " + line); ++pc_; }   // what the notice's "Got it" does (the notice may sit below the panel fold)
    else if (cmd == "redo") { app.editRedo(); note("ok   " + line); ++pc_; }
    else if (cmd == "place") {   // place <DEFINITION> [scriptname]
        std::istringstream rs(rest); std::string def, sn; rs >> def >> sn;
        if (!app.placeDefinition(def, sn)) fail("place failed: " + rest); else note("ok   " + line); ++pc_;
    }
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
