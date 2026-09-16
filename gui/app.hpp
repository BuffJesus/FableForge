#pragma once
// AlbionTerrain GUI application state + ImGui drawing. Three-slot layout:
// explorer (left) | 3D preview (middle) | actions (right). All heavy work
// (install scan, texture context, preview bake, export) runs on worker threads
// and lands on the main thread through futures polled every frame.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <d3d11.h>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"
#include "renderer.hpp"
#include "terrainexport.hpp"

namespace albion::gui {

struct MapEntry {
    std::string name;        // display + output file stem
    std::string key;         // unique selection key (name, or "file:<stem>" for loose files)
    std::string group;
    uint32_t size = 0;
    std::string loosePath;   // set when a loose .lev overrides the WAD copy
};

struct ExportSettings {
    int format = 0;          // 0 = glb, 1 = obj
    bool textures = true;
    int texels = 8;
    float tile = 4.0f;
    bool layers = false;
    bool walkable = false;
    int up = 0;              // 0 = Y, 1 = Z
    std::string outDir;
};

// Scripted driver for automated UI tests (see docs/AUTOMATION.md).
class Automation {
public:
    bool active() const { return active_; }
    void load(const std::string& scriptPath);
    // Called once per frame by the app after drawing. Returns false when the
    // script asked to quit.
    bool tick(class App& app);
    void registerWidget(const char* id);   // records the last item's rect
    bool takeScreenshot(std::string& path); // host polls this after rendering
    int exitCode() const { return failures_.empty() ? 0 : 1; }
    const std::vector<std::string>& failures() const { return failures_; }
    void fail(const std::string& why);
    void note(const std::string& what);
    bool wantsQuit() const { return quit_; }
private:
    bool active_ = false;
    std::vector<std::string> lines_;
    size_t pc_ = 0;
    int waitFrames_ = 0;
    double deadline_ = 0;   // wall-clock seconds for the current waiting command
    std::string pendingShot_;
    std::string clickTarget_;
    int clickPhase_ = 0;
    std::map<std::string, ImVec4> widgets_;
    std::vector<std::string> failures_;
    std::vector<std::string> log_;
    bool quit_ = false;
    std::string logPath_;
};

class App {
public:
    App();
    ~App();
    bool init(ID3D11Device* device, ID3D11DeviceContext* context, HWND hwnd,
              const std::string& installOverride);
    void frame(float dt);
    Automation& automation() { return auto_; }
    bool wantsQuit() const { return quit_; }

    // ---- state accessors used by Automation ----
    void selectMap(const std::string& name);
    bool previewLoaded() const { return previewLoadedFor_ == selectedName_ && !selectedName_.empty(); }
    bool previewBusy() const { return previewFuture_.valid(); }
    bool exportBusy() const { return exportFuture_.valid(); }
    bool contextReady() const { return ctx_.ready(); }
    bool contextBusy() const { return ctxFuture_.valid(); }
    bool mapsReady() const { return !maps_.empty(); }
    const std::string& selectedName() const { return selectedName_; }
    std::string lastExportPath() const { return lastExportPath_; }
    bool lastExportOk() const { return lastExportOk_; }
    void startExport();
    // Queue every map whose name is in `names` (sequential; reuses the export worker).
    void startBatchExport(const std::vector<std::string>& names);
    void cancelBatch();
    bool batchActive() const { return !batchQueue_.empty() || batchTotal_ > 0; }
    std::vector<std::string> visibleMapNames() const;
    // Load a loose .lev from disk (drag & drop / automation) and select it.
    bool openLooseLev(const std::string& path);
    void saveSettings() const;
    ExportSettings& settings() { return settings_; }
    Camera& camera() { return camera_; }
    void setMode(ViewMode m) { mode_ = m; }
    ViewMode mode() const { return mode_; }
    void setFilter(const std::string& f);
    bool installValid() const { return installValid_; }
    std::string lastError() const { return lastError_; }
    void requestQuit() { quit_ = true; }
    std::vector<std::string> stateDump() const;

private:
    void scanInstall(const std::string& root);
    void startContextLoad();
    void startPreviewLoad();
    void pollWorkers();
    void pushLog(const std::string& line, int level = 0);
    std::string resolveLevPath(const MapEntry& e, std::string& err);
    void startExportOf(const MapEntry& entry);
    const MapEntry* findEntry(const std::string& key) const;
    void loadSettings(std::string& savedInstall);
    std::string settingsPath() const;

    void drawTitleBar();
    void drawExplorer(float width);
    void drawViewport(float width);
    void drawActions(float width);
    void handleViewportInput(const ImVec2& origin, const ImVec2& size);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    HWND hwnd_ = nullptr;
    Renderer renderer_;
    Camera camera_;
    ViewMode mode_ = ViewMode::Textured;
    float time_ = 0;
    bool quit_ = false;

    // install
    std::string installPath_;
    std::string installSource_;
    bool installValid_ = false;
    std::vector<MapEntry> maps_;
    std::string filter_;
    std::map<std::string, bool> groupOpen_;
    std::string lastError_;

    // texture context
    terrainexport::Context ctx_;
    std::future<std::pair<bool, std::string>> ctxFuture_;
    std::shared_ptr<terrainexport::Context> ctxPending_;
    std::string ctxError_;

    // preview
    struct PreviewResult { std::string name; terrainexport::Scene scene; std::string error; bool textured = false; };
    std::string selectedName_;
    std::string previewLoadedFor_;
    bool previewTextured_ = false;
    std::future<PreviewResult> previewFuture_;
    std::string previewPendingName_;
    terrainexport::Scene previewScene_;   // kept for stats
    bool reloadWhenContextReady_ = false;
    int previewTexels_ = 4;

    // export
    ExportSettings settings_;
    struct ExportResult { bool ok = false; std::string path; std::vector<std::string> files; std::vector<std::string> log; double seconds = 0; };
    std::future<ExportResult> exportFuture_;
    std::string lastExportPath_;
    bool lastExportOk_ = false;
    std::vector<std::string> batchQueue_;
    int batchTotal_ = 0, batchDone_ = 0, batchFailed_ = 0;
    std::string batchCurrent_;
    bool focusFilter_ = false;
    std::vector<std::pair<int, std::string>> log_;   // level, line (0 info, 1 warn, 2 error, 3 success)
    std::mutex logMutex_;
    std::vector<std::pair<int, std::string>> logPending_;

    // ui
    ImFont* fontBody_ = nullptr;
    ImFont* fontBold_ = nullptr;
    ImFont* fontTitle_ = nullptr;
    ImFont* fontSmall_ = nullptr;
    char filterBuf_[128] = {};
    char outDirBuf_[512] = {};
    bool viewportHovered_ = false;
    Automation auto_;
    friend class Automation;
};

} // namespace albion::gui
