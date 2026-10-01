# Parity backlog: FableForge vs the vanilla debug editor (2026-09-28)

Every item below was found by an area-by-area comparison of FableForge's code against the vanilla FableWin editor
(inventory + disassembly), then checked by a second agent that tried to refute it; only the survivors are listed.
Order within a group: user value, then effort. `gap` = vanilla has it, we do not; `weakness` = we have it but it
deviates from vanilla in a way a user notices. Status is tracked in ROADMAP_1.0.md as items land.

## Editor

### Things mode: Things dialog (palette, Fin...

#### Things with no mesh (markers, camera points, region exits, nav seeds, switches) are not drawn, cannot be picked and are left out of the list
*gap, effort M, value high*

2026-09-30: implemented for things lacking a renderer instance. Shared visible
glyphs handle draw, ordinary/Ctrl/context selection and link targeting; Markers
chip, list filter and focus fallback are wired. MayorsHouseHallway UI checks pass.
The viewport link picker now has a successful exit-to-entrance UID assertion and
byte-exact undo coverage in addition to invalid-target rejection.
Authored editor meshes and native occlusion remain separate follow-ups. See
HANDOFF_WORLD_UI for validation and limits.

2026-09-29 bedtime checkpoint: researched, not implemented. Updated integration
notes in HANDOFF_WORLD_UI supersede the old mesh-first picking proposal below:
a mesh-first fallback prevents selecting markers drawn over floors/walls. Use one
shared visible overlay set and an explicit hit-priority policy, with viewport/depth
checks, section/toggle guards and context/link handling. Renderer ownership caching
must refresh at actual upload/bind/clear, not just document revision.


**Vanilla:** The editor draws and selects every thing through CTCEditor (IsDrawable 0x02037620, IsSelectable 0x01f964e0, AddReasonToNotDraw/NotBeSelectable only for quest or type filters). Driver things (MARKER, REGION_ENTRANCE/EXIT, NAVIGATION_SEED, CAMERA_POINT) can be selected, moved, linked and deleted. The dev FinalAlbion has 1,628 Marker things and 1,074 'Thing' things (2,493 CTCDCameraPoint, 198 CTCDRegionExit, 119 CTCDNavigationSeed).

**Fix:** 1. **Pick glyphs for things with no mesh.**
   - In gui/editor.cpp, add `App::glyphThingAt(float px, float py) const`, modelled on `trackNodeAt` (editor.cpp:809).
   - Loop over `doc_.thingCount()` and keep a thing when:
     - `doc_.frameOf()` succeeds;
     - it is not hidden (`thingHiddenBySection`);
     - it has no rendered instance.
   - To answer "has no rendered instance" cheaply, build a `std::vector<bool> hasInstance_` from `renderer_.instance(k).thing` each time the scene is synced (where `syncedRevision_` is set). Cache it by `doc_.revision()`, the same way `cachedTracks` does.
   - Project `{pos0, pos2+0.3, -pos1}` to the screen. Pick the nearest glyph within S(12) px.
   - Vanilla uses a bounding-radius ray test (`CThingFilter_BoundingRadiusIntersectsInfiniteLine` 0x01f93350). A screen radius is the right equivalent when there is no mesh.

2. **Draw the glyphs.**
   - Add `drawThingGlyphs(origin, size)` next to `drawTrackLines` and call it from the same place.
   - Draw a small shape per thing, coloured by kind: `Marker`, and CTCD region exit, region entrance, camera point, navigation seed and switch. Use `doc_.summary(i).type` and `.definition` prefixes such as `MARKER_`, `REGION_EXIT`, `CAMERA_POINT` and `NAVIGATION_SEED` to choose the kind.
   - Highlight selected glyphs. Show the def or script name via `thingLabel` in a tooltip when hovered.
   - Add a viewport toggle "Show markers" (default on in edit mode) and register it with `auto_` for automation.

3. **Wire the glyphs into `pickAt`** (editor.cpp:859).
   - Compute `dot = glyphThingAt(...)` once at the top.
   - Track-link branch: prefer `trackNodeAt`, then fall back to `dot`.
   - Link-pick branch: `target = inst >= 0 ? instance thing : dot`. This makes exit-to-entrance links and other marker targets work; `linkTargetFits` still validates the type.
   - Normal selection: if `inst < 0 && dot >= 0`, select or Ctrl-toggle `dot` instead of clearing the selection.
   - A glyph that is closer on screen than the mesh hit could win, but mesh-first with glyph fallback is enough.
   - Gizmo, delete, copy and properties already work by thing index, so they need no change.

4. **Objects list** (editor.cpp:3023).
   - Remove the `s.type == "Marker"` exclusion. Keep `TrackNode`, which the Level tab already covers.
   - Add a small type filter (All / Objects / Markers) so the list is not flooded with markers.

5. **Export report** (editor.cpp:4109). Change the note to say these things are shown as markers in the view.

6. **Optional, effort M on its own:** render the def's editor mesh when vanilla has one. Leave it out of this change.

Effort is M. Value is high: region exits, entrances, camera points, nav seeds and hero-start markers become selectable, movable, linkable and deletable, which row 17 exit-to-entrance linking needs.

#### Owned thing lifecycle parity
*weakness, effort M, value high*

**Current status (2026-09-30):** Movement, rotation, height and surface placement
now carry valid owned descendants in one undo step. The shared frame helper also
supports gizmo/carry preview, nested owners and locked children. The saved toggle
defaults on. The implementation plan below is retained as native evidence and
remaining scope; its transform, section and deletion steps are complete. OwnerUID
remapping on selected group copies and the cached count hint are complete. Full native
clone-from-definition behavior remains open. Other copied UID links still point
to their original targets. Quest-section and
day/night changes move valid owned descendants in one undo step; the UI supports
multiple selected roots and keeps selection by UID.
Deletion offers Delete all, Only selection (detaching surviving direct children),
and Cancel. Locked derived children follow owner deletion; direct locked selections
remain protected. Core and UI checks cover exact undo and no surviving LINK issue.

2026-09-30 native evidence update: FableTLC `CTCOwnedEntity::OnSerialise`
`0x007e8460` reads/writes the 64-bit OwnerUID, `InitialActivate` `0x007e8820`
resolves it, and `SetPOwner` `0x007e8590` creates the parent's CTCThingOwner
component if needed and inserts the child in its live list. This supports the
strict `Document::ownedDescendants` graph for valid links. Malformed, duplicate,
zero and unresolved IDs remain excluded; FableWin editor-specific movement
rules still come from its own disassembly. See HANDOFF_WORLD_UI. Transform
propagation landed for valid edges, with a saved UI toggle and one-step undo.
Deletion, section moves and selected-group OwnerUID remapping have since landed.

**Vanilla:** CTCThingOwner::OnEditorMove 0x025739b0 applies CMoveThing(delta) and CRotateThing(about the owner's position) to every owned object. It is called from every edit path: DragCarriedThingTo 0x029979ad, Drop 0x0299740e, MoveSelectedThing, RotateAngleXY, SetSelectedThingAngle, AdjustHeight, SetThingZ, CycleZ, SetThingFacingPos and UndoMovement. Deleting runs CTCThingOwner::OnKill 0x02573660 (CKillThing on the owned list). AddSelectedThingToCurrentQuest 0x02035420 also calls SetOwnedObjectsSerialisationSectionName 0x020354e2. The dev FinalAlbion has 2,434 owned things: 740 owned by Buildings, 974 by Objects, 222 by Markers.

**Fix:** 1. **Document helper** (src/leveledit.hpp/.cpp). Add `std::vector<size_t> Document::ownedClosure(const std::vector<size_t>& roots) const`.
   - Build a map from owner UID to the things owned by it in one pass over `file_.things()`: read CTCOwnedEntity.OwnerUID with the same accessor `linksOf` uses (leveledit.cpp:1712).
   - Breadth-first search from the roots' UIDs, with a visited set to guard against cycles.
   - Return only the owned indices that are not already roots. Recursion matches the recursive `CMoveThing`/`CRotateThing` to `OnEditorMove` path.

2. **Gizmo** (editor.cpp:2390-2395). After filling `groupStart_` from the selection, also push `doc_.ownedClosure(selectionIndices())` when `moveOwned_` is on. `groupFrame` already applies the owner's rigid start-to-now transform about its pivot, which is `CMoveThing(delta)` plus `CRotateThing` about the owner position. Make one change: on a scale drag (`gizmoOp_==3`), skip the owned extras. Vanilla has no owner scale propagation.

3. **Keyboard and button paths** (editor.cpp).
   - `moveSelected` (905): append the owned closure to the loop, deduped.
   - `rotateSelected` (918): turn it into a batch. Compute the primary's new frame, then for each extra (selection plus owned closure) rotate its position about the primary's position and its forward about Z by the same angle. Reuse the `groupFrame` math by setting `gizmoStart_`/`gizmoFrame_` temporarily, or add a static `rigidDelta(start, now, extra)` helper extracted from `groupFrame`.
   - `snapSelectedToGround` (1180): take `dz = ground - f.pos[2]` and apply `+dz` to the owned closure. Vanilla's `SetThingZ`/`AdjustHeight` also pass the delta to `OnEditorMove`; it does not snap each owned thing on its own.
   - Wrap each path in `beginBatch`/`endBatch` so it is one undo step. Undo already restores a snapshot, so the vanilla `UndoMovement` behaviour is covered.

4. **Delete** (`deleteSelected`, editor.cpp:1214). Compute the owned closure of the selection. If it is non-empty, show a small modal: "Also delete N owned things (furniture, doors, camera points)?" with Delete all (default, vanilla `CTCThingOwner::OnKill` 0x02573660 kills them), Only the selection, and Cancel. If "Only the selection" is chosen, clear their CTCOwnedEntity.OwnerUID (set it to 0) so no LINK warnings are left behind. Then remove all chosen indices in descending order inside one batch. Register the modal buttons with `auto_` for AUTOMATION.md.

5. **Sections** (leveledit.cpp:1799/1827). Mirror `SetOwnedObjectsSerialisationSectionName` 0x2573890, as `AddSelectedThingToCurrentQuest` does at 0x20354e2.
   - Add `moveToSection(const std::vector<size_t>&, name)`, or have the UI call it per UID. Resolve by UID each time, because each move re-inserts the block and shifts indices.
   - `setDayNight`: do the same for the owned closure inside its existing batch.
   - editor.cpp:376: pass the selection plus the owned closure, then reselect the primary by UID.

6. **Placement from the palette or paste.** Vanilla `PaintInputPlaceThingAt` also calls `OnEditorMove` and `SetOwnedObjectsSerialisationSectionName`. Forge's richer verbatim duplication now clears a single copy's OwnerUID and reconnects copied parent-child selections. Full native clone-from-definition behavior remains separate.

7. **UI toggle.** Add `bool moveOwned_ = true;` in app.hpp and a checkbox "Move owned things with it" in the Objects-tab transform card near "Drop to ground" (editor.cpp:~2919), registered with `auto_`. When the selection owns things, show a muted hint "+N owned".

8. **Test.** In the forgecore/leveledit tests, load a .tng with an owner and 2 owned things (one nested owner). Move, rotate and delete the owner, and check the owned positions, the rotation about the owner, and that no LINK issue is raised.

Effort: M. Value: high.

#### No click-to-place and no drag-on-ground carry: new things go to the view centre and moving needs the axis gizmo
*weakness, effort M, value high*

2026-09-30: pointer placement and ground carry implemented for unlocked primary
and selected extras. The native UI script covers Shift+click, ground offsets,
fixed height, grouped undo and Escape cancellation. Ctrl+Shift+drag clones and
carries a thing or selected group; Ctrl+D carries a new copy until ground click
or Escape. Both have one-step undo. Valid owned descendants now follow carried
parents through the shared transform path.

**Vanilla:** Shift+LMB places the chosen def at the pointed ground position (PaintInputPlaceThingAt 0x02994490, from ProcessInput 0x0294e80d). LMB picks up the nearest thing (PaintInputPickUpNearestThing 0x02996270), and dragging carries it over the terrain (DragCarriedThingTo 0x02997650: Z = GetGroundSizeZAt + the carried height offset at +0xb18, so it keeps its height above ground). Release drops it (0x02996e10), and Esc restores the start position (PaintInputUndoMovementOfSelectedThing 0x02996000). C+click clones the nearest thing and carries the clone (PaintInputCloneNearestThing 0x02996750). Ctrl+click samples the ground height into the Height box.

**Fix:** 1. Shared helper. Add `bool App::groundUnderCursor(float u, float v, float out[3])` to editor.cpp next to pickAt. It calls renderer_.screenRay, then renderer_.rayTerrain. On a hit it sets out = {hit[0], -hit[2], doc_.groundHeight(x,y).value_or(-hit[1]... use hit height)}, using the Fable-axis convention from terrainInput at editor.cpp:1599.

2. Click-to-place (vanilla 0x02994490).
   - Split placeDefinition into `placeDefinitionAt(def, const float pos[3], scriptName)`. Keep `placeDefinition(def)` as a wrapper that passes camera_.focus, so the buttons, presets and automation `place` do not change.
   - The body from editor.cpp:1430-1459 stays as it is: owner through ownerFor, fixed height through constantPlacementHeight, and the facing modes.
   - In app.cpp handleViewportInput, at the release branch (1767-1771): if editMode_, io.KeyShift, !placeDef_.empty(), the tool is Select or Move (gizmoOp_ != 4) and groundUnderCursor succeeds, call placeDefinitionAt instead of pickAt.
   - Update the hint at editor.cpp:2516, the palette tooltip at 3410 and the F1 cheat-sheet: 'Shift+click the ground to place'. Add an automation verb `place_at u v` in automation.cpp next to `pick` (408).

3. Carry a thing over the ground (vanilla PickUp 0x02996270, Drag 0x02997650, Drop 0x02996e10).
   - New state in app.hpp: `bool carrying_; editor::Frame carryStart_; float carryOffsetZ_; float carryGrab[2]; std::vector<std::pair<int,editor::Frame>> carryGroup_`.
   - On LMB press in the viewport, with no gizmo hover or use and gizmoOp_ being 0 (Select) or 1 (Move), do a mesh pick with renderer_.pick. If it hits the current selected thing, or one of the extras, arm a carry instead of the camera turn: the drag branch at app.cpp:1763 must skip the camera while a carry is armed.
   - Once the drag passes 4px (the same threshold as the click), start the carry:
     - carryStart_ = frame; carryOffsetZ_ = frame.pos[2] - groundHeight(frame.xy) (vanilla +0xb18);
     - record the XY offset between the grab ground-hit and the thing, so it does not jump to the cursor;
     - snapshot the extras' start frames the way drawGizmo does (editor.cpp:2390-2395).
   - Each frame: new XY = groundUnderCursor + grab offset; Z = groundHeight(newXY) + carryOffsetZ_. Preview with applyFrame for the primary and for each extra. An extra gets the same XY delta, with its own Z re-seated to its own ground plus its own start offset.
   - On release: doc_.beginBatch(); commitFrame(primary) and doc_.setFrame for the extras; doc_.endBatch(). That is one undo step, the same as the gizmo commit at editor.cpp:2426-2435.
   - Esc during a carry (vanilla 0x02996000) re-applies carryStart_ and the group starts with applyFrame, then ends the carry without committing. Put the check before the Esc-deselect at editor.cpp:2377.
   - Honour placeFixedHeight_: when it is set, Z = constantPlacementHeight(placeHeight_, ground), the same as placement.

4. Clone and carry (vanilla CloneNearestThing 0x02996750).
   - Alt is taken by camera orbit, so the trigger is Ctrl+Shift+drag on a selected thing: duplicateSelected() first, then start the carry on the copies. duplicateSelected already reselects the copies (editor.cpp:1207-1209).
   - Ctrl+D also enters carry mode, following the cursor on the ground; the next click drops the copy and Esc cancels it. The copy then stops landing on top of the original.

5. Owned things. When the owner-follow finding lands, carryGroup_ should also include the owned or contained children, as the gizmo group does.

Files: gui/app.hpp (state and the two new methods), gui/app.cpp:1760-1772 (input routing), gui/editor.cpp (placeDefinitionAt, groundUnderCursor, the carry update and commit, Esc at 2376-2377, hints at 2516/3410), gui/automation.cpp (place_at and carry test verbs), docs/AUTOMATION.md.

Effort M. Value high.

#### Omitted properties and definition-declared components
*partially implemented 2026-09-29; absent components and coupled controls remain*

Known serialized scalar fields on existing unique CTCDoor,
CTCSearchableContainer, CTCDRegionExit, CTCCreatureGenerator,
CTCExplodingObject, CTCStockItem and CTCInfoDisplay blocks can now be set when
absent, or reset by removing their explicit value. The inspector labels these
as unset with unknown effective values; it never guesses a default. Bool, signed
integer and finite float validation follows serialization evidence and recovered
ranges. Generator/explosion radius fields serialize floats despite integer
widgets in the native dialog. Existing unknown bytes and undo/redo are preserved;
ambiguous duplicate blocks/keys are refused.

Validation: 43 core checks; native `tests/ui/component_overrides.txt` exercises
real bool/numeric controls, rejected out-of-range values, exact undo, and an
actually omitted retail ReversedOnMiniMap field in MayorsHouseHallway. Screenshot:
`walkthrough/w17_component_overrides.png`.

Remaining work: decode definition-declared components with a checked shared
CThingComponentSet reader, including parameter/flag records and original name
offsets; then add absent blocks only where supported by definition/native evidence.
The navigation helper's arbitrary payload scan is not a sufficient writer basis.
Do not append all General metadata rows to every thing: some describe specialized
classes. The table describes widget kinds/ranges, not engine default values.

CTCLight's coordinated Overridden/Colour/radius/flicker fields and Script>Usable's
component creation/removal remain separate work requiring their complete native
serialization contract. The earlier proposed DoorTriggerType fixture was invalid
(the recovered CTCDoor table only has Open), and there are no CTCChest table rows
to synthesize. Native CreateGenericVars 0x02918380 builds from live components;
RefreshThingFromGenericVars 0x029197b0 writes those effective values. Forge does
not yet instantiate live components or resolve their effective defaults.

#### Moving a creature leaves its world-space InitialPos behind
*Fixed 2026-09-29; preservation policy verified separately from native drag.*

`writeFrame` now updates each existing InitialPos axis when position changes,
using map-local XY plus the destination world's origin and unchanged Z. Paste
forces rebasing even when local coordinates match the source. Generic navigator
PositionX/Y/Z edits use the same synchronization in one undo step. Pure rotation
or scale preserves separately authored initial positions; missing axes remain
absent. Raw-text document loading clears any previous world origin.

Native evidence correction: FableWin drag02997650 sets physics position, checks
creature type1/2, then calls SetInitialPos through017ff38c at02997a07. Setter02997b50
copies XYZ into creature+0x138. Drop02996e10 has no direct setter call in its body;
the earlier claim that both drag and drop call it was incorrect. Forge applies
the consistent saved-position rule to all actual position changes, including
Drop to ground. Native drag has no position-equality gate; preserving independent
InitialPos during pure rotate/scale is an intentional Forge preservation policy.

`tests/test_creatureframe.cpp` covers nonzero origins, cross-map paste at equal
local coordinates, partial/absent fields, raw-load origin reset, property edits,
and byte-exact undo/redo. Native evidence is retained in
`build/creature-initialpos-native.txt` (SHA256 and scoped disassembly).

#### Container contents (Contnrs tab) cannot be added to, removed from, or filled on an empty chest
*gap, effort S, value high*

**Implemented 2026-09-29:** a searchable, grouped definition picker adds/replaces
items; remove compacts indices. Empty existing container and generator blocks
remain editable after the last entry is removed. Works in the sidebar and floating
Properties window, one undo step per edit. Duplicate blocks, duplicate/gapped
indices and non-string entries retain raw fields and disable structured mutation.
CPU tests prove unrelated-field preservation and byte-identical undo; native
`container_lists.txt` exercises actual typing/clicks with retail apple definitions
and creature families. The available definition metadata does not expose the
native template flag, so the picker still includes templates. Components absent
from the TNG remain the separate default-component gap above.


**Vanilla:** The Contnrs tab lists a container's items and adds or removes them: a THING_GROUP list with a NONE entry (InitContainerGroupListBox 0x02921020), then the non-template defs of that group (InitContainerListBox 0x02920840), then ContainersAddObject 0x0291a9f0 (CTCContainer::InsertItem with the def's global index) and ContainersRemoveObject 0x0291b330 (RemoveItem). The dev FinalAlbion has 501 ContainerContents[] entries across CTCChest (119), CTCSearchableContainer (135) and CTCContainerRewardHero (213); most RewardHero blocks are empty.

**Fix:** 1. src/leveledit.hpp/.cpp: add `std::vector<std::string> Document::ctcBlocksOf(size_t index) const`. It returns the names in file_.things()[index].ctcBlocks, including empty blocks.
2. gui/editor.cpp drawPropertyGrid: add a small static table of list fields, `struct ListField { const char* ctc; const char* base; std::vector<std::string> defTypes; const char* addLabel; }`:
   - CTCCreatureGenerator and CTCDCreatureGenerator: CreatureFamilies, {CREATURE_GENERATION_FAMILY}, "+ add a creature family".
   - CTCChest, CTCSearchableContainer, CTCContainerRewardHero and CTCOnDieContainer: ContainerContents, {OBJECT}, "+ add an item".
   Replace `isFamily` with a lookup: the key starts with `base[` in a matching ctc and is K::String. Reuse the existing combo and x-button code with the table's base and def types. For ContainerContents, build the combo from defList_ (ctx_.groupedDefinitions, sorted type/group/name) filtered to type OBJECT, shown as a THING_GROUP submenu (BeginMenu per group, plus "(no group)") with a filter InputText at the top. This mirrors vanilla InitContainerGroupListBox followed by InitContainerListBox. Skip template defs if Context exposes that flag; otherwise note the deviation.
3. Generalise the lastOfGroup '+ add' combo (editor.cpp:536-547) to use the table: `doc_.addListEntry(idx, r.ctc, lf.base, "\"" + name + "\"")`. Log "container: <def> added" or "removed".
4. Empty blocks: after the row loop, for each name in doc_.ctcBlocksOf(idx) that has a ListField entry and no row in `rows`, draw its CollapsingHeader (open by default, captioned "Contents" for container CTCs as on vanilla's Contnrs tab) with an empty-state hint ("empty: the game's own table" for RewardHero) and the same '+ add' combo. Optionally caption the ContainerContents group "Contents" in the non-empty case too.
5. Break out of the loop after an add or remove, as the existing remove path does, because the rows change.
6. Tests: extend tests/test_export.cpp with a ContainerContents add into an empty CTCContainerRewardHero block (first entry leads the block) and a remove-from-middle renumber case. These mirror the CreatureFamilies test at test_export.cpp:1091.
7. Optional: an automation verb in docs/AUTOMATION.md (`container_add <def>` / `container_remove <n>`) plus auto_.registerWidget ids for the combos.
Effort S (about 60-90 lines of GUI plus one accessor). Value high: 501 ContainerContents entries in the dev FinalAlbion, and there is currently no way to stock an empty chest or reward.

#### No camera-point, shape or light-from-camera editing (CameraPoint, Shapes and Light tabs)
*gap, effort L, value med*

**Vanilla:** The CameraPoint tab sets look-at and end positions, adds a track or spline, edits and previews the spline, and has 'get camera from clipboard' (c:\cameras.bin, read in 0x02052eb0's neighbour at 0x02053566). Q with a modifier places a camera point at the current camera (ProcessInput 0x0294f31e-0x0294f3f5 via CTCCameraPointScripted/FixedPoint). The Shapes tab and modes 22/23 (CEditInputProcessEditShapes, CTCShapeManager) edit polygon or line shapes. The Light tab sets position and direction from the camera. The dev data has 2,493 CTCDCameraPoint, 1,382 CTCCameraPointScripted (22,186 KeyCameras[] entries) and 73 CTCShapeManager (1,263 Shape[] entries).

**Fix:** Stage 0 (S), a prerequisite: draw meshless camera-point and shape-manager things.
- In renderer.cpp, draw a small frustum or camera glyph at the frame of any thing that has a CTCDCameraPoint or CTCCameraPoint* block, and treat it as pickable in the same way the track-node dots are picked (editor.cpp:866).
- Without this, stages 1-3 have nothing to click.

Stage 1 (S): "Set from view".
- Add an App::thingFromView(idx) helper in editor.cpp next to setEntranceHere (editor.cpp:3246), using the same conversion: render (x,y,z) to Fable (x,-z,y), with the forward taken from camera_.dir as (-d0, d2, -d1)/n.
- Show a "Set from view" button in drawPropertyGrid when the selected thing has one of these CTC groups:
  - CTCCameraPointFixedPoint, GeneralCase or Scripted: write the camera eye into PositionX/Y/Z and the forward into RHSetForward (up stays 0,0,1).
  - CTCCameraPointScripted*: also write the current FOV into FOV.
  - CTCLight / CTCSpotLight: write position and forward. This is the vanilla Light tab's "position/direction from camera".
- Wrap each click in one undo step through the existing Document edit path in src/leveledit.cpp.
- Before landing, confirm the FOV units against retail. Arena.tng stores FOV 0.085847, so check whether it is radians or a scale by reading CTCCameraPointDefinitionBase::GetFOV 0x022001f0 and ConsoleEditSetCameraFOV 0x02053730.

Stage 2 (M): shape editor.
- Add a forge::shape parser/writer in libs/forgecore for the CTCShapeManager block: IsCoordsRelativeToMap; NumShapes; Shape[i].Type as SHAPE_TYPE_CLOSED or LINE; Shape[i].size(); Shape[i].pos[j].X/Y/Z. It must round-trip the retail text exactly, and needs a unit test on Arena.tng:4054-4085.
- In the Objects tab, when a CTCShapeManager thing is selected, draw each shape as a polyline, closed or open as its Type says, at map-relative coordinates.
- Make vertices draggable with ground snap (doc_.groundHeight). Add "Add point after", "Remove point", "Add shape" and a Closed/Line toggle.
- Write the result back and keep NumShapes and size() in sync.
- Port the drag rule from CTCShapeManager::EditUpdateDisplacement 0x025d7900 and the draw colours from DrawShapes 0x025d6f60.

Stage 3 (L): scripted camera points and splines.
- Parse KeyCameras[i] into a small table editor in the CameraPoint group, with Duration, PauseTime, FOV, RollAngle, ShuttleSpeed and Event per key. Position and LookDirection are relative to CoordBase when UsingRelativeCoords is set.
- Add three buttons:
  - "Add key from view" appends a key with the camera eye and forward. This is the vanilla EditSetPointForEditorPointAcquisition 0x025ce5c0 path.
  - "Set look-at thing" follows EditSetThingForEditorSetThing 0x025ce8a0.
  - "Preview" drives camera_ through the keys using TimeToPlay and Tension. Reuse the preview loop at editor.cpp:703-738, and take the spline evaluation from CTCCameraPointScriptedSpline's runtime update rather than forge/trackpath's linear arc-length sampler. Find that update function by xref from the 0x0206e720 dynamic cast before porting.
- Draw the key positions as dots joined by the spline, and make them pickable like the track nodes.
- Leave out the c:\cameras.bin clipboard import. "Add key from view" replaces it.

#### Target-side attachment modes
*mostly implemented; remaining: batched undo and additional native parity checks*

2026-09-30: the CTCDRegionExit entrance field is exposed. A source with both
exit and scripted-hook components shows one row and writes both under one undo.
Parent-anchored attachment modes, incoming links, visible link lines, owner CTC
addition/removal and a persistent viewport picker are implemented. Core tests
and `tests/ui/attach_picker.txt` cover attachment, detachment and exact undo.
The documented mode eligibility is still an approximation where it depends on
SimBuildingDef flags, and multiple clicks still create separate undo steps.
The MayorsHouseHallway region entrance picker now passes a retail GUI test,
including serialization and exact undo. The selected exit has only
CTCDRegionExit; the two-block case remains covered by a synthetic core fixture.

**Vanilla:** The anchor is the selected parent. GetViableAttachModesForThing 0x020342f0 offers 'Attach objects' (a thing owner), 'Attach people who live here / work here' (a building with a SimBuildingDef), 'Attach things to village', 'Attach triggers to this receptor', 'Select region entrance to connect to' and 'Select target to calculate route to'. Each click then toggles the clicked thing (ToggleThingAttachment 0x020326e0). For owned-by it adds the CTCOwnedEntity TC when missing (AddTC 0x02032893) and removes it when untoggled (RemoveTC 0x02032864). For region exits it sets both CTCDRegionExit::SetPEntranceConnectedTo (0x0203391e) and CTCActionUseScriptedHook's (0x02033979). Lines are drawn for all attachments (DrawAttachModeLines 0x02048230). Spouse and parent fields are absent in the scanned dev .tng data, but CThingAICreature::Save writes them conditionally when set.

**Fix:** 1. Link table (src/leveledit.cpp:1368)
- Add {"CTCDRegionExit","EntranceConnectedToUID","Region exit to entrance","a region entrance"}.
- In setLink, when field=="EntranceConnectedToUID", write the value into every block the thing has out of CTCDRegionExit and CTCActionUseScriptedHook, under one pushUndo. This mirrors 0x0203391e and 0x02033979.
- In linksOf, fold the two into a single row so the card does not show two entries.

2. Document API (src/leveledit.hpp/.cpp)
- Add `enum class AttachMode { Owned, LivesIn, WorksIn, Village, Receptor, RegionEntrance, RouteTarget }`.
- Add `std::vector<AttachMode> viableAttachModes(size_t anchor)`, ported from GetViableAttachModesForThing 0x020342f0:
  - Owned: always offered.
  - LivesIn / WorksIn: the anchor's type is Building (the SimBuildingDef check if cheap).
  - Village: the anchor has CTCVillage.
  - Receptor: the anchor has a CTCActivationReceptor* block.
  - RegionEntrance: the anchor has CTCDRegionExit or CTCActionUseScriptedHook.
  - RouteTarget: the anchor has CTCPreCalculatedNavigationRoute.
  - Spouse and parent are skipped because they are not serialised in the .tng.
- Add `bool toggleAttachment(size_t anchor, AttachMode m, size_t clicked, std::string& err)`, following ToggleThingAttachment 0x020326e0:
  - Owned: if clicked.CTCOwnedEntity.OwnerUID==uid(anchor), remove the whole CTCOwnedEntity block (RemoveTC 0x02032864). Otherwise add the block when missing (AddTC 0x02032893) with the default fields taken from a retail CTCOwnedEntity block, then set OwnerUID=uid(anchor).
  - LivesIn / WorksIn: the clicked thing must be an AICreature. If its HomeBuildingUID (or WorkBuildingUID) equals the anchor's uid, set it to 0. If it is 0, set it to the anchor. If it is another building, return err "already lives in X" (vanilla asserts GetHomeBuilding()==NULL).
  - Village: the clicked thing must already have CTCVillageMember, otherwise return err (vanilla asserts; do not add the block). Toggle VillageUID between the anchor's uid and 0.
  - Receptor: the clicked thing must have CTCActivationTrigger, otherwise return err. Toggle ReceptorUID.
  - RegionEntrance: the clicked thing must have CTCDRegionEntrance. Toggle the anchor's own EntranceConnectedToUID in both blocks (0x020338f4-0x0203398c).
  - RouteTarget: toggle the anchor's CTCPreCalculatedNavigationRoute.ThingToCalculateRouteToUID.
  - tng::File needs a new addCtcBlock(thingIndex, name, defaultProps) and removeCtcBlock(thingIndex, name). Put them in libs/forgecore/src/tng.cpp next to removeCtcProperty (line 298).
- Add `std::vector<std::pair<size_t,Link>> incomingLinks(size_t anchor)`. It scans every thing's linksOf for target==uid(anchor), and is cached per revision_.

3. UI (gui/editor.cpp, Selection card near 2951)
- Add an "Attach" row with one toggle chip per viableAttachModes(selected), labelled with the vanilla strings: "Attach objects", "Attach people who live here", "Attach people who work here", "Attach things to village", "Attach triggers to this receptor", "Select region entrance to connect to", "Select target to calculate route to".
- The active mode is kept in a new App::attach_ {mode, anchor, batchOpen}. While it is active, pickAt (before the linkPick_ branch at 875) calls toggleAttachment(anchor, mode, target) and does not change the selection. Errors go to pushLog.
- Coalesce the toggles into one undo step. Either add Document::beginBatch()/endBatch() so that pushUndo only fires once, or snapshot at mode start and drop the intermediate snapshots.
- Esc (editor.cpp:2376) or clicking the chip again ends the mode.
- Show a HUD caption "Attaching things to <label> (Esc to stop)", matching vanilla's "Attaching Things" mode and CAttachingThingsDialog.
- Register the chips with auto_.registerWidget for AUTOMATION.md.

4. Lines (drawLinkLines, editor.cpp:834)
- Also iterate incomingLinks(selected) and draw target->selected lines in the same colours, as DrawAttachModeLines 0x02048230 does.
- While an attach mode is active, draw only that mode's links, so the user sees what each click toggles.

Effort M (about a day: tng block add/remove, document API with undo batching, UI mode and lines). Value medium.

#### The palette and placement cover only OBJECT, BUILDING and CREATURE: no markers, holy sites, switches or camera points
*gap, effort M, value med*

**Vanilla:** The Things tree is built per thing type (CThingDialog ctor 0x028f2e90, GetSelectedThingType 0x028f7020, GetSelectedThingDriverType 0x028f7380). The types include THING_TYPE_MARKER, HOLY_SITE, SWITCH, PHYSICAL_SWITCH, VILLAGE and TRACK_NODE, and driver types such as REGION_ENTRANCE/EXIT, NAVIGATION_SEED, CAMERA_POINT and PARTICLE_EMITTER. PaintInputPlaceThingAt builds the thing through ConstructFromParams, so every def component is present.

**Fix:** 1. **Palette (gui/editor.cpp:1252, and the Objects tab list at ~3091):** use the same type set as `forge def-groups` (src/cli/export.cpp:213): OBJECT, BUILDING, CREATURE, MARKER, HOLY_SITE, SWITCH, PHYSICAL_SWITCH, VILLAGE, THING. Put that set in one shared constant so the CLI and GUI can't drift apart. groupedDefinitions (src/terrainexport.cpp:556) already groups by def type and then THING_GROUP, which matches the vanilla tree (CThingDialog 0x028f2e90). Leave NOISE out; noise defs are not placed as things.

2. **Map the def type to the NewThing type:** add `forge::thingplacer::thingTypeForDefClass(defClass)` in libs/forgecore/src/thingplacer.cpp:
   - OBJECT â†’ "Object"
   - BUILDING â†’ "Building"
   - MARKER â†’ "Marker"
   - HOLY_SITE â†’ "Holy Site"
   - SWITCH and PHYSICAL_SWITCH â†’ "Switch" (retail has 5 "NewThing Switch"; check a PHYSICAL_SWITCH instance's header in the corpus before fixing the string)
   - THING â†’ "Thing"
   - VILLAGE â†’ keep routing to Document::placeVillage
   - CREATURE â†’ keep placeCreature

   In placeDefinition (gui/editor.cpp:1431), replace the BUILDING_ prefix test with a lookup of the def's bin definition string (GroupedDefinition::type, or a new Context::definitionClassOf(name)). Stop using the name prefix.

3. **Template block for component-heavy types:** HOLY_SITE, SWITCH and some MARKER defs carry CTC blocks that the generic serialize() (thingplacer.cpp:197-254) does not write. Vanilla gets these from ConstructFromParams (0x02994490). Add `Document::placeFromTemplate(pos, fwd, def, scriptName, player)` in src/leveledit.cpp:
   - Find the first thing with the same DefinitionType, first in the open level and then in the retail corpus the levelstore/mods code already parses.
   - Copy its block text (file_.thingBlockText) and insert it.
   - Reset UID (nextUid), ScriptName (NULL or the given name), ScriptData "NULL", Position and RHSetForward (plus RHSetUp 0,0,1), and Player (clamped as in inventory line 550; only Object, Building, Village, Holy Site and Switch keep an owner, others get -1).
   - Clear link/attachment and container properties (CTCOwnedEntity / container contents / any UID-reference fields) so the copy has no dangling references.
   - Reuse the undo and section pattern from Document::duplicate (leveledit.cpp:1842) and Document::place (intoPlacementSection).
   - If no template exists, fall back to thingplacer::place with the mapped thingType.

4. **Glyph:** placeDefinition already warns when a def has no mesh (graphicModelId code<0). Draw a type glyph for meshless markers, holy sites and switches so they stay visible and selectable (this depends on the separate no-mesh glyph item).

5. **Test:** add a forgecore test that places MARKER_*, HOLY_SITE_* and SWITCH_* defs, then re-parses the .tng and checks the NewThing type and that the UID is unique.

Effort: M. Value: medium.

#### Locked In Place protection and controls
*implemented 2026-09-29; existing CTCEditor blocks only*

The document now enforces locks for movement, rotation, scale, direct physics
property edits, deletion and terrain reseating. Missing/empty editor settings
mean unlocked; an existing empty block can receive the flag. Missing blocks are
not invented. Copying and content/owner edits remain allowed. Ctrl+L, the
Properties checkbox and Actions menu apply the primary's toggled state to the
selection in one undo step. Mixed transforms/deletion preserve locked objects;
the Objects list marks them. Locking during a drag cancels the preview/capture.

Native evidence: CTCEditor constructor 0x01b9c8f9 initializes the flag false;
IsLockedInPlace 0x020377c0 reads it, and pickup 0x02996544 refuses locked things.
ToggleLockedStatusOfSelectedThing 0x02037740 acts on the single selected thing;
SetAsLockedInPlace 0x02555a20 also visits live owned children. Forge's group toggle
and scale protection are editor policy; no claim of exact group-toggle parity or
that arbitrary TNG OwnerUID links reproduce the native live child list. The old
proposal to block owner changes was not supported and was not implemented.

Core fixtures cover no-op history, byte-exact undo, mixed locks, copy/paste and
terrain reseating. Native `tests/ui/locked_things.txt` exercises the actual
checkbox/shortcut, protected transforms and mixed selection. Terrain reseating
also synchronizes existing InitialPos fields on unlocked creatures.

#### Surface cycling and direct terrain drop
*visible-surface cycle implemented 2026-09-29; native physics sweep/owner movement remain*

H and the Cycle surfaces below action now search visible placed-object triangles
and terrain. End retains explicit terrain-only drop. Particle proxies (thing=-1),
foliage, selected objects and generated children are excluded; transitive OwnerUID
children are excluded with a cycle guard. Hidden geometry is not a support. Queries
use actual transformed triangles, not bounding-box tops. All results are computed
before a batch of frame edits, preserving locks and creature InitialPos/undo.
Stale object geometry and active gizmo previews refuse the action.

Read-only FableWin disassembly verifies PaintInputCycleThingZOverSurfaces
0x02998ce0 checks locks at0x02998f09; compares abs(groundZ-currentZ) to0.0001
at0x029990d0; uses ground+150 at0x029990df when on terrain, otherwise Z-0.1
at0x029990ed. GetHeightZBelowAt at0x0299916a uses the physics radius and a
CIsNotThing filter. The earlier arbitrary no-hit wrap proposal was not supported.
Native also calls CTCThingOwner::OnEditorMove at0x02999344; Forge's existing
selection gizmo does not implement that cascade, contrary to the earlier backlog.

Forge's central, double-sided render-triangle ray is explicitly approximate:
nonphysical decorations may support objects, invisible physics surfaces do not,
and it lacks the native radius sweep. Explicit owned objects are not moved with
an owner. Ctrl+H now opens the absolute-height control described below.

Tests cover transformed/hidden/proxy/selected-child supports and exact triangle
hits, keyboard/menu operation, elevated retail roof support, locked mixtures,
InitialPos and byte-exact undo. Main native repeated-step test passes roof
Z16.328 to terrain Z8.395 at fixed XY, orientation and scale; locking and selection
regressions pass too (`build/surface-cycle-native-final.log`).

#### Fixed selection height: implemented 2026-09-29

Ctrl+H and Set selection height open a modal for an absolute Z. Each unlocked
selected thing clamps up to its own terrain height, preserving XY/basis/scale.
Existing creature InitialPos is synchronized by setFrame; the group is one undo.
Nonfinite inputs, missing terrain/frames and locked things leave data untouched;
identical heights do not create history or clear redo. Changed selection disables
Apply; active gizmo and terrain strokes refuse opening. Popups block new gizmo
captures behind them.

Native PaintInputSetThingZ 0x02998570 assigns requested Z at0x02998935,
GetGroundSizeZAt/GFLimitLower clamps at0x0299895a and lock check is at0x02998799.
ProcessInput routes H at0x0294f262: Ctrl0x1d or extended0x67 chooses SetThingZ;
Alt0x38 suppresses it. Forge uses the standard Ctrl modifier; extended native
key identity is unverified. Native owner notification at0x02998b31 remains a
separate missing cascade. Evidence uses the FableWin SHA recorded above.

27 core checks pass, including sloped terrain, negative requests, missing ground,
locks, no-op redo preservation and exact batch undo. Native selection_height tests
exercise popup typing, Ctrl+H/menu, terrain clamp, cancel, mixed locks, creature
InitialPos and background gizmo blocking.

#### No keyboard nudge, rotate, height or facing keys for things
*gap, effort S, value med*

2026-09-30: implemented in the Edit viewport. Arrow keys nudge 0.05 (Shift
0.5), Ctrl+arrows set cardinal facing, A faces the pointed ground, brackets
rotate 2 degrees about Z (Shift Y, Alt X), and comma/period or PageDown/PageUp
adjust height 0.2 (Shift 0.01). The existing batch path carries valid owned
descendants and gives one undo step. Retail UI automation checks exact movement,
all three rotation axes, facing and undo; see HANDOFF_WORLD_UI. Ctrl+brackets
remain the panel controls.

**Vanilla:** From the CEditInputProcessEditThings::ProcessInput 0x0294e6b0 constants: N/S/E/W nudge 0.05 units (MoveSelectedThing); Shift+N/S/E/W face 0 / 0.5 / 0.25 / 0.75 turns (SetSelectedThingAngle); [ / ] rotate -/+1/180 turn (2 degrees) about Z, with Ctrl giving YZ and Shift XZ; , / . lower/raise 0.2, or 0.01 with Shift (AdjustSelectedThingHeight); A turns the thing to face the pointer. All respect the lock and carry owned things.

**Fix:** 1. **Where it goes.** Add a thing-transform block in App::editorShortcuts (gui/editor.cpp, after line 2365). Guard it with `!rmb && gizmoOp_ != 4 && selectedThing_ >= 0 && !io.KeyAlt`, the same guard the RMB fly camera already relies on. Use `IsKeyPressed(key, true)` so that holding a key repeats.

2. **Nudge (vanilla MoveSelectedThing 0x02999580, step 0.05).**
   - Left/Right/Up/Down call moveSelected(âˆ“0.05, 0, 0) and moveSelected(0, Â±0.05, 0) in world X/Y. Match vanilla's N=+Y / E=+X convention.
   - Shift multiplies the step by 10, which fits vanilla's Shift Ã—10 on move speed.
   - moveSelected already batches the whole selection into one undo step.

3. **Facing (vanilla Shift+N/S/E/W â†’ SetSelectedThingAngle 0 / 0.5 / 0.25 / 0.75 turn).**
   - Add `App::setSelectedFacing(float turns)`. It sets each selected thing's forward to (sin, cos, 0) of turnsÂ·2Ï€ in the XY plane, keeps up = (0,0,1) and keeps the thing's scale. Check vanilla's zero-angle axis by reading the fld sites at 0x0294efde-0x0294f21a.
   - Bind it to Ctrl+arrows, not Shift, because Shift is the coarse nudge. Wrap it in doc_.beginBatch()/endBatch().

4. **Rotate (vanilla [ / ] Â±1/180 turn, i.e. Â±2 degrees, at 0x0294f556-0x0294f692; plain = XY, Ctrl = YZ, Shift = XZ).**
   - Generalise rotateSelected to `rotateSelected(float degrees, int axis)`: 0 = world Z (yaw), 1 = X (YZ plane), 2 = Y (XZ plane).
   - Build the rotation matrix R about the primary's pivot. Apply it to every selectionIndices() frame: rotate each forward/up by R and each pos about the primary's pos, which is the same rigid transform groupFrame uses (editor.cpp:2442-2454).
   - Wrap it in one beginBatch/endBatch.
   - Bind [ / ] only when gizmoOp_ != 4, so the brush-radius binding keeps working in terrain mode. Ctrl+[ / ] still toggles the explorer and actions panels (app.cpp:1426-1427), so either move those panel toggles to Ctrl+Shift or use Alt+[ / ] for YZ.

5. **Height (vanilla , / . = âˆ“0.2, Shift âˆ“0.01, AdjustSelectedThingHeight 0x02997e00).**
   - Comma/Period call moveSelected(0, 0, âˆ“0.2), or âˆ“0.01 with Shift. Add PgDn/PgUp as aliases.

6. **Discoverability.** Add the key list to the Objects-tab tool hint and to the shortcut table in app.cpp:267. Expose the keys in automation, alongside rotateSelected at automation.cpp:422-424, as a `rotateSelected axis` argument.

Effort: S. Value: medium.

#### Deleting leaves other things' links pointing at the removed UID
*weakness, effort S, value med*

**Status (2026-09-30):** Implemented for unambiguous incoming link fields.
`remove` and `removeWithOwned` clear them within the delete undo step, including
both region-exit fields. Track-node removal repairs surviving chains. Duplicate
target UIDs and repeated/malformed fields are deliberately left unchanged.
Core tests cover links, track validation and exact undo; delete, lock, track
preview and editor UI regressions pass. The Activity log reports cleared fields.

**Vanilla:** Links are CIntelligentPointer, so a deleted thing's referrers go null at once. PaintInputDeleteSelectedThing 0x0299bda0 also runs UpdateRegionExitConnections 0x020457c0 (0x0299c750/0x0299c7a2) to keep entrance/exit pairs consistent. Saved files therefore never carry stale UIDs.

**Fix:** 1) Add a private helper in src/leveledit.cpp, `size_t Document::clearLinksTo(uint64_t uid, size_t except)`. It loops over every thing index i != except and over linksOf(i), and for each link with l.target == uid it writes "0" directly. For a CTC link (l.ctc non-empty) it uses file_.setCtcProperty(i, l.ctc, l.field, "0"), and otherwise file_.setThingProperty(i, l.field, "0"). It writes directly rather than through setLink so it does not push its own undo, and it returns how many links it cleared. Declare it in src/leveledit.hpp. 2) Rewrite Document::remove(size_t index) as follows. Bounds-check, then beginBatch() and pushUndo(), so that one delete, or a whole multi-select deleteSelected (which already runs in a batch), is a single undo step. Get const uint64_t uid = uidOf(index). If isTrackNode(index), first detach it from its chain using the body of unlinkTrackNode (leveledit.cpp:1597-1620), which relinks the before/after parts, calls fixTrackEnds to fix the Start/End flags, and runs the INVALID/TrackTempName naming. Move that body into an internal unlinkTrackNodeNoUndo(node) that unlinkTrackNode also calls, so the existing Tracks-card behaviour stays the same. Next, const size_t n = clearLinksTo(uid, index). Then file_.removeThing(index), ++revision_ and endBatch(). Keep the n value where the UI can read it, e.g. make remove return size_t or store lastClearedLinks_. 3) In App::deleteSelected (gui/editor.cpp:1214-1230), add up the cleared counts. When the total is non-zero, log "removed <what>; cleared N links that pointed at it" (level 0) so the user can see that other things changed. Note that deleteSelected removes indices from highest to lowest, so indices shift only above the removed one. Since clearLinksTo works by UID, that ordering is safe. 4) Region pairs, matching UpdateRegionExitConnections 0x020457c0: deleting a CTCDRegionEntrance zeroes EntranceConnectedToUID on every hook that pointed at it, which clearLinksTo already covers. Deleting a region-exit hook needs nothing on the entrance side, because the link is stored one way on the exit (kLinkKinds line 1374). 5) Test in tests/: build a map with A (CTCVillageMember VillageUID = B's UID) and a 3-node track, delete B and the middle track node, then check that A's VillageUID is 0, that validateMap reports no LINK/TRACK problems, and that a single undo brings back both the deleted things and the original link values.

#### No 'delete all things in an area'
*gap, effort S, value med*

2026-09-30: implemented in the Copy terrain tool. Its retained ground rectangle
shows a count and Delete button; Del uses the rectangle while this tool is
active. The document enumerates all framed things in the normalized inclusive
rectangle, skips locked roots, detaches owned descendants outside it and clears
incoming links through the existing grouped delete path. Core checks cover
locked, owned and linked things with exact one-step undo. The retail
MayorsHouseHallway UI check deletes its exit by button and key, then undoes both.

**Vanilla:** In Copy and paste mode, Delete with a region selected calls PaintInputDeleteAllThingsInArea 0x0299ca70 (from CEditInputProcessCopyPaste::ProcessInput 0x02952eea). It removes every unlocked thing in the box and runs UpdateRegionExitConnections.

**Fix:** 1) Document (src/leveledit.hpp/.cpp): split the thing loop in copyTerrain (leveledit.cpp:787-799) into `std::vector<size_t> Document::thingsInRect(int x0,int y0,int x1,int y1) const`. It should normalise and clamp the corners exactly as copyTerrain does (lines 764-767), then test frameOf(i).pos[0..1] against the box. Have copyTerrain use it. Add `size_t Document::removeThingsInRect(int x0,int y0,int x1,int y1, size_t* skipped)`. It calls thingsInRect, sorts the indices in descending order, and removes them inside a single beginBatch()/endBatch(), which is the equivalent of vanilla's Backup (0x02045590) as one undo step. Remove each thing with the same doc_.remove() path deleteSelected uses, so any link or script-brush cleanup there also applies. That cleanup stands in for RemoveThingFromScriptBrushes (0x02042850). If FableForge ever models the editor-only LockedInPlace flag (CTCEditor::IsLockedInPlace 0x020377c0), skip those things and count them in *skipped. Until then, leave a TODO and skip nothing. 2) App (gui/app.hpp, gui/editor.cpp): store the last dragged rectangle in a member, e.g. `int clipRect_[4]; bool clipRectValid_`, when the mode 14 drag is released (editor.cpp:1666-1670). Draw it as an overlay while it is valid. In the mode 14 panel (editor.cpp:2582-2590), add a theme::dangerButton "Delete N objects inside (Del)". Get N from thingsInRect and refresh it when the rectangle or the document changes. In mode 14, bind ImGuiKey_Delete to this action when clipRectValid_ is set, the same gate as vanilla's [this+0x74] check. Keep the existing selection-delete binding at 2367 for other modes, and clear the selection afterwards. Log "removed N objects (one undo step)". 3) Optional modern extra: add a "Select objects inside" button that fills extraUids_/selectedThing_ from thingsInRect, so the usual move, duplicate and delete apply to the result. 4) Automation (gui/automation.cpp near line 339): add `clip_delete x0 y0 x1 y1`, which calls removeThingsInRect. Effort S. Value med.

#### No trigger or radius circles for switches, exits, spawners or lights
*gap, effort S, value med*

2026-09-30: implemented for selected things with saved, positive numeric Radius
fields. Forge draws terrain-following circles, a labelled arc and an elevation
line from the selected point to the circle centre. TriggerRadius uses the vanilla
teal; other radius fields have distinct colours. MayorsHouseHallway's saved
CTCDRegionExit Radius is exercised by `tests/ui/radius_rings.txt`; entrance
selection without a saved radius draws none. Read-only game access; no map write.

**Vanilla:** CEditControlCentre::DrawSwitchRadius 0x02049fe0, called from PreparePrimitivesForEngine 0x02046f27, draws the selected switch's TriggerRadius as a ground circle through DrawCircle 0x0184fa08 (via a thunk). Radius-bearing properties are edited in the same dialog: region-exit Radius/MessageRadius, CreatureGen GenerationRadius/SelfTriggerRadius, Light Inner/OuterRadius, StealableItemLocation radii, and so on.

**Fix:** 1) Add a declaration next to drawLinkLines in gui/app.hpp:434: `void drawRadiusRings(const ImVec2& origin, const ImVec2& size);`. Call it in gui/app.cpp right after drawLinkLines(origin,size) (line 1820).
2) Implement it in gui/editor.cpp next to drawLinkLines (around line 858). Return early unless editMode_, documentLoaded() and selectedThing_>=0 all hold. Get the frame with doc_.frameOf(selectedThing_, f). Go through doc_.propertiesOf(selectedThing_) and keep the rows where kind is Float or Int and the key contains "Radius" (case-insensitive). That covers the thing-level TriggerRadius, which is the vanilla switch ring, plus CTCDRegionExit Radius/MessageRadius, CTCCreatureGenerator/CTCDCreatureGenerator GenerationRadius/SelfTriggerRadius, CTCLight/CTCSpotLight Inner/OuterRadius, CTCExplodingObject Radius/TriggerRadius, CTCInfoDisplay/HeroCentreDoorMarker/BoastingArea Radius, and the two CTCStealableItemLocation radii. Parse each value with std::atof and skip anything <=0.
3) For each ring, reuse the projection pattern from drawBrushCursor (editor.cpp:1811-1820). Use 48 segments. Each point is fx=f.pos[0]+r*cos(a), fy=f.pos[1]+r*sin(a), and z = doc_.terrainHeight(fx,fy).value_or(f.pos[2]) + 0.05. Project p={fx, z, -fy}. Unlike the brush, skip points that fail to project instead of returning, then draw the result with dl->AddPolyline(closed).
4) Colours: the thing-level TriggerRadius gets vanilla's RGB(120,200,210). Other fields get a small fixed palette keyed by field. Draw the vanilla_field label (vanillaField(ctc,key)->label, falling back to the key) at the +X point of the ring. Optionally copy vanilla's pulse: a static phase that adds a per-frame step and wraps to 0, used to modulate alpha.
5) Live update needs no extra work because rows are re-read every frame, so dragging the value in the property grid or the spawner card redraws the ring at once. As an optional extra, also draw a preview ring for spawnerRadius_ at the view centre while the spawner card is open.
Effort S, value med. The switch TriggerRadius ring is vanilla parity; the other radius fields are an extension of that behaviour.

#### Find selected implemented; mesh attachment snapping remains
*gap, effort M, value low*

**Vanilla:** CThingDialog FIND_SELECTED_BUTTON (handler 0x028f6168-0x028f625c) takes PeekPSelectedThing, then GetDefGlobalIndex, then the def name, and selects that tree entry, ready to place another of the same thing. Ctrl+D calls SnapSelectionToNearestDummyObject 0x02052eb0: it searches nearby things (CThingFilter_OverlappingArea), reads their graphic dummies (CTCGraphicAppearance::GetDummyObject 0x0205318d) and snaps the selection's position and orientation to the nearest one (items onto shelves, wall mounts: 9 CTCWallMount).

**(A) Show in palette implemented 2026-09-29.** The selection Actions menu
selects the definition, clears filtering, opens its type/group, scrolls the inner
row and outer settings panel, and routes creatures to Actors. Tab changes use the
normal tool transition, so Terrain cannot remain active under Objects/Actors.
Missing/unplaceable definitions report without changing the existing palette.
Native show_in_palette verifies retail object/creature navigation from Terrain
and an unchanged document; screenshot `walkthrough/w21_show_in_palette.png`.

**(B) "Snap to nearest attachment point" (port of CEditControlCentre::SnapSelectionToNearestDummyObject 0x02052eb0). Effort M.**
1. Add `editor::nearestDummyFrame(const Document&, int thingIdx, Frame& out)` in src/leveledit.cpp/.hpp, or a small helper next to thingsexport.
2. Candidates: every other thing whose XY lies within Â±500 of the selection's position. This is vanilla's C2DBoxI with half-extent 500.0f, the constant at 0x4166278. Skip the selection itself.
3. For each candidate:
   - Resolve its graphic model through `context.graphicModelId(def, modelId, &graphicScale)`.
   - Load the geometry with `fe::cachedMesh(modelId)`.
   - For every entry in `geo->helpers`, compose its world frame. Reuse `childTransform(h.matrix, parentInstance, objectScale, graphicScale, c)` (thingsexport.cpp:83); this is the same composition as GetDummyObject 0x1d0ceb0 and GetDummyObjectPositionStatic.
   - Vanilla uses every dummy name returned by GetAllDummyObjectNames, not only CREATE* helpers.
4. Keep the helper with the smallest distance to the selection's position. There is no extra cap, as in vanilla.
5. Apply the result through commitFrame (editor.cpp:899) so it is one undo step:
   - position = the helper's x, y, z;
   - orientation = the helper's forward and up axes (vanilla sets the full right-handed set through physics TC vtbl +0x13c);
   - keep the thing's own scale.
6. If nothing is found, log "no attachment point within 500 units".
7. Expose it as a Selection-card button "Snap to attachment point" and a hotkey that does not clash, e.g. Alt+D, beside the End handler at :2378. Update the hint at :2516.
8. For multi-select, snap only the primary selection, as vanilla does.
9. Needs `childTransform` and `thingBasis` moved into a shared header so leveledit can call them.

**User value:** low-med overall. (A) is a quick convenience. (B) matters for props on shelves and wall mounts.

#### Snap to grid behaves differently from vanilla: relative 0.5-unit steps instead of cell centres on the ground
*weakness, effort S, value low*

**Vanilla:** With Options > Snap To Grid on, GetCurrentPointedAtPos 0x0204b2d0 passes the pointer through FindNearestSnappedPos 0x02049280. That gives x = int(x) + 0.5, y = int(y) + 0.5, z = GetGroundSizeZAt: the centre of the 1-unit cell, sitting on the ground. Placement and carry therefore land things on a world-aligned cell grid.

**Fix:** 1) Add a helper next to snapSelectedToGround in gui/editor.cpp, porting FindNearestSnappedPos 0x02049280:
   bool App::cellSnap(float p[3]) const
   It sets p[0]=float(int(p[0]))+0.5f and p[1]=float(int(p[1]))+0.5f. Vanilla truncates through ftol, so use std::floor to get the same result on non-negative world coordinates. It then sets p[2]=doc_.groundHeight(p[0],p[1]).value_or(p[2]).
2) Replace gizmoSnap_ with two settings:
   - gridSnap_ ("Snap to cell centres", vanilla Options > Snap To Grid). Persist it in the editor settings, as vanilla does with SnapToGridFlag.
   - The existing rotate snap (15 degrees) and scale snap (0.1x), kept as their own toggle.
3) Translate gizmo (editor.cpp:2410-2421): when gridSnap_ is on and gizmoOp_==1, stop passing snapT to ImGuizmo; pass nullptr for translate so the drag is free. After matrixToFrame, keep the unsnapped result in a separate gizmoRaw_ frame so the drag still follows the mouse. Set gizmoFrame_ = snap(gizmoRaw_) with x and y at cell centres. Leave z alone when the user drags the Z axis. When dragging only in the XY plane, set z = groundHeight + (gizmoStart_.pos[2] - groundHeight at the start), which keeps the height above ground. groupFrame() already moves the other selected things by the primary's delta, so the group keeps its layout.
4) Placement paths: when gridSnap_ is on, call cellSnap(pos) after the ground-height line in:
   - placeThing (1436)
   - placeSpawner (1860)
   - placeVillage (1880)
   - placeFishingSpot (2054)
   - the paths at 3252 and 3287; the 3287 one must keep its +0.5f flame offset after the snap
   Any future click-to-place or drag-on-ground code should call it too. Do not snap track nodes at 626: vanilla's mode-specific branches (the vertex snap in THEME2_MAP) show that snapping is mode-dependent.
5) Automation: register "toggle_grid_snap" alongside "toggle_snap" (editor.cpp:2514) and document it in docs/AUTOMATION.md.

Effort S, user value low.

### Height Toolbox and terrain (Land-tab pen...

#### Pen strokes stop at the open map's edge and leave the neighbour's copy of the shared edge stale (in-game seam step)
*gap, effort M, value high*

**Vanilla:** The pens work in world block coordinates. EditChangeHeightPenUndoable 0x02975710 and EditPlacePenUndoable 0x029753d0 call ConvertWorldMapCoordsToLocalCoords, and each block goes through EditSetGroundSizeZAtBlockUndoable 0x0297aa90, which checks IsPosInMap and IsMapEditable. Making one map editable makes its whole region editable (SetMapAsEditable 0x0204c4c0). A stroke therefore carries across the seam into the neighbouring editable map. Caveat: CWorldMap::SetGroundSizeZAtBlock 0x024a4120 writes the one map that vtable+0x44 returns for the block, so which copy of a shared vertex vanilla updates is not verified.

**Fix:** 1) Track edge edits, in src/leveledit.cpp/.hpp. Add `bool edgeRingDirty() const` on Document. It compares working_->heights (or terrain_->heights) against savedTerrain_->heights on the ring x==0 || x==cellsX-1 || y==0 || y==cellsY-1, tolerance 1e-4. Computing it on demand is cheap: the ring is only about 4*N vertices.

2) Add a copy-mode stitch, in src/stitch.hpp/.cpp.
- Add `enum class StitchMode { Average, CopyFromA }` to StitchOptions (default Average, so World-tab behaviour is unchanged).
- In stitchEdges at the `const float target = 0.5f * (ha + hb);` line, use `target = mode == CopyFromA ? ha : 0.5f*(ha+hb)`.
- In CopyFromA mode, skip building editsA, skip docA.setVertexHeights, skip docA.deployTerrain and skip docA's reseat. Only B is written, and B's feather ramp runs over `feather` cells as today.
- Add `stitchNeighboursFrom(gameRoot, layout, map, opts, reports, notes, err)`. It calls stitchEdges(map, n) with CopyFromA for every edge-sharing neighbour. It must not touch a neighbour that has unsaved edits open elsewhere. Documents are loaded from disk inside stitchEdges, so it must run after this map's Write terrain has deployed.

3) Wire it into the UI, in gui/editor.cpp.
- In the terrain footer next to "Re-seat objects on the new ground" (editor.cpp:3176), show a card only when doc_.edgeRingDirty(). Text: "You changed this map's edge. Neighbours still have the old edge, so the seam will show a step." Add a button "Match neighbours to this edge". It runs async, like worldFuture_. It loads the layout via editor::loadWorldLayout(installPath_, ...), as startFitNeighbourLoad does at editor.cpp:3521-3527, then calls stitchNeighboursFrom and logs the StitchReport lines with pushLog.
- Optionally auto-offer it after Write terrain.
- Add an automation verb `match_neighbours` in gui/automation.cpp, next to world_stitch at :392.

4) Add a cheap interim option: a "Lock map edges" pen toggle. It is a bool on TerrainBrush (default on when the map has placed neighbours). In applyBrush, after the heightpen or terrain::applyBrush call, restore ring vertices from working_->heights before the copy-back loops at leveledit.cpp:547-548 and :563-564. The other way is to pass a mask into heightpen so inside() excludes the ring. This mirrors how vanilla's IsMapEditable gate refuses writes it may not make, and guarantees no seam step appears without a stitch.

5) Leave true cross-map strokes, meaning world-block pen coordinates across several loaded LEVs as in 0x0297aa90, to the multi-map lane.

Effort: M (copy mode plus UI card about 1 day; the lock toggle is S). Value: high, because every edge sculpt currently produces a visible in-game seam.

#### Shift does a different job: vanilla Shift freezes the pen on the last point, FableForge Shift swaps raise and lower
*weakness, effort S, value med*

**Vanilla:** CEditInputProcessPaintMap::ProcessInput 0x0294dbe0 checks DIK_LSHIFT/RSHIFT (0x2a/0x36) on every event and calls PaintInputStickWithLastPoint(1) 0x02991680, which sets CEditControlCentre+0xaf8. While that is set, GetCurrentPointedAtPos 0x0204b2d0 returns the stored last point (+0x8dc) and does not ray-cast. The pen stays on one spot while the mouse moves or the ground rises under it. Releasing Shift calls Stick(0). The same Shift flag (+0x2c) is passed to PaintInputPaintMap (river) and PaintInputPickupTheme (Ctrl+Shift picks the theme to replace).

**Fix:** 1. State in App (gui/app.hpp or the editor header next to brushFable_): add `bool penStuck_ = false;` and `float stickPos_[2];`.

2. In App::terrainInput (D:\Code\FableForge\gui\editor.cpp:1594), before the ray-cast, work out `const bool stick = io.KeyShift && !io.KeyCtrl && !io.WantTextInput;`. This matches vanilla ProcessInput 0x0294dbe0, which sets the flag on held LSHIFT or RSHIFT (0x2a/0x36) and clears it on release.
   - On the rising edge, if brushHit_ was true on the last frame, copy brushFable_ into stickPos_ and set penStuck_=true. This is the +0x8dc snapshot.
   - While penStuck_ is set, skip the renderer_.rayTerrain block, set brushFable_ from stickPos_ and keep brushHit_=true. This matches GetCurrentPointedAtPos 0x0204b2d0, which returns +0x8dc when +0xaf8 is set.
   - On release, clear penStuck_.
   - Apply this to every brush tool (vanilla sets the flag for all paint input), so the height pens, theme paint and the -/= HeightKey path (editor.cpp ~1612-1622) all stay on the pinned point.
   - Allow a stroke that starts while stuck. If Shift is pressed with no prior hit, leave the pen unstuck.

3. Remove the Shift inversion at editor.cpp:1604-1605. Move raise/lower inversion to a non-conflicting binding: Alt is free in the terrain tools; alternatively offer a Raise/Lower toggle button. Keep the walkable/blocked swap on that same binding. Leave Ctrl and Ctrl+Shift, the eyedroppers and the theme-replace pick, unchanged.

4. Update the hints at editor.cpp:2791-2797: "Hold Shift to pin the brush to where it is now". Also update the Shift note in the Replace hint, and the Terrain section of docs/AUTOMATION.md and the user guide if they mention Shift-invert.

5. Check the automation scripts and tests that use `key_down Shift` with a terrain tool (grep tests/ and docs/AUTOMATION.md). Retarget them to the new invert binding, and add one test: Raise with Shift held while the mouse moves raises only the pinned spot.

6. Draw the brush ring at the pinned spot, with a small pin marker, so the lock can be seen.

Effort S. Value med.

#### The -/= height keys follow the moving ray instead of a frozen point, and they work in every Terrain tool, not just Height mode
*weakness, effort S, value med*

**Vanilla:** ProcessInput 0x0294dbe0, on a key-down of DIK_MINUS/DIK_EQUALS (0x0c/0x0d): only when GetEditMode()==3 (PAINT_MAP, the Height Toolbox), it calls PaintInputStickWithLastPoint(1) and then PaintInputPaintMapHeightAddition 0x02993840. On key-up it calls Stick(0) and then InsertUndoPartition (0x0294dd93..ddf9). The column therefore grows on one fixed spot, and the keys do nothing in theme, environment or other modes.

**Fix:** All changes are in gui/editor.cpp inside App::terrainInput, plus one member in the App header.

1. **Add the latched point.** Add a member `float keyAnchor_[2]` to App, next to keyStroke_.

2. **Gate the key block on the height tools.** Wrap the block at lines 1613-1628 so it runs only when `const bool heightTool = terrainMode_==0||terrainMode_==1||terrainMode_==2||terrainMode_==3||terrainMode_==16;`.
   - These are the sculpt tools except Path (9). Path is a trackpath tool with its own drag, and it is not part of the vanilla height toolbox (edit mode 3).
   - If the tool changes while a key stroke is active, still end the stroke. Put `if (keyStroke_ && (!down || !heightTool)) { doc_.endStroke(); keyStroke_ = false; }` before the gate.
   - Compute `down` as `keysFree && heightTool && (Minus||Equal)`.

3. **Latch the point on key-down.**
   - When a stroke is not active yet (`!keyStroke_`, brushHit_, viewportHovered_), copy brushFable_ into keyAnchor_ before beginStroke. This matches the vanilla PaintInputStickWithLastPoint(1) at 0x184a44a.
   - On every held frame, build `k` with `k.x = keyAnchor_[0]; k.y = keyAnchor_[1];` instead of the live brushFable_.
   - Once keyStroke_ is set, stop requiring brushHit_. The anchor is fixed, so a ray miss must not stop the stroke.
   - Optionally draw the brush ring at keyAnchor_ while keyStroke_ is set, as around line 1822, so the user can see the frozen spot.

4. **Undo on key-up is unchanged.** endStroke on release already matches the vanilla pair of Stick(0) and InsertUndoPartition (0x17fa698).

5. **Hint text.** Change the comment and the Height-tool hint so they say the keys raise or lower the spot under the cursor at the moment of the press, and only in Raise/Lower/Flatten/Smooth/Noise.

**Effort:** S. **Value:** med.

#### Ctrl eyedropper for height works only in Flatten, and only on a single click
*weakness, effort S, value med*

**Vanilla:** ProcessInput routes any LMB press or held event in Height mode with Ctrl down (0x1d/0x67) to PaintInputPickupHeight 0x029940d0 instead of the pen, whichever Land pen is selected (Change Height, Smear, Noise, Draw Paths, Paint Height). With the Land tab selected, it sets the Paint Height box (SetLeftAltitude) from CWorldMap::GetGroundSizeZAt. It samples on press and on every held event, so a Ctrl-drag keeps updating the value.

**Fix:** All changes are in gui/editor.cpp, in the terrain input function (around lines 1629-1700).
1) Replace the Flatten-only block at 1640-1647 with a block for all height tools. Put it after the theme eyedropper block (1632) and before the mode-8/14/15/9 branches:
   const bool heightTool = terrainMode_ == 0 || terrainMode_ == 1 || terrainMode_ == 2 || terrainMode_ == 3 || terrainMode_ == 9 || terrainMode_ == 16;
   if (heightTool && io.KeyCtrl && lmb && viewportHovered_ && brushHit_ && !io.KeyAlt && !doc_.strokeActive() && !pathDrag_) {
       if (const auto h = doc_.terrainHeight(brushFable_[0], brushFable_[1])) {
           penTarget_ = *h; penTargetFromStroke_ = false;
           if (press) { char msg[64]; std::snprintf(msg, sizeof msg, "picked height %.2f", *h); pushLog(msg, 0); }
       }
       return;
   }
   This matches vanilla ProcessInput 0x0294dbe0: in height mode, Ctrl sends LMB to PaintInputPickupHeight 0x029940d0 on press and on held events, whatever the pen. Log only on press so the log isn't flooded. Don't check Shift: vanilla tests Ctrl before anything else, so Ctrl+Shift also samples.
2) The `!doc_.strokeActive()` and `!pathDrag_` guards stop Ctrl, when pressed in the middle of a stroke, from hijacking that stroke. This is simpler than vanilla (whose events are stateless) and safer. Put the block before the `terrainMode_ == 9` branch at 1681, so that Ctrl+click in Path samples instead of starting a drag.
3) Make the sample visible. In drawPenControls (around editor.cpp:1740-1750), show a read-only "Paint height: %.2f (Ctrl+click to pick)" line for every height tool, not only in the Flatten options at 1746. The simpler alternative is to name the target in the pushLog message and say "Flatten will use it" in the hint text for tools 0/1/3/16/9. Update the hint text for the sculpt tools (around 2548-2590) to mention Ctrl+click = sample height.
4) Optional: if AUTOMATION.md has a terrain-pen command, add a `pick-height x y` verb to the automation path so the behaviour can be tested headless.
Effort S (about 20 lines, one function). Value med: it stops accidental sculpting on Ctrl+click and matches the vanilla workflow of sampling a height from any pen.

#### No single-vertex brush: the Size minimum is 1 cell, where vanilla goes down to 0.25
*weakness, effort S, value med*

**Vanilla:** GetBrushSize 0x02907170 gives radius = 2^slider with slider -2..5, so 0.25..32 blocks, default 0.25. The pens use distance < r + 0.5 around the rounded pointer, so r=0.25 or 0.5 edits exactly one vertex. That is how you fix single spikes, pits and single seam vertices.

**Fix:** 1. **Store the slider exponent.** In gui/app.hpp next to `brushRadius_`, add `float brushSizeExp_ = std::log2(6.0f);`. Keep FableForge's current default of 6 cells; a user who wants a single vertex drags the slider to its minimum. Always derive `brushRadius_ = forge::heightpen::sizeToRadius(brushSizeExp_)`.

2. **Replace the slider** at gui/editor.cpp:2776-2782:
   - Use `ImGui::SliderFloat("##radius", &brushSizeExp_, -2.0f, 5.9069f, "")`. The upper bound is log2(60), so the current 60-cell maximum stays; vanilla's own maximum is 5 (32 blocks).
   - After it, set `brushRadius_ = sizeToRadius(brushSizeExp_)`.
   - Change the label to `"%.2f cells"` when the radius is under 2, and keep `"%.0f cells"` otherwise.
   - Optionally add a hint: "0.25-0.5 = one vertex".
   - Keep the widget name "slider_radius" for automation.

3. **Change the keys** at editor.cpp:2363-2364 to step the exponent: `brushSizeExp_ = clamp(brushSizeExp_ Â± 0.5f, -2, 5.9069f)`, then recompute `brushRadius_`. Half-octave steps give 0.25, 0.35, 0.5, 0.71, 1, 1.4, 2 and so on up to 60.

4. **Keep automation in sync.** `App::setBrush` (app.hpp:215) and the `brush` command (gui/automation.cpp:502) should also set `brushSizeExp_ = log2(max(r, 0.25f))`, so the slider matches a scripted radius.

5. **Check the other brushes** that `brushRadius_` also feeds. The cell-centre brushes in src/leveledit.cpp:452-521 (walkable, theme, replace) test `(x+0.5-bx)^2 + (y+0.5-by)^2 <= r^2`. At r=0.25 they hit a cell only when the pointer is within 0.25 of its centre, so a click can change nothing. Either clamp their effective radius to at least 0.5 at the dispatch (e.g. `std::max(brush.radius, 0.5f)`), or leave it and note that in the hint.
   - Draw Paths: `Document::drawPath` (src/leveledit.cpp:910) accepts any r > 0, so nothing changes there.
   - Outline ring: the ring drawn at editor.cpp:1815 scales with the radius and stays visible at 0.25.

6. **Test:** a forgecore unit test that `changeHeight` at r=0.25 and at r=0.5 changes exactly 1 vertex, and at r=1 changes 9.

Effort S (about 30 lines, gui only, plus one test). Value medium.

#### No whole-map height operations (Raise all with things, Scale heights, Set all)
*gap, effort S, value med*

**Vanilla:** Console editor commands ConsoleEditRaiseZ 0x0204cc40, ConsoleEditResizeZPercent 0x0204c980 and ConsoleEditSetZ 0x0204cae0 run CEditWorldMap::EditRaiseZ 0x02973e20, EditResizeZPercent 0x02973c50 and EditSetZ 0x02973d40 over every editable map. Per map: CEditMap::EditRaiseZ 0x029a8d10 does h + d, EditResizeZPercent 0x029a8ac0 does h * factor, EditResetAllZ sets h = v, each clamped to [0, 2048-1e-4]. EditRaiseZ also moves every thing in the map up by d (CEditRaiseZOnThing 0x0298e5a0: physics position z += d, including camera-point TCs). Each call does Backup first.

**Fix:** 1) src/leveledit.hpp/.cpp, next to applyFractal (~leveledit.cpp:719), add three helpers that share one body. Each builds std::vector<VertexHeight> over all cx*cy vertices, clamps every value with std::clamp(v, 0.f, 2047.9999f) (vanilla uses 2048 - 1e-4), skips unchanged vertices, and commits through setVertexHeights. That gives one undo step, matching vanilla's Backup-before-edit.
   - size_t raiseHeights(float d, bool moveThings = true): h + d (CEditMap::EditRaiseZ 0x029a8d10).
   - size_t resizeHeightsPercent(float pct): h * (pct / 100) (EditResizeZPercent 0x029a8ac0; the argument is a PERCENT, and 100 means no change).
   - size_t setAllHeights(float v): h = v (EditResetAllZ 0x029a8c30).
   Return 0 if !hasTerrain() or a stroke is active (stroke_).
2) Moving things for raiseHeights (CEditRaiseZOnThing 0x0298e5a0): do NOT use reseatThings, because its tolerance filter would leave floating or buried things behind. Wrap the call in beginBatch()/endBatch(). After the height edit, for each i in file_.things() with frameOf(i, f), call file_.setCtcProperty(i, physicsOf(thing)->name, "PositionZ", formatFloat(f.pos[2] + d)), following the reseatThings loop at leveledit.cpp:1005-1017. Include camera-point things if frameOf covers them. Before claiming camera parity, read 0x0298e5a0 to see whether it adds d to anything besides the physics position. ResizeZ and SetZ leave things where they are, like vanilla; the UI hint can point to "Re-seat things".
3) UI in gui/editor.cpp: add a collapsible "Whole map" card under the Fractal terrain card (~line 2809). It has three rows, each with a numeric input and a button:
   - "Raise all by [d] m" with a "Move objects too" checkbox, default on as in vanilla.
   - "Scale heights to [pct] %".
   - "Set all to [v] m".
   Each button calls the Document helper and shows the changed-vertex count. The tooltip explains the 0..2048 clamp and that each is one undo step (Ctrl+Z). Register automation widgets (btn_raise_all and the others).
4) Automation (docs/AUTOMATION.md and the verb dispatcher): add `terrain_raise_all <d> [things 0|1]`, `terrain_scale_pct <pct>` and `terrain_set_all <v>`, matching the console names EditRaiseZ, EditResizeZPercent and EditSetZ.
5) Tests: a unit test on a small heightfield covering clamping at both ends, 50% scaling, and raising with things (every thing's Z moves by d, including one floating 10 m up). Add a ROADMAP_1.0.md parity row citing the addresses above.
Leave the vanilla world-level loop over every editable map (skipping index 0) out of this change, because multi-map region editing is handled elsewhere. Once that lane lands, the World tab can call the same Document helpers per selected map.
Effort S. Value med (the common case is lifting a map to meet its neighbour, or taming a fractal result).

#### The brush circle does not show the vanilla pens' real footprint
*weakness, effort S, value low*

**Vanilla:** Every ported pen acts on a hard disc around the pointer rounded to a block (fistp): Change Height and Paint Height use d < r + 0.5. Smear uses offsets [-r, r-1] with r = trunc(max(r,1)), so its footprint is asymmetric (EditSmoothPenUndoable 0x0297b3c0, EditChangeHeightPenUndoable 0x02975710).

**Fix:** 1. libs/forgecore: add to forge/heightpen.hpp and heightpen.cpp a function `std::vector<std::pair<int,int>> footprint(Mode-ish enum pen, int cellsX, int cellsY, float x, float y, float radius)`. It returns the vertices each pen would touch, using exactly the predicates of the pen functions:
   - changeHeight / paintHeight: the nearest() centre, the floor/ceil bounds and `radius > d - 0.5f`.
   - smear: `r = int(max(radius,1))`, offsets `i - r` for i in [0,2r), and dist < r.
   - heightAddition: the truncated `cx - radius + i` and dist < radius + kDnz.
   - noise: the square [-ceil r, ceil r], shown as the possible poke area.
   The best approach is to refactor each pen so its loop and the footprint share one inline predicate helper, so the two cannot drift apart. Add a unit test in the forgecore tests that runs each pen on a flat field with a +1 delta and asserts that the set of changed vertices equals footprint().

2. gui/editor.cpp drawBrushCursor (around 1807): when isVanillaPen(terrainMode_), stop drawing the circle at the raw centre.
   - Changing and Painting pens: draw the ring at the rounded centre (std::nearbyint of brushFable_) with radius brushRadius_ + 0.5.
   - Smear: draw the ring at the rounded centre offset by (-0.5, -0.5) with radius r, which approximates the asymmetric disc.
   - Heap (heightAddition): draw it the same way as the Changing pen.
   - Noise: draw its square with drawGroundRect.
   Then, when brushRadius_ <= 4, call footprint() and draw a small dot (dl->AddCircleFilled, theme::S(2.5f)) at each returned vertex. Drape each dot using doc_.terrainHeight(bx, by) + 0.05 and project it through renderer_.project, with the same colour as the ring.
   Non-vanilla (smooth-falloff) modes keep the current circle.

3. Optional: show the vertex count next to the '%.0f cells' radius label (editor.cpp:2778) for vanilla pens.

Effort: S (about 60-100 lines, plus a test). Value: low. It only matters at radius 1-3, because the slider minimum is 1.

#### The Speed slider (the step for -/=) is visible only in Flatten
*weakness, effort S, value low*

**Vanilla:** The Height Toolbox always shows Size and Speed (OPACITY_SLIDER, GetOpacity 0x02907840). Speed feeds Paint Height, Smear and the -/= step (PaintInputPaintMapHeightAddition 0x02993840), so the user can tune the key step whichever Land pen is selected.

**Fix:** In gui/editor.cpp drawPenControls:
(1) Take the Speed block out of the `m == 2` branch (lines 1750-1753): the row("Speed") call, the SliderFloat("##pspeed", &penSpeed_, 0..1, "%.1f"), the auto_.registerWidget("slider_pen_speed") call and its tooltip. Put it after the if/else chain so it draws for every height tool (0, 1, 2, 3, 16). The early return at line 1722 already limits the function to those tools. Keep the widget id and the automation name "slider_pen_speed" unchanged so docs/AUTOMATION.md scripts still work.
(2) Make the tooltip depend on the tool. In Flatten (m == 2), keep the existing text: how far toward the target each application goes. In every tool, also say "Step of the - / = keys: %.2f m per key-held frame", filled with forge::heightpen::speedToOpacity(penSpeed_) so the user sees the actual key step.
(3) Optional: next to the slider, show the step in metres as a small muted readout, e.g. ImGui::SameLine(); TextColored(Muted, "%.2f m", speedToOpacity(penSpeed_)), or put it in the slider's format string. This matches vanilla, where the one Speed slider is always visible.
fillPen and the HeightKey path need no changes, because they already read penSpeed_. Effort S, value low.

### Themes dialog (ENGINE/ENVIRONMENT: Smear...

#### Theme Smear (ENGINE tab) missing: the vanilla blend pen
*gap, effort M, value high*

**Vanilla:** On the ENGINE tab, Smear routes PaintInputEngineThemeMap 0x02993a80 (the GetSmearFlag branch, mode 4) to CEditWorldMap::EditPlaceEngineThemeBlendPenUndoable 0x0297c510. r = (int)brush size. For each vertex (x,y) in the 2r x 2r box from (cx-r, cy-r) with hypot(r-i, r-j) < r, and stride 1 for ENGINE: take the 3x3 neighbourhood (offset table DAT_04ac5558, in-map cells only). Sum the blend byte of each of the 3 slots per theme; theme 0 contributes nothing, and weights under 16 are dropped. Keep the 3 heaviest themes by insertion sort and set blend = weight*255/sum(top3), adding the rounding remainder to the first. Everything is computed from the pre-pass state into a scratch buffer and written in a second pass (Jacobi), then UpdateOverlayAtmos runs for atmos. In vanilla this is the ONLY way to get soft transitions between ground themes.

**Fix:** 1. Add a library routine in libs/forgecore (declare it in include/forge/terrain.hpp, implement it in src/terrain.cpp):
   `size_t smearThemes(lev::File& lev, float cx, float cy, float radius, const std::vector<bool>& nullSlot, const std::vector<uint8_t>* mask = nullptr)`
   It ports 0x0297c510 for ENGINE only, with stride 1:
   - Set r = int(radius), c = (int(cx), int(cy)), box origin = c - r.
   - Pass 1: for i,j in [0, 2r), (x,y) = origin + (i,j). Skip the cell unless hypot(r-i, r-j) < r and (x,y) is in the map. Clear a per-palette-slot `uint32 w[]`. For each of the 9 offsets in DAT_04ac5558 (dump the table from FableWin with capstone; it is expected to be the 3x3 including (0,0)), take in-map neighbours only. Read s0 and s1 from themeStrengthAt, set s2 = 255-s0-s1 (a byte, as vanilla does), and for k in 0..2 add sk to w[themeIndexAt(n,k)] when the slot is not null.
   - Zero every w < 16. Pick the top 3 by vanilla's insertion rule: walk slots in ascending order and insert at the first position whose current weight is strictly lower, so ties keep the lower slot. Unused picks are 0 / null.
   - Set sum = total of the picked weights, blend_k = w_k*255/sum (integer division), and add 255 minus the blend total to blend_0. If sum is 0, leave the cell unchanged: vanilla would divide by zero, so guard it.
   - Store the 3 indices and 3 strengths in a scratch vector keyed by (i,j).
   - Pass 2: write each stored cell back with setThemeAt / setThemeStrengthAt (whatever lev::File exposes; the existing applyThemeBrush writes are the model). If `mask` is set, skip cells outside it (vanilla's script-brush mask). Return the number of cells written.
   - Vanilla's "theme 0" is the NULL ENGINE_THEME class. `nullSlot[s]` is true for palette slots that are empty or whose name resolves to the null theme ("X0-NULL Theme" / ENGINE_THEME class index 0). Build it in Document from groundThemes() names.
2. Add `SmearTheme` to TerrainBrush::Mode (src/leveledit.hpp:84). In Document::applyBrush (src/leveledit.cpp, next to the Mode::Theme block around line 475), call forge::terrain::smearThemes(*level_, brush.x, brush.y, brush.radius, nullSlots). Then mirror the box from x-r-1 to x+r+1 into working_->themeIndex/themeStrength exactly like the Theme block does, and bump terrainRev_. It joins the same stroke undo as Theme.
3. In gui/editor.cpp:2549, add {17, "Smear"} to the paint group and map terrain mode 17 to Mode::SmearTheme wherever the mode-to-brush switch lives. Add 17 to isVanillaPen (gui/app.hpp:600) so it applies once per click, or every frame with "Repeat while held" (vanilla Spray can). Show only the radius control; smear takes no theme selection. Add a tooltip: "Blends ground themes with their neighbours (vanilla Themes > Smear)". Register an automation widget id and document it in docs/AUTOMATION.md.
4. Optional, as a separate item: vanilla's ENVIRONMENT-tab Smear is Blur 0x0297b910 on the atmos (game-map) grid. It could later sit on the Environ. tool as a "Smear" sub-toggle, but port it from its own decompile (themes.c:807), not from this routine.
5. Tests: add a check to tests/test_export.cpp on a 5x5 lev. Centre (2,2) is theme A=255, with neighbours of theme B=255 on the left column and theme C with a weight under 16. Assert the centre's top-3 order, that blends sum to 255, that the remainder lands on slot 0, that C is dropped, and that the Jacobi rule holds (a neighbour smeared in the same pass sees the pre-pass values). Also add a tests/ui/theme_smear.txt script with a screenshot.
Effort M, value high.

#### No sound or environment overlay (vanilla Show all / Show selected sounds, atmos colour overlay)
*gap, effort M, value high*

**Vanilla:** Survey > Sounds has Off / Show all sounds / Show selected sound (EditShowAllSounds 0x029a9d50, EditShowSelectedSounds 0x029a9db0, then CEditMap::SoundOverlayInfo 0x029aa550). Every height cell is tinted with its game cell's SOUND_THEME def EditColour (CSoundThemeDef+0x40). For 'selected', only cells whose sound equals the chosen layer are tinted, and the rest are transparent. Painting an environment theme refreshes UpdateOverlayAtmos 0x02971d80: per cell, sum over 3 slots of ENVIRONMENT_THEME_DAY EditorColour (+0x74 in FableWin) times blend/255, at alpha 0x80.

**Fix:** Effort M, value high. Port the behaviour of SoundOverlayInfo 0x029aa550 and UpdateOverlayAtmos 0x02971d80.

1) Read the colours from defs. ctx_.defIntField(name, field) (src/terrainexport.cpp:507) already decodes a 4-byte field by name, and CRGBColour is 4 bytes. Add a helper in src/terrainexport.cpp, Context::defColourField(name, field) -> optional<array<uint8_t,4>>, which unpacks the byte order the same way defedit.cpp:149 does. Use field 'EditColour' on SOUND_THEME defs and 'EditorColour' on ENVIRONMENT_THEME_DAY defs. Use the field names, not the FableWin offsets 0x44/0x74, which differ from Ego_r's 64/108. Cache the colours per def name in the editor and drop the cache when soundDefs_/envDefs_ reload.

2) Build the overlay in Document (src/leveledit.cpp/.hpp). Add two functions, each filling one RGBA per height cell (cellsX x cellsY), with each height cell mapped to its 4x4 game-map cell the way environmentAndSoundAt already does:
   - soundOverlay(int layer, colourFn, vector<uint32_t>& out). Get s = the cell's sound index. If layer==0: s>0 gets colour(soundThemes[s-1]), anything else gets 0 (transparent). If layer!=0: s==layer gets its colour, anything else gets 0.
   - atmosOverlay(colourFn, out). Start with rgb=0. For the 3 slots, add EditorColour(atmosPalette[idx]) * strength/255 to rgb, with idx 0 counting as black. Round, clamp to 8 bits, and set A=0x80.

3) Renderer (gui/renderer.cpp/.hpp):
   - Add an optional per-vertex overlay colour. Either extend the terrain vertex, which already carries walkable at renderer.cpp:891, with a packed RGBA (update the input layout), or bind a second vertex stream.
   - Add Renderer::updateOverlay(const uint32_t* rgba, int cellsX, int cellsY) and a flag overlayOn.
   - In the pixel shader (around renderer.cpp:72-91, next to the Walkable branch), blend lerp(base, overlay.rgb, overlay.a) on top of any view mode.

4) UI (gui/editor.cpp, the terrainMode_ 10/11 block at 2601-2679):
   - While Environ. (10) is active, show the atmos overlay automatically. While Sound (11) is active, show the sound overlay.
   - Under the sound combo (editor.cpp:2646), add a segmented 'Show: Off / All sounds / This sound only'. 'This sound only' passes soundIndex_ as the layer. These are vanilla Survey > Sounds Off / Show all / Show selected.
   - Add a compact legend: each sound this map names, with its EditColour chip (and the atmos palette entries for Environ.). Clicking a chip selects that sound or slot.
   - Rebuild the overlay after every paint stroke, fill, or add-from-game, the way vanilla EditSetSound 0x029a97c0 re-runs SoundOverlayInfo. Rebuilding only the dirty box is enough, as UpdateOverlayAtmos does with its C2DBoxI. Rebuild it on undo too.

5) Automation (gui/automation.cpp:241): add overlay=sound|sound:<n>|atmos|off so headless screenshots can check it.

Leave out the Reflections overlay here (ReflectionsOverlayInfo 0x029aad40 is a separate survey).

#### Ground and environment Paint is a soft airbrush; vanilla Paint is a hard stamp
*weakness, effort S, value high*

**Vanilla:** EditPlaceEngineThemeUndoable 0x029799d0: for every vertex with (x-cx)^2+(y-cy)^2 < r^2 (integer cursor, editable map, script-brush mask), slot0 = theme, slots 1-2 = 0, blend0 = 255, blend1 = 0. One application gives full coverage. For ENGINE it does nothing if theme == 0 (X0-NULL). With ATMOS_THEME the same loop writes the game cell under each vertex. Soft edges come from Smear.

**Fix:** 1. Add `bool hardEdge = false;` to editor::TerrainBrush (src/leveledit.hpp, next to exactStep at about :91) with the comment "vanilla EditPlaceEngineThemeUndoable 0x029799d0 stamp". Set `b.hardEdge = penHardTheme_;` in App::fillPen (gui/editor.cpp:1709).

2. In Document::applyBrush Mode::Theme (src/leveledit.cpp:475), when brush.hardEdge is set:
   - If themeIndex == 0, return. Vanilla skips ENGINE theme 0.
   - Set `icx = int(std::lround(brush.x))` and `icy` the same way. Vanilla's cursor is a C2DCoordI, so the circle is centred on an integer vertex.
   - For y from max(0, ftol(icy - r)) to min(h, icy + r), and x likewise: if `float(sqr(x-icx) + sqr(y-icy)) < r*r` (strict), call `level_->setThemeBlendAt(x, y, {theme,0,0}, {255,0,0})`, then mirror it into working_->themeIndex/themeStrength as the existing loop does.
   - Increment terrainRev_ and return.
   Put the helper in libs/forgecore terrain.cpp as `size_t stampTheme(lev::File&, int cx, int cy, float r, uint8_t theme)` so the automation path and tests can reuse it.

3. Mode::Environment (leveledit.cpp:492), hard branch: loop over the same integer vertices inside r^2 (strict `<`). Map each one to its game cell with gx = x/4, gy = y/4, clamped to the game-map size. Set `atmosIndex[g] = {theme,0,0}` and `atmosStrength[g] = {255,0,0}`. Theme 0 is allowed here, because vanilla allows ATMOS with 0. A cell is written if any of its vertices is under the pen, which matches vanilla's footprint better than the current cell-centre test.

4. Add modes 6 and 10 to App::isVanillaPen (gui/app.hpp:600) when penHardTheme_ is set, so the existing penApplied_ gate (editor.cpp:1704) applies the stamp once per click and every frame with Spray. Since the stamp is idempotent, per-frame application during a drag also works. The main goal is to stop dt scaling.

5. drawPenControls (editor.cpp:1718) currently returns early unless m is 0/1/2/3/16. Extend it to 6 and 10 with a `Hard edge (vanilla)` checkbox (`auto_.registerWidget("check_pen_hard")`) and the tooltip "Vanilla Paint: every point under the brush becomes 100% this theme in one click. Use Smear for soft edges." Hide the Strength slider when it is on. Add the checkbox to docs/AUTOMATION.md.

6. Default: keep it off until the Smear pen (EditPlaceEngineThemeBlendPenUndoable 0x0297c510 / Blur) is ported. After that, make it on by default, because Paint plus Smear is the vanilla workflow.

7. Add a forgecore test: stamping one vertex inside r gives exactly {t,0,0}/{255,0,0}, a vertex at exactly r stays unchanged, and theme 0 does nothing.

Effort: S. Value: high.

#### Environment Smear missing (vanilla Blur on the atmos grid)
*gap, effort S, value med*

**Vanilla:** On the ENVIRONMENT tab (mode 5), Smear calls CEditWorldMap::Blur 0x0297b910. r = max(brush size, 1). It visits game-map cells (height coords divisible by 4) with hypot <= r and, for each, reads the 3x3 GAME cells around it (offset*4). It sums the atmos blend of all 3 slots per atmos theme, including slot value 0 ('none'), in a multimap. It then sorts by summed weight descending, writes the top 3 into slots 0..2 and sets blend[k] = round(w_k*255/sum_top3) for k = 0,1. It works in place (Gauss-Seidel) and refreshes UpdateOverlayAtmos.

**Fix:** 1) libs/forgecore: add `size_t blurAtmos(std::vector<std::array<uint8_t,3>>& idx, std::vector<std::array<uint8_t,3>>& str, int gw, int gh, float cx, float cy, float radius)` to forge/terrain.hpp/.cpp. It works on game-cell units: the centre game cell is (cx/4, cy/4) and r = max(radius,1) in height cells, so it visits the game cells whose height-coord offset satisfies hypot <= r. Visit order is row-major (y outer, x inner), in place, to match vanilla's Gauss-Seidel update order. For each cell, sum str[n][k] per idx[n][k] over the 3x3 neighbourhood (offsets -1..1 game cells, centre included) x 3 slots. Keep idx 0 ('none') as a real key and skip neighbours outside [0,gw)x[0,gh) (vanilla reads across the world map, but we have one map). Sort by weight descending. For equal weights, put the higher theme index first: vanilla's std::multimap reverse iteration returns the later-inserted key, and insertion runs in ascending theme order. Write the top 3 (fewer if fewer distinct themes; pad unused slots with index 0 / strength 0). Set s0 = round(w0*255/S) and s1 = round(w1*255/S), where S is the top-3 sum, and s2 = 255 - s0 - s1 (vanilla writes only blends 0 and 1; slot 2 is implicit). If S == 0, leave the cell as it is (vanilla would divide by zero). Return the count of changed cells. Add a unit test in tests/test_export.cpp: a hard two-theme border is softened, a uniform area is left unchanged, and the ordering holds.
2) src/leveledit.hpp:84: add Mode::EnvSmear. In src/leveledit.cpp:492, handle EnvSmear beside Environment. Call blurAtmos on working_->atmosIndex/atmosStrength with brush.x/y/radius, ignore dt/strength (vanilla Blur has no opacity), and bump terrainRev_ when n > 0. The existing mirror at leveledit.cpp:419-429 and the dirty check at :942 already cover atmos, so saving and undo work unchanged. Use the same spray semantics as the other pens: one application per brush event while LMB is held.
3) gui/editor.cpp: in the Environ. tool panel (~2604-2676), add a Paint | Smear segmented toggle (a new member such as envSmear_). Make the mode mapping at ~1577 return M::EnvSmear when terrainMode_==10 && envSmear_. Hide the theme picker and add-from-game search while in Smear mode. Leave Ctrl+click sampling as it is. Tooltip: "Blends each 4x4 cell's environment with its 8 neighbours (vanilla ENVIRONMENT Smear)".
4) Optional: add an `env smear x y r` verb to docs/AUTOMATION.md if the automation language exposes environment paint. Effort S, value med.

#### Sound and environment brush footprint too small: game-cell-centre test instead of the vanilla height-cell pen
*weakness, effort S, value med*

**Vanilla:** EditSetSounds 0x02971010 uses a cursor rounded to the nearest vertex (GFFloatToLongNear in SoundsInput 0x02052230). It loops over height cells in [floor(c-r), ceil(c+r)] and, for each with hypot - 0.5 < r, calls EditSetSound on the game cell containing it. The game cell under the cursor is always painted, even at radius 1. Env paint (0x029799d0 with ATMOS) likewise maps every vertex in the pen to its game cell.

**Fix:** 1. In src/leveledit.cpp, Document::applyBrush, replace the centre-test loop in the `Mode::Environment || Mode::Sound` branch (lines 497-510) with a port of 0x02971010:
```
const int cxr = int(std::lround(brush.x)), cyr = int(std::lround(brush.y));   // GFFloatToLongNear (SoundsInput 0x02052230)
const int vx0 = int(std::floor(brush.x - brush.radius)), vx1 = int(std::ceil(brush.x + brush.radius));
const int vy0 = int(std::floor(brush.y - brush.radius)), vy1 = int(std::ceil(brush.y + brush.radius));
std::vector<uint8_t> hit(size_t(gw) * gh, 0);
for (int vy = vy0; vy <= vy1; ++vy)
  for (int vx = vx0; vx <= vx1; ++vx) {
    if (std::hypot(float(vx - cxr), float(vy - cyr)) - 0.5f >= brush.radius) continue;
    if (vx < 0 || vy < 0 || vx >= cx || vy >= cy) continue;   // ContainsPoint / IsPosInMap
    const int gx = vx / 4, gy = vy / 4;                      // HeightMapUnitToGameMapUnit
    if (gx < gw && gy < gh) hit[size_t(gy) * gw + gx] = 1;
  }
for (size_t g = 0; g < hit.size(); ++g) if (hit[g]) { /* existing Sound / Environment body, unchanged */ }
```
   - The loop bounds come from the unrounded pen position. Only the distance test uses the rounded cursor, which is what the vanilla code does.
   - Use the `hit` mask so a cell is painted once per applyBrush call. Vanilla's own guard in EditSetSound (skip if the value is already set) is the same thing for Sound.
   - Environment keeps the strength-weighted paintThemeBlend; only the footprint changes.
   - Do the vx/vy bounds check before dividing by 4 so edge vertices can't map outside the map.
2. If the viewport draws a circle for the brush while in Sound or Environment mode (terrainMode_ 10/11 in gui/editor.cpp), draw it at radius r+0.5 around the rounded cursor. Better still, outline the game cells the mask will paint, so the preview matches what gets painted.
3. Optional: vanilla has a separate Sounds brush radius (CSurveyDialog::GetSoundsBrushRadius). FableForge shares brushRadius_, which is fine and needs no change.

Effort: S. Value: low to medium. The fix mainly matters at small radii (up to about 2.8) and makes the painted footprint slightly larger, matching vanilla. The default radius of 6 already paints something on every click.

#### Clear all sounds missing
*gap, effort S, value med*

**Vanilla:** Survey > Sounds > Clear all: CEditControlCentre::ClearAllSounds 0x02051750 asks 'Clear All Sounds?' / 'Confirm Clear All', then calls EditFillSound(0) 0x02970df0, which sets every game cell of every editable map to sound 0.

**Fix:** 1) src/leveledit.hpp, next to addSoundTheme (~line 450): declare `size_t fillSound(uint8_t index);`. The comment should say it is vanilla EditFillSound 0x02970df0: every game-map cell of this map gets the index, 0 = none, which is Survey > Sounds > Clear all.

2) src/leveledit.cpp, modelled on replaceTheme (671-716):
```
size_t Document::fillSound(uint8_t idx) {
    if (!hasTerrain() || stroke_ || !level_->hasGameMap() || terrain_->sound.empty()) return 0;
    if (idx > level_->soundThemes().size()) return 0;
    auto next = std::make_unique<TerrainState>(*terrain_);
    size_t changed = 0;
    for (auto& s : next->sound) if (s != idx) { s = idx; ++changed; }
    if (!changed) return 0;
    pushUndo();
    terrain_ = std::shared_ptr<const TerrainState>(next.release());
    writeTerrainToLevel();   // already mirrors sound via level_->setSoundAt (line 430)
    ++revision_; ++terrainRev_;
    return changed;
}
```
There is no hf_ reset and no themeRev_ bump, because heights and ground themes are untouched. Vanilla's IsMapEditable filter is implicit here, since the Document only edits its own map.

3) gui/editor.cpp: in the Sound branch, after the add-from-game list (~line 2672, before the shared hint at 2674), add `theme::ghostButton(soundIndex_ == 0 ? "Clear all sounds on this map" : "Fill map with this sound", ImVec2(cardInner, S(28)))`. On click it calls `doc_.fillSound(uint8_t(soundIndex_))` and runs pushLog with the number of cells changed, or "already filled" when it returns 0. Register it with `auto_.registerWidget("btn_fill_sound")`. Skip vanilla's modal, because the action is a single undo step. That fits the "port behaviour, not UI" rule.

4) Automation needs no new verb: `click btn_fill_sound` already works through registerWidget. Add a one-line mention in docs/AUTOMATION.md's widget list, plus a smoke script: select_tool sound, pick "(no sound)", click btn_fill_sound, assert that the sound under the cursor is 0.

Effort S, value low-med. Clearing or reseeding a map's ambience is occasional, but painting a whole map by hand is tedious.

#### Replace / Flood Replace miss vanilla's water-family match and hold-by-index rule
*weakness, effort S, value med*

**Vanilla:** EditReplaceEngineThemeUndoable 0x0297a6c0, EditFloodReplaceEngineThemeUndoable 0x02979ff0 and IsPosValidFloodReplaceSite 0x02979e00 treat a slot as matching when its theme index == theme_to_replace, OR when both the slot's and the replace theme's CEngineThemeDef NoWaterThemeDef (+0x78) are non-zero and equal. Picking one depth theme of a water ladder therefore replaces the whole family (W1..Wn). A slot matches whatever its blend (strength 0 included). Only the index is rewritten and blends are kept. The pen uses integer vertices with a strict (dx^2+dy^2) < r^2.

**Fix:** 1) forgecore, libs/forgecore/include/forge/terraintex.hpp:109: add `uint32_t noWaterThemeDef = 0;` to ThemeEntry. In libs/forgecore/src/terraintex.cpp next to the WaterType/WaterHeight read (~line 375), fill it with `fieldInt32(decoded, "NoWaterThemeDef", found)`. Before shipping, check with a def dump that each TG_WATER_* ladder shares one non-zero value.

2) forgecore, libs/forgecore/src/terrain.cpp:304: add `ThemeBlend replaceThemesInBlend(ThemeBlend, const std::array<bool,256>& match, uint8_t to)`. It sets every slot whose index is in `match` to `to`, keeps the strengths, and keeps the existing merge of duplicate slots so the 3-slot invariant holds. Keep replaceThemeInBlend as the single-index wrapper.

3) Document, src/leveledit.hpp/.cpp: add `void setThemeFamilies(std::array<uint32_t,256>)`, one NoWaterThemeDef per LEV palette slot, 0 when there is none. Add a helper `std::array<bool,256> replaceMatchSet(uint8_t from) const` that sets match[s] = (s == from) || (fam[s] != 0 && fam[s] == fam[from]).

4) Wire the families in App/editor (gui/editor.cpp, wherever the palette is resolved, e.g. near the ctx_.themeLibrary() uses at ~1090/1163). For each palette slot, look up the slot's ENGINE_THEME name with lib->byName(name)->noWaterThemeDef. Call doc_.setThemeFamilies(...) on level load, on palette change and when the library finishes loading. With no library, all zeros gives exact-index matching.

5) Document::replaceTheme (src/leveledit.cpp:671-716):
- Return 0 if to == 0, matching vanilla's param_3 != 0 guard.
- Make holds(i) true when any slot k has match[themeIndex[i][k]], and drop the `themeStrength > 0` test.
- Write the result through replaceThemesInBlend.
- Keep the 8-neighbour flood. It visits the same set as vanilla's list-based DFS using the neighbour table at DAT_04ac55a0.

6) ReplaceTheme pen (src/leveledit.cpp:514-529):
- Use the same match set.
- Use an integer centre `const int pcx = int(std::floor(brush.x + 0.5f))` (and the same for pcy); if brush.x is already in block units, use the mouse-to-C2DCoordI rounding vanilla uses.
- Loop y from pcy - r to pcy + r and x the same way, with integer `dx = x - pcx`.
- Skip when `dx*dx + dy*dy >= r*r` (strict <, as in vanilla).

7) Tests, tests/test_export.cpp:684-697:
- A zero-strength slot holding `from` must continue the flood.
- A two-theme family (fam[a] == fam[b] != 0): replacing a must also rewrite b cells, and must not touch a cell whose family is 0.
- The pen footprint at r = 1 must cover only the centre block.

Coordinate the family rule with the water lane, since it only affects depth themes. Effort S (about half a day). Value med.

#### Budget survey cannot survey a dragged box or clicked things
*weakness, effort M, value low*

**Vanilla:** Survey > Engine: OnEngineSurveyLMBPressed/Held/Released 0x0204f4c0..0x0204f5a0. Dragging with LMB adds a box (AddAreaToEngineSurvey 0x02051310), and the things inside join the survey. Clicking a thing adds it (AddThingToEngineSurvey 0x02051180). Shift keeps the earlier picks, several areas can accumulate, and the picks are drawn (DrawEngineSurveySelections 0x0204e220).

**Fix:** Port OnEngineSurveyLMB* (0x0204f4c0..0x0204f5a0) and AddAreaToEngineSurvey (0x02051310) as a fourth scope, "Area". Effort S-M, value low-med.
1) gui/app.hpp near line 562: add `std::vector<std::array<float,4>> budgetAreas_; bool budgetAreaDrag_ = false; float budgetAreaStart_[2]{};`. Change the comment to mark scope 3 as "areas". In gui/automation.cpp:269, raise the clamp to 0..3. Add automation keys `budget_area=x0,y0,x1,y1` (appends) and `budget_clear_areas`, and document them in docs/AUTOMATION.md.
2) Input: add `App::budgetAreaInput(origin,size)` and call it from app.cpp:1817, next to terrainInput. It runs only when `budgetOpen_ && budgetScope_==3 && editMode_ && viewportHovered_`, and it runs before handleViewportInput so that LMB does not also select or move gizmos. Alternatively, handleViewportInput can early-out on LMB in that state. Ray the ground the same way terrainInput does (renderer_.screenRay + rayTerrain, editor.cpp:1595-1600), keeping a local hit in map coordinates (x, -z).
   - On press, as in vanilla OnEngineSurveyLMBPressed with its shift flag: if Shift is not held, clear budgetAreas_. Then set budgetAreaDrag_ and the start point.
   - On release: normalise the rectangle (min/max) and push it. If it is degenerate (below about 0.5 units), treat the release as a click. In that case pick the thing under the cursor with the existing viewport pick and add it to a `std::set<int> budgetThings_`. This mirrors AddThingToEngineSurvey, which Shift also accumulates. Then set budgetDirty_.
3) runBudgetSurvey (editor.cpp:3926): in inScope, when budgetScope_==3, return true if thing>=0 is in budgetThings_ or if (x,y) is inside any budgetAreas_ box. Local detail (editor.cpp:3943) stays included for scope 3 and is point-tested against the boxes. Vanilla uses the renderer's area stats here, so point-in-box is the bank-data equivalent.
4) Drawing: add `App::drawBudgetOverlay(origin,size)` and call it next to drawBrushCursor (app.cpp:1819). drawBrushCursor early-outs outside terrain mode, so the overlay needs its own call. For scope 3, outline every box with drawGroundRect (editor.cpp:1791) and draw the live drag box. For scope 2, drape a 48-segment circle of radius budgetRadius_ around camera_.focus, reusing the loop from editor.cpp:1813-1825. Pick one budget colour. This stands in for DrawEngineSurveySelections 0x0204e220.
5) Window (editor.cpp:4000): make the segment list {"Whole map","Selected","Around the view","Areas"}. For Areas, add the hint "Drag on the ground to add a box; Shift+drag keeps earlier boxes; click a thing to add it", plus a "Clear areas" ghost button that clears both containers and sets budgetDirty_. Update scopeName[] at editor.cpp:4125 and change its clamp to 0..3.

#### Reflections survey (shore-point flag brush) not ported
*gap, effort S, value low*

**Vanilla:** Survey > Reflection: ReflectionsInput 0x02052620, where LMB sets and the modifier-click clears, calls EditSetReflections 0x029712e0 (height cells with hypot - 0.5 < r) and then CEditMap::EditSetReflection 0x029a9860, which toggles CHeightMapCell+9 bit 2. PDB egor names that bit ShorePoint, and it is persisted as the .lev cell byte +19. The overlay is ReflectionsOverlayInfo 0x029aad40, red at alpha 0x80 where set.

**Fix:** Effort S, value low.

1. forgecore: add `bool File::shorePointAt(int x,int y) const { return cell(x,y)[19]!=0; }` and `void setShorePointAt(int,int,bool)` in libs/forgecore/src/lev.cpp, next to preferredPathAt (about line 400). Declare both in lev.hpp.

2. Document state, in src/leveledit.hpp:
   - Add `std::vector<uint8_t> shorePoint;` to TerrainState, beside cameraPassable (line 64).
   - Extend `enum class Mode` (line 84) with ShoreSet and ShoreClear.

3. Load and save, in src/leveledit.cpp:
   - Resize and fill the vector from shorePointAt where cameraPassable is loaded (lines 219 and 240).
   - Write it back in the save loop (about line 410) only for cells whose value changed. Unlike +16 there is no OR with walkable: vanilla stores the bit as painted.
   - Add `shorePoint` to the equality and dirty checks at lines 573 and 940.

4. Brush: copy the CameraPass/CameraBlock block at leveledit.cpp:451-461 into a ShoreSet/ShoreClear block. Use vanilla's pen test from EditSetReflections 0x029712e0: a cell is inside when hypot(dx,dy) - 0.5 < r. Before porting, confirm that test by disassembling 0x029712e0 with scratchpad/cmp/fwdis.py. Write only cells whose value changes, as EditSetReflection 0x029a9860 does, so undo stays minimal.

5. GUI, in gui/editor.cpp:
   - Put "Shore point on / off" tools in the Terrain tab's passability group next to the camera brush, sharing its radius slider. LMB sets. Vanilla clears with a modifier-click (ReflectionsInput's bool arg), so map clearing to the ShoreClear tool or to the same modifier the camera brush uses.
   - Add a toggleable overlay that tints set cells red at alpha 0x80, matching ReflectionsOverlayInfo 0x029aad40.
   - Add a tooltip saying this is the editor's shore-point/reflection flag stored in the .lev, and that the game's water shoreline comes from the water generator.

6. Automation: add `shore set|clear x y r` alongside the camera-pass verb in docs/AUTOMATION.md and its dispatcher.

7. Gate before calling it a gameplay feature: scan Fable.exe (retail) for reads of the loaded ShorePoint bit. Look in the CHeightMapCell loader and ConvertFromOld (FableWin 0x022423a0) for the +19 to +9 bit-2 transfer, then look for consumers of `[cell+9]` bit 2. If there are none, ship it as a data-parity/inspection tool (the overlay is useful because about 160 retail maps carry the flag) and label it editor-only.

### World map + regions + map popup + map vi...

#### No way to save terrain work without writing it into the game; Ctrl+S covers only objects
*weakness, effort M, value high*

**Vanilla:** Ctrl+S, F6 and toolbar SAVE_ALL all call SaveAllIfChanged 0x0204b550, which saves the world file and every changed editable level (.lev and .tng) as working files. F7 saves all and then reloads the world. Writing the game data (STB/nav) is a separate load-time step (GenerateOfflineDataForWorld).

**Fix:** 1) Add App::saveAll() in gui/editor.cpp, next to saveDocument(). The game's shared Levels folder is involved, so first call gameWriteBlocked("save") and return if it blocks. Then: if doc_.dirty(), call saveDocument(). If doc_.hasTerrain() && doc_.terrainDirty(), call doc_.saveTerrainLoose(saveRoot(), err, &notes) and pushLog each note plus "saved <map>.lev (draft)". It writes the same data/Levels/FinalAlbion/<map>.lev path that deployTerrain later rewrites, so no new folder and no change to resolveLevPath are needed: app.cpp:596 already reopens a loose .lev first. After saving, refresh the MapEntry's loosePath the same way the .tng draft path does, so the next open picks up the draft. Handle an external (other-world) map the same way saveDocument does at editor.cpp:1466.
2) Change the Ctrl+S / F6 handler at editor.cpp:2370 to call saveAll() when hasUnsavedEdits() (app.hpp:849) instead of saveDocument() when doc_.dirty().
3) In drawEditFooter (editor.cpp:3226), have 'Save draft' use hasUnsavedEdits() and saveAll(). In the Unsaved-changes modal (editor.cpp:2478), enable the button on hasUnsavedEdits(), call saveAll(), and remove the hint at 2486-2489.
4) Add F7 = saveAll() and then re-select or reload the current map (selectMap(selectedName_) with discardEdits_ set) to match vanilla's save-and-reload.
5) Automation: have save_level (gui/automation.cpp:489) call saveAll(), or add a save_all command, and document it in docs/AUTOMATION.md.
Caveats to show in the tooltip: (a) on a loose install with no FinalAlbion.wad, the game reads the loose .lev. It is therefore live on the next visit, like the .tng draft already is, but FinalAlbion_RT.stb is not re-baked. Warn that heights shown in game will not match until 'Write terrain into the game' re-bakes the STB. (b) saveTerrainLoose patches nav walkability in the .lev at save time, which matches what deploy would do anyway. (c) deployTerrain's hadLoose / backup logic (leveledit.cpp:1082-1085) then sees a FableForge-created loose file. That is fine because saveTerrainLoose already runs backupOnce and markCreated first, but check that Restore (app.cpp:423) still deletes the created .lev.
Optional, lower priority: World-tab pending edits (gui/world.cpp:600-625) have the same problem. Serialising worldPending_ / worldOwnerEdits_ / worldSeesEdits_ into a sidecar JSON under saveRoot and restoring it in loadWorld could follow later.
Effort: S-M (mostly wiring; the save function exists). Value: high.

#### No user backups (Create backup file) and no editor autosave
*gap, effort M, value high*

**Vanilla:** The toolbar BACKUP button calls CreateBackupFile 0x0204b690: SetSaveToBackupFiles(true) (CEditWorldMap+0x17d), SaveAllLevels(1,0), then the flag is cleared. CEditWorldMap::SaveSingleLevel 0x0296e150 then appends '.backup' to the .lev and .tng names (strings 0x4349e0c/0x4349e14), which gives a manual snapshot of every editable level. Autosave: console var EnableEditorAutosave binds CEditWorldMap+0x17c (ctor 0x0296a5d4). CEditWorldMap::Update counts frames in +0x178, and every 0x34BC (13,500) frames it saves each changed (vtable+0x2c) editable map through SaveSingleLevel under a name with '_a' inserted before the extension (0x0296a9bc-0x0296aa0e, 'Name_a.lev'). The real files are never overwritten. Whole-world snapshots also exist: CEditWorld::BackupWorldAndAllMapsToFile 0x0207f9a0 / Restore 0x0207fce0.

**Fix:** Keep writes away from the game and from drafts. Everything goes under the app's data dir: <settingsPath dir>/autosave and <settingsPath dir>/snapshots/<install-hash>/<map>/.

1. Document serializers (src/leveledit.hpp/.cpp, S):
   - Add `bool writeThingsTo(const fs::path& tng, std::string& err) const`, which writes file_.serialize() to an arbitrary path and leaves markSaved alone.
   - Add `bool writeLevelTo(const fs::path& lev, std::string& err) const`. It runs writeTerrainToLevel's serialization on a COPY of *level_ (the pattern already used at editor.cpp:1518: make_shared<lev::File>(*doc_.level())). It does no nav patch and no STB bake, so the autosave only holds the heights, themes and blends.
   - Add loaders: `bool loadThingsFrom(path)` / `bool loadLevelFrom(path)` that replace the document text/terrain as ONE undo step and leave it dirty. This mirrors vanilla, where restoring a backup is an edit rather than a save.

2. Autosave (the port of CEditWorldMap::Update 0x0296a8ba-0x0296aa0e):
   - Add ExportSettings fields: `bool autosave=true` (vanilla EnableEditorAutosave, CEditWorldMap+0x17c) and `int autosaveMinutes=5`. Vanilla counts 0x34BC = 13,500 frames, about 7.5 min at 30 fps; use wall time because FableForge's frame rate varies.
   - In App::frame (gui/app.cpp:1368), accumulate dt. When the interval elapses and (doc_.dirty() || doc_.terrainDirty()), which is vanilla's vtable+0x2c 'changed' test, write <map>_a.tng and, if terrainDirty, <map>_a.lev. The '_a' goes before the extension, exactly as vanilla names them.
   - Do the write on a std::async (a snapshot copy, like editor.cpp:1518), then reset the counter. Log one muted line: "autosaved <map> (objects+terrain)".
   - Delete <map>_a.* after a successful saveDocument / terrain deploy, or when the user discards edits in drawUnsavedPrompt.

3. Recovery: in App::openDocument (gui/editor.cpp:54), if <map>_a.tng/.lev exists and is newer than the draft or the game file, show a modal: 'Recover unsaved edits from <time>? (objects / terrain)' with Recover / Discard. Recover calls the loaders from step 1.

4. Manual snapshots (the port of CreateBackupFile 0x0204b690 + SaveSingleLevel's '.backup' names):
   - Add a 'Snapshot' action (Level-tab footer next to 'Save draft' at editor.cpp:3226, plus Ctrl+B, which is vanilla's backup key per inventory:115).
   - For the current map, the action writes <map>.tng + <map>.lev (from the in-memory doc, unsaved edits included) plus copies of FinalAlbion.wld/.bwd into snapshots/<map>/<YYYYMMDD-HHMMSS>/.
   - Add a 'Snapshotsâ€¦' list in the Level tab with timestamp, what it holds, and Restore / Delete. Restore loads into the doc as an undoable edit (never straight into the game), and the user then saves or deploys as usual. World-file restore goes through the existing World-tab apply path, so the running-game guard and .forge-orig backups still apply.
   - Optionally add 'Snapshot all changed maps' later. Vanilla's SaveAllLevels(1,0) covers every edited map, but FableForge only has one open doc.

5. Automation (docs/AUTOMATION.md): add `autosave_now`, `assert_autosave_exists 0|1`, `snapshot`, `restore_snapshot <i>`. Add a test that sculpts, calls autosave_now, reopens, recovers and checks assert_heights_changed 1.

Effort: M (about 1 day; the async copy and dialogs already have patterns in editor.cpp). Value: high. Terrain edits currently have no crash safety at all, and loose-install drafts have no restore point short of retail.

#### World tab cannot create, remove or rename regions or set their display name, def, minimap or world-map flag
*gap, effort M, value high*

**Vanilla:** CRegionDialog (Maps and Regions > Regions radio) shows a tree of regions with their maps and supports: CreateNewRegion 0x02965870 (CEditWorldMap::CreateNewRegion plus GetUniqueNameForRegion), RemoveSelectedRegion 0x02967a30 with 'confirm removal', RenameSelectedRegion 0x02965f80, RenameSelectedRegionDisplayText 0x029668b0, and the Region def text box (SetRegionDefTextBoxText 0x02963ee0). All of it is saved by EditSaveRegionsToString 0x0296b850 (RegionName, NewDisplayName, RegionDef, AppearOnWorldMap, MiniMapGraphic, MiniMapScale/Offsetâ€¦).

**Fix:** Plan (effort M, value high):

1. forgecore wld (libs/forgecore/include/forge/wld.hpp + src/wld.cpp)
   - Add renameRegion(old, new). It rewrites the RegionName line and every cross-reference, keeping the other lines byte-for-byte.
   - Add removeRegion(name). It deletes the NewRegion..EndRegion block, then the other regions' SeesMap and region lookups are fixed up.
   - Add uniqueRegionName(base). Port GetUniqueNameForRegion 0x02965d60: decompile it first to copy its exact suffix scheme; do not invent one.
   - Mirror these in forge::bwd::File. Check how compileFromWld (overworld.cpp:488/524) assigns region slots. If slots are positional, removal shifts every later region's index, and the BWD, the .gtg REGION_ENTRANCE_POINTs and saves all key on it. So removal must re-slot consistently or be refused.
   - Decompile RemoveSelectedRegion 0x02967a30 to see what vanilla does with the maps the region still contains: does it unassign them ("Unassigned" caption) or refuse?

2. overworld (src/overworld.hpp/.cpp)
   - Add a RegionEdit {op: create|rename|remove|props, name, newName, RegionProps}.
   - Extend applyWorldEdits(gameRoot, moves, owners, sees, regionEdits, ...) so region edits apply to the WLD before the owner and sees edits, then compileFromWld. That way a new region can be given maps in the same write, all with the existing one-time .forge-orig backups.
   - Fold setRegionProperties' body into that path so there is one writer and it stays reachable from the CLI.

3. GUI (gui/world.cpp)
   - Add a Maps | Regions segmented switch to the World tab, the equivalent of vanilla's MAPS/REGIONS radio in CMapsAndRegionsDialog.
   - Regions view: list world_.regions with owned and sees counts, plus a "New region" button that picks a name via uniqueRegionName.
   - Selecting a region fills its owned maps on the canvas and outlines the maps it sees. It also opens a region card with:
     - Name (rename)
     - Display name
     - Region def: a combo over REGION_* defs from the game data catalog
     - Minimap graphic: a combo over MINIMAP_* entries
     - "Appears on the world map" checkbox
     - Minimap framing button, reusing forge/minimapframe as in the new-level path
     - A danger "Remove region" button behind confirmRow. It is disabled while the region owns maps, or follows whatever 0x02967a30 is shown to do.
   - Store the edits in a worldRegionEdits_ vector alongside worldOwnerEdits_ and worldSeesEdits_. They count in worldPendingCount and are covered by worldPushUndo. Let the owner combo list pending new regions too.
   - Call raiseRule("region") on create, remove and rename, because saves cache the region table.
   - Register automation widgets and add a tests/ui script.

#### Cannot place an existing .lev (from another world, the dev tree or another modder) into the world
*gap, effort M, value high*

**Vanilla:** World Map Placement 'Select Map' (CWorldMapPlacementDialog::ChooseMap 0x02b32f70, a GetOpenFileName 'Level Files (*.lev)' dialog) then SelectMapForPlacement 0x020446c0 and PlaceSelectedMapAt 0x02044c20, which calls AddLevelFromFileWithUID 0x0296ade0 after CanPlaceMapAt 0x029738a0 (overlaps refused, and 'You cannot load the same level in twice').

**Fix:** 1. Backend (src/worldedit.hpp/.cpp). Add `struct ImportLevelRequest { ProgressFn progress; std::filesystem::path levPath, tngPath /*optional; empty = sibling <stem>.tng if present, else empty .tng*/; std::string name /*default: file stem*/; std::string hostRegion; OwnRegion ownRegion; int worldX, worldY; bool rebuildNav=false; }` and `bool importLevelFromFile(gameRoot, req, const ThemeLibrary&, NewLevelResult&, std::string& error)`. Build it by splitting createBlankLevel (worldedit.cpp:517ff) into two parts:
   (a) The "author LEV" step, which import replaces with reading the file.
   (b) A shared helper `bakeAndInstall(gameRoot, lev, levTmp, tngBytes, name, region, origin, ownRegion, lib, out, error)`. It holds everything after the LEV is authored: the rebaseThemePalette step, the heightfield, paletteMaterials, bakeLodAlbedo, buildTerrainChunk64, the minimap bake, the backupOnce calls, installLevel with ir.levBytes/ir.tngBytes/ir.chunkBytes, finishDedicatedRegion and defaultEntrance.
   Both createBlankLevel and importLevelFromFile call (b). The donor-name lookup and the STB relocation in createLevelFromDonor are bypassed completely.

2. Checks inside importLevelFromFile, in order:
   - Width and height must be multiples of 16 (the rule createBlankLevel already enforces).
   - Mirror "You cannot load the same level in twice": case-insensitively refuse any name already in the BWD maps or in wld.findMap("FinalAlbion\\"+name+".lev"). Offer a rename rather than a hard fail.
   - Mirror CanPlaceMapAt (0x02973b10, box overload): refuse the origin if the box [x, x+w) x [y, y+h) overlaps any used BWD map box. Reuse the World tab's checkMove overlap test.
   - After rebaseThemePalette, list any palette slot whose theme name has no def in this install, or for which paletteMaterials reports available=false. Block with the missing names, reusing the theme-add palette check, instead of baking white terrain.
   - Nav: if the imported LEV's nav section is empty or unparseable, or the user ticked "Rebuild navigation", run navmesh::generateTerrain the same way createBlankLevel does. Otherwise keep the file's nav.

3. .tng handling. Parse it with the existing TNG reader used by leveledit.cpp:175ff. Remap every thing UID that collides with a UID already used in the world to a fresh one above the world's current maximum, and rewrite any links or references to the remapped UIDs. Record in out.notes how many UIDs were remapped. The map UID needs no work: installLevel already assigns one (worldinstall.cpp:157).

4. GUI (gui/editor.cpp:2168 New level card). Add a third segment, "From a .lev file", to `theme::segmented("##newlevelmode", ...)`, with:
   - a Browse button using the app's existing file-dialog helper, filtered to *.lev, that also picks up a sibling .tng;
   - a read-only line showing the size and palette theme names;
   - the existing name, origin, region and own-region controls;
   - the name defaulting to the file stem.
   Put the size, theme and overlap problems inline, before the primary button. In startNewLevel (gui/editor.cpp:2267), add a `newLevelMode_ == 2` branch that mirrors the blank branch (intoPack support included) and calls importLevelFromFile.
   When a loose file is open (a MapEntry with key "file:*", gui/app.cpp:686), pre-select this mode and prefill the path from m.loosePath, so File > Open level leads straight to "Place in world". Also add a "Place a .lev file..." entry point on the World tab (gui/world.cpp) that preselects the clicked empty grid spot as the origin, matching vanilla PlaceSelectedMapAt 0x02044c20, where you click on the world map to place.

5. CLI and automation. Add a `forge level import <file.lev> --name --at X Y --region R [--own-region]` command next to src/cli/levels.cpp:101/134. Register the new widgets with auto_.registerWidget ("seg_new_level_mode" option 2, "btn_import_lev_browse") so a tests/ui script can cover the path. Add a scratch-install test that imports a dev-tree test level (for example Blank.lev from the Lionhead Data/Levels) and checks the BWD/WLD records, the STB static map, the absence of duplicate thing UIDs, and that a second import of the same stem is refused.

Effort M (mostly refactoring existing bake/install code plus TNG UID remapping). Value high (dev-tree test levels, other worlds' maps and other modders' levels become usable).

#### Closing FableForge silently drops unsaved object, terrain and world edits
*weakness, effort S, value high; implemented 2026-09-30*

2026-09-30: implemented close prompt for window close/File > Exit, with Save,
Discard and Cancel. Pending World writes wait for success before exit and survive
failure with undo history. Native close, map-switch and failed-write UI scripts
pass; terrain still uses its separate explicit save action.

**Vanilla:** File > Exit (MENU_ITEM_FILE_QUIT) and Alt+X both call SaveAllIfChanged(1,0) 0x0204b550 before OnExit (menu handler 0x02947fc0). Vanilla never loses edits on exit.

**Fix:** 1) gui/main.cpp wndProc: add `case WM_CLOSE: if (g_app && !g_automated) { g_app->requestClose(); return 0; } break;` so the window is never destroyed directly. In automated runs, keep the current behaviour, or route through requestClose when a script sets promptInAuto_. The main loop already stops on app.wantsQuit() (main.cpp:274) and then calls DestroyWindow at :290, so no other change is needed there. 2) gui/app.hpp: add `bool closePending_ = false; void requestClose();`. Leave the existing requestQuit() (quit_=true) as the forced path. 3) App::requestClose(): if hasUnsavedEdits() || worldPendingCount()>0, set closePending_=true; otherwise set quit_=true. 4) gui/editor.cpp drawUnsavedPrompt: make the early return `if (pendingSelect_.empty() && !closePending_) return;`. Pick the body text by mode: in close mode say "Closing FableForge drops them", and add a line "N pending world change(s) not written" when worldPendingCount()>0. Buttons in close mode: Save (saveDocument(), plus the World tab's existing write path at world.cpp ~185 when worldPendingCount()>0; set quit_ only if !hasUnsavedEdits() && worldPendingCount()==0 after that, and if the world write is async (worldFuture_), wait for it before quitting); Discard (quit_=true); Cancel (closePending_=false). Keep the "Save terrain into the game" hint for terrainDirty. Make sure App::frame draws the prompt when closePending_ is set, not only when pendingSelect_ is. 5) gui/app.cpp:1495: File > Exit calls requestClose() directly. Keeping the WM_CLOSE post also works once step 1 is in. 6) Automation: add a `close` command in gui/automation.cpp that calls app.requestClose(), next to the existing `quit` at :574. Add tests/ui/quit_prompt.txt: dirty the doc (move an object), `close`, expect the btn_unsaved_cancel widget and that the app is still running, click cancel, then `close` again, click btn_unsaved_discard, and expect exit. Effort S, value high.

#### No map resize
*gap, effort L, value med*

**Vanilla:** Popup RESIZE_MAP (SelectMapForResize 0x02044e30), then drag the new box, then ApplyMapResize 0x02044f60. The rules: area > 0 ('The map must have an area greater than zero!'), the new box must overlap the original ('The new map area must overlap the original map area.'), and it must not overlap another map (CanPlaceMapAt on the four grown strips; 'The new map area overlaps another map.'). CEditWorldMap::EditResizeMap 0x02974110 kills things outside the new area (CThingFilter_InArea negated), resizes the game map (CThingMapManager::ResizeGameMap), and calls CEditMap::EditResizeMap 0x029a8f40 â†’ CHeightMap::Resize 0x029e27c0 + CGameMap::Resize 0x024a0160 + SetDimensions, then SetMapPlacement and a re-added engine map. Only allowed on editable maps.

**Fix:** 1. forgecore LEV resize (libs/forgecore/src/lev.cpp, lev.hpp): add `File resized(int newW, int newH, int offX, int offY) const`, a port of CHeightMap::Resize 0x029e27c0.
   - Build a (newW+1)*(newH+1) grid of 21-byte cells. Initialise every cell from a "default cell" that has the u32 21 / u8 7 record header, height 0, theme slot 0, strength 0 and walkable/camera flags cleared, matching the default-constructed CHeightField cell. Offer an option to use the template ground height instead: vanilla fills with defaults, but a UI "fill new ground at edge height" toggle is useful.
   - Copy each old cell (x,y) to (x-offX, y-offY) when it lands in range. That is the vanilla loop: src index from the arg+0x10/+0x14 offsets, bounded by `this+0x20c`/`+0x210`.
   - Resize the (w/4)*(h/4) game-map grid the same way at cell/4 granularity, porting CGameMap::Resize 0x024a0160; decompile it first to confirm the default atmos/sound values.
   - Rewrite the header width/height and serialize fully. This needs a new full writer, because save() today only patches the original bytes in place.
   - Require newW and newH to be multiples of 4, and the offsets too, so the game-map grid stays aligned. World boxes snap to 32 anyway.
   - Add a round-trip test: resized(w,h,0,0) must be byte-identical to the original.

2. Validation (src/overworld.hpp/.cpp): add `struct MapResize{name; x0,y0,x1,y1;}` and `checkResize(layout, pending, r, why)` with vanilla's three rules and exact messages:
   - (x1-x0)*(y1-y0) > 0, else "The map must have an area greater than zero!"
   - the box intersects the original box, else "The new map area must overlap the original map area."
   - the four grown strips (new box minus old box) are free of every other box, the CanPlaceMapAt 0x02973b10 check; else "The new map area overlaps another map."
   - plus the existing 32-snap and kWorldExtent/u16 grid limits from checkMove.
   - Refuse locked or retail-protected maps the same way the lock rule does, since vanilla only allows it on editable maps.

3. Things (.tng): port the CEditWorldMap::EditResizeMap 0x02974110 filter, i.e. CThingFilter_InArea negated. Collect the things whose map-local position falls outside the new area. Show them in the confirm dialog with a count and names, and delete them. Translate every surviving thing by (-offX*cellSize, -offY*cellSize), because .tng positions are map-local and the origin moves when the left or top edge moves.

4. Apply (src/overworld.cpp applyWorldEdits, or a new applyResize with the same .forge-orig backup discipline):
   - write the resized LEV through levelstore, so both WAD and loose installs work
   - update WLD MapX/MapY/width/height and recompile the BWD (compileFromWld, byte-check path)
   - re-bake the map's STB chunk from scratch through the createBlankLevel bake path in src/worldedit.cpp. A translate does not work here because the chunk's patch count changes. Check that the bake no longer depends on a same-size retail template, or pick the nearest template for header/palette only.
   - re-bake the touching neighbours' seams with stitchNeighbours
   - regenerate the minimap (bakeMinimap) since its aspect changes
   - log that nav for this map is stale and point to the nav lane.

5. GUI (gui/world.cpp):
   - Add a "Resize" toggle on the selected map's card, next to "Move here" (~line 487). In resize mode, draw 8 edge/corner handles on the selected box and drag them snapped to 32. Colour the box by live checkResize and show the `why` text, the same way worldDragValid_/worldDragWhy_ work today.
   - Add numeric x0/y0/x1/y1 fields as an alternative to dragging.
   - Queue the resize as a pending change that "Apply" shows ("Resize N maps"), with a confirm that lists the things to be deleted and the nav warning.
   - Register automation widgets (btn_world_resize, btn_world_resize_apply) per docs/AUTOMATION.md, and add a `forge world resize` CLI verb.

Effort L: the full LEV writer, the GameMap resize, re-baking a chunk of a new size, and tng translate/delete. Value med.

#### Cannot remove a map from the world
*gap, effort M, value med*

**Vanilla:** The per-map popup's DELETE_MAP calls CEditControlCentre::RemoveMap 0x0204c460 (only when IsMapEditable): CEditWorldMap remove through its vtable, then SetWorldAsChangedSinceLoad, Backup, InitRegionDialog and UpdateRegionExitConnections. It is called from 0x028e5260.

**Fix:** Plan (effort M, value med):

1. forgecore wld (wld.hpp/wld.cpp):
   - Add `bool removeMap(std::string_view levelName)`.
   - It drops every ContainsMap/SeesMap line naming the map, in every region (loop the existing removeMapFromRegion with alsoSees=true).
   - It then deletes the map's NewMap...EndMap block from rawLines_/maps_.
   - Leave MapUIDCount unchanged, so UIDs are never reused.
   - Every unrelated line must stay byte-identical. Add a round-trip test: install, remove, and the WLD equals the pre-install bytes when the map was the last one appended.

2. forgecore bwd (bwd.hpp/bwd.cpp):
   - Add `void retireMap(int slot)`. It erases the slot index from every Region::contains/sees and never renumbers slots, because region records and saves hold slot indices.
   - If the slot is the last one and was added by FableForge, truncate it instead, so the file returns to exactly its pre-install shape.
   - Only after checking CMapInfo::LoadBinary 0x4fb4f0 and its consumers in Fable.exe should the code also set used=0 (and flag2=0) on non-tail slots. Until that check is done, unreferencing alone makes the map unreachable.

3. Other files:
   - Remove the map's REGION_ENTRANCE_POINT / level entries that install-level added to FinalAlbion.gtg. Mirror worldinstall.cpp so each thing it writes has a matching undo, e.g. a shared `worldinstall::uninstallLevel(root, levelName)` in libs/forgecore/src/worldinstall.cpp.
   - Leave the STB chunk in place as dead bytes. compact-stb (gui/app.cpp:430 compactBank) reclaims it, so the notice should point there.
   - Use the same staging and commit path as install-level (tools/forge-cli/main.cpp:3697/3730), so a failure leaves the install untouched.
   - Run the vanilla follow-ups from CEditControlCentre::RemoveMap 0x0204c460: mark the world changed, take a backup, then refresh the region list and the region exit connections. In FableForge that means rescanning world_ and the region list, and warning if any region exit still targets the removed map.

4. GUI (gui/world.cpp Selected-map card, after the owner combo ~line 513):
   - Add a theme::dangerButton "Remove from worldâ€¦" wrapped in confirmRow, like the restore confirm in app.cpp:422.
   - Confirm text: "saves that visited this map keep cached region data; test on a new game".
   - Enable the button directly for maps whose .lev backups::scan reports as Kind::Created (src/backups.cpp:81). For retail maps, add a second "remove a retail map" confirm, which mirrors vanilla's IsMapEditable gate. Refuse while Fable runs.
   - Register an automation widget such as btn_world_remove_map.

5. CLI: add `forge world remove-level <root> <name>` next to install-level (main.cpp:11362 dispatch) and document it in docs/AUTOMATION.md.

#### Opening another .wld does not switch the World tab or the quest list to it
*weakness, effort M, value med*

**Vanilla:** File > Load World â†’ LoadWorld 0x02038d90 replaces the whole editing world (maps, regions, world map view) and loads the matching quest file, so the section list comes from that world's registered quests.

**Fix:** 1) src/overworld.hpp/.cpp: add `loadWorldLayoutFromWld(const fs::path& wldPath, WorldLayout&, std::string& err)`. It parses the .wld (regions, contains, sees and mapX/mapY, reusing the region loop at overworld.cpp:143-150). It builds each WorldMapBox from the .wld map list. Box w/h come from the <wld dir>/<LevelName> .lev header (width/height in tiles, the same units the BWD rect uses). Load <stem>.bwd only if it exists. Set inStb=false and slot=map index. Keep loadWorldLayout(gameRoot) as the FinalAlbion wrapper. 2) Add `saveWorldLayoutText(wldPath, moves, owners, seesEdits)`. It rewrites only that .wld: MapX/MapY, ContainsMaps and SeesMaps, going through the forge::wld writer that the FinalAlbion path already uses. There is no BWD, mirror or STB step. If no <stem>_RT.stb exists, return the note "text world: no static-map bake". 3) gui/world.cpp App::loadWorld: key the cache on the chosen world (add `std::string worldTarget_`, empty meaning FinalAlbion). When worldTarget_ is set, call loadWorldLayoutFromWld. Route apply and save to saveWorldLayoutText, and disable the STB, pack-shadow and 3D-bake actions with a tooltip. 4) World tab header: add a small combo listing FinalAlbion plus each distinct MapEntry.worldFile from maps_. openWorld (gui/app.cpp:617) sets worldTarget_ to the new .wld and clears worldLoaded_. selectMap on a map with worldFile set also switches it. 5) gui/app.cpp:773 and gui/editor.cpp:3525 (fit-to-neighbours) currently bail for other worlds. Point them at the active layout so neighbours work there too. 6) Add a tests/ui script: open lake-n-shack.wld, switch to World, and assert the region and map count match the .wld. Leave out the quest-file loading from the original fix (sections already come from the map's .tng). Effort M, value low-med.

#### Region visibility (SeesMap) is editable only for touching neighbours, and a region's full sees list is not shown
*weakness, effort S, value med*

**Vanilla:** The Map Visibility dialog (CMapVisDialog) takes the clicked map's region (SetEditedMap 0x028e6ad0 â†’ GetRegionNumberMapIsIn â†’ GetRegion), lists that region's GetVisMaps (UpdateVisListBox 0x028e6b60), and AddVisibleMap 0x028e6cc0 adds any clicked map that is not already owned or seen. The Regions tree has 'Visibility' mode add/remove for any map (AddMapToSelectedRegion 0x02968eb0 pushes into GetVisMaps). Retail relies on non-adjacent sees: PicnicArea sees 22 maps including OrchardFarm and GreatwoodBanditToll, and LookoutPoint sees Greatwood_1/2 (from `forge world --regions`).

**Fix:** All changes are in gui/world.cpp plus one pick-mode member in gui/app.hpp. worldSetSees and its undo and pending plumbing are reused as they are.

1) Add a "Seen from <owner region>" card between the Selected map card and the Neighbours card, at about line 516.
   - Take `mine = worldOwnerOf(box->name)` and `r = world_.region(mine)`.
   - Build the effective list: r->sees, minus worldSeesEdits_ entries for that region with sees=false, plus entries with sees=true.
   - Show one row per map: the name, a muted tag "(touching)" or "(distant)" from current.touching, and a small x button that calls worldSetSees(mine, m, false).
   - Clicking a row name calls worldSelect(m) so the view jumps to that map.
   - Register the widgets `list_world_sees` and `btn_world_sees_remove_<i>` for automation.
   - Also add a small count on the Selected map card: "seen by N regions". Compute it by scanning world_.regions through worldSees(r, box->name). This mirrors UpdateVisListBox 0x028e6b60, which lists the edited map's region's GetVisMaps.

2) Add an "Add seen mapâ€¦" button to the new card that sets a new member, `std::string worldSeesPickRegion_ = mine`.
   - In the canvas click handler (gui/world.cpp:331), check this before the drag branch. While worldSeesPickRegion_ is set and worldHover_ is non-empty:
     - Refuse with a log line if the region owns the map (worldOwnerOf(worldHover_) == region) or already sees it (worldSees). This is the vanilla AddVisibleMap 0x028e6cc0 rule.
     - Otherwise call worldSetSees(region, worldHover_, true).
     - Clear the pick mode and do not start a drag or change the selection.
   - Esc or right-click cancels.
   - Show a legend hint while picking: "click a map for <region> to see â€” Esc cancels".
   - Optionally add a "Remove by click" mode that calls worldSetSees(region, hover, false). This matches vanilla Visibility mode, which both adds and removes.

3) Canvas overlay, in the draw loop at lines 371-409. When a map is selected and its owner is known, draw every map that worldSees(owner, b->name) with a dashed outline (short AddLine segments) in regionColour(ownerSlot, 0.9f). Draw it under the neighbour, pending and selection outlines so those still win. Add "dashed = seen from <region>" to the legend at line 432.

4) Keep the Neighbours card as the quick two-way toggle for adjacent maps.

5) Add a UI test in tests/ui/ with this sequence:
   - select PicnicArea;
   - assert that the list shows OrchardFarm;
   - remove it, then add it back through pick mode;
   - check the pending count;
   - undo.

Effort S (about 100 lines of ImGui, no format or writer changes). Value med. Vista and distant loading is what retail regions rely on, and today it can only be edited by script.

#### No per-map flags on the World tab: Sea, 'Load when player near', script name
*gap, effort S, value med*

**Vanilla:** CWorldMapPlacementPopupDialog::OnChildWindowLeftRelease 0x028e50e0 toggles IS_SEA (CMapInfo sea flag, then SetWorldAsChangedSinceLoad) and LOAD_MAP_WHEN_PLAYER_NEAR (CWorldMap::SetMapAsLoadedOnPlayerProximity 0x01c957f0). OnChildWindowContentsChanged 0x028e5ba0 edits SCRIPT_NAME (SetLevelScriptName, the WLD LevelScriptName / BWD scriptName). Retail uses these: 14 sea maps, and 151 of 400 maps with LoadedOnPlayerProximity TRUE.

**Fix:** 1. forgecore wld (libs/forgecore/include/forge/wld.hpp and src/wld.cpp): add `void setMapFlags(std::string_view levelName, std::optional<bool> isSea, std::optional<bool> loadedOnProximity, std::optional<std::string> levelScriptName);`, modelled on relocateMap. It rewrites only the IsSea / LoadedOnPlayerProximity / LevelScriptName raw lines inside that map's NewMap..EndMap block, using the same formatting as the addMap writer (wld.cpp:229-233: `IsSea TRUE;`/`FALSE;` and the quoted script name via quoteWld). It updates maps_ too and keeps every other line byte-identical. If a key is missing from the block, insert it after MapY. Add a round-trip test: an unmodified file stays byte-identical, and a single-flag edit changes only one line.

2. src/overworld.hpp: add `struct MapFlagEdit { std::string map; std::optional<bool> isSea, proximity; std::optional<std::string> scriptName; };` and a `const std::vector<MapFlagEdit>& flags` parameter on applyWorldEdits. Extend the "nothing to do" check to include it.

3. src/overworld.cpp, in the WLD/BWD block of applyWorldEdits (after the setMapOwner loop, before serialize):
   - Resolve each edit with before.find(map), then call wld.setMapFlags(box->levelName, ...).
   - After writeFile(wldPath), patch the on-disk BWD record directly: `auto& m = bwd.maps().at(slot-1); if (isSea) m.isSea = *isSea; if (proximity) m.loadedOnProximity = *proximity; if (scriptName) m.scriptName = *scriptName;`. Do not route this through compileFromWld; the existing code deliberately keeps the other BWD fields from disk.
   - Push a note per edit. The mirrors at gameRoot/FinalAlbion.bwd and Levels/FinalAlbion/FinalAlbion.bwd are already rewritten by the existing loop.
   - Validate the script name: non-empty, a C identifier, and unique across the other maps' scriptName (refuse otherwise).

4. gui/app.hpp:793: add `std::vector<editor::MapFlagEdit> worldFlagEdits_`. Add it to WorldSnap, worldSnapshot() and worldRestore (gui/world.cpp:161), include it in worldPendingCount and world_revert, and pass it into both applyWorldEdits calls (gui/world.cpp:203 and :215, the pack shadow path and the game path). Add a helper `worldSetFlag(map, ...)` that merges into an existing entry for that map and drops the entry once it equals the on-disk box values.

5. gui/world.cpp Selected-map card (after "Owned by region", around line 525): add a small "Map settings" group.
   - Checkbox "Sea map", tooltip: "IsSea: the engine treats this map as open sea (vanilla popup IS_SEA). Retail has 14."
   - Checkbox "Load when the hero is near", tooltip: "LoadedOnPlayerProximity: the map is streamed in when the hero approaches it, not only when its region loads. Retail: 151 of 400 maps."
   - InputText "Script name", tooltip: "LevelScriptName: the name quest scripts use to refer to this level. Renaming it breaks scripts and saves that use the old name." Show a Warn-coloured line when the value differs from the stem or from the current value.
   - Show pending values next to the move line, using the same style as "pending: x,y -> x,y".
   - Register widgets check_world_sea, check_world_proximity and input_world_scriptname.

6. gui/automation.cpp and docs/AUTOMATION.md: add `world_flag <map> sea|proximity <0|1>` and `world_scriptname <map> <name>`, plus state keys that expose the selected map's flags.

Effort S-M: the wld line-editor and the plumbing through two applyWorldEdits call sites and the undo snapshot make it a bit more than S. Value med.

#### Quest sections accept any typed name; no picker from registered quests and no orphan-section warning
*weakness, effort S, value med*

**Vanilla:** The section list is filled from CQuestManager::GetRegisteredQuestNames at LoadWorld (AddPrecreatedQuest 0x028c61d0), i.e. the world's .qst AddQuest names. There are also Select all / Deselect all buttons. When a map is unlocked, SetMapAsEditable calls RemoveThingsInNonExistantQuests 0x02035580 (called from 0x0204c6a4): it collects things in any quest whose GetSerialisationSectionName is not registered and prompts ('Missing Section Name' / keep these things?). Things in an unregistered section never load in-game.

**Fix:** 1) Registered-quest set: add `std::set<std::string> registeredQuests_` (stored lowercase) and `uint64_t registeredQuestsStamp_` to App (gui/app.hpp). Add `App::refreshRegisteredQuests()` next to texturesBigPath in gui/textures.cpp, or in editor.cpp. It uses the same lookup order as texturesBigPath: first saveRoot()/data/Levels/FinalAlbion.qst, then installPath_/data/Levels/FinalAlbion.qst. Parse the file with forge::qst::File::parse (libs/forgecore/include/forge/qst.hpp:35). For every statement where isQuest() is true, insert lowercase(args[0]). Call it when a document loads and when mods are applied or deployed, because a quest deploy adds an AddQuest line. Leave the set empty if the file is missing or fails to parse, and in that case turn every check below off (show one hint: "FinalAlbion.qst not found: quest registration not checked"). Treat NULL as always registered.
2) Orphan marking, vanilla RemoveThingsInNonExistantQuests 0x02035580: in the drawSectionsCard row loop (editor.cpp ~335-352), take `base = splitDayNight(n).first`. When base is not NULL and not in registeredQuests_, draw a warning glyph or warn-coloured text on the row. Its tooltip reads: "'<name>' is not an AddQuest in FinalAlbion.qst: its N things will not load in-game." Add a small "Move to NULL" button on that row. It loops over every thing whose sectionOf() matches the section, in both the plain and the %DayOnly/%NightOnly variants, and calls doc_.moveToSection(i, "NULL"). Iterate indices from high to low, or re-query after each move, because moveToSection returns a new index. Set sectionsDirty_ afterwards. Offer the move instead of copying vanilla's delete/keep prompt: keeping the things (vanilla's "keep") is still the default because nothing happens unless the user clicks.
3) Picker in place of the free text box (editor.cpp ~359-367): use an ImGui combo or filtered popup whose items are the registered quest names that are not yet sections in the .tng. The filter is the existing newSection_ buffer. Picking an item calls doc_.addSection(name) + setPlacementSection. Typed text that matches no registered quest still works through "Add" (for quests the user will register later), but the button label or hint then says "not registered in FinalAlbion.qst: its things will not load until it is", and the pushLog is a warning (level 1). Keep the automation ids input_new_section and btn_add_section so docs/AUTOMATION.md scripts keep working.
4) Select all / Deselect all, from the vanilla QUEST_NAMES_COMBO_BOX handler 0x028c3ac0: add two small ghost buttons above the row list. "Show all" runs hiddenSections_.clear(). "Hide all" inserts lowercase(n) for every entry in `shown`. Both set sectionsDirty_ = true. Register them as btn_sections_all and btn_sections_none.
Effort S (about 80-120 lines, all in gui/editor.cpp plus a few members in gui/app.hpp; no format work because forge::qst already parses the file). Value med: it stops things silently disappearing in-game, which is currently the easiest way to lose placed content.

#### View menu lacks per-type show/hide (Creatures / Buildings / Objects / Holy Sites / Villages)
*gap, effort S, value med*

**Vanilla:** View > Show &Creatures/&Buildings/&Objects/&Holy Sites/&Villages â†’ SetThingDrawing(type,on) 0x02036ee0. Not saved to the level ('This hiding or showing of Things won't be saved out.'). The state persists in editor settings (ShowingCreatures â€¦ ShowingVillages, SaveSettings 0x0203d9c0).

**Fix:** 1. Settings (gui/app.hpp settings struct and its load/save code): add `unsigned hiddenThingKinds = 0;` as a bitmask. Bits: Creature=1, Building=2, Object=4, HolySite=8, Village=16. It persists like showExplorer and is never written to the level, matching vanilla's ShowingCreatures..ShowingVillages behaviour.

2. Classifier (new static helper in gui/editor.cpp): `unsigned thingKindBit(const std::string& type)`.
   - Compare case-insensitively.
   - "creature" or "aicreature" returns 1, "building" 2, "object" 4, "holysite" 8, "village" 16.
   - Anything else (markers, switches, ...) returns 0, meaning never hidden. Vanilla has no toggle for those either.
   - Do not reuse bg::kindOfThingType. It merges holy sites and villages into kOthers.

3. applySectionVisibility (gui/editor.cpp:787-798):
   - Add settings_.hiddenThingKinds to the early-out key: keep a `kindsApplied_` member and compare it.
   - Compute `hidden = thingHiddenBySection(per,t) || (settings_.hiddenThingKinds & thingKindBit(doc_.summary(t).type))`.
   - Use whichever type accessor the section code already has; the Village/HolySite checks at editor.cpp:2930 show that `s.type` exists.
   - Hidden instances then become unpickable automatically (renderer.cpp:1069/1089).
   - Also skip hidden kinds in trackNodeAt (editor.cpp:815) and in any 2D thing-marker overlay draw so that hidden things are not selectable there either.
   - After a toggle, set `sectionsDirty_ = true`.

4. View menu (gui/app.cpp, after line 1500): add a separator and a "Show" sub-group with five checkable MenuItems: Creatures, Buildings, Objects, Holy sites, Villages. Each one XORs its bit, calls saveSettings() and sets sectionsDirty_. Tooltip: "Only hides it in the editor; the level is not changed".

5. Optionally, add a small caret popover next to the Objects layer chip (gui/app.cpp:1933) with the same five checkboxes. Register them with auto_.registerWidget so AUTOMATION.md can drive them.

6. If a selected thing becomes hidden, clear the selection so a hidden object cannot be edited by mistake. This is the same policy the section hiding follows.

Effort S (roughly 60-80 lines). Value med.

#### Editor session (last map, tab/mode, camera) is not restored
*weakness, effort S, value med*

**Vanilla:** SaveSettings 0x0203d9c0 / LoadSettings 0x0203cdc0 write StartEditorSettings ViewMode EditMode CamPos CamViewDir EngineGridFlag SnapToGridFlag Showing{Creatures,Buildings,Objects,HolySites,Villages} ShowDetailMode EditingScriptBrush EndEditorSettings, so the editor reopens in the same mode and camera.

**Fix:** 1) In saveSettings (gui/app.cpp:518-525), add these keys:
- "lastMap": selectedName_ (the MapEntry key)
- "terrainMode": terrainMode_
- "camera": {"map": selectedName_, "pos": [posX, posY, posZ], "yaw", "pitch", "distance"} from camera_
- the World-tab zoom and pan, if worldview.cpp keeps them in App members

Map-scoped camera: store it next to its map name so it is applied only to that map. Keep the existing `if (auto_.active()) return;` guard.

2) In loadSettings (gui/app.cpp:486-511), read the new keys into these members:
- pendingRestoreMap_
- terrainMode_, clamped to the valid mode range
- savedCam_ (a Camera) plus savedCamMap_

3) At the end of init (gui/app.cpp:164, after scanInstall succeeds), call selectMap(pendingRestoreMap_) when all of these hold:
- automation is off,
- a map is saved,
- that map is still in maps_.
selectMap already ignores unknown keys (gui/app.cpp:706).

4) In the preview-upload path (gui/app.cpp:992), when r.name == savedCamMap_ and savedCam_ is set:
- call renderer_.upload(r.scene, camera_, false),
- assign camera_ = savedCam_,
- set lastFramedFor_ = r.name,
- clear savedCam_ so it applies only once.

5) Save settings on selectMap and in setEditTab/setTerrainMode, or only on exit as gui/main.cpp:278 already does. The camera only needs saving on exit.

6) Vanilla's Showing{Creatures,Buildings,Objects,HolySites,Villages}, snap and grid flags: persist them the same way once FableForge has those toggles. Creatures is already saved as settings_.creatures.

Do not restore the edit mode if the document fails to open. setEditTab already falls back to tab 0 when a map has no terrain (gui/app.hpp:364). Effort S, value med.

#### File > New World
*gap, effort M, value low*

**Vanilla:** File > &New World â†’ SaveAllIfChanged, then NewWorld 0x020454d0: an empty world to place levels into (then saved as a new .wld).

**Fix:** 1. forgecore: add `static File forge::wld::File::makeEmpty(int mapUidCount = 0, int thingUidCount = 0)` in libs/forgecore/src/wld.cpp. It returns parseText() of a minimal template: `MapUIDCount 0;`, `ThingManagerUIDCount 0;`, `START_INITIAL_QUESTS` / `END_INITIAL_QUESTS`, and no maps or regions. Before writing it, check the exact header lines and ordering against FinalAlbion.wld, and against what NewWorld 0x020454d0 resets (disassemble it with capstone from FableWin.exe to confirm the defaults it zeroes). Add a round-trip test in tests/test_export.cpp: makeEmpty().serialize() must re-parse, and addMap followed by addRegion must work on it.
2. GUI: add "New world..." at the top of the File menu (gui/app.cpp:1488). First run the existing unsaved-edits prompt (the SaveAllIfChanged equivalent). Then open a save dialog for <folder>/<Name>.wld, refusing the install's data/Levels/FinalAlbion.wld. Write makeEmpty().serialize() to that path and call openWorld on it. Tooltip: "For total conversions / dev: the game only loads FinalAlbion.wld".
3. Relax App::openWorld (gui/app.cpp:648). When world.maps() is empty, still register the world in a new `openedWorlds_` list (path and stem) with a placeholder explorer group "<stem>: (empty world)". Log "Opened empty world X" and return true, not false. This keeps the empty-world case apart from the "listed levels missing" error.
4. Let the existing new-level flow (the newLevelFuture_ job, and overworld.cpp's world write at :509) take a target .wld path, defaulting to FinalAlbion or the pack's world. Add the opened worlds to its target combo so levels can be created into the new world. The world gets its first map through wld::File::addMap and its first region through addRegion, and the map is then reopened with openWorld.
5. Automation: add a `new_world <path>` verb next to `open_world` in gui/automation.cpp:211, and document it in docs/AUTOMATION.md.
Effort M (steps 1-3 are S; the new-level target plumbing makes it M). Value low.

#### Fit Neighbours is not reachable from the World tab
*weakness, effort S, value low*

**Vanilla:** FIT_MAP sits on the per-map world-map popup (0x028e5442): confirm 'you want to fit this map to its neighbours(this will change height data!)', then EditFitFillerMap(map, Height, Steep, Tension, LoNoise, HiNoise).

**Fix:** 1) gui/world.cpp, Selected-map card, after the owner combo block (before theme::endCard() at ~line 515): add two buttons. (a) ghostButton "Open in editor", which runs the same sequence as the double-click at world.cpp:321-328 (worldDragging_=false; setWorldMode(false); selectMap(box->name); setEditMode(true); pushLog). (b) ghostButton "Fit to neighbours...", which runs that sequence, then setEditTab(1) (the Terrain tab, app.hpp:364) and setFitOpen(true) (app.hpp:548). Register the buttons as widgets btn_world_open and btn_world_fit. Disable both while worldPending_ is non-empty (the same guard as the double-click), with a tooltip saying "Write or put back pending moves first". Fit reads neighbour heights from the written layout (loadWorldLayout in startFitNeighbourLoad, editor.cpp:3525), so unwritten moves would give wrong seams. Also disable Fit when box->inStb is false or the map has no terrain. Tooltip: reuse editor.cpp:3733 plus "changes height data", which stands in for vanilla's confirm text; the Fit window's Now/Fitted preview and explicit Fit button (editor.cpp:3882) already act as the confirmation, so no modal is needed. 2) No change to drawFitWindow is needed. It opens once documentLoaded() is true, because fitOpen_ persists across the asynchronous map load, and it reloads neighbours through the fitNeighboursFor_ != doc_.mapName() check (editor.cpp:3745). Keep the vanilla defaults and do not reset fitParams_. 3) Optional: an automation verb world_fit <map> in gui/automation.cpp next to fit_open (line 326) that does the same sequence, so the flow can be screenshot-tested; then update the ROADMAP row 22 note. Out of scope: the "several fillers at once" part of row 22 and the pack entry point.

#### Options > Snap To Grid (cell-centre placement) has no equivalent
*gap, effort S, value low*

**Vanilla:** Options > &Snap To Grid â†’ SetSnapToGridFlag 0x0203d5d0. GetCurrentPointedAtPos 0x0204b3f8 then calls FindNearestSnappedPos 0x02049280: x,y are truncated to int (_ftol), +0.5 is added (constant 0x401de04 = 0.5), and z = GetGroundSizeZAt. The pointer is snapped to the centre of the 1-unit cell, on the ground, for placement and pointer-driven edits. Persisted as SnapToGridFlag in editor settings.

**Fix:** 1. forgecore helper (libs/forgecore/include/forge/thingplacer.hpp and src/thingplacer.cpp, next to constantPlacementHeight): add `Vec3 snapToCellCentre(float x, float y)`. It returns {float(int(x)) + 0.5f, float(int(y)) + 0.5f}. Use C-style truncation to match _ftol; this differs from floor only for negative coordinates, which Fable maps do not use. The caller fills in z. Add a unit test in the thingplacer tests covering 3.7 -> 3.5, 0.0 -> 0.5 and 12.99 -> 12.5.

2. App state (gui/app.hpp near placeFixedHeight_ at line 384): add `bool snapToGrid_ = false;`, commented as vanilla Options > Snap To Grid, CEditControlCentre::FindNearestSnappedPos 0x02049280. Save and load it wherever the other placement toggles are persisted, since vanilla saves SnapToGridFlag in its editor settings.

3. One pick helper: add `App::placementPoint(float out[3])` (world x, y and z). It takes camera_.focus, converts to world coordinates (x = f[0], y = -f[2]), and, when snapToGrid_ is on, applies snapToCellCentre. It then sets z = doc_.groundHeight(x, y) at the snapped point, which is the order vanilla uses: snap first, then the ground height at the cell centre. After that the Fixed-height rule applies as it does now. Route placeDefinition (editor.cpp:1429-1437), pasteClipboard's anchor (editor.cpp:264) and the other camera_.focus placement sites (1858, 1878, 1976, 2052, 3250, 3285, 3364, 3920) through it. Before switching each site, check that it is a placement or pointer-driven edit and not a camera or sampling use; for example, "Sample here" at 1370 stays raw.

4. UI: add a theme::toggle("Snap to cell centres", &snapToGrid_) in the placement options block after Fixed height (around editor.cpp:1362), with auto_.registerWidget("toggle_place_snap_grid"). Give it the tooltip "New and pasted things land on the centre of the 1 m grid cell, on the ground (vanilla Options > Snap To Grid)." Also add it to the Options/View menu if one exists. Add an automation key "place_snap_grid on|off" in gui/automation.cpp next to place_height (line 297) and document it in docs/AUTOMATION.md.

5. Leave the gizmo drag snap alone; it is a separate feature. Vanilla skips this snap in edit-mode 4 (the other branch in GetCurrentPointedAtPos), so apply it only to thing placement and paste, not to terrain or brush tools.

Effort S, value low.

#### Initial quests (START_INITIAL_QUESTS) not exposed
*gap, effort S, value low*

**Vanilla:** The CInitialQuestsDialog panel (ctor 0x028c1910, shown live on entry) chooses the quests active at world start. CEditWorld::SaveInitialActiveQuests 0x0207ed60 writes 'START_INITIAL_QUESTS;' â€¦ 'END_INITIAL_QUESTS;' into the .wld. Retail lists Q_SunnyvaleMaster, PersonalScriptMain, PersonalScript_GlobalThings, HeroBoasts, V_HeroDolls, CS_PlayCutscene. The compiled BWD carries no quest list (forge::bwd has no such field), so the retail runtime effect comes from the .qst AddQuest(...,TRUE) flag.

**Fix:** 1) forgecore, libs/forgecore/include/forge/wld.hpp + src/wld.cpp: add `void setInitialQuests(std::vector<std::string>)`. It rewrites only the lines between the START_INITIAL_QUESTS; and END_INITIAL_QUESTS; rawLines_, one `<Name>;` per line with the file's line terminator, and keeps every other byte, the same way relocateMap does. Add a round-trip test to tests/test_export.cpp next to the existing START_INITIAL_QUESTS fixture at l.1388. This mirrors vanilla SaveInitialActiveQuests 0x0207ed60, which writes the block at the top of the .wld.

2) CLI, tools/forge-cli/main.cpp: add `forge qst set-active <file.qst> <Quest> on|off [--json]`. It parses, calls File::setQuestActive, fails if the quest is missing, and writes with a one-time .forge-orig backup. Put it next to `qst list`, add it to the help at l.108, and add an AUTOMATION.md verb.

3) GUI, gui/world.cpp drawWorldPanel (after the "Pending changes" card, ~l.562): add a collapsible "Quests at game start" card.
- It parses <install>/Data/Levels/FinalAlbion.qst once per install change (cache it in App).
- It lists AddQuest statements with a filter box and an Active checkbox each, and shows AddTestQuest entries greyed out.
- Toggling stages a change. Apply uses confirmRow, is refused while the game runs via gameWriteBlocked("quests") (editor.cpp:1474), writes the .qst with a .forge-orig backup, and mirrors the same active set into FinalAlbion.wld START_INITIAL_QUESTS via the new setter, for vanilla parity.
- Tooltip: "The game reads the .qst flag at world load; a Steam verify resets it; saves remember started quests, so start a new game."

4) Mod-pack interaction: mods.cpp re-merges FinalAlbion.qst from the base plus mods on apply (qst::merge, main.cpp:8518). So either store the toggles as a user "picks"/overlay entry in the active mod profile that the merge applies last, or warn in the card that re-applying mod packs rebuilds the file.

Effort S to M (about half a day). Value low to medium.

### Tracks dialog, copy & paste + brush libr...

#### No camera-point / spline / shape editing (vanilla modes 21-25)
*gap, effort L, value high*

**Vanilla:** CEditInputProcessEditCameraPoint::ProcessInput 0x0295af60 has these keys: O sets CoordBase at the cursor (SetCoordBase), P aims CoordAxis at the cursor (SetCoordAxis), S/E/L/M set start/end/look points from the camera or the cursor (CTCCameraPointScripted::EditSetPointForEditorPointAcquisition), and T makes the nearest thing the subject (EditSetThingForEditorSetThing). In AddTrackPoints (0x0295c0f0), J appends the current editor camera as a key camera (AddCameraPoint 0x0204eaf0) or a shape point (AddShapePoint 0x0204f030), and K finishes. PreviewSpline is 0x0295ce20, and the spline evaluates through CCardinalSpline::GetSplinePosition 0x0324b730 with the block's Tension. Polygon shapes (NShape CLOSED/LINE, CTCShapeManager, SetShapeMode 0x0291f2a0) come from EditShapes 0x0295b8d0 and SetPolygonalArea 0x0295a8a0. The dev data holds 2,086 CTCCameraPointScriptedSpline blocks (KeyCameras[i].Position/LookDirection/FOV/Duration/PauseTime/RollAngle, TimeToPlay, Tension, CoordBase) and 73 CTCShapeManager blocks (Shape[i].Type/pos[k]). The shapes are trigger areas of 54 CAMERA_POINT_FIXED_POINT things plus MARKER_SET_BAD_CREATURE_GENERATION_AREA.

**Fix:** Plan, in dependency order:

1) Document model (src/leveledit.hpp/.cpp)
- Add struct KeyCamera {float pos[3], look[3], fov, shuttle, duration, pause, animSpeed, roll; std::string event;}.
- Add Document::keyCameras(size_t thing), which parses the KeyCameras[i].* rows of the CTCCameraPointScriptedSpline block. Use the same indexed-key walk as listEntries, but group the rows by index.
- Add setKeyCameras(thing, vector<KeyCamera>). It rewrites every KeyCameras[i].* row in order, removes stale indices and keeps the array-size row in step, as the CreatureFamilies list edit at :1641-1655 does. Everything goes through one undo entry.
- Add Shape { int type /*NShape CLOSED=?, LINE=?*/; vector<vec3> pts; } with shapes(thing) / setShapes(thing, ...) for CTCShapeManager Shape[i].Type / Shape[i].pos[k]. Read the CLOSED/LINE enum values from CTCShapeManager's serialiser, reached from 0x025d6f40.
- Honour IsCoordBaseRelativeToParent: world = thing frame (frameOf) + CoordBase + Position. Honour IsCoordsRelativeToMap for shapes in the same way.
- Unit tests in tests/test_export.cpp: round-trip ArenaHallOfHeroes.tng blocks byte-for-byte when nothing is edited.

2) Spline evaluation (libs/forgecore, new camspline.hpp/.cpp)
- Add a vec3 wrapper that calls forge::fillerfit::cardinalSpline per axis with the block's Tension.
- Before relying on it, disassemble CCardinalSpline::GetSplinePosition 0x0324b730 and the CTCCameraPointScriptedSpline update/play function in FableWin with capstone. Check whether the parameter is spread evenly over the keys, as fillerfit's floor(segs*s) is, or weighted by the per-key Duration/PauseTime/ShuttleSpeed. Check how TimeToPlay scales it and what units FOV uses: the data has 0.2, while the grid range is 5..90, so it may be a multiplier or radians. Port that timing exactly.
- Note that fillerfit's version wraps with `% n`. Make sure the open camera path does not wrap.

3) Viewport drawing (gui/editor.cpp, next to drawTrackLines)
- When the selected thing has a CTCCameraPointScriptedSpline, draw each key as a small frustum: position plus LookDirection plus FOV. Draw the spline polyline sampled at about 32 steps per segment.
- When it has a CTCShapeManager, draw the Shape polygons draped on the terrain: CLOSED shapes as a closed loop, LINE shapes as an open polyline. Use the existing Fable-to-render axis swap (x, z, -y) seen at editor.cpp:729.

4) 'Camera path & areas' card in the Level tab (gui/editor.cpp), shown only for these components
- A key list with select, delete and reorder, plus fields for Duration, Pause, FOV, Roll and Event.
- 'Add key from view' ports vanilla J / AddCameraPoint 0x0204eaf0. It writes the camera_ eye and forward (converted back to Fable axes), minus CoordBase and the parent frame when relative.
- 'Set base here' ports O / SetCoordBase. 'Aim at cursor' ports P / SetCoordAxis. 'Set subject = nearest thing' ports T / EditSetThingForEditorSetThing. Take the exact field writes from CEditInputProcessEditCameraPoint::ProcessInput 0x0295af60.
- Register the automation widgets, following the auto_.registerWidget pattern.

5) Preview
- Generalise TrackPreview (app.hpp:883) with a spline mode that samples step 2 over TimeToPlay (or the ported duration timing), sets camera_ each frame, and sets Camera::fovY from the interpolated key FOV. Restore the saved camera and FOV on stop, as vanilla does (editor.cpp:718). This ports PreviewSpline 0x0295ce20.

6) Shape editing
- A shape tool mode: click the ground (existing terrain pick) to append pos[k] (AddShapePoint 0x0204f030), drag vertices with the existing gizmo pick, delete a vertex, pick CLOSED or LINE, and add or remove shapes. Behaviour comes from EditShapes 0x0295b8d0 / SetPolygonalArea 0x0295a8a0 and CTCShapeManager::EditUpdate*.

Effort: L overall. Steps 1+3 (view only) are M. Value: high, since shipped levels carry hundreds of these blocks that the editor cannot currently see or edit.

#### Helper things (markers, track nodes, camera points) cannot be seen or clicked, and the Scene Browser hides markers and track nodes
*weakness, effort M, value high*

**Vanilla:** Vanilla draws every thing that has no model through its edit-draw info (CThingTrackNode::DrawGetEditDrawInfo 0x027ff4f0, CTCCameraPointScriptedSpline::EditorDrawPreparePrimitivesForRendering 0x025ca120). Picking selects the NEAREST thing, not a mesh hit: ProcessPlaceTracksDialog 0x02950490 calls SetNearestThingAsSelected / PaintInputPickUpNearestThing. The Scene Browser (CSceneDialog 0x028efdc0, JumpTo 0x028f21c0) lists all script-named things. The dev tree holds 4,264 script-named markers, 317 track nodes and 2,498 CAMERA_POINT_* things, and quest scripts refer to them by ScriptName.

**Fix:** 1) gui/editor.cpp: generalise trackNodeAt (809) into `int App::helperAt(float px, float py, bool nodesOnly=false) const`. Keep a cache keyed on doc_.revision(), in the same pattern as cachedTracks at 800. The cache holds a vector of {index, kind} for every thing where summary(i).hasFrame is true and the type has no render instance. That covers type=="Marker", doc_.isTrackNode(i), and defs starting with CAMERA_POINT_ or with no Graphic. Build the no-render set by collecting renderer_.instance(k).thing for every instance and taking the complement, so it matches thingsexport's noGraphic skips exactly. helperAt projects each position the way trackNodeAt does ({pos0, pos2+0.3, -pos1}). It skips entries where thingHiddenBySection is true and returns the nearest one within S(12)px. The existing link-pick call at 867 becomes helperAt(..., true).
2) Add `void App::drawHelpers(origin,size)` and call it next to drawTrackLines/drawLinkLines when editMode_ is on and a new toggle showHelpers_ is set (default on). It draws a diamond for a marker and a small camera/triangle glyph for a CAMERA_POINT or camera thing. Track nodes are already drawn by drawTrackLines, so skip them. If a helper has a ScriptName, draw it in fontSmall_ as a label, but only within about 60 m of the camera or when hovered, so the 4,264 dev-tree markers don't flood the view. Highlight the selected one and the ones in the multi-selection.
3) pickAt (editor.cpp:888): before `if (inst < 0)`, call `const int h = helperAt(screen px,py)`. Choose h when inst<0. Also choose h when the mesh hit's thing is farther from the camera than the helper, which is a cheap way to approximate vanilla's nearest-thing pick. Then follow the same Ctrl toggleSelect/selectThing path. The gizmo and nudge already work on any frame through frameOfSelected/commitFrame.
4) Scene list (editor.cpp:3023): remove `|| s.type=="Marker" || s.type=="TrackNode"`. Add a small-font `Helpers` checkbox (thingsShowHelpers_, default off) next to 'Script-named only' and 'Nearest first'. When the checkbox is off, still show markers and track nodes that have a ScriptName, because those are the ones vanilla's 'Only ScriptNamed Objects' view lists. Register the widget as check_things_helpers for AUTOMATION.
5) Tracks card (editor.cpp:614-619): make each track row a TreeNode that lists its nodes (index, then 'head'/'tail'). Clicking a node calls selectThing(node) and then frameSelected().
6) Optionally add vanilla's Area Range, a max distance from the camera, as a slider next to Nearest first. Filter rows by the distance that is already computed at 3040-3043.
Effort M (about a day). Value high.

#### Track authoring is slow: nodes are placed at the view centre and each link needs a button press
*weakness, effort M, value high*

**Vanilla:** Tracks placement mode (ProcessPlaceTracksDialog 0x02950490) works like this: Shift+LMB places a TRACK_NODE_BASIC under the cursor (PaintInputPlaceTrackNodeAt 0x02995d90, at GetCurrentPointedAtPos, in the current quest section). LMB grabs the nearest node and dragging moves it (PaintInputPickUpNearestThing, then PaintInputDragCarriedThingTo, dropped on release). Delete deletes the node, and ',' / '.' adjust its height. In linking mode (ProcessLinkTracksDialog 0x02950370) you press on a node and drag to another: StartLinkingTracks 0x0202f950, ContinueLinkingTracks 0x0202f690, FinishLinkingTracks 0x020302d0. F (key 0x21) flips the track and Delete removes the node's links. In selection mode (0x029502f0), a click selects the nearest track (SetNearestTrack 0x0202f880).

**Fix:** 1. Add a 'Draw track' toggle (drawTrack_ bool, plus drawTail_ int = -1) to drawTracksCard next to Place node (gui/editor.cpp:623), and register it as btn_track_draw. While the toggle is on, pickAt (editor.cpp:859) takes a new branch before the trackLinkPick_ branch. First it calls trackNodeAt(screen px, py). If the click is on an existing node and drawTail_ < 0, that node becomes the tail, so a chain can be extended from its end. If the click is on an existing node and drawTail_ >= 0, it calls doc_.linkTrackNodes(drawTail_, node, err), which closes onto that node, and then ends the chain. Otherwise, if cursorHit_, it calls n = doc_.placeTrackNode(cursorFable_[0], cursorFable_[1], cursorFable_[2]), the vanilla PaintInputPlaceTrackNodeAt-at-GetCurrentPointedAtPos behaviour. If drawTail_ >= 0 it also calls linkTrackNodes(drawTail_, n, err) and logs err on failure. Then it sets drawTail_ = n and selectThing(n). Wrap each click in doc_.beginBatch()/endBatch() so one undo removes one place+link. Esc (next to the existing handler at editor.cpp:2376) ends the chain and turns the mode off. RMB is not used for this because it is fly-look. The link-rule checks (no branches or loops) stay inside linkTrackNodes. Show a ghost segment from drawTail_ to the cursor in drawTrackLines (editor.cpp:742). 2. Click-to-select nodes. In pickAt's normal path (before the mesh test at editor.cpp:888), check trackNodeAt first and select the dot if one is hit. This is vanilla's Selection mode / SetNearestTrack 0x0202f880, and it also makes mesh-less nodes selectable. 3. Change 'Place node' to use the cursor ground (cursorFable_ when cursorHit_) when the toggle is used from the viewport, and keep the view centre as the fallback. Do NOT rebind F (frame-selected) or LMB-drag (camera). Flip, Unlink and Delete already exist as buttons and the Delete key, so optionally add only Shift+F for Flip while a track node is selected. Add an automation verb or reuse 'pick' so the tests can drive it (docs/AUTOMATION.md). Effort S-M, value high.

#### Deleting, duplicating or pasting track nodes breaks the chains (the engine asserts)
*weakness, effort S, value high*

**Status 2026-09-30:** Resolved for the supported edit paths. Delete repairs
the chain; edit-brush copy and paste reject native non-copyable types including
CTCCreatedEntity (PDB TCI_CREATED_ENTITY = 50), and direct TrackNode
duplication makes an unlinked node. Village verbatim duplication is refused.
Core copy, clone, paste, terrain-brush and undo checks pass.

**Vanilla:** DeleteSelectedTrackNode 0x020305a0 calls DeleteLinksFromSelectedTrackNode before PaintInputDeleteSelectedThing, so the neighbours are re-linked or ended. Copy and paste refuses these things: CThingFilter_IsEditBrushCopyable::operator() 0x02957910 rejects thing types 4, 8, 9, 10 and 11 (villages, switches, markers, track nodes; inventory 5.8) and anything with TC 0x32. Vanilla also says 'Track nodes cannot be pasted'.

**Fix:** 1. Deleting a track node, as vanilla 0x020305a0 does. In Document::remove (src/leveledit.cpp:2172), add `if (isTrackNode(index)) unlinkTrackNode(index);` before file_.removeThing. The existing unlinkTrackNode re-links the pieces before and after the node, fixes their Start/End flags, and names them INVALID or TrackTempName<n>, which matches DeleteLinksFromSelectedTrackNode 0x020305e0. Removing the node does not shift any indices before the unlink runs. Multi-delete is safe: deleteSelected already removes things highest index first inside a beginBatch, so each unlink sees the current chain and pushUndo folds into the batch. Check that pushUndo inside unlinkTrackNode really is a no-op inside a batch, as it already is for remove.

2. Copy/paste filter, as vanilla 0x02957910 does. Add a helper, `bool Document::isEditBrushCopyable(size_t i) const`. It returns false for thing types Village, Switch, Marker and TrackNode; to be exact, map vanilla type ids 4, 8, 9, 10 and 11 to the TNG `NewThing <Type>` names, using the engine's thing-type enum. It also returns false for things with the CTC that TC id 0x32 maps to; resolve that id in the TC enum first. Apply the helper in three places:
   - Document::extract (leveledit.cpp:~340): skip non-copyable things and count them. Show "N villages/switches/markers/track nodes were not copied" through pushLog in App::copySelection (editor.cpp:~255).
   - copyTerrain withThings (leveledit.cpp:~787): skip non-copyable things.
   - Document::duplicate (leveledit.cpp:1842), which serves Ctrl+D: refuse a track node or village with the same message. Optionally, a duplicated track node could instead be placed as a fresh unlinked node: after the UID rewrite, set LinkedToUID1/2 to 0, Start and End to TRUE, and ScriptName to NULL through setTrackField.

   Also add the same strip as a guard in Document::paste. Blocks that reach paste from old clipboards or preset files bypass extract, so the check has to run on the staged thing's type. Presets saved before this fix may already contain these things.

3. Optional extension beyond vanilla, only if wanted later: in paste, build an old-to-new UID map over the fragment and rewrite LinkedToUID1/2 whose targets are inside the fragment, zeroing the rest. Then run trackChain, fixTrackEnds and nameChain(TrackTempName<n>) on each pasted chain so whole tracks copy correctly. Refusing them, as vanilla does, is the faithful default and costs less.

Tests: extend the Document unit tests. Delete the middle node of a 3-node chain and assert two INVALID singletons with no dangling UIDs. Delete the end node and assert the new end has End TRUE. Assert that Ctrl+C and Ctrl+D of a track node, a village or a switch produce nothing, and that region copy skips them.

Effort S, value high: saved maps currently get dangling or asymmetric track links, which the engine asserts on (inventory section 10).

#### No top-down 2D relief view
*gap, effort M, value med*

**Vanilla:** EEditViewMode RELIEF=2 (EditSetViewMode 0x020786f0; View > 2D Relief) is a top-down height-map view. Its input is CEditInputProcessView2D 0x0294bc10: arrows pan, PgUp/PgDn and the wheel zoom, RMB-drag pans, and Shift multiplies the speed by 10 (SetMoveSpeed 0x0294bb40). Painting heights and themes, copy rectangles and track placement all work in it.

**Fix:** 1. Camera (gui/renderer.hpp:20).
   - Add `bool ortho=false; float orthoHalfH=100;`.
   - Add `void setTop(float cx,float cz,float halfH)`. It sets yaw=0 and pitch=Ï€/2 exactly, putting the eye high above the map: posY = maxHeight + 1000, focus distance = eye height minus the ground.
   - Leave the Â±1.55 clamp in look/orbit alone; setTop writes pitch directly.

2. Renderer::render (renderer.cpp:~985-993).
   - When camera.ortho is set, build the view with a fixed up vector of (0,0,-1) render-space, so Fable +Y points up the screen and matches the minimap's north-up. Do not use lookAtRH's Y-up, which degenerates at pitch Ï€/2.
   - Build an orthoRH(w=halfH*aspect, h=halfH, zn=0.1, zf=eyeHeight+maxHeight+2000) matrix. Store it in lastProj_ so project() keeps working unchanged, since w=1 in ortho.

3. Renderer::screenRay (renderer.cpp:738).
   - Add an ortho branch: dir = f, origin = eye + r*sx*halfH*aspect + up*sy*halfH, with sx=(2u-1) and sy=(1-2v).
   - This is the single place that feeds the brush, the copy rectangle, track clicks and pickAt, so those tools need no other change.

4. Input, in App::handleViewportInput (gui/app.cpp:1737). When camera_.ortho is set, port the CEditInputProcessView2D 0x0294bc10 grammar before the perspective branch:
   - RMB-drag or MMB-drag pans the eye by dx,dy Ã— (2*halfH/size.y) world units.
   - The wheel zooms orthoHalfH by 1.25^-wheel about the cursor: keep the world point under the cursor fixed by shifting the eye by the delta of the ortho ray origin.
   - Held arrow keys pan at moveSpeed*halfH*dt, and PgUp/PgDn zoom.
   - Shift multiplies by 10, as in SetMoveSpeed 0x0294bb40. Ctrl can give the "fine" 0.01/0.4 factor.
   - RMB does not look or fly in this view, since rotation is disabled. F calls frameMap / frameSelected, which set the ortho centre and halfH (â‰ˆ span*0.55) instead of calling lookAt.
   - Leaving the view restores the previous perspective camera, saved in `Camera perspSaved_`.

5. UI. Add a "Top" / "3D" chip next to the ViewMode chips in the viewport toolbar (gui/app.cpp drawViewport). Add the hotkey T, or Numpad 7 to match Unreal. Keep the shading modes orthogonal: Height shading plus Top view reproduces vanilla's relief look. Show a cursor readout of X/Y and height from the brush hit.

6. Gizmo. In gui/editor.cpp:2397, pass ImGuizmo::SetOrthographic(camera_.ortho). ImGuizmo also needs the ortho proj from renderer.projMatrix(), which it already reads.

7. Automation. Add an automation command `view top|3d` in gui/automation.cpp next to camera_above_selected, and document it in docs/AUTOMATION.md.

Effort M, about 150-250 lines across renderer.hpp/.cpp, app.cpp and editor.cpp. Value medium.

#### New tracks all end up named INVALID after linking (vanilla gives each a unique TrackTempName<n>)
*weakness, effort S, value med*

**Status 2026-09-30:** Resolved for link and unlink. Loaded temp names advance
the counter; new names are checked against all track nodes. Core tests cover
fresh, named and preexisting tracks; track_preview.txt passes.

**Vanilla:** FinishLinkingTracks 0x020302d0 reads the name of the chain's start node (PeekTrackStartNode, then PeekTrackName). If that name is empty, is "INVALID" or starts with "TrackTempName" (13-char compare), it sets "TrackTempName" + counter (counter at this+0xb08, incremented) with SetTrackName. Every new track therefore gets a unique name, and tracks are looked up by name (inventory 10).

**Fix:** 1. In src/leveledit.cpp, Document::linkTrackNodes (around line 1560): after `joined` is built, read `name = file_.things()[joined.front()].scriptName()`. This is the start node, the same node vanilla gets from PeekTrackStartNode. Add a helper `static bool isPlaceholderTrackName(const std::string& n)` that returns true when n is empty, n == "INVALID", n == "NULL" (the summary spells an empty name as NULL), or n.compare(0, 13, "TrackTempName") == 0. When the helper returns true, set `name = nextTrackTempName()`. Replace line 1569 with `nameChain(joined, name);`. This ports FinishLinkingTracks 0x020302d0 (the checks run from 0x203034f to 0x20303bc, and the name is built at 0x20303fb). Keep vanilla's rule of not borrowing the name of b's part. If that behaviour is wanted anyway, add it as an explicitly non-vanilla option.

2. Add a private helper `std::string Document::nextTrackTempName()`. It returns "TrackTempName" + std::to_string(trackTempCounter_++), so the name is built before the counter increments, matching vanilla 0x2030436. It loops while the name is already used by any TrackNode ScriptName in file_. unlinkTrackNode (line 1620) should call the same helper, so that both paths share one counter and one collision rule.

3. At level load (leveledit.cpp:258), stop resetting the counter to 0. Scan every TrackNode ScriptName that matches TrackTempName<digits> and set trackTempCounter_ to the highest number found plus 1. Retail maps already contain names such as TrackTempName30.

4. Tests, in tests/test_export.cpp testTracks:
   - Place two fresh nodes and link them. Check that the track name is TrackTempName<n> and not INVALID.
   - Build a second fresh pair. Check that it gets a different name.
   - Link a lone INVALID node as the head in front of a named track. Check that the result is a new TrackTempName, as vanilla does. The existing GuardTrack case at line 1074 must still keep the name GuardTrack.

5. Optional: add a warning to validate() and the check card when two separate track chains share a ScriptName, not counting NULL.

Effort: S. Value: med.

#### Relative paste does not work like vanilla's 'Relative to' altitude (top/bottom) and has no preview of height or sampling
*weakness, effort S, value med*

**Vanilla:** Paste 0x02953c10 computes, when 'Place height relatively' is on: h = cell + RelativeTo - (RelativeToBottom ? brushMin(+0x40) : brushMax(+0x3c)), clamped to [0, 2048). RelativeTo is a number box. Ctrl+LMB fills it with GetGroundSizeZAt under the cursor (ProcessInput 0x02952950 calls SetPasteRelativeHeight), and '-' / '=' step it by 1.0. '[' / ']' call RotateBrush -0.25 / +0.25 turn. The 3D preview (Draw3DCopyPasteBrush 0x0204a620) draws the brush's height span (GetMaxHeightDifference 0x029556a0 = max - min) at RelativeTo, so you can see where it will land.

**Fix:** 1. **Document (src/leveledit.hpp/.cpp).**
   - Add `enum class PasteHeight { AsCopied, Corner, Top, Bottom }`.
   - Change the `bool relative` parameter of `pasteTerrain` (leveledit.cpp:803) and `pasteTerrainCells` (leveledit.cpp:848) to `(PasteHeight mode, float relativeTo)`.
   - In `pasteTerrainCells`, compute `mn`/`mx` over `clip.heights` once. Set `base` from the mode:
     - AsCopied: 0
     - Corner: the existing lines 865-870 (keep it as a FableForge extra)
     - Top: `relativeTo - mx`
     - Bottom: `relativeTo - mn`
   - Keep the existing clamp at line 884.
   - Add `float TerrainClip::heightSpan()`, returning max minus min (vanilla GetMaxHeightDifference 0x029556a0).
   - Update the caller at editor.cpp:4161 and any automation verb that passes `relative`. Check docs/AUTOMATION.md, and keep the old bool as an alias for Corner.

2. **UI (gui/editor.cpp:2578).**
   - Replace the Relative checkbox with a combo: 'Heights: As copied / Corner on ground / Top at / Bottom at'.
   - When Top or Bottom is chosen, show an `ImGui::DragFloat("Altitude", &clipRelTo_, 1, 0, 2047.99)`.
   - Add `clipPasteMode_` and `clipRelTo_` members and register them with `auto_`.
   - Update the hint text at editor.cpp:2587.

3. **Input (editor.cpp around 1672-1680, mode 15).**
   - Ctrl+LMB with `brushHit_`: set `clipRelTo_` to the ground height sampled under the cursor, as vanilla SetPasteRelativeHeight does. If the mode was AsCopied or Corner, switch it to Top. Do not paste on that click; the current `press` check needs `!io.KeyCtrl`.
   - '-' / '=' pressed: `clipRelTo_ -= 1` / `+= 1`. Vanilla steps by the constant at 0x401df70 (1.0 per the claim).
   - '[' / ']': `clipTurns_ = (clipTurns_ + 3) % 4` / `(clipTurns_ + 1) % 4`. Keep R as the shortcut.
   - Stop the global '-' / '=' HeightKey block (editor.cpp:1614-1628) from running in mode 15: add `terrainMode_ != 15` to `keysFree`. Without this, the keys also raise or lower the ground.

4. **Preview (drawBrushCursor, editor.cpp:1828-1831).**
   - In Top or Bottom mode, keep the ground rectangle.
   - Also project and draw the rectangle's four corners at world Z equal to `clipRelTo_` and at `clipRelTo_ âˆ“ heightSpan()`. Use span below the altitude for Top and above it for Bottom.
   - Connect the two levels with vertical edges to form a wire box, as vanilla Draw3DCopyPasteBrush 0x0204a620 does. Use the same projection helper as drawGroundRect with an explicit Z.

5. **Test.** Add a forgecore/leveledit test where a clip with heights {10, 30} is pasted:
   - Top at 100 gives {80, 100}.
   - Bottom at 100 gives {100, 120}.
   - Values outside the range are clamped to [0, 2047.9999].

#### No way to delete or select all things inside a region
*gap, effort S, value med*

**Vanilla:** In Copy and paste mode, Delete (key 0x75) with a selected region calls CEditControlCentre::PaintInputDeleteAllThingsInArea with the region box (ProcessInput 0x02952950, around 0x02952ec4). You drag a rectangle and clear everything in it at once.

**Fix:** 1. Keep the region. In app.hpp, next to clipStart_ (line 520), add `bool clipRegionValid_ = false; float clipEnd_[2];`. This mirrors vanilla's region box at this+0x2c and its valid flag at +0x74. In editor.cpp:1666-1670, when the mouse is released with brushHit_, store brushFable_ into clipEnd_ and set clipRegionValid_ = true. Clear it on document load or unload. In the overlay near editor.cpp:1827, keep drawing drawGroundRect(clipStart_, clipEnd_) while clipRegionValid_ is set and terrainMode_ is 14 or 15, not only while dragging.

2. Add a Document query. In leveledit.hpp/.cpp, add `std::vector<size_t> Document::thingsInArea(int x0, int y0, int x1, int y1) const`. Reuse the same frameOf plus pos[0]/pos[1] box test that copyTerrain uses (leveledit.cpp:788-791), so "what copy takes" and "what delete removes" always agree. Normalise min and max first. Copy-region drags can go in any direction, and the existing test assumes x0<=x1.

3. Delete in the region. In the Copy region mode's key handling (terrainMode_ 14, before the global Del at editor.cpp:2367), when clipRegionValid_ is set and nothing is selected, Del does the following:
   - calls thingsInArea;
   - drops things hidden by the Things/section view filters (vanilla filters with CThingFilter_IsSelectableBasedOnEditorMode);
   - sorts descending;
   - removes them between doc_.beginBatch() and endBatch() (one undo step), with the same removal path as deleteSelected. Switch to a track-safe remove if one lands.
   Log "removed N objects in region". Add a matching danger button "Delete objects in region" on the Copy region card, next to the brush library. Register it with auto_ (for example btn_region_delete) for AUTOMATION.md.

4. Select in the region. Add a "Select objects in region" button. It calls selectThing(first), then pushes the uids of the rest into extraUids_ and calls syncExtraSelection(). The existing gizmo, Delete, Duplicate and Ctrl+C then work on the group.

Effort S. Value med.

#### No whole-map height operations (vanilla console EditRaiseZ / EditResizeZPercent / EditSetZ)
*gap, effort S, value med*

**Vanilla:** The console commands ConsoleEditRaiseZ 0x0204cc40, ConsoleEditResizeZPercent 0x0204c980 and ConsoleEditSetZ 0x0204cae0 call CEditWorldMap ops over every editable map (0x02973c50...). CEditMap::EditRaiseZ 0x029a8d10 does h += dz and EditResizeZPercent 0x029a8ac0 does h *= p / 100, each clamped to [0, 2048 - 1e-4] through SetGroundSizeZAt. There is also a Backup first.

**Fix:** 1) src/leveledit.hpp/.cpp: add `size_t Document::transformHeights(float scale, float offset, std::optional<float> setTo, bool moveThings)`. For each vertex, compute v = setTo ? *setTo : h*scale + offset and clamp it with std::clamp(v, 0.f, 2047.9999f), the same constant applyFractal uses. Collect the changed vertices and apply them through setVertexHeights so the change is one undo step. This follows the pattern of applyFractal (leveledit.cpp:~715). If moveThings is set, i.e. only for Raise (matching CEditWorldMap::EditRaiseZ 0x02973e20 â†’ CEditRaiseZOnThing 0x02974000), add offset to every thing's z inside the same undo step, either by folding it into a combined edit or by pushing one undo before both changes. Do not move things for Scale or Set, matching 0x02973c50 and 0x02973d40; the existing "Re-seat objects on the new ground" button at gui/editor.cpp:3176 stays available for those. 2) gui/editor.cpp Terrain tab: add a collapsible "Whole map" group next to the Fractal and Fit-to-neighbours panels (around editor.cpp:3700), with three rows: Raise by [m] (negative lowers), Scale [%] and Set to [m]. Each row gets an Apply button and a confirm popup that shows how many vertices change and says whether objects follow. Register widgets btn_whole_raise, btn_whole_scale and btn_whole_set. 3) Automation (the runner behind docs/AUTOMATION.md): add `raise_z <dz>`, `resize_z_percent <p>` (scale = p/100) and `set_z <h>` and document them there. Add a UI test using snapshot_heights / assert_height. Effort S, value med (an older map re-leveled in one step, e.g. lifting a map to meet a neighbour's seam).

#### Orbit and zoom do not pivot on the point under the cursor; no 'fly to pointed spot'
*weakness, effort S, value med*

**Vanilla:** In the 3D view input (CEditInputProcessView3D 0x0294c310), dragging the mouse rotates about the world position grabbed under the cursor (RotateViewAroundGrabbedWorldPos 0x0294d920; the grab uses EditConvertVScreenPosToWorldPos). Alt+H (key 0x23 with LALT/RALT) calls ZoomToLastWorldCoordPointedAt 0x02044380, which moves the camera to the pointed ground (GetGroundSizeZAt) and keeps it inside the map boxes.

**Fix:** 1) Keep the render-space hit. In gui/app.hpp, next to cursorHit_/cursorFable_, add `float cursorWorld_[3]`. In drawViewportOverlays (app.cpp:1120), set cursorWorld_ = hit (renderer x, y-up, z) as well as cursorFable_. 2) Pivot the orbit on the cursor. In handleViewportInput, add `if (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && cursorHit_ && !gizmo) { float e[3]={camera_.posX,camera_.posY,camera_.posZ}; float dist = |e - cursorWorld_|; camera_.lookAt(cursorWorld_[0],cursorWorld_[1],cursorWorld_[2], camera_.yaw, camera_.pitch, dist); }` before the drag branch at app.cpp:1763. Better, add a Camera::setPivot(x,y,z) in gui/renderer.cpp that recomputes yaw and pitch from eye-to-hit, so the view does not jump: yaw = atan2 of the XZ delta and pitch = asin(-dy/dist) under the same sign convention as Camera::dir. Then set distance = dist without moving pos. The existing orbit() then rotates about the grabbed ground point, which is vanilla RotateViewAroundGrabbedWorldPos 0x0294d920. If there is no hit, keep today's focus. 3) Add 'Focus here', ported from ZoomToLastWorldCoordPointedAt 0x02044380. Bind it to Alt+H, plus a MMB double-click when cursorHit_ is true: `camera_.lookAt(cursorWorld_[0], cursorWorld_[1] + 0.8f, cursorWorld_[2], camera_.yaw, camera_.pitch, std::clamp(camera_.distance, 8.f, 200.f))`, with the target clamped to [0,mapWidth] x [-mapHeight,0], because vanilla keeps the camera inside the map boxes. The 0.8f is vanilla's ground-height offset at 0x415e2b4. Guard it with !ImGui::IsAnyItemActive(), the same way F is guarded at app.cpp:1779. 4) Optionally make the wheel dolly toward the cursor. When cursorHit_, move pos along the normalized (cursorWorld_ - eye) by the same amount Camera::dolly moves now (distance*(1-0.85^steps)), and re-seat the pivot. 5) Update the controls comment block (app.cpp:1732-1738), the viewport controls hint and help overlay, and docs/AUTOMATION.md if the camera verbs are listed there. Effort S, value med.

#### Show/hide per thing kind is missing (vanilla View > Show Creatures/Buildings/Objects/Holy Sites/Villages)
*gap, effort S, value low*

**Vanilla:** The &View menu items call SetThingDrawing(type, on) 0x02036ee0 for creatures, buildings, objects, holy sites and villages separately (not saved). They are also stored in the editor settings (ShowingCreatures ... ShowingVillages, SaveSettings 0x0203d9c0).

**Fix:** 1) State: add `std::set<std::string> hiddenKinds_` to App (app.hpp, next to hiddenSections_ at app.hpp:403), holding upper-case thing types: CREATURE, BUILDING, OBJECT, HOLY_SITE, VILLAGE. Vanilla groups types this way; switches, markers and other kinds stay under OBJECT unless SetThingDrawing at 0x02036ee0 shows another mapping, so disassemble that function first to confirm which type ids each menu item covers. 2) Rule: widen thingHiddenBySection (editor.cpp:1415), or add thingHidden(per,t) that wraps it, so it also returns true when `hiddenKinds_.count(doc_.summary(t).type)`. Point every caller at it: applySectionVisibility (editor.cpp:795), trackNodeAt (editor.cpp:815) and the selected_visible probe (app.cpp:1339). Set sectionsDirty_=true on every toggle so applySectionVisibility runs again. Hidden kinds then stop being drawn and picked through the existing instance.visible path, with no renderer change. 3) UI: keep the 'Objects' chip as the master toggle and give it a small right-click popover (or a caret next to it) at app.cpp:1933, with checkboxes Creatures / Buildings / Objects / Holy sites / Villages. Call auto_.registerWidget for each checkbox, and when any kind is hidden, show a partial state on the chip, such as 'Objects (3/5)'. 4) Persistence: write the set into the settings key/value dump (the pattern at app.cpp:1264, e.g. `hidden_kinds=CREATURE,VILLAGE`) and read it back on load, as vanilla's ShowingX settings keys do. Never write it into the level. 5) Automation: add a `show_kind <TYPE> 0|1` command in automation.cpp beside the section-visibility command at automation.cpp:292. Effort S, value low.

#### The camera view is not remembered between sessions; no camera bookmarks and no FOV control
*gap, effort S, value low*

**Vanilla:** SaveSettings 0x0203d9c0 / LoadSettings 0x0203cdc0 persist CamPos, CamViewDir, EditMode, ViewMode and the grid/snap/show flags (StartEditorSettings ... EndEditorSettings). The console has EditSetCameraPos, EditSetCameraToFace, EditSetCameraFOV and EditLoadCamera. The CameraPoint tab can take a camera from the clipboard (c:\cameras.bin), so a framed view can be reused as a camera point.

**Fix:** 1. Remember the view per map, as vanilla's CamPos/CamViewDir do.
   - In gui/app.hpp, add `std::map<std::string, Camera> savedCams_;` (camera state per map, keyed by the r.name / selectedName_ map key).
   - When the selected map changes, and in saveSettings, store the outgoing map's camera_: `savedCams_[lastFramedFor_] = camera_`. Save posX, posY, posZ, yaw, pitch, distance, fovY and flySpeed.
   - In app.cpp:992, when `lastFramedFor_ != r.name` and savedCams_ has an entry for r.name, assign camera_ from that entry and call `renderer_.upload(r.scene, camera_, false)`. Otherwise keep the current auto-frame.
   - Persist the map in saveSettings (app.cpp:518) as a JSON object, `"cameras": { mapKey: [px,py,pz,yaw,pitch,dist,fov,speed] }`, and read it back in loadSettings (app.cpp:486).
   - Like the rest of the settings, this is skipped under auto_.active(), so automation runs stay deterministic.
   - Also save and restore renderer ViewMode per map, to match vanilla's ViewMode key.
2. Add a FOV control. Put a slider on the viewport HUD camera-speed popover, or in the View menu, driving camera_.fovY over about 30-100 degrees (0.52-1.75 rad). Save it with the camera. This is the equivalent of vanilla's EditSetCameraFOV.
3. Add saved views, the equivalent of EditSetCameraPos / EditLoadCamera.
   - Add a "Views" dropdown on the viewport HUD with "Save current view..." (a name field) and a list per map with restore and delete.
   - Store the list in settings as `"views": { mapKey: [{name, cam[8]}] }`.
   - Bind Ctrl+Shift+1..9 to save a view and Ctrl+1..9 to recall one, checking the existing keymap for conflicts first.
4. Add `camera save <name>` and `camera load <name>` verbs to the automation `camera` command (docs/AUTOMATION.md), so views can be scripted like vanilla's console commands.
5. Skip the cameras.bin to camera-point copy until a camera-point edit card exists. At that point, add a "Set from current view" button that writes the eye position and look direction into the thing's camera fields.

Effort: S (1-2) + S (3-4). Value: low to medium.

## Assets and animation

### Animation viewing and editing: CAnimatio...

#### 3D animation preview player (play/pause/stop, frame slider, speed, interpolate, reset position/orientation, track movement)
*gap, effort L, value high*

**Vanilla:** The 'Preview Animation' section has EDITORGUI_PLAY/PAUSE/STOP icons (0x2929b04, 0x2929b4a, 0x2929bbd), 'Frame' STANDARD_FRAME_SLIDER_BAR and number control (0x2929c34, 0x292ae44, 0x292ab45) and 'Speed' (0x2929dc2). It also has four toggles: 'Interpolate When Stepping' INTERPOLATE_BUTTON (0x2929f1e, 0x293499d), 'Reset Position After Loop' RESET_POSITION_BUTTON (0x2929f8a, 0x2934cad; IsResetPositionFlagSet 0x02934b50), 'Reset Orientation After Loop' (0x2929ff6, 0x2934fbd) and 'Track Movement With Camera' TRACK_MOVEMENT_BUTTON (0x292a062, 0x29352cd). Frame asserts: 'total_frames>1' 0x2935d75 and 'FramesInCurrentAnim==0 || frame_number<=FramesInCurrentAnim' 0x29362a0.

**Fix:** Six steps, all in FableForge. Effort L, value high.

1. **Animation decoder (forgecore).** Add libs/forgecore/{include/forge,src}/animbank.{hpp,cpp}.
   - Read graphics.big MBANK entries of types 6, 7 and 9: a u32 decompSize, then one raw LZO1X stream. Reuse the fableLzo helper already in meshpreview.cpp.
   - Parse the chunks ANRT, AOBJ, XSEQ, HLPR, MVEC, TMEV, AMSK and XALO, plus the 24-byte TOC Info record.
   - Port EgoCore AnimParser.h AnimTrack, with EvaluateFrame for stepped playback and the smooth lerp/nlerp run evaluator.
   - Validate against FableTLC tools/parse_anim_xseq.py over all 3,435 entries, using the ANIM.md Â§9.3 counts.
   - Add a CLI verb, `forge anim list|info|dump <entry>`.

2. **Skinned-mesh loader (forgecore).** Extend meshpreview.cpp:172-173 so it keeps the bone data it currently skips: the bone name CRCs, parent indices, the 48-byte local transforms and the 64-byte inverse-bind matrices.
   - Also keep the per-vertex blend data (bone indices and weights) for animated blocks. Take the layout from EgoCore Meshes/MeshParser.h (C3DVertexBlend2, BoneIndex fields), not from byte-RE.
   - Expose it as a SkinnedMesh struct next to MeshPreview.

3. **Pose evaluation.** Map tracks to bones by name CRC, using the AOBJ rig name and the AMSK mask.
   - Compose local â†’ model â†’ skin matrices.
   - Skin on the CPU first. Move to a bone-palette vertex shader in gui/renderer only if CPU skinning is too slow.

4. **Assets > Animations page (gui).**
   - List the anims, using display names from Anims.txt when present and falling back to the ANIM_* bank names.
   - Filter by rig.
   - Show a live offscreen viewport in the existing D3D11 Renderer: extend the thumbnail() render-target path (renderer.hpp:182) to render every frame, with the orbit camera.
   - Choose the mesh automatically from the creature whose rig matches AOBJ, or let the user pick any type-5 mesh whose bone names cover the tracks.

5. **Player controls.** Play, pause and stop; a frame slider plus a number box, with total_frames>1 and frame<=FramesInCurrentAnim clamps matching the vanilla asserts; speed, which scales the clock.
   - Interpolate when stepping: the stepped or the smooth evaluator.
   - Reset position after loop: MVEC or Movement_dummy root motion accumulates across loops unless this is on.
   - Reset orientation after loop: the same rule for the helper and Info rotation.
   - Track movement with camera: the camera target follows the root.

6. **Timeline markers.** Show TMEV events, plus entries from Data\Misc\game_animation_events.txt and sound_animation_events.txt (plain text; the .bin layout is only partly known, so read the .txt only). Share this timeline widget with the later events editor.

This scope is read-only. Writing is left to the events and editing gaps, which can reuse FableTLC tools/anim_build.py as the reference for a writer port.

#### Load and preview an external animation file (.bba / glTF), then install it as a custom ANIM
*gap, effort L, value high*

**Vanilla:** 'Load From .BBA' FILE_ANIMS_BUTTON and FIND_BBA_BUTTON (0x29295c9, 0x292c23d, 0x292b4a3), a file dialog 'Animation Files (*.bba)' / 'Load Animation File' (0x292dc72, 0x292dcce), IsTCEditorAnimationUsingAnimFromFile 0x02932dd0, and GetFileAnimKeyFrameCount (assert 0x2936192). Vanilla only previews the file; it cannot install it.

**Fix:** 1. **Reader (libs/forgecore)**
   - Add forge/anim.hpp and anim.cpp.
   - Port the EgoCore AnimParser.h 3DAF chunk walker, covering ANRT, AOBJ, XSEQ, HLPR, MVEC, TMEV, AMSK and XALO.
   - Input is a graphics.big MBANK type-6, 7 or 9 entry: a u32 decompSize followed by LZO1X. Decompress it with the existing forge/lzo.hpp.
   - Parse the 24-byte Info record.
   - Test against docs/formats/ANIM.md: all 3,435 of 3,435 entries should parse. (S-M)

2. **.bba loader**
   - First confirm the format. Either inspect one sample .bba, or disassemble FableWin's load path: the "Load Animation File" dialog handler, then the call that follows it (IsUsingAnimFromFile / GetFileAnimKeyFrameCount on CTCEditorAnimation).
   - If .bba turns out to be the uncompressed 3DAF chunk image, reuse the reader from step 1.
   - Until it is confirmed, accept only raw '>>>>' 3DAF images. (S once the format is known)

3. **glTF input**
   - Port EgoCore GltfAnimImporter.h.
   - Retarget by bone name onto the rig of the chosen type-5 skinned mesh. The skeleton comes from the existing mesh reader.
   - Report any unmatched bones. (M)

4. **Preview**
   - Add an Assets > Animations page to gui/textures.cpp, alongside Textures, Models and Ground themes.
   - The page lists ANIM_* entries from graphics.big and has an "Open animation file..." button.
   - Play the animation on the chosen skinned mesh in the existing meshpreview renderer.
   - This needs CPU skinning from the XSEQ bone tracks, a frame slider, speed control and loop. (M-L; skinning in the preview is the main new work)

5. **Install**
   - Port tools/anim_build.py into forgecore as an anim writer, using EgoCore AnimCompiler.h for the chunk nesting and XALO.
   - LZO-compress the result and append a type-6 ANIM_<name> entry plus the 24-byte Info record (from C3DAnimationInfo::Serialize) through the same graphics.big writer path that model import uses.
   - Ship it as a mod-pack layer.
   - Add a matching 'forge anim import <file> --rig <MESH> --name ANIM_X' CLI command. (M)

6. **Hook to a creature**
   - Optional: add the new ANIM to a cloned CAnimationSet def (the schema is in docs/re_reference/def_schema.json).
   - Keep this behind the in-game probe that ROADMAP_1.0.md:475 already requires. (M, needs the user to launch the game)

Overall effort is L and user value is high. Suggested order: read-only viewer first (steps 1 and 4), then install (steps 2, 3 and 5).

#### Animation bank browser (Assets > Animations list with metadata)
*gap, effort M, value high*

**Vanilla:** CAnimationDialog ctor 0x02929200. The 'Select Animation' section has a 'Normal Animations' tree (NORMAL_ANIM_TREE, xref 0x292bf38 and 0x292ee3a) grouped by the current thing's appearance def (GetSubDef(&pappearance_def) 0x292f072, 'no group' 0x292f38d). It also has 'Full Name' / 'In Game' name radios and 'Save Names' (0x2929844-0x2929953), which writes 'Anims.txt' (0x292be7f), plus a NUMBER_OF_FRAMES_TEXT readout (0x292e517).

**Fix:** 1) Reader library. Add libs/forgecore/include/forge/animbank.hpp and src/animbank.cpp.
- listAnims(graphicsBig): reuse forge::big, which meshpreview.cpp:256 already uses to find banks. Filter TOC entries of types 6, 7 and 9, and parse the 24-byte Info into 6Ã—f32.
- loadAnim(entry): if the first u32 is 0x3E3E3E3E, take the payload as raw. Otherwise run forge::lzo::decompress(payload+4, decompSize). Check first that lzo.hpp handles a bare stream with no [u16 clen] framing, and test it on all 3,435 entries.
- Walk the [fourcc][u32 size] chunks, recursing into ANRT, AOBJ and MVEC. Decode each XSEQ track (the quaternion key pool + palette, the i16 position pool + palette, SamplesPerSecond, PositionFactor), plus TMEV (name, time), AMSK and the cyclic flag. Port this from FableTLC tools/parse_anim_xseq.py, cross-checked against EgoCore Animations/AnimParser.h.
- Unit test: parse every graphics.big anim and assert the ANIM.md census (3DAF 3435, XSEQ 3419, TMEV 624, AMSK 34). Also check that the Info duration matches ANRT on about 95% of entries.

2) Animations page. Make 'Animations' the fourth segment in gui/textures.cpp:109 (assetsTab_==3), backed by a lazily built cache like texRows_. It should have:
- A list with search.
- Grouping by AOBJ rig name, plus a 'By creature' mode built from CAnimationSet defs in game.bin. This mirrors the vanilla appearance-def grouping, with 'no group' as the fallback.
- A detail pane showing duration, frame count (duration Ã— SamplesPerSecond, like vanilla's NUMBER_OF_FRAMES_TEXT), fps, cyclic flag, bone count, movement vector and rotation, the TMEV list, the AMSK/partial flag and a lipsync/pose tag.
- Name display toggle: internal ANIM_ name vs friendly name. Take the friendly name from the CAnimationSet slot/enum name or the text.big ANIM: keys, not from Anims.txt, which FableForge never writes.

3) CLI. Add 'forge anim list [--type 6|7|9] [--rig X]' and 'forge anim info <ANIM_X> [--json]' to tools/forge-cli/main.cpp.

This covers read-only browsing only. The 3D skinned preview (which needs the model skeleton + XSEQ sampling in meshpreview), event DB editing (game/sound_animation_events.txt/.bin) and .bncfg bone editing are separate follow-ups. Effort M, value high, since this is the foundation for every animation feature.

#### Skeleton and skin weights discarded by the mesh decoder (prerequisite for any animated preview)
*weakness, effort M, value high*

**Vanilla:** The dialog animates the thing's real render mesh: GetTCGraphicAppearance()->GetRenderMeshObject(mesh_obj) at 0x29336a1, and 'PCurrentThing->PeekBaseDef().GetSubDef(&pappearance_def)'. The engine loader is C3DMesh2::LoadBinary 0x00a8ad40 (Fable.exe, MESH.md). The animated vertex declaration is CVertexShaderInputAnimated::Initialise 0x00a90630, which adds bone index and weight elements.

**Fix:** 1. In meshpreview.hpp (libs/forgecore), add a `Skeleton` struct inside Geometry, left empty when boneCount is 0. It holds:
   - globalIds: vector<uint16_t> (raw block 1, local slot -> global Fable bone ID)
   - names: vector<string> (block 2)
   - for each bone: nameCrc, parentLocal (i32, -1 = root), originalChildren and localization[12] (block 3, 60 B)
   - bindTrs[11] for each bone: quat xyzw, translation, scale (block 4, 48 B)
   - inverseBind[16] for each bone (block 5, 64 B), with row 4 forced to (0,0,0,1)
   - derived world bind W_i = inverse(IBM_i) and local bind L_i = inverse(W_parent) * W_i, as in MESH.md Â§8
   Also add a parallel `std::vector<SkinWeights>` of `{uint8_t bone[4]; float w[4];}` records, sized to match vertices only when some primitive has animatedBlocks. The Vertex struct stays unchanged, so the static path, thumbnails and existing callers are untouched.
2. In meshpreview.cpp at lines 172-173, keep the buffers the code already reads: `c.skip(bones*2)` becomes a read, and the three fableLzo results are stored and decoded instead of dropped.
3. Around line 202, store the animated block's Groups[] (gc bytes) and its vertexCount instead of `c.skip(gc)`.
4. In the vertex loop, when abc>0, read ind[4] at iOff = packedPosition?4:12 and wgt[4] at iOff+4. Check that vertexLayout already puts the normal at iOff+8. Then:
   - pID = ind[k]/3
   - localBone = block.Groups[pID], clamped below boneCount with 0 as the fallback
   - w = wgt/255, renormalized, falling back to [1,0,0,0] when the sum is 0.001 or less
   - each block owns its next vertexCount vertices, walked in file order, as in MESH.md Â§9
5. Test this in libs/forgecore tests against retail graphics.big:
   - every skinned vertex's weights sum to about 1
   - every localBone is less than boneCount
   - names match the Blender-addon output (FableTLC tools/blender_addon/io_scene_fable/mesh_rw.py decode_to_compose_args is the reference oracle; EgoCore MeshParser.h/GltfExporter.h is the cross-check)
   - bone heads (the W_i translation) fall inside the mesh bbox
   - the MESH.md retail invariant holds: weight bytes sum to exactly 255 on all 82,073 skinned vertices, with at most 3 non-zero influences
6. Follow-ups that are not part of this item: a CPU linear-blend skinning helper in forgecore (pose matrices times IBM, weighted), which lets the D3D11 renderer (gui/renderer.hpp) redraw a posed vertex buffer without shader changes, and a `forge mesh skeleton <id>` CLI dump for inspection.

Effort M, value high. This is the prerequisite for ANIM 3DAF playback (docs/formats/ANIM.md tracks are keyed by global bone ID, so step 1's globalIds remap is required), for CBoneDialog-style bone editing, and for attach-to-bone.

#### Animation events editor (game, sound and stop events on frames; saves the animation event DB)
*gap, effort M, value high*

**Vanilla:** CAnimationEventsDialog ctor 0x028b2e60, SetThing 0x028b4d20. Controls: 'Insert game event' 0x28b3150, 'Insert sound event' 0x28b33a2, 'Insert Stop event' 0x28b345d, tabs All/Game/Sound (EVENT_VIEW_TAB 0x28b370a), 'Delete selected events' 0x28b3b70, 'View events for this frame only' 0x28b3dc3, 'Save'/'Revert' 0x28b3f35/0x28b4001, 'Sound name: ' 0x28b4e3d, GAME_EVENTS_LIST_BOX and SOUND_EVENTS_LIST_BOX. Save failure shows 'Unable to save animation event file - is it checked out?' (0x28b6b22). CEditControlCentre::SaveAnimationEventsIfChanged 0x0204b880 prompts 'Save changes to the animation event database?'. The backend is NAnimationEvents::CManager: LoadBinary 0x023f9320, SaveBinary 0x023f9650, GetEventSymbolMap 0x01e7f930, the 'UseEmbeddedEvents' switch at 0x23f9c60, and an 'event_index > 0' assert. Files: Data\Misc\{game,sound}_animation_events.{txt,bin}. The txt grammar is BEGIN_ANIMATION_EVENTS / 'BEGIN_EVENTS: <ANIM_NAME>' / '<EVENT_SYMBOL> <normalized time 0..1> START|STOP' / END_EVENTS; for example SE_FOOTSTEP 0.482759 START on ANIM_BIPED_GENERIC_MAN_WALK. The .bin layout, read from the bytes and still a hypothesis: u32 animCount, then per anim {u32 animKey, u32 n, n x {u32 eventSymbolIndex, u16 kind, f32 time}}. The game .bin holds 8 anims against 9 in the txt, so one entry does not resolve.

**Fix:** 1) **Format library.** Add libs/forgecore/include/forge/animevents.hpp and src/animevents.cpp.
   - Model: `struct AnimEvent{std::string symbol; float t; enum{Start,Stop} kind;}` and an ordered map from anim name to a vector of AnimEvent.
   - Parse the txt grammar: BEGIN_ANIMATION_EVENTS / 'BEGIN_EVENTS: <ANIM>' / '<SYM> <t 0..1> START|STOP' / END_EVENTS / END_ANIMATION_EVENTS.
   - Write it losslessly, keeping float text formatting (%g-style six significant digits, e.g. 0.482759), the anim order and blank lines.
   - Add a ctest that round-trips both retail files byte-for-byte (sound file 1,487 anims, game file 9) from the FableWin install's Data\Misc.
   - Use EgoCore EventBackend.h only as a cross-check.

2) **CLI.** Add `forge anim-events list|dump|set|del <game|sound> [anim]` so the logic is testable without the GUI.

3) **GUI.** First pass, without the animation player: an Assets > Animation Events page.
   - Left: an anim list filtered by the All/Game/Sound tab.
   - Right: a normalized 0..1 timeline strip, with markers coloured by START/STOP and by game vs sound.
   - Actions: insert game, sound or stop at the cursor; drag to retime; 'Delete selected events'; 'View events for this frame only'; Save and Revert.
   - Symbol pickers: SE_* names come from the existing txt entries and the sound bank; game symbols come from the game txt.
   - A dirty flag triggers a 'Save changes to the animation event database?' prompt on close or mod-pack switch, mirroring SaveAnimationEventsIfChanged.
   - When the ANIM (3DAF) reader lands, add the frame slider and the preview, and show the embedded TMEV events as read-only markers (the UseEmbeddedEvents path).

4) **Output.** Save as a mod-pack loose override of Data\Misc\{game,sound}_animation_events.txt. Never write into the shared install directly.

5) **.bin support.** This step blocks shipping .bin output.
   - Disassemble LoadBinary 0x023f9320 and SaveBinary 0x023f9650 in FableWin.exe with capstone. The symbols already show that the container is vector<pair<u32, CSmallVector<CEvent,32>>> sorted by the key.
   - Pin down three things: whether the key is a CRC of the anim name or a graphics.big id; whether the symbol index comes from the CSymbolMap (see GetEventSymbolMap 0x01e7f930); and the CEvent field order.
   - Test the hypothesis {u32 count; per anim {u32 key; u32 n; n x {u32 symIdx; u16 kind; f32 t}}} against the retail .bin (8 vs 9 anims; the missing one probably fails the 'event_index > 0' assert).
   - Also check which file retail Fable.exe loads (grep its strings for animation_events and follow the xrefs). If it reads the .bin, the txt alone is not enough and the mod pack must ship a regenerated .bin.

**What already exists:** txt: EgoCore (partial) and the grammar in FableTLC docs; ANIM 3DAF/TMEV: docs/formats/ANIM.md; .bin: nothing.

**Effort:** M (the txt editor alone is S-M). **Value:** high.

#### Animate a placed thing in the level (THING_ANIMATION / EDIT_ANIMATION modes, the Anim tab and 'Edit Animation')
*gap, effort M, value med*

**Vanilla:** Edit modes 9 THING_ANIMATION and 19 EDIT_ANIMATION (SetEditMode 0x02036dd0). CEditInputProcessAnimation ctor 0x0202e420 takes the CAnimationDialog; ProcessInput 0x02959a70 asserts 'ControlCentre.GetEditMode()==EDIT_MODE_EDIT_ANIMATION' (0x2959adb). CAnimationDialog::SetThing 0x02930bf0 requires TCI_EDITOR_ANIMATION_THING or TCI_EDITOR_ANIMATION_CREATURE (assert 0x2930c79), and IsThingCreature is at 0x02932f10. The CThingPropertyDialog ctor 0x0290d830 takes a CAnimationDialog*; its main tab has the 'Edit Animation' button and its 'Anim' tab plays normal or combination anims, carry object, speech and reset position (VANILLA_EDITOR_INVENTORY.md lines 220-222).

**Fix:** This depends on one shared core, also needed by the Assets > Animations player.

**Core A: forgecore/anim**
- Parse graphics.big MBANK entries of types 6, 7 and 9 (the ANIM_* entries): a u32 decompSize followed by one LZO1X stream.
- Reuse the fableLzo helper already in meshpreview.cpp.
- Decode the chunks ANRT, AOBJ, XSEQ, HLPR, MVEC, TMEV and AMSK, following D:\Documents\FableTLC\docs\formats\ANIM.md (all 3,435 of 3,435 entries parse) and EgoCore's AnimParser.h, which is the reference.
- Output: a per-bone keyframe track list (rotation, position, scale), the frame count and fps, movement vectors (MVEC) and time events (TMEV).
- Add a `forge anim info|dump <name>` CLI verb and a round-trip test over every bank entry.

**Core B: skeleton and weights**
- Extend meshpreview.cpp:172-173 to keep, rather than skip, the bone names, the parent/local records (60 bytes each), the bind matrices (48 bytes each) and the inverse-bind matrices (64 bytes each).
- Also keep the per-vertex bone indices and weights from the animated blocks (meshpreview.cpp:193-235 currently flattens them into bind-pose triangles).
- Use EgoCore's mesh parser as the answer key.
- Add CPU skinning, or a GPU palette upload, to the renderer: a new optional bone-palette buffer on InstanceDraw (renderer.hpp:80-85).

**Feature: 'Animate' in the property grid** (gui/editor.cpp:388 drawPropertyGrid)
- **When it shows:** only when the selected thing's graphic is a skinned mesh (boneCount > 0). This mirrors the SetThing assert at 0x02930bf0, which only accepts TCI_EDITOR_ANIMATION_THING or TCI_EDITOR_ANIMATION_CREATURE, and IsThingCreature at 0x02932f10.
- **Tool window:** a docked window lists the anims from the thing's CAnimationSet def. Fall back to anims whose AOBJ bone-name set matches the mesh skeleton, and use Anims.txt display names when present.
- **Playback controls:** a frame slider, play/pause, speed, interpolate, and loop.
- **Placement controls:** a 'Track movement' toggle that applies the MVEC root motion to the instance matrix preview only, and a 'Reset position/orientation' button that restores the stored .tng transform.
- **Retarget and close:** selecting another thing retargets the tool, matching SetThing. Closing the tool restores the bind pose.
- **Read-only:** nothing is written to the .tng or the level, matching vanilla EDIT_ANIMATION mode 19 and THING_ANIMATION mode 9. While active, it disables drag/move input as CEditInputProcessAnimation::ProcessInput (0x02959a70) does.
- **Event markers:** optionally draw TMEV markers on the slider, and join them with Data\Misc\game_animation_events.txt and sound_animation_events.txt (plain text, readable now; the .bin companions are only partly understood).
- **Quest link:** the quest node 'Play Animation' picker (questnode_defs_data.hpp:1357) can open the same preview.

**Formats: what FableForge can read today**
- It can read graphics.big bank access, the LZO decompressor and the def schema (CAnimationSet, CCarryableDef, CCarryingDef in def_schema.json).
- It cannot yet read ANIM chunks or the mesh bone and weight data.

**Effort and value**
- Effort: L including Cores A and B; M once they land with the Assets > Animations player.
- Value: med.

#### Edit embedded TMEV timing events inside an ANIM entry
*gap, effort M, value med*

**Vanilla:** 3DAF chunk TMEV (C3DAnimFileTimingEventChunk: cstr eventName, f32 time) in 624 retail anims (FableTLC ANIM.md Â§3). The engine chooses embedded or DB events through the 'UseEmbeddedEvents' switch (0x23f9c60, NAnimationEvents code).

**Fix:** Do this in two steps, and only after the animation reader (Assets > Animations page) exists.

(a) Read-only first, effort S. When the animpreview reader parses an ANIM entry (graphics.big MBANK, u32 decompSize + LZO1X â†’ 3DAF ANRT âŠƒ HLPR âŠƒ TMEV), decode every TMEV leaf as {cstr eventName, f32 time}. Show those markers on the timeline strip beside the DB events from Data\Misc\game_animation_events.txt/.bin and sound_animation_events.txt/.bin. Label each one "embedded (TMEV)" or "DB". Add a tooltip saying the engine chooses the source through CManager::UseEmbeddedEvents (PDB member at offset 76, per struct_layouts_egor.tsv), not through a vanilla dialog.

(b) Editing later, effort M, value low-med. Neither FableForge nor anim_build.py can write TMEV today; anim_build keeps TMEV as a raw leaf (tools/anim_build.py:21).
- Add a TMEV codec to libs/forgecore (a new anim module ported from FableTLC tools/anim_build.py, checked against EgoCore AnimParser.h/AnimCompiler.h): chunk tag 'TMEV', payload cstr + f32, and when nothing is edited, emit the original raw bytes unchanged.
- Rebuild the HLPR/ANRT chunk sizes, re-LZO1X, and write the entry into a mod-pack graphics.big layer through the existing big writer. Update the decompSize prefix and the 24-byte TOC Info record.
- Gate: a no-op round-trip must be byte-identical on all 624 TMEV-bearing retail anims before any edit is allowed.

Do not call this vanilla parity. The vanilla CAnimationEventsDialog writes only the event DB; FableWin's CFillTimingEventNames (0x0323a290) only reads timing-event names into a list. Build DB-event editing (the true vanilla feature) first.

#### Animation export (ANIM to glTF/FBX) for round-tripping in DCC tools
*gap, effort M, value med*

**Vanilla:** Vanilla has no exporter; the in-house pipeline used the 3ds Max Biped exporter that produced .bba. This row is the asset-pipeline counterpart the user asked for ('don't forget about assets ... animations').

**Fix:** Prerequisite: extend forgecore meshpreview (meshpreview.cpp:172) so it keeps the skeleton instead of skipping it. Keep the bone names (the CRC-matched name block), the 60-byte bone records (parent index plus local transform), and the 48-byte and 64-byte matrix arrays (bind and inverse-bind). Also keep the per-vertex bone indices and weights from the AnimatedBlocks groups. Base the layout on EgoCore Meshes (C3DMeshContent) and GltfExporter.h:329-410. This is the same skeleton the viewer/player row needs.

Then:
(1) Add forgecore/anim.{hpp,cpp}. It reads the graphics.big MBANK entries of types 6, 7 and 9: a u32 decompSize, then one LZO1X stream decoded with the existing fableLzo, then the chunks ANRT, AOBJ, XSEQ (the pooled/palettized quaternion and position tracks with positionFactor, per FableTLC docs/formats/ANIM.md Â§9), HLPR, MVEC, TMEV, AMSK and XALO. Port it from FableTLC tools/parse_anim_xseq.py and EgoCore Animations/AnimParser.h, and add a regression test that parses all 3,435 retail entries.
(2) Extend src/glbwriter with skins/joints/inverseBindMatrices, the JOINTS_0/WEIGHTS_0 attributes, and animation channels. Write keys at the source frame rate (the track frame palette) instead of resampling to 30 fps like EgoCore. Sign-align the quaternions and use LINEAR interpolation; offer an optional --bake-fps.
(3) Add 'forge anim export <ANIM_NAME|index> --mesh <MESH_NAME> [--anim more ...] out.glb' in tools/forge-cli, plus 'forge anim list'. Put Duration, NonLoopingDuration, MovementVector, the raw MVEC, TMEV events and the game/sound_animation_events.txt entries for that anim into the glTF clip under extras.fable, like EgoCore's FableAnimData. Put AMSK in extras too. Keep the coordinate conventions from ANIM.md Â§5, including the movement_dummy 180-degree rotation; EgoCore GltfExporter.h:183-194 shows the pitfall.
(4) GUI: an Export... button on the Assets > Animations page, reusing the file dialog from gui/textures.cpp.
(5) Round-trip test: export to glTF, run it through the existing FableTLC anim_build donor-clone path or a later forge anim import, re-parse, and compare per frame with the ANIM.md Â§11 tolerances. Validate the file with the Khronos glTF validator.

Effort is M-L, not M, because FableForge has neither an ANIM parser nor skeleton retention yet; S-M once the Animations viewer row has landed. Value: med.

#### Carry object / carry slot preview
*gap, effort M, value low*

**Vanilla:** 'Carry Object' section (ANIMATION_SECTION3_TAB 0x292a10b, caption 0x292a151). PopulateCarryObjectList 0x0292ff30 enumerates OBJECT defs ('OBJECT' 0x2930177, def_manager.GetDef 0x293027e) into CARRY_OBJECT_LIST_BOX (0x292f967). OnCarryObjectSelected 0x0292f890 fills CARRY_SLOT_LIST_BOX (0x292fb39) with "<Object's Default Slot>" (0x29307d2) plus the creature's slots. Slot def fields: AvailableCarrySlots, ActiveCarrySlot, SecondaryActiveCarrySlot, PassiveCarrySlot and PassiveCarrySlotScale (0x4091b50-0x4091cbc), CCarryableDef and CCarryingDef.

**Fix:** Build it in two phases.

**Phase 1: static carry preview in the bind pose (S/M, can be built now).**
1. Add a Carry panel to the model/creature preview on the Assets page. It lists OBJECT defs that have a CCarryableDef, decoded with forgecore defdecode against def_schema.json (CCarryableDef retail offsets are known).
2. For the selected carrier creature, read CCarryingDef.AvailableCarrySlots. That is a vector of CCarrySlotDef indices; its offsets are donor-only, so confirm them against EgoCore or a retail record first. Use CCarryingDef.OverriddenDummyObject if it is set.
3. Default the slot to the object's ActiveCarrySlot, then offer SecondaryActiveCarrySlot, PassiveCarrySlot and the rest of the carrier's slots. This mirrors vanilla's "<Object's Default Slot>" entry in OnCarryObjectSelected at 0x0292f890.
4. Resolve the slot's CCarrySlotDef.DummyPosName, or SecondaryDummyPosName for the secondary position, to the carrier mesh's Helper by name or CRC. Helpers come from meshpreview::Geometry::helpers, which is already decoded. Use DummyPosIndex to pick between helpers that share a name.
5. Draw the carried object's LOD0 mesh (meshpreview::readLod0) with the transform helper.matrix Ã— translate(OffsetCoordRelativeToAttachToDummy). For the passive slot, also scale by PassiveCarrySlotScale.

**Phase 2: attach to the animated bone (M, only after the ANIM player lands).**
1. Add skeleton/bone-hierarchy decoding to meshpreview. It currently only counts bones. EgoCore's mesh parser is the reference.
2. Evaluate the pose from the ANIM (3DAF) XSEQ tracks. The format is decoded in FableTLC docs/formats/ANIM.md; EgoCore AnimParser.h is the reference parser.
3. Each frame, compose boneWorld[helper.bone] Ã— helper.matrix Ã— offset, so the carried object follows the animation, matching vanilla CAnimationDialog carry mode.

**Data read** (no writes; this is preview-only, as in vanilla): game.bin defs CCarryableDef, CCarryingDef and CCarrySlotDef, plus the carrier and carried object's OBJECT/CREATURE graphics defs; graphics.big MBANK meshes (FableForge already reads these); and, for phase 2, graphics.big ANIM entries (FableForge has no reader yet).

### Bones and attachments. This covers CBone...

#### Target-side, multi-toggle attach mode with incoming link lines
*weakness, effort M, value high*

**Vanilla:** You select the TARGET (a building, village, receptor or owner), pick a viable mode (GetViableAttachModesForThing 0x020342f0), then click any number of things. Each click toggles its link (ToggleThingAttachment 0x020326e0), and only viable things can be selected (CThingFilter_IsSelectableBasedOnEditorMode 0x0203fc90). CAttachingThingsDialog 0x0290c590 shows 'X attaching to:' with a Stop button. DrawAttachModeLines 0x02048230 draws the links of everything attached to the target.

**Fix:** Data format: .tng UID fields that FableForge already reads and writes through Document::linksOf and setLink (src/leveledit.cpp:1368 kLinkKinds and :1712-1760). No new format is needed. The link is still stored on the source thing (VillageUID, OwnerUID, HomeBuildingUID, WorkBuildingUID, ReceptorUID, EntranceConnectedToUID, ThingToCalculateRouteToUID, WifeLivingHereUID), exactly as vanilla stores it.

1) Document (src/leveledit.cpp/.h): add `std::vector<IncomingLink> linksInto(size_t target) const`. Back it with a lazily built map from target UID to (sourceIndex, ctc, field, label), cached against revision_ and rebuilt when the revision changes. Also add `std::vector<AttachMode> viableAttachModes(size_t target)`, the port of GetViableAttachModesForThing 0x020342f0. It returns {field, ctc, caption}, using the vanilla captions: Village gives "Attach things to village" (VillageUID). A building gives "Attach people who live here" (HomeBuildingUID) and "Attach people who work here" (WorkBuildingUID). An activation receptor gives "Attach triggers to this receptor" (ReceptorUID). Any thing gives "Attach objects" (OwnerUID). A region entrance gives the exit-to-entrance mode. A buyable house gives the wife mode. Also add `bool canAttach(size_t source, const AttachMode&)`: the source must carry the kLinkKinds ctc (or accept its field), and the target must pass linkTargetFits. Before hard-coding the per-type rules, decompile 0x020342f0 and 0x0203fc90 (with capstone, or with DecompAt.java if Ghidra is not locked) so the rules match vanilla exactly.

2) GUI (gui/editor.cpp, selected-thing card after the Links block at about :2977): add an "Attached here" section. List linksInto grouped by field (Residents / Workers / Members / Triggers / Owned objects / Routes to). Clicking a row selects that source, and an x button clears it with setLink(src, ctc, field, 0). Each viable mode gets an "Attachâ€¦" button that sets `attachMode_ = {targetIndex, field, ctc, caption}`.

3) Attach mode (pickAt, editor.cpp:859): when attachMode_ is active, each click on thing T toggles the link: if T's field already equals the target UID, set it to 0; otherwise set it to the target UID. Make each click one undo step, and leave the mode active until Esc or a Stop button. Show a small overlay banner "<target> attaching to: <caption>  [Stop attaching objects]", mirroring CAttachingThingsDialog 0x0290c590. While hovering, tint things that fail canAttach grey and ignore clicks on them. That tinting is the CThingFilter_IsSelectableBasedOnEditorMode behaviour.

4) drawLinkLines (editor.cpp:834): while a thing is selected or attachMode_ is active, also draw linksInto(target) as lines from each source to the target (DrawAttachModeLines 0x02048230), with one colour per field and a legend. Keep the outgoing lines. Cap the label text when there are more than about 50 lines (villages have hundreds of members), and still draw the lines.

5) Register the automation widgets (btn_attach_<field>, btn_stop_attaching) and add a leveledit unit test for linksInto and the toggle round-trip on a retail .tng.

Effort M, value high: villages, houses and workplaces are among the most common edits, and this is the only way to see who belongs to a target. Assets note: this needs no anim, bone or mesh data. Bone and carry-slot attachment is a separate, def-driven gap.

#### Cannot link a creature to a home/work building, spouse or parents when the field is absent
*weakness, effort S, value high*

**Status 2026-09-30:** Home, Work, Father, Mother and Spouse are offered on
AICreatures and saved in native order. Spouse edits are reciprocal and reject
an already-linked target. Optional fields disappear on clear or target
deletion. The GUI checks decoded parent sex when available and warns when
unknown. Document::setLink also rejects known mismatches when a definition
lookup is attached; the editor supplies one on map open. Core and retail
male/female UI tests pass.

**Vanilla:** ATTACH_THINGS modes 'Attach people who live here' / 'who work here', 'Select creature's spouse' and 'Select creature's parents' (captions at 0x0415e958..0x0415fc50; 'Set as father/mother to selected creature', spouse assert at 0x0415f9f8) write the CThingAICreature fields. The persisted order (strings at 0x040d8f04) is HomeBuildingUID, WorkBuildingUID, FatherCreatureUID, MotherCreatureUID, SpouseCreatureUID, OverridingBrainName. They are only written when set: of 1168 retail creature blocks only 4 have HomeBuildingUID and 6 have WorkBuildingUID.

**Fix:** Plan (effort S; value high for Home and Work, medium for Father, Mother and Spouse):

1. Add a flag to LinkKind in src/leveledit.cpp:1365, e.g. `const char* offeredOn;` set to "aicreature" for the thing-level creature links. linksOf then offers a missing field as target 0 when lower(t.type) matches, instead of skipping it with `continue`. Other rows keep today's behaviour.

2. Add three rows after Work:
   - {"", "FatherCreatureUID", "Father", "a creature"}
   - {"", "MotherCreatureUID", "Mother", "a creature"}
   - {"", "SpouseCreatureUID", "Spouse", "a creature"}
   Before fixing where they sit in the thing, confirm the persisted field order in FableWin at the string table around VA 0x040d8f04 and in Fable.exe around file offset 0xe72740, where the order looks like OverridingBrainName, then Spouse, then Father. Also read the tng loader in the Fable.exe CThingAICreature persistence to see where it expects each field.

3. linkTargetFits:
   - Father, Mother and Spouse must be an AICreature or Creature and not the creature itself.
   - Mother: check the target's CREATURE def Sex/Gender through the def lookup FableForge already uses for creature placement. Father: same check for male. If the def cannot be resolved, only warn.
   - Spouse: refuse a target whose own SpouseCreatureUID is not 0, matching vanilla's assert selected_creature.GetSpouseCreature()==NULL.

4. setLink, for a key that is missing:
   - Do not use the append in File::setThingProperty, which inserts at thing.endLine.
   - Add a forgecore helper, File::insertThingPropertyBefore(thingIndex, key, value, anchorKey), that inserts the line just before the anchor. The anchor is `OverridingBrainName`, or failing that the line after `Health`, then endLine.
   - Put Home before Work (a new Work goes after an existing Home), and Father, Mother and Spouse in the order the loader expects.
   - When the link is cleared to 0 on a field that setLink inserted, or on one absent from the original, remove the line with removeThingProperty, so an untouched retail file saves byte-identical. Retail never writes a 0 Home or Work line.
   - All of this goes through pushUndo as today.

5. Decide spouse symmetry from vanilla: disassemble ToggleThingAttachment 0x020326e0 and the spouse branch of SetAsAttachingWithMode 0x0291f370 with capstone. If vanilla writes both creatures, setLink('SpouseCreatureUID') also writes the partner under the same undo step, and clearing it clears both. The same goes for Father and Mother: vanilla's caption is "Set as father/mother to selected creature", so the picked thing is the parent and the field goes on the selected child.

6. GUI: nothing new is needed. The links card (gui/editor.cpp:~2950 Pick/x) and drawLinkLines (gui/editor.cpp:833) already draw every Link that linksOf returns. Check that the 'wants' text shows 'a building' or 'a creature' in the picker filter.

7. Tests in tests/test_export.cpp next to the case at :1339:
   (a) a creature with no Home line lists 'Lives in' as 0;
   (b) setLink inserts HomeBuildingUID between Health and OverridingBrainName;
   (c) setting Work after Home keeps the order Home then Work;
   (d) clearing to 0 removes the line, and the file is byte-identical to the original;
   (e) Mother is rejected for a male def and Spouse is rejected when the target already has one;
   (f) the spouse write is symmetric if step 5 confirms it.

Data formats: only FinalAlbion .tng thing blocks, which forgecore tng.cpp already parses and edits line by line. Retail Fable.exe reads all five keys, so no engine or FSE work is needed.

#### Skinned creature preview that shows bone scaling (and later animation)
*gap, effort L, value med*

**Vanilla:** CBoneDialog edits the live thing's render mesh (SetThing: GetTCGraphicAppearance, GetRenderMeshObject, GetRenderMesh), so scale changes show at once in the 3D view. EDIT_BONES input (CEditInputProcessBone::ProcessInput 0x02958ab0) orbits the camera around that creature.

**Fix:** 1) forgecore/meshpreview: stop throwing the bone data away. At :172-173, keep BoneIndices (u16 x bones), BoneNames (the LZO block of names), Bones (60 B each: NameCRC, ParentIndex, OriginalNoChildren, 3x4 LocalizationMatrix) and the 48 B and 64 B per-bone blocks as raw bytes. Follow EgoCore MeshParser.h:671-687 as the answer key.
   - At :199, keep each animated block's u32, u16 BonesPerVertex, u8 PalettedFlag and the `gc` palette Group bytes. Don't skip them.
   - In the vertex loop, when abc>0, read the packed joint indices and weights at the start of the vertex (the normal offset moves by 8). Remap each palette index through Groups of the block that owns the vertex. Walk the blocks by VertexCount, exactly as EgoCore MeshRenderer.h:355-401 does.
   - Add Vertex.joints[4] and weights[4], and MeshPreview.bones and boneNames.
   - Add a unit test: for a retail creature from graphics.big (e.g. a hero or a guard), check that the weights sum to about 1 and all joint indices are below boneCount.

2) forgecore/skeleton.{hpp,cpp}:
   - Build the bind world matrices by walking ParentIndex over the LocalizationMatrix. Find which of the LocalizationMatrix, the 48 B block and the 64 B block is local vs inverse-bind by checking against EgoCore GltfExporter.h:1536 (inverseBindMatrices).
   - pose(localOverrides, perBoneScale) returns the skinning palette, world * inverseBind.

3) forgecore/bncfg.{hpp,cpp}: a tiny CRLF text reader and writer for `Creature_type: X;`, the #Start/#End_group_settings block (`name: "Bone A", "Bone B";`) and the #Start/#End_Bone_data block (`Bip01 Pelvis: x, y, z;`).
   - Round-trip all 60 retail Data\Bones\*.bncfg byte-for-byte in a test.
   - Match bones by name or CRC to boneNames.
   - Before assuming it, verify how the runtime applies C3DSkeletalBoneMorph.Scaling: local scale that children inherit, or non-inherited. Disassemble the skeletal-morph apply path near CSkeletalMorphResourceManager (0x023e3f10) and its callers in FableWin, or find it in the Ego_r PDB types. Do this rather than guessing.

4) Renderer: do CPU linear-blend skinning for the one previewed thing only (4 weights, same maths as the EgoCore HLSL at MeshRenderer.h:203-210). Upload it into the existing instance VBO. gui/renderer.cpp needs no shader change.

5) GUI: build a reusable CreaturePreview widget (an orbit and zoom camera like CEditInputProcessBone::ProcessInput 0x02958ab0).
   - Put it on the selected creature's card and on a new Assets > Bones page.
   - The page gives a bone list with multi-select, X/Y/Z factor fields with locks (the CBoneDialog ChangeSelectedBonesBy / AreSelectedBonesScalingFactorsIdentical semantics), groups (reject colons and duplicate names, as vanilla does), and Load and Save .bncfg with a creature-type mismatch warning.
   - Save .bncfg through the mod-pack or loose-file path so the runtime picks it up. PreloadBoneConfigs reads it from the bone config dir.

6) The later Assets > Animations page reuses the same skeleton::pose path. Feed it per-frame local transforms from .bba / graphics.big anim entries, using EgoCore Animations/AnimParser.h as the answer key.

Effort L: the mesh decode is M and the rest is M. Value med-high, because it unlocks animation preview.

#### Creature random appearance: seed shown as a raw int, variant not previewed, and new creatures get no seed
*gap, effort L, value med*

**Vanilla:** CTCRandomAppearanceMorph (Seed in the tng, 584 retail creatures). OnCreate 0x0252d9c0 calls SetAppearanceSeed 0x0252dba0, then UpdateMorphSet 0x0252dbd0, then CRandomAppearanceMorph::GetRandomMorphSet 0x0289ac90, which picks body-part meshes (GetRandomBodyParts 0x0289b110), per-mesh texture swaps (GetRandomTextureMorphs 0x0289b300) and a skeletal morph, i.e. a .bncfg build (GetRandomSkeletalMorph 0x0289ad40). The data is CCreatureDef+48, a 76-byte CRandomAppearanceMorph. OnAppearanceDraw 0x0252e720 renders the variant, so the editor shows every villager with its own look.

**Fix:** 1) Def decode (M). Add a CCreatureDef.RandomAppearanceMorph entry to the forgecore def schema, driven by chocolatebox_def.xml control 237DCBE3 (line 5014). Cross-check the byte walk against CRandomAppearanceMorph::TransferBinaryIn 0x0289b880 in FableWin, which is the real binary reader. The data is: an array of body parts, each an array of CBodyPartMesh {ModelID, MeshGroup, AddTextureToMesh[]} (a CVectorMap<ulong, CTextureMorph>); a skeletal-morph map keyed by long (group) pointing to a CSkeletalMorph that names .bncfg builds through NamesBINOffset; and one trailing ulong. Test the decode over every retail creature def in game.bin: the whole blob must be consumed with no bytes left over. Nothing parses this today, not forgecore, EgoCore or FableTLC.
2) Variant selection (M). Disassemble GetRandomMorphSet 0x0289ac90, GetRandomBodyParts 0x0289b110, GetRandomTextureMorphs 0x0289b300 and GetRandomSkeletalMorph 0x0289ad40, and port them to forgecore/appearancemorph.cpp. Keep the engine's RNG and its call order; confirm the generator first rather than assuming it is thingplacer's vanillaFloatRandom. Output: the chosen body-part ModelIDs, the texture swaps per mesh, and the chosen skeletal morph name.
3) Placement (S). In placeCreature, emit `StartCTCRandomAppearanceMorph; Seed <signed int>; EndCTCRandomAppearanceMorph;` in retail block order whenever the def has a RandomAppearanceMorph, with the seed drawn from placeSeed_. Before adding the block, confirm with retail .tng samples that the block appears exactly when the def has one.
4) Card (S). Relabel the Seed row as "Appearance seed" and add a Reroll button that writes a fresh seed.
5) Viewport (M, after step 2). In thingsexport and the renderer, draw the chosen body-part meshes with their diffuse textures swapped, instead of only the base Graphic (src/thingsexport.cpp:339). Apply the .bncfg bone scaling only once skinned skeleton support exists; that part is out of scope here.
6) CLI (S). Add `forge creature-variants <DEF> [--seeds N]` to print the choice for each seed, as a check against the port.
Effort: L overall. Value: medium.

#### Bone configuration (.bncfg) editor: an Assets > Bones page
*gap, effort M, value med*

**Vanilla:** CBoneDialog (ctor 0x02937ac0, SetThing 0x02939b40, ~27 methods named in fablewin_pdb.xml). It lists every bone of the selected creature's render mesh as a button (BONE_BOX, AddBoneButtons ~0x0293a4e0) with multi-select. X/Y/Z_FACTOR number controls have per-axis LOCK buttons (IsX/Y/ZLocked, AdjustValueBy, ChangeValueTo, ChangeSelectedBonesBy ~0x0293cc80). MULTI_*_TEXT shows up when the selected bones' factors differ (AreSelectedBonesScalingFactorsIdentical ~0x0293f770). Named bone groups can be created (AddGroup ~0x0293b3b0: no colon, unique names). It saves and loads *.bncfg (SaveSettingsToFile ~0x0293cfa0, LoadSettingsFromFile ~0x0293da90) and refuses a file whose Creature_type is not the selected thing's def ('Mismatched thing type'). The same files are loaded by the game at startup (CSkeletalMorphResourceManager::PreloadBoneConfigs 0x023e3f10, reader game_tools.cpp ~0x0287d630).

**Fix:** Effort M, user value medium. Nothing in FableForge, EgoCore or FableTLC parses .bncfg today. Mesh bone records are parsed only by EgoCore.

1) libs/forgecore/include/forgecore/bncfg.hpp and src/bncfg.cpp:
   - Struct: `BoneConfig{ std::string creatureType; std::vector<Group{name, std::vector<std::string> bones}>; std::vector<BoneRow{name, float x, y, z, std::string rawText}> }`.
   - The parser tokenizes on ':' ';' ',' and quoted strings. The grammar is `Creature_type: X;`, then an optional `#Start_group_settings` ... `#End_group_settings` block with lines of the form `name: "b", "b";`, then `#Start_Bone_data` with lines of the form `Bone Name: x, y, z;` (bone names contain spaces, so split on the last ':'), ending at `#End_bone_data`.
   - Keep the original line text and write it back unchanged for untouched rows. Use CRLF.
   - Test: a load and save of all 60 files in <game>\Data\Bones must be byte-identical. Add a fixture test gated on the game path.
   - Before shipping, confirm the tokenizer by disassembling the reader, game_tools.cpp ~0x0287d630 ("Invalid bone file."), with capstone, and check whether whitespace or case matters.

2) meshpreview: stop discarding the bone blocks at meshpreview.cpp:172. Decode the bones into `Geometry::bones{name, nameCrc, parent, float local[12]}`:
   - `u16 BoneIndices[n]`.
   - LZO names block (null-separated).
   - 60-byte C3DBone: `{u32 NameCRC, i32 ParentIndex, u32 OriginalNoChildren, float[12]}`.
   - Keep the 48-byte and 64-byte blocks as raw data for now.
   - Cross-check the layout against EgoCore Meshes/MeshParser.h and GltfExporter.h:1373 (ParentID via BoneIndices).

3) Resolving a creature to its bones: map the CREATURE def to CTCGraphicAppearance, then to the Graphic mesh id, then load that mesh from graphics.big with the existing forgecore big reader and meshpreview. The result is the bone list.

4) GUI, in gui/textures.cpp next to Textures/Models/Ground themes: a new Assets > Bones page.
   - Left: the Data\Bones file list, plus "New from creature" (a CREATURE def picker).
   - Middle:
     - Bone tree with multi-select.
     - Group chips: create a group from the selection. Reject names that contain ':' or are already used, as vanilla AddGroup does. Clicking a chip selects its bones.
     - X/Y/Z drag fields with per-axis lock toggles. Locked axes change together (vanilla AdjustValueBy/ChangeValueTo). Show "mixed" when the selected bones differ (AreSelectedBonesScalingFactorsIdentical).
   - Right: the existing renderer. Until skinning exists, draw a stick-figure skeleton with bone lengths scaled by the factors.
   - Validation:
     - Creature_type must equal the chosen def (vanilla message: "Mismatched thing type").
     - Every bone name must exist in the mesh ("Unknown bone being modified").
     - Show a hint for files that CHeroMorphDef.SkeletalMorphs or CCreatureDef.RandomAppearanceMorph reference by name. Find these by a string search over the defs, so the user knows which runtime morph each file feeds.

5) Writes go to <game>\Data\Bones\*.bncfg. Keep a .forge-orig backup and refuse to write while Fable.exe is running. Also add the file to mod packs.

6) CLI: `forge bncfg list|show <file>|set <file> <bone> x y z|validate <file>`, where validate resolves the def and the mesh bones.

#### Mod packs / installs ignore Data\Bones (and other loose Data dirs)
*gap, effort S, value med*

**Vanilla:** The game preloads every *.bncfg from GetBoneConfigDir at startup (0x023e3f10 plus the '*.bncfg' string at 0x04201072). 60 retail files. Hero physique (CHeroMorphDef.SkeletalMorphs) and NPC builds (CCreatureDef RandomAppearanceMorph.AddSkeletalMorph by name) resolve against them.

**Fix:** Add a general passthrough for loose Data files to the mod pack, starting with Data\Bones\*.bncfg. Do not special-case bones inside the level-diff code.

(1) Format. A pack gets an optional data/Bones/ folder that mirrors <game>\Data\Bones. The files are CRLF ASCII (Creature_type / #Start_group_settings / #Start_Bone_data). Nothing in FableForge parses them yet, so version 1 copies them as opaque bytes and checks only the text structure.

(2) src/modpack.cpp prepareShadow. When not viewOnly, copy <game>\Data\Bones\* into shadow/data/Bones, then the pack's data/Bones/* over it. This uses the same copy lambda as the Levels\FinalAlbion loop at :285-295.

(3) capture(). Diff shadow/data/Bones against gameRoot/data/Bones by lowercased filename and byte compare. Put new or changed files into rep.files and write them to pack/data/Bones. This is the same pattern as the loose-level branch at :352-358.

(4) Install/apply. Write the files to <game>\Data\Bones with the existing .forge-orig backup of any retail file it overwrites, and add them to the uninstall/restore list. Pack-vs-pack conflicts are keyed on the filename, the same as loose level files.

(5) Validation, a cheap version that needs no new parser: read the first line, `Creature_type: X;`, and warn if X is not a CREATURE def name in game.bin (the defs reader already exists in libs/forgecore). Warning when no def references a morph name (CHeroMorphDef.SkeletalMorphs, CCreatureDef RandomAppearanceMorph) has to wait for the appearance-morph decode gap.

(6) Write it as a small list of loose-data rules ({"Data/Bones", "*.bncfg"}) so that later dirs can be added without new code. Leave install-level (levels only) unchanged.

Effort S. Value low-med now, rising to med once the CBoneDialog-equivalent bone editor (a .bncfg writer, the SaveSettingsToFile/LoadSettingsFromFile equivalents) exists and produces these files. Build it together with, or just after, that editor.

#### No carry-slot / bone-attachment preview (weapons, carried items, clothing on creatures)
*gap, effort M, value low*

**Vanilla:** CAnimationDialog::PopulateCarrySlotList, PopulateCarryObjectList, OnCarrySlotSelected and OnCarryObjectSelected put any carryable object into a creature's slot for preview. The slots are defs: CCarryingDef.AvailableCarrySlots, then CCarrySlotDef{DummyPosName, DummyPosIndex, PrimarySlot, SecondaryDummyPosName}, which names a helper dummy in the creature mesh. CCarryableDef gives Active/SecondaryActive/PassiveCarrySlot and PassiveCarrySlotScale. The App tab's 'Toggle Clothing Item' (CLOTHING_LIST, CTCHeroAttachableAppearanceModifiers) previews clothing. CParticleAttacherDef.ParticlesToAttach puts effects on dummies.

**Fix:** This depends on the Assets > Animations / creature preview page from the animation gap. Build that first, or fold this into it.

1) forgecore meshpreview: stop skipping bone data (meshpreview.cpp:172). Decode the bone hierarchy and the bind (inverse-bind) matrices. Use EgoCore's mesh reader as the answer key: C:\Users\Cornelio\Documents\EgoCoreInspect\EgoCore-master mesh/skeleton code. Expose a function dummyWorldMatrix(dummy) = bone bind world matrix x HDMY local matrix. Dummy names and bone indexes are already parsed at :161-166.

2) Def access: the game.bin def reader already resolves defs by name with def_schema.json offsets. Add typed getters for:
   - CCarryingDef.AvailableCarrySlots (list of CCarrySlotDef refs)
   - CCarrySlotDef {DummyPosName, DummyPosIndex, PrimarySlot, SecondaryDummyPosName}
   - CCarryableDef {ActiveCarrySlot, SecondaryActiveCarrySlot, PassiveCarrySlot, PassiveCarrySlotScale}
   - CParticleAttacherDef.ParticlesToAttach
   Verify the retail offsets against the struct_schema.json CCarrySlotDef entry and ChocolateBox before trusting them.

3) UI, in the creature preview panel: add a 'Carry slot' combo, filled from AvailableCarrySlots and mirroring PopulateCarrySlotList. Add an object combo filtered to OBJECT defs whose CCarryableDef Active, SecondaryActive or Passive slot equals the chosen slot, mirroring PopulateCarryObjectList. On select, draw the object's Graphic mesh at dummyWorldMatrix(DummyPosName) of the creature mesh. Use DummyPosIndex when several dummies share a name. For passive slots, scale by PassiveCarrySlotScale. For two-handed items, add a second instance at SecondaryDummyPosName. Then add a 'Show particle attachers' toggle that places CParticleAttacherDef effects at their dummies with the existing placeParticle.

4) Optional hero-only clothing toggle (the App tab's CLOTHING_LIST, CTCHeroAttachableAppearanceModifiers): swap in CAppearanceModifier graphics meshes. Size it separately.

Keep it read-only with no .tng or def writes, the same as vanilla. Effort: M once the skeleton decode and preview panel exist, L including them. Value: low to medium.

### Asset banks overall. This compares what...

#### Animation browser and playback (Assets > Animations)
*gap, effort L (decoder and list S; skinned preview M; import L), value high*

**Vanilla:** CAnimationDialog (ctor 0x02929200, 43 methods) drives CTCEditorAnimationThing. Methods: LoadAnimationFromFile 0x02555e00 (a .bba file; strings FIND_BBA_BUTTON / FILE_ANIMS_BUTTON), LoadAnimation 0x02558cc0, SetAnimationFrame 0x025560b0 (the frame slider), OnPlay/Stop/PauseButtonPressed 0x02557d10/0x02557d40/0x02557da0, GetFileAnimDuration 0x02556050, GetFileAnimKeyFrameCount 0x02556070, ResetThingPosition/Orientation 0x02557e80/0x02557e20, GetRHSetAndMovementDelta 0x02556c80 (tracks root movement), AddCombinationAnim 0x02558bf0 (partial-anim layering), LoadSteppingAnimation 0x02558c60. The Thing Properties Anim tab has PlayCurrentAnim 0x0291f950, PlayCurrentCombinationAnim 0x0291fec0 and CarryObject 0x02920520. The edit modes are THING_ANIMATION=9 and EDIT_ANIMATION=19. Data: graphics.big MBANK_ALLMESHES entry types 6 (Animation, 3,272), 7 (Delta, 56) and 9 (Partial, 107), per docs/formats/INSTALLED_GAME_ASSET_REPORT.md:60-66.

**Fix:** Correct the attribution: the playback methods (LoadAnimationFromFile 0x02555e00, SetAnimationFrame 0x025560b0, GetFileAnimDuration/KeyFrameCount 0x02556050/70, GetRHSetAndMovementDelta 0x02556c80, AddCombinationAnim 0x02558bf0, LoadSteppingAnimation 0x02558c60) belong to CTCEditorAnimationThing. CAnimationDialog (0x02929200) is the UI that drives it through SetThing 0x02930bf0 and the carry-object list 0x0292ff30/0x0292f890. Anchor any disassembly in the FableTLC CAnimateThing::AddCombinationAnim at 0x70d780, which already has coverage, to learn the base-plus-partial blend rules.

Plan:
(1) S. Port EgoCore Animations/AnimParser.h to libs/forgecore/{include/forge/anim.hpp,src/anim.cpp}. It takes a graphics.big MBANK_ALLMESHES entry of type 6 (full), 7 (delta) or 9 (partial) and returns tracks: bone name, parent, samples per second, frame count, and rotation and position keys, with Evaluate(t). Add a test that decodes all 3,272+56+107 entries without error. Add the CLI `forge-tools anim list|info <graphics.big> <name>`, printing duration, keyframes and bones.
(2) M. meshpreview.cpp:172-173 currently skips the bone blocks. Keep them instead: the name CRCs, the 48-byte local matrices and the 64-byte inverse-bind matrices. Also keep the per-vertex bone indices and weights of the animated blocks (reads at :199; the layout is in EgoCore Meshes/MeshParser.h C3DBone/CAnimatedBlock). Add a Skeleton to the readLod0 result and CPU-skin it per frame.
(3) M. Add Assets > Animations as a 4th segment at textures.cpp:109. It has a searchable list grouped by prefix and a mesh picker that defaults to the selected thing's graphic. A small interactive orbit viewport reuses the gui/renderer thumbnail path. Controls: play/pause, frame slider, speed, loop, and a track-movement toggle that applies the root delta like GetRHSetAndMovementDelta. A reset-position button follows ResetThingPosition. Partial (type 9) anims layer over a base anim like AddCombinationAnim. A readout shows duration and keyframes, and a carry-object slot picker attaches a mesh to a named bone (optional).
(4) S. Add a 'Preview animation' button to the Actors property card that opens this page with the creature mesh preselected. This is editor-only, since the level format stores no animation.
(5) L, later. Export and import through glTF (EgoCore GltfExporter.h / GltfAnimImporter.h + AnimCompiler.h), appended to graphics.big through the existing forge::big writer as a bank layer. Keep the animation event DB (game_/sound_animation_events.bin) and .bncfg as separate gaps. Neither FableForge nor EgoCore parses the binary event DB (EgoCore's EventBackend reads only the text form), so that would need RE of CAnimationEventsDialog's save path.

Effort: L overall (decoder S, skinned preview M, import L). Value: high.

#### Mesh browser with 3D preview (Assets > Meshes)

**2026-09-29: browsing implemented in Assets > Models.** Lazy render-mesh index,
case-insensitive name/exact-id search, textured/wireframe orbit preview, geometry
counts and primitive layout, material texture links, helper names/bones/positions,
and object/creature/building definition references. Existing imports remain behind
Import model. Collision, helper-axis overlays, animations, export/place actions and
replacement remain open. `tests/ui/model_browser.txt` exercises the browser; the
existing scratch mesh-import test still passes.

*gap, effort M, value high*

**Vanilla:** Thing Properties main tab: 'Find BBM name' (FIND_BBM_NAME_BUTTON, GetBBMFromMeshName 0x02921bf0) maps a mesh to its source .bbm. 'Mesh From File' (*.bbm) swaps a raw .bbm onto a thing with no bank rebuild (CEnginePrimitiveAnimatingMeshFromFile ctor 0x01e0abd0). Engine survey reports triangles, vertices and texture memory per primitive (editor_engine_stats.csv). Debug draw toggles: ConsoleSetDrawRenderMeshes 0x018cd870, ...PhysicsMeshes 0x018cd990, ...CameraPhysicsMeshes 0x018cd9f0, ...NavigationMeshes 0x018cda20.

**Fix:** Add an Assets > Meshes page as a 4th segment ("Meshes") at gui/textures.cpp:109, dispatching to a new App::drawMeshBrowser. Split "Models" into Meshes (browse) and Import (the current drawModelImportCard), or keep Import as an action button on the page.

1) **Index.** Build a lazy, cached list of MBANK_ALLMESHES from graphics.big with forgecore big.hpp: id, name, BIG entry type and payload size. Resolve the path as editor.cpp:3440 does (data/graphics[/pc]/graphics.big).
   - Filter by name/id text and by type: 1/2/4/5 are meshes; 6/7/9 are animations, which are listed but greyed out until the Animations page lands.

2) **Orbit preview.**
   - Generalise Renderer::thumbnail (renderer.cpp:510) into an offscreen preview target that is not cached by key and is re-rendered each frame. It takes a yaw/pitch/distance camera and a mode flag: textured, wireframe (an RS with D3D11_FILL_WIREFRAME) or helpers overlay (a small axis gizmo per meshpreview::Helper matrix).
   - Feed it meshpreview::readLod0 â†’ the same Mesh/Image conversion editor.cpp:3450 uses, then show it with ImGui::Image and drag-to-orbit.
   - Collision-hull mode is a follow-up. First extend meshpreview with a hull decode, taking the layout from the writer in src/meshimport* and cross-checking it against EgoCore Meshes/MeshParser.h.

3) **Info panel** (all from meshpreview::Geometry):
   - Vertex and triangle counts, primitiveCount, per-primitive vertexFormat/stride, and boneCount.
   - Materials table showing each diffuse/bump/reflection/alpha texture id. Clicking an id switches to the Textures segment and sets texSelected_.
   - Helpers list with name, bone and translation; CREATEOBJECT dummies are highlighted.
   - "Used by": the game.bin defs whose graphic model id equals this mesh id, via defdecode over the object/creature defs. Build it once and cache it.

4) **Actions:**
   - "Export .glb" reuses glbwriter (as foliageexport/terrainexport do).
   - "Place in map" picks a def from "Used by" and enters the existing Add-object placement.
   - "Replace meshâ€¦" runs the meshimport pipeline targeting an existing id instead of a new MESH_<NAME>, with a one-time .forge-orig backup.

5) **Vanilla parity mapping.**
   - Draw-mode toggles correspond to ConsoleSetDrawRenderMeshes/PhysicsMeshes/CameraPhysicsMeshes/NavigationMeshes (0x018cd870/990/9f0/a20). Physics/camera-physics are the hull follow-up; navigation belongs to the map viewport, not this page.
   - "Mesh From File" (CEnginePrimitiveAnimatingMeshFromFile 0x01e0abd0) corresponds to previewing an external model file before import.
   - "Find BBM name" (0x02921bf0) is skipped because retail ships no .bbm tree.

**Formats:**
- Reads graphics.big (BIG, already parsed), textures.big (already), game.bin defs (already).
- Writes nothing, except Replace (graphics.big via meshimport).
- Animated meshes show a bind pose only. Skeleton and skin weights are not parsed (see EgoCore MeshParser.h C3DBone/CAnimatedBlock) and belong to the Animations work.

**Effort / value:** M overall; the renderer preview target and the page are ~S-M each, hull decode is S-M. Value high.

#### Sound browser and preview (Assets > Sounds)
*gap, effort M, value high*

**Vanilla:** Console: PlaySound 0x018d2a70, PlaySoundFromBank 0x018d2aa0, PlaySoundCriteria 0x018c4660, ReloadSoundBanks 0x018bf2d0, UnloadSoundBanks 0x018bf2c0, SetSoundGain 0x018bf290. Thing Properties has a PLAY_SPEECH button (PlayCurrentSpeech 0x029203b0). Survey > Sounds paints SOUND_THEME defs (EditSetSounds 0x02971010). Data: data/Sound/*.lug (50) plus .met (50), music .ogg (66), lang/English/Dialogue*.lut and ScriptDialogue*.lut (4 files, about 672 MB), and name tables data/Defs/gamesnds.bin, dialoguesnds.bin and scriptdialoguesnds.bin.

**Fix:** Add an Assets > Sounds page (effort M, value high) in these steps.

(1) Add libs/forgecore audio.hpp/.cpp.

.lut reader (Part A of FableTLC docs/formats/AUDIO.md):
- 44-byte header with magic LiOnHeAdLHAudioBankCompData.
- Clip records start at 0x2C.
- The tail directory at TocOffset is a flat table, not a hash.
- Each clip is a RIFF/WAVE with fmt 0x0069.
- Port it from tools/parse_lut.py and EgoCore's .lut handling.

.lug and .met readers:
- .lug is LiOnHeAd plus an LHFileSegmentBankInfo block stream, using the name[32]+size block convention. .met is the sidecar.
- Ingame.lug carries an event-tag map.
- Port them from EgoCore Audio/LugParser.h and MetParser.h, cross-checked against tools/parse_lug.py.

(2) Xbox IMA ADPCM (0x0069) to PCM16 decoding is implemented in `forge/xboxadpcm.hpp`; fixed mono/stereo fixtures and retail samples from all four dialogue banks match FableTLC's independent Python decoder by sample count and PCM CRC32. `File::wavPcm16` wraps a selected clip in standard PCM WAV. For the .ogg music, vendor stb_vorbis.

(3) Vendor miniaudio, as EgoCore AudioBackend.h does, and feed it decoded PCM buffers with play, stop and loop controls.

(4) Names:
- For .lut dialogue, join clip Index to the dialogue.big/text.big entries. Use the existing textbig.hpp speechBank field and the pairing proven in AUDIO.md A.5, so each line shows its subtitle text.
- For .lug SFX, use the in-bank sample names, plus data/Defs/gamesnds.bin (and dialoguesnds.bin / scriptdialoguesnds.bin) decoded through the existing defdecode path.

(5) UI in gui/textures.cpp, as a fourth tab beside Textures / Models / Ground themes (textures.cpp:109):
- Bank list: 50 .lug files, 4 .lut files per language, and the loose .ogg files.
- Search by name or subtitle.
- Play, stop and Export WAV buttons.
- A duration and format column.
- In the SOUND_THEME card (gui/editor.cpp:2644-2677), add a play button that resolves the theme def's sample names to .lug entries and plays them. This mirrors vanilla EditSetSounds and PlaySoundFromBank.
- In Thing properties, add a 'Play speech' button for a speaking NPC, mirroring PlayCurrentSpeech 0x029203b0.

(6) Phase 2 (effort M): replace and add samples.
- Encode WAV to 0x0069 mono 22050 Hz, block 36, using EgoCore XboxAdpcmEncoder or xbadpcm.py.
- Repack using the lut_write.py semantics: identity gate, same-size replace, and --allow-resize full repack.
- Do the same for .lug via lug_build.py.
- Byte-exact identity round-trip tests go in tests/.

Out of scope: live in-engine ReloadSoundBanks, because FableForge does not host the running engine.

#### Def field editor in the GUI (Assets > Definitions)
*gap, effort M, value high*

**Vanilla:** Dev defs are text: 198 .def and .tpl files in Data/Defs of the dev tree. The editor build can recompile them live: CDefinitionManager::RecompileDefType 0x0305b560, RecompileAllDefs 0x0305b5f0, ClearDefsForRecompile 0x0305f440, ConsoleResetDefinitions 0x018cdb70, ConsoleResetScriptDefinitions 0x018cdc30, CMainGameComponent::ValidateDefinitions 0x018a3ffa. Thing Properties can jump to a thing's def. Retail ships only CompiledDefs (game.bin, names.bin, script.bin, frontend.bin).

**Fix:** Add a fourth Assets segment, "Definitions", beside the three at gui/textures.cpp:109, handled by a new drawDefinitionsCard (gui/defs.cpp) dispatched like drawModelImportCard.

(1) Left pane: a tree of definitions grouped by type (OBJECT, BUILDING, CREATURE, and so on), with GroupDef (THING_GROUP) subgroups, from ctx_.groupedDefinitions. Reuse drawDefPalette's search box. Include a bank switch for game.bin, frontend.bin and script.bin.

(2) Right pane: decode the entry with defdecode against its defschema DefType, then draw one widget per field:
- int32, uint32, float and bool become numeric inputs and a checkbox.
- C2DVector and C3DVector become DragFloat2 and DragFloat3.
- CRGBColour becomes a ColorEdit4.
- CWideString and CCharString become text inputs.
- CDefString becomes a text-ref picker.
- Fields known to hold def references (by schema name or type) become a def combo from ctx_.definitions.
- Graphic and mesh ids get a "show in Models" link. Texture ids get a thumbnail and "open in Textures", reusing texRows_.
- Types defedit::setField rejects are shown read-only with their hex bytes, and marked "not editable yet". The editor must not guess an encoding for them.

(3) Edits commit through defedit::setField on an in-memory bin::File. "Save" writes via the existing backup / mod-pack path, never straight into retail CompiledDefs. "Revert" and "Diff vs vanilla" reuse forge-tools defs diff logic.

(4) "Clone as new def" lifts the donor-copy code out of the mesh-import, theme-add and questcard paths into one forgecore function, defedit::cloneEntry(name, newName), and uses it here.

(5) A pre-save check, like ValidateDefinitions at FableWin 0x018a3ffa: every def-reference field must resolve to an existing entry name or index. List the failures and block the save.

(6) Add an "Open definition" button to the Objects property card (gui/editor.cpp drawPropertyGrid). It jumps to Assets > Definitions with the thing's def selected.

(7) The engine cannot recompile defs live. Vanilla RecompileDefType 0x0305b560 and RecompileAllDefs 0x0305b5f0 depend on a dev text Data/Defs tree and editor-only code. Say this in the UI. After save, offer livelink sendReload (src/livelink.hpp:29) to re-stream the region, and note that some defs only take effect after a restart.

(8) Optionally, for text .def mods (EgoCore pack type), add a "Compile text defs" action that runs the existing egocore runDefc.

Formats: compiled .bin defs only. FableForge already reads and writes them (defdecode, defedit), so no new RE is needed. Extending setField to arrays and containers is follow-up work (L).

Effort M; value high.

#### Text and string editor in the GUI (Assets > Text)
*gap, effort S-M, value high*

**Vanilla:** The dev console has WriteAllTextToFile 0x018d91d0, which dumps all strings. PlayCurrentSpeech 0x029203b0 plays a thing's text line. The runtime reports 'No lipsync bank entry for speech index' when a line lacks lipsync. The strings live in lang/English/text.big (28,913 entries: type 0 string, 1 group, 2 narrator).

**Fix:** Phase 1 (S-M, high value): add a 4th segment "Text" to theme::segmented at gui/textures.cpp:109 with a drawTextPanel().
- Load <install>/data/lang/<Language>/text.big through the forgecore big reader and textbig::decode. Cache the rows as id, name (TEXT_*), type (0 string, 1 group, 2 narrator), content and tags.
- Add a searchable virtualised list (ImGuiListClipper, about 29k rows) that filters by name, id or content, with a language picker.
- Add a detail card:
  - content as an editable multiline field
  - Tag{position,name} as chips (ANIM:, CAM:, moods), editable
  - speaker, speech bank and identifier fields
  - for type 1 groups, member ids as clickable links
  - type 2 read-only until the encoder covers it
- Add "New string from donor" that calls textbig::upsertString(file, name, entry, requestedId, donor), the same path as the CLI `text set --donor` (forge-cli main.cpp:7885-7910). Keep the CLI's read-back verification.
- Write in one of two ways: back up text.big and write it in place, or send the edit to the active mod pack's text layer (the same key/value shape as main.cpp:8304 p.text[key]) so Deploy merges it.
- Add an "Export all" button that dumps every string to TSV. This is the equivalent of vanilla NGlobalConsole::WriteAllTextToFile at 0x018d91d0.
- In the property grid, add "Edit text" jump links on TextTag-typed fields (CTCActionUseReadable, CTCInfoDisplay, quest-card text) that open this page on the entry.

Phase 2 (M, med value): "Play line". This mirrors CThingPropertyDialog::PlayCurrentSpeech at 0x029203b0.
- Port a speech-bank reader from EgoCore Audio/LugParser.h and MetParser.h, resolving the entry's SpeechBank/Identifier to a sample.
- Assets > Dialogue now plays a chosen decoded .lut clip through the Windows waveform output device; keep the native PlayCurrentSpeech jump from a selected thing on the backlog.
- Flag lines with no lipsync entry in dialogue.big, like the runtime's "No lipsync bank entry for speech index" warning. EgoCore Lipsync/* has the parser.

The four dialogue .lut banks have a lazy read/index path and all 20,214 English
clip IDs join a nonempty lipsync entry in the paired sub-bank. .lug/.met and
Assets > Dialogue now plays a selected Xbox ADPCM line and shows its paired
lip sync and resolves the linked subtitle through `*snds.bin` and `text.big`.
The planned Sounds/Text pages still need indexed browsing,
editing and a jump from the selected thing.

#### Model import is static-only and cannot replace an existing mesh
*weakness, effort L, value med*

**Vanilla:** Vanilla previews an arbitrary .bbm on any thing, animated meshes included (CEnginePrimitiveAnimatingMeshFromFile), so artists iterate without rebuilding banks.

**Fix:** 1) **Replace in place (effort M, value high).** Change meshimport so it can overwrite an existing entry.
- Add `ImportRequest::replaceMeshId` (or `replaceName`) and a new `forge mesh-replace <model> <MESH_NAME|id> [--keep-physics|--new-hull] [--install]`.
- Find the MBANK_ALLMESHES entry by name or id. Recompose it with meshcompose, keeping the same id, name and entry type.
- Rebuild its Info blob:
  - Keep the original PhysicsIndex unless `--new-hull` is given. A new hull overwrites the referenced type-3 entry in place.
  - Recompute bounds and SafeBoundingRadius.
  - Set LODCount=1 and LODSizes from the new payload.
  - Keep the texture ids unless a new texture is given.
- Refuse this path for type 2/4/5 (animated) entries until step 2 exists.
- Reuse the existing backupOnce, the gameRunningIn guard and the baseRootâ†’outRoot modpack recipe path, so it works as a forge_pack.json step.
- Leave the defs alone: every OBJECT whose Graphic.modelId points at the id picks up the new mesh.
- GUI: a "Replaceâ€¦" button on the Models page, with the mesh chosen from a searchable MESH_ list. Show a before/after preview with meshpreview readLod0 in the existing viewport.
- Verify: readLod0 round-trip in tests/test_export.cpp, then one in-game probe via tools/ingame/release_probes.py (replace MESH_BARREL-type prop).

2) **Skinned or animated import (effort L, value med).** This is needed for retail animations to keep working on a remodelled creature or hero.
- Port EgoCore Meshes/MeshCompiler.h' animated path (C3DAnimatedBlock and C3DBone table, bone indices and weights per vertex, C3DGroup2 groups) and GltfMeshImporter.h' JOINTS_0/WEIGHTS_0 read into `forge::meshcompose::composeAnimated` (entry type 2).
- The bind skeleton must be the donor retail mesh's bone table: same bone names, order, parents and bind matrices. Retail anims (types 6/7/9) address bones by index or name. Map glTF joints to donor bones by name and reject any unmatched joints.
- FableForge's meshpreview currently only counts bones. It first needs a C3DBone and skin-weight parser (port it from EgoCore MeshParser.h) to read the donor skeleton, and that parser also serves the Animations page.
- Replace-in-place for type 2 then reuses step 1.
- This pairs with the separate Assets > Animations gap: preview the retail anim bank (graphics.big types 6/7/9, EgoCore AnimParser.h) on the imported skinned mesh before deploying.

3) **.bbm input (effort M, value low-med).** Port EgoCore Meshes/BBMParser.h into forgecore as a `forge::bbm` reader that turns a .bbm into meshcompose Primitives (and bones and weights once step 2 exists).
- Accept .bbm in mesh-import, mesh-replace and the Models page file picker.
- Add a "Preview .bbm file" action that loads a loose .bbm into the viewport without writing any bank, the equivalent of vanilla's CEnginePrimitiveAnimatingMeshFromFile (0x01E0ABD0) and CThingPropertyDialog::GetBBMFromMeshName (0x02921BF0).

Skip LOD authoring. Retail-compatible LODCount=1 plus the ghost LOD already works in-game.

#### Lipsync for new or edited voice lines
*gap, effort L, value med*

**Vanilla:** The runtime looks up a lipsync entry per speech index ('No lipsync bank entry for speech index'). The retail English dialogue.big contains 20,214 nonempty type-1 LIPSYNC entries across four banks (plus 291 empty/non-type-1 records), measured by the Forge and FableTLC roundtrip readers on 2026-09-30.

**Fix:** 1. `forge/lipsync.hpp` and `lipsync.cpp` now parse and write the grammar documented in FableTLC tools/lipsync_build.py:
   - u32 visemeCount, then {u8 id; cstr mnemonic} per viseme
   - u32 fps (43)
   - u32 frameCount, then per frame: u8 keyCount, then {u8 visemeId; u8 weight} per key
   - Info block: f32 duration, where frameCount == ceil(duration * fps)
   - `fableforge_lipsync_tests` checks a fixed byte fixture and malformed boundaries under CTest. Against the installed English dialogue.big it re-encodes all 20,214 nonempty type-1 entries byte-exact, including their four-byte Info duration. `tools/check_all.py` runs this retail pass when the bank is available. FableTLC's independent Python reader reports the same 20,214/20,214 result. An exact-sub-bank upsert now edits or adds a type-1 entry in memory and round-trips through the BIG writer. A scratch retail rebuild preserved 20,504 untouched records and the independent Python reader parsed all 20,215 resulting nonempty entries byte-exact. Assets > Dialogue exports scratch archives and stores selected lines as Forge pack recipes; wire this API into the planned Sounds page too.
2. Port EgoCore Lipsync/SpeechAnalyzer.h (LoadWav and AnalyzeWav into CLipSyncData) as forgecore/speechanalyze.hpp. This generates viseme curves from a 22050 Hz WAV.
3. The `forge/lut.hpp` read path now indexes clips by their exact paired-bank Index. Retail English validates 20,214/20,214 audio/lipsync IDs, with maximum duration delta under 0.000233 s. `File::pcm16(index)` decodes a selected Xbox ADPCM clip and matches the independent FableTLC decoder on samples from all four banks; `wavPcm16` exports PCM WAV bytes. Assets > Dialogue plays that PCM through a Windows waveform device and follows its sample position. Add the write path using FableTLC tools/dialogue_pipeline.py / xbadpcm.py; .lug/.met support remains separate for effects and ambient sounds.
   Bind audio lookup to the selected language and paired .lut bank as well as Sound ID. EgoCore's September 2026 `EnsureDialogueAudioLoaded` caches by Sound ID alone and falls back across all four dialogue .lut names and every installed language; that can preview a different line when IDs overlap or the selected language is missing. A failed lookup should show "audio unavailable" while keeping the viseme preview usable.
4. Add a "New voiced line" flow to the planned Sounds/Text pages:
   - Append the WAV to the .lut and take the next Index N.
   - Add the TEXT_ entry with a speech ref to that bank and index, through textbig.hpp.
   - Add LIPSYNC entry Dialogue_<N> to the paired sub-bank. For example, Dialogue.lut pairs with LIPSYNC_ENGLISH_MAIN. Forge pack recipes now compose these entries by language, bank and Sound ID in load order.
   - Let the user edit the generated curves.
   - Emit the audio and text files as pack overrides and the lip sync entry as a record recipe, never over retail.
5. Preview after the codec, audio lookup, skeleton and animation readers. EgoCore's 2026-09-30 `300f949` commit is a working reference (`EgoCore/Lipsync/LipSyncProperties.h`, `UpdateLipSyncBones` and `DrawLipSyncProperties`): a resizable head viewport beside editable frames; play/pause, stop, loop, mute and a scrubber; five retail head presets (bandit lieutenant, female/male villager, Demon Door, male child); frame-to-frame viseme interpolation; and a closed-mouth MM pose for the unused weight. Forge has `lipsync::sample` for interpolation and resting weight, checked with two-frame and silent fixtures. Assets > Dialogue has a scrubbed viseme timeline with exact language/bank/ID selection, exercised by a retail GUI test. `lipsync_preset.hpp` resolves and audits all five head presets against retail graphics.big, including the Demon Door's AH-to-AI and SZ-to-ST mappings; the UI shows missing assets without losing the timeline. `meshpreview::Geometry` decodes their skeletons and per-vertex skin weights, independently matched across all five retail heads. `forge/animation.hpp` decodes 3DAF/XSEQ pools and palettes; 30 phoneme assets match FableTLC by field CRC and all 3,435 retail animations parse with matching track totals. `forge/headpose.hpp` blends first-frame phoneme tracks over the bind skeleton and skins vertices and normals on the CPU. The Dialogue viewport renders the textured head in its own orbitable target above the viseme timeline; the audio device clock drives both. All five retail heads load in the GUI script, and the pose test checks neutral identity, first-phoneme movement and framing of all 30 retail phoneme tracks across the five heads. The page now edits byte weights and frame/key rows, stages edits across Sound IDs and banks, and exports a new verified `dialogue.big` without overwriting existing files. `tools/test_dialogue_edit.py` proves a two-bank staged export against retail English and checks pack authoring. `fableforge_lipsync_pack_tests` checks two-pack record composition. Audio-bank writing remains a follow-up; subtitle lookup now joins 20,088 English voiced strings through the bank-specific `*snds.bin` tables, with a retail Demon Door check.

   The retail dialogue edit harness invokes `forge-tools mods build` with two differing record recipes and with a recipe pack plus a whole-file archive pack in both orders. It verifies the winning weight/frame count, same-line conflict report and skipped-recipe report; the GUI test opens Mods > Check conflicts and finds the lip sync row.

   Playback parity matters: derive the preview time from the audio player's actual position while audio is playing, then use that time for pose and frame highlighting. Forge's Dialogue browser now does this with `waveOutGetPosition`, and its transport seeks in audio sample time. EgoCore currently advances the pose with UI `DeltaTime` and seeks audio using `lipSyncTime / lipSyncDuration`; those can drift when the clip duration differs from the lipsync Info duration. Validate silence, a partially weighted frame, missing MM pose, a missing phoneme animation, a different-length WAV, loop boundaries and each head preset against retail data.

   Source: https://github.com/eeeeeAeoN/EgoCore/blob/300f949/EgoCore/Lipsync/LipSyncProperties.h (reviewed 2026-09-30). This source is a behavior reference; Forge's own codecs and renderer need retail roundtrip and image checks.

Effort L. Value med.

#### Animation event DB editor
*gap, effort M, value med*

**Vanilla:** CAnimationEventsDialog (ctor 0x028b2e60, SetThing 0x028b4d20). It inserts game, sound and stop events at a time on the current animation and saves to the DB. Strings: 'Save Animation Events', 'Save changes to the animation event database?', 'Unable to save animation event file - is it checked out?'. At load the engine prints 'Loading Game Animation Events' and 'Loading Sound Animation Events'. Files: data/Misc/game_animation_events.bin and sound_animation_events.bin. The retail copies are identical to the dev tree's (game_animation_events.bin is 158 B). Its header reads u32 8, u32 1197, u32 2, u32 17...; this looks like anim id, event count, event index and time, but that is my guess and is not verified. Event-name enums are in data/Defs/game_animation_events.h and sound_animation_events.h (retail). The dev tree also has the .txt source (BEGIN_ANIMATION_EVENTS / BEGIN_EVENTS: <ANIM> / <EVENT> <time 0..1> START / END_EVENTS) at D:\tmp\fablewin_editor\...\Data\Misc.

**Fix:** 1. Codec: add libs/forgecore/animevents.{hpp,cpp}.
   - Binary layout: struct AnimEventDB { vector<Anim{u32 animId; vector<Event{u32 type; u16 flag /*0=START, 0x100=STOP*/; float time;}>}> }.
   - Reader and writer, packed little-endian, with animations sorted by animId on write.
   - Text reader and writer for the dev .txt grammar: BEGIN_ANIMATION_EVENTS / BEGIN_EVENTS: <ANIM> / <EVENT> <time> START|STOP / END_EVENTS / END_ANIMATION_EVENTS. Keep the writer's 'BEGIN_EVENTS: ' trailing space as FableWin writes it.
   - Event-name enums: parse the EGameAnimationEvent and sound enums from Data/Defs/game_animation_events.h and sound_animation_events.h. FableForge's defc header handling already copies these files and can reuse them.
   - Test: .bin -> decode -> encode must rebuild both FableWin-written files byte-exact (158 B and 42850 B, dev tree D:\tmp\fablewin_editor\...\Data\Misc).

2. Anim-name resolution:
   - Resolve animId <-> ANIM_* name. First confirm the source: it is most likely the graphics.big entry id, via big.hpp, since animations are graphics.big types 6/7/9. Check that 1197 resolves to ANIM_BANDIT_ATTACK_LEFT_1.
   - With that mapping, txt -> bin compilation also works. Match FableWin's drop and dedupe behaviour, found by diffing txt against bin: 9 -> 8 animations in the game file, 3105 -> 3095 events in the sound file. If the rule is unclear, disassemble FableWin's writer near the 'Save Animation Events' xrefs.

3. CLI:
   - forge anim-events [game|sound] [<ANIM>]: list the events.
   - forge anim-events-set <game|sound> <ANIM> <EVENT> <time> [--stop] and anim-events-rm: edit events, writing Data/Misc/*_animation_events.bin with the existing one-time backup.

4. GUI, under Assets > Animations (the page the other animation gaps need):
   - A per-animation timeline strip showing game and sound events as markers, with add, drag-time and delete, plus an event-type combo filled from the enums. Before animation playback (types 6/7/9) exists, it can ship as a list/table editor.
   - Where it can, mirror CAnimationEventsDialog (ctor 0x028b2e60, SetThing 0x028b4d20): insert game, sound and stop events at the current frame on the selected thing's animation.

5. Mods:
   - Register both .bin files (and optionally the .txt) as a modpack file layer.
   - Merge per animId at event granularity. This matches EgoCore ModManagerCompiler.h:1231, which concatenates BEGIN_EVENTS blocks, so it stays compatible with EgoCore mods that ship the .txt.
   - Recompile to .bin on deploy.

#### Bone scaling presets (.bncfg)
*gap, effort M (after skinning), value med*

**Vanilla:** CBoneDialog ('Bone editing tool': X/Y/Z factor, locks, groups, save/load *.bncfg), CEditInputProcessBone (ctor 0x0202e330), edit mode EDIT_BONES=11. The Thing Properties App tab has 'edit bones' and ToggleClothing 0x0291f3d0. Retail data/Bones holds 60 text .bncfg files, e.g. bandit_short.bncfg: 'Creature_type: CREATURE_BANDIT_GRUNT;', #Start_group_settings with 'upperarm: "Bip01 L UpperArm", "Bip01 R UpperArm";', and #Start_Bone_data with 'Bip01 L Calf: 0.9, 1.0, 1.0;'.

**Fix:** Phase 1 (S, value medium-high, ships now, no skinning needed):
1. Add libs/forgecore/bncfg.hpp/.cpp with a lossless text parser and writer. Model it as:
   struct Bncfg { std::string creatureType; std::vector<Group{name, std::vector<std::string> bones}>; std::vector<BoneScale{name, float x, y, z}>; }
   - Keep bone order.
   - Write floats the way the retail files do (1.549999 style) so unedited files round-trip byte-for-byte.
   - Test by round-tripping all 60 files in the FableWin anniversary tree (D:\tmp\fablewin_editor\...\Fable\Data\Bones).
   - Before finalizing the syntax, decompile retail PreloadBoneConfigs at 0x006c37d0. Its parse helpers include RBTreeMap_InsertOrFindString at 0x006c3410. Check four things: how comments and whitespace are handled, whether Creature_type is the lookup key (one file per creature type, or several variants per type chosen by the def), what an unlisted bone falls back to (assumed 1.0), and whether group lines are editor-only or also read at runtime.
2. Add a CLI command, `forge bones list|show|set|new <file>`, following the defedit set-field pattern.
3. Add an Assets > Bones page next to Textures / Models / Ground themes. Tab switch at gui/textures.cpp:109 (drawTexturesPanel starts at :104).
   - Left side: a list of .bncfg files grouped by Creature_type, validated against creature defs through defdecode.
   - Right side: a table of per-bone X/Y/Z drag-floats, plus lock-axes toggles that make X=Y=Z (the vanilla dialog's 'locks').
   - A group selector that applies an edit to every bone in that group (the vanilla dialog's 'groups').
   - Buttons: add or remove a bone row, and 'New variant' (copy to a new file).
   - Save to a loose Data/Bones/*.bncfg, writing a .bak first and registering the file in the mod-pack/install manifest, as other loose-file installs do.
   - Validate bone names against the skeleton's bone names. This needs one small addition to meshpreview: read the bone name strings, which EgoCore Meshes/MeshParser.h C3DBone covers. Bone counting alone is not enough. Show unknown names as warnings.

Phase 2 (M, after skeleton and skin decoding from the animation work):
- Preview the scales live on the creature's skinned mesh in the existing renderer. Apply each scale per bone in the bone's local frame, the way CEditInputProcessBone does (0x02958ab0 ProcessInput, 0x0202e330 ctor).
- Match vanilla edit mode EDIT_BONES=11: pick a bone in the viewport and drag to scale it.
- Optional: an 'Edit bones' action on a selected creature thing in the level editor, mirroring the Thing Properties 'edit bones' button (CThingPropertyDialog 0x0290d830 is built with a CBoneDialog*), that opens the Bones page for that creature's type.

Data read and written:
- Data/Bones/*.bncfg: text; FableForge, EgoCore and FableTLC have no parser today.
- Creature defs: FableForge can already decode them.
- graphics.big mesh skeleton: FableForge counts bones only; EgoCore parses the names.

Nothing touches binary banks, so the risk is low, and the output takes effect in retail through PreloadBoneConfigs.

#### Textures page covers only textures.big, as a list with no thumbnails
*weakness, effort S, value med*

**Vanilla:** No texture browser exists in vanilla; the only related feature is the texture-memory survey. The bar here is set by EgoCore's Textures/* and Banks/BankExplorer.h, not by vanilla.

**Fix:** 1) Bank file picker (S). Add a "Bank file" combo with textures.big and frontend.big. Base it on texturesBigPath(): make it a function of the chosen file, still preferring the save root, and keep one texRows_ per file. listTextures() and decodeTexture() already accept any BIG path. For writes, add a `bigRelPath` parameter to texbrowse::replaceTexture() and addTexture() (src/texturebrowse.hpp:39-49). The default stays data/graphics/pc/textures.big. Keep the .forge-orig backup and the refuse-while-running guard. Check the frontend.big entry contract with the same forgecore texturewrite validator before enabling Replace there.
2) Thumbnail grid (M). Add a List/Grid toggle. The grid uses ImGuiListClipper over rows of N cells, and only visible cells are decoded. A new renderer_.thumb(key=(file,id)) resamples mip0 down to about 96px, keeps an LRU of about 512 SRVs, and decodes on a worker thread. swatch() is keyed only by id and never evicts, so it cannot be reused directly.
3) Preview toggles (S). Add RGB/A/RGBA channel buttons: swizzle alpha into gray on the CPU, and draw RGBA on a checkerboard. Add a mip slider that decodes mip k. This needs a decodeMip(k) next to forge::terraintex::decodeMip0, walking the per-mip sizes that the subheader already carries (mips are counted at texturebrowse.cpp:104).
4) Frames (S-M). Where the entry sub-header frameCount > 1 (the sprite sheets and sequences that terraintex already reads frameHeight for), add a frame stepper or play button that decodes frame k at offset k*frameBytes.
5) "Use in UI elementâ€¦" (S). Selecting a texture opens a CUIDef picker (search game.bin CUIDef names plus a state index). It calls the forge::uidef::setStateGraphicIndex path already used by uiSetGraphic (main.cpp:7113), moved into a shared lib function, and writes CompiledDefs to the out-root. Describe this as editing game.bin, not frontend.big.
Every format involved is already parsed: forgecore BIG, terraintex, the DXT1/DXT3/ARGB decoders, and uidef/defschema. No new format RE is needed. Value: medium (UI and quest-card art modding).

#### Asset reference graph / where-used
*weakness, effort S-M, value med*

**Vanilla:** 'Find BBM name' (0x02921bf0), and Things palette Quick Find / 'Find selected', which jumps to a thing's def. Engine survey gives a 'Detailed textures summary' and per-primitive stats saved to editor_engine_stats.csv.

**Fix:** Treat this as an improvement beyond vanilla (vanilla has no where-used). Effort S-M, value med. It is most useful as a guard on Replace texture and on def and mesh edits.
1) Add libs/forgecore assetrefs.{hpp,cpp} with `struct AssetRefIndex { map<uint32_t texId, vector<Ref>> texUsers; map<uint32_t meshId, vector<Ref>> meshUsers; map<string def, vector<LevelRef>> defPlacements; }`, where Ref = {kind: Def|Mesh|Theme|Level, name, detail}. Build it lazily on a background thread after the context loads, and cache it keyed by the mtimes of graphics.big, textures.big and game.bin (or the defs bin), plus the level list.
   - Theme to texture: reuse ThemeLibrary::referencedTextures, extended to keep the theme name and whether the id is a base or cliff layer.
   - Def to mesh: iterate the compiled defs (forge::bin) and call Context::graphicModelId (src/terrainexport.cpp:472) for each Graphic-bearing def. Also record secondary model fields such as LOD and damaged models when defschema names them.
   - Mesh to texture: for each MBANK_ALLMESHES entry of types 1/2/4/5, use meshpreview readLod0 materials (diffuse, bump and reflection ids). Animations (types 6/7/9) have no texture refs, so skip them.
   - Level to def: walk each level's .tng through the existing thingsexport/tng reader and count placements per def.
   - Also index effects.big by name: meshes refer to effects by name (src/effects.hpp:16), so an effect's "used by" can come from mesh names.
2) GUI:
   - Add a collapsible "Used by (N)" card on Assets > Textures under the selected texture (textures.cpp around the #texsel card), listing themes, meshes, and defs through meshes. Clicking a row jumps to that def or theme.
   - Put the same card on the Models tab and on the definition inspector, and add a "placed in: level x count" list for defs.
   - Change the Replace hint so it states how many assets it affects ("affects 14 meshes, 37 defs, 3 themes") and ask for confirmation above a threshold.
3) Mods / install health: add "custom assets nothing references". These are textures.big or graphics.big entries past the retail id range, or added by a mod pack, with no user in the index. Also add "defs whose Graphic points at a missing mesh id". Expose both as `forge assets refs <id|name>` and `forge assets unused [--custom-only]` next to the existing `texture free-slots`.
4) Formats: every input already parses (big.hpp, bin.hpp/defdecode, meshpreview readLod0, terraintex ThemeLibrary, tng). No new RE is needed and EgoCore is not needed. Once animations (types 6/7/9) and the animation event DB are parsed, the same index can gain anim to def edges (ANIMATION_* fields in defs) as a follow-up.

#### Particle effects are read-only
*weakness, effort M, value low*

2026-09-29: the Assets > Effects inspector described in (1) below is implemented
and UI-tested, including name/id search, sprites and texture links, mesh links,
light details and partial-decode status. Install-root isolation and owning decoded
effect handles cover switching/refresh without stale readers. Selected-effect
sprite playback in (2) now supports deterministic normal emission/motion, sprite
animation, Play/Pause/Restart/Step and a dedicated orbitable viewport. Unsupported
active components are reported. Bounded mesh playback now shares populations with
sprites and renders textured geometry with size/tint fades; FL10 WARP and native
mesh-only pause/restart/pixel checks pass. Authored bounds and fixed XYZ/axis
rotation are supported; random initial and spin-axis rotations now use a
separate deterministic preview RNG. Direction/game orientation remains
unsupported. Light rendering, placed-emitter animation
and editing/writing in (4) remain open. This is a scoped implementation informed
by EgoCore, not full engine or upstream feature parity.

**Vanilla:** ConsoleDumpParticleInfo exists in the console. Things get particles through the CTC property tabs.

**Fix:** Add an Assets > Effects page next to Textures, Models and Ground themes in gui/textures.cpp:109. Reuse the effects::entryNames() and byName() search list from editor.cpp:3316-3334. The page has three parts.

(1) An inspector (effort S). For each Effect, show displayName, the systems count and a parsedFully warning. Then show one row per SpriteSystem: the sprite id (a textures.big GBANK_MAIN_PC id, with a thumbnail from the existing texturebrowse decoder), start colour RGBA, start and end size, blendMode (3 = additive), perSecond, lifeSecs, offset and single. Show one row per MeshSystem (mesh id, which opens in the Models preview via meshpreview readLod0; size; colour) and one per LightSystem (colour, radius). Everything the page shows is already parsed by effects.hpp, so this is equivalent to ConsoleDumpParticleInfo.

(2) A CPU billboard preview in the existing renderer (effort M). Spawn perSecond particles for each sprite system at its offset. Each particle lives lifeSecs and draws a camera-facing quad textured with the decoded sprite. Lerp size from startSize to endSize, fade alpha, and blend additively when blendMode == 3. Draw mesh systems as the LOD0 mesh and light systems as a tinted glow. Reuse the same sim for the placed-emitter proxy in the level viewport (replacing thingsexport's static tint), which approximates vanilla's EditorDrawPreparePrimitivesForRendering. Label it an approximation: velocity, orbit and morph fields are not parsed, and the real effect needs the game.

(3) Optional "Spawn in game" (effort S if the ForgeFSE sidecar is present). Send ConsoleCreateParticleOnCamera(<name>) through the FSE console bridge for a live view, matching vanilla. The user coordinates game launches.

(4) Later: clone and edit (effort L). Round-trip entries with EgoCore Particles/ParticleParser.h and ParticleCompiler.h (C:\Users\Cornelio\Documents\EgoCoreInspect\EgoCore-master\EgoCore\Particles). Oracle the layout against FableWin's CParticleEmitter::WriteBinary (0x2f0dbb0) and the CPSC*::WriteBinary family, and write through the existing forgecore BIG writer into a mod-pack copy of effects.big, never the install.

Format status: FableForge reads effects.big partially and read-only (emitter, sprite, mesh, light and offset fields; unknown components stop the walk). EgoCore has a full parser and compiler. FableTLC has tools/parse_effects.py. Nothing in FableForge writes the format today.
