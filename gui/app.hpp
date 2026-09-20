#pragma once
// FableForge GUI application state + ImGui drawing. Three-slot layout:
// explorer (left) | 3D preview (middle) | actions (right). All heavy work
// (install scan, texture context, preview bake, export) runs on worker threads
// and lands on the main thread through futures polled every frame.

#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <d3d11.h>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <array>

#include "imgui.h"
#include "foliageexport.hpp"
#include "leveledit.hpp"
#include "livelink.hpp"
#include "backups.hpp"
#include "renderer.hpp"
#include "thingsexport.hpp"
#include "terrainexport.hpp"
#include "worldedit.hpp"
#include "presets.hpp"
#include "gtg.hpp"
#include "texturebrowse.hpp"
#include "overworld.hpp"

namespace albion::gui {

struct MapEntry {
    std::string name;        // display + output file stem
    std::string key;         // unique selection key (name, or "file:<stem>" for loose files)
    std::string group;
    uint32_t size = 0;
    std::string loosePath;   // set when a loose .lev overrides the WAD copy
    float worldX = 0, worldY = 0;
    bool hasWorld = false;
};

struct ExportSettings {
    int format = 0;          // 0 = glb, 1 = obj
    bool textures = true;
    int texels = 8;
    float tile = 8.0f;
    float gain = 1.0f;
    bool layers = false;
    bool walkable = false;
    bool foliage = true;     // export baked grass/plants as instances
    bool things = true;      // export placed objects (.tng)
    bool water = true;       // export the water surface
    bool creatures = false;  // export scripted creatures (bind pose)
    int texSize = 0;         // object/plant texture cap: 0 full, 1 half (256), 2 quarter (128)
    bool world = false;      // place at WLD MapX/MapY so maps line up
    int up = 0;              // 0 = Y, 1 = Z
    float uiScale = 1.0f;    // user font/UI scale on top of DPI + window size (0.8 .. 1.5)
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
    // The scripted mouse position, re-applied by the host every frame so the
    // Win32 backend's real-cursor fallback cannot override it.
    bool virtualMouse(float& x, float& y) const { x = vmX_; y = vmY_; return haveVm_; }
    void setVirtualMouse(float x, float y) { vmX_ = x; vmY_ = y; haveVm_ = true; }
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
    float dragDx_ = 0, dragDy_ = 0;   // drag_gizmo in progress when dragPhase_ > 0
    int dragPhase_ = 0;
    float dragX0_ = 0, dragY0_ = 0;
    std::map<std::string, ImVec4> widgets_;
    std::vector<std::string> failures_;
    std::vector<std::string> log_;
    bool quit_ = false;
    float camSnap_[3] = {0, 0, 0};
    float vmX_ = 0, vmY_ = 0;
    bool haveVm_ = false;
    std::string logPath_;
    double t0_ = 0;   // wall clock at load(); every log line carries the elapsed seconds
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
    bool foliageLoaded() const { return foliageLoadedFor_ == selectedName_ && !selectedName_.empty(); }
    bool foliageBusy() const { return foliageFuture_.valid(); }
    void setPreviewFoliage(bool on);
    bool previewFoliage() const { return previewFoliage_; }
    void setPreviewThings(bool on);
    bool previewThings() const { return previewThings_; }
    void setPreviewWater(bool on) { renderer_.showWater = on; }
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
    // A file dropped on the window: .lev opens as a loose map; a .png / .jpg / .tga becomes
    // the custom-texture input (Edit > Terrain > paint) with a name guessed from the file.
    bool openDropped(const std::string& path);
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
    bool logContains(const std::string& needle) const;   // any drained log line holding `needle` (scripted assert_log)

    // ---- editor (placed things). The document is the source of truth; the
    // preview instances follow it (fast path for moves, reload for structure).
    void setEditMode(bool on);
    bool editMode() const { return editMode_; }
    bool documentLoaded() const { return docLoadedFor_ == selectedName_ && !selectedName_.empty(); }
    editor::Document& document() { return doc_; }
    int selectedThing() const { return selectedThing_; }
    void selectThing(int index);
    int selectByDefinition(const std::string& def);   // first thing with this DefinitionType
    // Multi-selection (0.16 #4): the primary (selectedThing_, the gizmo's pivot and the
    // Selection card) plus extras kept by UID so they survive index shifts. Ctrl+click in
    // the viewport / the objects list toggles; the gizmo moves the whole set as a rigid
    // group about the primary; Del / Ctrl+D / Ctrl+C / Ctrl+V act on the set (one undo step).
    void toggleSelect(int index);
    std::vector<int> selectionIndices() const;        // primary first, then the extras that still exist
    size_t selectionCount() const { return selectionIndices().size(); }
    void copySelection();
    void pasteClipboard();
    // Presets (0.16 #2): shipped next to the exe (presets/) + the user's (%APPDATA%/FableForge/presets)
    std::vector<std::filesystem::path> presetFolders() const;
    void refreshPresets();
    bool placePreset(const std::string& name);         // at the view centre, on the ground; selects it
    bool savePresetFromSelection(const std::string& name, const std::string& description);
    const std::vector<editor::PresetInfo>& presets() const { return presets_; }
    // Region entrance (0.16 #3): where the map screen / teleports drop the hero in this
    // map's WLD slot (FinalAlbion.gtg). Set at the view centre, facing the camera.
    bool setEntranceHere();
    std::optional<editor::RegionEntrance> currentEntrance() const;
    void drawEntranceCard(float pad, float inner, float cardInner);
    bool hasClipboard() const { return !clipboard_.empty(); }
    // Screen position (window pixels) of the selected thing's pivot, for scripted gizmo drags.
    bool selectedPivotScreen(float& x, float& y) const;
    void setGizmoOp(int op) { gizmoOp_ = op; }
    int gizmoOp() const { return gizmoOp_; }
    // Ray pick at viewport-relative (u, v) in [0,1]; selects the hit thing.
    int pickAt(float u, float v);
    // terrain tool (scripted tests): one brush application at a map-local point
    void terrainStroke(float x, float y, float seconds);
    void setTerrainMode(int m) { terrainMode_ = m; }
    void setPaintTheme(int slot) { paintTheme_ = slot; }
    void setBrush(float radius, float strength) { brushRadius_ = radius; brushStrength_ = strength; }
    bool terrainDeployBusy() const { return terrainDeployFuture_.valid(); }
    void deployTerrain() { startTerrainDeploy(); }
    void moveSelected(float dx, float dy, float dz);   // map-local Fable units
    void rotateSelected(float degrees);                // yaw about the up axis
    void scaleSelected(float factor);
    void snapSelectedToGround();
    void reseatThings();             // objects on ground that changed since the last save follow it
    bool addPaintTheme(const std::string& name);   // ENGINE_THEME -> a free LEV palette slot, selected for painting
    char themeSearch_[64] = {};
    // a ground theme from the user's own PNG (textures.big + game.bin append), then into the palette
    bool createCustomTheme(const std::string& png, const std::string& name, const std::string& donor, const std::string& cliffPng = "");
    char customPng_[512] = {};
    char customName_[64] = {};
    bool customThemeOpen_ = false;
    void duplicateSelected();
    void deleteSelected();
    void editUndo();
    void editRedo();
    void frameSelected();
    bool placeDefinition(const std::string& def, const std::string& scriptName = "");   // at the camera focus point, on the ground; CREATURE_ as an AICreature
    bool saveDocument();                                // loose .tng under saveRoot()
    bool deployDocument();                              // FinalAlbion.wad under saveRoot()
    void revertDocument();
    // Where saves go: the install by default; tests point it at a scratch tree.
    void setSaveRoot(const std::string& root) { saveRoot_ = root; }
    std::string saveRoot() const { return saveRoot_.empty() ? installPath_ : saveRoot_; }

private:
    void scanInstall(const std::string& root);
    void startContextLoad(const std::string& root = "");   // textures.big + defs from `root` (default: the install)
    void startPreviewLoad();
    void startFoliageLoad();
    void pollWorkers();
    void pushLog(const std::string& line, int level = 0);
    // The one confirm pattern (0.15b #7): an amber question, then [Yes, <verb>] [Cancel]
    // side by side at `width`. Returns 1 for yes, -1 for cancel, 0 while undecided.
    // `widget` names the yes button for the automation ("btn_x_confirm").
    int confirmRow(const char* question, const char* yes, float width, float height, const char* widget);
    std::string resolveLevPath(const MapEntry& e, std::string& err);
    void startExportOf(const MapEntry& entry);
    const MapEntry* findEntry(const std::string& key) const;
    void loadSettings(std::string& savedInstall);
    std::string settingsPath() const;
    // what the chosen folder offers: each false switches a feature off with a reason
    struct InstallHealth { bool gameBin = false, wad = false, stb = false, texturesBig = false, fse = false, saves = false; };
    InstallHealth installHealth() const;
    void drawSetupPanel();
    bool setupOpen_ = false;         // the Setup modal (first run, or the status click)
    bool helpOpen_ = false;          // the shortcut cheat-sheet (? / F1, or the header button)
    void drawHelpOverlay();
    bool confirmRestore_ = false;
    std::vector<backups::Entry> backupList_;
    double backupsScannedAt_ = -1;
public:
    bool restoreAllBackups();        // put every backed-up file back (refused while the game runs)
    void rescanBackups() { backupsScannedAt_ = -1; }
private:
    bool firstRun_ = false;
    // First-run tour (0.15b #6): three callouts after the Setup panel closes the first
    // time -- the map list, the panel tabs, the viewport -- each with Next / Skip.
    int tourStep_ = -1;              // -1 off, 0..2 the callout shown
    bool tourPending_ = false;       // the first run: start the tour when Setup closes
    void drawTour();
public:
    void setTourStep(int s) { tourStep_ = s; }
    int tourStep() const { return tourStep_; }
private:

    void drawTitleBar();
    void drawExplorer(float width);
    void drawViewport(float width);
    void drawActions(float width);
    void handleViewportInput(const ImVec2& origin, const ImVec2& size);
    void frameMap();

    // editor internals (gui/editor.cpp)
    void openDocument();
    void bindInstances(const foliageexport::Scene& things);   // after a things upload
    void syncInstances();                                     // document revision -> instance worlds / reload
    void applyFrame(int thing, const editor::Frame& f);       // preview only (no command)
    bool frameOfSelected(editor::Frame& f) const;
    void commitFrame(const editor::Frame& f);                 // document command
    void drawGizmo(const ImVec2& origin, const ImVec2& size);
    void editorShortcuts();
    void drawEditPanel(float pad, float inner, float cardInner);
    void drawEditFooter(float pad, float inner);
    void startThingsReload();
    bool editMode_ = false;
    editor::Document doc_;
    std::string docLoadedFor_;
    int selectedThing_ = -1;
    uint64_t selectedUid_ = 0;
    int gizmoOp_ = 1;            // 0 select, 1 move, 2 rotate, 3 scale
    std::vector<uint64_t> extraUids_;                 // the multi-selection beyond the primary
    editor::Frame gizmoStart_;                        // the primary's frame when the drag began
    std::vector<std::pair<int, editor::Frame>> groupStart_;   // the extras' frames when the drag began
    editor::Document::Fragment clipboard_;
    std::vector<editor::PresetInfo> presets_;
    bool presetsLoaded_ = false;
    char presetName_[64] = {};
    void drawPresetsCard(float pad, float inner, float cardInner);
    // particle emitter card (Actors tab): pick an effects.big entry, place an emitter
    std::vector<std::string> effectNames_;
    bool effectsLoaded_ = false;
    char effectSearch_[64] = {};
    std::string effectPick_;
    void drawEffectsCard(float pad, float inner, float cardInner);
public:
    bool placeEmitter(const std::string& effectName, const std::string& scriptName = "");
private:
    void syncExtraSelection();                        // renderer.alsoSelected from extraUids_
    editor::Frame groupFrame(const editor::Frame& start) const;
    // Edit panel sub-tabs (0.15b #1): 0 Objects (selection / list / add), 1 Terrain (brush +
    // paint), 2 Actors (village / spawner / live link), 3 Level (new level). The Terrain tab
    // and the terrain tool follow each other; actions raise the tab that owns their card so
    // a result (and its engine-rule notice) is on screen. Remembered in settings.json.
    int editTab_ = 0;
    int lastGizmoOp_ = -1;
public:
    void setHelpOpen(bool on) { helpOpen_ = on; }
    void setDefSearch(const std::string& s) { std::snprintf(defSearch_, sizeof defSearch_, "%s", s.c_str()); }
    void setThemeSearch(const std::string& s) { std::snprintf(themeSearch_, sizeof themeSearch_, "%s", s.c_str()); }
    void setEditTab(int tab) { editTab_ = std::clamp(tab, 0, 3); if (editTab_ == 1 && doc_.hasTerrain()) gizmoOp_ = 4; else if (editTab_ != 1 && gizmoOp_ == 4) gizmoOp_ = 1; lastGizmoOp_ = gizmoOp_; }
    int editTab() const { return editTab_; }
private:
    bool gizmoSnap_ = false;
    bool gizmoWasUsing_ = false;
    editor::Frame gizmoFrame_;   // frame while dragging
    std::vector<std::array<float, 16>> instLocal_;   // instance = local * thing world
    std::vector<uint64_t> instUids_;                 // uid per thing index when instances were bound
    uint64_t syncedRevision_ = 0;
    bool thingsReloadPending_ = false;
    std::string saveRoot_;
    float settingsScroll_ = 0;       // ##settings ScrollY (state dump, wheel tests)
    char defSearch_[64] = {};
    std::vector<std::pair<std::string, std::string>> defList_;   // (name, type) placeable definitions
    char thingSearch_[64] = {};
    bool confirmDeploy_ = false;
    // engine-rule notice at the point of action (0.15 #2): raised by the action that the
    // rule applies to, drawn directly under that action's button, dismissed per rule for
    // the session ("Got it"). Keys: creature | spawner | region.
    std::string ruleKey_;
    std::set<std::string> rulesDismissed_;
    void raiseRule(const std::string& key);
    // 24 px albedo swatch for an ENGINE_THEME's base texture in the theme pickers
    // (null when the texture is not decoded yet); draws it + the label on one row
    ID3D11ShaderResourceView* themeSwatch(const std::string& themeName);
    void themeRow(const std::string& themeName, const ImVec2& at, float size, const char* label);   // draw-only, over an item already submitted
    // Object palette thumbnails: the definition's mesh rendered once (renderer cache); at
    // most one new one is decoded per frame so the list never stalls. Null = no mesh.
    ID3D11ShaderResourceView* defThumbnail(const std::string& def, bool& pending);
    std::map<std::string, ID3D11ShaderResourceView*> defThumbs_;
    std::vector<terrainexport::Image> thumbImages_;
    std::map<uint32_t, int> thumbTextureToImage_;
    bool thumbBankOpen_ = false;
    int thumbBudget_ = 0;
public:
    void dismissRule(const std::string& key) { rulesDismissed_.insert(key); if (ruleKey_ == key) ruleKey_.clear(); }
private:
    void drawRuleNotice(const char* key, float width);
    // terrain tool (gizmoOp_ == 4)
    int terrainMode_ = 0;            // 0 raise, 1 lower, 2 flatten, 3 smooth, 4 walkable, 5 blocked, 6 paint theme
    int paintTheme_ = 0;             // LEV palette slot for mode 6
    uint64_t syncedThemeRev_ = 0;
    bool rebakePending_ = false;     // a theme stroke ended: re-bake the ground albedo from the LEV
    void startThemeRebake();
    float brushRadius_ = 6.0f;
    float brushStrength_ = 4.0f;
    bool brushHit_ = false;
    float brushFable_[2] = {0, 0};   // map-local x/y under the cursor
    uint64_t syncedTerrainRev_ = 0;
    struct TerrainDeployResult { bool ok = false; std::string error; std::vector<std::string> notes; };
    std::future<TerrainDeployResult> terrainDeployFuture_;
    // new level from the selected map (donor): inputs, the donor lookup and the install job
    char newLevelName_[64] = "";
    int newLevelX_ = 0, newLevelY_ = 0;
    std::string newLevelRegion_;
    std::string newLevelDonor_;            // donor the fields were filled for
    editor::DonorInfo newLevelInfo_;
    bool newLevelInfoOk_ = false;
    int newLevelMode_ = 0;                 // 0 copy of this map, 1 blank 64x64
    std::string blankTemplate_;            // retail map (of the chosen size) whose palette/header a blank level reuses
    std::vector<editor::MapSize> blankSizes_;
    bool newLevelOwnRegion_ = false;       // own region + minimap
    bool newLevelDedicated_ = true;        // a new region slot (default) instead of a filler take-over
    std::vector<editor::ReusableRegion> reusableRegions_;
    char newLevelDisplay_[64] = "";
    int blankSize_ = -1;                   // index into blankSizes_
    std::vector<std::string> blankPalette_;
    int blankTheme_ = -1;
    float blankHeight_ = 20.0f;
    struct NewLevelJob { bool ok = false; std::string error; std::string name; bool ownRegion = false; editor::NewLevelResult result; };
    std::future<NewLevelJob> newLevelFuture_;
    void drawNewLevelCard(float pad, float inner, float cardInner);
    // enemy spawner card: CREATURE_GENERATION_FAMILY picker + radius/limit, placed at the view centre
    void drawSpawnerCard(float pad, float inner, float cardInner);
    // village card: VILLAGE_* picker; membership lives in the Selection card
    void drawVillageCard(float pad, float inner, float cardInner);
    // live link card: ForgeFSE hook install/remove, hero heartbeat, go-here / spawn-here
    void drawLiveLinkCard(float pad, float inner, float cardInner);
    livelink::Status link_;
    double linkPolledAt_ = -1;
    bool linkFollow_ = false;              // camera follows the hero while he is in this map
    uint64_t linkLastSent_ = 0;
public:
    bool linkInstall();
    bool linkRemove();
    bool linkGoHere();                     // teleport the hero to the camera focus (transition when in another map)
    bool linkSpawnSelected();              // spawn the selected creature at its position
    bool linkPing();
    bool linkReload();                     // re-stream the hero's region (he must be in this map)
    void linkPoll(bool force = false);
    const livelink::Status& linkStatus() const { return link_; }
private:
    std::vector<std::pair<std::string, std::string>> villageList_;
    char villageSearch_[64] = {};
public:
    bool placeVillage(const std::string& def, const std::string& scriptName = "");
    bool setSelectedVillage(uint64_t villageUid);   // 0 = leave
private:
    std::vector<std::string> familyList_;
    std::vector<std::string> spawnerFamilies_;   // chosen families
    char familySearch_[64] = {};
    float spawnerRadius_ = 12.0f;
    int spawnerLimit_ = 3;
public:
    // scripted: place a spawner at the view centre with these families
    bool placeSpawner(const std::vector<std::string>& families, float radius, int limit, const std::string& scriptName = "");
private:
    void startNewLevel();
    void setNewLevelOwnRegion(bool on) { newLevelOwnRegion_ = on; }
    void setNewLevelDedicated(bool on) { newLevelDedicated_ = on; }
    void setNewLevelBlank(int theme, float height, int w = 0, int h = 0) { newLevelMode_ = 1; blankTheme_ = theme; blankHeight_ = height; if (w > 0) selectBlankSize(w, h); }
    void selectBlankSize(int w, int h);
    void setNewLevel(const std::string& name, int x, int y, const std::string& region) { std::snprintf(newLevelName_, sizeof newLevelName_, "%s", name.c_str()); newLevelX_ = x; newLevelY_ = y; newLevelRegion_ = region; }
    bool newLevelBusy() const { return newLevelFuture_.valid(); }
    bool confirmTerrainDeploy_ = false;

    // ---- overworld (gui/world.cpp): every map's box on a 2D grid, drag to move,
    // pending moves applied to the install in one go (WLD/BWD/STB)
public:
    void setWorldMode(bool on);
    // ---- Textures tab (gui/textures.cpp, 0.17): browse / preview / export / replace / add
    void setTexturesMode(bool on);
    bool texturesMode() const { return texturesMode_; }
    void refreshTextures();
    std::filesystem::path texturesBigPath() const;
    bool selectTexture(const std::string& nameOrLabel);
    const texbrowse::TextureRow* selectedTexture() const;
    bool exportSelectedTexture(const std::string& outPath);
    bool replaceSelectedTexture(const std::string& image);
    bool addTexture(const std::string& name, const std::string& image, const std::string& bank, const std::string& format);
    std::vector<uint32_t> selectedThingTextures() const;
    void drawTexturesPanel(float pad, float inner, float cardInner);
private:
    bool texturesMode_ = false;
    bool texturesLoaded_ = false;
    std::vector<texbrowse::TextureRow> texRows_;
    std::vector<std::string> texBanks_;
    std::string texBank_, texSelected_, texPreviewFor_, lastTexturePng_;
    ID3D11ShaderResourceView* texPreview_ = nullptr;
    char texSearch_[64] = {};
    char texImagePath_[512] = {};
    char texAddName_[64] = {};
    char texAddPath_[512] = {};
    int texAddFormat_ = 0;
    bool texReplaceOpen_ = false;
    std::vector<std::vector<uint32_t>> meshTextures_;   // per renderer mesh: its parts' diffuse texture ids
public:
    bool worldMode() const { return worldMode_; }
    bool worldLoaded() const { return worldLoaded_; }
    void worldSelect(const std::string& map);
    const std::string& worldSelected() const { return worldSelected_; }
    // queue a move (validated: 32-aligned, no overlap); returns false with the reason in the log
    bool worldMove(const std::string& map, int x, int y);
    void worldRevert();
    // undo/redo over the pending region edits (moves, owners, visibility): one step per
    // move / owner change / visibility toggle / revert; cleared when the edits are written
    bool worldUndo();
    bool worldRedo();
    bool worldCanUndo() const { return !worldUndo_.empty(); }
    bool worldCanRedo() const { return !worldRedo_.empty(); }
    void worldApply();
    bool worldBusy() const { return worldFuture_.valid(); }
    size_t worldPendingCount() const { return worldPending_.size() + worldOwnerEdits_.size() + worldSeesEdits_.size(); }
    // region edits (queued like moves): the owning region of a map, and whether a region sees a map
    bool worldSetOwner(const std::string& map, const std::string& region);
    bool worldSetSees(const std::string& region, const std::string& map, bool sees);
    std::string worldOwnerOf(const std::string& map) const;            // pending edit or the layout's owner
    bool worldSees(const std::string& region, const std::string& map) const;
    bool worldLastOk() const { return worldLastOk_; }
    void setWorldStitch(bool on, int feather) { worldStitch_ = on; worldStitchFeather_ = feather; }
private:
    void loadWorld();
    void drawWorldCanvas(const ImVec2& origin, const ImVec2& size);
    void drawWorldPanel(float pad, float inner, float cardInner);
    void drawWorldFooter(float pad, float inner);
    bool worldPlacement(const std::string& map, int& x, int& y) const;   // pending move or the layout's box
    bool worldMode_ = false;
    bool worldLoaded_ = false;
    std::string worldLoadedFrom_;
    editor::WorldLayout world_;
    std::vector<editor::MapMove> worldPending_;
    std::vector<editor::OwnerEdit> worldOwnerEdits_;
    std::vector<editor::SeesEdit> worldSeesEdits_;
    struct WorldSnap { std::vector<editor::MapMove> moves; std::vector<editor::OwnerEdit> owners; std::vector<editor::SeesEdit> sees; };
    std::vector<WorldSnap> worldUndo_, worldRedo_;
    WorldSnap worldSnapshot() const { return WorldSnap{worldPending_, worldOwnerEdits_, worldSeesEdits_}; }
    void worldRestore(const WorldSnap& s);
    void worldPushUndo();
    std::string worldSelected_;
    std::string worldHover_;
    float worldPanX_ = 0, worldPanY_ = 0, worldZoom_ = 0;   // zoom = pixels per world unit (0 = fit)
    bool worldDragging_ = false;
    int worldDragX_ = 0, worldDragY_ = 0;    // the dragged box's candidate origin (snapped)
    bool worldDragValid_ = false;
    std::string worldDragWhy_;
    ImVec2 worldDragStart_;
    bool worldPanning_ = false;
    int worldEditX_ = 0, worldEditY_ = 0;    // the panel's X/Y fields
    std::string worldEditFor_;
    bool confirmWorldApply_ = false;
    bool worldStitch_ = false;               // average shared-edge heights with every neighbour after a move (off: retail leaves seams as they are)
    int worldStitchFeather_ = -1;            // cells the seam correction fades over (-1 = auto: one per unit of step, 4..32)
    bool worldLastOk_ = false;
    struct WorldJob { bool ok = false; std::string error; std::vector<std::string> notes; };
    std::future<WorldJob> worldFuture_;
    void terrainInput(const ImVec2& origin, const ImVec2& size);
    void drawBrushCursor(const ImVec2& origin, const ImVec2& size);
    void syncTerrain();
    void startTerrainDeploy();
    bool clickArmed_ = false;
    ImVec2 clickPos_;
    std::string pendingSelect_;      // map switch held back by the unsaved-changes prompt
    bool promptInAuto_ = false;      // scripted runs skip the prompt unless they opt in
    bool discardEdits_ = false;      // set by the prompt's Discard: the next selectMap drops the document
    void drawUnsavedPrompt();
    bool hasUnsavedEdits() const { return documentLoaded() && (doc_.dirty() || (doc_.hasTerrain() && doc_.terrainDirty())); }
    ImVec2 viewportOrigin_, viewportSize_;
    // Viewport overlays (0.15b #3): the ground under the cursor (map-local Fable
    // coordinates + height) and a compass; drawn every frame the viewport is hovered.
    bool cursorHit_ = false;
    float cursorFable_[3] = {0, 0, 0};   // x, y (Fable), height
    void drawViewportOverlays(const ImVec2& origin, const ImVec2& size);
    // Toasts (0.15b #2): every warning / error / success log line also surfaces in the
    // viewport's top-right corner for a few seconds, so a job's result is seen without
    // reading the Activity log. Info lines (level 0) stay in the log only.
    struct Toast { int level; std::string text; float at; };
    // The long jobs (new level, terrain deploy, world apply) report their stage from
    // their thread; the busy button shows it with the elapsed time.
    mutable std::mutex jobMutex_;
    std::string jobStage_;
    float jobStart_ = 0;
    void beginJob() { std::lock_guard<std::mutex> l(jobMutex_); jobStage_ = "starting"; jobStart_ = time_; }
    editor::ProgressFn jobProgress() { return [this](const std::string& s) { std::lock_guard<std::mutex> l(jobMutex_); jobStage_ = s; }; }
    std::string jobLabel(const char* verb) const;   // "<verb>: <stage>  (12 s)"
    std::vector<Toast> toasts_;
    void drawToasts(const ImVec2& origin, const ImVec2& size);

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
    terrainexport::RegionIndex regions_;
    std::vector<std::string> regionMapKeys(const std::string& region) const;
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
    struct FoliageResult { std::string name; foliageexport::Scene scene; foliageexport::Scene things; thingsexport::Stats thingStats; bool thingsOnly = false; };
    bool previewThings_ = true;
    size_t thingInstances_ = 0;
    std::future<FoliageResult> foliageFuture_;
    std::string foliageLoadedFor_;
    std::string foliagePendingName_;
    bool previewFoliage_ = true;
    size_t foliageInstances_ = 0;
    std::string foliageStatus_;

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
    bool scrollToSelected_ = false;
    bool gainDirty_ = false;
    std::string lastFramedFor_;
    std::vector<std::pair<int, std::string>> log_;   // level, line (0 info, 1 warn, 2 error, 3 success)
    std::mutex logMutex_;
    std::vector<std::pair<int, std::string>> logPending_;

    // ui
    // UI scale = monitor DPI x window-size factor; fonts are rebuilt when it changes.
    float dpiScale_ = 1.0f;
    float uiScale_ = 1.0f;
    float wantScale_ = 1.0f;
    float settingsContentH_ = 0;   // measured last frame; lets the activity log take the slack
    void buildFonts(float scale);
public:
    // Main loop hook: true when the font atlas must be rebuilt before the next frame.
    bool fontsDirty() const { return std::fabs(wantScale_ - uiScale_) > 0.01f; }
    void rebuildFonts();
    void setDpiScale(float s) { dpiScale_ = s; }
private:
    ImFont* fontBody_ = nullptr;
    ImFont* fontBold_ = nullptr;
    ImFont* fontTitle_ = nullptr;
    ImFont* fontSmall_ = nullptr;
    char filterBuf_[128] = {};
    char outDirBuf_[512] = {};
    bool viewportHovered_ = false;
    bool viewportCaptured_ = false;
    Automation auto_;
    friend class Automation;
};

} // namespace albion::gui
