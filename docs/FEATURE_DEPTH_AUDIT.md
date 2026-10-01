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

## 2026-10-01 continuation: refuse incomplete region-entrance inputs

`build/gtg-before.log` reproduced an unterminated GTG map accepted for editing.
Round-trip equality alone accepted the whole broken map as a preserved tail.
The entrance writer now validates complete, positive/unique/ascending map slots,
thing-section framing and complete parsed things/components before backup or
writes. This is writer preflight; the existing read-only GTG parser is unchanged.
It rejects non-finite placement/direction, invalid level-name tokens and exhausted
UID allocation. Direction normalization uses double hypot to avoid overflow for
finite float inputs.

Empty-section comments are preserved when adding the first entrance. GTG output
is staged and checked through close before replacement; locked/read failures
preserve the old file, and success notes are emitted only after commit. Exceptions
are returned through the existing bool/error API. This does not establish new
engine semantics or full validation of arbitrary TNG property values.

New `fableforge_gtg_tests` covers malformed maps, duplicate/order errors, incomplete
things/components, invalid input/UID exhaustion, comment preservation, idempotent
updates, locked/unreadable files and successful retry. All 34 normal CTest suites
pass (19.71s); core export + GTG focused ASan tests pass (1.81s). Normal editor and
CLI builds pass. `tools/test_newlevel_workspace.py --loose` also passes at
`build/newlevel-workspace-y0_rogew`: real copied-bank own-region creation, GTG
entrance, minimap, GUI palette, renamed-WAD preservation and byte-exact Restore.
The source install was not changed; runtime region travel remains untested.

## 2026-10-01 continuation: preserve entrance object customizations

`build/gtg-preserve-before.log` reproduced custom fields disappearing when an
existing entrance moved. The writer now identifies the player-start by parsed
ScriptName plus HOLY_SITE_PLAYER_START definition, then edits only position and
orientation fields through forgecore's TNG editor. The adjacent preceding entrance
is updated with it. Other properties, components, UIDs, comments and formatting
remain intact. An unpaired player-start receives a new entrance and retains its
own data; missing physics is added. Quoted names are matched without reformatting.
Duplicate names or the same name on another definition are refused. Incidental
ScriptName text inside ScriptData no longer selects an unrelated object.

Expanded GTG core tests pass normal/ASan, including byte-exact expected deltas,
quoted names, unpaired starts, duplicate names and unrelated script text. The core
export suite also passes. `tools/test_entrance_preserve.py` copies real WLD/BWD/GTG
and game.bin, creates/moves an entrance with custom fields, checks that only the
requested position bytes change, repeats idempotently, and Restores exact bytes
and file inventory. Normal `build/entrance-preserve-b5vw_pve` and ASan CLI
`build/entrance-preserve-pdyjkzue` pass. Normal writer targets build. No live game
files were changed and no runtime entrance behavior is claimed.

## 2026-10-01 continuation: guard high-level CLI install writes

`build/cli-write-guard-isjj_or9/failure.json` records `forge entrance` returning
success and changing GTG while a synthetic Fable.exe from that scratch root ran.
The CLI now shares an explicit install-specific process preflight before writes:
blank/new level, entrance set, region properties, world move/owner/sees/stitch,
minimap registration, STB compaction, chunk-zcheck --write, and asset imports.
Existing lower-level asset/Restore guards remain in place. Entrance/world/backups
reads and compact/stitch dry runs remain available.

`tools/test_cli_write_guard.py` builds a hidden inert helper, launches only its own
scratch Fable.exe, checks all 15 command refusals and unchanged hashes/inventory,
then exercises five read/dry-run paths. After its own helper exits, entrance write
and exact Restore succeed. A similarly named sibling install process allows both
write and Restore. Final normal `build/cli-write-guard-7konlyo7` and ASan CLI
`build/cli-write-guard-wvas9mvu` pass; the initial own-root-only check also passed
at `build/cli-write-guard-6w0c4hdo`. The real game process is never stopped.

This is command preflight, not synchronization against a game launched during a
long operation. Explicit-path forge-tools container commands were not changed.

## 2026-10-01 continuation: reject malformed entrance CLI coordinates

`build/entrance-preserve-vihzqumy/invalid_0.log` records `oops 2 3` being silently
converted into an entrance at `(0,2,3)`. The entrance CLI now parses each entire
coordinate with locale-independent from_chars, requires finite in-range floats,
and accepts exactly two/three coordinates for a write or none for a read. Missing
--install values report usage errors. Explicit plus signs and decimal exponents
remain accepted; malformed double signs, suffixes and extra/missing coordinates
are refused with exit 2 before install lookup or writes.

`tools/test_entrance_preserve.py` now checks 14 invalid argument sets against exact
bank hashes and inventory before its successful create/customize/move/idempotent
update/Restore path. Final normal `build/entrance-preserve-gdmnu978` and ASan CLI
`build/entrance-preserve-56724y6j` pass. This change is limited to entrance argument
parsing; other CLI numeric options retain their existing parsers.

## 2026-10-01 continuation: recover diagnostic STB writes

`build/chunk-write-recovery-mky8mf52/write_report.json` records all three baseline
facts: chunk-zcheck --write changed STB, created no original backup, and consumed
an unrelated `<bank>.atlas-tmp` file. The command now prepares output in an owned
PendingBanks workspace, reparses and verifies the intended chunk/record before
commit, rechecks the running-game guard, and installs with the original-backup
preflight. Missing prepared map/entry or mismatched bytes/audit failure reports
an error without publishing the prepared bank. Locked replacement keeps the prior
bank and cleans only owned temporary files.

`tools/test_chunk_write_recovery.py` uses real copied game.bin/STB plus an unowned
temporary-file sentinel. Greatwood_1's one-unit foliage ride exercises relayout
(3,357,180 -> 3,359,217 chunk bytes), with exact chunk/record readback and zero
audit issues. It checks the backup, byte-exact Restore, locked refusal, retry and
second exact Restore, including file inventory and owned-workspace cleanup.
Normal `build/chunk-write-recovery-kl161ie8` and ASan CLI
`build/chunk-write-recovery-q4mt3nq1` pass. No relocation, compression or
foliage algorithm changed; no live game data was written. Same-size replacement
remains on the existing core writer path and was not separately forced here.

## 2026-10-01 continuation: owned STB compaction output

`build/compact-workspace-before.log` reproduced compaction consuming an unrelated
`<bank>.compact-tmp`. The install wrapper now prepares its compacted bank inside an
owned PendingBanks workspace, keeps the existing full payload comparison, prepares
the original backup only after verification, and replaces with rollback-aware file
operations. It checks for a game from that install both before work and before
replacement. The compaction algorithm and table/payload layout are unchanged.

The synthetic core test verifies exact compacted bytes, the original backup,
unowned-file preservation and a second already-compact result. All 34 normal CTest
suites pass (16.93s), and the core suite passes under clang-cl ASan. Normal editor
and CLI builds pass. `tools/test_chunk_write_recovery.py --compact` exercises real
copied-bank compaction, locked replacement refusal, retry and two exact Restores,
including inventory and owned-workspace cleanup. Normal evidence is
`build/chunk-write-recovery-8l_8ihxw`: 426 payloads verified, 578.1 -> 574.5 MiB.
The same real-bank check passes under ASan at
`build/chunk-write-recovery-xnv7iwgc`.
The source install is untouched; live runtime on the compacted bank was not tested.

## 2026-10-01 continuation: prepare texture imports before recovery metadata

`build/texture-recovery-yxqv_6_1/invalid_report.json` reproduced an invalid PNG
creating an original backup and deleting an unrelated textures.big.atlas-tmp.
The add/replace wrapper now imports and validates into an owned PendingBanks
workspace, then checks the game process again and installs with backup preflight.
Validation failure does not publish a bank or create recovery metadata. Read/file
exceptions return through bool/error rather than escaping the GUI/CLI call. The
core native importer also checks its output stream close; texture encoding and
bank serialization are unchanged.

`tools/test_texture_recovery.py` copies real game.bin/textures.big, preserves an
unowned temporary-file sentinel, refuses invalid images and both read/rename
locks, retries add + replace, verifies an unrelated exported texture byte-exact,
and Restores the full bank and inventory exactly. Normal
`build/texture-recovery-doioront` and ASan CLI `build/texture-recovery-i3chtle9` pass.
Normal editor/CLI targets build. This tests the shared file workflow; no new GUI
visual inspection or live-game texture rendering was performed.

## 2026-10-01 continuation: minimap texture/registry rollback

`build/minimap-recovery-932ujh3_/failure_report.json` reproduced a locked game.bin
leaving a new texture installed and consuming an unrelated textures.big.atlas-tmp.
Minimap baking now prepares the texture and both definition banks together. The
registry helper writes and reads back only prepared files; PendingBanks backs up
all originals before replacement and rolls back prior replacements if a later
bank fails. Standalone registration stages the definition pair the same way.
Success notes are published after installation, and the game process is checked
before committing. Minimap pixels, framing and registry serialization are unchanged.

`tools/test_minimap_recovery.py` locks the last bank, checks original hashes and
unowned-file preservation, retries full stock new-level creation, and verifies
exact Restore; it also exercises standalone registration refusal/retry/Restore.
Normal `build/minimap-recovery-d3f9nosd` and ASan CLI
`build/minimap-recovery-qshtarhw` pass. All 34 rebuilt normal suites pass (28.92s),
and the editor/CLI build passes. This is a three-bank minimap transaction, not a
transaction for all new-level stages; later failures can still require Restore.
No new GUI visual or in-game minimap rendering check was performed.

## 2026-10-01 continuation: strict minimap registration arguments

`build/minimap-cli-inputs-r8tfr13j/invalid_0.json` reproduced `minimap-register`
accepting `oops` as texture ID zero and changing game.bin. The CLI now parses the
entire unsigned 32-bit value and rejects unknown/trailing options before install
lookup or writes. Decimal, hexadecimal, octal and a single leading plus retain
their prior valid meaning; zero and UINT32_MAX remain representable.

`tools/test_minimap_cli_inputs.py` checks 17 malformed ID/option cases against
exact scratch-bank/inventory hashes, then six valid forms with exact Restore.
Normal `build/minimap-cli-inputs-qvbyzcty` and ASan CLI
`build/minimap-cli-inputs-we8i1kob` pass. This validates argument syntax and range,
not whether an arbitrary supplied ID names a suitable minimap texture.

## 2026-10-01 continuation: region-property bank rollback

`build/region-recovery-ez_o4rb2/failure_report.json` reproduced a locked final BWD
mirror leaving the WLD, primary BWD and root mirror changed. Region-property edits
now prepare and read back the WLD and each existing BWD copy in one PendingBanks
workspace, then back up all originals and install with rollback. Missing mirrors
remain absent. Notes appear only after commit; the shared writer checks close
errors, and the selected-install game guard runs before work and before commit.
The existing field-edit and binary serialization algorithms are unchanged.

`tools/test_region_recovery.py` verifies last-mirror refusal, all original hashes,
retry, unrelated WLD bytes, matching BWD copies, optional mirrors and exact Restore.
Normal `build/region-recovery-nbwtfrfc` and ASan CLI
`build/region-recovery-jil6pt_n` pass. All 34 rebuilt normal suites pass (20.09s),
and editor/CLI builds pass. No in-game region transition was exercised.

## 2026-10-01 continuation: world-edit file group rollback

`build/world-move-recovery-rcetgvgd/failure_report.json` reproduced a locked STB
leaving OrchardFarm's WLD/BWD copies, packed and loose creature positions changed,
and consuming both unrelated .atlas-tmp files. applyWorldEdits now prepares every
affected WLD/BWD, loose TNG, WAD and STB in one PendingBanks workspace. WLD/BWD,
replacement WAD entries and changed STB chunks are read back before backup and
replacement. A late failure rolls back earlier replacements. Invalid region edits
leave no recovery metadata; success notes publish after commit, and the game
process is checked at entry and before commit. Placement/terrain/TNG algorithms
are unchanged. Unaffected WAD/STB files no longer receive redundant backups; the
older overworld test now requires backups for every changed bank.

`tools/test_world_move_recovery.py` tests late-STB refusal, original hashes,
unowned temporary files, invalid owner/visibility requests, retry, WLD/BWD/STB
placement, packed/loose TNG agreement, creature coordinate deltas and exact Restore.
Normal packed `build/world-move-recovery-ii4pgkha`, normal extracted
`build/world-move-recovery-nrhv_nnn`, and ASan packed
`build/world-move-recovery-zdjg2yva` pass (the latter two include the added invalid
region and coordinate-delta assertions). All 34 rebuilt normal suites pass (26.50s).

The hidden World tab workflow also passes on ii4pgkha's restored copy: move
refusal, queued undo/redo, batch apply/move-back, ownership and visibility changes,
and exact Restore. Its `world_gui.txt.log` records RESULT PASS; w3_applied.png was
visually inspected and shows the new selected coordinates and mapped overview.
The fixture adapts the 400-map source count and disables optional seam stitching.
Stitching remains a separate subsequent operation, and no live-game transition
was performed. This rollback handles reported file errors, not power loss.

## 2026-10-01 continuation: world coordinate parsing and overflow bounds

`build/world-cli-inputs-twex1g9p/invalid_0.json` reproduced `2176tail` moving
OrchardFarm as coordinate 2176. `build/world-bounds-before.log` reproduced an
aligned near-INT_MAX X coordinate passing checkMove when its right edge overflowed.
The CLI now consumes complete signed decimal integers before looking up an install.
Move bounds and box contact/overlap additions use 64-bit arithmetic; zero/negative
map dimensions are refused. The established 32-unit alignment and 8192-unit world
extent are unchanged.

The world-draft suite checks legal edge placement, extreme X/Y, negative origins
and invalid dimensions. `tools/test_world_cli_inputs.py` checks 16 malformed
inputs, three extreme bounds, plus/leading-zero decimal input, a valid placement
and exact Restore. Normal `build/world-cli-inputs-904xe856` and ASan CLI
`build/world-cli-inputs-uuyyvh5_` pass, as do the focused normal/ASan unit suite.
All 34 rebuilt normal suites pass (24.88s); editor/CLI builds pass. The CLI test
uses copied WLD/BWD without terrain banks; packed/loose movement and the World tab
were exercised in the preceding transaction milestone.

## 2026-10-01 continuation: prepare complete terrain deployments

`build/terrain-deploy-recovery-1u13l9lp/failure_report.json` reproduced a locked
STB consuming unrelated WAD/STB .atlas-tmp files despite the old LEV rollback.
Terrain deployment now prepares the loose LEV, optional WAD entry and STB together
in PendingBanks. The navigation serialization helper leaves the live document
state alone; saved terrain, navigation baseline, sound-list state and success
notes publish only after installation. WAD entry and STB chunk read-back checks
precede replacement. New loose files keep creation-marker semantics, and game
process checks bracket preparation. Pack writes use the same rollback group for
LEV/chunk/record, replacing their manual rewrite-based undo. Terrain/nav/bake
algorithms are unchanged; external-world loose saves retain their existing route.

`tools/test_terrain_deploy_recovery.py` verifies locked-STB refusal, dirty draft,
original hashes, unowned temporary files, packed/loose LEV agreement, retry and
exact Restore. Normal new-loose `build/terrain-deploy-recovery-p1i_bhsh`, normal
extracted `build/terrain-deploy-recovery-ez6vkno6`, and ASan existing-loose
`build/terrain-deploy-recovery-bqdemjek` pass. Pack last-record rollback/retry and
unchanged source hashes pass in `build/terrain-pack-recovery-1uxy_4fk` and ASan
`build/terrain-pack-recovery-qfaipla1` via `tools/test_terrain_pack_recovery.py`.

All nine background terrain cases pass (`build/terrain-async-ff8_n56e`): later
strokes, map switches, competing writes, repeated saves, separate sculpt saves,
and painted theme output agree with references. Repeated game terrain/TNG saves
and two exact Restores pass (`build/created-restore-oy65n_v6`). All 34 rebuilt
normal suites pass (20.95s); core and locked-things suites pass under ASan. The
locked-things unit expectation now checks untouched caller notes/error/dirty
state instead of requiring the removed unconditional pack rollback message.
Normal and ASan editor/CLI builds pass. These are offline/hidden-editor checks;
no new live-game terrain or navigation validation was performed. Multi-map seam
stitching remains a sequence of separate terrain deployments.

## 2026-10-01 continuation: strict world visibility flags

`build/world-flag-inputs-gn_c43if/invalid_0.json` reproduced `region-props
--worldmap oops` hiding Greatwood and returning success. Region properties and
world-sees now require the documented literal 0 or 1. Region-property parsing
also refuses an option without a value, even after valid earlier edits, before
any install lookup/write.

`tools/test_world_flag_inputs.py` checks 23 malformed flag/incomplete-option
cases against exact copied-bank/inventory hashes, then both valid flag values
and exact Restore. Normal `build/world-flag-inputs-w0ifnvir` and ASan CLI
`build/world-flag-inputs-wlfyj6n1` pass. This change is confined to CLI argument
validation; the preceding 34-suite core gate remains applicable.

## 2026-10-01 continuation: bounded new-level placement

`build/worldinstall-bounds-before.log` reproduced suggestOrigin putting a donor
at the right edge beyond the world grid. The blank-level case in
`build/newlevel-placement-t2vxa_mp` reproduced an unaligned request changing
minimap banks before final installation refused it. A shared core placement
check now enforces positive dimensions, 32-unit origin alignment and the existing
8192-unit engine boundary. Both editor creation routes call it before minimap
writes; installLevel validates its actual common-record dimensions before world
mutation. The editor's extent constant now aliases the core constant. The origin
helper bounds its preferred scan, searches the remaining grid if needed, and
reports a full grid instead of returning an invalid fallback.

This reuses the established CWorld::Init / SetMapPlacement evidence documented
in EDITOR.md (0x4a6e30 / 0x4fc9c0 and the prior y=9024 transition failure); no new
engine limit or terrain algorithm is inferred. The new worldinstall unit suite
checks edge placement, invalid/extreme dimensions, alignment, full-grid refusal
and donor-width overflow. It passes normally and under ASan; all 35 rebuilt normal
suites pass (26.15s).

`tools/test_newlevel_placement.py` verifies eight donor/blank placement refusals
leave all copied banks and inventory unchanged. Normal
`build/newlevel-placement-a9ki2yiv` and ASan CLI `build/newlevel-placement-bam2l__u`
pass. Both fixtures also successfully create EdgePlacementProbe at (8128,8128)
with far corner (8192,8192), then Restore exactly (`edge_report.json`). Editor,
CLI and tools builds pass. This is placement preflight, not a transaction covering
all creation stages; later failures can still need Restore. No live-game boundary
transition was attempted.

## 2026-10-01 continuation: rollback for the core level installer

`build/worldinstall-recovery-nx0wdl36` reproduced changed world files and lost
fixed staging-file sentinels when the final STB replacement failed. The core
installer now prepares WLD, primary BWD and existing mirrors, WAD or new loose
LEV/TNG files, and STB in an exclusively owned directory. Candidates are checked
before replacement; requested backups must succeed and existing backup paths
must be regular files. A failed commit rolls back preceding replacements. If
rollback itself fails, the error reports a retained recovery directory.
TemporaryDirectory's explicit retention behavior has a focused unit check.

`tools/test_worldinstall_recovery.py` checks a locked final STB, original hashes,
unrelated staging occupants, successful retry, BWD mirror equality and exact
Restore. Packed normal `build/worldinstall-recovery-szqxx52z` and ASan
`build/worldinstall-recovery-ri5q6fju` pass, including refusal of a directory at a
backup path and byte-exact recovery from all six core `.bak` files. Those `.bak`
files are restored explicitly by the test; editor Restore does not consume them.

Loose normal `build/worldinstall-recovery-ooso6fax` also passes, preserving the
renamed WAD and checking both new loose files on retry. Existing blank-level
late-failure and marker-refusal checks pass in `build/newlevel-recovery-dpkcvfkp`
and `build/newlevel-recovery-lci3a6my`; the late-failure assertion now requires
new loose files to be rolled back, with exact Restore clearing their markers.

Normal editor/CLI/tools builds and all 35 regression suites pass (18.88s).
The focused worldinstall and temporary-directory suites also pass under ASan.

This groups the core world installation only: earlier minimap and later region
or entrance work remain separate stages. Failed rollback retention is unit
checked, but an actual rollback failure was not injected. No power-loss or
concurrent-writer guarantee is claimed. The standalone legacy forge-tools
installer still has its duplicated writer and is the next follow-up.

## 2026-10-01 continuation: one level installer for the legacy CLI

`build/legacy-worldinstall-7tr_cais` reproduced partial WLD/BWD/WAD replacement
and deletion of unrelated `.tmp` / `.tmp2` files after a locked-STB failure in
`forge-tools world install-level`. Its duplicated writer is replaced by a
Request adapter over forgecore worldinstall. Existing command options retain
their meaning, including dedicated-region creation and default `.bak` backups.
The command now inherits bounded placement, overlap/name checks, existing BWD
mirror updates, owned staging and rollback, and extracted-level routing.
Coordinates require complete decimal integers. Explicit optional inputs must
be readable, nonempty regular files; empty inputs cannot silently select donor
bytes in the core API.

The old core 141-region-cap notes and their editor suppression are removed.
The dedicated-region note now describes save caching, reusing the existing
2026-09-17 EDITOR.md/ENGINE_RULES.md findings (CWorldMap binary loader and the
successful fresh-game region-146 AtlasIsle probe). No region-allocation or
filler-selection algorithm changed, and no new runtime probe was performed.

The new `tools/test_legacy_worldinstall.py` checks a late file-lock failure,
malformed coordinates, missing/empty custom inputs, custom TNG preservation,
mirror equality, successful retry and exact manual `.bak` recovery in packed
and loose layouts. `test_pack_world.py` now owns a unique scratch directory;
it no longer deletes a fixed directory that another test run may own.

Packed ASan `build/legacy-worldinstall-xhs_2oc9` and normal loose
`build/legacy-worldinstall-6hmxizq4` pass, including byte-exact custom TNG
preservation and six/five baseline backups respectively. Both retry logs carry
the corrected save-cache note. Normal editor/CLI/tools and ASan tools builds
pass; all 35 rebuilt normal suites pass (22.21s).

The full own-region blank workflow also passes in
`build/newlevel-workspace-2ptwk4ao`: creation, entrance, minimap bake, hidden GUI
palette check, unowned-file preservation and exact Restore.

`build/pack-world-mh87u_5p` passes the full world-pack test: A/B/AB/BA
composition, byte-identical expected BWD output, new WAD/STB entries, hidden
GUI world move and new-level export, source preservation, and reverted-override
cleanup. Its GUI script reports PASS; the retained `pack_world_view.png` was
visually checked for the selected (2048,8064) map and PackG destination.

## 2026-10-01 continuation: strict new-level numeric options

`build/newlevel-inputs-e3p6bfxh/0.json` reproduced `new-level --at
6400,6400tail` succeeding and changing the copied install. The two creation
commands now parse complete decimal origin pairs before install lookup. Blank
creation also parses complete size pairs, finite heights and bounded numeric
palette slots. Trailing text, overflow, whitespace, incomplete pairs and empty
explicit values are refused; leading plus signs and leading decimal zeroes
remain valid. Named themes retain the existing palette lookup.

The editor creation API separately refuses non-finite heights and theme slots
outside -1 (automatic) or 0..255 before template work. The worlddraft suite
checks those API refusals with an empty owned scratch root and no theme library,
requiring no files or success notes. Normal and ASan focused suites pass.
`tools/test_newlevel_inputs.py` checks 43 malformed options against file hashes
and inventory, then creates a valid signed/scientific-height blank level and
requires exact Restore. No terrain serialization or bake algorithm changed.

Normal `build/newlevel-inputs-swqn1qvb` and ASan
`build/newlevel-inputs-6mblu59s` pass all 43 refusals, valid creation at
(+6400,06400) with size +64x064 and height +1.25e1, and exact Restore. Normal
editor/CLI/tools builds and all 35 regression suites pass (27.98s).

## 2026-10-01 continuation: validate world registration before minimap writes

`build/newlevel-preflight-e5pjgq27/new-level_0.json` reproduced duplicate-name
creation returning failure after changing textures.big and creating seven
backup files. Core `validateRequest` now executes the same read-only container,
name, bounds, collision and in-memory region-registration checks as installLevel,
returning before staging, backup creation or creation-marker callbacks. Both
editor creation routes invoke it before minimap writes. Blank creation supplies
its authored common record and payloads so validation uses its actual dimensions.
The installer repeats the checks at write time; this is not a slot reservation
or a prediction of later filesystem failures.

`tools/test_newlevel_preflight.py` checks duplicate names, invalid stems,
overlap and missing takeover regions in both donor and blank creation, with
additional loose-file collisions for extracted installs. Every refusal must
leave all original hashes and the entire file inventory unchanged, including
minimap banks. A valid own-region blank creation and exact Restore follow.
No world-registration or minimap encoding algorithm changed. Later I/O or
terrain-preparation failures can still require Restore for separate stages.

Normal packed `build/newlevel-preflight-0858ycd3` passes eight refusal cases;
ASan loose `build/newlevel-preflight-hzmzbfxo` passes ten, including unrelated
loose-file collisions. Both also pass valid own-region creation and exact
Restore; the loose run preserves its renamed WAD. All 35 rebuilt normal suites
pass (23.13s), with editor/CLI/tools builds successful. Remaining stale cap and
retail-texture-slot workaround paragraphs in EDITOR.md are reconciled with its
2026-09-17 findings; PLAN.md labels its old cap investigation as superseded.

## 2026-10-01 continuation: checked standalone PNG publication

`build/image-exports-86s5wk4_/minimap-bake_locked.log` reproduced minimap-bake
printing "wrote" and returning success when its destination was locked against
writes. A shared terrainexport::writePng helper now encodes to an owned candidate,
checks write and close, and uses PendingBanks to publish the complete file.
Texture export (including the GUI action) and minimap-bake use it. A failed
replacement keeps the previous destination; the shared rollback-failure path
retains recovery files and reports their location. No PNG encoding or minimap
pixel algorithm changed, and no backup is added for a standalone export.

`tools/test_image_exports.py` checks locked output, directory targets and a
regular file occupying the parent path for both commands. It requires no false
success message, preservation of prior output and unrelated siblings, successful
retry and deterministic overwrite. Successful PNGs are checked independently
for chunk CRCs, IEND, dimensions and complete zlib row data. Copied source
banks must remain byte-identical and owned staging must be cleaned.

Normal `build/image-exports-2soa519w` and ASan CLI
`build/image-exports-kcypwqpo` pass. Editor/CLI/tools builds and all 35 rebuilt
normal suites pass (20.50s). The GUI uses the same texture-export function;
its file picker was not exercised in this pass.

## 2026-10-01 continuation: strict texture-command options

`build/texture-cli-inputs-di0nm4pm/0.json` reproduced texture-add silently
accepting `--typo`, returning success and changing textures.big. The texture
list/add/replace/export parser now refuses unknown options, missing or empty
values, extra positional arguments and empty required names/paths before install
lookup or writes. `--bank` is accepted only for list/add, and `--format` only for
add, matching their documented behavior; they no longer silently do nothing for
replace/export.

`tools/test_texture_cli_inputs.py` checks 43 rejected argument combinations,
requiring unchanged source hashes, file inventory and a prior export file. It
then exercises valid add, replace, export and bank-filtered listing, followed by
exact Restore. The core texture writer and GUI behavior are unchanged.

Normal `build/texture-cli-inputs-lw16wal2` and ASan CLI
`build/texture-cli-inputs-hyhj07cu` pass. Both CLI builds pass. The preceding
35-suite export gate (20.50s) covers the unchanged core; it was not repeated
for this CLI-only argument change.

## 2026-10-01 continuation: reject malformed minimap framing

`build/minimap-inputs-4fnkjjv4/0.json` reproduced minimap-bake accepting
`--framing 1,0,0tail` and replacing the prior PNG. Framing now requires exactly
three complete finite numbers and a positive scale. The command rejects
unknown/incomplete options, extra arguments, empty required values and combined
--region/--framing before lookup or export. Signed/scientific values remain
supported. bakeMinimapImage separately rejects invalid framing before template
work, including values loaded from a WLD or supplied by other callers.

`tools/test_minimap_inputs.py` passes 23 refusal cases while preserving an old
export, then verifies default and signed explicit framing produce identical PNGs
and region lookup succeeds. Normal `build/minimap-inputs-ul63jbyd` and ASan CLI
`build/minimap-inputs-xvi78an3` pass with unchanged copied banks. The worlddraft
suite covers six API framing refusals before file/pixel creation and passes in
normal and ASan builds. All 35 rebuilt normal suites pass (20.77s), and normal
editor/CLI/tools builds pass. No minimap transform or pixel algorithm changed.

## 2026-10-01 continuation: region text uses the core WLD writer

`build/region-text-inputs-96l54dcy/invalid_0.json` reproduced region-props
accepting an embedded quote and changing the world banks. Region display names,
definitions and minimap graphics now go through wld::File::setRegionText before
staging. The existing quoted-text rejection is reused, with NUL also refused.
Bare region values reject whitespace, quotes, semicolons and NUL; empty values
retain the core clear-field contract. Prepared WLD read-back also checks the
requested field values, beyond preserving its serialized text. Flag/framing
line edits and the rollback group remain unchanged.

The core worldinstall suite checks rejected strings leave the in-memory WLD
unchanged, including embedded NUL, and verifies apostrophes/quoted semicolons and
empty minimap clearing round-trip. It passes normally and under ASan.
`tools/test_region_text_inputs.py` passes twelve invalid cases, valid punctuation
and exact Restore: normal `build/region-text-inputs-by1qrual`, ASan
`build/region-text-inputs-6e9zbya0`. Existing region rollback, unrelated-WLD-byte,
optional-mirror and exact-Restore checks pass in normal
`build/region-recovery-sdeav56f` and ASan `build/region-recovery-u9ivee17`.
All 35 rebuilt normal suites pass (26.61s); editor/CLI/tools builds pass.

The full own-region blank workflow passes in
`build/newlevel-workspace-3u29cnrk`, including filler takeover, minimap and
entrance generation, hidden GUI palette loading and exact Restore. This
exercises empty minimap preflight followed by the populated minimap token.

## 2026-10-01 continuation: full texture-tab round trip and Restore

`tools/test_textures.py` now owns a unique scratch install, script, PNG outputs
and screenshot, passes the source root explicitly to the GUI, requires its
RESULT PASS marker, and verifies exact Restore of every copied bank. It no
longer deletes a shared fixed scratch tree. Export by name and ID must be
byte-identical, and replacement still checks decoded pixel drift.

`build/textures-ui-ib1ah798` passes the hidden GUI browse, selected-object texture,
PNG export, slot replacement and DXT3 append workflow. The retained screenshot
`ui/t1_textures.png` was visually checked: the new 512x256 ATLAS_UI_TEX is selected
with the barrel texture preview and scratch write destination visible. The
appended ID is 6292; export by label and ID matches exactly. Mean absolute RGB
replacement difference is 0.020551, below the established 2.0 threshold. Restore
returns all copied files and inventory exactly (`report.json`). The final
name/ID comparison was also checked against the retained outputs. No production
code changed in this pass; the preceding 35-suite gate remains applicable.

## 2026-10-01 continuation: prepare donor chunks before minimap writes

`build/donor-prepare-rx7gp3se/failure_report.json` reproduced failed donor
translation changing textures.big and game.bin and leaving three backup files.
Copy creation now completes the existing terrain translation before baking its
minimap. No translation or minimap encoding algorithm changed.

`tools/test_donor_preparation.py` substitutes a truncated terrain chunk in an
owned STB, verifies refusal leaves every file and hash unchanged, restores the
valid donor, creates its own-region copy with a minimap, then checks exact
Restore. Normal `build/donor-prepare-npa47atr` and AddressSanitizer
`build/donor-prepare-4b49343g` pass. The rebuilt normal 35-suite gate passes in
28.26 seconds. Later creation stages remain separate operations; this is an
earlier preparation boundary, not a transaction across all creation stages.

## 2026-10-01 continuation: strict and bounded stitch feather inputs

`build/stitch-workspace-zjwn5uwt/invalid_0.log` reproduced `--feather oops`
being accepted as zero. world-stitch now requires complete signed 32-bit decimal
integers or auto, and rejects unknown, empty and incomplete options. Negative
integers retain automatic selection. The feather loop stops beyond both map
grids, retaining the requested falloff; its denominator uses wide addition.
Automatic selection clamps before converting to int. The seam averaging and
ordinary falloff remain unchanged.

The extended `tools/test_stitch_workspace.py` passes eleven invalid cases and
six valid boundaries (including both signed limits) without dry-run writes.
It then deploys a seam, verifies matching edge heights, preserves unrelated
scratch files and Restores the exact original inventory. Stock normal
`build/stitch-workspace-jb_r807m` and extracted ASan
`build/stitch-workspace-1h7aua5a` pass. Six normal seam samples match the retained
pre-change `stitch-workspace-fvp941k4` report exactly. All 35 rebuilt normal
suites pass in 28.34 seconds. This is offline validation; multi-map stitch
deployment still consists of separate map writes.

## 2026-10-01 continuation: publish ground diagnostic PNGs together

`build/ground-exports-qysgbxcd/locked_first.log` reproduced ground returning
success and announcing all three PNGs despite a locked first output. The CLI
now encodes and closes all three files in an owned .forge-ground-export-*
workspace before publishing through PendingBanks. A later replacement failure
rolls back earlier ones; success is printed only after the complete group lands.
The existing output names, current-directory destination and image encoding stay
unchanged. These standalone exports do not create install backups.

`tools/test_ground_exports.py` passes first/last output locks, a directory at
the last destination, retry, deterministic overwrite, unrelated temporary-file
preservation and exact source hashes/inventory: normal
`build/ground-exports-w7zqmxz1`, ASan `build/ground-exports-8wf_h4py`.
The retail background check now owns a unique output directory;
`build/world-background-nea4plro` passes with all 192512 uncovered pixels
matching the LEV fallback and zero false black pixels. This CLI-only change
reuses the preceding full 35-suite gate; both CLI builds pass.

## 2026-10-01 continuation: complete numeric inputs for map exports

`build/export-inputs-yoisa43h/invalid_0.json` reproduced `--texels oops`
being accepted and replacing the previous GLB. The export parser now requires
complete signed 32-bit integers for texel/texture limits, finite complete floats
for tile/gain, exactly two finite origin coordinates and a y/z up axis.
Value-taking options reject empty, missing or following-option values before
install lookup or output work. Existing numeric clamps/defaults are unchanged.

`tools/test_export_inputs.py` passes 49 refusals preserving the old GLB, then
valid signed/scientific inputs. Its GLB passes container, accessor, index and
bounds checks (4225 vertices, 8192 triangles), including the requested Z-up
origin (16,-32). Copied input hashes/inventory remain unchanged. Normal
`build/export-inputs-ipsg_jkz` and ASan `build/export-inputs-khfbpafk` pass.
The retail smoke runner now accepts an explicit source install;
`build/export-numeric-smoke-epyylx5z/smoke.log` passes eight textured maps,
including two with things/foliage, and independent trimesh loading for all eight.
This CLI-only parser change reuses the preceding 35-suite core gate.

## 2026-10-01 continuation: model geometry and sidecars publish together

`build/model-export-recovery-_pg0ai4m/glb_locked_metadata.log` reproduced export
success despite a locked themes.json, after replacing the GLB and writing PNGs.
Both terrain-only and terrain-plus-layer GLB/OBJ writers now prepare their
complete declared output set through a shared publishExport helper. It preserves
basenames and relative material/image references, checks every stream write and
close, rejects outputs outside its owned workspace, deduplicates declared paths
and publishes through PendingBanks. Failure rolls back earlier replacements;
failed rollback retains and reports recovery files. Existing unrelated files
are preserved. Exports do not create install backups. This is one-map output
publication; multi-map batches remain separate exports.

`tools/test_model_export_recovery.py` passes blocked metadata for GLB and OBJ,
exact previous-file/inventory preservation, retry, structural GLB checks,
independent OBJ loading, deterministic overwrite and copied-source preservation.
Normal `build/model-export-recovery-mvnw4rgf` and ASan
`build/model-export-recovery-8wx7oko_` pass, as does the focused export suite in
both builds (including textured synthetic foliage OBJ append).
`build/model-export-smoke-bllysgse` passes eight textured retail maps with
independent trimesh loading. Every GLB is byte-identical to the retained outputs
from before this publication change.

Hidden GUI export `build/model-export-gui-_dvdkcy2` passes and produces a valid
Greatwood_1 GLB (18721 vertices, 36864 triangles, 384x768 albedo). Its export
screenshot was visually checked. A locked-sidecar GUI check in
`build/model-export-gui-refusal-vze2o3ct` reports export_ok=0 and Export failed,
retains both prior outputs and leaves no new files; its screenshot shows the
failure and filename in the activity/toast area. All 35 rebuilt normal suites
pass in 18.84 seconds. No live-game files or runtime behavior were exercised.

## 2026-10-01 continuation: OBJ water no longer corrupts later layers

A synthetic terrain/water/ice/foliage/object fixture reproduced two OBJ bugs:
water's position-only vertices shifted subsequent geometry, but appendObjLayer
used one shared position/UV/normal base; and the terrain map_Kd line followed the
ice material declaration, leaving terrain untextured. The failing export-suite
log is retained in `build/obj-water-baseline-d90h9n1s/test.log`.

OBJ append now tracks position and attribute bases separately across every
instance and layer, counting emitted water positions only in the former. The
terrain albedo declaration now precedes water/ice material declarations. The
synthetic test verifies distinct indices through two appended layers, bounds,
and terrain material ownership. The focused suite passes normally and under
ASan. No geometry generation, GLB output or game-file writer changed.

`tools/test_obj_water_export.py` exports TeleporterGreatwood with water and
foliage, checks every OBJ index, opens every referenced diffuse PNG and loads
the result independently through trimesh. Normal `build/obj-water-export-l0vf5w5x`
and ASan `build/obj-water-export-mq7kjuj8` pass: 148985 positions, 148257 UVs and
normals, 728 water-only positions, 123550 appended faces and 17 loaded geometries.
Both writer builds and all 35 normal suites pass (28.72 seconds). Tests read the
retail source and write only owned build outputs; no live-game check was run.

## 2026-10-01 continuation: explicit origins move foliage with the map

`build/export-origins-y7i1xnng` reproduced --origin 16,-32 leaving OrchardFarm
foliage at its baked world coordinates while terrain and placed things used the
requested offset. The first Y-up foliage translation changed from
(73.2373,46.9174,-6.0686) to (3273.2373,46.9174,-3174.0686), rather than the
requested (16,0,32) delta. Explicit-origin exports now load map-local foliage
and add the same Fable-space offset used for terrain and things. The loader
comment distinguishes baked world coordinates from arbitrary export offsets.
Default local and --world behavior retain their existing branches.

`tools/test_export_origins.py` compares every terrain vertex and every foliage/
thing node translation in both Y-up and Z-up GLBs, preserving rotations/scales.
It also checks --origin retains precedence when --world is supplied. Normal
`build/export-origins-gv80waea` and ASan `build/export-origins-7q3g796s` pass:
17148 foliage nodes and 227 placed-thing nodes per axis. These are read-only
retail exports into owned build directories. This CLI-only behavioral change
reuses the preceding full core gate; both CLI builds pass.

## 2026-10-01 continuation: GUI smoke owns its output evidence

`tools/ui_smoke.py` now copies its automation script into a unique build/ui-smoke-*
workspace and redirects screenshots/GLB there. It no longer deletes files in
shared build/ui or rewrites the checked-in script log. Source install and binary
paths can be explicit; the hidden GUI's captured output and a JSON report stay
with its evidence.

`build/ui-smoke-cl6q3jru` passes the complete button-driven smoke, all screenshot
pixel assertions and structural Greatwood_1 GLB checks (18721 vertices, 36864
triangles, 384x768 albedo). Hashes/inventory of all 326 pre-existing shared UI
files remain unchanged. The normal GUI/tools rebuild also passes. No production
behavior changed in this test-harness pass.

## 2026-10-01 continuation: mod picks commit before the UI changes

`build/mod-pick-recovery-wmnpwe4s/locked_add.txt.log` reproduced a locked picks
file still reporting success and changing mods_picks from 0 to 1. Saving conflict
choices now prepares and closes an owned candidate, or stages removal for an
empty selection, through PendingBanks. The GUI adopts the candidate choices
only after persistence succeeds. Errors retain the earlier displayed choice and
file, and reach the Activity log. Back to retail also checks that result before
reporting success. The existing running-game/write guard remains in place.

`tools/test_mod_pick_recovery.py` creates two small conflicting packs in an owned
scratch root. `build/mod-pick-recovery-axfkjif7` passes locked add, retry, locked
last-pick removal and successful removal through four hidden GUI sessions.
State assertions verify the old choice count stays unchanged on refusal; exact
picks bytes and an unrelated .tmp file are preserved, and no owned staging
workspace remains. The locked-add screenshot was visually checked: PackB remains
the load-order winner, with a visible save error. This minimal scratch fixture
does not contain graphics.big, so model-health checking is unavailable there;
that warning is unrelated to the exercised file conflict. The GUI rebuild
passes; preceding core/ASan gates remain applicable to unchanged core code.

## 2026-10-01 continuation: Mods diagnostics fit narrow cards

The mod-pick failure screenshot exposed an unwrapped model-health warning
running beyond the Conflicts card. Load-order errors and model-health diagnostics
now wrap to the card width. Winner controls are bounded by that width; long
winner names are available on hover, and read-only lip-sync winners use a fitted
label with their full name in the tooltip.

`build/mods-warning-layout-uy7wnozm` passes hidden GUI captures requested at
1440x900/1.0, 1024x600/1.5 and 800x600/1.5. Separate compact captures scroll the
winner fully into view and show its long-name tooltip. Screenshots were visually
checked: the complete warning fits when revealed, the combo stays within the
card and the full winner name is readable on hover. The GUI rebuild passes.
This presentation-only pass reuses the preceding behavior/core checks.

## 2026-10-01 continuation: preserve the mod order on save failure

The new synthetic mod-order check reproduced JSON serialization failure erasing
the prior forge_mods.json. `build/mod-order-baseline-l46acp_i/test.log` retains
the failure. Core modorder::save now serializes before filesystem mutation,
closes a complete candidate in an owned .forge-mod-order-* directory, then
replaces the target. If replacement fails after saving the old file, it restores
that file; failed rollback retains and reports recovery files. Directory targets
are refused, and unrelated temporary filenames are never reused.

The mod-pack unit suite passes invalid UTF-8 refusal, a replacement lock that
still permits in-place writes, unchanged file inventories and valid retry,
normally and under ASan. `tools/test_mod_order_recovery.py` passes locked CLI
add/remove/move/disable and retry: normal `build/mod-order-recovery-6laonr4q`,
ASan CLI `build/mod-order-recovery-kvfmupsv`. The normal run also clicks the GUI
enable and reorder controls while locked, then retries after unlocking. The
failure screenshot was visually checked: both original enabled rows remain in
order with visible save errors. All copied metadata bytes and the unrelated
.tmp file survive refused edits. Normal writer builds and all 35 suites pass
(19.08 seconds). No game banks or live runtime were changed.

## 2026-10-01 continuation: validate mod commands before mutation

`build/mod-cli-inputs-j0np6i7b` reproduced `mods undeploy --typo`
silently reverting a deployed stage. The load-order command family now validates
all arguments before metadata edits or stage reversal. Unknown/extra options,
missing values and partial/overflowing decimal indices fail with usage status 2.
Supported JSON output works regardless of option order.

`tools/test_mod_cli_inputs.py` passes 37 refused inputs with exact deployed-root
hash inventories, plus valid list/build/conflicts, undeploy, add, move and remove:
normal `build/mod-cli-inputs-ondb9tz_`, ASan `build/mod-cli-inputs-8po7e9a1`.
Concurrent independent conflicts/deploy/undeploy and malformed-patch cleanup pass
at `build/mod-workspaces-oxvui_r0`. Both CLI builds pass. This parser-only change
reuses the preceding 35-suite core gate. Deploy still reverts its old stage before
building; this change does not make the complete deployment transactional.

## 2026-10-01 continuation: read mod decisions before reverting a stage

`build/mod-picks-inputs-bak5gyhx` reproduced a missing --picks file silently
rebuilding the deployed stage with load-order defaults. The shared CLI choice
reader now rejects non-files, failed opens and failed reads. Deploy loads the
order, sources and choices before reverting its current stage, and passes the
loaded choices into the build instead of reopening the file after reversal.
An absent implicit default remains an ordinary no-choice build.

`tools/test_mod_picks_inputs.py` passes missing/directory/locked explicit choices,
a directory at the default path, malformed order preservation, a valid explicit
winner retry, no-choice fallback and byte-exact undeploy. Direct defs/qst merge
also refuse missing choices without touching existing outputs. Evidence:
normal `build/mod-picks-inputs-xjpxbl3c`, ASan `build/mod-picks-inputs-_bwwh_d5`.
The 37-case mod argument regression still passes at `build/mod-cli-inputs-lktsvt0x`.
Both CLI builds pass; unchanged core gates are reused. Later deployment failures
can still leave the install at baseline; this is decision-input preflight, not
a transactional deployment rewrite.

## 2026-10-01 continuation: keep deploy JSON parseable

`build/mod-cli-inputs-bz52ybl2` reproduced a successful redeploy whose stdout
could not be parsed as JSON: the restored-stage message preceded the report.
JSON mode now sends that diagnostic to stderr and emits a JSON result when no
mods are enabled. Human-readable commands retain their existing output.

The extended `tools/test_mod_cli_inputs.py` passes at
`build/mod-cli-inputs-x6wwtlgl`: full stdout JSON parsing for redeploy and empty
orders, expected stage counts, winner content, byte-exact undeploy, plus all 37
malformed-input preservation cases. The normal CLI build passes. This output-only
change reuses the immediately preceding sanitizer/core gates.

## 2026-10-01 continuation: preserve externally edited conflict choices

`build/mod-pick-refresh-3k185jt1` reproduced the GUI overwriting a choice added
externally after its conflict report loaded. Each choice edit now reads a fresh
candidate from disk and only adopts it after the existing recoverable save
succeeds. Failed reads preserve the cached display and refuse the edit. Switching
save roots explicitly clears old-root choices. Parsing matches CLI whitespace
and equals/tab separator handling.

`tools/test_mod_pick_refresh.py` passes at `build/mod-pick-refresh-jo7qlaiy`:
external choices survive an edit, and a lock that denies reads while allowing
replacement refuses an edit with byte-exact file and cached-choice preservation.
The failure capture at `build/mod-pick-refresh-jl9bgerq/unreadable.png` was visually
checked: prior winner and a readable error remain. The earlier locked-write,
retry and removal regression passes at `build/mod-pick-recovery-x8uq2f07`.
Normal GUI build passes; unchanged core/sanitizer gates are reused. Concurrent
writers during the brief read-to-replacement interval remain unsupported.

## 2026-10-01 continuation: unreadable orders are errors, not empty orders

`build/mod-picks-inputs-3ndk7ij5` reproduced deploy treating a read-locked
forge_mods.json as an empty order and reverting all staged files. Core load now
returns an empty order only when the file is absent; non-files, failed opens and
read failures throw. Existing callers surface the error before saving or deploy
stage reversal.

The mod-pack unit suite covers missing, directory and read-locked orders with
unchanged bytes, normally and under ASan. The extended decision-input scratch
check refuses locked-order deploy/list/add without changing the deployed root:
normal `build/mod-picks-inputs-fi7sdg_9`, ASan `build/mod-picks-inputs-xxt7n_om`.
Normal all-target rebuild and all 35 suites pass (20.41 seconds). Sanitizer CLI
and focused mod-pack builds/checks pass. The absent-file first-use behavior is
unchanged; no live game files were touched.

## Audit rule

For each row, record a concrete behavior, the tested map or asset, the saved
artifact if applicable, the visual result, undo/restore result, and any known
gap before treating it as reviewed. New findings go into this file and their
fixes into the existing handoff or editor documentation.
