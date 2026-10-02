#include "app.hpp"
#include "worlddemand.hpp"

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
#include "forge/wld.hpp"
#include "nlohmann/json.hpp"
#include "theme.hpp"
#include "dialogueaudio.hpp"

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

// A folder (types == nullptr) or a file of the given types, e.g. {L"Fable level", L"*.lev"}.
std::string pickPath(HWND owner, const std::string& initial, const COMDLG_FILTERSPEC* types = nullptr, UINT typeCount = 0, bool save = false) {
    std::string result;
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | (types ? 0 : FOS_PICKFOLDERS) | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (types && !save ? FOS_FILEMUSTEXIST : 0));
    if (types) dlg->SetFileTypes(typeCount, types);
    if (save) dlg->SetDefaultExtension(L"big");
    if (!initial.empty()) {
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, initial.data(), int(initial.size()), nullptr, 0);
        std::wstring w(size_t(std::max(count,0)),L'\0');
        if(count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, initial.data(), int(initial.size()), w.data(),count);
        const auto attr = GetFileAttributesW(w.c_str());
        if(attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            const auto at=w.find_last_of(L"\\/");
            if(at!=std::wstring::npos) {
                if(types) dlg->SetFileName(w.substr(at+1).c_str());
                w.resize(at+1);
            }
        }
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
                std::string s(size_t(std::max(n, 0)), '\0');
                if(n) { WideCharToMultiByte(CP_UTF8, 0, path, -1, s.data(), n, nullptr, nullptr); s.pop_back(); }
                result = s;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

std::string pickFolder(HWND owner, const std::string& initial) { return pickPath(owner, initial); }

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

bool App::drawPathInput(const char* id,const char* hint,char* value,size_t capacity,
                        float width,PathField kind,const char* widget) {
    using theme::S;
    const float browseWidth=S(78),gap=ImGui::GetStyle().ItemSpacing.x;
    const bool stacked=width<S(215);
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(stacked?width:std::max(S(40),width-browseWidth-gap));
    bool changed=ImGui::InputTextWithHint("##path",hint,value,capacity);
    auto_.registerWidget(widget);
    if(ImGui::IsItemHovered() && value[0]) ImGui::SetTooltip("%s",value);
    if(!stacked) ImGui::SameLine();
    const bool browse=theme::ghostButton("Browse...",ImVec2(stacked?width:browseWidth,ImGui::GetFrameHeight()));
    auto_.registerWidget((std::string(widget)+"_browse").c_str());
    bool chooseFile=browse,chooseFolder=false;
    if(kind==PathField::ModSource) {
        chooseFile=false;
        if(browse) ImGui::OpenPopup("##browse_kind");
        if(ImGui::BeginPopup("##browse_kind")) {
            chooseFile=ImGui::Selectable("Choose mod file...");
            auto_.registerWidget((std::string(widget)+"_file").c_str());
            chooseFolder=ImGui::Selectable("Choose mod folder...");
            auto_.registerWidget((std::string(widget)+"_folder").c_str());
            ImGui::EndPopup();
        }
    }
    if(chooseFile || chooseFolder) {
        static const COMDLG_FILTERSPEC mods[]={{L"Mod files",L"*.fmp;*.patch;*.qst"},{L"All files",L"*.*"}};
        static const COMDLG_FILTERSPEC images[]={{L"Images",L"*.png;*.jpg;*.jpeg;*.tga;*.bmp"},{L"All files",L"*.*"}};
        static const COMDLG_FILTERSPEC png[]={{L"PNG image",L"*.png"},{L"All files",L"*.*"}};
        static const COMDLG_FILTERSPEC models[]={{L"3D models",L"*.glb;*.gltf;*.obj"},{L"All files",L"*.*"}};
        static const COMDLG_FILTERSPEC dialogue[]={{L"Dialogue archive",L"*.big"},{L"All files",L"*.*"}};
        const auto* filter=kind==PathField::ModSource?mods:kind==PathField::Model?models:
            kind==PathField::Image?images:kind==PathField::Png?png:dialogue;
        const std::string picked=pickPath(hwnd_,value,chooseFolder?nullptr:filter,
                                         chooseFolder?0:2,kind==PathField::DialogueExport);
        if(!picked.empty()) {
            if(picked.size()>=capacity) pushLog("The selected path is too long for this field.",2);
            else { std::memcpy(value,picked.c_str(),picked.size()+1);changed=true; }
        }
    }
    ImGui::PopID();
    return changed;
}

App::App() = default;
App::~App() {
    // The future joins during member destruction; release any diagnostic hold
    // and let obsolete preparation leave at its next worker checkpoint.
    if (worldDetailWork_) worldDetailWork_->cancel = true;
}

bool App::init(ID3D11Device* device, ID3D11DeviceContext* context, HWND hwnd,
               const std::string& installOverride) {
    device_ = device; context_ = context; hwnd_ = hwnd;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    buildFonts(dpiScale_);

    if (!renderer_.init(device_, context_)) {
        pushLog(std::string("renderer: ") + renderer_.error(), 2);
    }

    settings_.outDir = (fs::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".") / "Documents" / "FableForge").string();
    std::string savedInstall;
    if (!auto_.active()) { firstRun_ = !fs::exists(settingsPath()); loadSettings(savedInstall); }   // scripted runs stay deterministic
    std::snprintf(outDirBuf_, sizeof outDirBuf_, "%s", settings_.outDir.c_str());

    std::string root = installOverride;
    installSource_ = "command line";
    if (root.empty() && !savedInstall.empty() && forge::levelstore::detect(savedInstall).valid()) {
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
    const fs::path gameBin = r / "data" / "CompiledDefs" / "game.bin", stb = r / "data" / "Levels" / "FinalAlbion_RT.stb";
    h.gameBin = fs::exists(gameBin, ec);
    h.levels = levels_.valid();   // detected by scanInstall (a directory walk: not per frame)
    h.levelsHow = levels_.looseOnly() ? std::to_string(levels_.looseLevels) + " loose levels, no FinalAlbion.wad" : "FinalAlbion.wad";
    h.stb = fs::exists(stb, ec);
    if (!h.gameBin) h.missing.push_back(gameBin.string());
    if (!h.levels) h.missing.push_back((r / "data" / "Levels" / "FinalAlbion.wad").string() + "  (or loose levels in " + (r / "data" / "Levels" / "FinalAlbion").string() + ")");
    if (!h.stb) h.missing.push_back(stb.string());
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
    // never taller than the window: a short screen scrolls the sheet instead of cutting it off
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(std::min(S(720), vp->Size.x - S(40)), vp->Size.y - S(40)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(18)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(12));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::vec(theme::Bg1));
    ImGui::PushStyleColor(ImGuiCol_Border, theme::vec(theme::Border));
    ImGui::Begin("##help", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
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
        {"Edit: objects", {{"Q  W  E  R", "select / move / rotate / scale tool"}, {"Click", "select a thing or marker"}, {"Drag a thing", "carry it across the ground"}, {"Shift + click", "place the palette pick on the ground"}, {"Ctrl + Shift + drag", "clone and carry the selection"}, {"Ctrl + click", "add to / remove from the selection"}, {"Arrows / Shift + arrows", "nudge 0.05 / 0.5 units"}, {"Ctrl + arrows / A", "face a cardinal direction / pointer"}, {"[  ] / Shift / Alt", "rotate 2 degrees about Z / Y / X"}, {",  . / PgDn  PgUp", "lower / raise; Shift uses 0.01"}, {"Ctrl + D", "clone; click ground to drop"}, {"Ctrl + C  /  Ctrl + V", "copy / paste at the view centre"}, {"Del", "delete"}, {"End", "drop to the ground"}, {"Esc", "cancel a write confirmation, a carry or the selection"}, {"Ctrl + Z  /  Ctrl + Y", "undo / redo (also on the World tab)"}, {"Ctrl + S  or  F6", "save the draft (the loose .tng)"}, {"V", "show the first invalid thing"}}},
        {"Edit: terrain", {{"T", "terrain tool (opens the Terrain tab)"}, {"LMB hold", "sculpt / paint"}, {"Shift", "swap raise / lower (including exact step); paint walkable"}, {"[  ]", "brush radius"}, {"Ctrl + click", "sample the theme to paint"}, {"Ctrl + Shift + click", "sample the theme to replace"}, {"LMB drag", "a path / a copy rectangle"}, {"R", "turn the paste 90 degrees"}}},
        {"World tab", {{"Drag a map", "move it (snaps to 32)"}, {"Arrow keys", "nudge the selected map by 32"}, {"Wheel / right drag", "zoom / pan"}, {"F  or  Home", "fit the world"}}},
        {"Everywhere", {{"1  2  3  4", "objects / terrain / actors / level"}, {"Ctrl + F", "search the current browser"}, {"Ctrl + E", "export the selected map"}, {"Ctrl + O  /  Ctrl + Shift + O", "open a .lev / a world (.wld)"}, {"Ctrl + [  /  Ctrl + ]", "hide / show the side panels"}, {"Drop a .lev / .tng / .wld", "open a loose file or a world"}}},
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

// ---------------------------------------------------------------- tool windows

bool App::beginToolWindow(const char* id, const char* title, const char* subtitle, bool* open, float width) {
    using theme::S;
    if (!*open) return false;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float viewportWidth = std::max(S(360), viewportSize_.x - S(24));
    const float toolWidth = std::min({width, viewportWidth, std::max(1.0f, vp->Size.x - S(24))});
    const float toolMaxHeight = std::max(1.0f, vp->Size.y - S(60));
    const ImVec2 centre(viewportOrigin_.x + viewportSize_.x * 0.5f,
                        viewportOrigin_.y + viewportSize_.y * 0.5f);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(toolWidth, 0), ImVec2(toolWidth, toolMaxHeight));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::vec(theme::Bg1));
    ImGui::PushStyleColor(ImGuiCol_Border, theme::vec(theme::Border));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(14)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    const bool visible = ImGui::Begin(id, open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    if (!visible) { ImGui::End(); return false; }
    {
        // keep the whole window on screen (a resize or a smaller window can push it past an edge)
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        ImVec2 fix = wp;
        fix.x = std::clamp(fix.x, vp->Pos.x, std::max(vp->Pos.x, vp->Pos.x + vp->Size.x - ws.x));
        fix.y = std::clamp(fix.y, vp->Pos.y, std::max(vp->Pos.y, vp->Pos.y + vp->Size.y - ws.y));
        if (fix.x != wp.x || fix.y != wp.y) ImGui::SetWindowPos(fix);
    }
    const float inner = ImGui::GetContentRegionAvail().x;
    // header: title, subtitle, close
    const ImVec2 top = ImGui::GetCursorPos();
    ImGui::PushFont(fontBold_);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (subtitle && *subtitle) {
        ImGui::PushFont(fontSmall_);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner - S(36));
        ImGui::TextColored(theme::vec(theme::Muted), "%s", subtitle);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    const ImVec2 after = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(top.x + inner - S(26), top.y));
    if (theme::ghostButton("\xC3\x97##toolclose", ImVec2(S(26), S(26)))) *open = false;
    const std::string key = std::string(id).rfind("##", 0) == 0 ? id + 2 : id;
    auto_.registerWidget(("tool_close_" + key).c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close (Esc)");
    ImGui::SetCursorPos(after);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, a.y + S(4)), ImVec2(a.x + inner, a.y + S(4)), theme::col(theme::Border), 1.0f);
    ImGui::Dummy(ImVec2(0, S(10)));
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) *open = false;
    // Keep the title and Close reachable while the body scrolls on short screens.
    const float bodyMaxHeight = std::max(1.0f, toolMaxHeight - ImGui::GetCursorPosY() - ImGui::GetCurrentWindow()->WindowPadding.y);
    ImGui::SetNextWindowSizeConstraints(ImVec2(inner, 0), ImVec2(inner, bodyMaxHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##tool_body", ImVec2(inner, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize,
                      ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    toolWindowInner_ = ImGui::GetContentRegionAvail().x;
    return true;
}

void App::endToolWindow() { ImGui::EndChild(); ImGui::End(); }

void App::drawToolWindows() {
    if (!editMode_ || !documentLoaded() || texturesMode_ || worldMode_ || modsMode_) {
        selectionInspectorOpen_ = fitOpen_ = fractalOpen_ = budgetOpen_ = false;
        return;
    }
    // These are extensions of their sidebar sections, not independent workspaces.
    // Closing them on navigation keeps a tool from covering an unrelated view.
    if (editTab_ != 0 && editTab_ != 2) selectionInspectorOpen_ = false;
    if (editTab_ != 1) fitOpen_ = fractalOpen_ = false;
    if (editTab_ != 3) budgetOpen_ = false;
    drawSelectionInspector();
    drawFitWindow();
    drawFractalWindow();
    drawBudgetWindow();
}

void App::drawSetupPanel() {
    if (!setupOpen_) return;
    using theme::S;
    ImGui::OpenPopup("Setup");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(S(560), vp->Size.x - S(24)),
                                    std::min(S(installValid_ ? 620 : 400), vp->Size.y - S(24))));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(16)));
    if (ImGui::BeginPopupModal("Setup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::BeginChild("##setup_details", ImVec2(0, -S(52)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        const float contentWidth = ImGui::GetContentRegionAvail().x;
        const InstallHealth h = installHealth();
        ImGui::PushFont(fontBold_);
        ImGui::TextUnformatted(installValid_ ? "Your Fable install" : "Point FableForge at Fable: The Lost Chapters");
        ImGui::PopFont();
        ImGui::PushFont(fontSmall_);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        ImGui::TextColored(theme::vec(theme::Muted), "%s", installValid_ ? installPath_.c_str() : "The Steam or GOG folder that holds Fable.exe (Steam: steamapps\\common\\Fable The Lost Chapters). Nothing in it is changed until you write something; every file touched gets a one-time .forge-orig backup.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(8)));
        auto row = [&](bool ok, const char* what, const char* enables, const char* without) {
            ImGui::TextColored(theme::vec(ok ? theme::Success : theme::Warn), ok ? "  ok   " : "  --   ");
            ImGui::SameLine(0, 0);
            ImGui::TextColored(theme::vec(theme::Text), "%s", what);
            ImGui::SameLine(0, S(8));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(theme::vec(theme::Muted), "%s", ok ? enables : without);
            ImGui::PopTextWrapPos();
        };
        const std::string dataOk = "levels (" + h.levelsHow + "), objects, terrain and the world editor";
        row(h.gameBin && h.levels && h.stb, "game data", dataOk.c_str(), installPath_.empty() ? "no folder chosen yet" : "missing (pick the folder that holds Fable.exe, not its Data folder):");
        if (!installPath_.empty() && !h.missing.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            for (const auto& m : h.missing) ImGui::TextColored(theme::vec(theme::Warn), "         %s", m.c_str());
            ImGui::PopTextWrapPos();
        }
        row(h.texturesBig, "textures.big", "textured preview, ground-theme paint, custom textures, minimaps", "no textured preview, no theme paint or custom textures (a trimmed install?)");
        row(h.fse, "ForgeFSE", "the live link to the running game (go here, spawn, follow)", "no live link: install ForgeFSE (FSE_Launcher.exe) to talk to the running game; everything else works");
        row(h.saves, "saves folder", "the in-game test harness can continue your profiles", "no My Games/Fable/Saves yet: start the game once");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(10)));
        ImGui::PushFont(fontSmall_);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        theme::hintMore("Some changes only show in a new game; hover for the engine's rules.", "Rules the engine imposes: a new region only shows in a game started after it was added (saves cache the region table); new objects and creatures need a fresh game or a first visit; enemy spawners only run once the hero is past childhood; never write while the game is running (the editor refuses when the live link sees a hero).");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        // backups: everything FableForge has touched, and the way back
        if (installValid_) {
            if (backupsScannedAt_ < 0 || ImGui::GetTime() - backupsScannedAt_ > 5.0) {
                backupList_ = backups::scan(installPath_); backupsScannedAt_ = ImGui::GetTime();
                try { bankReport_ = stbcompact::measure(installPath_); bankReportOk_ = true; } catch (const std::exception&) { bankReportOk_ = false; }
            }
            size_t changed = 0; for (const auto& e : backupList_) changed += e.differs;
            ImGui::Dummy(ImVec2(0, S(10)));
            ImGui::PushFont(fontBold_);
            ImGui::Text("Backups: %zu file(s) tracked, %zu differ from backup", backupList_.size(), changed);
            ImGui::PopFont();
            if (!backupList_.empty()) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
                ImGui::BeginChild("##backuplist", ImVec2(contentWidth, S(std::min(120.0f, 18.0f * float(backupList_.size()) + 8.0f))), ImGuiChildFlags_None);
                ImGui::PopStyleColor();
                ImGui::PushFont(fontSmall_);
                for (const auto& e : backupList_) {
                    const char* tag = !e.differs ? "same" : e.kind == backups::Kind::Created ? "new " : e.kind == backups::Kind::Staged ? "mods" : e.kind == backups::Kind::Overlay ? "ovr " : "edit";
                    ImGui::TextColored(theme::vec(e.differs ? theme::Warn : theme::Faint), "%s  %s", tag, fs::relative(e.file, installPath_).string().c_str());
                }
                ImGui::PopFont();
                ImGui::EndChild();
                if (!confirmRestore_) {
                    if (changed) {
                        if (theme::dangerButton("Restore backed-up files", ImVec2(contentWidth, S(28)))) confirmRestore_ = true;
                    } else theme::ghostButton("Nothing to restore", ImVec2(contentWidth, S(28)));
                    auto_.registerWidget("btn_restore_all");
                } else {
                    const int r = confirmRow("Restore every backed-up file and remove files FableForge created? This includes loose .lev/.tng drafts; the open map reloads from disk.",
                                             "Yes, restore", contentWidth, S(28), "btn_restore_confirm");
                    if (r != 0) confirmRestore_ = false;
                    if (r > 0) restoreAllBackups();
                }
            }
        }
        // the static-map bank: every deploy that resizes a chunk appends and leaves dead bytes behind
        if (installValid_ && bankReportOk_) {
            ImGui::Dummy(ImVec2(0, S(10)));
            ImGui::PushFont(fontSmall_);
            ImGui::TextColored(theme::vec(theme::Faint), "Static-map bank: %.0f MB, %.1f MB reclaimable (retail itself ships %.1f MB of it)",
                               double(bankReport_.bytesBefore) / 1048576.0, double(bankReport_.deadBytes()) / 1048576.0, 3.4);
            ImGui::PopFont();
            const bool busy = compactBusy();
            if (theme::ghostButton(busy ? "Compacting..." : (bankReport_.deadBytes() ? "Compact the bank (payloads verified, game must be closed)" : "Bank is compact"), ImVec2(contentWidth, S(28))) && !busy && bankReport_.deadBytes()) compactBank();
            auto_.registerWidget("btn_compact_stb");
        }
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0, S(10)));
        const float w = (ImGui::GetContentRegionAvail().x - S(6)) * 0.5f;
        if (theme::ghostButton("Choose the install folder...", ImVec2(w, S(32)))) {
            const std::string picked = pickFolder(hwnd_, installPath_);
            if (!picked.empty()) {
                const std::string previous = installPath_;
                scanInstall(picked);
                if (installPath_ != previous) installSource_ = "manual";
            }
        }
        auto_.registerWidget("btn_setup_browse");
        ImGui::SameLine(0, S(6));
        if (theme::primaryButton(installValid_ ? "Continue" : "Continue without an install", ImVec2(w, S(32)))) { setupOpen_ = false; ImGui::CloseCurrentPopup(); if (tourPending_) { tourPending_ = false; tourStep_ = 0; } }
        auto_.registerWidget("btn_setup_continue");
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

bool App::compactBank() {
    if (!installValid_ || compactFuture_.valid()) return false;
    if (fileWriteBlocked("compact")) return false;
    const std::string root = installPath_;
    pushLog("compacting the static-map bank (a ~570 MB rewrite; every payload is verified before the swap)", 0);
    compactFuture_ = std::async(std::launch::async, [root]() { return stbcompact::compact(root); });
    return true;
}

bool App::restoreAllBackups() {
    if (!installValid_) return false;
    if (previewFuture_.valid() || foliageFuture_.valid() || neighbourFuture_.valid() || ctxFuture_.valid() ||
        terrainDeployFuture_.valid() || worldFuture_.valid() || newLevelFuture_.valid() || compactFuture_.valid() ||
        meshImportFuture_.valid() || modsBusy() || exportFuture_.valid() || fitFuture_.valid() ||
        worldDetailFuture_.valid() ||
        std::any_of(worldTileWorkers_.begin(),worldTileWorkers_.end(),[](const auto& job){ return job.valid(); })) {
        pushLog("restore: a load or write is still running; try again when it finishes", 1);
        return false;
    }
    std::vector<std::string> notes; std::string err;
    const std::string selected = selectedName_;
    const size_t n = backups::restoreAll(installPath_, true, notes, err);
    for (const auto& x : notes) pushLog("restore: " + x, 0);
    if (!err.empty()) pushLog("restore: " + err, 2);
    if (n) pushLog("restore: " + std::to_string(n) + " file(s) returned to their backups; reloading the map", 3);
    rescanBackups();
    if (n) {
        // The archive on disk changed even when the install folder did not.
        // Keep separately staged edits, but retire the loaded line and subtitle cache.
        dialogueAudio_.reset();dialogueMotionPlaying_=false;dialogueLoaded_=false;
        dialogueTime_=0;dialogueAudioDuration_=0;dialogueHeadLastTime_=-1;
        dialogueSubtitles_.clear();dialogueError_.clear();dialogueExportMessage_.clear();
        dialogueTextIndex_.reset();dialogueTextRoot_.clear();dialogueTextError_.clear();
        dialogueSearchCacheKey_.clear();dialogueSearchResults_.clear();dialogueSearchGroups_.clear();
        dialogueScratchLanguage_.clear();setDialogueEditing(false);
        refreshModOrder();
        scanInstall(installPath_);
        selectedName_.clear(); docLoadedFor_.clear();
        previewLoadedFor_.clear(); foliageLoadedFor_.clear();
        previewPendingName_.clear(); foliagePendingName_.clear(); neighboursFor_.clear(); lastFramedFor_.clear();
        renderer_.clear(); renderer_.clearThings();
        previewScene_ = {}; previewTextured_ = false;
        foliageInstances_ = thingInstances_ = 0;
        selectedThing_ = -1; selectedUid_ = 0; extraUids_.clear();
        worldLoaded_ = false; worldLoadedFrom_.clear(); newLevelDonor_.clear();
        if (findEntry(selected)) selectMap(selected);
    }
    return err.empty();
}

std::string App::settingsPath() const {
    const char* appdata = std::getenv("APPDATA");
    const fs::path dir = fs::path(appdata ? appdata : ".") / "FableForge";
    // the app was Albion Atlas until 0.16: carry the old settings and presets over once
    const fs::path old = fs::path(appdata ? appdata : ".") / "AlbionAtlas";
    std::error_code ec;
    if (!fs::exists(dir, ec) && fs::is_directory(old, ec)) fs::copy(old, dir, fs::copy_options::recursive, ec);
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
        settings_.showExplorer = j.value("showExplorer", settings_.showExplorer);
        settings_.showActions = j.value("showActions", settings_.showActions);
        moveOwned_ = j.value("moveOwned", moveOwned_);
        if (j.contains("effectBackground") && j["effectBackground"].is_array() && j["effectBackground"].size()==3)
            for (size_t i=0;i<3;++i) {
                const float colour=j["effectBackground"][i].get<float>();
                if (std::isfinite(colour)) effectBackground_[i]=std::clamp(colour,0.f,1.f);
            }
        effectShowGrid_=j.value("effectShowGrid",effectShowGrid_);
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
            {"showExplorer", settings_.showExplorer}, {"showActions", settings_.showActions},
            {"moveOwned", moveOwned_},
            {"effectBackground", {effectBackground_[0],effectBackground_[1],effectBackground_[2]}},
            {"effectShowGrid", effectShowGrid_},
        };
        std::ofstream(settingsPath()) << j.dump(2);
    } catch (...) {}
}

void App::scanInstall(const std::string& picked) {
    // the Data folder itself was picked: the install is its parent
    std::string root = picked;
    {
        const fs::path p(picked);
        std::error_code ec;
        if (!fs::exists(p / "data", ec) && fs::exists(p / "CompiledDefs" / "game.bin", ec) && p.has_parent_path()) {
            root = p.parent_path().string();
            pushLog("that is the install's Data folder; using " + root, 1);
        }
    }
    const bool switching = !installPath_.empty() &&
        fs::path(installPath_).lexically_normal() != fs::path(root).lexically_normal();
    if (switching) {
        if (hasUnsavedEdits() || worldPendingCount() || !dialogueStaged_.empty()) {
            pushLog("install: save or discard the current edits before changing folders", 1);
            return;
        }
        if (ctxFuture_.valid() || previewFuture_.valid() || foliageFuture_.valid() ||
            neighbourFuture_.valid() || terrainDeployFuture_.valid() || worldFuture_.valid() ||
            newLevelFuture_.valid() || compactFuture_.valid() || meshImportFuture_.valid() ||
            modsBusy() || exportFuture_.valid() || fitFuture_.valid() ||
            worldDetailFuture_.valid() ||
            std::any_of(worldTileWorkers_.begin(), worldTileWorkers_.end(),
                        [](const auto& job) { return job.valid(); })) {
            pushLog("install: a load or write is still running; try again when it finishes", 1);
            return;
        }
        selectedName_.clear(); docLoadedFor_.clear(); previewLoadedFor_.clear();
        foliageLoadedFor_.clear(); previewPendingName_.clear(); foliagePendingName_.clear();
        neighboursFor_.clear(); lastFramedFor_.clear();
        renderer_.clear(); renderer_.clearThings();
        selectedThing_ = -1; selectedUid_ = 0; extraUids_.clear();
        previewScene_ = {}; previewTextured_ = false;
        foliageInstances_ = thingInstances_ = 0;
        worldLoaded_ = false; worldLoadedFrom_.clear();
        ctx_ = {}; ctxError_.clear();
        modelsLoaded_ = modelUsersLoaded_ = modelReady_ = false;
        modelRows_.clear(); modelUsers_.clear(); modelName_.clear();
        renderer_.clearModelPreview(); renderer_.clearHeadPreview();
        dialogueAudio_.reset();
        dialogueLoaded_=false;
        dialogueEditGesture_.reset();dialogueEditHistory_.clear();
        dialogueExportMessage_.clear();
        dialogueScratchLanguage_.clear();
    }
    maps_.clear();
    installPath_ = root;
    if (switching) resetModDestination();
    backupList_.clear(); backupsScannedAt_ = -1; bankReportOk_ = false;
    effectsLoaded_ = effectBrowserLoaded_ = effectBrowserReady_ = false;
    effectNames_.clear(); effectPick_.clear();
    effectBrowserRows_.clear(); effectBrowserSelection_ = {};
    effectBrowserThumbnails_.clear(); effectBrowserError_.clear();
    effectSimulation_.reset({}); effectRenderer_.clear(); effectTexturesReady_=false; effectMeshesReady_=false;
    effectTextureWarnings_.clear();
    texturesLoaded_ = false; texRows_.clear(); texBanks_.clear(); texSelected_.clear();
    texPreviewFor_.clear(); texPreview_ = nullptr;
    levels_ = forge::levelstore::detect(root);
    const bool hasDefs = fs::exists(fs::path(root) / "data" / "CompiledDefs" / "game.bin");
    installValid_ = hasDefs && levels_.valid();
    if (!installValid_) {
        pushLog("Not a Fable TLC install: " + root + (hasDefs ? " (no FinalAlbion.wad and no loose levels in data\\Levels\\FinalAlbion)" : " (no data\\CompiledDefs\\game.bin)"), 2);
        if (!auto_.active()) setupOpen_ = true;   // the Setup panel lists exactly what is missing
        return;
    }
    if (levels_.looseOnly())
        pushLog("loose-level install: " + std::to_string(levels_.looseLevels) + " levels in data\\Levels\\FinalAlbion, no FinalAlbion.wad" +
                (levels_.renamedWad.empty() ? "" : " (" + levels_.renamedWad.filename().string() + " is ignored, as the game ignores it)") +
                "; level writes go to the loose files", 0);
    regions_ = te::loadRegionIndex(root);
    try {
        for (const auto& lv : forge::levelstore::listLevels(levels_)) {
            MapEntry m; m.name = lv.stem; m.key = m.name; m.size = lv.size;
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
        pushLog(std::to_string(maps_.size()) + " maps in " + levels_.describe(), 0);
    } catch (const std::exception& e) {
        pushLog("cannot read " + levels_.describe() + ": " + e.what(), 2);
        installValid_ = false;
        return;
    }
    startContextLoad();
}

void App::startContextLoad(const std::string& fromRoot) {
    if (modFilesBusy()) return;
    if (!installValid_) return;
    modelsLoaded_ = modelUsersLoaded_ = modelReady_ = false;
    modelRows_.clear(); modelUsers_.clear(); modelName_.clear(); modelError_.clear();
    modelGeometry_ = {}; modelId_ = 0;
    renderer_.clearModelPreview();
    renderer_.clearHeadPreview();
    dialogueHeadReady_=false;
    dialoguePresetChecked_=false;
    modelRoot_ = fromRoot.empty() ? installPath_ : fromRoot;
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

std::string App::resolveLevPath(const MapEntry& e, LevWorkspace& scratch, std::string& err) {
    if (!e.loosePath.empty()) return e.loosePath;
    try {
        const fs::path wadPath = fs::path(installPath_) / "data" / "Levels" / "FinalAlbion.wad";
        const auto wad = forge::wad::Archive::open(wadPath);
        const std::string want = lower(e.name) + ".lev";
        for (const auto& en : wad.entries()) {
            if (lower(fs::path(en.name).filename().string()) != want) continue;
            const auto bytes = wad.read(en);
            scratch = std::make_shared<albion::detail::TemporaryDirectory>("gui-level-");
            const fs::path path = scratch->path() / fs::path(e.name + ".lev").filename();
            std::ofstream out;
            out.exceptions(std::ios::failbit | std::ios::badbit);
            out.open(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            out.close();
            return path.string();
        }
        err = "map not found in " + levels_.describe();
    } catch (const std::exception& ex) {
        err = ex.what();
    }
    return {};
}

bool App::openWorld(const std::string& wldPath) {
    if (modFilesBusy()) { fileWriteBlocked("open world"); return false; }
    std::error_code ec;
    const fs::path wld = fs::absolute(wldPath, ec);
    if (!fs::is_regular_file(wld, ec) || lower(wld.extension().string()) != ".wld") { pushLog("Not a .wld file: " + wldPath, 2); return false; }
    if (installValid_ && fs::equivalent(wld, fs::path(installPath_) / "data" / "Levels" / "FinalAlbion.wld", ec)) {
        pushLog("FinalAlbion.wld is the game's world; its maps are already listed", 1);
        return false;
    }
    forge::wld::File world;
    try { world = forge::wld::File::parse(wld); }
    catch (const std::exception& e) { pushLog("open world: " + std::string(e.what()), 2); return false; }
    // LevelName is relative to the folder of the .wld (data\Levels): "X.lev", "Sub\X.lev"
    const std::string stem = wld.stem().string();
    maps_.erase(std::remove_if(maps_.begin(), maps_.end(), [&](const MapEntry& m) { return m.worldFile == wld.string(); }), maps_.end());
    std::vector<MapEntry> added;
    int missing = 0;
    for (const auto& m : world.maps()) {
        std::string rel = m.levelName;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const fs::path lev = wld.parent_path() / fs::path(rel);
        if (!fs::is_regular_file(lev, ec)) { ++missing; continue; }
        std::string region = "(no region)";
        for (const auto& r : world.regions())
            for (const auto& c : r.containsMaps)
                if (lower(c) == lower(m.levelName)) { region = r.regionName; break; }
        MapEntry e;
        e.name = lev.stem().string();
        e.key = "world:" + stem + "/" + std::to_string(m.index);
        e.group = stem + ": " + region;
        e.loosePath = lev.string();
        e.size = uint32_t(fs::file_size(lev, ec));
        e.worldX = float(m.mapX); e.worldY = float(m.mapY); e.hasWorld = true;
        e.worldFile = wld.string();
        e.tngPath = fs::path(lev).replace_extension(".tng").string();
        groupOpen_[e.group] = true;
        added.push_back(std::move(e));
    }
    if (added.empty()) { pushLog("open world: " + wld.filename().string() + " lists no level found beside it (" + std::to_string(missing) + " missing)", 2); return false; }
    maps_.insert(maps_.begin(), added.begin(), added.end());   // opened worlds first: visible without scrolling
    pushLog("Opened world " + wld.string() + ": " + std::to_string(added.size()) + " maps" + (missing ? ", " + std::to_string(missing) + " listed levels not found" : std::string()), missing ? 1 : 0);
    selectMap(added.front().key);   // (not clearing selectedName_ first: that would skip the unsaved-edits prompt)
    return true;
}

bool App::openDropped(const std::string& path) {
    const std::string ext = lower(fs::path(path).extension().string());
    if (ext == ".wld") return openWorld(path);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
        std::snprintf(customPng_, sizeof customPng_, "%s", fs::absolute(path).string().c_str());
        if (!customName_[0]) {
            std::string nm = "GROUND_" + fs::path(path).stem().string();
            for (auto& c : nm) { c = char(std::toupper(static_cast<unsigned char>(c))); if (!std::isalnum(static_cast<unsigned char>(c))) c = '_'; }
            std::snprintf(customName_, sizeof customName_, "%.*s", int(sizeof customName_ - 1), nm.c_str());
        }
        setTexturesMode(true);
        assetsTab_ = 2;
        pushLog("drop: " + fs::path(path).filename().string() + " is ready as a new ground theme (Assets > Ground themes); name it and press Create", 0);
        return true;
    }
    if (ext == ".glb" || ext == ".gltf" || ext == ".obj") {
        // a model: Assets > Models with the import form filled in (nothing is imported yet)
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) { pushLog("drop: cannot read " + path, 2); return false; }
        if (meshImportFuture_.valid()) { pushLog("drop: a model import is running; drop the file again when it finishes", 1); return false; }
        std::snprintf(meshModelPath_, sizeof meshModelPath_, "%s", fs::absolute(path).string().c_str());
        if (!meshName_[0]) {
            std::string nm = fs::path(path).stem().string();
            for (auto& c : nm) { c = char(std::toupper(static_cast<unsigned char>(c))); if (!std::isalnum(static_cast<unsigned char>(c))) c = '_'; }
            std::snprintf(meshName_, sizeof meshName_, "%.*s", int(sizeof meshName_ - 1), nm.c_str());
        }
        setTexturesMode(true);
        assetsTab_ = 1;
        modelImportOpen_ = true;
        pushLog("drop: " + fs::path(path).filename().string() + " is ready to import as OBJECT_" + std::string(meshName_) + " (Assets > Models); check the name and destination, then import", 0);
        return true;
    }
    return openLooseLev(path);
}

bool App::openLooseLev(const std::string& path) {
    if (modFilesBusy()) { fileWriteBlocked("open level"); return false; }
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
    selectMap(m.key);   // (not clearing selectedName_ first: that would skip the unsaved-edits prompt)
    return true;
}

void App::requestClose() {
    if (hasUnsavedEdits() || worldPendingCount() > 0 || worldFuture_.valid() ||
        !dialogueStaged_.empty()) {
        pendingSelect_.clear();
        closePending_ = true;
        closeSaveWaiting_ = worldFuture_.valid();
    } else quit_ = true;
}

void App::selectMap(const std::string& nameOrKey) {
    if (modFilesBusy()) { fileWriteBlocked("select map"); return; }
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
    renderer_.clearLayer(2);
    renderer_.clearThings();
    foliageLoadedFor_.clear();
    reportedThingWarnings_.clear();
    foliageInstances_ = 0;
    thingInstances_ = 0;
    foliageStatus_.clear();
    openDocument();
    startPreviewLoad();
}

void App::setPreviewFoliage(bool on) {
    previewFoliage_ = on;
    renderer_.showFoliage = on;
    if (on && (!foliageLoaded() || (documentLoaded() && doc_.terrainRevision() != foliageTerrainRev_)) &&
        !foliageFuture_.valid() && previewLoaded()) startFoliageLoad(foliageLoaded());
}

void App::setPreviewThings(bool on) {
    previewThings_ = on;
    renderer_.showThings = on;
    if (on && !foliageLoaded() && !foliageFuture_.valid() && previewLoaded()) startFoliageLoad();
}

void App::startFoliageLoad(bool foliageOnly) {
    if (modFilesBusy()) { foliagePendingName_ = selectedName_; return; }
    if (selectedName_.empty() || !ctx_.ready()) return;
    if (foliageFuture_.valid()) { foliagePendingName_ = selectedName_; return; }
    const MapEntry* found = findEntry(selectedName_);
    if (!found) return;
    const MapEntry entry = *found;
    // the worker holds its own reference: a context reload on the UI thread
    // (ctx_ = *ctxPending_) must not free what it is reading
    const auto ctxHold = std::make_shared<const te::Context>(ctx_);
    const te::Context* ctx = ctxHold.get();
    const std::string root = installPath_;
    const std::string tngText = documentLoaded() ? doc_.text() : std::string();
    if (documentLoaded() && !foliageOnly) syncedRevision_ = doc_.revision();
    const auto graphics = graphicsBigPath();
    foliageFuture_ = std::async(std::launch::async, [ctxHold, entry, ctx, root, tngText, foliageOnly, graphics]() {
        FoliageResult r; r.name = entry.key; r.foliageOnly = foliageOnly;
        foliageexport::Options fo;
        fo.gameRoot = root;
        fo.graphicsBig = graphics;
        fo.textures = true;
        fo.up = te::UpAxis::Y;
        fo.mapLocal = true;
        try { r.scene = foliageexport::load(entry.name, fo, *ctx); } catch (const std::exception& e) { r.scene.warnings.push_back(e.what()); }
        if (!foliageOnly) {
            thingsexport::Options to;
            to.gameRoot = root;
            to.graphicsBig = graphics;
            to.textures = true;
            to.up = te::UpAxis::Y;
            to.tngText = tngText;
            try { r.things = thingsexport::load(entry.name, to, *ctx, &r.thingStats); } catch (const std::exception& e) { r.things.warnings.push_back(e.what()); }
        }
        return r;
    });
}

const MapEntry* App::findEntry(const std::string& key) const {
    auto it = std::find_if(maps_.begin(), maps_.end(), [&](const MapEntry& m) { return m.key == key; });
    return it == maps_.end() ? nullptr : &*it;
}

void App::startNeighbourLoad() {
    if (modFilesBusy()) return;
    if (neighbourFuture_.valid() || selectedName_.empty() || !installValid_) return;
    neighboursFor_ = selectedName_;   // one attempt per map: a map that cannot be placed must not retry every frame
    const MapEntry* cur = findEntry(selectedName_);
    if (!cur || !cur->worldFile.empty()) return;   // another world's maps: FinalAlbion.wld does not place them
    editor::WorldLayout layout;
    std::string err;
    if (!editor::loadWorldLayout(installPath_, layout, err)) { pushLog("neighbours: " + err, 1); return; }
    const editor::WorldMapBox* box = layout.find(cur->name);
    if (!box) { pushLog("neighbours: " + cur->name + " is not placed in FinalAlbion.wld", 1); return; }
    // .lev paths on this thread (the WAD read uses app state), the bakes on the worker
    struct Job { std::string lev; int dx, dy; std::string name; LevWorkspace scratch; };
    std::vector<Job> jobs;
    for (const auto* n : layout.touching(*box, box->x, box->y)) {
        if (n->name == box->name) continue;
        const MapEntry* e = findEntry(n->name);
        if (!e) continue;
        LevWorkspace scratch;
        const std::string lev = resolveLevPath(*e, scratch, err);
        if (!lev.empty()) jobs.push_back({lev, n->x - box->x, n->y - box->y, n->name, std::move(scratch)});
    }
    // the worker holds its own reference: a context reload on the UI thread
    // (ctx_ = *ctxPending_) must not free what it is reading
    const auto ctxHold = (ctx_.ready()) ? std::make_shared<const te::Context>(ctx_) : std::shared_ptr<const te::Context>();
    const te::Context* ctx = ctxHold.get();
    const std::string name = selectedName_;
    const float gain = settings_.gain;
    const fs::path root = installPath_;
    neighbourFuture_ = std::async(std::launch::async, [ctxHold, jobs, ctx, name, gain, root]() {
        NeighbourResult r; r.name = name;
        for (const auto& j : jobs) {
            try {
                const auto file = forge::lev::File::open(j.lev);
                te::Options o;
                o.textures = ctx != nullptr; o.texelsPerCell = 2; o.gain = gain; o.up = te::UpAxis::Z;
                o.water = false;
                if (ctx) { o.gameRoot = root; o.mapName = j.name; }
                const auto sc = te::buildScene(file, o, ctx);
                foliageexport::Mesh m;
                m.name = j.name;
                m.geometry.vertices.reserve(sc.vertices.size());
                for (const auto& v : sc.vertices) m.geometry.vertices.push_back({v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.u, v.v});
                foliageexport::SubMesh part;
                part.indices = sc.indices;
                if (sc.hasAlbedo) { part.image = int(r.scene.images.size()); r.scene.images.push_back(sc.albedo); }
                m.parts.push_back(std::move(part));
                foliageexport::Instance inst;
                inst.mesh = int(r.scene.meshes.size());
                inst.x = float(j.dx); inst.y = float(j.dy); inst.z = 0; inst.scale = 1;
                r.scene.meshes.push_back(std::move(m));
                r.scene.instances.push_back(inst);
                ++r.maps;
            } catch (const std::exception& e) { r.note = j.name + ": " + e.what(); }
        }
        return r;
    });
}

void App::startPreviewLoad() {
    if (modFilesBusy()) { previewPendingName_ = selectedName_; return; }
    if (selectedName_.empty()) return;
    if (previewFuture_.valid()) { previewPendingName_ = selectedName_; return; }
    const MapEntry* found = findEntry(selectedName_);
    if (!found) return;
    const MapEntry entry = *found;
    const bool textured = ctx_.ready();
    reloadWhenContextReady_ = !textured;
    // the worker holds its own reference: a context reload on the UI thread
    // (ctx_ = *ctxPending_) must not free what it is reading
    const auto ctxHold = (textured) ? std::make_shared<const te::Context>(ctx_) : std::shared_ptr<const te::Context>();
    const te::Context* ctx = ctxHold.get();
    const float gain = settings_.gain;
    previewFuture_ = std::async(std::launch::async, [ctxHold, this, entry, ctx, textured, gain]() {
        PreviewResult r; r.name = entry.key; r.textured = textured;
        std::string err;
        LevWorkspace scratch;
        const std::string lev = resolveLevPath(entry, scratch, err);
        if (lev.empty()) { r.error = err; return r; }
        try {
            const auto file = forge::lev::File::open(lev);
            te::Options o;
            o.textures = textured;
            o.texelsPerCell = previewTexelsFor(file.cellsX(), file.cellsY());
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
    if (modFilesBusy()) { fileWriteBlocked("export"); return; }
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
    if (modFilesBusy()) { fileWriteBlocked("export"); return; }
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
    // the worker holds its own reference: a context reload on the UI thread
    // (ctx_ = *ctxPending_) must not free what it is reading
    const auto ctxHold = (ctx_.ready()) ? std::make_shared<const te::Context>(ctx_) : std::shared_ptr<const te::Context>();
    const te::Context* ctx = ctxHold.get();
    const std::string outPath = (fs::path(s.outDir) / (entry.name + (s.format == 0 ? ".glb" : ".obj"))).string();
    lastExportPath_ = outPath;
    lastExportOk_ = false;
    batchCurrent_ = entry.name;
    if (!batchActive()) { pushLog("Exporting " + entry.name + " ...", 0); saveSettings(); }
    exportFuture_ = std::async(std::launch::async, [ctxHold, this, entry, s, ctx, outPath]() {
        ExportResult r; r.path = outPath;
        const auto t0 = std::chrono::steady_clock::now();
        std::string err;
        LevWorkspace scratch;
        const std::string lev = resolveLevPath(entry, scratch, err);
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
            if (reloadWhenContextReady_ && !selectedName_.empty()) {
                if (hasUnsavedEdits() || (documentLoaded() && doc_.strokeActive())) startThemeRebake();
                else startPreviewLoad();
            }
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
    if (neighbourFuture_.valid() && neighbourFuture_.wait_for(0ms) == std::future_status::ready) {
        NeighbourResult r = neighbourFuture_.get();
        if (r.name == selectedName_ && showNeighbours_) {
            renderer_.uploadLayer(2, r.scene, te::UpAxis::Y);
            neighboursFor_ = r.name;
            pushLog("neighbours: " + std::to_string(r.maps) + " maps around " + r.name + (r.note.empty() ? "" : " (" + r.note + ")"), 0);
        } else if (showNeighbours_ && neighboursFor_ != selectedName_) startNeighbourLoad();
    }
    if (showNeighbours_ && !neighbourFuture_.valid() && neighboursFor_ != selectedName_ && previewLoaded()) startNeighbourLoad();
    if (foliageFuture_.valid() && foliageFuture_.wait_for(0ms) == std::future_status::ready) {
        FoliageResult r = foliageFuture_.get();
        if (r.name == selectedName_ && r.thingsOnly) {
            thingInstances_ = r.things.instances.size();
            renderer_.uploadThings(r.things, te::UpAxis::Y);
            sectionsDirty_ = true;   // fresh instances start visible
            bindInstances(r.things);
            for (const auto& w : r.things.warnings) if (reportedThingWarnings_.insert(w).second) pushLog("objects: " + w, 1);
        } else if (r.name == selectedName_) {
            foliageLoadedFor_ = r.name;
            foliagePreviewReseated_ = 0;
            if (documentLoaded() && doc_.hasTerrain()) {
                const auto* before = doc_.savedTerrain();
                if (before && before->heights.size() == doc_.terrain().heights.size()) {
                    for (auto& inst : r.scene.instances) {
                        const auto oldHeight = editor::Document::sampleHeight(*before, doc_.cellsX(), doc_.cellsY(), inst.x, inst.y);
                        const auto newHeight = editor::Document::sampleHeight(doc_.terrain(), doc_.cellsX(), doc_.cellsY(), inst.x, inst.y);
                        if (oldHeight && newHeight && std::fabs(*newHeight - *oldHeight) > 1e-4f) {
                            inst.z += *newHeight - *oldHeight;
                            ++foliagePreviewReseated_;
                        }
                    }
                }
                foliageTerrainRev_ = doc_.terrainRevision();
            }
            localDetail_.clear();
            for (const auto& inst : r.scene.instances) {
                if (inst.mesh < 0 || size_t(inst.mesh) >= r.scene.meshes.size()) continue;
                const auto& m = r.scene.meshes[size_t(inst.mesh)];
                localDetail_.push_back({inst.x, inst.y, m.meshId, m.name});
            }
            budgetDirty_ = budgetOpen_;
            foliageInstances_ = r.scene.instances.size();
            if (!r.foliageOnly) thingInstances_ = r.things.instances.size();
            if (r.scene.found && !r.scene.instances.empty()) {
                renderer_.uploadLayer(0, r.scene, te::UpAxis::Y);
                foliageStatus_ = std::to_string(r.scene.instances.size()) + " plants (" + std::to_string(r.scene.treeInstances) + " trees)";
            } else {
                foliageStatus_ = r.scene.found ? "no baked foliage" : "no foliage bank entry";
            }
            if (!r.foliageOnly && !r.things.instances.empty()) {
                renderer_.uploadThings(r.things, te::UpAxis::Y);
            sectionsDirty_ = true;   // fresh instances start visible
                bindInstances(r.things);
                foliageStatus_ += ", " + std::to_string(r.things.instances.size()) + " objects";
            } else if (!r.foliageOnly && r.things.found) {
                foliageStatus_ += ", no placed objects";
            }
            if (r.foliageOnly && thingInstances_) foliageStatus_ += ", " + std::to_string(thingInstances_) + " objects";
            for (const auto& w : r.things.warnings) if (reportedThingWarnings_.insert(w).second) pushLog("objects: " + w, 1);
            for (const auto& w : r.scene.warnings)
                if (w.rfind("mesh", 0) != 0) pushLog("foliage: " + w, 1);
        }
        if (!foliagePendingName_.empty()) { foliagePendingName_.clear(); if (!foliageLoaded()) startFoliageLoad(); }
        else if (thingsReloadPending_) startThingsReload();
    }
    if (compactFuture_.valid() && compactFuture_.wait_for(0ms) == std::future_status::ready) {
        const stbcompact::Result r = compactFuture_.get();
        if (!r.ok) pushLog("compact: " + r.error, 2);
        else if (r.alreadyCompact) pushLog("compact: the bank was already compact", 0);
        else {
            char buf[128];
            std::snprintf(buf, sizeof buf, "compact: %.1f MB -> %.1f MB, %u payloads verified byte-identical", double(r.report.bytesBefore) / 1048576.0, double(r.report.bytesAfter) / 1048576.0, r.report.entries);
            pushLog(buf, 3);
        }
        rescanBackups();
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
    pollMeshImport();
    pollModsTool();
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

float App::viewportControlsLift() const {
    using theme::S;
    if (viewportSize_.x < S(620)) return 0.0f;   // compact View/Show/Frame row
    ImGui::PushFont(fontSmall_);
    const float gap = S(6);
    auto chipW = [&](const char* t) { return ImGui::CalcTextSize(t).x + S(24); };
    float modesW = chipW("Frame  (F)") + S(8);
    for (const char* name : kModeNames) modesW += chipW(name) + gap;
    float layersW = 0;
    for (const char* name : {"Foliage", "Objects", "Markers", "Water", "Grid", "Neighbours"}) layersW += chipW(name) + gap;
    const bool twoRows = modesW + layersW + ImGui::CalcTextSize("Show:").x + S(40) > viewportSize_.x;
    const float lift = twoRows ? ImGui::GetFrameHeight() + S(6) : 0.0f;
    ImGui::PopFont();
    return lift;
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
        const ImVec2 p0(origin.x + S(14), yChips - viewportControlsLift() - ts.y - S(22));
        dl->AddRectFilled(p0, ImVec2(p0.x + ts.x + S(16), p0.y + ts.y + S(10)), theme::col(theme::Bg1) | 0xD0000000, S(6));
        dl->AddText(ImVec2(p0.x + S(8), p0.y + S(5)), theme::col(theme::Muted), buf);
    }
    drawCompass(origin, size, camera_.yaw, rowH + S(10) + viewportControlsLift());
    ImGui::PopFont();
}

void App::drawCompass(const ImVec2& origin, const ImVec2& size, float yaw, float bottomInset) {
    using theme::S;
    if(size.x < S(96) || size.y < S(96) + bottomInset) return;
    ImGui::PushFont(fontSmall_);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Heading compass: Fable north is +y (render -z), and camera yaw 0 faces it.
    // A projected ground vector collapses at the horizon and flips when looking
    // up; subtracting distant projected points also loses precision. Heading is
    // independent of pitch, eye position, focus distance and projection.
    {
        const float dx = std::sin(yaw), dy = -std::cos(yaw);
        const float r = S(16), extent = S(34);
        const ImVec2 c(origin.x + size.x - S(14) - extent,
                       origin.y + size.y - bottomInset - S(12) - extent);
        auto_.registerRect("viewport_compass", ImVec4(c.x-extent,c.y-extent,c.x+extent,c.y+extent));
        dl->AddCircleFilled(c, r + S(4), theme::col(theme::Bg1) | 0xD0000000, 32);
        dl->AddCircle(c, r + S(4), theme::col(theme::Border), 32, 1.0f);
        const ImVec2 tip(c.x + dx * r, c.y + dy * r), tail(c.x - dx * r * 0.6f, c.y - dy * r * 0.6f);
        const ImVec2 side(-dy * S(4), dx * S(4));
        dl->AddTriangleFilled(tip, ImVec2(c.x + side.x, c.y + side.y), ImVec2(c.x - side.x, c.y - side.y), theme::col(theme::Accent));
        dl->AddTriangleFilled(tail, ImVec2(c.x - side.x, c.y - side.y), ImVec2(c.x + side.x, c.y + side.y), theme::col(theme::Muted));
        const ImVec2 ns = ImGui::CalcTextSize("N");
        dl->AddText(ImVec2(c.x + dx * (r + S(11)) - ns.x * 0.5f, c.y + dy * (r + S(11)) - ns.y * 0.5f), theme::col(theme::Text), "N");
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
    if (theme::ghostButton("Cancel  (Esc)", ImVec2(half, height))) r = -1;
    // Escape backs out of a pending write; Enter deliberately does not confirm one
    if (r == 0 && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) r = -1;
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
    float y = origin.y + ((size.x < S(620) && renderer_.hasMesh()) ? S(90) : S(14));
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
    v.push_back("levels_loose=" + std::string(levels_.looseOnly() ? "1" : "0"));
    { const InstallHealth h = installHealth(); v.push_back("install_textures=" + std::string(h.texturesBig ? "1" : "0")); v.push_back("install_fse=" + std::string(h.fse ? "1" : "0")); v.push_back("setup_open=" + std::string(setupOpen_ ? "1" : "0")); }
    if (installValid_) { size_t d = 0; for (const auto& e : backupList_) d += e.differs; v.push_back("backups_differ=" + std::to_string(d)); }
    v.push_back("maps=" + std::to_string(maps_.size()));
    v.push_back("selected=" + selectedName_);
    v.push_back("settings_scroll=" + std::to_string(int(settingsScroll_)));
    v.push_back(std::string("settings_scrolled=") + (settingsScroll_ > 0.5f ? "1" : "0"));
    { ImGuiContext& g = *ImGui::GetCurrentContext(); v.push_back(std::string("hovered_window=") + (g.HoveredWindow ? g.HoveredWindow->Name : "-")); v.push_back("mouse=" + std::to_string(int(g.IO.MousePos.x)) + "," + std::to_string(int(g.IO.MousePos.y))); if (g.HoveredWindow) v.push_back("hovered_scrollmax=" + std::to_string(int(g.HoveredWindow->ScrollMax.y)));
      v.push_back(std::string("wheeling_window=") + (g.WheelingWindow ? g.WheelingWindow->Name : "-") + " scrolled_frame=" + std::to_string(g.WheelingWindowScrolledFrame) + " frame=" + std::to_string(g.FrameCount) + " hovered_flags=" + std::to_string(g.HoveredWindow ? g.HoveredWindow->Flags : 0) + " parent=" + (g.HoveredWindow && g.HoveredWindow->ParentWindow ? g.HoveredWindow->ParentWindow->Name : "-") + " parent_scrollmax=" + std::to_string(g.HoveredWindow && g.HoveredWindow->ParentWindow ? int(g.HoveredWindow->ParentWindow->ScrollMax.y) : -1)); }
    v.push_back("preview_loaded=" + std::string(previewLoaded() ? "1" : "0"));
    v.push_back("preview_has_mesh=" + std::string(renderer_.hasMesh() ? "1" : "0"));
    v.push_back("preview_textured=" + std::string(previewTextured_ ? "1" : "0"));
    v.push_back("context_ready=" + std::string(ctx_.ready() ? "1" : "0"));
    v.push_back("mode=" + std::string(kModeNames[int(mode_)]));
    v.push_back("rule_notice=" + (ruleKey_.empty() ? std::string("-") : ruleKey_));
    v.push_back("edit_tab=" + std::to_string(editTab_));
    v.push_back("palette_definition="+placeDef_);
    v.push_back("palette_reveal_pending="+std::to_string(!revealDef_.empty()));
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
    size_t selectedMeshInstances = 0;
    if (selectedThing_ >= 0) for (size_t i = 0; i < renderer_.instanceCount(); ++i)
        if (renderer_.instance(i).thing == selectedThing_ && renderer_.instance(i).mesh >= 0) ++selectedMeshInstances;
    v.push_back("selected_mesh_instances=" + std::to_string(selectedMeshInstances));
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
    v.push_back("world_3d=" + std::string(world3D_ ? "1" : "0"));
    v.push_back("world_tiles=" + std::to_string(worldTiles_.size()));
    v.push_back("world_overview_upload_frames=" + std::to_string(worldOverviewUploadFrames_));
    v.push_back("world_thumbnail_upload_frames=" + std::to_string(worldThumbnailUploadFrames_));
    v.push_back("world_overview_first_map=" + worldOverviewFirstMap_);
    v.push_back("world_tiles_total=" + std::to_string(worldTileTotal_));
    v.push_back("world_tiles_busy=" + std::string(worldTileWorkers_.empty() ? "0" : "1"));
    v.push_back("world_hover=" + worldHover_);
    v.push_back("world_detail_maps=" + std::to_string(worldDetailShown_.size()));
    v.push_back("world_eye_x=" + std::to_string(worldCamera_.posX));
    v.push_back("world_eye_y=" + std::to_string(-worldCamera_.posZ));
    v.push_back("world_eye_height=" + std::to_string(worldCamera_.posY));
    bool worldInside = false;
    const float worldBelow = worldGroundAt(worldCamera_.posX, -worldCamera_.posZ, worldInside, nullptr);
    v.push_back("world_ground_clearance=" + std::to_string(worldCamera_.posY - worldBelow));
    v.push_back("world_camera_yaw=" + std::to_string(worldCamera_.yaw));
    v.push_back("world_camera_pitch=" + std::to_string(worldCamera_.pitch));
    v.push_back("world_detail_cached_maps=" + std::to_string(worldDetailCache_.size()));
    v.push_back("world_detail_cached_bytes=" + std::to_string(worldDetailCache_.bytes()));
    v.push_back("world_detail_cache_budget=" + std::to_string(worldDetailCache_.budget()));
    v.push_back("world_gpu_memory_valid=" + std::to_string(worldVideoMemory_.valid));
    v.push_back("world_gpu_memory_budget=" + std::to_string(worldVideoMemory_.budget));
    v.push_back("world_gpu_memory_usage=" + std::to_string(worldVideoMemory_.usage));
    v.push_back("world_gpu_memory_simulated=" + std::to_string(worldVideoMemoryOverride_.has_value()));
    v.push_back("world_memory_evictions=" + std::to_string(worldMemoryEvictions_));
    v.push_back("world_detail_cache_hits=" + std::to_string(worldDetailCacheHits_));
    v.push_back("viewport_x=" + std::to_string(viewportOrigin_.x));
    v.push_back("viewport_y=" + std::to_string(viewportOrigin_.y));
    v.push_back("viewport_width=" + std::to_string(viewportSize_.x));
    v.push_back("viewport_height=" + std::to_string(viewportSize_.y));
    const auto pool = renderer_.texturePoolStats();
    v.push_back("texture_retired_cpu_bytes=" + std::to_string(renderer_.retiredTextureBytes()));
    v.push_back("world_aa_mode=" + std::to_string(renderer_.worldAaMode));
    v.push_back("world_aa_samples=" + std::to_string(renderer_.aaSamples()));
    v.push_back("world_aa_support=" + std::to_string(renderer_.aaSupport()));
    v.push_back("world_aa_target_bytes=" + std::to_string(renderer_.renderTargetBytes()));
    v.push_back("world_aa_rebuilds=" + std::to_string(renderer_.aaRebuilds()));
    v.push_back("world_aa_fallbacks=" + std::to_string(renderer_.aaFallbacks()));
    v.push_back("texture_pool_cutout=" + std::to_string(pool.cutout));
    v.push_back("texture_pool_mipmapped=" + std::to_string(pool.mipmapped));
    v.push_back("texture_pool_allocations=" + std::to_string(pool.allocations));
    v.push_back("texture_pool_gpu_bytes=" + std::to_string(pool.gpuBytes));
    v.push_back("texture_pool_identity_bytes=" + std::to_string(pool.identityBytes));
    v.push_back("texture_pool_hits=" + std::to_string(pool.hits));
    v.push_back("world_detail_loads=" + std::to_string(worldDetailLoads_));
    v.push_back("world_detail_upload_frames=" + std::to_string(worldDetailUploadFrames_));
    v.push_back("world_detail_cancelled=" + std::to_string(worldDetailCancelled_));
    v.push_back("world_detail_worker_held=" + std::to_string(worldDetailWork_ && worldDetailWork_->held.load()));
    v.push_back("world_cutout_cache_bytes=" + std::to_string(worldCutoutCacheStats_.bytes));
    v.push_back("world_cutout_cache_entries=" + std::to_string(worldCutoutCacheStats_.entries));
    v.push_back("world_cutout_cache_hits=" + std::to_string(worldCutoutCacheStats_.hits));
    v.push_back("world_cutout_cache_builds=" + std::to_string(worldCutoutCacheStats_.builds));
    v.push_back("world_detail_deferred=" + std::to_string(worldDetailDeferred_));
    v.push_back("world_detail_failures=" + std::to_string(worldDetailFailures_));
    v.push_back("world_detail_last_objects=" + std::to_string(worldDetailLastObjects_));
    v.push_back("world_detail_last_creatures=" + std::to_string(worldDetailLastCreatures_));
    std::string detailNames;
    for (const auto& [name, shown] : worldDetailShown_) { if (!detailNames.empty()) detailNames += ","; detailNames += name; }
    v.push_back("world_detail_names=" + detailNames);
    v.push_back("world_water_batches=" + std::to_string(renderer_.worldWaterBatches()));
    v.push_back("world_drawn_batches=" + std::to_string(renderer_.worldDrawnBatches));
    v.push_back("world_culled_batches=" + std::to_string(renderer_.worldCulledBatches));
    v.push_back("world_drawn_object_parts=" + std::to_string(renderer_.worldDrawnObjects));
    v.push_back("world_culled_object_parts=" + std::to_string(renderer_.worldCulledObjects));
    v.push_back("world_lower_lod_parts=" + std::to_string(renderer_.worldLodObjects));
    v.push_back("world_object_draw_calls=" + std::to_string(renderer_.worldObjectDrawCalls));
    v.push_back("world_object_distance=" + std::to_string(renderer_.worldObjectDistance));
    v.push_back("world_scenery_maps=" + std::to_string(worldScenery_.loaded));
    v.push_back("world_scenery_pending=" + std::to_string(worldScenery_.pending));
    v.push_back("world_scenery_bytes=" + std::to_string(worldScenery_.bytes));
    v.push_back("world_scenery_failures=" + std::to_string(worldScenery_.failures));
    v.push_back("world_scenery_blocked=" + std::to_string(worldScenery_.blocked));
    v.push_back("world_scenery_priority_evictions=" + std::to_string(worldScenery_.priorityEvictions));
    v.push_back("world_scenery_memory_limit=" + std::to_string(worldScenery_.memoryLimit));
    v.push_back("world_scenery_drawn_parts=" + std::to_string(renderer_.worldSceneryDrawnParts));
    v.push_back("world_scenery_holds=" + std::to_string(worldSceneryHolds_));
    std::string sceneryNames;
    for (const auto& name : worldScenery_.names) { if (!sceneryNames.empty()) sceneryNames += ','; sceneryNames += name; }
    v.push_back("world_scenery_names=" + sceneryNames);
    v.push_back("world_terrain_triangles=" + std::to_string(renderer_.worldTerrainTriangles));
    v.push_back("world_terrain_full_triangles=" + std::to_string(renderer_.worldTerrainFullTriangles));
    v.push_back("world_terrain_coarse_patches=" + std::to_string(renderer_.worldTerrainCoarsePatches));
    v.push_back("world_label=" + (worldHover_.empty() ? worldSelected_ : worldHover_));
    v.push_back("world_detail_memory_ceiling=" + std::to_string(worldDetailMemoryMaps_));
    v.push_back("world_detail_radius=" + std::to_string(worldview::requestedDrawDistance(worldDetailRadius_)));
    v.push_back("world_detail_budget=" + std::to_string(worldAutoDetail_ ? worldDetailBudget_.maps : worldDetailMaps_));
    v.push_back("assets_tab=" + std::to_string(assetsTab_));
    v.push_back("dialogue_loaded=" + std::to_string(dialogueLoaded_));
    v.push_back("dialogue_language=" + dialogueLanguage_);
    v.push_back("dialogue_bank=" + std::to_string(dialogueBank_));
    v.push_back("dialogue_id=" + std::to_string(dialogueId_));
    v.push_back("dialogue_frames=" + std::to_string(dialogueLoaded_ ? dialogueEntry_.frames.size() : 0));
    v.push_back("dialogue_frame=" + std::to_string(dialogueLoaded_ && !dialogueEntry_.frames.empty()
        ? 1+std::min(dialogueEntry_.frames.size()-1,size_t(std::max(0.f,dialogueTime_)*dialogueEntry_.fps)) : 0));
    v.push_back("dialogue_subtitles_count=" + std::to_string(dialogueSubtitles_.size()));
    v.push_back("dialogue_subtitle_name=" + (dialogueSubtitles_.empty() ? "" : dialogueSubtitles_.front().name));
    v.push_back("dialogue_search_results=" + std::to_string(dialogueSearchResults_.size()));
    v.push_back("dialogue_search_first_id=" + std::to_string(
        dialogueSearchResults_.empty()?0:dialogueSearchResults_.front().soundId));
    v.push_back("dialogue_staged=" + std::to_string(dialogueStaged_.size()));
    v.push_back("dialogue_editor_open=" + std::string(dialogueToolsOpen_ ? "1" : "0"));
    const auto dialogueHistory=dialogueEditHistory_.find(dialogueLoadedKey_);
    v.push_back("dialogue_can_undo=" + std::to_string(dialogueLoaded_ &&
        (dialogueEditGesture_.has_value() || (dialogueHistory!=dialogueEditHistory_.end() &&
         !dialogueHistory->second.undo.empty()))));
    v.push_back("dialogue_can_redo=" + std::to_string(dialogueLoaded_ &&
        dialogueHistory!=dialogueEditHistory_.end() && !dialogueHistory->second.redo.empty()));
    v.push_back("dialogue_exported=" + std::to_string(dialogueExportMessage_.rfind("Wrote ",0)==0));
    v.push_back("dialogue_pack_added=" + std::to_string(dialogueExportMessage_.rfind("Added ",0)==0));
    v.push_back("dialogue_audio_available=" + std::to_string(dialogueLoaded_ && dialogueAudioDuration_>0));
    v.push_back("dialogue_scrubbed=" + std::to_string(dialogueLoaded_ && dialogueTime_>0));
    v.push_back("dialogue_playing=" + std::to_string(dialogueMotionPlaying_ ||
        (dialogueAudio_ && dialogueAudio_->playing())));
    v.push_back("dialogue_audio_muted=" + std::to_string(dialogueAudioMuted_));
    v.push_back("dialogue_audio_advanced=" + std::to_string(dialogueAudio_ && dialogueAudio_->position()>0));
    v.push_back("dialogue_preset=" + std::to_string(dialoguePreset_));
    v.push_back("dialogue_preset_ready=" + std::to_string(dialoguePresetChecked_ && dialoguePresetAssets_.complete()));
    v.push_back("dialogue_head_ready=" + std::to_string(dialogueHeadReady_));
    v.push_back("dialogue_head_yaw=" + std::to_string(dialogueHeadYaw_));
    v.push_back("dialogue_head_pitch=" + std::to_string(dialogueHeadPitch_));
    v.push_back("dialogue_head_zoom=" + std::to_string(dialogueHeadZoom_));
    v.push_back("dialogue_preset_mesh=" + std::to_string(dialoguePresetAssets_.meshId));
    v.push_back("effects_count=" + std::to_string(effectBrowserRows_.size()));
    v.push_back("effects_filtered=" + std::to_string(effectBrowserFiltered_));
    v.push_back("effect_ready=" + std::to_string(effectBrowserReady_));
    v.push_back("effect_preview_time=" + std::to_string(effectSimulation_.time()));
    v.push_back("effect_preview_particles=" + std::to_string(effectSimulation_.particleCount()));
    v.push_back("effect_preview_supported=" + std::to_string(effectSimulation_.supportedSystems()));
    v.push_back("effect_preview_warnings=" + std::to_string(effectSimulation_.warnings().size()+effectTextureWarnings_.size()));
    v.push_back("effect_preview_drawn=" + std::to_string(effectRenderer_.drawnSprites()));
    v.push_back("effect_preview_meshes=" + std::to_string(effectSimulation_.meshes().size()));
    v.push_back("effect_preview_lights="+std::to_string(effectSimulation_.lights().size()));
    v.push_back("effect_preview_light_volumes="+std::to_string(effectLightVolumesDrawn_));
    v.push_back("effect_show_light_volumes="+std::to_string(effectShowLightVolumes_));
    v.push_back("effect_background="+std::to_string(effectBackground_[0])+","+
        std::to_string(effectBackground_[1])+","+std::to_string(effectBackground_[2]));
    v.push_back("move_owned="+std::to_string(moveOwned_));
    v.push_back("effect_preview_meshes_drawn=" + std::to_string(effectRenderer_.drawnMeshes()));
    v.push_back("effect_preview_mesh_triangles=" + std::to_string(effectRenderer_.meshTriangles()));
    std::set<int32_t> authoredMeshes;
    for (const auto& mesh:effectBrowserSelection_.meshes)
        if (effectRenderer_.meshUsesAuthoredBounds(mesh.mesh)) authoredMeshes.insert(mesh.mesh);
    v.push_back("effect_preview_authored_meshes="+std::to_string(authoredMeshes.size()));
    v.push_back("effect_preview_playing=" + std::to_string(effectPlaying_));
    v.push_back("effect_preview_grid=" + std::to_string(effectShowGrid_));
    v.push_back("effect_preview_loop=" + std::to_string(effectLoop_));
    v.push_back("effect_preview_loops=" + std::to_string(effectLoopCount_));
    v.push_back("effect_preview_speed=" + std::to_string(effectSpeedIndex_));
    v.push_back("effect_preview_duration=" + std::to_string(effectDuration_));
    v.push_back("effect_preview_auto_duration=" + std::to_string(effectAutoDuration_));
    v.push_back("effect_preview_position=" + std::to_string(effectSimulation_.position()));
    v.push_back("effect_selected=" + effectBrowserSelection_.name);
    v.push_back("effect_id=" + std::to_string(effectBrowserSelection_.id));
    v.push_back("effect_display_name=" + effectBrowserSelection_.displayName);
    v.push_back("effect_parsed_fully=" + std::to_string(effectBrowserSelection_.parsedFully));
    v.push_back("effect_systems=" + std::to_string(effectBrowserSelection_.systems));
    v.push_back("effect_sprites=" + std::to_string(effectBrowserSelection_.sprites.size()));
    v.push_back("effect_meshes=" + std::to_string(effectBrowserSelection_.meshes.size()));
    v.push_back("effect_lights=" + std::to_string(effectBrowserSelection_.lights.size()));
    v.push_back("models_count=" + std::to_string(modelRows_.size()));
    v.push_back("models_filtered=" + std::to_string(modelFiltered_));
    v.push_back("model_selected=" + modelName_);
    v.push_back("model_ready=" + std::to_string(modelReady_));
    v.push_back("model_import_open=" + std::to_string(modelImportOpen_));
    v.push_back("mesh_name=" + std::string(meshName_[0] ? meshName_ : "-"));
    v.push_back("map_list_visible=" + std::to_string(settings_.showExplorer && !standaloneAssetPreview()));
    v.push_back("model_vertices=" + std::to_string(modelGeometry_.vertices.size()));
    v.push_back("model_triangles=" + std::to_string(modelGeometry_.triangles.size()));
    v.push_back("model_wire=" + std::to_string(modelWire_));
    v.push_back("model_users=" + std::to_string(modelUsers_.count(modelId_) ? modelUsers_.at(modelId_).size() : 0));
    v.push_back("textures_mode=" + std::string(texturesMode_ ? "1" : "0"));
    v.push_back("mods_mode=" + std::string(modsMode_ ? "1" : "0"));
    v.push_back("mods_count=" + std::to_string(modOrder_.mods.size()));
    v.push_back("pack_destination=" + fs::path(packDest_).generic_string());
    v.push_back("mods_picks=" + std::to_string(modPicks_.size()));
    v.push_back("mods_conflicts=" + std::to_string(modConflicts_.size()));
    v.push_back("mods_first_winner=" + (modConflicts_.empty() ? std::string() : modConflictWinner(modConflicts_.front())));
    v.push_back("mods_missing_models=" + std::to_string(modNewMissingMeshes_.size()));
    v.push_back("origin_mods=" + std::to_string(originMods_.size()));
    v.push_back("mesh_import_busy=" + std::string(meshImportFuture_.valid() ? "1" : "0"));
    v.push_back("mesh_import_failed=" + std::string(meshImportError_.empty() ? "0" : "1"));
    v.push_back("mesh_import_name=" + std::string(meshName_));
    v.push_back("custom_theme_failed=" + std::string(customThemeError_.empty() ? "0" : "1"));
    v.push_back("origin_things=" + std::to_string(thingOrigin_.size()));
    v.push_back("textures_count=" + std::to_string(texRows_.size()));
    v.push_back("texture_thumbs_made=" + std::to_string(texThumbsMade_));
    v.push_back("texture_search=" + std::string(texSearch_));
    v.push_back("tool_panel_visible=" + std::to_string(settings_.showActions));
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
    v.push_back("selection_popup=" + std::to_string(selectionPopupOpen_));
    v.push_back("selection_inspector=" + std::to_string(selectionInspectorOpen_));
    v.push_back("fit_open=" + std::to_string(fitOpen_));
    v.push_back("fit_preview_has_changes=" + std::to_string(fitPreviewChanged_ > 0));
    v.push_back("fit_preview_changed=" + std::to_string(fitPreviewChanged_));
    v.push_back("fractal_open=" + std::to_string(fractalOpen_));
    v.push_back("foliage_preview_reseated=" + std::to_string(foliagePreviewReseated_));
    v.push_back("foliage_preview_reseated_any=" + std::to_string(foliagePreviewReseated_ > 0));
    v.push_back("budget_open=" + std::to_string(budgetOpen_));
    v.push_back("selection_inspector_def=" + (selectionInspectorOpen_ && documentLoaded() && selectedThing_ >= 0 && size_t(selectedThing_) < doc_.thingCount()
        ? doc_.summary(size_t(selectedThing_)).definition : std::string{}));
    v.push_back("doc_things=" + std::to_string(documentLoaded() ? doc_.thingCount() : 0));
    v.push_back("thing_glyphs=" + std::to_string(thingGlyphs_.size()));
    v.push_back("selected_radius_fields=" + std::to_string(selectedRadiusFields().size()));
    v.push_back("doc_dirty=" + std::string(documentLoaded() && doc_.dirty() ? "1" : "0"));
    v.push_back("close_prompt=" + std::string(closePending_ ? "1" : "0"));
    v.push_back("doc_changes=" + std::to_string(documentLoaded() ? doc_.changes().size() : 0));
    v.push_back("selected_thing=" + std::to_string(selectedThing_));
    v.push_back("selected_locked="+std::to_string(selectedThing_>=0 && doc_.isLocked(size_t(selectedThing_))));
    v.push_back("selection_height_popup="+std::to_string(selectionHeightPopupOpen_));
    v.push_back("owned_delete_popup="+std::to_string(ownedDeletePopupOpen_));
    v.push_back("gizmo_using="+std::to_string(ImGuizmo::IsUsing()));
    v.push_back("carry_armed="+std::to_string(carryArmed_));
    v.push_back("carrying="+std::to_string(carrying_));
    v.push_back("things_first=" + std::to_string(thingsFirst_));
    v.push_back("things_shown=" + std::to_string(thingsShown_));
    v.push_back(std::string("track_preview=") + (trackPreview_.active ? "1" : "0"));
    if (thingsFirst_ >= 0 && documentLoaded() && size_t(thingsFirst_) < doc_.thingCount()) v.push_back("things_first_def=" + doc_.summary(size_t(thingsFirst_)).definition);
    v.push_back(std::string("things_first_is_selected=") + (thingsFirst_ >= 0 && thingsFirst_ == selectedThing_ ? "1" : "0"));
    v.push_back("place_owner=" + std::string(placeOwner_ < 0 ? "auto" : placeOwner_ >= 4 ? "neutral" : std::to_string(placeOwner_)));
    v.push_back(std::string("section_day=") + (showDayOnly_ ? "1" : "0"));
    v.push_back(std::string("section_night=") + (showNightOnly_ ? "1" : "0"));
    if (documentLoaded() && selectedThing_ >= 0) {
        v.push_back("selected_def=" + doc_.summary(size_t(selectedThing_)).definition);
        if (doc_.summary(size_t(selectedThing_)).type == "AICreature") {
            const auto sex=ctx_.defIntField(doc_.summary(size_t(selectedThing_)).definition,"Sex");
            v.push_back("selected_creature_sex=" + (sex ? std::to_string(*sex) : std::string("unknown")));
        }
        v.push_back("selected_section=" + doc_.sectionOf(size_t(selectedThing_)));
        const auto roots=selectionIndices();
        v.push_back("selected_owned_count="+std::to_string(doc_.ownedDescendants(
            std::vector<size_t>(roots.begin(),roots.end())).size()));
        {
            std::string player = "none";
            for (const auto& r : doc_.propertiesOf(size_t(selectedThing_)))
                if (r.ctc.empty() && r.key == "Player") { player = r.value; break; }
            v.push_back("selected_player=" + player);
            v.push_back(std::string("selected_visible=") + (thingHiddenBySection(doc_.thingSections(), size_t(selectedThing_)) ? "0" : "1"));
        }
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
    v.push_back("brush_radius=" + std::to_string(brushRadius_));
    v.push_back("terrain_dirty=" + std::string(documentLoaded() && doc_.hasTerrain() && doc_.terrainDirty() ? "1" : "0"));
    v.push_back("write_both_pending=" + std::string(writeObjectsAfterTerrain_ ? "1" : "0"));
    v.push_back("confirm_pending=" + std::string(confirmPending() ? "1" : "0"));
    v.push_back("terrain_deploy_busy=" + std::string(terrainDeployBusy() ? "1" : "0"));
    std::string fileJob = activeFileJob() ? activeFileJob() : "none";
    std::replace(fileJob.begin(), fileJob.end(), ' ', '_');
    v.push_back("file_job=" + fileJob);
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
    renderer_.pollTextureCleanup();
    time_ += dt;
    frameDialoguePlayback();
    if (texturesMode_ && assetsTab_==3 && effectBrowserReady_ && effectPlaying_)
        advanceEffectPlayback(dt);
    pollWorkers();
    ImGuizmo::BeginFrame();
    syncInstances();
    syncTerrain();
    editorShortcuts();
    updateTrackPreview(dt);
    pollWorldTiles();
    updateWorldDetail(); // also retire cancelled work while another tab/2D view is active
    updateWorldScenery();
    if (terrainDeployFuture_.valid() && terrainDeployFuture_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        const TerrainDeployResult r = terrainDeployFuture_.get();
        if (r.ok && r.written) doc_.acceptTerrainWrite(*r.written);
        for (const auto& n : r.notes) pushLog("terrain: " + n, 0);
        if (r.ok && !r.pack.empty()) pushLog("terrain written into pack " + packLabel(r.pack) + " (Mods > Deploy puts it in the game)", 3);
        else if (r.ok) pushLog("terrain saved into the game (start a new game or re-enter the region to see it)", 3);
        else pushLog("terrain save failed: " + r.error, 2);
        if (writeObjectsAfterTerrain_) finishWriteBoth(r.ok);
    }
    if (worldFuture_.valid() && worldFuture_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        const WorldJob r = worldFuture_.get();
        for (const auto& n : r.notes) pushLog("world: " + n, 0);
        worldLastOk_ = r.ok;
        if (r.ok && !r.pack.empty()) pushLog("world: edits written into pack " + packLabel(r.pack) + " (Mods > Deploy puts them in the game)", 3);
        else if (r.ok) pushLog("world: maps moved (start a new game to walk the new layout)", 3);
        else pushLog("world: move failed: " + r.error, 2);
        if (r.ok) {
            acceptWorldWrite(r.submitted, r.undoSerial);
            if (saveRoot_.empty() || saveRoot_ == installPath_) { const std::string root = installPath_; scanInstall(root); }
        }
        if (closeSaveWaiting_) {
            closeSaveWaiting_ = false;
            if (r.ok && closePending_ && !hasUnsavedEdits() &&
                worldPendingCount() == 0 && dialogueStaged_.empty()) quit_ = true;
        }
    }
    if (newLevelFuture_.valid() && newLevelFuture_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        const NewLevelJob r = newLevelFuture_.get();
        for (const auto& n : r.result.notes) pushLog("new level: " + n, 0);
        if (r.ok && !r.pack.empty()) {
            // the level lives in the pack until Mods > Deploy: the explorer lists the game's maps
            pushLog("new level " + r.name + " written into pack " + packLabel(r.pack) + " (map slot " + std::to_string(r.result.mapSlot) + " in the pack's world; Mods > Deploy puts it in the game, then it opens here)", 3);
            if (r.ownRegion) raiseRule("region");
            newLevelDonor_.clear();
        } else if (r.ok) {
            pushLog("new level " + r.name + " installed (map slot " + std::to_string(r.result.mapSlot) + ", origin " + std::to_string(r.result.worldX) + "," + std::to_string(r.result.worldY) + ")", 3);
            if (r.ownRegion) raiseRule("region");
            newLevelDonor_.clear();
            if (saveRoot_.empty() || saveRoot_ == installPath_) {
                // Refresh the map list, preserving edits made while creation ran.
                const std::string root = installPath_;
                const bool keepCurrentEdits = hasUnsavedEdits();
                scanInstall(root);
                if (keepCurrentEdits) {
                    pushLog("new level: kept the current map because it was edited during creation; the new level is available in Maps", 1);
                } else {
                    discardEdits_ = true;
                    selectMap(r.name);
                }
            } else {
                pushLog("new level written under the save root " + saveRoot_ + " (not the explorer's install)", 1);
            }
        } else {
            pushLog("new level failed: " + r.error, 2);
        }
        if (r.ok) {
            const bool hadWorld = worldLoaded_ || worldPendingCount() != 0;
            worldLoaded_ = false; worldLoadedFrom_.clear();
            if (hadWorld) loadWorld(true);
        }
    }
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
            focusFilter_ = true;
            if (texturesMode_ && assetsTab_ == 4) setDialogueEditing(false);
            if (texturesMode_ && assetsTab_ != 2) settings_.showActions = true;
            else settings_.showExplorer = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) { if (io.KeyShift) openWorldFile(); else openLevelFile(); }
        if (!standaloneAssetPreview() && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) { settings_.showExplorer = !settings_.showExplorer; saveSettings(); }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_RightBracket)) { settings_.showActions = !settings_.showActions; saveSettings(); }
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
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    drawMenuBar();

    const float total = ImGui::GetContentRegionAvail().x;
    const float strip = theme::S(14.0f);
    const bool assetPreview = standaloneAssetPreview();
    const bool showExplorer = settings_.showExplorer && !assetPreview;
    const float leftStrip = assetPreview ? 0.0f : strip;
    float left = showExplorer ? std::clamp(total * 0.22f, theme::S(230.0f), theme::S(320.0f)) : 0.0f;
    float right = settings_.showActions ? std::clamp(total * 0.26f, theme::S(300.0f), theme::S(400.0f)) : 0.0f;
    // On compact windows the nominal panel minimums can exceed the window.
    // Share the available width rather than letting the tool panel run offscreen.
    const float panelsAvailable = std::max(0.0f, total - leftStrip - strip - theme::S(200.0f));
    if (left + right > panelsAvailable && left + right > 0) {
        const float fit = panelsAvailable / (left + right);
        left *= fit;
        right *= fit;
    }
    const float middle = std::max(0.0f, total - left - right - leftStrip - strip);

    if (showExplorer) { drawExplorer(left); ImGui::SameLine(0, 0); }
    if (!assetPreview) { drawPanelStrip(true); ImGui::SameLine(0, 0); }
    drawViewport(middle);
    ImGui::SameLine(0, 0);
    drawPanelStrip(false);
    if (settings_.showActions) { ImGui::SameLine(0, 0); drawActions(right); }
    drawUnsavedPrompt();
    if (firstRun_ && !auto_.active()) { firstRun_ = false; setupOpen_ = true; tourPending_ = true; }
    drawToolWindows();
    if (!texturesMode_ || assetsTab_ != 1) modelImportOpen_ = false;
    if (modelImportOpen_ &&
        beginToolWindow("##model_import", "Import model", "Choose a model and its destination.",
                        &modelImportOpen_, theme::S(460))) {
        const float pad = ImGui::GetCursorPosX();
        const float inner = ImGui::GetContentRegionAvail().x;
        drawModelImportCard(pad, inner, inner - theme::S(24));
        endToolWindow();
    }
    drawSetupPanel();
    drawHelpOverlay();
    drawTour();

    ImGui::End();
}

// The menu bar: File / View / Help on the left, the install status and the two quick
// buttons on the right. Replaces the old 52 px title strip (a modder's review called it
// redundant); the window title already says what the program is.
void App::drawMenuBar() {
    using theme::S;
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, theme::vec(theme::Bg1));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(7)));
    if (!ImGui::BeginMenuBar()) { ImGui::PopStyleVar(); ImGui::PopStyleColor(); return; }
    ImGui::TextColored(theme::vec(theme::Accent), "FableForge");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("v" ALBION_VERSION "   Fable: The Lost Chapters level editor");
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open world (.wld)...", "Ctrl+Shift+O")) openWorldFile();
        if (ImGui::MenuItem("Open level file (.lev)...", "Ctrl+O")) openLevelFile();
        ImGui::Separator();
        if (ImGui::MenuItem("Change install folder...")) changeInstall();
        if (ImGui::MenuItem("Setup check...")) setupOpen_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) requestClose();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::BeginDisabled(standaloneAssetPreview());
        if (ImGui::MenuItem("Map list", "Ctrl+[", settings_.showExplorer)) { settings_.showExplorer = !settings_.showExplorer; saveSettings(); }
        ImGui::EndDisabled();
        if (standaloneAssetPreview() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("The map list returns when you leave this asset preview.");
        if (ImGui::MenuItem("Tool panel", "Ctrl+]", settings_.showActions)) { settings_.showActions = !settings_.showActions; saveSettings(); }
        ImGui::Separator();
        if (ImGui::BeginMenu("Text size")) {
            // on top of the display DPI and the window size
            for (const float z : {0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f}) {
                char l[16]; std::snprintf(l, sizeof l, "%d %%", int(std::round(z * 100.0f)));
                if (ImGui::MenuItem(l, nullptr, std::fabs(settings_.uiScale - z) < 0.01f)) { settings_.uiScale = z; saveSettings(); }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard and mouse", "F1", helpOpen_)) helpOpen_ = !helpOpen_;
        if (ImGui::MenuItem("Take the tour")) tourStep_ = 0;
        ImGui::Separator();
        ImGui::TextDisabled("FableForge v" ALBION_VERSION);
        ImGui::EndMenu();
    }

    // right side: install status (click for the setup check), ?, Install...
    const float barW = ImGui::GetWindowWidth();
    const float btnW = S(84.0f), helpW = S(26.0f), gap = S(8.0f);
    const float barH = ImGui::GetCurrentWindow()->MenuBarHeight;   // fixed at Begin, not by the padding pushed here
    const float btnH = barH - S(6);
    // a redirected save root (scripted runs, scratch trees) is the one thing a writer
    // must not miss: every write goes there, not into the install shown
    const bool redirected = !saveRoot_.empty() && saveRoot_ != installPath_;
    std::string status = !installValid_ ? "no install selected" : redirected ? "writes -> " + saveRoot_ : installPath_;
    const float menuLineH = ImGui::GetTextLineHeight();
    ImGui::PushFont(fontSmall_);
    const float statusMax = std::max(S(120.0f), barW - ImGui::GetCursorPosX() - btnW - helpW - gap * 4 - S(24));
    if (ImGui::CalcTextSize(status.c_str()).x > statusMax) {
        while (status.size() > 4 && ImGui::CalcTextSize(("..." + status).c_str()).x > statusMax) status.erase(0, 1);
        status = "..." + status;
    }
    const float statusW = ImGui::CalcTextSize(status.c_str()).x;
    const float rowY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosX(barW - statusW - btnW - helpW - gap * 3);
    // the menu bar's cursor already sits inside the frame padding: centre the small
    // font on the regular-size menu labels
    ImGui::SetCursorPosY(rowY + (menuLineH - ImGui::GetTextLineHeight()) * 0.5f);
    const ImU32 dot = installValid_ ? (ctx_.ready() ? theme::col(theme::Success) : theme::col(theme::Warn)) : theme::col(theme::Error);
    ImGui::TextColored(theme::vec(redirected ? theme::Warn : theme::Muted), "%s", status.c_str());
    {
        // the dot on the text's own line: menu-bar text sits a frame padding below the cursor
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(a.x - S(10), (a.y + b.y) * 0.5f), S(4.0f), dot);
    }
    if (ImGui::IsItemHovered()) {
        const InstallHealth h = installHealth();
        ImGui::SetTooltip("%s  (%s)\ntextures.big %s   ForgeFSE %s   saves %s%s%s\nclick for the setup check", installValid_ ? installPath_.c_str() : "no install", installSource_.c_str(),
                          h.texturesBig ? "yes" : "no", h.fse ? "yes" : "no", h.saves ? "yes" : "no",
                          redirected ? "\nwrites go to " : "", redirected ? saveRoot_.c_str() : "");
    }
    if (ImGui::IsItemClicked()) setupOpen_ = true;
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetWindowPos().y + (barH - btnH) * 0.5f));
    ImGui::SetCursorPosX(barW - btnW - helpW - gap * 2);
    // the menu bar's tall frame padding would push a fixed-height button's label down: none here
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
    if (theme::ghostButton("?", ImVec2(helpW, btnH))) helpOpen_ = !helpOpen_;
    auto_.registerWidget("btn_help");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keyboard and mouse cheat-sheet  (? or F1)");
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetWindowPos().y + (barH - btnH) * 0.5f));
    ImGui::SetCursorPosX(barW - btnW - gap);
    if (theme::ghostButton("Install...", ImVec2(btnW, btnH))) changeInstall();
    ImGui::PopStyleVar();
    auto_.registerWidget("btn_change_install");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick the Fable: The Lost Chapters folder");
    ImGui::EndMenuBar();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::changeInstall() {
    const std::string picked = pickFolder(hwnd_, installPath_);
    if (!picked.empty()) {
        const std::string previous = installPath_;
        scanInstall(picked);
        if (installPath_ != previous) installSource_ = "manual";
    }
}

void App::openWorldFile() {
    static const COMDLG_FILTERSPEC types[] = {{L"Fable world (*.wld)", L"*.wld"}};
    const std::string start = installValid_ ? (fs::path(installPath_) / "data" / "Levels").string() : std::string();
    const std::string picked = pickPath(hwnd_, start, types, 1);
    if (!picked.empty()) openWorld(picked);
}

void App::openLevelFile() {
    static const COMDLG_FILTERSPEC types[] = {{L"Fable level (*.lev)", L"*.lev"}};
    const std::string start = installValid_ ? (fs::path(installPath_) / "data" / "Levels").string() : std::string();
    const std::string picked = pickPath(hwnd_, start, types, 1);
    if (!picked.empty()) openLooseLev(picked);
}

// A side panel's inner edge: a thin full-height strip with a chevron that hides or
// shows the panel. `left` = the strip belongs to the map list.
void App::drawPanelStrip(bool left) {
    using theme::S;
    bool& shown = left ? settings_.showExplorer : settings_.showActions;
    const float w = S(14.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg1));
    ImGui::BeginChild(left ? "##strip_l" : "##strip_r", ImVec2(w, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    const ImVec2 p = ImGui::GetWindowPos();
    const float h = ImGui::GetWindowHeight();
    ImGui::SetCursorScreenPos(p);
    if (ImGui::InvisibleButton("##toggle", ImVec2(w, h))) { shown = !shown; saveSettings(); }
    auto_.registerWidget(left ? "btn_toggle_explorer" : "btn_toggle_actions");
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // a child clips half its window padding off each side: draw over the whole strip, edges included
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + h), false);
    if (hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), theme::col(theme::Bg2));
    // the divider line is the viewport's (drawViewportEdges): it is drawn last, so nothing covers it
    // the chevron points the way the panel will move
    const bool pointLeft = left == shown;
    const float cx = p.x + w * 0.5f, cy = p.y + h * 0.5f, a = S(3.5f);
    const ImU32 c = theme::col(hov ? theme::Text : theme::Faint);
    if (pointLeft) dl->AddTriangleFilled(ImVec2(cx - a, cy), ImVec2(cx + a, cy - a * 1.6f), ImVec2(cx + a, cy + a * 1.6f), c);
    else dl->AddTriangleFilled(ImVec2(cx + a, cy), ImVec2(cx - a, cy - a * 1.6f), ImVec2(cx - a, cy + a * 1.6f), c);
    dl->PopClipRect();
    if (hov) ImGui::SetTooltip("%s", shown ? (left ? "Hide the map list  (Ctrl+[)" : "Hide the tool panel  (Ctrl+])")
                                           : (left ? "Show the map list  (Ctrl+[)" : "Show the tool panel  (Ctrl+])"));
    ImGui::EndChild();
}

void App::drawExplorer(float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg1));
    ImGui::BeginChild("##explorer", ImVec2(width, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    const ImVec2 p0 = ImGui::GetWindowPos();
    (void)p0;   // the divider is the strip's (drawPanelStrip)

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
    if (focusFilter_ && (!texturesMode_ || assetsTab_ == 2)) { ImGui::SetKeyboardFocusHere(); focusFilter_ = false; }
    if (ImGui::InputTextWithHint("##filter", width < S(250) ? "Search maps..." : "Search maps...   (Ctrl+F)", filterBuf_, sizeof filterBuf_)) filter_ = filterBuf_;
    ImGui::PopStyleVar();
    auto_.registerWidget("input_filter");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search map names  (Ctrl+F)");
    ImGui::Dummy(ImVec2(0, S(6)));
    {
        // a divider where the list starts: a row scrolled half under it reads as clipped, not as overlap
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x + S(16), at.y - S(3)), ImVec2(at.x + width - S(16), at.y - S(3)), theme::col(theme::Border));
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg1));
    ImGui::BeginChild("##maplist", ImVec2(-1.0f, 0), ImGuiChildFlags_None);   // 1 px short: the panel's border line stays visible
    ImGui::PopStyleColor();
    if (maps_.empty()) {
        ImGui::SetCursorPos(ImVec2(S(16), S(20)));
        ImGui::PushTextWrapPos(width - S(24));
        ImGui::TextColored(theme::vec(theme::Faint),
                           installValid_ ? "Reading the level list..." :
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
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.group.c_str());
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
            if (ImGui::IsItemHovered()) {
                if (m.loosePath.empty()) ImGui::SetTooltip("%s", m.name.c_str());
                else ImGui::SetTooltip("%s\n%s", m.name.c_str(), m.loosePath.c_str());
            }
            ImGui::PopID();
        }
        if (shown == 0) {
            ImGui::SetCursorPos(ImVec2(S(16), S(20)));
            ImGui::TextColored(theme::vec(theme::Faint), "No map matches \"%s\"", filter_.c_str());
        }
    }
    if (ImGui::GetScrollY() > 0.0f) {
        // rows scrolled under the top edge fade out instead of leaving a sliver of text
        const ImVec2 wp = ImGui::GetWindowPos();
        const float ww = ImGui::GetWindowWidth();
        const ImU32 solid = theme::col(theme::Bg1), clear = solid & 0x00FFFFFF;
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(wp, ImVec2(wp.x + ww, wp.y + S(14)), solid, solid, clear, clear);
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
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        cancelCarry();
        contextClickArmed_ = clickArmed_ = viewportCaptured_ = false;
        return;
    }
    const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    // Keep flying while RMB is held even if the cursor leaves the image.
    const bool active = viewportHovered_ || viewportCaptured_;
    viewportCaptured_ = active && (rmb || ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle));
    if (!active) return;
    const float dx = io.MouseDelta.x, dy = io.MouseDelta.y;
    const float panK = camera_.distance / std::max(size.y, 1.0f) * 1.6f;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        contextClickArmed_ = editMode_ && documentLoaded() && viewportHovered_;
        contextClickMoved_ = false;
        contextClickPos_ = io.MousePos;
    }
    if (contextClickArmed_) {
        const float mx = io.MousePos.x-contextClickPos_.x, my = io.MousePos.y-contextClickPos_.y;
        contextClickMoved_ |= mx*mx+my*my > 4 || io.MouseWheel != 0 ||
            ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_A) ||
            ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_D) ||
            ImGui::IsKeyDown(ImGuiKey_Q) || ImGui::IsKeyDown(ImGuiKey_E) ||
            ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            if (!contextClickMoved_ && viewportHovered_ && size.x > 0 && size.y > 0)
                selectionPopupRequested_ = pickContextSelection((io.MousePos.x-origin.x)/size.x, (io.MousePos.y-origin.y)/size.y);
            contextClickArmed_ = false;
        }
    }

    if (rmb) {
        // Ignore click jitter until it becomes a look gesture; flight keys and
        // wheel still act immediately and prevent a popup on release.
        if (!contextClickArmed_ || contextClickMoved_) camera_.look(-dx * 0.005f, dy * 0.005f);
        if (io.MouseWheel != 0) camera_.flySpeed = std::clamp(camera_.flySpeed * std::pow(1.25f, io.MouseWheel), 0.5f, 5000.0f);
        float fwd = 0, strafe = 0, rise = 0;
        if (ImGui::IsKeyDown(ImGuiKey_W)) fwd += 1; if (ImGui::IsKeyDown(ImGuiKey_S)) fwd -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_D)) strafe += 1; if (ImGui::IsKeyDown(ImGuiKey_A)) strafe -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_E)) rise += 1; if (ImGui::IsKeyDown(ImGuiKey_Q)) rise -= 1;
        const float boost = io.KeyShift ? 3.0f : 1.0f;
        if (fwd || strafe || rise) camera_.fly(fwd * boost, strafe * boost, rise * boost, std::min(io.DeltaTime, 0.1f));
    } else {
        if (carryCursorMode_) {
            if (viewportHovered_ && size.x > 0 && size.y > 0) {
                updateCarry((io.MousePos.x - origin.x) / size.x, (io.MousePos.y - origin.y) / size.y);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && carrying_) finishCarry();
            }
            return;
        }
        const bool terrainTool = editMode_ && gizmoOp_ == 4 && documentLoaded() && doc_.hasTerrain();
        const bool gizmo = editMode_ && (terrainTool || (selectedThing_ >= 0 && gizmoOp_ != 0 && (ImGuizmo::IsOver() || ImGuizmo::IsUsing())));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && viewportHovered_ && !gizmo) {
            clickArmed_ = true; clickPos_ = io.MousePos;
            const bool cloneCarry = io.KeyShift && io.KeyCtrl && !io.KeyAlt;
            if ((!io.KeyShift && !io.KeyCtrl && !io.KeyAlt || cloneCarry) && size.x > 0 && size.y > 0 &&
                armCarry((io.MousePos.x - origin.x) / size.x, (io.MousePos.y - origin.y) / size.y))
                carryCloneRequested_ = cloneCarry;
        }
        if (((carrying_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) || ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) && (carryArmed_ || !gizmo)) {
            if (carryArmed_) {
                const float mx = io.MousePos.x - clickPos_.x, my = io.MousePos.y - clickPos_.y;
                if (carrying_ || mx * mx + my * my >= 16.0f) {
                    clickArmed_ = false;
                    if (!carryCloneRequested_ || carryCloneActive_ || startCloneCarry())
                        updateCarry((io.MousePos.x - origin.x) / size.x, (io.MousePos.y - origin.y) / size.y);
                }
            } else if (io.KeyAlt) camera_.orbit(-dx * 0.008f, dy * 0.008f);
            else { camera_.turn(-dx * 0.005f); camera_.dolly(-dy * 0.02f); }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && carryArmed_) {
            finishCarry(); clickArmed_ = false;
        } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && clickArmed_) {
            clickArmed_ = false;
            const float mx = io.MousePos.x - clickPos_.x, my = io.MousePos.y - clickPos_.y;
            if (editMode_ && !gizmo && mx * mx + my * my < 16.0f && size.x > 0 && size.y > 0) {
                const float u = (io.MousePos.x - origin.x) / size.x, v = (io.MousePos.y - origin.y) / size.y;
                float position[3];
                if (io.KeyShift && !io.KeyCtrl && !io.KeyAlt && !placeDef_.empty() &&
                    (gizmoOp_ == 0 || gizmoOp_ == 1) && groundUnderCursor(u, v, position))
                    placeDefinitionAt(placeDef_, position);
                else pickAt(u, v);
            }
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

// The two panel dividers: one 1 px line on each side edge of the viewport (the strips' viewport side),
// drawn by the viewport itself because it is drawn after the strips and would cover theirs.
void App::drawViewportEdges() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
    // filled 1 px columns, not AddLine: an anti-aliased line smears over two pixels
    const float x0 = std::round(p.x), x1 = std::round(p.x + sz.x) - 1.0f;
    dl->PushClipRect(p, ImVec2(p.x + sz.x, p.y + sz.y), false);
    dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x0 + 1.0f, p.y + sz.y), theme::col(theme::Border));
    dl->AddRectFilled(ImVec2(x1, p.y), ImVec2(x1 + 1.0f, p.y + sz.y), theme::col(theme::Border));
    dl->PopClipRect();
}

void App::drawViewport(float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
    ImGui::BeginChild("##viewport", ImVec2(width, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (texturesMode_ && assetsTab_ == 3) {
        viewportHovered_ = viewportCaptured_ = false;
        drawEffectViewport(origin, size);
        drawViewportEdges();
        ImGui::EndChild();
        return;
    }
    if (texturesMode_ && assetsTab_ == 4) {
        viewportOrigin_ = origin; viewportSize_ = size;
        viewportHovered_ = viewportCaptured_ = false;
        drawDialogueViewport(origin, size);
        drawViewportEdges();
        ImGui::EndChild();
        return;
    }
    if (texturesMode_ && assetsTab_ == 1) {
        viewportHovered_ = viewportCaptured_ = false;
        drawModelViewport(origin, size);
        drawViewportEdges();
        ImGui::EndChild();
        return;
    }
    if (worldMode_ && world3D_) {
        drawWorld3D(origin, size);
        drawViewportEdges();
        ImGui::EndChild();
        return;
    }
    if (worldMode_) {
        thingGlyphs_.clear();
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##worldcanvas", ImVec2(std::max(size.x, 8.0f), std::max(size.y, 8.0f)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        viewportOrigin_ = origin; viewportSize_ = size;
        viewportHovered_ = ImGui::IsItemHovered();
        drawWorldCanvas(origin, size);
        auto_.registerWidget("viewport");
        drawToasts(origin, size);
        drawViewportEdges();
        ImGui::EndChild();
        return;
    }
    ID3D11ShaderResourceView* srv = renderer_.render(uint32_t(std::max(size.x, 8.0f)), uint32_t(std::max(size.y, 8.0f)), camera_, mode_, time_);
    if (srv) {
        ImGui::SetCursorScreenPos(origin);
        ImGui::Image((ImTextureID)(intptr_t)srv, size);
        viewportOrigin_ = origin; viewportSize_ = size;
        viewportHovered_ = ImGui::IsItemHovered();
        // The controls are drawn later in this child. Keep their clicks from
        // selecting terrain or clearing the current thing underneath them.
        if (viewportHovered_) {
            const float controlsTop = origin.y + size.y - ImGui::GetFrameHeight() - theme::S(16) - viewportControlsLift();
            if (ImGui::GetIO().MousePos.y >= controlsTop) viewportHovered_ = false;
        }
        refreshThingGlyphs(origin, size);
        if (editMode_ && selectedThing_ >= 0 && size.x >= theme::S(280) &&
            ImGui::IsMouseHoveringRect(ImVec2(origin.x+theme::S(12),origin.y+theme::S(100)),
                                      ImVec2(origin.x+theme::S(276),origin.y+theme::S(140))))
            viewportHovered_ = false;
        handleViewportInput(origin, size);
        terrainInput(origin, size);
        drawGizmo(origin, size);
        drawBrushCursor(origin, size);
        if (editMode_ && gizmoOp_ == 4 && terrainMode_ == 14 && !clipDrag_ &&
            clipRectValid_ && clipRectMap_ == doc_.mapName())
            drawGroundRect(origin, size, float(clipRect_[0]), float(clipRect_[1]),
                           float(clipRect_[2]), float(clipRect_[3]), IM_COL32(255, 190, 105, 230));
        drawLinkLines(origin, size);
        drawRadiusRings(origin, size);
        drawTrackLines(origin, size);
        drawThingGlyphs();
        applySectionVisibility();
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
        const bool compactHud = size.x < S(620);
        // text on channel 1, then a translucent backdrop behind the whole block on channel 0,
        // so the name and stats stay readable over bright terrain
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + S(16), origin.y + S(14)));
        ImGui::BeginGroup();
        ImGui::PushFont(fontTitle_);
        const MapEntry* cur = findEntry(previewLoadedFor_);
        ImGui::TextUnformatted(cur ? cur->name.c_str() : previewLoadedFor_.c_str());
        if (cur && !cur->loosePath.empty() && !compactHud) {
            ImGui::SameLine(0, S(10));
            ImGui::PushFont(fontSmall_);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(9));
            ImGui::TextColored(theme::vec(theme::Faint), "%s", cur->loosePath.c_str());
            ImGui::PopFont();
        }
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(origin.x + S(16), origin.y + S(44)));
        ImGui::PushFont(fontSmall_);
        if (compactHud) {
            ImGui::TextColored(theme::vec(theme::Muted), "%d x %d cells", previewScene_.mapWidth, previewScene_.mapHeight);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%zu vertices | height %.1f .. %.1f%s%s%s",
                previewScene_.vertices.size(), previewScene_.minHeight, previewScene_.maxHeight,
                cur && !cur->loosePath.empty() ? "\n" : "", cur && !cur->loosePath.empty() ? cur->loosePath.c_str() : "",
                previewTextured_ ? "" : "\nTextures loading...");
        } else ImGui::TextColored(theme::vec(theme::Muted), "%d x %d cells   |   %zu vertices   |   height %.1f .. %.1f%s",
                                previewScene_.mapWidth, previewScene_.mapHeight,
                                previewScene_.vertices.size(), previewScene_.minHeight, previewScene_.maxHeight,
                                previewTextured_ ? "" : "   |   textures loading...");
        if (!compactHud && (previewFoliage_ || previewThings_)) {
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
        ImGui::EndGroup();
        dl->ChannelsSetCurrent(0);
        const float pad = S(8);
        dl->AddRectFilled(ImVec2(ImGui::GetItemRectMin().x - pad, ImGui::GetItemRectMin().y - pad * 0.5f),
                          ImVec2(ImGui::GetItemRectMax().x + pad, ImGui::GetItemRectMax().y + pad * 0.5f),
                          (theme::col(theme::Bg0) & 0x00FFFFFF) | 0xB8000000, S(8.0f));
        dl->ChannelsMerge();
    }

    // Chips along the bottom: view modes on the left, layers on the right. When the
    // viewport is too narrow for one row the layer chips move up onto a second row.
    {
        if (size.x < S(620)) {
            ImGui::PushFont(fontSmall_);
            const float gap = S(5), margin = S(12), rowH = ImGui::GetFrameHeight();
            const float y = origin.y + size.y - rowH - S(10);
            const float frameW = S(53);
            const float menusW = size.x - 2 * margin - frameW - 2 * gap;
            const float menuW = menusW * 0.57f, showW = menusW - menuW;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + margin, y));
            ImGui::SetNextItemWidth(menuW);
            constexpr const char* shortViewNames[] = {"Color", "Wire", "Walk", "Height"};
            if (ImGui::BeginCombo("##viewport_view", shortViewNames[int(mode_)])) {
                for (int i = 0; i < 4; ++i) {
                    if (ImGui::Selectable(kModeNames[i], int(mode_) == i)) mode_ = ViewMode(i);
                    auto_.registerWidget((std::string("mode_view_") + kModeNames[i]).c_str());
                }
                ImGui::EndCombo();
            }
            auto_.registerWidget("combo_view_mode");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("View: %s", kModeNames[int(mode_)]);
            ImGui::SetCursorScreenPos(ImVec2(origin.x + margin + menuW + gap, y));
            if (ImGui::Button("Show...", ImVec2(showW, rowH))) ImGui::OpenPopup("##viewport_layers");
            auto_.registerWidget("btn_view_layers");
            if (ImGui::BeginPopup("##viewport_layers")) {
                bool foliage = previewFoliage_, things = previewThings_;
                if (ImGui::Checkbox("Foliage", &foliage)) setPreviewFoliage(foliage);
                auto_.registerWidget("check_view_foliage");
                if (ImGui::Checkbox("Objects", &things)) setPreviewThings(things);
                auto_.registerWidget("check_view_objects");
                ImGui::Checkbox("Markers", &showThingGlyphs_);
                auto_.registerWidget("check_view_markers");
                ImGui::Checkbox("Water", &renderer_.showWater);
                auto_.registerWidget("check_view_water");
                ImGui::Checkbox("Grid", &renderer_.showGrid);
                auto_.registerWidget("check_view_grid");
                bool neighbours = showNeighbours_;
                if (ImGui::Checkbox("Neighbours", &neighbours)) {
                    showNeighbours_ = neighbours;
                    if (neighbours) startNeighbourLoad();
                    else { renderer_.clearLayer(2); neighboursFor_.clear(); }
                }
                auto_.registerWidget("check_view_neighbours");
                ImGui::EndPopup();
            }
            ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - margin - frameW, y));
            if (ImGui::Button("Frame", ImVec2(frameW, rowH))) frameMap();
            auto_.registerWidget("chip_reset");
            ImGui::PopFont();
        } else {
        ImGui::PushFont(fontSmall_);
        const float gap = S(6), rowH = ImGui::GetFrameHeight() + S(2);
        const bool twoRows = viewportControlsLift() > 0;
        float layersW = 0;
        for (const char* name : {"Foliage", "Objects", "Markers", "Water", "Grid", "Neighbours"})
            layersW += ImGui::CalcTextSize(name).x + S(24) + gap;
        const float yModes = origin.y + size.y - rowH - S(10);
        const float yLayers = twoRows ? yModes - rowH - S(4) : yModes;
        float x = origin.x + S(14);
        if (twoRows) {
            const ImVec2 viewLabel = ImGui::CalcTextSize("View:");
            dl->AddText(ImVec2(x, yModes + (rowH - viewLabel.y) * 0.5f), theme::col(theme::Faint), "View:");
            x += viewLabel.x + S(8);
        }
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
        const float showLabelW = ImGui::CalcTextSize("Show:").x;
        float lx = twoRows ? origin.x + S(14) + showLabelW + S(8)
                           : origin.x + size.x - S(14) - layersW + gap;
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
        if (theme::chip("Markers", showThingGlyphs_)) showThingGlyphs_ = !showThingGlyphs_;
        auto_.registerWidget("chip_markers");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Editor points for things without a mesh appear as you zoom in.\nYellow diamond M: marker   Cyan circle E/I: region exit/entrance\nPurple square: camera, navigation, switch or other point.\nHover for its role and definition; click to inspect and edit.");
        ImGui::SameLine(0, gap);
        if (theme::chip("Water", renderer_.showWater)) renderer_.showWater = !renderer_.showWater;
        auto_.registerWidget("chip_water");
        ImGui::SameLine(0, gap);
        if (theme::chip("Grid", renderer_.showGrid)) renderer_.showGrid = !renderer_.showGrid;
        auto_.registerWidget("chip_grid");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The LEV cell grid (1 unit), heavier every 8 cells (one terrain patch).");
        ImGui::SameLine(0, gap);
        if (theme::chip(neighbourFuture_.valid() ? "Neighbours..." : "Neighbours", showNeighbours_)) {
            showNeighbours_ = !showNeighbours_;
            if (!showNeighbours_) { renderer_.clearLayer(2); neighboursFor_.clear(); }
            else startNeighbourLoad();
        }
        auto_.registerWidget("chip_neighbours");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The maps that touch this one, placed where FinalAlbion.wld puts them\n(low-res ground, no objects) -- see across the seams.");
        const char* hint = "RMB look + WASD fly   LMB dolly/turn   MMB pan   Alt+LMB orbit   Wheel zoom   F frame";
        const ImVec2 hs = ImGui::CalcTextSize(hint);
        const float hintRight = lx - ImGui::CalcTextSize("Show:").x - S(8) - S(28);   // clear of the "Show:" caption
        // the controls hint gets the same translucent pill as the HUD so it reads over any terrain
        auto hintAt = [&](ImVec2 at) {
            dl->AddRectFilled(ImVec2(at.x - S(8), at.y - S(3)), ImVec2(at.x + hs.x + S(8), at.y + hs.y + S(3)),
                              (theme::col(theme::Bg0) & 0x00FFFFFF) | 0xB8000000, S(6.0f));
            dl->AddText(at, theme::col(theme::Muted), hint);
        };
        if (!twoRows && hintRight - hs.x > modesEnd + S(16))
            hintAt(ImVec2(hintRight - hs.x, yModes + (rowH - hs.y) * 0.5f));
        else if (renderer_.hasMesh() && previewLoaded() && size.y > S(300))
            hintAt(ImVec2(origin.x + S(16), origin.y + S(72) + (previewTextured_ && previewScene_.unresolvedThemes > 0 ? ImGui::GetTextLineHeight() : 0)));
        ImGui::PopFont();
        }
    }
    drawSelectionActions(origin, size);
    drawViewportEdges();
        ImGui::EndChild();
}

void App::drawActions(float width) {
    using theme::S;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg1));
    ImGui::BeginChild("##actions", ImVec2(width, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    const ImVec2 p0 = ImGui::GetWindowPos();
    (void)p0;   // the divider is the strip's (drawPanelStrip)
    const float pad = S(16), inner = width - 2 * pad;
    // Footer: primary Export + batch/open-folder rows. Always visible, never scrolls away.
    const bool showOpen = lastExportOk_ && !exportFuture_.valid() && !batchActive();
    const MapEntry* footerEntry = findEntry(selectedName_);
    const bool showRegion = footerEntry && regions_.loaded && regions_.mapsOfRegion.count(footerEntry->group) && regionMapKeys(footerEntry->group).size() > 1 && !batchActive();
    const float footerHeight = modsMode_ ? S(72) : texturesMode_ ? S(60) : worldMode_ ? S(42 + 8 + 30 + 16 + 30) : editMode_ ? S(42 + 8 + 32 + 16 + 30) : S(42 + 8 + 32 + 16) + (showOpen ? S(40) : 0) + (showRegion ? S(40) : 0) + (batchActive() ? S(40) : 0);
    // The header never scrolls: the panel tabs and, in Edit, its sub-tabs (the cards below scroll).
    ImGui::SetCursorPos(ImVec2(pad, S(12)));
    {
        int tab = modsMode_ ? 4 : texturesMode_ ? 3 : worldMode_ ? 2 : editMode_ ? 1 : 0;
        bool changed = false;
        if (width < S(310)) {
            constexpr const char* tabs[] = {"Export", "Edit", "World", "Assets", "Mods"};
            ImGui::SetNextItemWidth(inner);
            if (ImGui::BeginCombo("##paneltab", tabs[tab])) {
                for (int i = 0; i < 5; ++i) {
                    if (ImGui::Selectable(tabs[i], tab == i)) { tab = i; changed = true; }
                    auto_.registerWidget((std::string("mode_panel_") + tabs[i]).c_str());
                }
                ImGui::EndCombo();
            }
        } else changed = theme::segmented("##paneltab", tab, {"Export", "Edit", "World", "Assets", "Mods"}, inner);
        if (changed) {
            if (tab == 4) setModsMode(true);
            else if (tab == 3) { setModsMode(false); setTexturesMode(true); }
            else if (tab == 2) { setModsMode(false); setTexturesMode(false); setWorldMode(true); }
            else { setModsMode(false); setTexturesMode(false); setWorldMode(false); if (editMode_ != (tab == 1)) setEditMode(tab == 1); }
        }
        auto_.registerWidget("seg_panel");
    }
    if (editMode_ && !modsMode_ && !texturesMode_ && !worldMode_ && documentLoaded()) {
        // the tool and the Terrain tab follow each other
        if (gizmoOp_ != lastGizmoOp_) {
            if (gizmoOp_ == 4) editTab_ = 1;
            else if (lastGizmoOp_ == 4 && editTab_ == 1) editTab_ = 0;
            lastGizmoOp_ = gizmoOp_;
        }
        ImGui::Dummy(ImVec2(0, S(2)));
        ImGui::SetCursorPosX(pad);
        int tab = editTab_;
        const char* sections[] = {"Objects", "Terrain", "Actors", "Level"};
        if ((inner - S(4)) / 4 < ImGui::CalcTextSize("Objects").x + S(8)) {
            ImGui::SetNextItemWidth(inner);
            if (ImGui::BeginCombo("##edit_section", sections[tab])) {
                for (int i = 0; i < 4; ++i) {
                    if (ImGui::Selectable(sections[i], i == tab)) setEditTab(i);
                    auto_.registerWidget((std::string("edit_section_") + sections[i]).c_str());
                }
                ImGui::EndCombo();
            }
            auto_.registerWidget("combo_edit_section");
        } else {
            if (theme::segmented("##edittab", tab, {"Objects", "Terrain", "Actors", "Level"}, inner)) setEditTab(tab);
        }
        auto_.registerWidget("seg_edit_tab"); // stable automation alias for either layout
    }
    ImGui::Dummy(ImVec2(0, S(6)));
    // The settings stack takes what it needs (measured last frame); the activity log
    // takes the rest when open. On a short window it starts folded so the tools get the height.
    const float availH = ImGui::GetContentRegionAvail().y;
    const bool logOpen = activityOpen_ < 0 ? availH >= S(760) : activityOpen_ == 1;   // -1: by the height until toggled
    const float logMin = logOpen ? S(72) : 0.0f, logHeader = S(30);
    float settingsH = settingsContentH_ > 0 ? settingsContentH_ + S(8) : availH * 0.6f;
    settingsH = std::min(settingsH, availH - footerHeight - logHeader - logMin);
    settingsH = std::max(settingsH, S(120));
    const float logHeight = std::max(logMin, availH - settingsH - footerHeight - logHeader);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg1));
    ImGui::BeginChild("##settings", ImVec2(width, settingsH), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    settingsScroll_ = ImGui::GetScrollY();
    ImGui::Dummy(ImVec2(0, S(4)));

    const float cardInner = inner - S(24);
    if (modsMode_) {
        drawModsPanel(pad, inner, cardInner);
        ImGui::Dummy(ImVec2(0, S(6)));
        settingsContentH_ = ImGui::GetCursorPosY();
        ImGui::EndChild();  // ##settings
    } else if (texturesMode_) {
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


    ImGui::Dummy(ImVec2(0, S(6)));
    settingsContentH_ = ImGui::GetCursorPosY();
    ImGui::EndChild();  // ##settings
    }

    // ---- footer
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p0.x + pad, ImGui::GetCursorScreenPos().y), ImVec2(p0.x + width - pad, ImGui::GetCursorScreenPos().y), theme::col(theme::Border));
    ImGui::Dummy(ImVec2(0, S(8)));
    if (modsMode_) {
        ImGui::SetCursorPosX(pad);
        ImGui::PushFont(fontSmall_);
        theme::hintMore("The load order is kept next to Fable.exe; Undeploy restores the originals.", "The order lives in forge_mods.json next to Fable.exe. Deploy writes the merged files with .forgebak originals; Undeploy restores them. Setup's Restore also reverts the staged deployment and restores other tracked backups.");
        ImGui::PopFont();
    } else if (texturesMode_) {
        ImGui::SetCursorPosX(pad);
        ImGui::PushFont(fontSmall_);
        if (assetsTab_ == 3) theme::hint("Effects inspection is read-only.");
        else if (assetsTab_ == 4) {
            ImGui::PopFont();
            if(theme::primaryButton(dialogueToolsOpen_?"Back to dialogue":"Edit lip sync", ImVec2(inner,S(32)),dialogueLoaded_))
                setDialogueEditing(!dialogueToolsOpen_);
            auto_.registerWidget("button_dialogue_tools");
            ImGui::PushFont(fontSmall_);
            if(!dialogueStaged_.empty()) ImGui::Text("%zu unsaved line(s)",dialogueStaged_.size());
        }
        else theme::hintMore("The asset tools write the game's shared banks; originals are backed up once.", "The asset tools write the game's shared banks (textures.big, graphics.big, game.bin). Each original is backed up once as <file>.forge-orig; Setup > Restore puts them back.");
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

    // Activity log takes whatever height is left; the header folds it (short windows start folded).
    ImGui::SetCursorPosX(pad);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushFont(fontBold_);
        ImGui::TextColored(theme::vec(theme::Muted), logOpen ? "ACTIVITY  v" : "ACTIVITY  >");
        ImGui::PopFont();
        if (!logOpen && !log_.empty()) {
            // folded: the newest line, cut to fit, in its level's colour
            ImGui::SameLine(0, S(10));
            ImGui::PushFont(fontSmall_);
            const float room = inner - (ImGui::GetCursorScreenPos().x - at.x) - S(4);
            const std::string last = theme::fitText(log_.back().second, room);
            const int lvl = log_.back().first;
            ImGui::TextColored(lvl == 1 ? theme::vec(theme::Warn) : lvl == 2 ? theme::vec(theme::Error) : lvl == 3 ? theme::vec(theme::Success) : theme::vec(theme::Faint), "%s", last.c_str());
            ImGui::PopFont();
        }
        ImGui::SetCursorScreenPos(at);
        if (ImGui::InvisibleButton("##activityhdr", ImVec2(inner, ImGui::GetTextLineHeightWithSpacing()))) activityOpen_ = logOpen ? 0 : 1;
        auto_.registerWidget("btn_activity");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(logOpen ? "Fold the activity log" : "Show the activity log");
    }
    if (!logOpen) { ImGui::EndChild(); return; }
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

} // namespace albion::gui
