# Feature depth audit — started 2026-09-30

This is the working checklist for reviewing Forge's existing features as complete
user workflows. The inventory comes from `docs/EDITOR.md`, `docs/ROADMAP_1.0.md`,
the active GUI, and the 2026-09-28 UI review. A passing unit or UI script proves
only the behavior it exercises; every row still needs interaction, visual,
round-trip, undo and failure-path review where those apply. Mark a row verified
only after checking those paths against the current build.

| Area | First depth question | Current evidence / finding | Next verification |
| --- | --- | --- | --- |
| Setup, game discovery, backups, restore | Can a new user find the install, understand writes, and recover a bad edit? | CLI backup manager and Setup restore pass on isolated roots. The GUI reloads an open map after restore; labels describe the backed-up bytes and loose draft removal. A missing path and 13-backup restore are visually checked at 800x600 / 1.5 scale. Changing folders protects unsaved work and clears the previous map, terrain and context. | Walk through write failure in the GUI; verify restore after a real modded-bank write. |
| Map browser and viewport | Can a user find, inspect and navigate a map at common window sizes? | Eight requested size/scale tours pass; 3840×2160 is clamped by the current monitor. At 800x600 / 1.5, all panes now fit, map rows reveal full names on hover, and compact View/Show/Frame and tool-mode menus remain usable. | Recheck zoom, selection and true 4K when available. |
| Placed objects, ownership and links | Do placement, transforms, duplication, links and deletion stay correct through undo and game save? | Core ownership/locked tests and several retail UI scripts cover individual paths. Terrain-following now carries owned descendants with a grounded parent in the terrain edit's undo step. | Review object families on a real map, linked-copy edge cases, and saved game behavior. |
| Ground sculpt, paint, copy and Fit/Generate | Does what the preview shows match the terrain and objects the game receives? | Fractal height range is explicit; Fit now reports affected vertices and largest height shift. Fit preview returns to zero changes after Apply, back to pending changes after Undo, and zero after Redo on Greatwood_Filler_04. Grounded things and foliage follow height edits. | Inspect remaining brush modes, Fit/Generate seams and game output on representative maps. |
| Foliage and scenery | Do baked trees/grass remain seated and visible before and after terrain deployment? | 2,514 StartOakValeWest foliage instances were re-seated in the preview after a raised-ground stroke; undo restored zero offsets. Terrain-triggered foliage refresh avoids re-decoding placed things. | Compare near trees in the preview and game after deployment; test maps with missing/partial STB foliage. |
| World view and new levels | Are map placement, seams, neighbour preview and level creation understandable and recoverable? | Scratch copied/blank creation, region entrances, minimap framing, compact actual creation and byte-exact restore pass. Existing edits and edits made during creation are preserved; restoring a removed map clears its preview. | Review pack/loose creation, world movement/seams and new game output. |
| Assets: themes, models, effects, dialogue | Can assets be found, previewed accurately and applied without ambiguous state? | Effect background presets and the 1280x720 / 1.5 scale preview fit; effect transport now carries elapsed time through loops. Dialogue's Load line stays with Sound ID, and the head/timeline fit at 1280x720 and 1024x600 / 1.5 scale. Retail head pose checks pass. | Review each browser from search through preview and export; compare eye attachment with a live retail capture. |
| Mods and content packs | Can users combine common TLC mods, see conflicts and undo installs? | Mod-pack and Freeroam/Aeon research exists; integration outcomes vary by install order. | Exercise representative local corpus packs in an isolated install and document exact supported paths. |
| Live game link and deployment | Does a previewed change appear in TLC, and are failure messages actionable? | Scratch-install terrain, theme and custom-theme writes pass; the resulting Greatwood_1 chunk audits cleanly. Failed pack and missing-STB paths restore files and keep edits dirty. | Verify object/terrain/foliage in a running game, then inspect restore from the GUI. |
| Popups and small-screen UI | Does every floating tool remain tied to its source and usable at high UI scale? | Context test covers Terrain, Level and Properties window closure; tour captures four tool windows at eight requested sizes. | Review setup, confirmation, import and error dialogs with focus/keyboard and resize. |
| CLI and automation | Can documented commands reproduce the editor's outputs and failures? | `check_all.py` and many focused tests exist. | Audit command examples against actual binaries, then run the required gate. |

## Terrain/foliage change in this pass

- `Document::endStroke` and `setVertexHeights` move grounded, unlocked placed
  objects with the height change in its existing undo step. Owned descendants
  follow the parent's delta, even when the child is floating or locked. Unrelated
  floating, buried and locked objects keep their position. `reseatThings` remains for older drafts and
  skips an object already closer to the new ground so a small lift is not doubled.
- The map preview reloads baked foliage after a committed terrain change and adds
  the saved-to-current ground delta to each instance's Z. This matches the
  existing STB deployment re-seat rule. A foliage-only reload avoids re-decoding
  placed things; duplicate object import warnings are suppressed per map.
- `fableforge_lockedthings_tests` passes direct heights, brush stroke, locks,
  nested ownership, initial position and undo. `tests/ui/terrain_follows_objects.txt` passes raise,
  2,514 preview re-seats, undo and zero remaining offsets on StartOakValeWest.
  At 1280x720 and 1366x768 with UI scale 1.5, write-actions screenshots show
  the wrapped two-write notice and both named actions without overlap; the
  panel scrolls to expose them.
  The broader `tests/ui/editor.txt`, `tests/ui/height_pens.txt` and core export
  suite pass with the automatic move. The script writes nothing to the game.
- Terrain and placed things are separate game write paths. A terrain write now
  warns when object edits remain in the draft; the footer states that both writes
  are needed and names the object write explicitly. The object write is still
  needed for those positions to appear in TLC. A combined reviewed deployment
  remains an open depth task.
- The fixed-size STB write now patches a temporary copy and replaces the bank
  only after both chunk and record writes succeed. Failed pack writes restore
  prior `.lev`, `.chunk` and `.record` bytes (or remove newly created output)
  and leave the terrain edit dirty for retry. The locked-things test checks
  these failure paths and the loose `.lev` rollback when the game STB is absent.
  `python tools/test_overworld.py` passed on the scratch install after this
  change: terrain, painted theme and custom-theme GUI deployments passed,
  and Greatwood_1's resulting chunk audited with zero findings. The scratch
  install was removed by the test. This verifies file output and parser
  integrity; an in-game visit after deployment remains open.

## Setup restore change in this pass

- Setup now says the files return to their backups, because a backup may have
  been taken from an already modded install. The confirmation states that
  loose `.lev`/`.tng` drafts can be restored or removed; it no longer promises
  to leave them intact.
- Restore refuses to run while a preview, asset load, world job or write is
  active. On success it rescans the install and reopens the selected map, so
  the editor cannot continue to show the pre-restore terrain or object draft.
- `python tools/test_setup_restore_ui.py` passed at 1280x720 and 1366x768 with
  UI scale 1.5. On a synthetic loose-level install, the open map changed from
  center height 9 to its backed-up height 4, the backup count fell to zero,
  and screenshots show the confirmation without overlap. The existing CLI
  `python tools/test_backups.py` also passed, including stage revert/rebase.

## Setup small-window depth check

- A 13-backup scratch install at 800x600 with UI scale 1.5 clipped the restore
  button below the window. Setup now keeps its install choices in a fixed footer
  and scrolls the health and backup details above them. Control and wrap widths
  follow the available scroll area, so the restore button and confirmation do
  not run beneath its scrollbar.
- `python tools/test_setup_restore_ui.py` now covers the 13-backup case as well
  as 1280x720 and 1366x768 / 1.5. It restores all files and reloads the open
  map to its backed-up height. The missing-install dialog was also inspected at
  800x600 / 1.5: missing game.bin, level bank and STB paths remain readable,
  with both install choices visible. These are scratch paths; a real modded-bank
  restore and permission-denied path remain to verify.

## Install switch depth check

- `scanInstall` previously emptied the map list while leaving the selected
  document and terrain mesh from the old install. A folder change now refuses
  unsaved object, terrain, World or staged-dialogue edits and waits for active
  loads/writes. Once accepted, it clears the old selection, preview, renderer,
  asset context and World layout before scanning the new root.
- `python tools/test_install_switch_ui.py` opens a scratch map, makes an unsaved
  terrain stroke and checks that a switch is refused. After Undo, it switches to
  a nonexistent folder, checks zero maps and no loaded document or preview,
  then switches to a second scratch install and loads its distinct map. The
  invalid-path capture at 800x600 shows an empty viewport and a compact Setup
  dialog. The editor does not write either scratch level.

## Compact map and viewport depth check

- At 800x600 with UI scale 1.5, the nominal map and tool panel minimums had
  pushed the tool panel offscreen. The panel widths now share the available
  width while reserving a usable viewport. The map search hint shortens; every
  map and group row shows its full name on hover.
- Narrow tool panels use a full-label mode menu. Narrow viewports use one
  View/Show/Frame row; Show opens the same layer toggles as the wide chips.
  The map title and cell size remain visible, while detailed terrain statistics
  move to a hover tooltip. Toasts start below the title.
- `tests/ui/compact_layout.txt` passes on retail StartOakValeWest at 800x600 /
  1.5: it selects Wireframe, turns off foliage through Show, and opens Edit
  through the tool-mode menu. The main view and open menus were inspected.
  The release gate includes this script. This checks fit and interaction at one
  compact size; a real 4K monitor and live resize still need inspection.

## Effect playback change in this pass

- The preview now keeps the elapsed time past a loop boundary. Before, the
  first frame beyond the selected duration reset to zero and lost the excess,
  causing drift at faster playback. Real frame time is still limited to 0.2 s
  after a stall, and the speed multiplier is honoured across short simulation
  chunks. The transport script checks one tick carried through a 0.5 s loop;
  playback and background scripts pass. A 1280x720 / UI scale 1.5 screenshot
  shows the playback, colour and preview controls without overlap. This is an
  editor simulation; unsupported engine particle behaviors remain approximate.

## Dialogue head layout change in this pass

- On 1280x720 with UI scale 1.5, the Load line control had fallen below the
  visible right-panel area, so selecting a head preset left an empty centre
  preview. Load line now sits directly below Sound ID; search remains below it.
- On 1024x600 with UI scale 1.5, the centre header and Reset view overlapped,
  and the timeline/status text ran outside the narrow panel. The head preview
  now reserves height for the visible viseme rows and status, and the narrow
  layout uses shorter header and hint text. `tools/test_dialogue_layout.py`
  passes the same retail line and scrub action at both sizes; inspected captures
  show the head, timeline and pose status without overlap. The retail head-pose
  test passes all five presets. Exact eye placement in the running game remains
  unverified.

## Terrain tool preview change in this pass

- Generate and Fit preview cache keys now preserve the exact floating-point
  parameters, so a fine adjustment cannot reuse an image generated with a
  rounded parameter string. Fit shows the number of vertices that will change
  and the largest absolute height shift before Apply.
- The tool copy now describes the current terrain workflow: grounded objects
  follow a generated or fitted height change; floating and locked objects can
  need manual placement, and terrain/object writes are separate.
- `tests/ui/fit_neighbours.txt` passes on Greatwood_Filler_04: 10,114 vertices
  change to meet four touching maps, the preview reports no remaining changes
  after Apply, Undo restores the original heights and pending preview, and Redo
  restores the fit. `tests/ui/visual_fractal.txt` and the core export suite pass.
  At 1280x720 with UI scale 1.5, both tool windows scroll to expose their
  actions; the Fit change summary and Generate height warning are readable.
  No game files were written by these UI scripts.

## 2026-10-01 asset-browser depth check

Tall texture previews preserve decoded aspect ratio, texture details wrap and
Export/Replace remain readable in narrow panels. Exported BRAZIER_POLE_24 is
64x256; tall and wide previews and action controls were inspected at requested
1280x720 and 800x600 with 1.5 UI scale. The existing scratch texture suite passes
replacement, append, backup and re-export (mean pixel difference 0.02).
The compact Assets selector now shows full page names in a menu. Model Reset
view fits below Wireframe. Existing model-browser automation and a scrolled
800x600 / 1.5 variant pass search, wireframe, material links and refresh; direct
menu selection and the repaired controls were also clicked and inspected.
Game rendering, texture animation and model import/placement remain separate
checks. Details and local evidence paths are in HANDOFF_WORLD_UI.

## 2026-10-01 install destination depth check

A reproduced stale mod-order/pack destination survived switching between two
installs. Accepted switches now reload the new root's order and conflict picks
and clear the old pack/report/provenance. Refused switches preserve the current
destination. The expanded install-switch test passes with different pack lists,
an invalid intermediate root and a return switch; every fixture file remains
byte-identical, with no added files. The second root's destination was inspected
on screen. All three Setup restore cases still pass. Automation's explicit
save-root override is now covered too: switching it refreshes the same state,
clearing it returns to the install's packs, and a same-root assignment preserves
an explicit destination. The expanded fixture remains byte-identical.

## 2026-10-01 model-import failure depth check

Invalid graphics input previously left an appended texture behind. Imports now
prepare all outputs first and roll back bank replacements on a reported failure.
The mesh-import suite verifies unchanged files for invalid input and a forced
late Windows rename failure, as well as successful OBJ/GLB geometry, collision,
definition references and backups. Recipe-pack order, deploy and byte-exact
undeploy checks pass. Recovery after process termination remains unverified.
Returning the GUI placement check to Edit exposed a redirected-root mesh preview
mismatch. The preview and thumbnails now use the same bank as the model browser,
with a base-install fallback. The final check verifies a rendered mesh instance,
an inspected brown cube in Arena and byte-exact placement undo. Core export,
texture round-trip and eight-map retail export checks pass. In-game imported-model
behavior remains unverified. See HANDOFF_WORLD_UI.

## 2026-10-01 model-input boundaries

GLB container input validation now has 313 retail-independent checks, passing
both normal and AddressSanitizer builds, plus the full scratch model-import
suite. Short/overflowing/truncated chunks and invalid ordering are rejected;
valid triangle geometry and unknown extension chunks are preserved. Accessor
bounds and scene traversal are not covered by this container-only checkpoint.

The accessor follow-up reproduces reads outside a declared buffer view and
checks logical buffer lengths, offset/count overflow, alignment and strides
before decoding. Sparse data now reports unsupported instead of being ignored.
The suite now passes 334 checks in normal and AddressSanitizer builds, including
interleaved data and both view/accessor offsets. Scene traversal remains open.

Scene traversal now rejects cycles/repeated nodes and missing references, handles
a 12,001-node chain iteratively, preserves instance order/transforms/winding and
keeps an empty selected scene empty. Normal and AddressSanitizer runs pass all
356 input checks. This is targeted importer coverage, not full glTF conformance.

## 2026-10-01 custom ground-theme failures

Theme creation now stages its texture/definition banks using the model import
transaction helper. A bad cliff PNG and a late locked-bank failure preserve every
scratch file; a valid two-texture theme has correct references and backups.
The GUI refuses a full map palette before bank I/O. Model-import and recipe-pack
order/deploy/byte-exact undeploy regressions pass. Recovery after abrupt process
termination remains outside this guarantee; no new live-game probe was run.

## 2026-10-01 mod-order interaction depth

Pack creation through the compact UI persists the correct manifest, source and
destination. Wrapped mod-name rows make long names readable. Enable/dependency
mutations are deferred until drawing finishes, avoiding invalid row references.
Actual popup, checkbox, arrow and drag interactions pass at normal and compact
sizes with saved order/dependencies checked. Deployment behavior retains the
separate recipe-pack evidence above.

## 2026-10-01 pack asset failure depth

A failed same-basename model add previously overwrote another recipe's file.
Recipe/role directories now isolate assets, and staged asset/manifest commits
preserve prior files on missing inputs or late write failures. The new pack test
covers malformed manifests, collisions, rollback and replacement; normal and
ASan runs pass. Mixed old/new asset paths build/deploy/undeploy correctly, and
actual UI adds preserve source bytes. The local ASan build disables its broken
stack-use-after-return instrumentation per LLVM #215376; see HANDOFF_WORLD_UI.

## 2026-10-01 glTF recipe portability

External geometry buffers are now copied with `.gltf` recipes and their packed
URIs relocated. Same-basename buffers retain distinct geometry; missing-buffer
failure preserves the previous pack. Normal/ASan pack checks and two-order
scratch recipe build/deploy/byte-exact undeploy pass. External images, GLB
external buffers and percent-encoded file URIs remain outside this step.

## 2026-10-01 OBJ input validation

Malformed coordinates and missing UV/normal references are refused instead of
producing altered geometry. Normal/ASan runs pass 371 model-input checks,
including supported relative indices, comments and optional texture coordinates.
The scratch OBJ/GLB import and GUI placement/undo regression passes.

## 2026-10-01 glTF triangle validation

Indices retain integer precision and reject invalid type, count and vertex
references. Normal/ASan runs pass 384 model-input checks. A full relink passes
all 27 CTest suites; the OBJ/GLB scratch integration passes with both CLI
executables refreshed. Full glTF conformance remains outside this check.

## 2026-10-01 import error visibility

Model/theme import errors now stay beside the action and wrap at compact widths.
Actual missing-input clicks, successful retries and screenshots pass at 800x600
/ 1.5 scale. Direct asynchronous errors and install-switch reset preserve the
scratch fixture's bytes. Full-palette and install-switch regressions pass.

## 2026-10-01 new-level lifecycle

Compact origin values now remain readable. Unsaved-edit refusal and retention
of edits made during creation pass in the expanded new-level suite. Actual
dedicated-region creation and GUI restore pass at 800x600 / 1.5; eight original
file hashes match after restore. A stale removed-map preview was reproduced and
fixed, with GPU-mesh absence checked by the expanded Setup restore test.
No new live-game, loose-layout or new-level pack-deployment evidence was added.

## 2026-10-01 world-pack capture and extracted layout

World capture now preserves existing pack files on malformed input or late
replacement failure; normal/ASan checks pass. Individual/two-order world merges
and sequential GUI world-move/new-level capture pass. The extracted-layout check
passes creation and deployment without recreating the WAD or altering its renamed
copy. All 27 CTest suites pass after the final relink. Live-game and composed-world
deploy/undeploy checks are still separate.

## 2026-10-01 pack workspace isolation

Nested pack operations no longer delete each other's shared temporary tree.
Owned edit/process-view folders pass normal/ASan cleanup checks and the complete
world-pack integration. Concurrent commits to the same destination remain
outside this workspace-isolation guarantee.

## 2026-10-01 captured override reversion

Capturing original content now removes the stale world/level/static-map override
instead of leaving the previous edit active. Normal/ASan tests cover removals,
rollback and unrelated recipes; the real-bank world-pack test covers removal of
a world file plus both static-map files. Missing shadow content remains unchanged
in the destination; capture is not a general deletion manifest.

## Terrain background-write depth, 2026-10-01

A second stroke during a terrain write reproduced mismatched LEV and baked
chunk/record output. The worker now owns a terrain snapshot, and successful
completion updates only the saved baseline of the same document session.
`tools/test_terrain_async.py` verifies exact output equality against a
single-stroke reference while editing and while switching away/reopening.
Later-edit undo/redo and source-bank hashes pass. Focused normal/ASan core
checks cover snapshot independence and stale completion refusal.

The new-level pack path also passes creation, deploy, GUI open and undeploy:
all eight original bank hashes return and the pack is unchanged. The retained
map screenshot shows terrain/water; the reduced fixture lacks graphics.big.
Neither check supplies new in-game evidence. Concurrent writers targeting
the same pack/bank remain a separate audit item.

## Repeated terrain pack writes, 2026-10-01

Pack bakes now compare against the terrain belonging to their source STB,
so successive saves retain the complete foliage height adjustment and painted
layer rebuilds. Stock and extracted scratch tests compare all three pack files
byte-for-byte for single versus repeated writes, including painted themes.
All source/extracted-file hashes remain unchanged and no active WAD is created
in the extracted layout. A missing-source failure preserves the previous pack
and keeps the draft dirty. This does not serialize competing write jobs.

## Overlapping GUI file operations, 2026-10-01

The editor now permits one file job at a time and refuses competing saves,
pack edits, mod-order changes and save-folder changes with the active job named.
Draft editing remains available. Terrain, new-level and actual mod-deployment
probes verify refusal, retained draft edits, successful retry and unchanged or
restored source-bank hashes. Separate processes/CLI writers remain outside this
guard; it is not a filesystem lock.

## Mod refresh and undeploy recovery, 2026-10-01

Same-window new-level deployment refreshes maps/assets outside the Mods tab and
waits for World/preview readers. Three scratch cycles (clean, World active, dirty
map draft) pass with byte-exact eight-bank restoration and an unchanged pack.
The draft retains object/terrain undo. The inspected new-map screenshot shows
terrain/water; graphics.big and in-game checks are absent. Normal/ASan stage
checks prove missing-backup refusal and retry after a locked second target.
Stage/apply failure atomicity, cross-process writes and pending-world-edit
refresh remain open. Earlier new-level busy evidence covers failed creation;
the new refresh script explicitly checks successful creation before deployment.

## Staging failure recovery, 2026-10-01

Stage/apply publishes checked recovery data before changing target files. Tests
cover a locked target followed by complete undeploy, unreadable-original cleanup,
stale-backup refusal, recovery filename aliases and duplicate Windows targets.
A failed redeploy from a malformed patch also refreshes the GUI back to the
restored map list and clears the removed preview; all source-bank hashes match.
The successful clean/World/draft cycles still pass. These are reported-failure
recovery checks, not power-loss durability or cross-process serialization.

## Concurrent CLI workspace isolation, 2026-10-01

Concurrent conflict/deploy commands on different installs no longer delete one
another's scratch trees. Merge, output and binary-patch probe directories are
owned separately and removed on success/failure. A real-definition-bank test
checks distinct payloads, exact restoration, two failure paths, preserved
unowned sentinels and no leaked owned workspace. This does not permit concurrent
writers to the same install or output destination.

## Pending World edits across mod refresh, 2026-10-01

World refresh now reloads the base layout while retaining queued moves,
ownership/visibility edits and their undo history. The added-map count changes
in both views; undo/redo/revert still work. A move for an undeployed map remains
queued, refuses Apply before bank changes, and can be undone. The complete
five-case GUI script passes with exact source-bank restoration and an unchanged
pack. No new in-game evidence is claimed.

## World draft preservation during creation, 2026-10-01

Creating a pack level retains pending World moves, ownership/visibility edits,
selection and undo while refreshing the layout. The focused GUI case verifies
successful creation and all three undo paths; source-bank hashes remain exact.
Independent WLD checks confirm queued edits were not captured into the new-level
pack. Direct-install creation with a World draft remains unexercised.

## World background-write drafts, 2026-10-01

Later moves and owner/visibility edits survive a completed World pack write,
including fields put back to their old baseline. Undo/redo reaches the new saved
baseline; truncating the 128-entry history does not drop all later steps. Four
GUI cases verify queued values, serialized WLD values, repeated saves and exact
source-bank preservation. The pure draft helper passes normal/ASan tests; the
complete 29-suite core gate passes. Direct-install and in-game behavior were not
newly exercised by these cases.

## World LEV scratch ownership, 2026-10-01

World moves no longer overwrite an existing LEV in the shared temp directory.
Four GUI pack-save cases preserve a marker there and leave no owned extraction
workspace. A separate ASan CLI move and restore preserve all seven source-bank
hashes; only a byte-identical, unchanged WAD backup remains. The 29-suite core
gate passes. Other fixed extraction paths and same-install writers remain open.

## Restore comparison and cleanup, 2026-10-01

Rebasing now prepares the editor baseline from the staged original before stage
recovery consumes its manifest/backups. Locked-baseline and subsequent locked
target cases retain recovery data and retry to retail bytes in normal and ASan
CLI tests. This resolves the rebase-failure gap recorded below.

Failed staged recovery now stops ordinary Restore. A missing staged original
fixture preserves the edited target, ordinary backup and manifest, then restores
retail bytes on retry after supplying the missing original. The existing mixed
backup-convention/stage-rebase regression also passes. Rebase failures after a
successful stage revert remain under review.

Unreadable files no longer compare equal, locked-target errors retain their
real cause, and failed backup/marker cleanup is reported and retryable.
`--forget` removes verified unchanged originals while retaining orphaned staged
backups. Windows lock fixtures pass with normal and ASan CLI builds; the real
unchanged WAD backup from the World move probe is removed with all seven bank
hashes preserved. Full build and core gate pass. No new GUI or in-game probe.

## Terrain neighbour scratch ownership, 2026-10-01

Greatwood_1 bakes no longer overwrite the shared neighbour extraction folder.
Nine GUI cases each on stock and loose layouts preserve old-path markers, clean
owned workspaces and preserve source banks. The three-file reference pack is
byte-identical before/after the ownership change; loose tests retain the renamed
WAD layout. Writer builds pass. No native bake algorithm or in-game claim changed.

## Seam-stitch scratch ownership, 2026-10-01

Dry-run seam checks no longer overwrite shared LEVs. Stock and loose tests move
TeleporterGreatwood beside OrchardFarm, stitch, compare six shared heights,
confirm a tight seam and Restore the exact original inventory/hashes. Markers
survive and owned workspaces are cleaned. The stock test also passes under ASan.
Writer builds pass; seam/feather algorithms are unchanged.

## CLI level extraction ownership, 2026-10-01

Concurrent info calls no longer share a temporary LEV. All resolveLevel callers
retain workspace ownership; checked extraction and RAII cleanup cover success
and early return. Normal/ASan tests compare concurrent WAD info and loose/direct
file info, preserve the old shared marker and verify no owned workspace remains.
Missing-theme validation exercises early-return cleanup without writing a level.
Other fixed extraction paths remain separate work.

## Navigation diagnostic extraction, 2026-10-01

nav-lines/nav-compare use owned CLI extraction. Normal/ASan tests preserve shared
markers and all eight bank hashes; TeleporterGreatwood diagnostics match across
WAD and loose layouts, and nav-lines output matches the prior implementation.
This map has no placed hull lines. Only file ownership changed; navigation
parity remains experimental.

## GUI extraction ownership, 2026-10-01

GUI level loads and World overview tiles retain their own extraction folders,
including neighbour/fit/detail work transferred between threads. Stock, loose
and ASan GUI tests preserve old-path markers and source hashes across fit undo/
redo, four neighbour previews, export, 400 overview tiles and detailed ground.
The export and tile payloads match before/after (tile source keys excluded).
Screenshots show expected fit terrain and World shoreline/water. The fixture
omits graphics.big; placed-object rendering is not covered by these probes.

## New-level helper workspace ownership, 2026-10-01

Blank authoring, palette lookup, entrance setup and minimap LEV/PNG generation
own their scratch files and check writes. Stock, loose and ASan CLI+GUI tests
create a 64x64 own-region level, verify entrance/minimap, exercise GUI palette
loading, preserve old shared files and restore exact original inventories.
Authored LEV/minimap PNG match baseline bytes. Full build and all 29 core suites
pass. Creation and minimap algorithms are unchanged.

## Definitions diagnostic workspace, 2026-10-01

Concurrent defs roundtrips own separate output directories and no longer collide
or remove unowned scratch data. Normal/ASan tests verify semantic roundtrip
identity, original bank hashes and marker preservation. Writer format unchanged.

## EgoCore compiler workspace ownership, 2026-10-01

Concurrent real Controller Support compiles now preserve both text layers and
produce identical banks (43 fields, one new record), while retaining unowned
files. Normal/ASan host checks cover real compiler success and failure cleanup.
The shared directory helper is now in forgecore with ownership/exception/prefix
tests; all 30 suites and the full GUI World workspace probe pass. Compiler
failure still warns and permits DLL-only output; that behavior is not changed.

## 2026-10-01 continuation: repeated edits retain created-file baselines

Repeated terrain writes and loose TNG synchronization used to add an original
backup to files already marked as created. The normal GUI reproduction
(`build/created-restore-rk2oohdg`) left both files after two Restores. The shared
backup helper now honors modern and legacy creation markers; TNG save/deploy
uses that helper too.

Full build and all 30 core suites pass (28.32 seconds). The actual repeated
terrain/TNG GUI workflow and two exact Restores pass normally
(`build/created-restore-2zss1uol`) and under ASan
(`build/created-restore-tyhsj1pp`): all eight original banks match and no created
files or backup artifacts remain. The first ASan run exceeded the ordinary
60-second terrain wait; the test now uses the existing 180-second file-job wait.
Already contradictory metadata from older versions is not repaired by this change.

## 2026-10-01 continuation: conflicting Restore metadata

Restore now refuses a creation marker paired with an ordinary/legacy original
or overlay backup for the same target, before any stage recovery or file change.
The diagnostic names both records. It cannot infer which record represents the
intended baseline, so it retains both for explicit resolution. Missing targets
are checked too; Windows target matching ignores case.

The pre-fix fixture (`build/restore-conflicts-wq8b7w0n`) consumed a stage and both
contradictory records while reporting success. All 12 marker/backup/missing-target
combinations now preserve the complete install and recover after explicit marker
removal: normal `build/restore-conflicts-ho0s42n8`, ASan
`build/restore-conflicts-pvder3r6`. Existing lock/retry/forget checks pass normally
(`build/restore-failures-vf8oap6b`) and under ASan
(`build/restore-failures-5xrdtzqa`); mixed conventions and staged rebase pass via
`tools/test_backups.py --keep`. All three normal writer targets build. This check
does not resolve editor originals taken after a stage created a new file.

## 2026-10-01 continuation: validate stage ownership before baseline preparation

A missing staged original could be discovered only after an editor original was
rebased (`build/restore-stage-plan-ubvv1fjs`). forgecore now exposes its existing
whole-plan validation as `stage::inspectRecovery`; Restore uses that read-only
plan before any baseline change, and stage revert revalidates before applying it.
Only originals belonging to the validated stage are eligible for the existing
mtime-based rebase. An unrelated orphaned .forgebak cannot replace an editor
original just because another stage manifest exists.

A stage-created target paired with an original/legacy/overlay backup is now
reported as conflicting recovery data before writes. Restore cannot establish
whether that backup predates the stage, so it preserves both records for explicit
resolution instead of removing then resurrecting the staged file.

`tools/test_restore_stage_plan.py` passes normally
(`build/restore-stage-plan-xudnjwox`) and under ASan
(`build/restore-stage-plan-jkiak2as`): missing-plan preservation/retry, unowned
staged backup preservation and three stage-created conflicts with resolved retry.
Normal conflict/failure checks pass (`build/restore-conflicts-r_ekojt2`,
`build/restore-failures-r503rzon`); ASan failure and stage unit checks pass
(`build/restore-failures-4xtjrn8v`). Full build and all 30 suites pass (21.53 seconds),
as does the mixed-convention/staged-rebase check. The mtime heuristic within a
valid stage is unchanged; concurrent writers to one install remain unsupported.

## 2026-10-01 continuation: checked creation markers

A directory at the creation-marker path made loose TNG Save report success and
clear the draft, despite leaving the new file untracked by Restore
(`build/creation-marker-nej29q4j`). Marker validation now rejects non-files and
marker writes check close/flush success. Loose saves report the error before
writing their target; the draft remains dirty. Existing regular markers are
retained, and an existing original backup remains the baseline when a missing
loose file is written again (no contradictory creation marker is added).

Normal and ASan GUI checks pass (`build/creation-marker-40nt1v6t`,
`build/creation-marker-6u88aspt`): modern/legacy marker-directory refusal,
draft retention, successful retry/Restore, modern/legacy original preservation,
and unchanged eight-bank hashes/inventory. All normal writer targets and ASan
CLI/GUI build. New-level installation currently adds markers after its core
commit; late commit failures are being investigated separately.

## 2026-10-01 continuation: recovery metadata before loose-level commit

A locked STB at the last commit step left the newly committed loose LEV/TNG
without creation markers; Restore retained both files
(`build/newlevel-recovery-axfje_04`). The core installer now offers a preparation
callback after staging and before target replacements. Both copied and blank
editor-level creation use it to prepare checked markers, so even a late failure
leaves enough metadata for Restore. The core API no longer describes its
multi-file commit as atomic; failure still requires Restore.

Normal late-failure recovery passes (`build/newlevel-recovery-t9z5fuvu`) and ASan
passes (`build/newlevel-recovery-sdfg42zu`), with exact original file inventory
and hashes after Restore. A directory blocking the second marker refuses the
commit, preserves originals and the occupant, and cleans the first marker via
Restore: normal `build/newlevel-recovery-4ylvpwmf`, ASan
`build/newlevel-recovery-ci_bz431`. The first marker-test runs redundantly hashed
the whole install inside a per-file loop and were stopped; the corrected test
hashes it once. Successful own-region loose blank creation, entrance, minimap,
GUI palette and exact Restore pass (`build/newlevel-workspace-84p9xln8`). Full
build and all 30 core suites pass (27.34 seconds). This adds recovery tracking,
not rollback of every already-committed bank or power-loss atomicity.

## 2026-10-01 continuation: fail incomplete EgoCore text builds

A deliberately failing compiler still produced a successful DLL-only build
(`build/egocore-workspace-srzlriok`). Mod merge now returns failure when a mod
contains Data/Defs but its text layer cannot be normalized. It stops before
writing output banks or staging its DLL; diagnostics retain the compiler reason.
Mods without Data/Defs can still build their DLL-only layer.

Normal real concurrent Controller Support compiles pass
(`build/egocore-workspace-dcoqiri5`): 14 changed records, 43 fields, one added
record, identical output banks and preserved source banks. The failure-only
probe (`build/egocore-workspace-_jw0wtg5`) verifies nonzero build/conflicts/deploy
status, no partial new output or stage, unchanged existing output, missing-text
refusal and DLL-only compatibility. ASan host runs all these cases plus real
concurrent compiles (`build/egocore-workspace-pahpvari`); the external Rust compiler
itself is not instrumented. Owned workspaces clean up on every exercised exit.
Both forge-tools builds pass. Existing redeploy ordering is unchanged: it reverts
an earlier stage before building, so a failed redeploy can leave the baseline
install with the previous mod already removed.

## 2026-10-01 continuation: report EgoCore copy and resource I/O failures

EgoCore's tree copier discarded copy errors, allowing a locked DLL to be reported
as copied and registered (`build/egocore-io-before.log`). Resource reads could
silently become empty payloads; an unreadable source bank was also downgraded to
a warning (`build/egocore-bank-io-before.log`). Tree iteration/copies now throw on
I/O errors, resource/header reads check open/read status, and text/bank writes
check open and close/flush. An existing bank that cannot be opened fails the build.
Missing target banks and unsupported resource placements retain their existing
reported-skip behavior.

The new `fableforge_egocore_io_tests` passes normally and under ASan: locked DLL,
Mods.ini, source bank, resource, header and output-bank failures; successful retry;
correct replacement payload/header; preserved unrelated entry and source bank.
Full build and all 31 suites pass (23.59 seconds). Real concurrent text compiles,
failure refusal and DLL-only compatibility pass normally
(`build/egocore-workspace-v143znqa`) and with the ASan host
(`build/egocore-workspace-7nypzgv9`). Both output bank pairs match the pre-I/O-change
hashes (`v143znqa/baseline_comparison.json`). No native resource layout changed.
A failed build may leave already-written files in an explicit build output;
this is error reporting and deploy prevention, not transactional output rollback.

## 2026-10-01 continuation: reject malformed text overrides before compilation

Removing the final closer from a Controller Support definition still produced a
successful partial build (`build/egocore-workspace-jla6w9fv`): the merge omitted
that override before defc saw it. Block extraction now rejects missing closers,
a new definition before the prior closer and missing type/name headers. Errors
from normalization identify the source file. Comments and quoted strings are
masked without changing byte offsets so their directive text cannot split blocks.

Evidence: EgoCore `Mods/ModManagerBackend.h:316` supplied the original merge
pattern; the local fable-defs compiler's `defs/src/text/mod.rs` treats a new
block before its closer as an error, and `lexer.rs:468` ends raw quoted strings
at the next quote (backslashes are literal). The block merger remains a limited
extractor, not a replacement for the full compiler grammar.

The new definition test covers invalid overrides/baselines, replacement/template
addition, comments, quoted directive text and exact untouched prefix/suffix
preservation. Normal and ASan tests pass. Full build and all 32 suites pass
(19.26 seconds). Real concurrent compiles and the malformed-mod refusal pass
normally (`build/egocore-workspace-51bydlfr`) and under the ASan host
(`build/egocore-workspace-0i789iru`); malformed input emits no build output.
Successful game.bin/names.bin hashes match the prior implementation
(`51bydlfr/baseline_comparison.json`).

## 2026-10-01 continuation: running-install directory boundaries

The process guard compared raw path prefixes, so `install-other/Fable.exe`
blocked Restore for `install` (`build/install-guard-tn1n417_`). It now includes
the directory separator in the normalized install prefix. Own-install and
nested processes remain blocked; the conservative fallback when a process path
cannot be queried is unchanged.

`tools/test_install_guard.py` builds a small waiting helper in scratch, names
only those helper copies Fable.exe, and stops only its own process handles.
No real game process is launched or stopped. Normal (`build/install-guard-x1dnohu8`)
and ASan CLI (`build/install-guard-xcaxtmpe`) pass: sibling Restore succeeds,
own/nested Restore refuses without changing target or backup, uppercase/forward
slash/trailing-separator aliases remain blocked, and retry after helper exit
restores/forgets exactly. All three normal writer targets and the ASan CLI build.

## 2026-10-01 continuation: guard active loose TNG draft saves

On an extracted install, Save draft wrote the active TNG and cleared its dirty
state while Deploy correctly refused the same running-install write
(`build/loose-save-guard-c1kr3ou9`). Save now applies the shared game-write guard
when the destination uses loose levels or the document is an external world.
Stock installs keep their separate loose draft behavior.

Normal loose GUI (`build/loose-save-guard-bf5bbo95`) and ASan GUI/CLI
(`build/loose-save-guard-p_ave4uw`) pass: both save/deploy refuse while a scratch
helper named Fable.exe runs, dirty state and the complete install remain intact,
save succeeds after that helper exits, and Restore returns exact original hashes
and inventory without recreating the WAD. Normal stock GUI
(`build/loose-save-guard-plyn_8fn`) permits the loose draft while still refusing
WAD deploy and also restores exactly. The initial stock retry reused an existing
ScriptName; the fixture now uses distinct names per run. No real game was started,
stopped or written. Normal and ASan GUI builds pass.

## 2026-10-01 continuation: roll back failed WAD/loose TNG deployment

A locked loose TNG was silently left stale after the WAD changed, while the GUI
reported success and cleared its draft (`build/tng-deploy-failures-gaywh3ra`,
including `baseline_report.json`). Object deployment now uses the existing
PendingBanks helper: prepare the WAD and checked loose text, complete backup
preflight, then install both. A failed target replacement rolls back earlier
replacements; dirty state is cleared only after the pair succeeds. Scratch
outputs use the helper's owned directory instead of a shared WAD temp filename.

Normal (`build/tng-deploy-failures-b662b0o7`) and ASan GUI/CLI
(`build/tng-deploy-failures-zq85g8th`) pass early locked-WAD and late locked-TNG
failures with exact prior target hashes, dirty-draft retention, workspace cleanup,
successful retry with matching WAD/loose payloads and exact Restore. The repeated
terrain/TNG creation-marker workflow still passes two exact Restores
(`build/created-restore-wyy666el`). Full build and all 32 suites pass (17.77 seconds).
As with other PendingBanks users, a failed rollback retains its recovery directory
and reports its path; this is not a power-loss transaction.

## 2026-10-01 continuation: validate original-backup path types

A directory at `.forge-orig` was treated as an existing original, allowing a new
untracked loose TNG to be saved (`build/creation-marker-a7ijwtxp`). Original
lookup now validates both modern and legacy paths as regular files and reports
filesystem errors. Creation-marker preparation validates originals even if a
marker already exists.

Expanded normal (`build/creation-marker-tffaeycq`) and ASan GUI/CLI
(`build/creation-marker-6qmvlxrz`) checks pass for modern/legacy creation-marker
and original-backup directories, both new and existing target refusal, dirty
draft preservation, unchanged directory occupants, successful retry, existing
valid original retention and exact Restore/inventory. All three normal writer
targets and ASan CLI/GUI build. Regular backup contents are not inferred from
filename alone; differing backup conventions are being checked separately.

## 2026-10-01 continuation: require multiple originals to agree

Restore consumed a live stage plus differing modern/legacy originals and reported
success (`build/restore-originals-dpgglctc`). Its preflight now compares every
ordinary/legacy/overlay original for the same target. Different or unreadable
copies stop Restore before any stage or target change; identical copies remain
accepted and `--forget` clears both after recovery. Diagnostics name the records
that need an explicit baseline decision.

All three convention pairs pass with differing and identical contents, including
full-state preservation, stage retention and resolved retries: normal
`build/restore-originals-b7d7z5t0`, ASan `build/restore-originals-8843yfds`.
Existing creation conflicts, stage ownership/preflight and lock/retry/forget
scripts pass normally (`restore-conflicts-0ztti2_g`, `restore-stage-plan-y6nxlgzj`,
`restore-failures-0zmg4k99`) and under ASan (`restore-conflicts-rhgik8o0`,
`restore-stage-plan-pnt5tghg`, `restore-failures-tzhywks5`), all under `build/`.
All three normal writer targets and ASan CLI build.

## 2026-10-01 continuation: Restore includes the standard graphics bank

Real GLB+texture import changed all four banks, but Restore omitted
`data/graphics/graphics.big` and left its backup (`build/mesh-restore-8vsljipr`).
Backup scanning now includes `data/graphics` as well as its `pc` subdirectory.
This applies to backup listing, conflict preflight and Restore.

`tools/test_mesh_restore.py` imports the existing synthetic cube fixture into
real copied banks, verifies all four backups/changes, then lists and restores
all banks byte-exactly with no extra files. Normal standard graphics path
(`build/mesh-restore-ye00564z`), normal `--graphics-pc` fallback
(`build/mesh-restore-drkfyyqr`) and ASan CLI standard path
(`build/mesh-restore-uyghjdof`) pass. Each import creates 24 vertices/12 triangles,
its collision hull, texture and OBJECT definition. All three normal writer targets
and the ASan CLI build; source install files remain untouched.

## 2026-10-01 continuation: preserve user script around live-link hooks

Offline regression `build/livelink-before.log` reproduced removal truncating all
Lua appended after the hook. Removal now validates and removes only the complete
generated hook, preserving surrounding bytes. LF/CRLF and old embedded install
paths are recognized; modified or duplicate hooks are refused without writes.
Host paths must stay beneath FSE and cannot target the AtlasLink worker directory.

Install checks host reads and the original backup result, then stages worker/host
replacements through PendingBanks. Removal stages the host edit and command-file
deletion together. Failed commits roll back earlier replacements; checked close
errors are reported. Existing backup and retained-worker behavior is unchanged.
Same-install concurrent writers and power-loss atomicity remain unsupported.

New `fableforge_livelink_tests` uses an owned synthetic FSE tree. It covers exact
tail preservation, CRLF, old paths, reinstall, duplicate/modified hooks, invalid
backup/command paths, unreadable hosts, and Windows locks forcing rollback after
worker or host replacement. Successful retries restore the expected bytes. Normal
writer builds and the 33-suite core gate pass (16.31s); final added lock/path checks
also pass, as does the complete focused test under clang-cl AddressSanitizer
(existing nested-catch workaround). No live game or FSE runtime was exercised.

## 2026-10-01 continuation: complete live-link commands and owned writes

`build/livelink-command-before.log` reproduced a successful send with truncated
definition/script names. Teleport/spawn/reload now build full strings, keep the
existing three-decimal coordinate format with a fixed decimal locale, and refuse
NaN/infinite coordinates before touching the pending command. Lua strings escape
control bytes with three decimal digits, including embedded NUL followed by a
digit; quotes/backslashes retain their existing escaping. Syntax follows the
[Lua 5.0 lexical contract](https://www.lua.org/manual/5.0/manual.html#2.1).

Commands use an owned PendingBanks workspace rather than overwriting the shared
`cmd.lua.tmp`. Locked command replacement reports failure and preserves the old
command; a retry succeeds and owned workspaces are cleaned. The unowned temporary
file is untouched. The protocol is still a single latest-command slot, not a queue.

Expanded `fableforge_livelink_tests` passes normally and under clang-cl ASan:
long spawn/map strings, exact control-byte escapes, finite-coordinate refusals,
locked replacement/retry, unrelated temporary-file preservation and cleanup,
plus all earlier hook cases. Normal editor/CLI targets build. These are offline
file/protocol checks; game-side execution was not exercised.

## 2026-10-01 continuation: validate live-link log records and freshness

`build/livelink-status-before.log` reproduced a minute-old log initially reported
as freshly live. Polling now seeds heartbeat age from log modification time and
keeps observations per normalized install path. Re-reading the same heartbeat,
including after unrelated log activity, cannot reset its observed age. A changed
valid heartbeat starts a new observation. This is log-based status, not proof of
a running game; the process guard remains independent.

Only complete newline-terminated records are parsed. The clipped first line of
the 64-KiB tail is skipped. Hero records require a valid sequence, map and three
fully parsed finite coordinates before replacing the prior valid position. Ack
IDs/booleans are validated; message text retains internal pipes and loses only
the CRLF terminator. Unreadable logs report no live heartbeat.

Expanded offline tests cover stale/fresh logs, identical beats in separate roots,
unrelated log activity, NaN/infinity/malformed/partial records, clipped tails,
full ack messages and locked logs. Normal writer builds, focused checks and
clang-cl ASan checks pass. Live FSE execution and UI camera-follow were not run.

## 2026-10-01 continuation: prepare loose LEV navigation before publishing

`build/terrain-save-failure-before.log` reproduced a refused navigation patch that
had already written edited cells and cleared the terrain dirty state. A synthetic
32x32 LEV with a valid directory but unsupported navigation block version exercises
this path for both an existing output and a new output.

`Document::saveTerrainLoose` now serializes and patches navigation in an owned
PendingBanks workspace. Only after preparation succeeds does it prepare recovery
metadata and replace the target. The in-memory navigation baseline, saved terrain,
sound-list state and success notes update after replacement. Invalid navigation
leaves the original/absent target and metadata untouched, keeps the draft dirty,
and supports exact Undo. LEV save now also checks stream close errors. Serialization
and navigation algorithms are unchanged; unrelated LEV bytes retain their existing
preservation contract.

Normal 33-suite CTest passes (15.68s); core export/navigation and locked-things
checks pass under clang-cl ASan (1.38s). All normal writer targets build. Repeated
real copied-bank terrain/TNG deployment and two byte-exact Restores pass at
`build/created-restore-ifa3d56z`, including created-file markers and clean inventory.
The first post-fix run hit baseline leftovers in the new test's initially shared
fixture; that fixture now owns a unique TemporaryDirectory and the rerun passes.
No live game writes or in-game navigation claims.

## Audit rule

For each row, record a concrete behavior, the tested map or asset, the saved
artifact if applicable, the visual result, undo/restore result, and any known
gap before treating it as reviewed. New findings go into this file and their
fixes into the existing handoff or editor documentation.
