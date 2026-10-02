#pragma once
#include "detailretry.hpp"
#include "cutoutcache.hpp"
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
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <array>

#include "worldtiles.hpp"
#include "worldvisibility.hpp"
#include "deferredrelease.hpp"
#include "detailcache.hpp"
#include "forge/budget.hpp"
#include "forge/levelstore.hpp"
#include "forge/lipsync.hpp"
#include "forge/lipsync_preset.hpp"
#include "forge/dialoguetext.hpp"
#include "forge/headpose.hpp"
#include "forge/modorder.hpp"
#include "imgui.h"
#include "foliageexport.hpp"
#include "leveledit.hpp"
#include "livelink.hpp"
#include "backups.hpp"
#include "temporarydirectory.hpp"
#include "stbcompact.hpp"
#include "renderer.hpp"
#include "worldscenery.hpp"
#include "thingsexport.hpp"
#include "terrainexport.hpp"
#include "worldedit.hpp"
#include "presets.hpp"
#include "gtg.hpp"
#include "texturebrowse.hpp"
#include "effects.hpp"
#include "particlepreview.hpp"
#include "particlepreviewrenderer.hpp"
#include "overworld.hpp"
#include "worlddraft.hpp"

namespace albion::gui {

class DialogueAudioPlayer;

struct MapEntry {
    std::string name;        // display + output file stem
    std::string key;         // unique selection key (name, or "file:<stem>" for loose files)
    std::string group;
    uint32_t size = 0;
    std::string loosePath;   // set when a loose .lev overrides the WAD copy
    float worldX = 0, worldY = 0;
    bool hasWorld = false;
    std::string worldFile;   // set for a map of an opened world other than FinalAlbion (its .wld)
    std::string tngPath;     // ... and the .tng beside its .lev
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
    bool showExplorer = true; // the left map list (View menu / Ctrl+[)
    bool showActions = true;  // the right tool panel (View menu / Ctrl+])
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
    void registerRect(const char* id, const ImVec4& rect) { if (active_) widgets_[id] = rect; }
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
    std::string capturePrefix_;
    std::string captureShot_;
    int captureFrame_ = 0;
    bool worldDetailWaitStarted_ = false;
    std::string worldCoverageWatch_;
    size_t worldCoverageWatchFrames_ = 0;
    std::string clickTarget_;
    std::string revealTarget_;
    std::string documentSnap_,thingSnap_;
    uint64_t thingSnapUid_=0;
    std::array<float,4> meshOrientationSnap_={0,0,0,1};
    int32_t meshOrientationId_=-1;
    std::optional<editor::Frame> frameSnap_;
    std::optional<editor::Frame> ownedFrameSnap_;
    uint64_t ownedSnapUid_=0;
    uint64_t ownedSnapParentUid_=0;
    std::vector<float> heightSnap_;   // snapshot_heights / assert_heights_changed   // reveal <widget>: scroll its window so the widget is on screen
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
    // A world other than FinalAlbion (a .wld under data\Levels, like the vanilla
    // editor's File > Load World): its maps join the list, grouped by region.
    bool openWorld(const std::string& wldPath);
    void saveSettings() const;
    ExportSettings& settings() { return settings_; }
    Camera& camera() { return camera_; }
    void setMode(ViewMode m) { mode_ = m; }
    ViewMode mode() const { return mode_; }
    void setFilter(const std::string& f);
    bool installValid() const { return installValid_; }
    std::string lastError() const { return lastError_; }
    void requestQuit() { quit_ = true; }
    void requestClose();
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
    bool selectedGlyphScreen(float& x, float& y) const;
    void setGizmoOp(int op) { gizmoOp_ = op; }
    int gizmoOp() const { return gizmoOp_; }
    // Ray pick at viewport-relative (u, v) in [0,1]; selects the hit thing.
    int pickAt(float u, float v);
    bool groundUnderCursor(float u, float v, float out[3]) const;
    bool armCarry(float u, float v);
    void updateCarry(float u, float v);
    void finishCarry();
    void cancelCarry();
    // terrain tool (scripted tests): one brush application at a map-local point
    void terrainStroke(float x, float y, float seconds);
    void setTerrainMode(int m) { terrainMode_ = m; }
    void setPaintTheme(int slot) { paintTheme_ = slot; }
    void setBrush(float radius, float strength) { brushRadius_ = radius; brushStrength_ = strength; }
    bool terrainDeployBusy() const { return terrainDeployFuture_.valid(); }
    void deployTerrain() { startTerrainDeploy(); }
    // terrain first, then the objects once that write succeeded (both touch FinalAlbion.wad)
    bool startWriteBoth();
    bool writeBothPending() const { return writeObjectsAfterTerrain_; }
    void moveSelected(float dx, float dy, float dz);   // map-local Fable units
    void rotateSelected(float degrees);                // yaw about the up axis
    void rotateSelectedWorld(float degrees,int axis);  // 0=Z, 1=X, 2=Y
    void setSelectedFacing(float turns);
    void scaleSelected(float factor);
    void snapSelectedToGround();
    void reseatThings();             // objects on ground that changed since the last save follow it
    bool addPaintTheme(const std::string& name);   // ENGINE_THEME -> a free LEV palette slot, selected for painting
    char themeSearch_[64] = {};
    // a ground theme from the user's own PNG (textures.big + game.bin append), then into the palette
    bool createCustomTheme(const std::string& png, const std::string& name, const std::string& donor, const std::string& cliffPng = "");
    char customPng_[512] = {};
    char customName_[64] = {};
    std::string customThemeError_;
    // Import model (Edit > Objects): a .glb/.gltf/.obj becomes MESH_/OBJECT_<NAME> (src/meshimport), off the UI thread
    char meshModelPath_[512] = {};
    char meshName_[64] = {};
    char meshTexturePng_[512] = {};
    std::string meshImportError_;
    std::string meshImportSuccess_;
    struct MeshImportJob {
        bool ok = false;
        std::string error;
        std::vector<std::string> notes;
        std::string objectName;
        std::array<std::string, 3> submittedFields;
    };
    std::future<MeshImportJob> meshImportFuture_;
public:
    bool importMesh(const std::string& model, const std::string& name, const std::string& texturePng);
    bool meshImportBusy() const { return meshImportFuture_.valid(); }
    void pollMeshImport();
private:
    void duplicateSelected();
    void deleteSelected();
    void applyOwnedDelete(bool includeOwned);
    void editUndo();
    void editRedo();
    void frameSelected();
    void showSelectedInPalette();
    void drawPlacementOptions(float width);
    bool placeDefinition(const std::string& def, const std::string& scriptName = "");   // at the camera focus point, on the ground; CREATURE_ as an AICreature
    bool placeDefinitionAt(const std::string& def, const float position[3], const std::string& scriptName = "");
    bool saveDocument();                                // loose .tng under saveRoot()
    bool deployDocument();                              // FinalAlbion.wad under saveRoot()
    void revertDocument();
    // Where saves go: the install by default; tests point it at a scratch tree.
    void setSaveRoot(const std::string& root);
    std::string saveRoot() const { return saveRoot_.empty() ? installPath_ : saveRoot_; }

private:
    void scanInstall(const std::string& root);
    void startContextLoad(const std::string& root = "");   // textures.big + defs from `root` (default: the install)
    void startPreviewLoad();
    void startFoliageLoad(bool foliageOnly = false);
    void pollWorkers();
    void pushLog(const std::string& line, int level = 0);
    // The one confirm pattern (0.15b #7): an amber question, then [Yes, <verb>] [Cancel]
    // side by side at `width`. Returns 1 for yes, -1 for cancel, 0 while undecided.
    // `widget` names the yes button for the automation ("btn_x_confirm").
    int confirmRow(const char* question, const char* yes, float width, float height, const char* widget);
    using LevWorkspace = std::shared_ptr<albion::detail::TemporaryDirectory>;
    std::string resolveLevPath(const MapEntry& e, LevWorkspace& scratch, std::string& err);
    void startExportOf(const MapEntry& entry);
    const MapEntry* findEntry(const std::string& key) const;
    void loadSettings(std::string& savedInstall);
    std::string settingsPath() const;
    // what the chosen folder offers: each false switches a feature off with a reason
    // levels = FinalAlbion.wad or the loose FinalAlbion\*.lev files (modders' extracted installs);
    // missing = the full path of every required file that is not there, for the Setup panel
    struct InstallHealth { bool gameBin = false, levels = false, stb = false, texturesBig = false, fse = false, saves = false;
                           std::string levelsHow; std::vector<std::string> missing; };
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
    // the static-map bank's dead space (header + table read, polled with the backups)
    forge::stb::CompactReport bankReport_;
    bool bankReportOk_ = false;
    std::future<stbcompact::Result> compactFuture_;
public:
    bool compactBank();              // scripted too: compact_stb
    bool compactBusy() const { return compactFuture_.valid(); }
private:
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

    void drawMenuBar();
    void drawPanelStrip(bool left);
    void changeInstall();
    void openLevelFile();
    void openWorldFile();
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
    std::vector<int> unlockedSelection(bool report = true);
    void setSelectedLocked(bool locked);
    void cycleSelectedSurfaces();
    std::optional<float> surfaceBelow(const editor::Frame&,const std::vector<int>& excludedThings) const;
    void requestSelectionHeight();
    void setSelectedHeight(float height);
    bool selectionHeightRequested_=false,selectionHeightPopupOpen_=false;
    float selectionHeight_=0;
    std::string selectionHeightMap_;
    std::vector<uint64_t> selectionHeightUids_;
    bool ownedDeleteRequested_=false,ownedDeletePopupOpen_=false;
    std::vector<size_t> ownedDeleteRoots_;
    std::vector<int> ownedDeleteSelection_;
    std::string ownedDeleteMap_;
    uint64_t ownedDeleteRevision_=0;
    size_t ownedDeleteCount_=0;
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
    bool moveOwned_=true;
    std::string ownedCountMap_;
    uint64_t ownedCountRevision_=~uint64_t(0);
    std::vector<int> ownedCountSelection_;
    size_t ownedCountCached_=0;
    std::set<int> ownedPreview_;
    void previewOwned(const std::vector<std::pair<int,editor::Frame>>& roots);
    void restoreOwnedPreview();
    void commitFramesWithOwned(const std::vector<std::pair<int,editor::Frame>>& roots);
    editor::Frame gizmoStart_;                        // the primary's frame when the drag began
    std::vector<std::pair<int, editor::Frame>> groupStart_;   // the extras' frames when the drag began
    bool carryArmed_ = false, carrying_ = false;
    bool carryCloneRequested_ = false, carryCloneActive_ = false, carryCursorMode_ = false;
    uint64_t carryOriginalUid_ = 0;
    std::vector<uint64_t> carryOriginalExtraUids_;
    editor::Frame carryStart_, carryFrame_;
    float carryGrab_[2] = {}, carryOffsetZ_ = 0.0f;
    std::vector<std::pair<int, editor::Frame>> carryGroup_;
    editor::Frame carriedExtra(const editor::Frame& start) const;
    bool startCloneCarry();
    bool beginCursorCloneCarry();
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
    // placeable definitions with their GroupDef (THING_GROUP), sorted type / group / name:
    // the vanilla editor's Things tree (thing type -> group -> def)
    std::vector<terrainexport::Context::GroupedDefinition> defList_;
    std::string placeDef_;           // the def the Place button puts down
    std::string revealDef_;          // pending palette tree/scroll target
    // how new things face and sit (the vanilla Things dialog's placement options)
    int placeFacing_ = 0;            // 0 toward the camera, 1 random (vanilla), 2 fixed angle
    float placeAngleDeg_ = 0.0f;     // fixed angle, degrees clockwise from +Y (vanilla turns x 360)
    bool placeFixedHeight_ = false;  // vanilla "Place at constant height": absolute Z, never under the ground
    // vanilla owner combo (NEditGui::CMenu PLAYER_LIST_BOX -> CEditControlCentre::SelectedPlayerNumber):
    // -1 Auto, 0..3 Player N, 4 Neutral. Placement: creatures, buildings, villages, objects take it;
    // Auto = a creature def's DefaultOwner, 4 otherwise (PaintInputPlaceThingAt 0x02994490, owner 5 ->
    // CThing::ConstructFromParams clamps to 4, CThingAICreature::ConstructFromParams -> DefaultOwner).
    // Markers, spawners, fishing spots, track nodes, emitters keep their fixed values (no owner there).
    int placeOwner_ = -1;
    int ownerFor(const std::string& definition) const;
    void applyOwnerToSelection();    // vanilla O: PaintInputSetNearestThingOwnershipToSelectedOwner (Auto -> 4)
    // vanilla Quests dialog "Day only" / "Night only": show the %DayOnly / %NightOnly sections of the
    // shown quests. Vanilla starts them off (a whitelist dialog); FableForge shows everything by default.
    bool showDayOnly_ = true, showNightOnly_ = true;
    bool thingHiddenBySection(const std::vector<std::string>& perThingSection, size_t thing) const;
    static bool isOwnerType(const std::string& thingType);
    float placeHeight_ = 0.0f;
    uint32_t placeSeed_ = 0;         // GFFloatRandom state for random angles (vanilla uses the world seed)
    // link pick mode: the next viewport click on a thing sets this link of the selection
    struct LinkPick { std::string ctc, field, label; bool active = false; } linkPick_;
    struct AttachPick {
        bool active = false;
        uint64_t anchorUid = 0;
        editor::Document::AttachMode mode = editor::Document::AttachMode::Owned;
        std::string caption;
    } attachPick_;
    // quest sections: hidden ones (lower-case names) are not drawn or pickable
    std::set<std::string> hiddenSections_;
    char newSection_[64] = {};
    uint64_t sectionsAppliedRev_ = ~0ull;
    size_t sectionsAppliedInstances_ = 0;
    bool sectionsDirty_ = true;
    void drawSectionsCard(float pad, float inner, float cardInner);
    void drawPropertyGrid(float cardInner);
    void drawListProperties(float cardInner);
    char listPropertySearch_[96] = {};
    void drawMissingComponentProperties(float cardInner);
    char componentOverrideValue_[128] = {};
    void drawSelectionInspector();
    void drawSelectionActions(const ImVec2& origin, const ImVec2& size);
    bool pickContextSelection(float u, float v);
    bool selectionInspectorOpen_ = false;
    bool selectionPopupRequested_ = false, selectionPopupOpen_ = false;
    // Assets tab (was Textures): 0 textures, 1 models, 2 ground themes -- the tools that
    // write the game's shared banks, kept out of the map editor
    int assetsTab_ = 0;
    std::string customDonor_;
    void drawModelImportCard(float pad, float inner, float cardInner);
    // where an Assets import goes: a FableForge mod pack of the load order (a recipe the
    // composer applies at deploy) or, "" = directly into the game's banks (the old way)
    std::string packDest_;
    bool packDestChosen_ = false;
    char newPackName_[64] = {};
    void drawPackDestination(float cardInner, bool allowDirect = true);
    std::vector<std::pair<std::string, std::string>> packChoices();   // (label, folder) of the FableForge packs in the order
    std::string packLabel(const std::string& folder);
    void drawPackPicker(float width);   // the compact "Writes go into" combo (edit footer)
public:
    void setPackDest(const std::string& folder) { packDest_ = folder; packDestChosen_ = true; }
private:
    bool addToPackOrGame(bool model);
    void drawGroundThemeCard(float pad, float inner, float cardInner);
public:
    void setAssetsTab(int t) { assetsTab_ = std::clamp(t, 0, 4); }
    bool standaloneAssetPreview() const { return texturesMode_ && (assetsTab_ == 1 || assetsTab_ == 3 || assetsTab_ == 4); }
private:
    struct PendingColour { std::string id; float rgba[4] = {}; bool live = false; } pendingColour_;   // a light colour being edited
    void applySectionVisibility();
    void drawLinkLines(const ImVec2& origin, const ImVec2& size);
    struct RadiusField { std::string label; float radius; ImU32 colour; };
    std::vector<RadiusField> selectedRadiusFields() const;
    void drawRadiusRings(const ImVec2& origin, const ImVec2& size);
    // tracks (Level tab): the card, the pick-a-node-to-link mode, the lines in the view
    void drawTracksCard(float pad, float inner, float cardInner);
    // the invalid-thing check (vanilla V): cached per revision
    std::vector<editor::Document::Issue> validateMap();
    void drawCheckCard(float pad, float inner, float cardInner);
    void showFirstInvalid();
    std::vector<editor::Document::Issue> issuesCache_;
    uint64_t issuesRev_ = ~0ull;
    std::set<std::string> familyNames_;   // CREATURE_GENERATION_FAMILY defs, once per context
    void drawTrackLines(const ImVec2& origin, const ImVec2& size);
    struct ThingGlyph { int thing; ImVec2 screen; ImU32 colour; const char* role; char symbol; };
    std::vector<ThingGlyph> thingGlyphs_;
    size_t suppressedThingGlyphs_ = 0;
    void refreshThingGlyphs(const ImVec2& origin, const ImVec2& size);
    void drawThingGlyphs();
    int glyphThingAt(float px, float py) const;
    bool trackLinkPick_ = false;
    int trackNodeAt(float px, float py) const;   // a drawn track node near a screen point, -1 = none
    // per-revision caches (the cards redraw every frame; the document scans are O(things))
    const std::vector<editor::Document::Track>& cachedTracks();
    std::vector<editor::Document::Track> tracksCache_;
    uint64_t tracksCacheRev_ = ~0ull;
    std::vector<std::pair<std::string, std::string>> envDefs_, soundDefs_;   // ENVIRONMENT_THEME_DAY / SOUND_THEME, once per context
    bool thingsStale() const;
    uint64_t sectionsCardRev_ = ~0ull;
    std::vector<std::string> sectionNamesCache_;
    std::map<std::string, size_t> sectionCountsCache_;   // renderer instances lag the document (a reload is pending)
    char trackName_[64] = {};
    std::string thingLabel(size_t index) const;
    std::map<std::string, std::string> themeGroupOf_;   // ENGINE_THEME -> its ENGINE_THEME_GROUP (the vanilla Themes lists)
    void drawDefPalette(const char* id, const std::vector<std::string>& types, float width, float height);
    char thingSearch_[64] = {};
    int thingsKindFilter_ = 0;   // all, ordinary things, markers
    // forge_mods_provenance.json (written by a mod deploy): "uid:<n>" -> mod for the open map;
    // badges in the object list, an origin filter, "back to retail" = a vanilla pick
    std::map<std::string, std::string> thingOrigin_;
    std::vector<std::string> originMods_;
    std::string originFilter_;   // "" = every object, "retail", or a mod name
    void loadThingOrigins();
    const char* originOf(uint64_t uid) const;
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
    // 0 raise, 1 lower, 2 flatten, 3 smooth, 4 walkable, 5 blocked, 6 paint theme,
    // 7 replace theme (pen), 8 flood replace (click), 9 draw path (drag start -> end),
    // 10 paint environment (atmos), 11 paint sound -- the .lev's 4x4 game-map grid
    int terrainMode_ = 0;
    int paintTheme_ = 0;             // LEV palette slot painted (6) / put in (7, 8)
    int replaceFrom_ = -1;           // LEV palette slot taken out (7, 8); Ctrl+Shift+click samples it
    int envSlot_ = 0;                // 10: atmos palette slot painted (0 = the "no environment" slot)
    int soundIndex_ = 0;             // 11: sound list index painted (0 = none)
    char envSearch_[64] = {};
    forge::fractal::Params fractal_;     // the Fractals card (vanilla dialog defaults)
    // 14 copy region (drag), 15 paste region (click; R rotates): vanilla Copy and paste
    editor::TerrainClip terrainClip_;    // survives map changes: copy on one map, paste on another
    int clipTurns_ = 0;
    bool clipHeights_ = true, clipThemes_ = true, clipRelative_ = true;
    bool clipThings_ = true;             // vanilla "Copy things": the things inside the rectangle ride along
    // ---- brush library (vanilla Brush library dialog): saved copies, one .brush.json per brush
    char brushName_[64] = {};
    std::vector<std::string> brushList_;
    bool brushListDirty_ = true;
    std::filesystem::path brushDir() const;
    void drawBrushLibrary(float cardInner);
public:
    bool saveBrush(const std::string& name);   // the current copy -> <brushDir>/<name>.brush.json
    bool loadBrush(const std::string& name);   // a saved brush -> the copy clipboard, paste mode on
    bool copyRegion(int x0, int y0, int x1, int y1);            // automation: mode 14's drag
    size_t pasteRegion(int x, int y);                           // automation: mode 15's click
private:
    size_t deleteRegionThings();
    bool clipDrag_ = false;
    float clipStart_[2] = {0, 0};
    int clipRect_[4] = {0, 0, 0, 0};
    bool clipRectValid_ = false;
    std::string clipRectMap_;
    void drawGroundRect(const ImVec2& origin, const ImVec2& size, float x0, float y0, float x1, float y1, ImU32 col);
    // Fit to neighbours (the vanilla world map's Fit Neighbours, forge/fillerfit.hpp): the
    // touching maps' heights, read once per map on a worker; two preview images (now / fitted)
    bool fitOpen_ = false, fitFineTune_ = false;
    forge::fillerfit::Params fitParams_;
    std::vector<forge::fillerfit::Neighbour> fitNeighbours_;
    std::string fitNeighboursFor_, fitNeighboursNote_;
    std::future<std::pair<std::vector<forge::fillerfit::Neighbour>, std::string>> fitFuture_;
    ID3D11ShaderResourceView* fitPreviewNow_ = nullptr;     // owned by the renderer (uiTexture "fit_now" / "fit_after")
    ID3D11ShaderResourceView* fitPreviewAfter_ = nullptr;
    std::string fitPreviewKey_;
    forge::fillerfit::Report fitReport_;
    bool fitHasResult_ = false;
    size_t fitPreviewChanged_ = 0;
    float fitPreviewMaxDelta_ = 0.0f;
    int fitScrollTo_ = 0;        // frames left to bring the opened card into view (the content height lags a frame)
    void startFitNeighbourLoad();
    void drawFitCard(float pad, float inner, float cardInner);   // the sidebar entry: a button that opens the window
    void drawFitWindow();
    void drawFractalWindow();
    // Floating tool windows: the bigger, occasional tools (previews, many fields) open
    // beside the view instead of stretching the sidebar. Non-modal (the view stays
    // live), centred over the viewport the first time and left where the user drags it,
    // a title + subtitle header with a close button, Esc closes the focused one.
    bool beginToolWindow(const char* id, const char* title, const char* subtitle, bool* open, float width);
    void endToolWindow();
    void drawToolWindows();
    float toolWindowInner_ = 0;   // the content width of the tool window being drawn
public:
    void setFitOpen(bool on) { fitOpen_ = on; fitScrollTo_ = on ? 3 : 0; }
    void setFractalOpen(bool on) { fractalOpen_ = on; }
    void setBudgetOpen(bool on) { budgetOpen_ = on; budgetDirty_ = on; }
    void runBudgetSurvey();   // the budget survey window's numbers for the current options
    const forge::budget::Report& budgetReport() const { return budgetReport_; }
    bool fitBusy() const { return fitFuture_.valid(); }
    size_t fitApply();   // fit the open map to its neighbours (automation / the button)
private:
    bool fractalOpen_ = false;
    // ---- budget survey (vanilla Surveys > Engine, inventory 11b): what an area costs the renderer
    struct LocalDetailItem { float x = 0, y = 0; uint32_t mesh = 0; std::string name; };
    std::vector<LocalDetailItem> localDetail_;   // the open map's baked plants, kept from the foliage load
    bool budgetOpen_ = false;
    bool budgetDirty_ = false;       // options changed: re-run next frame
    int budgetScope_ = 0;            // 0 whole map, 1 the selection, 2 around the view centre
    float budgetRadius_ = 20.0f;
    unsigned budgetInclude_ = forge::budget::kAll;
    bool budgetAllDuplicates_ = false;
    int budgetView_ = 0;             // breakdown: 0 definitions, 1 meshes, 2 textures
    bool budgetHasReport_ = false;
    forge::budget::Report budgetReport_;
    std::string budgetSaved_;        // where the last report was written
    void drawBudgetWindow();
    void drawBudgetCard(float pad, float inner, float cardInner);
    ID3D11ShaderResourceView* fractalPreview_ = nullptr;   // owned by the renderer (uiTexture "fractal")
    std::string fractalPreviewKey_;                         // params + map it was drawn for
    float fractalPreviewMin_ = 0.0f, fractalPreviewMax_ = 0.0f;
    bool pathDrag_ = false;          // mode 9: LMB down, start fixed at pathStart_
    float pathStart_[2] = {0, 0};
    void paletteCombo(const char* id, int& slot, float width);
    uint64_t syncedThemeRev_ = 0;
    bool rebakePending_ = false;     // a theme stroke ended: re-bake the ground albedo from the LEV
    void startThemeRebake();
    float brushRadius_ = 6.0f;
    float brushStrength_ = 4.0f;
    // the vanilla Height Toolbox pens (forge/heightpen), see TerrainBrush
    bool penExactStep_ = false;        // Raise / Lower: vanilla Change Height
    float penStep_ = 1.0f;
    bool penTargetFromStroke_ = true;  // Flatten: take the ground where the stroke starts
    float penTarget_ = 0.0f;           // Flatten: vanilla Paint Height "m" box (Ctrl+click samples it)
    float penSpeed_ = 0.5f;
    float penSmoothness_ = 0.5f;       // vanilla starts 0 (a no-op pen); 50% is a usable default
    float penSpikyness_ = 0.0f;
    float penMagnifier_ = 1.0f;
    bool penSpray_ = true;             // vanilla Spray can: repeat while held (off = once per click)
    int activityOpen_ = -1;   // -1 = follow available height until the user chooses folded (0) or open (1)
    bool thingsScriptOnly_ = false;    // Objects list: vanilla Scene Browser "Only ScriptNamed Objects"
    bool thingsNearest_ = false;       // Objects list: vanilla "Sort by distance" (from the camera)
    int thingsFirst_ = -1;             // the Objects list's first row (automation state)
    size_t thingsShown_ = 0;
    bool penApplied_ = false;          // this stroke has had its one application (spray off)
    bool keyStroke_ = false;           // a -/= stroke (vanilla height keys) is running
    void fillPen(editor::TerrainBrush& b) const;
    void drawPenControls(float cardInner);
    void drawViewportEdges();
    bool isVanillaPen(int mode) const { return mode == 2 || mode == 3 || mode == 16 || ((mode == 0 || mode == 1) && penExactStep_); }
    bool brushHit_ = false;
    float brushFable_[2] = {0, 0};   // map-local x/y under the cursor
    uint64_t syncedTerrainRev_ = 0;
    struct TerrainDeployResult { bool ok = false; std::string error, pack; std::vector<std::string> notes; std::shared_ptr<editor::Document> written; };
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
    struct NewLevelJob { bool ok = false; std::string error; std::string name, pack; bool ownRegion = false; editor::NewLevelResult result; };
    std::future<NewLevelJob> newLevelFuture_;
    void drawNewLevelCard(float pad, float inner, float cardInner);
    // enemy spawner card: CREATURE_GENERATION_FAMILY picker + radius/limit, placed at the view centre
    void drawSpawnerCard(float pad, float inner, float cardInner);
    // fishing spot card: MARKER_FISHING_SPOT at the view centre, optional OBJECT_* first catch
    void drawFishingSpotCard(float pad, float inner, float cardInner);
    char fishingReward_[64] = {};
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
    // The one running-game check every writer uses: the live-link heartbeat when the
    // link is installed, and a Fable.exe process scan always (the engine holds the WAD,
    // STB, textures.big and defs open; rewriting them underneath it crashes it). Logs
    // "<what>: the game is running ..." and returns true when the write must not happen.
    bool gameWriteBlocked(const char* what);
    const char* activeFileJob() const;
    bool fileWriteBlocked(const char* what);
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
    // scripted: place a fishing spot at the view centre (reward = OBJECT_* def or empty)
    bool placeFishingSpot(const std::string& reward, const std::string& scriptName = "");
private:
    void startNewLevel();
    void setNewLevelOwnRegion(bool on) { newLevelOwnRegion_ = on; }
    void setNewLevelDedicated(bool on) { newLevelDedicated_ = on; }
    void setNewLevelBlank(int theme, float height, int w = 0, int h = 0) { newLevelMode_ = 1; blankTheme_ = theme; blankHeight_ = height; if (w > 0) selectBlankSize(w, h); }
    void selectBlankSize(int w, int h);
    void setNewLevel(const std::string& name, int x, int y, const std::string& region) { std::snprintf(newLevelName_, sizeof newLevelName_, "%s", name.c_str()); newLevelX_ = x; newLevelY_ = y; newLevelRegion_ = region; }
    bool newLevelBusy() const { return newLevelFuture_.valid(); }
    bool confirmTerrainDeploy_ = false;
    bool confirmWriteBoth_ = false;
    bool confirmPending() const { return confirmDeploy_ || confirmTerrainDeploy_ || confirmWriteBoth_ || confirmWorldApply_ || confirmRestore_; }
    bool writeObjectsAfterTerrain_ = false;   // set by startWriteBoth until the terrain job finishes
    std::string writeBothMap_, writeBothPack_;
    void finishWriteBoth(bool terrainOk);

    // ---- overworld (gui/world.cpp): every map's box on a 2D grid, drag to move,
    // pending moves applied to the install in one go (WLD/BWD/STB)
public:
    void setWorldMode(bool on);
    // ---- Textures tab (gui/textures.cpp, 0.17): browse / preview / export / replace / add
    void setTexturesMode(bool on);
    bool texturesMode() const { return texturesMode_; }
    void refreshTextures();
    std::filesystem::path texturesBigPath() const;
    std::filesystem::path graphicsBigPath() const;
    bool selectTexture(const std::string& nameOrLabel);
    const texbrowse::TextureRow* selectedTexture() const;
    bool exportSelectedTexture(const std::string& outPath);
    bool replaceSelectedTexture(const std::string& image);
    bool addTexture(const std::string& name, const std::string& image, const std::string& bank, const std::string& format);
    std::vector<uint32_t> selectedThingTextures() const;
    void drawTexturesPanel(float pad, float inner, float cardInner);
    void refreshModels();
    bool selectModel(const std::string& nameOrId);
    void drawModelBrowser(float pad, float inner, float cardInner);
    void drawModelViewport(const ImVec2& origin, const ImVec2& size);
    struct ModelRow { uint32_t id, type, bytes; std::string name; };
    std::vector<ModelRow> modelRows_;
    std::map<uint32_t, std::vector<std::string>> modelUsers_;
    bool modelsLoaded_ = false, modelUsersLoaded_ = false, modelReady_ = false, modelImportOpen_ = false;
    std::string modelRoot_, modelBankPath_, modelName_, modelError_;
    uint32_t modelId_ = 0;
    forge::meshpreview::Geometry modelGeometry_;
    char modelSearch_[96] = {};
    size_t modelFiltered_ = 0;
    float modelYaw_ = 0.8f, modelPitch_ = 0.55f, modelZoom_ = 1.0f;
    bool modelWire_ = false;
    // ---- Read-only Effects browser (gui/effectbrowser.cpp).
    struct EffectBrowserRow { uint32_t id = 0; std::string name, displayName; };
    std::vector<EffectBrowserRow> effectBrowserRows_;
    effects::Effect effectBrowserSelection_;
    bool effectBrowserLoaded_ = false, effectBrowserReady_ = false;
    char effectBrowserSearch_[96] = {};
    size_t effectBrowserFiltered_ = 0;
    std::string effectBrowserError_;
    struct EffectThumbnail { bool attempted = false; ID3D11ShaderResourceView* image = nullptr; std::string error; };
    std::vector<EffectThumbnail> effectBrowserThumbnails_;
    void frameEffectPreview(bool currentOnly=false);
    particlepreview::Simulation effectSimulation_;
    ParticlePreviewRenderer effectRenderer_;
    Camera effectCamera_;
    float effectBackground_[3]={.025f,.035f,.05f};
    bool effectShowGrid_=false;
    bool effectShowLightVolumes_=true;
    size_t effectLightVolumesDrawn_=0;
    bool effectPlaying_ = true, effectTexturesReady_ = false, effectRendererReady_ = false, effectMeshesReady_ = false;
    bool effectLoop_=true;
    bool effectAutoDuration_=true;
    float effectDuration_=10.0f;
    int effectSpeedIndex_=2;
    size_t effectLoopCount_=0;
    void advanceEffectPlayback(double seconds);
    void fitEffectDuration();
    std::vector<std::string> effectTextureWarnings_;
    void refreshEffectBrowser();
    bool selectEffect(const std::string& nameOrId);
    void drawEffectBrowser(float pad, float inner, float cardInner);
    void drawEffectViewport(const ImVec2& origin, const ImVec2& size);
    // Dialogue and lip sync preview/editor (gui/dialoguebrowser.cpp).
    void drawDialogueBrowser(float pad, float inner, float cardInner);
    enum class PathField { ModSource, Image, Png, Model, DialogueExport };
    bool drawPathInput(const char* id, const char* hint, char* value, size_t capacity,
                       float width, PathField kind, const char* widget);
    void drawDialogueViewport(const ImVec2& origin, const ImVec2& size);
    void drawDialogueTools(float pad, float inner, float cardInner);
    bool dialogueToolsOpen_ = false;
    void setDialogueEditing(bool editing);
    bool dialogueTracksOpen_ = false;
    void frameDialoguePlayback();
    std::string dialogueLanguage_ = "English";
    int dialogueBank_ = 0, dialogueId_ = 1;
    bool dialogueLoaded_ = false;
    forge::lipsync::Entry dialogueEntry_;
    forge::lipsync::Entry dialogueOriginalEntry_;
    using DialogueKey = std::tuple<std::string,std::string,uint32_t>;
    DialogueKey dialogueLoadedKey_;
    std::map<DialogueKey,forge::lipsync::Entry> dialogueStaged_;
    struct DialogueEditSnapshot {
        forge::lipsync::Entry entry;
        float time = 0;
        bool staged = false;
    };
    struct DialogueEditHistory {
        forge::lipsync::Entry original;
        std::vector<DialogueEditSnapshot> undo, redo;
    };
    std::map<DialogueKey,DialogueEditHistory> dialogueEditHistory_;
    std::optional<DialogueEditSnapshot> dialogueEditGesture_;
    DialogueEditSnapshot dialogueEditSnapshot() const;
    void rememberDialogueEdit(DialogueEditSnapshot before);
    void finishDialogueEditGesture();
    bool undoDialogueEdit(bool redo = false);
    std::array<char,512> dialogueScratchPath_{};
    std::string dialogueScratchLanguage_, dialogueExportMessage_;
    double dialogueAudioDuration_ = 0;
    float dialogueTime_ = 0;
    bool dialogueLoop_ = false;
    bool dialogueMotionPlaying_ = false;
    bool dialogueAudioMuted_ = false;
    std::unique_ptr<DialogueAudioPlayer> dialogueAudio_;
    std::string dialogueError_;
    std::string dialogueTextRoot_, dialogueTextError_;
    std::unique_ptr<forge::dialoguetext::Index> dialogueTextIndex_;
    std::vector<forge::dialoguetext::Line> dialogueSubtitles_;
    std::array<char,160> dialogueSearchQuery_{};
    std::string dialogueSearchCacheKey_;
    std::vector<forge::dialoguetext::Match> dialogueSearchResults_;
    std::map<std::string,std::vector<size_t>> dialogueSearchGroups_;
    int dialoguePreset_ = 0;
    bool dialoguePresetChecked_ = false;
    forge::lipsync::PresetAssets dialoguePresetAssets_;
    std::string dialoguePresetError_;
    forge::meshpreview::Geometry dialogueHeadGeometry_;
    foliageexport::Mesh dialogueHeadMesh_;
    std::map<std::string,forge::animation::Animation> dialogueHeadAnimations_;
    bool dialogueHeadReady_ = false;
    float dialogueHeadLastTime_ = -1, dialogueHeadYaw_ = 0;
    float dialogueHeadPitch_ = 0.15f, dialogueHeadZoom_ = 0.8f;
    bool dialogueHeadWire_ = false;
    // the Mods tab (gui/mods.cpp): the save root's load order + forge-tools.exe for deploy/undeploy/conflicts
    void setModsMode(bool on);
    bool modsMode() const { return modsMode_; }
    void refreshModOrder();
    bool modAdd(const std::string& source, const std::string& name);
    bool modRemove(const std::string& nameOrIndex);
    bool modMove(const std::string& nameOrIndex, int to);
    bool modEnable(const std::string& nameOrIndex, bool on);
    bool runModsTool(const std::string& verb);   // "deploy" | "undeploy" | "conflicts"
    bool modsBusy() const { return modsFuture_.valid() || modsRefreshPending_ || !modsQueuedVerb_.empty(); }
    bool modFilesBusy() const { return modsRefreshPending_ || !modsQueuedVerb_.empty() || (modsFuture_.valid() && modsVerb_ != "conflicts"); }
    void pollModsTool();
    void drawModsPanel(float pad, float inner, float cardInner);
    size_t modCount() const { return modOrder_.mods.size(); }
    // masters: `mod` (name or index) requires / stops requiring `master` (forge_pack.json)
    bool modSetRequires(const std::string& mod, const std::string& master, bool on);
    std::string modProblems() const;   // every row's master problems, "; "-joined (automation)
private:
    bool modsMode_ = false;
    bool modsRefreshPending_ = false;
    std::string modsQueuedVerb_, modsQueuedCommand_;
    bool modReadersBusy() const;
    void launchModsCommand(const std::string& command);
    void refreshAfterMods();
    void resetModDestination();
    forge::modorder::Order modOrder_;
    std::string modOrderError_;
    // per order row: a FableForge pack's folder / name / masters and what is wrong with them
    struct ModRowInfo { std::string packFolder, packName; std::vector<std::string> masters, problems; };
    std::vector<ModRowInfo> modRows_;
    char modAddPath_[512] = {};
    char modAddName_[128] = {};
    struct ModsToolResult { std::vector<std::string> lines; int rc = 0; };
    std::future<ModsToolResult> modsFuture_;
    std::string modsVerb_;
    // the conflict report of the last Check conflicts (forge-tools mods conflicts --json) and the
    // picks the user made on it (forge_mods_picks.txt next to forge_mods.json; deploy reads it)
public:
    struct ModConflict { std::string kind, key, label; std::vector<std::string> mods;
                         std::string winner; bool overridden = false, pickable = true, fieldMerged = false; };
    std::string modConflictWinner(const ModConflict& conflict) const;
private:
    std::vector<ModConflict> modConflicts_;
    std::map<std::string, std::string> modPicks_;
    bool modReportLoaded_ = false;
    std::string modReportSummary_;
    struct ModMissingMesh { std::string definition, type; uint32_t meshId = 0; };
    std::vector<ModMissingMesh> modNewMissingMeshes_;
    size_t modMissingMeshTotal_ = 0, modMissingMeshBaseline_ = 0;
    size_t modAssetUnparsed_ = 0;
    std::string modAssetStatus_, modAssetError_;
    void loadModPicks();
    bool saveModPicks(const std::map<std::string, std::string>& picks);
public:
    size_t modConflictCount() const { return modConflicts_.size(); }
    // pick a winner for a conflict (by its key, or the first row when key is "*"); "-" = back to load order
    bool modPick(const std::string& key, const std::string& winner);
    // set or clear ("-") one pick directly (no report needed): the editor's "back to retail"
    bool setModPick(const std::string& key, const std::string& winner);
private:
    bool texturesMode_ = false;
    bool texturesLoaded_ = false;
    std::vector<texbrowse::TextureRow> texRows_;
    std::vector<std::string> texBanks_;
    std::string texBank_, texSelected_, texPreviewFor_, lastTexturePng_;
    ID3D11ShaderResourceView* texPreview_ = nullptr;
    ImVec2 texPreviewSize_ = ImVec2(1, 1);
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
    // move / owner change / visibility toggle / revert; a write clears only the
    // history it saved, retaining edits accepted while that write was running
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
    void loadWorld(bool preserveDraft = false);
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
    using WorldSnap = editor::WorldDraft;
    std::vector<WorldSnap> worldUndo_, worldRedo_;
    uint64_t worldUndoSerial_ = 0;
    WorldSnap worldSnapshot() const { return WorldSnap{worldPending_, worldOwnerEdits_, worldSeesEdits_}; }
    void worldRestore(const WorldSnap& s);
    void worldPushUndo();
    void acceptWorldWrite(const WorldSnap& submitted, uint64_t undoSerial);
    std::string worldSelected_;
    std::string worldHover_;
    void drawWorldLabel(const ImVec2& origin, const ImVec2& size);
    // ---- the whole-world overview (gui/worldview.cpp): map tiles for the textured 2D map and the 3D view
    std::map<std::string, worldtiles::Tile> worldTiles_;
    std::map<std::string, ID3D11ShaderResourceView*> worldTileTex_;   // owned by the renderer (uiTexture)
    std::mutex worldTileMutex_;
    std::vector<worldtiles::Tile> worldTileDone_;   // finished on the workers, collected on the UI thread
    std::vector<std::future<void>> worldTileWorkers_;
    std::shared_ptr<std::atomic<bool>> worldTileCancel_;
    std::chrono::steady_clock::time_point worldTileStarted_;
    size_t worldTileTotal_ = 0;
    int worldOverviewBatchLimit_ = 32; // count guard; each upload phase still has a 2 ms budget
    size_t worldOverviewUploadFrames_ = 0, worldThumbnailUploadFrames_ = 0;
    std::string worldOverviewFirstMap_;
    std::string worldTilesFor_;
    std::map<std::string, std::pair<int, int>> worldLayerAt_;   // tiles in the 3D layer, at which origin
    bool world3D_ = false;
    bool worldTerrain2D_ = true;       // draw the tiles inside the 2D boxes
    Camera worldCamera_;
    bool worldCameraSet_ = false, worldCaptured_ = false, worldClickArmed_ = false;
    ImVec2 worldClickPos_;
    void startWorldTiles();
    void stopWorldTiles();
    void pollWorldTiles();
    void setWorld3D(bool on);
    void drawWorld3D(const ImVec2& origin, const ImVec2& size);
    void openFromWorld3D(const std::string& name);
    float worldGroundAt(float wx, float wy, bool& inside, std::string* name) const;
    bool worldPickTile(const float o[3], const float d[3], std::string& name, float hit[3]) const;
    // Detail around the camera in the 3D world view: the nearest maps get their full terrain, foliage,
    // things and creatures (streamed one map at a time, dropped when far), their low-res tile hidden.
    struct WorldDetail {
        std::string name;
        std::vector<Renderer::PreparedBatch> batches;
        std::map<int, std::shared_ptr<const cutoutmips::Chain>> cutoutMips;
        cutoutmips::Cache::Stats cutoutCacheStats;
        std::vector<terrainexport::Image> images;
        uint64_t generation = 0;
        int objects = 0, creatures = 0;
        std::string error;
        bool cancelled = false;
    };
    uint64_t worldDetailGeneration_ = 0;
    struct WorldDetailWork {
        std::atomic<bool> cancel{false}, hold{false}, held{false};
    };
    std::shared_ptr<WorldDetailWork> worldDetailWork_;
    std::string worldDetailHoldPrepare_; // automation: pause after terrain preparation
    size_t worldDetailCancelled_ = 0;
    std::shared_ptr<cutoutmips::Cache> worldCutoutCache_ = std::make_shared<cutoutmips::Cache>();
    cutoutmips::Cache::Stats worldCutoutCacheStats_;
    bool worldCutoutCacheOn_ = true;
    bool worldDetailOn_ = true;
    bool worldSmoothObjects_ = true; // automation A/B diagnostic
    bool worldDetailFoliage_ = true, worldDetailThings_ = true, worldDetailCreatures_ = true;
    float worldDetailRadius_ = 250.0f;   // world units around the camera's ground point
    int worldDetailMaps_ = 6;            // at most this many maps in full detail
    bool worldAutoDetail_ = true;
    WorldScenery worldScenery_;
    bool worldSceneryOn_ = true; // automation can isolate the near-detail path
    size_t worldSceneryHolds_ = 0;
    int worldDetailAutoMaps_ = 24;
    int worldDetailMemoryMaps_ = 6;
    uint64_t worldDetailLargestMap_ = 0;
    worldview::DetailBudget worldDetailBudget_;
    std::future<WorldDetail> worldDetailFuture_;
    std::optional<WorldDetail> worldDetailUpload_;
    DeferredRelease<WorldDetail> worldDetailRelease_;
    bool worldDetailRetiring_ = false;
    size_t worldDetailUploadAt_ = 0;
    size_t worldDetailUploadFrames_ = 0;
    int worldDetailUploadBudgetMs_ = 0; // automation: 0 adaptive, 2/4 fixed comparison
    struct WorldDetailVisibility { float fade = 0; bool wanted = true; };
    std::map<std::string, WorldDetailVisibility> worldDetailShown_;   // fully uploaded maps, including transitions
    worldview::DetailCache worldDetailCache_;
    worldview::CacheMemoryBudget worldCacheMemoryBudget_;
    std::optional<float> worldDetailFadeOverride_; // automation: isolate transition endpoints
    Renderer::VideoMemoryInfo worldVideoMemory_;
    std::optional<Renderer::VideoMemoryInfo> worldVideoMemoryOverride_; // automation only
    double worldVideoMemoryNext_ = 0;
    size_t worldMemoryEvictions_ = 0;
    size_t worldDetailCacheHits_ = 0, worldDetailLoads_ = 0;
    int worldDetailLastObjects_ = 0, worldDetailLastCreatures_ = 0;
    worldview::DetailRetry worldDetailRetries_;
    size_t worldDetailDeferred_ = 0, worldDetailFailures_ = 0;
    std::string worldDetailFailPrepare_, worldDetailFailUpload_; // automation injection
    void failWorldDetail(const std::string& name, const std::string& reason);
    std::string worldDetailLoading_;
    size_t worldDetailWanting_ = 0;   // wanted maps not in the layer yet (automation waits on 0)
    double worldDetailNext_ = 0;
    int worldTag(const std::string& name) const;
    void updateWorldDetail();
    void updateWorldScenery();
    void clearWorldDetail();
    bool releaseWorldDetail();
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
    struct WorldJob { bool ok = false; std::string error, pack; std::vector<std::string> notes; WorldSnap submitted; uint64_t undoSerial = 0; };
    std::future<WorldJob> worldFuture_;
    void terrainInput(const ImVec2& origin, const ImVec2& size);
    void drawBrushCursor(const ImVec2& origin, const ImVec2& size);
    void syncTerrain();
    void startTerrainDeploy();
    bool clickArmed_ = false;
    ImVec2 clickPos_;
    bool contextClickArmed_ = false, contextClickMoved_ = false;
    ImVec2 contextClickPos_;
    std::string pendingSelect_;      // map switch held back by the unsaved-changes prompt
    bool closePending_ = false;      // window close held back by unsaved work
    bool closeSaveWaiting_ = false;  // a World write started from the close prompt
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
    void drawCompass(const ImVec2& origin, const ImVec2& size, float yaw, float bottomInset);
    float viewportControlsLift() const;
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
    // the vanilla Tracks dialog's Preview Track (CEditControlCentre::PreviewCameraTrack): the camera rides
    // one track and looks at a point riding another, linearly by arc length, then snaps back
    struct TrackPreview {
        bool active = false;
        std::vector<std::array<float, 3>> eye, look;   // node positions, map-local Fable
        float eyeLength = 0, lookLength = 0, seconds = 10, u = 0;
        Camera saved;
    } trackPreview_;
    int previewEyeTrack_ = 0, previewLookTrack_ = 1;
    float previewSeconds_ = 10.0f;
    bool startTrackPreview(int eyeTrack, int lookTrack, float seconds);
    void updateTrackPreview(float dt);
    void stopTrackPreview();
    ViewMode mode_ = ViewMode::Textured;
    float time_ = 0;
    bool quit_ = false;

    // install
    std::string installPath_;
    std::string installSource_;
    bool installValid_ = false;
    forge::levelstore::Layout levels_;   // WAD or loose levels; decides where level writes go
    // level writes land in loose files, not FinalAlbion.wad (a loose-level install); a
    // redirected save root is judged by Document::deployWad itself
    // the deploy is the loose file: a loose-level install, or a map of another world
    bool writesLoose() const { return (saveRoot_.empty() || saveRoot_ == installPath_) && (levels_.looseOnly() || doc_.external()); }
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
    // Viewport ground albedo density: the engine tiles 256 px theme textures every 8
    // units (32 texels per cell); 4 per cell read as the lowest mip. As dense as a
    // 4096 px texture allows, 4..16 per cell (HookCoast, the largest, bakes in ~1 s).
    static int previewTexelsFor(int cellsX, int cellsY) {
        const int longest = std::max(std::max(cellsX, cellsY), 1);
        return std::clamp(4096 / longest, 4, 16);
    }
    struct FoliageResult { std::string name; foliageexport::Scene scene; foliageexport::Scene things; thingsexport::Stats thingStats; bool thingsOnly = false, foliageOnly = false; };
    bool previewThings_ = true;
    bool showThingGlyphs_ = true;
    size_t thingInstances_ = 0;
    std::future<FoliageResult> foliageFuture_;
    // the maps touching the open one, low-res and textured at their WLD offsets (layer 2):
    // a first step to the vanilla editor's whole-world view
    struct NeighbourResult { std::string name; foliageexport::Scene scene; int maps = 0; std::string note; };
    bool showNeighbours_ = false;
    std::string neighboursFor_;
    std::future<NeighbourResult> neighbourFuture_;
    void startNeighbourLoad();
    std::string foliageLoadedFor_;
    uint64_t foliageTerrainRev_ = 0;
    size_t foliagePreviewReseated_ = 0;
    std::string foliagePendingName_;
    bool previewFoliage_ = true;
    size_t foliageInstances_ = 0;
    std::string foliageStatus_;
    std::set<std::string> reportedThingWarnings_; // avoid repeating the same import warning after every terrain stroke

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
