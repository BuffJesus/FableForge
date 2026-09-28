# Vanilla (Lionhead) level editor inventory

2026-09-27. A static inventory of the in-engine level editor in the Lionhead dev
build `FableWin.exe`. Nothing was launched. This extends
`DEBUG_EDITOR_FEATURE_AUDIT.md` and does not repeat it: that audit already covers fractal
application maths, the preferred-path and camera-passability cell flags, the
empty minimap-zone writer, minimap building silhouettes, `FindInvalidThing` and
`CEditLevelMerger`. This document maps the **whole editor surface**, including
modes, toolbar, menus, dialogs, hotkeys and console commands. It then answers a
modder's parity checklist item by item.

## Evidence base and confidence

- Executable: `D:/Documents/FableTLC/debug_build/FableWin.exe`. It is byte-identical to
  `D:/tmp/fablewin_editor/Fable_Anniversary-2013-02-25/Fable/FableWin.exe` (checked with `cmp`).
  **All addresses are FableWin VAs** (ImageBase 0x400000). They are not retail addresses.
- **PDB names:** `ghidra_out/fablewin_pdb_names.tsv`. **Enums:** `ghidra_out/fablewin_pdb.xml`,
  where data symbol addresses are RVAs.
- **Strings:** `strings -n 4` for ASCII control and window names, and `strings -e l` for UTF-16
  UI captions. The captions are the text the user actually sees.
- **Bodies:** fresh headless decompiles made with `tools/ghidra_scripts/DecompAt.java`
  (`-readOnly -noanalysis`, project FableTLC). Hotkey switches were read with Capstone
  disassembly of the jump tables and the pushed `EInputKey`/`EEditMode` immediates.
  Callers come from an E8/E9 xref scan of `.text`. These scratch outputs are not durable.
  The addresses below are enough to regenerate them.

Confidence labels:
- **H (high):** an inspected body or data table establishes the behaviour.
- **M (medium):** the body was skimmed, or the conclusion rests on caption/control strings
  together with matching symbol names.
- **L (low):** a name or string only. Treat it as a lead.

Nothing here was verified in-game.

## 1. How the editor is structured

| Item | Evidence | What it is | Conf. |
|---|---|---|---|
| Entry | Wide strings `4 - Editor` and `5 - Select World for Editor` in the dev frontend menu. Console `SetEditor` / `GConsoleSetEditor` `0x01911b70`. | The dev frontend enters the editor. The community route is Debug Profile, then option 4, then edit FinalAlbion.wld. Option 5 picks a different world first. | M |
| Component | `CEditComponent` (`Run` `0x01a48300`, `SetAsEditingLevel` `0x01a49f80`, `SetAsEditingWorld` `0x01a49f90`, `IsEditorActive` `0x01a48600`) | The editor is its own game component with its own GUI graphics and font. | M |
| Global mode | `EGlobalMode`: NULL, `GLOBAL_MODE_EDIT_LEVELS` | Only one global mode exists. | H (enum) |
| **Edit modes** | `EEditMode` (PDB enum). `SetEditMode` `0x02036dd0` resets input processes, then calls `InitEditMode` `0x0203b500` and `InitViewMode` `0x0203c880`. | 0 NULL, 1 MAP_PLACEMENT, 2 REGIONS, 3 PAINT_MAP, 4 THEME2_MAP, 5 THEME_ENV, 6 EDIT_THINGS, 7 SCRIPT_BRUSH, 8 TRACK_EDITING, 9 THING_ANIMATION, 10 COPY_AND_PASTE, 11 EDIT_BONES, 12 FRACTAL, 13 SURVEY_PASSABILITY, 14 SURVEY_THEMES, 15 SURVEY_SOUNDS, 16 SURVEY_MINIMAP, 17 SURVEY_ENGINE, 18 SURVEY_REFLECTIONS, 19 EDIT_ANIMATION, 20 ATTACH_THINGS, 21 CAMERA_POINT, 22 EDIT_SHAPES, 23 ADD_NEW_SHAPE, 24 ADD_TRACK_POINTS, 25 PREVIEW_SPLINE | H |
| **View modes** | `EEditViewMode`: `EDIT_VIEW_MODE_3D`=1, `RELIEF`=2, `WORLD_MAP`=3. `CEditDisplayEngine::EditSetViewMode` `0x020786f0`. | There are three views: the 3D engine view, a 2D relief (height-map) view and a 2D world-map view. | H (enum) |
| Mode caption (HUD) | Wide strings: `Maps and Regions`, `Height mode`, `Paint Engine Theme`, `Edit Things`, `Attaching Things`, `Animation`, `Edit Tracks`, `Survey (Engine/Passability/Sound Layers/Minimap Zones/Themes)`. Also `Ruler: xy`, `Coord:`, `Selected:`, `World:`, `(Changed)`. `CEditDisplayEngine::ShowMode` `0x02078430`, `ShowOpenFiles` `0x0207b980`. | An on-screen status overlay shows the mode, cursor coordinates, a ruler, the selection, the world name and a dirty flag. | M |
| Input stack | `CEditInputProcess*` classes, one per mode: Main, View2D, View3D, PaintMap, EditThings, ScriptBrushes, Tracks, AddTrackPoints, PreviewSpline, MapPlacement, Region, CopyPaste, Survey{Passability,Themes,Engine,Sounds,Minimap,Reflections}, Animation, Bone, EditShapes, EditCameraPoint, SetPolygonalArea, Console. `CEditInputProcessManager::ProcessInputs` `0x029a1470`. | Each mode stacks its own input handler on top of Main and View. | H (symbols) |
| GUI toolkit | `NEditGui::CWindowCreator` (`CreateImageButton`, `ListBox`, `RadioButton`, `SignedNumberBox`, `SliderBar`, `StaticText`, `Tab`, `TabPane`, `ToggleButton`, `Tree`) `0x02b33370`..`0x02b342b0`. `CGuiWindowBox` has `Minimise` `0x03268ae0`, `Restore` `0x03268ea0`, `AddMinimiseButton` `0x032696a0`, `AddResizeButton` `0x032692a0`, scrollbars, title bar and drag, plus `OnSave` `0x03269e00` / `OnLoad` `0x03269fe0`. Icons `EDITORGUI_MINIMISE_ICON` and `EDITORGUI_RESTORE_ICON`. | Floating dialogs can be dragged, resized, scrolled and **minimised or restored**. Each dialog saves its layout and settings through `OnSave`/`OnLoad` in `<Key> value ... EndWindow` blocks. | M |
| Editor settings persistence | Strings `StartEditorSettings ViewMode EditMode CamPos CamViewDir EngineGridFlag SnapToGridFlag ShowingCreatures ShowingBuildings ShowingObjects ShowingHolySites ShowingVillages ShowDetailMode EditingScriptBrush EndEditorSettings`. `SaveSettings` `0x0203d9c0`, `LoadSettings` `0x0203cdc0`. | The view, mode, camera and toggles survive between sessions. The file name was not traced. | M |
| Undo | `CEditTransactionManager` (`Undo` `0x0295d830`, `Redo` `0x0295daa0`). Transactions `SetHeight`, `SetEngineTheme`, `SetEngineBlend` (`0x02b36350`..`0x02b36cc0`). `CEditWorldBackup` and `CEditLevelBackup` undo buffers. Console `EditorUndoBufferSize`, `EditorSetUndoActive`, `EditorClearUndoBuffer`. | Height, theme and blend edits are undoable transactions, with partitions per stroke (`InsertUndoPartition` `0x0203d5f0`). There are also whole-world and whole-level snapshots. | M |

## 2. Main toolbar (`NEditGui::CToolbar`, window `GW_MAIN_TOOLBAR`)

The button-to-mode table is static data `GModeMap` at `0x04a60648`: `CModeMap[10]`, stride 0x104, with the name followed by the mode. It is read by
`GetEditModeFromButtonName` `0x02b35f00` and `GetButtonNameFromEditMode` `0x02b35f90`.
`OnChildWindowLeftRelease` `0x02b35d50` calls `SetEditMode` for mapped buttons. BACKUP calls
`CreateBackupFile` `0x0204b690` and SAVE_ALL calls `SaveAllIfChanged(1,0)`. All of this is **H**.

| Button (icon) | Caption (tooltip string) | Mode set |
|---|---|---|
| `TOOLBAR1_OPTION_EDIT_WORLD_MAP` (`EDITORGUI_WORLD_MAP_ICON`) | Edit world map | 1 MAP_PLACEMENT |
| `TOOLBAR1_OPTION_PAINT_MAP` (`EDITORGUI_HEIGHT_TOOLBAR_ICON`) | Height mode | 3 PAINT_MAP |
| `TOOLBAR1_OPTION_PAINT_THEME` (`EDITORGUI_THEME_TOOLBAR_ICON`) | Engine themes | 4 THEME2_MAP |
| `TOOLBAR1_OPTION_EDIT_THINGS` (`EDITORGUI_THING_TOOLBAR_ICON`) | Thing mode | 6 EDIT_THINGS |
| `TOOLBAR1_OPTION_EDIT_SCRIPT_BRUSH` (`EDITORGUI_SCRIPT_BRUSH_TOOLBAR_ICON`) | ScriptBrush mode | 7 SCRIPT_BRUSH |
| `TOOLBAR1_OPTION_EDIT_TRACKS` (`EDITORGUI_THING_TRACK_ICON`) | ThingTrack mode | 8 TRACK_EDITING |
| `TOOLBAR1_OPTION_SELECT_REGION` (`EDITORGUI_SELECT_REGION_ICON`) | Select region for copy and paste | 10 COPY_AND_PASTE |
| `TOOLBAR1_OPTION_FRACTAL` (`EDITORGUI_FRACTAL_ICON`) | Fractal mode | 12 FRACTAL |
| `TOOLBAR1_OPTION_SURVEY` (`EDITORGUI_WORLD_MAP_ICON`, reused) | Survey mode | 13 SURVEY_PASSABILITY |
| `TOOLBAR1_OPTION_BACKUP` (`EDITORGUI_BACKUP_ICON`) | Create backup file | calls `CreateBackupFile` |
| `TOOLBAR1_OPTION_SAVE_ALL` (`EDITORGUI_SAVE_ICON`) | Save all | calls `SaveAllIfChanged` |

## 3. Top menu (`NEditGui::CMenu`, handler `0x02947fc0`)

The handler was body-inspected (**H**) for dispatch. Captions come from wide strings.

| Menu | Item (caption / control) | Action |
|---|---|---|
| &File | &New World (`MENU_ITEM_FILE_NEW_WORLD`) | `SaveAllIfChanged`, then `NewWorld` `0x020454d0` |
| | &Load World (`MENU_ITEM_FILE_LOAD_WORLD`) | `SaveAllIfChanged`, then `LoadWorld("")` `0x02038d90`. An empty name opens a **Win32 `GetOpenFileNameW`** dialog titled "Load Level File", filtered to `World Files (*.wld)`. Any .wld can be loaded. LoadWorld also loads the matching quest file and calls `CWorld::GenerateOfflineDataForWorld` (see section 10). **H** |
| | Save &World / &Save All / Save &As | `SaveWorldIfChanged` `0x0204bf00` / `SaveAllIfChanged` `0x0204b550`. Save As goes through the same handler path. A `Save Level File` / `Level Files (*.lev)` caption exists, but Save As semantics were not traced (**L**). |
| | Resolve Files... (`MENU_ITEM_FILE_RESOLVE`) | Confirmation dialog, then `CResolveDialog::StartNewResolve` `0x028c0170`: a three-way .lev/.tng merge (see section 9). |
| | E&xit (`MENU_ITEM_FILE_QUIT`) | `SaveAllIfChanged`, then `OnExit` |
| &View | &2D Relief / &3D Engine | `SetViewMode(RELIEF / 3D)` |
| | Show &Creatures/&Buildings/&Objects/&Holy Sites/&Villages | `SetThingDrawing(type, on)` `0x02036ee0` (the change is not saved: "This hiding or showing of Things won't be saved out.") |
| &Options | Show &Grid / &Snap To Grid | `SetEngineGridFlag` / `SetSnapToGridFlag` |
| | Edit Script &Brushes | `SetEditingScriptBrush` |
| (panel) | `PLAYER_LIST_BOX` (`Player Neutral`, `Player Auto`, `Player N`) | `SetSelectedPlayerNumber` `0x02948ff0`. Selects the owning player for placed things. **M** |

## 4. Hotkeys (read from the switch tables)

`CEditInputProcessMain::ProcessInput` `0x0294aab0` handles key-pressed events. The switch is on
`EInputKey` through the byte table `0x0294b798` and the jump table `0x0294b734`. Modifiers are tested with
`IsKeyboardEventInQueue(HELD, KB_LCONTROL/KB_RCONTROL/KB_LALT/KB_RALT/KB_LSHIFT/KB_RSHIFT)`. **H**
unless noted otherwise.

| Key | Action |
|---|---|
| backtick (char 0x60) | Toggle console |
| 1 / Num1 | World map mode. Pressing it again toggles between MAP_PLACEMENT(1) and REGIONS(2) |
| 2 / Num2 | Height (PAINT_MAP) |
| 3 / Num3 | Engine themes (THEME2_MAP) |
| 4 / Num4 | Edit things |
| 5 / Num5 | Script brush |
| 6 / Num6 | Tracks |
| 7 / Num7 | Copy and paste |
| 8 / Num8 | Fractal |
| 9 / Num9 | Survey. Cycles Passability, then Themes, then Engine (13, 14, 17) |
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| Ctrl+S | Save all (`SaveAllIfChanged(1,0)`) |
| Alt+S | Screenshot: back buffer to `wishworld<N>.tga` |
| F6 | Save all |
| F7 | Save all, then **reload the current world** |
| V | `FindAndShowInvalidThing` `0x0203fe40` |
| Space | `ToggleDetail` `0x0204d250`. Flips the detail mode and runs `turn_off_detail.ini` (`RemoveLocalDetail();`) or `turn_on_detail.ini` through `UpdateDetailMode` `0x0204d360`. This matches the community report that disabling local detail crashes the editor. |
| Ctrl+W | Toggle widescreen 16:9 framing |
| Ctrl+B | `Backup(1)`, then toggle script-brush editing (**M**) |
| Alt+F / Alt+P | Toggle FPS text / profile text |
| Alt+Enter | Toggle exclusive (fullscreen) mode |
| Alt+R | Toggle resolution between 640x480 and 800x600 (asserts "Unknown res change" otherwise). This matches the community report that Alt+R crashes. |
| Alt+E | Save all, construct a new `CMainGameComponent` and switch to it, which leaves the editor for the game. This matches the report that Alt+E crashes. |
| Alt+X | Save all, then `OnExit`. This matches the report that Alt+X crashes. |

The community "Alt+F then S" and "Alt+F then L" are the File menu accelerators (&File, &Save All / &Load World).

Views, from `CEditInputProcessView2D` `0x0294bc10` and `View3D` `0x0294c310` (**M**; branches skimmed):
- 2D view: held arrow keys pan, PgUp/PgDn zoom, RMB drag pans and the mouse wheel zooms. Holding Shift
  multiplies the move speed by 10 (base 0.4, or 0.01 in a fine mode). `SetMoveSpeed` `0x0294bb40`.
- 3D view: held Up/Down move along the view vector, Left/Right rotate XY, Home/End tilt, PgUp/PgDn move
  vertically. RMB or MMB drag with mouse movement rotates about the camera or about a grabbed world position
  (`RotateViewAroundGrabbedWorldPos` `0x0294d920`). The wheel moves. A key-press on H with a
  modifier calls `ZoomToLastWorldCoordPointedAt` `0x02044380`. Camera ground collision is optional
  (console `EditCameraGroundCollision`).

Edit Things keys, from `CEditInputProcessEditThings::ProcessInput` `0x0294e6b0` (**M**; each binding is
key code plus call, with modifier semantics only partly read):
- Held C, M or R change click behaviour: multi-select, and `SetAlwaysGetNearestThing`.
- LMB picks up or carries, then drops or drags the nearest thing. Shift+click places the selected def
  (`PaintInputPlaceThingAt` `0x02994490`). There is also a clone path (`PaintInputCloneNearestThing` `0x02996750`).
- N/S/E/W nudge the selected thing (`PaintInputMoveSelectedThing` `0x02999580`). The Shift variant sets its angle.
- `[` and `]` rotate XY. Ctrl gives YZ and Shift gives XZ (`PaintInputRotateSelectedThingAngle*`).
- `,` and `.` lower or raise the thing's height (`PaintInputAdjustSelectedThingHeight` `0x02997e00`).
- H sets Z or cycles Z over surfaces (`0x02998570`/`0x02998ce0`).
- Delete removes the selected thing (`0x0299bda0`).
- O calls `PaintInputSetNearestThingOwnershipToSelectedOwner` `0x02997b90`.
- Alt+M steps alpha (`StepAlphaOnSelectedThing`).
- Ctrl+L toggles the lock (`ToggleLockedStatusOfSelectedThing` `0x02037740`, **L** for the exact key).
- RCtrl+`;` calls `AddSelectedThingToCurrentQuest` `0x02035420`.
- Ctrl+D calls `SnapSelectionToNearestDummyObject` `0x02052eb0`.

Paint (height/theme) keys, from `CEditInputProcessPaintMap::ProcessInput` `0x0294dbe0` (**H** for dispatch):
- LMB paints: `PaintInputPaintMap` `0x029916a0` in height mode, `PaintInputEngineThemeMap` `0x02993a80` in theme modes.
- Ctrl+LMB is an **eyedropper**. It samples height (`PaintInputPickupHeight` `0x029940d0`) or theme (`PaintInputPickupTheme` `0x029942a0`).
- Holding Shift constrains the stroke to the last point (`PaintInputStickWithLastPoint` `0x02991680`).
- `-` and `=` in height mode step the height by a fixed amount (`PaintInputPaintMapHeightAddition` `0x02993840`).

## 5. Modes and their dialogs

### 5.1 World map (`EDIT_MODE_MAP_PLACEMENT`) and regions (`EDIT_MODE_REGIONS`)

| Feature | Evidence | What it does | Conf. | UI |
|---|---|---|---|---|
| 2D world-map view | `CEditEngine2DWorldMap` (`RenderTerrain` `0x029b7370`, `AddMap` `0x029b6b80`, `SetMapAsLoaded` `0x029b6c90` creates a per-map texture, `SetMapAsUnloaded` `0x029b6ee0`, `RenderGranularityGrid` `0x029b7a00`, `SetLandscapeDebugTintColour` `0x029b72f0`) | Every map in the .wld is drawn as quads. Loaded maps get a texture made from their terrain. Maps can be tinted, which is how the region colouring is shown. Unloaded or other maps render as coloured quads (**L** for the exact fallback). | M | View 3 |
| Maps and Regions dialog | `CMapsAndRegionsDialog` (`MAPS_RADIO_BUTTON` / `REGIONS_RADIO_BUTTON`, `OpenMapDialog` `0x028e8020`, `OpenRegionDialog` `0x028e8190`, `MAP_NAME_TOOLTIP`, `SetTooltipText` `0x028e7cd0`) | Switches between the map-placement sub-dialog and the region sub-dialog. Hovering shows map-name tooltips. | M | Captions "Maps", "Regions", "Maps and Regions" |
| Map placement | `CWorldMapPlacementDialog::ChooseMap` `0x02b32f70` (captions "Select Map", "Level Files (*.lev)", "Load Level File"). `SelectMapForPlacement` `0x020446c0`, `PlaceSelectedMapAt` `0x02044c20` calls `AddLevelFromFileWithUID` `0x0296ade0`, `CanPlaceMapAt` `0x029738a0`. `CreateAndSaveNewLevel` `0x0204c3c0`. | Adds an existing .lev file to the world, or creates a new one, at a grid position. Overlapping maps are rejected, and on load you get " is overlapping another map - it is being removed from the world." | M | World Map Placement dialog |
| Per-map popup | `CWorldMapPlacementPopupDialog::OnChildWindowLeftRelease` `0x028e50e0`. Opened from the map-placement input `0x029509c0` via `PopupMapPlacementDialog` `0x0204b490` on LMB release over a map. | Controls: **"Locked for editing"** (`TOGGLE_LOCK`, which calls `SetMapAsEditable` `0x0204c4c0` / `SetMapAsUneditable` `0x0204c780`), "Cut Map" (`CutMapAndSelectForPlacement`), "Delete Map" (`RemoveMap`), "Resize Map" (`SelectMapForResize` then `ApplyMapResize` `0x02044f60`, with a `CMapResizeDialog`), "Sea" (`IS_SEA`), "Load when player near" (`LoadedOnPlayerProximity`), "script name", and "Fit Neighbours" (`EditFitFillerMap` `0x0297dab0`) with the mountain parameters PHt/TStp/Tens/LoNs/HiNs. Fit Neighbours confirms first: "you want to fit this map to its neighbours(this will change height data!)". | H (dispatch) / M (effects) | Click a map in world-map view |
| Editability is per region | `SetMapAsEditable` `0x0204c4c0` makes every map of the map's region editable (`SetRegionAsEditable`). If the files are read-only it warns: "To save any changes you make, you will need to check one of the maps in this region out from source control. Continue?" / "Open Map For Viewing Only". `CEditWorldMap::SetMapAsEditable` `0x0296ec00` unloads the level and reloads it for edit. Things whose defs are missing are deleted after the prompt "OBJECT NOT IN DEF!!!!". | Only editable (unlocked) maps accept edits: every writer checks `IsMapEditable` `0x0296f350`. `DrawEditableMaps` `0x0204ab20` outlines them. | H | |
| Region editor | `NEditGui::CRegionDialog` (29 methods, `0x02961ff0`..`0x0296a0f0`). Captions: "create new region", "remove this region", "confirm removal", "add map", "click map to add", "remove map", "click map to remove", "Inclusion", "Visibility", "Name", "Display name", "Region def", "Unassigned". State enum `STATE_ADDING_MAP_TO_REGION` / `REMOVING` / `REQUESTING_CONFIRM_REGION_REMOVAL`. | A tree of regions and their maps. You can create, remove and rename regions, set the display name and region def, and click-add or click-remove maps. Each map is linked in **Inclusion** mode (the region contains it) or **Visibility** mode (the region sees it). | H (UI) / M | Regions radio |
| Region save format | `EditSaveRegionsToString` `0x0296b850`. Keys: `NewRegion`, `RegionName`, `NewDisplayName`, `RegionDef`, `AppearOnWorldMap`, `MiniMapGraphic`, `MiniMapScale`, `MiniMapOffsetX/Y`, `WorldMapOffsetX/Y`, `NameGraphicOffsetX/Y`, `MiniMapRegionExitTextOffsetX/Y[]`, `ContainsMap`, `SeesMap`, `EndRegion`. | Serialises regions into the .wld. | H (strings) | |
| Map visibility | `CMapVisDialog` (`AddVisibleMap` `0x028e6cc0`, `SetEditedMap` `0x028e6ad0`), caption "Map Visibility" | Per-map list of visible maps. | L | |
| World-file keys | `Load .wld file`, `MapUIDCount`, `ThingManagerUIDCount`, `LevelScriptName`, `NewMap`/`EndMap`, `MapUID`, `MapX`, `MapY`, `IsSea`, `LoadedOnPlayerProximity`, `LevelName`, `START_INITIAL_QUESTS;`/`END_INITIAL_QUESTS;` | .wld grammar that the editor reads and writes. | H (strings) | |

### 5.2 Height mode (`EDIT_MODE_PAINT_MAP`, `CPaintMapDialog`, caption "Height Toolbox")

There are two tabs, "Land" and "Water". Options are read through `CPaintMapDialog` getters (`0x02903690`..`0x0290ac10`)
and saved in the `HeightFlag Height ChangeHeightFlag ... SeaTG EndWindow` block. Pen dispatch goes through
`PaintInputPaintMap` `0x029916a0`, then the `CEditControlCentre::Pen*` functions and the `CEditWorldMap::Edit*PenUndoable` functions.

| Tool (caption / control) | Evidence | Behaviour | Conf. |
|---|---|---|---|
| Size / Speed (`BRUSH_SIZE_SLIDER`, `OPACITY_SLIDER`) | `GetBrushSize` `0x02907170`, `GetOpacity` `0x02907840` | Pen radius and strength | M |
| Paint Height (`PAINT_HEIGHT_FLAG` / `PAINT_HEIGHT_BOX`) | `GetPaintHeightFlag`, `GetLeftAltitude`, `EditPlacePenUndoable` `0x029753d0` | Paint towards an absolute altitude | M |
| Change Height (`CHANGE_HEIGHT_*`) | `PenChangeHeightAt` `0x02992010`, then `EditChangeHeightPenUndoable` `0x02975710` | Raise or lower by an amount | M |
| Smooth / Smear ("Smear: Spikyness Allowed", "Smear: Smoothness %") | `PenSmearAt` `0x02991e40`, then `EditSmoothPenUndoable` `0x0297b3c0` | Slope-thresholded smoothing | M |
| Spray can | `GetSprayCanFlag` `0x02908c20` | Continuous or spray application | L |
| **Draw Paths** (`DRAW_PATHS_FLAG`) | `SetDrawPathStartPos` `0x02991b70` / `EndPos` `0x02991bb0`. `PenDrawPath` `0x02991bf0` (sole caller `PaintInputPaintMap`) calls `EditDrawPathPenUndoable` `0x02975ad0`. | **Path maker.** Takes a start and an end point. Over the swept box it sets the ground height of cells within the pen radius of the segment (parameter t in [0,1]) to the interpolated start-to-end height, as a single undoable stroke, via `EditSetGroundSizeZAtBlockUndoable`. | H (body) |
| Noise (`NOISE_FLAG`, `NOISE_MAGNIFIER_SLIDER_NAME`) | `PenGenerateNoiseAt` `0x02991ed0`, then `EditGenerateNoisePenUndoable` `0x02975f00` | Adds noise under the pen | M |
| Eyedropper | Ctrl+LMB calls `PaintInputPickupHeight` | Samples the height into the Paint Height box | M |
| Water: Lakes (`LAKE_FLAG`, `WATER_FILL_ALTITUDE_BOX`) | `PaintInputFloodFillStaticWaterAt` `0x02992930`, then `EditFloodFillWaterUndoable` `0x02978300`. Captions "Water Flood Fill", "Fill with water of height". | Flood-fills static water to an altitude. Invalid sites trigger "The ground is too high, or this point is outside a script brush". | M |
| Water remove (`WATER_AUTOREMOVE_FLAG`) | `PaintInputFloodRemoveWaterAround` `0x02992dc0` then `EditFloodRemoveWaterUndoable`, caption "Water Flood Remove" | Removes a connected water body | M |
| Rivers (`RIVER_FLAG`, `RIVER_HEIGHT_BOX_NAME`, Generate, Clear) | `CEditRiver` (`AddWaypointAtPos` `0x0299e9e0`), `PaintInputGenerateRiver` `0x02992f10`, `EditGenerateRiverUndoable` `0x02978b20`, `IsRiverLikelyToOverflow` ("This river is likely to overflow") | Waypoint and spline river carving plus water fill | M |
| Ice / Ocean(Sea) + water theme list (`TG_WATER_LAKE/ICE/RIVER/SEA`) | `GetWaterThemeGroupIndex` `0x0290a6d0`, `InitWaterDefList` `0x02976700` | Chooses the water theme group | M |

### 5.3 Engine / environment themes (`EDIT_MODE_THEME2_MAP`=4, `THEME_ENV`=5, `CTheme2MapDialog`, caption "Themes")

The dialog has two tabs, "ENGINE" and "ENVIRONMENT". Controls: `BRUSH_SIZE_SLIDER(_ENV)`, radio buttons `SMEAR_FLAG(_ENV)`,
`PAINT_FLAG(_ENV)`, `REPLACE_FLAG` and `AUTOREPLACE_FLAG` (captions Size / Smear / Paint / Replace /
**Flood Replace**), and the lists "Theme to Place" and "Theme to Replace" (`ENGINE_THEME_GROUP_LIST_BOX`,
`ENGINE_THEME_LIST_BOX`, `REPLACED_ENGINE_THEME_GROUP_LIST_BOX`, `REPLACED_ENGINE_THEME_LIST_BOX`,
`ENV_THEME_LIST_BOX`). The list entries are "X0-NULL Theme" plus defs.

| Feature | Evidence | Behaviour | Conf. |
|---|---|---|---|
| Theme source lists | `InitThemeGroupListBox` `0x028fbcc0` / `InitThemeListBoxAndReturnFirstMember` `0x028fc5f0`, strings `ENGINE_THEME_GROUP`, `ENGINE_THEME`, `ENVIRONMENT_THEME_DAY` | The group list is built from **ENGINE_THEME_GROUP** defs, the theme list from **ENGINE_THEME** defs in the selected group, and the environment list from **ENVIRONMENT_THEME_DAY** defs | M |
| Paint | `PaintInputEngineThemeMap` `0x02993a80` (body read) calls `EditPlaceEngineThemeUndoable` `0x029799d0` with an `EThemePaintType` (`ENGINE_THEME`=0, `ATMOS_THEME`=1) | Paints the chosen theme under the pen. Each cell has up to `MAX_ENGINE_THEMES_PER_CELL` themes and blends; the assert strings show three layers in the loops. | H |
| Smear | Same function. In engine mode it calls `EditPlaceEngineThemeBlendPenUndoable` `0x0297c510`; in env mode (mode 5) it calls `Blur` `0x0297b910` | Blend smoothing | H (dispatch) |
| **Replace** (brush) | `GetReplaceFlag`, then `EditReplaceEngineThemeUndoable` `0x0297a6c0` | Within the pen, each of the 3 per-cell theme slots holding the "Theme to Replace" becomes "Theme to Place" | H |
| **Flood Replace** (click) | `GetAutoreplaceFlag`. On press only, it asks "Are you sure?" / "Theme Flood Replace", then calls `EditFloodReplaceEngineThemeUndoable` `0x02979ff0` | An 8-neighbour flood fill from the clicked cell replaces the old theme with the new one across the connected area, limited to editable maps and the script-brush mask | H |
| **Theme sampling (eyedropper)** | Ctrl+LMB calls `PaintInputPickupTheme` `0x029942a0`, which uses `EditGetMaxThemeAt` `0x029750d0` | Takes the dominant theme at the cursor. Plain Ctrl sets "Theme to Place" (`SetTheme`); the second flag sets "Theme to Replace" (`SetThemeToReplace`) | H |
| Environment painting | The dedicated `PaintInputEnvironmentThemeMap` `0x029940c0` is an **empty body**. Env painting is routed through `PaintInputEngineThemeMap` with `ATMOS_THEME`. | Env (atmosphere) themes are painted per cell with the same brush | M |
| Themes from heights | `EditSetThemesFromHeights` `0x0297aa80` | Name only. No caller was examined. | L |

### 5.4 Things (`EDIT_MODE_EDIT_THINGS`), `CThingDialog` ("Things") and `CThingPropertyDialog` ("Thing Properties")

| Feature | Evidence | Behaviour | Conf. |
|---|---|---|---|
| Palette tree | `CThingDialog` ctor `0x028f2e90`: `CGuiControlTree` built from `CThingManager::GetThingTypeDefClassName`, `GetNoInstantiatedDefsOfType`, `IsTemplate` and driver info, plus `THING_GROUP` defs (entries marked "(group)"). Controls `THING_GROUP_LIST_BOX`, `THING_TYPE_OPEN_LIST_BOX`, `THING_TREE`, and "Thing to Place:" | Things are listed **by thing type (category)**, then non-template def names. Driver-only things (markers, region exits and so on) appear through driver info. THING_GROUP defs provide groups. Anything unavailable shows " is not available in editor". | M |
| Search | "Quick Find" / "Next" / "Find selected" (`QUICK`, `GO_BUTTON`, `GO_NEXT_BUTTON`, `FIND_SELECTED_BUTTON`) | Text search across the tree, and a jump to the selected thing's def | M |
| Placement options | "Random placement angle", "Place at constant angle" + Angle, "Place at constant height" + Height | Angle and height rules for new things. Ctrl+click samples ground height into Height (`SetHeightToPlaceThingsAt`). | M |
| Property dialog main tab | `CreateMainControls` `0x0290e2a0`. Captions: "Type:", "Find BBM name", "Locked In Place", "Facing Angle", "Mesh From File" (`*.bbm`), "Edit Animation" | Common thing properties | M |
| **Per-class property tabs (CTC editing)** | `CreateGenericVars` `0x02918380`, `RefreshThingFromGenericVars` `0x029197b0`. The virtual `GetPropertiesStruct` / `SetFromPropertiesStruct(CGuiVarTransferStruct&)` is implemented on `CThing`, `CThingPhysical`, `CThingAICreature`, `CThingSwitch`, `CThingPhysicalSwitch` and **27 CTC classes**. Tabs are keyed by category name (`TabNamesAndOffsets[category_name]`). | Each thing class or component contributes (category, label, value) entries. Each category becomes a tab of generic edit controls, and edits are written back through `SetFromPropertiesStruct`. The per-class table is below. | H (mechanism) / M (per-field types) |
| Other tabs | Captions "General", "Anim", "App", "Contnrs", "Shapes", "CameraPoint", "Light", "Script", "Quest:" | Anim tab: play normal and combination animations, carry object, play speech, reset position. App tab: toggle clothing, edit bones. Contnrs tab: object groups, then add or remove objects in a container. Shapes tab: add or edit shapes. Light tab: position and direction from camera. CameraPoint tab: look-at and end position, add track or spline, edit or preview spline, get camera from clipboard (`c:\cameras.bin`), add camera to hero anim. | M |
| Attach / link things | `EAttachModeType` (PDB). `GetViableAttachModesForThing` `0x020342f0`, `SetAttachingThing` `0x02030940` (from `CThingPropertyDialog::SetAsAttachingWithMode` `0x0291f370`), `ToggleThingAttachment` `0x020326e0`, `CAttachingThingsDialog` ("Thing attaching to:", "Stop attaching objects"), `DrawAttachModeLines` `0x02048230` | Modes: things **owned by** / **working in** / **living in** a thing; **spouse**; **parent** (the parent must be female); **to village** (`CTCVillageMember` to village parent); **to activation receptor** (trigger to receptor); **region entrance to exit**; **thing to pre-calculated navigation point**. After choosing a mode you click targets to toggle links. Lines are drawn between linked things. | H (enum + asserts) / M (flow) |
| Region exit links | `UpdateRegionExitConnections` `0x020457c0` (asserts `GetTCDriver(&ptcd_region_exit)`, `ptc_scripted_hook`) | Keeps entrance/exit hook pairs consistent | M |
| Ownership brush | `PaintInputSetNearestThingOwnershipToSelectedOwner` `0x02997b90`, `CTCThingOwner::OnEditorMove` | Assigns an owner quickly | M |
| Validation | V key calls `FindAndShowInvalidThing`. Messages: "There are no invalid things on the loaded maps" / "This thing is invalid. Reason:" | See the existing audit | H |

Per-class property entries, as (category | label) pairs from the string immediates in each `GetPropertiesStruct`.
These are **H** for the labels and do not establish value types:

| Class @addr | Entries |
|---|---|
| CThing @01cf3d10 | General: Script Name, Script Data, Game Persistent. Script: Usable |
| CThingPhysical @01f20190 | General: ObjectScale, CanComeBetweenCamAndHero |
| CThingAICreature @01d71840 | General: BRAIN / OverridingBrain (brain def picker) |
| CThingSwitch @027f9df0 | TriggerRadius, TriggeredBy, TimeToChangeEnvironmentDef, EnvironmentDef |
| CThingPhysicalSwitch @02801880 | General: TriggeredBy |
| CTCDoor @0257fc70 | General: Start open |
| CTCSearchableContainer @025a7470 (chests) | Containers: Number of times to search. Contents use the Contnrs tab. |
| CTCDRegionExit @02635020 | General: Exit Radius, Message Radius, Reversed On MiniMap, Hidden On MiniMap |
| CTCActionUseScriptedHook @0253e6a0 | Script: Force Confirmation, Teleport To Region Entrance, Reversed/Hidden On MiniMap, Camera Track |
| CTCCreatureGenerator @02515d90 | CreatureGen: TriggerOnActivate, SelfTrigger, GenerationRadius, SelfTriggerRadius, SelfTriggerResetInterval, NumTriggers, ActiveCreatureLimit, TotalGenerationLimit, CreatureFamilies (CREATURE_GENERATION_FAMILY) |
| CTCLight @02229100 / CTCSpotLight @02582860 | Light: Overridden, ColourRed/Green/Blue, InnerRadius, OuterRadius, Flicker, Inverted (spot light adds Angle and Width) |
| CTCCameraPointDefinitionBase @024f3430 | CameraPoint: CutInto, CutOutOf, TestAngleBeforeActivation, FOV, SelfTerminate, HeroIsSubject |
| CTCExplodingObject @025a5370 | Explosion: Radius, MaxDamage, Triggered on creature proximity, Trigger radius, Fire damage |
| CTCStockItem @0262d280 | Shop item: For sale, Stealable, Price |
| CTCActivationReceptorBase @02637e10 / ...CreatureGenerator @026395c0 | Activation: Timed deactivation, Frames until deactivation / GeneratorSetActive, GeneratorTrigger |
| CTCHeroStats @01e67abf | Appearance: Morality, Age, SunTan, Fatness |
| CTCActionUseBed @02546450 | General: Bed usable by hero, Bed owned by hero |
| CTCActionUseReadable @02540f80 / CTCInfoDisplay @02783440 | General: TextTag (plus TextTagBack, Radius and DisplayTime for InfoDisplay) |
| CTCHeroCentreDoorMarker @02648310 | General: Door type (EHeroCentreDoorType), Radius |
| CTCDayOrNightOnlySupport @02667ee0 | General: DayNightExclusive (Day only / Night only / Both day and night) |
| CTCStealableItemLocation @026dade0 | General: Radius items should be within, Radius to take items back to |
| CTCAtmosPlayer @02797070 | Sound: AtmosType (SOUND_THEME) |
| CTCDParticleEmitter @01d68c70 | ParticleEmitter: ParticleID |
| CTCRandomAppearanceMorph @0252e510 | General: RandomAppearanceSeed |
| CTCBoastingArea @02652320, CTCGatherPointBase @026675e0, CTCDiggingSpot @0278da20 | Radius / MaxOccupation / Start Hidden |
| CTCBase @02190d00, CTCShapeManager @025d6f40 | No literal labels (base and forwarding) |

### 5.5 Quest sections (`CQuestDialog` "Quests", `CInitialQuestsDialog` "Initial quests")

| Feature | Evidence | Behaviour | Conf. |
|---|---|---|---|
| Show or hide by section | `QUEST_NAMES_COMBO_BOX` (multi-select), "Quests to display:", "Select all", "Deselect all", "Day only", "Night only". Handler `0x028c3ac0` calls `ShowOrHideThingsInQuests` `0x020350e0` | Toggles which quest sections' things are drawn and selectable (`CTCEditor::EReasons` QUEST=4) | H |
| Current section | `CQuestDialog::OnChildWindowContentsChanged` `0x028c4eb0` calls `SetQuest` `0x02034e60`. `CEditDisplayEngine::SetQuestBeingAddedTo` `0x020350a0` | New things go into the chosen section | M |
| Available sections | `AddPrecreatedQuest` `0x028c61d0` (duplicate warning "has been added more than once to the quest lists"). `LoadWorld` fills the list from `CQuestManager::GetRegisteredQuestNames` | The list is limited to **registered (script) quest names**. No free-text "new section" control was found. | M |
| Move a thing into a section | RCtrl+`;` calls `AddSelectedThingToCurrentQuest`. Property dialog `QUESTS_LIST_BOX` / `UpdateQuestAttachmentStatus` `0x02919310` | Reassigns a thing's section | M |
| Orphan sections | `RemoveThingsInNonExistantQuests` `0x02035580`, prompt "An entity is attached to section name '...' ... is not registered ... keep these things?" | Runs when maps are unlocked | M |
| Initial quests | `CInitialQuestsDialog` (`INITIAL_QUESTS`), `CEditWorld::SaveInitialActiveQuests` `0x0207ed60` | Chooses the quests active at world start (`START_INITIAL_QUESTS;` in .wld) | M |

### 5.6 Script brushes (`EDIT_MODE_SCRIPT_BRUSH`, `CScriptBrushDialog` "Script Brush")

`CreateScriptBrush` `0x02042ea0` (from `CEditInputProcessScriptBrushes` `0x0294e080`), `SetSelectedScriptBrushName`,
`SetSelectedScriptBrushCreationTime`, `SetScriptBrushThingsToAdd/Delete/Move` (`0x02042010`/`0x02041800`/`0x020416b0`),
`CEditMap::RemoveScriptedMapBrush`, `EditApplyScriptedMapBrush` `0x02970050`, captions "Script Name" and "Creation time", and the error
"Cannot have two scripted map brushes with the same name." The feature is named rectangular areas that record
height, theme and thing changes for scripts to apply at runtime. Edits inside an active brush are masked to its box
(`GetCurrentMask`). **M.**

### 5.7 Tracks (`EDIT_MODE_TRACK_EDITING`, `CTracksDialog` "Tracks")

The dialog has three radio modes: "Placement mode", "Linking mode" and "Main/Secondary Track Selection mode". It also has track name and length
("Main Track Name :", "TrackLength : %.2f"), "Preview Track" and "Preview Time Seconds". The code is `PaintInputPlaceTrackNodeAt` `0x02995d90`,
`Start/Continue/FinishLinkingTracks` (`0x0202f950` / `0x0202f690` / `0x020302d0`), `FlipSelectedTrack`, `DeleteSelectedTrackNode`,
`DeleteLinksFromSelectedTrackNode` and `PreviewCameraTrack` `0x0202f090`. Track nodes cannot be pasted. See the existing audit for
`FinishLinkingTracks`. **M.**

### 5.8 Copy and paste (`EDIT_MODE_COPY_AND_PASTE`, `CCopyPasteDialog` "Copy and paste") and brushes

The dialog offers "Select Region", "Copy heights", "Copy themes", "Copy things", "Place height relatively", "Relative to bottom" and "Relative to".
The code is `CEditInputProcessCopyPaste` (`CopySelectedRegion` `0x02953540`, `Paste` `0x02953c10`, `RotateBrush` `0x029556d0` in 90-degree
steps, `SaveBrush` `0x029558c0` / `LoadBrush` `0x02955d10` to `Data\Brushes\*.brush`). The brush library
(`CBrushLibraryDialog`, `FLAT_LIST_BOX`, "Save brush") holds saved brushes. Pasting refuses villages, switches, markers
and track nodes. **M.**

### 5.9 Fractal (`EDIT_MODE_FRACTAL`, `CFractalDialog` "Fractals")

Parameters: Lacunarity, Fractal dimension, Octaves, Map pos X/Y, World scaler, Scale, Use falloff, and Start and End falloff distance.
Buttons: "Generate fractal" (`GenerateFractal` `0x028e0350`, preview via `CEditFractal::DrawFractal` / `DrawClouds`) and
"Apply fractal". Maths and application are in the existing audit. **H** for the fields.

### 5.10 Survey (`EDIT_MODE_SURVEY_*`, `CSurveyDialog` "Surveys")

The tabs are Engine, Passability, Themes, Sounds, Minimap and Reflection. The state enum `ESurveyState` has 16 values.

| Tab | Controls (captions) | Backing code | Conf. |
|---|---|---|---|
| Passability | Brush radius, Off, **Passability**, **Camera passability**, **Villager Preferability** (pref-nav), **Show navigability**, **Nav layer** (numbox) | `EditSetPassability` `0x02970060`, `EditSetCameraPassability` `0x02970630`, `EditSetPrefNav` `0x02970360`, `EditShowNavigability` `0x029722e0` / `OverlayNav` `0x029723b0` (quad-tree nav nodes by layer) | H (dispatch) |
| Themes | Sampling radius, Off, **Theme density** (overlay using `EDITOR_THEME_MOST_DENSE` / `MOST_SPARSE`), **Sampled themes** list | `SampleThemesForThemesSurvey` `0x02051c90` calls `EditSampleThemes` (set of themes within the radius). `DisplayThemeDensities` `0x029a9e80` | H |
| Engine | "Stats for selected areas/things": number of things, triangles, vertices, texture memory. Include local detail / buildings / creatures / objects / others. Count all duplications. "Detailed textures summary...". Save stats to file (`editor_engine_stats.csv`) | `CEditPrimitiveStats`, `AddAreaToEngineSurvey` `0x02051310` | M |
| Sounds | Brush radius, Off, Show all sounds, Show selected sound, **Paint selected sound**, sound-layer list (`SOUND_THEME` defs), Clear all ("Clear All Sounds?") | `EditSetSounds` `0x02971010` (per-cell sound theme per layer, limited to editable maps), `EditFillSound` `0x02970df0` | M |
| Reflections | Brush radius | `EditSetReflections` `0x029712e0` | L |
| Minimap | Brush radius, zone numbox, show all or selected, paint, clear all | The writer `EditSetMinimapZone` `0x029a9930` is **empty** (existing audit) | H (inactive) |

### 5.11 Other modes and dialogs (leads)

| Dialog / mode | Evidence | Notes | Conf. |
|---|---|---|---|
| Animation / Animation events | `CAnimationDialog` (43 methods: normal anims tree, load .bba, frame slider, speed, interpolate, reset position/orientation, track movement, carry object/slot). `CAnimationEventsDialog` (insert game/sound/stop events, save to the animation event DB: "Unable to save animation event file - is it checked out?") | Asset tools, not level tools | M |
| Bones | `CBoneDialog` ("Bone editing tool", X/Y/Z factor with locks, groups, save/load `*.bncfg`) | Per-bone scaling presets | M |
| Shapes / camera points / splines | Modes 21–25. `CEditInputProcessEditShapes` / `EditCameraPoint` / `AddTrackPoints` / `PreviewSpline` / `SetPolygonalArea`, `CTCShapeManager::EditUpdate*`, `CTCCameraPointScripted::EditSet*` | Polygon or line shapes (`NShape::EShape` CLOSED/LINE), scripted camera points and splines | M |
| Scene browser | `CSceneDialog` ("Scene Browser...", "Only ScriptNamed Objects", "Auto Update", "Sort by distance", "Area Range", "Center Object", `JumpTo` `0x028f21c0`) | A tree of nearby script-named things that jumps the camera to a thing | M |
| Resolve (merge) | `CResolveDialog` ("Accept ours", "Accept theirs", log / ours / theirs views, "---- Merge Utility ----", `Ours.lev`/`Theirs.lev`/`Prev.lev`) plus `CEditLevelMerger` | Interactive three-way level merge | M |
| Backups | `CreateBackupFile` `0x0204b690`, `.backup` files, `SetSaveToBackupFiles` `0x0204b6e0`, `BackupWorldAndAllMapsToFile` `0x0207f9a0` / `Restore...` `0x0207fce0`, console `EnableEditorAutosave` | Snapshot and restore of the world plus all maps, and autosave | M |
| Heightmap TGA import/export | `ImportTGA` `0x0203b220`, `ExportTGA` `0x0203b330` | **No direct caller found** by the E8/E9 scan. They are probably unreachable. | L |
| Console (editor) | `EditLoadCamera`, `EditSetCameraFOV`, `EditSetCameraPos`, `EditSetCameraToFace`, `EditResizeZPercent`, `EditSetZ`, `EditRaiseZ`, `ActivateSelectionHighlighting`, `EditBlinkHighlightedThing`, `EditCameraGroundCollision` (handlers `CEditControlCentre::Console*` `0x0204c980`..`0x02053b60`) | Scriptable camera and height ops | M |

## 6. Navigation and static-map (STB) generation

| Feature | Evidence | Behaviour | Conf. |
|---|---|---|---|
| Nav regeneration on save | `GenerateNavigationInformation` `0x0204b700`. Its only callers are `SaveAllLevels` `0x0204b5d0` and `SaveLevelIfChanged` `0x0204b990` (when the flag argument is set). | For each qualifying map it calls `CNavigatorManager::RemoveNavigationMap`, then `CWorldMap::ActivateNavMap`, which rebuilds in-memory nav. There is **no separate "generate nav" button**; it happens when saving. The string "Generating navigation map for map" exists. | H (callers) / M |
| Navigation seeds | `CTCDNavigationSeed` (`Create` `0x0262f7b0`). The world map gathers them with `GetAllTCDsInRoughArea<CTCDNavigationSeed, CThingFilter_IsInQuestOrNullQuest>` `0x01ccbd00` | Seeds are placeable driver things (the NAVIGATION_SEED marker) that the nav builder consumes. Seed semantics belong to the nav lane; see `HANDOFF_NAV.md`. | M |
| Inputs | Passability, camera passability and pref-nav brushes (section 5.10). Show navigability overlays the quad tree by nav layer. | Authoring surface for nav | H |
| STB build | `CWorld::GenerateOfflineDataForWorld` `0x01b3ef70`, called from `CWorld::Load` and editor `LoadWorld`. If `GEnableStaticMapCreation` (bool @0x04a410c6) is set, it calls `CWorldMap::CreateStaticMaps` `0x01c93600`. Console variables: `EnableStaticMapCreation`, `OnlyUpdateNonExistantStaticMaps`, `ForceStaticMapUpdateOnMap`, `EnableMinimumStaticMapRebuild`, `BuildRetailStaticMaps`, `SetStaticMapQuality`, `InstallStaticMaps`, `EnableStaticMapGlobalOptimizations`. | STB generation is **not an editor button**. It is a load-time offline-data pass controlled by console variables. The dev `userst.ini` contains `SetStaticMapQuality(1);` and a commented `OnlyUpdateNonExistantStaticMaps TRUE;`. | H (body) |

## 7. Modder parity checklist, answered from vanilla evidence

| Request | Does vanilla have it? | Evidence and notes |
|---|---|---|
| Collapsible panels | **Yes (M).** | Every editor dialog is a `CGuiWindowBox`, which has minimise, restore, resize, drag and scrollbars (`0x03268ae0`/`0x03268ea0`/`0x032696a0`) with `EDITORGUI_MINIMISE_ICON` / `RESTORE_ICON`, and its layout persists through OnSave/OnLoad. Tree branches collapse too (`CGuiControlTree::CollapseTree` `0x03270960`). |
| Top file/settings menu | **Yes (H).** | File / View / Options menu (section 3), plus the separate icon toolbar (section 2). |
| Load other .wld files | **Yes (H).** | File > Load World opens `GetOpenFileNameW` "Load Level File", `World Files (*.wld)` (`LoadWorld` `0x02038d90`). The dev frontend also has "5 - Select World for Editor". F7 reloads the current world. |
| Engine-theme painting from ENGINE_THEME defs, with local detail | **Yes for painting (H); local detail is implicit (L).** | Lists come from ENGINE_THEME_GROUP, then ENGINE_THEME defs. Paint, smear, replace and flood replace work on 3 slots per cell plus blend. No separate local-detail brush was found. Local detail appears only as the global Space toggle (`turn_on/off_detail.ini`) and as an engine-survey filter. Local detail presumably follows the theme through the theme defs (`CEngineLocalDetailLayerDef` exists), but no editor code proves this. |
| Replace engine theme (click patch) and Replace All | **Partly (H).** | Two replace tools exist. "Replace" is a brush: within the pen, old theme becomes new (`0x0297a6c0`). "Flood Replace" is a click that asks for confirmation and floods the **connected** region, 8-neighbour, on editable maps (`0x02979ff0`). **No global "replace everywhere" was found.** Flood replace only covers the contiguous patch. |
| Theme sampling | **Yes (H).** | Ctrl+LMB eyedropper for theme to place or theme to replace (`PaintInputPickupTheme` `0x029942a0`). There is also the Themes survey with a sampling radius and a list of sampled themes (`0x02051c90`), and a theme-density overlay. |
| CTC-class property editing per entity type (CTCDoor, CTCChest...) | **Yes (H mechanism).** | The Thing Properties dialog builds category tabs from the virtual `GetPropertiesStruct` / `SetFromPropertiesStruct(CGuiVarTransferStruct&)` on 5 thing classes and 27 CTC classes (table in 5.4). Doors expose "Start open". Chests go through `CTCSearchableContainer` ("Number of times to search") plus the Contnrs tab (add or remove objects). No `CTCChest` class exists in this build under that name. |
| Actor/entity lists from def entity groups by category | **Yes (M).** | The Things palette tree is organised by thing type, then non-template defs, with `THING_GROUP` def groups, Quick Find and Find selected. The container object picker uses `THING_GROUP` groups too (`InitContainerGroupListBox` `0x02921020`). |
| 3D whole-world view; flat quads for maps without STB; click-to-activate for editing; multiple .lev loaded | **Mostly (M).** | View modes are 3D, 2D Relief and 2D World Map. The 2D world-map renderer draws each map as quads, textured when loaded (`SetMapAsLoaded` `0x029b6c90`), with tint colours. **Click-to-activate:** in the world map, clicking a map opens a popup whose "Locked for editing" toggle calls `SetMapAsEditable`, which unlocks the whole region and reloads its levels for editing. All maps of the .wld are loaded, and edits are gated per map by `IsMapEditable`. The claim "flat quads for maps without STB in 3D" is **not proven**: the quads observed are in the 2D world-map view. The 3D fallback for missing STB was not traced. The community reports that switching from 2D to 3D crashes. |
| Heightmap path maker (drag between two points with radius) | **Yes (H).** | Height Toolbox "Draw Paths": start and end points, then `PenDrawPath` `0x02991bf0` calls `EditDrawPathPenUndoable` `0x02975ad0`, which sets cells within the pen radius of the segment to the height interpolated between start and end, as one undoable stroke. Shift constrains to the last point. |
| Quest sections toggle / add | **Toggle yes (H); add is limited (M).** | The Quests dialog shows or hides things by section (multi-select combo box, select or deselect all, day or night only), picks the current section for new things, and has RCtrl+`;` to move a thing. Sections come from **registered quest names** (`AddPrecreatedQuest`). No free-text "create new section" was found. There is also an Initial quests dialog. |
| Painting environment themes | **Yes (M).** | The ENVIRONMENT tab (`ENV_THEME_LIST_BOX` from ENVIRONMENT_THEME_DAY). Painting goes through the engine-theme path with `ATMOS_THEME`. The dedicated `PaintInputEnvironmentThemeMap` is empty. |
| Painting sound themes | **Yes (M).** | Survey > Sounds: sound-layer list of SOUND_THEME defs, paint selected, show all or selected, clear all (`EditSetSounds` `0x02971010`). |
| Nav / navigability generation from NAVIGATION_SEED | **Yes, implicitly (M).** | Nav is rebuilt on save (`GenerateNavigationInformation`). There is no explicit button. Seeds are `CTCDNavigationSeed` things gathered by the world map. The Show navigability overlay has a layer selector. Passability, camera passability and villager-preferability brushes feed it. |
| Fractals | **Yes (H).** | Fractal dialog and mode (section 5.9). See the existing audit for application maths. |
| Link entities to parents/villages; entrance and exit markers | **Yes (H).** | `EAttachModeType`: owned-by, working-in, living-in, spouse, parent, village, activation receptor, **region entrance to exit**, pre-calculated nav point. Plus `UpdateRegionExitConnections`. |

## 8. Things that are absent or inactive (do not copy them blindly)

- The minimap-zone painting writer is empty (existing audit, `0x029a9930`).
- `PaintInputEnvironmentThemeMap` `0x029940c0` is empty. Env painting uses the engine path instead.
- `ImportTGA` / `ExportTGA` have no callers found.
- There is no explicit Generate Nav or Build STB button. Both are side effects of save and load, controlled by console variables.
- Several documented crash hotkeys (Alt+E/R/X, Space for detail) map to heavy operations: component switch, resolution change, quit, and a console script. The crashes are reported by the community and were **not reproduced** here.

## 9. Suggested next evidence if parity work starts

1. `CreateGenericVars` `0x02918380`: recover the control type per `CGuiVarTransferStruct` entry (bool, float, SLONG,
   string or def picker) so the Forge property grid matches.
2. `InitThemeGroupListBox` `0x028fbcc0`: confirm the group-to-theme membership rule.
3. The 3D display path for maps without static-map data: `CEditDisplayEngine::UpdateLandscapeDebugColours` `0x0207bd70`
   and the engine's missing-STB fallback.
4. `EditFitFillerMap` `0x0297dab0`: the mountain parameters behind "Fit Neighbours".

## 10. Recovered since (2026-09-27, static RE, FableWin; ported and tested)

**Environment and sound themes in the .lev.** These are not stored in the 21-byte height cells. They live in a game-map grid
(`CGameMapCell`) of `(width/4)*(height/4)` records, 11 bytes each, one per 4x4 height cells. The grid follows the
height cells and ends exactly at `obsOffset`. It is present only when fileVersion (map header +0x8416) is above 1.
Record layout: `u32 11, u8 1` header; atmos slots at +5..7; atmos strengths at +8..9, with slot 2 taking the
remainder; sound index at +10, where 0 means none.

Where the names come from:
- The atmos palette is the 256 x {name[128], u32} block at map header +0x841e. The engine resolves each slot by name.
- The sound list is the counted strings after it, with index 0 the implicit NULL.

Evidence: `CMap::LoadFromFile` 0x022327b0, `SaveToFile` 0x02234c60, `Get/SetSoundAt` 0x02238d20/0x02238d90.
Height-cell bytes: +0..4 is the record header, +9 and +18 are unused, +16 is camera-passable, +17 is the old sound
(read only below fileVersion 3), +19 is the shore point. All 399 retail maps parse the grid
(`forge info <map>` lists both). Adding a sound name changes the list's length, so every later offset moves,
including obs, nav and the nav table of contents. FableForge limits painting to the map's own list.

**Fractals.** `CEditFractal` is a Musgrave hybrid multifractal over 2D gradient Perlin noise, with seed 0.
The ported code and addresses are in `libs/forgecore/include/forge/fractal.hpp`, with tests in `testFractal`.
Several paths are dead code: the seeded `CNoise(long)` ctor, `SetNewFractalPos`, `PostProcessRescale`, and the
ridged/hetero variants. "Apply fractal" sets heights to `fractal(world) * Scale` in world units;
`CHeightMap::SetSizeZAt` clamps them to [0, 2048). The .lev stores height / 2048. The dialog's default Scale is 1000.

**Tracks.** Tracks are chains of `TrackNode` things (`TRACK_NODE_BASIC`); there is no separate track object.
- **Fields.** `LinkedToUID1` is the previous node and `LinkedToUID2` the next. `Start TRUE` marks the head and
  `End TRUE` the tail. ScriptName, the track name, is written twice, and the loader reads the last copy.
  A lone node is `Start TRUE, End TRUE`.
- **Invariants the engine asserts.** Links are symmetric, a node has at most two links, and there are no
  branches or loops. Every node in a chain has the same name. All 528 links in the dev data are symmetric.
- **Use.** Village guards patrol along them (`GuardTrack`, 293 nodes), and cut-scene cameras follow them.
- **Camera preview.** The camera runs along a main track while looking along a view track. Both use straight
  segments and are sampled by arc length at `u·L`, with `u += dt/T`, so they finish together.
- **`CameraTrackUID`.** This is on `CTCActionUseScriptedHook`, but none of its 19 values matches a thing, so
  it looks stale. Tracks are looked up by name.
- **Evidence.** `CThingTrackNode` Save/Load, `SetAsLinkedTo`, `FinishLinkingTracks` 0x020302d0,
  `UpdatePreviewTrack` 0x0202f380. Ported as `Document::tracks` / `linkTrackNodes` / …, tested in `testTracks`.

**Script brushes.** These are named areas that record a target state of heights, themes and things for
`ApplyScriptBrush(name)`, stored in the .lev block at [BrushDataOffset (+13), navOffset).
- **Byte layout.**
  - A 33801-byte header: a 5-byte prefix, `NoBrushes`, then a 256 × {name[128], i32} theme palette.
  - Then, per brush:
    - a 73-byte `CFileFormatBrush` (version 2): GameArea box, ScriptName[32], CreationTimeSeconds,
      CreationPattern, and the move/delete/add counts;
    - W×H 14-byte cells: f32 height, u8 theme[3], u8 blend[2];
    - move records (37 bytes), then delete and add records (13 bytes, UID only).
- **Not used by the shipped game.** Every retail and dev level has `NoBrushes = 0`: the dev tree (457), the
  Steam and dev WADs, and the Xbox ISO. Retail Fable.exe never calls the binding at vtable slot 0xABC.
- **Possible engine bug (inferred from the code, not tested).** The timed apply only applies themes and
  things when progress lands within 1e-4 of 1, so it may never finish.
- **Evidence.** `CMap::LoadBrushesFromFile` 0x02236060 / `SaveBrushesToFile` 0x02236900,
  `CGameScriptInterface::ApplyScriptBrush` (FableWin 0x02aa7ed0, retail 0x0088F480).
- **Status.** Parked: no retail use, a possible apply bug, and the height units are unverified.

**Region minimaps.** The retail `MINIMAP_*` textures are hand-painted parchment art, not output of the engine's
generator.
- **The generator is debug-only.** It is `CRegionMinimap`, and it runs only when the `GenerateMiniMaps` bool
  is set. It writes `<Region>.tga`.
- **Output.**
  - A 256² image over the union square of the region's maps, with the rows flipped.
  - Each pixel gets a class from the strongest ENGINE_THEME's `MinimapTheme` (+0x98).
  - The class maps to a 9-colour palette taken from `PLAYER_GUI_PC.MinimapThemeColours`:
    none #000000, grass #2A8B41, earth #776F4E, wood #524202, snow #BEE5F7, cliff #E9DFC4,
    water #40CAD6, foliage #045806, building #223123.
  - For lit classes, the colour is lerped toward 0.8 × itself by the face normal · (1,1,1).
  - Buildings: a vertical segment test against the physics mesh inside each building's bounding box gives
    class 8.
- **Evidence.** `PopulateRegionMinimap` 0x02186790, `BuildRegionMinimapTGA` 0x021886b0,
  illumination 0x021878d0.
- **Retail check.** None of the palette colours appears in seven exported retail minimaps; the median distance
  is 33–48 RGB. All seven share one byte-identical alpha disc: opaque to a radius of about 109 px, 0 at about
  130 px. FableForge's current fade is close to it.
- **Consequence.** No engine-exact bake can reproduce retail art. If wanted, FableForge could offer the
  generator's class map as a "debug-style" minimap.

## 11. Live run (2026-09-28, FableWin from the Anniversary dev tree)

- Route that works: dev profile screen -> **A** [Debug Profile] -> **4** Editor. The world
  loads (`OpenStaticMap`) and the editor opens in **Maps and Regions** mode on
  `Data\Levels\FinalAlbion.wld`: panels *Initial quests*, *Center Object*, *Map Placement*
  (Select Map, Area Range), *Map Visibility*; a toolbar of ten mode icons; the world grid
  with the compass rose; status line `World:` / mode name / `Coord: x, y, z`.
- On the way in it asserts `NOT GPDisplayManager->IsVirtualCoordsResolutionIndependant()`
  (`edit_component.cpp` 228) at 1600x900; "always skip" gets past it, but the editor's
  panels then lay out on top of each other (the editor UI is written for virtual,
  resolution-independent coordinates). The flag is a byte on the display manager set only
  after a deferred request (+0x255 request, +0x258 frame countdown, then +0x254 = 1).
- Synthesized input (SetCursorPos + SendInput clicks, keys) does not reach the editor: the
  `Coord` readout never changes and no toolbar mode switches, while the process runs a full
  core. Keys worked on the dev front end, so the editor reads the mouse through another
  path. Next: try real mouse input, and a resolution / windowed setting that makes the
  virtual coordinates resolution-independent before entering the editor.
- Quest cards: no editor panel. Cards are OBJECT defs specialising
  `OBJECT_QUEST_CARD_TEMPLATE` with a `<CQuestCardDef>` block (QuestName, QuestSummary,
  RegionName, IsCoreQuest, CanPlayerCancel, GoldReward, RenownReward, RewardObjects[],
  NumBoasts, IsExclusive, IsVignette, InventoryCategory) plus `Graphic.BankIndex` (the card
  mesh); the source is in the dev tree's `Data/Defs/objects.tpl` (~915) and
  `objects_gameplay.def`. The binary also carries dev test cards (TestQuestCard1-3 scripts),
  `GiveHeroQuestCardDirectly` and the card-screen layout keys (`QuestCardInfo*`).
