#include "app.hpp"

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
    if (!auto_.active()) loadSettings(savedInstall);   // scripted runs stay deterministic
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

void App::startContextLoad() {
    if (!installValid_) return;
    ctxPending_ = std::make_shared<te::Context>();
    const fs::path root = installPath_;
    auto ctx = ctxPending_;
    ctxFuture_ = std::async(std::launch::async, [ctx, root]() {
        std::string err;
        const bool ok = ctx->load(root, root / "data" / "graphics" / "pc" / "textures.big", err);
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
    auto it = std::find_if(maps_.begin(), maps_.end(), [&](const MapEntry& m) { return m.key == nameOrKey; });
    if (it == maps_.end()) it = std::find_if(maps_.begin(), maps_.end(), [&](const MapEntry& m) { return m.name == nameOrKey; });
    if (it == maps_.end()) return;
    selectedName_ = it->key;
    scrollToSelected_ = true;
    groupOpen_[it->group] = true;
    renderer_.clearLayer(0);
    renderer_.clearLayer(1);
    foliageLoadedFor_.clear();
    foliageInstances_ = 0;
    thingInstances_ = 0;
    foliageStatus_.clear();
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
    foliageFuture_ = std::async(std::launch::async, [entry, ctx, root]() {
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
        if (r.name == selectedName_) {
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
                renderer_.uploadLayer(1, r.things, te::UpAxis::Y);
                foliageStatus_ += ", " + std::to_string(r.things.instances.size()) + " objects";
            } else if (r.things.found) {
                foliageStatus_ += ", no placed objects";
            }
            for (const auto& w : r.things.warnings) pushLog("objects: " + w, 1);
            for (const auto& w : r.scene.warnings)
                if (w.rfind("mesh", 0) != 0) pushLog("foliage: " + w, 1);
        }
        if (!foliagePendingName_.empty()) { foliagePendingName_.clear(); if (!foliageLoaded()) startFoliageLoad(); }
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
    for (auto& l : logPending_) log_.push_back(std::move(l));
    logPending_.clear();
    if (log_.size() > 400) log_.erase(log_.begin(), log_.begin() + long(log_.size() - 400));
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
    v.push_back("maps=" + std::to_string(maps_.size()));
    v.push_back("selected=" + selectedName_);
    v.push_back("preview_loaded=" + std::string(previewLoaded() ? "1" : "0"));
    v.push_back("preview_textured=" + std::string(previewTextured_ ? "1" : "0"));
    v.push_back("context_ready=" + std::string(ctx_.ready() ? "1" : "0"));
    v.push_back("mode=" + std::string(kModeNames[int(mode_)]));
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
    return v;
}

// ------------------------------------------------------------------ drawing

void App::frame(float dt) {
    time_ += dt;
    pollWorkers();
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) focusFilter_ = true;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E) && !exportFuture_.valid() && !selectedName_.empty()) startExport();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !filter_.empty() && !ImGui::IsAnyItemActive()) setFilter("");
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    {
        // Size factor: a 1200 px tall window is 1.0; 1080p runs at 0.9, 1440p at 1.2,
        // anything shorter than ~1000 px at 0.85. Snapped to 0.05 so a resize does not
        // rebuild fonts every frame.
        const float sizeFactor = std::clamp(vp->Size.y / 1200.0f, 0.85f, 1.25f);
        wantScale_ = std::round(dpiScale_ * sizeFactor * 20.0f) / 20.0f;
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
    std::string status = installValid_ ? installPath_ : "no install selected";
    const float statusMax = std::max(S(120.0f), p.x + w - btnW - S(70) - (titleEnd + (w > S(760) ? subtitleW + S(24) : S(12))));
    if (ImGui::CalcTextSize(status.c_str()).x > statusMax) {
        while (status.size() > 4 && ImGui::CalcTextSize(("..." + status).c_str()).x > statusMax) status.erase(0, 1);
        status = "..." + status;
    }
    const float statusW = ImGui::CalcTextSize(status.c_str()).x;
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - statusW - btnW - S(40), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f));
    const ImU32 dot = installValid_ ? (ctx_.ready() ? theme::col(theme::Success) : theme::col(theme::Warn))
                                    : theme::col(theme::Error);
    dl->AddCircleFilled(ImVec2(ImGui::GetCursorScreenPos().x - S(12), ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f), S(4.0f), dot);
    ImGui::TextColored(theme::vec(theme::Muted), "%s", status.c_str());
    if (installValid_ && ImGui::IsItemHovered()) ImGui::SetTooltip("%s  (%s)", installPath_.c_str(), installSource_.c_str());
    ImGui::PopFont();
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
    (void)origin;
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
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            if (io.KeyAlt) camera_.orbit(-dx * 0.008f, dy * 0.008f);
            else { camera_.turn(-dx * 0.005f); camera_.dolly(-dy * 0.02f); }
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) camera_.pan(-dx * panK, dy * panK);
        if (io.MouseWheel != 0 && viewportHovered_) camera_.dolly(io.MouseWheel);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl && !ImGui::IsAnyItemActive()) frameMap();
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

    ID3D11ShaderResourceView* srv = renderer_.render(uint32_t(std::max(size.x, 8.0f)), uint32_t(std::max(size.y, 8.0f)), camera_, mode_, time_);
    if (srv) {
        ImGui::SetCursorScreenPos(origin);
        ImGui::Image((ImTextureID)(intptr_t)srv, size);
        viewportHovered_ = ImGui::IsItemHovered();
        handleViewportInput(origin, size);
    }
    auto_.registerWidget("viewport");

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
        const char* layerNames[3] = {"Foliage", "Objects", "Water"};
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
        const char* hint = "RMB look + WASD fly   LMB dolly/turn   MMB pan   Alt+LMB orbit   Wheel zoom   F frame";
        const ImVec2 hs = ImGui::CalcTextSize(hint);
        if (!twoRows && lx - hs.x - S(24) > modesEnd + S(16))
            dl->AddText(ImVec2(lx - hs.x - S(24), yModes + (rowH - hs.y) * 0.5f), theme::col(theme::Faint), hint);
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
    const float footerHeight = S(42 + 8 + 32 + 16) + (showOpen ? S(40) : 0) + (showRegion ? S(40) : 0) + (batchActive() ? S(40) : 0);
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

    ImGui::SetCursorPos(ImVec2(pad, S(14)));
    ImGui::PushFont(fontBold_);
    ImGui::TextColored(theme::vec(theme::Muted), "EXPORT");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(4)));

    const float cardInner = inner - S(24);
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

    // ---- footer
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p0.x + pad, ImGui::GetCursorScreenPos().y), ImVec2(p0.x + width - pad, ImGui::GetCursorScreenPos().y), theme::col(theme::Border));
    ImGui::Dummy(ImVec2(0, S(8)));
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
        if (rest == "viewport" && widgets_.count("viewport")) {
            const ImVec4 r = widgets_["viewport"];
            setVirtualMouse((r.x + r.z) * 0.5f, (r.y + r.w) * 0.5f);
        } else { float x = 0, y = 0; std::istringstream(rest) >> x >> y; setVirtualMouse(x, y); }
        io.AddMousePosEvent(vmX_, vmY_);
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
        else if (key == "texsize") s.texSize = std::atoi(val.c_str());
        else if (key == "world") s.world = val == "1";
        else if (key == "preview_things") app.setPreviewThings(val == "1");
        else if (key == "preview_water") app.setPreviewWater(val == "1");
        else if (key == "texels") s.texels = std::atoi(val.c_str());
        else if (key == "tile") s.tile = float(std::atof(val.c_str()));
        else if (key == "gain") { s.gain = float(std::atof(val.c_str())); app.previewLoadedFor_.clear(); app.startPreviewLoad(); }
        else if (key == "up") s.up = lower(val) == "z" ? 1 : 0;
        else if (key == "outdir") { s.outDir = val; std::snprintf(app.outDirBuf_, sizeof app.outDirBuf_, "%s", val.c_str()); }
        else fail("set: unknown key " + key);
        note("ok   " + line); ++pc_;
    }
    else if (cmd == "export") { app.startExport(); note("ok   " + line); ++pc_; }
    else if (cmd == "export_all") { app.startBatchExport(app.visibleMapNames()); note("ok   " + line); ++pc_; }
    else if (cmd == "export_region") {
        const MapEntry* e = app.findEntry(app.selectedName());
        if (!e) fail("export_region: nothing selected");
        else { app.settings().world = true; app.startBatchExport(app.regionMapKeys(e->group)); note("ok   " + line); }
        ++pc_;
    }
    else if (cmd == "wait_batch") waitOn(!app.batchActive() && !app.exportBusy(), "batch export");
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
    else if (cmd == "assert_widget") {
        if (!widgets_.count(rest)) fail("widget not on screen: " + rest); else note("ok   " + line);
        ++pc_;
    }
    else if (cmd == "dump_state") { for (const auto& kv : app.stateDump()) note("     " + kv); ++pc_; }
    else if (cmd == "quit") { quit_ = true; note("ok   quit"); return false; }
    else { fail("unknown command: " + line); ++pc_; }
    return true;
}

} // namespace albion::gui
