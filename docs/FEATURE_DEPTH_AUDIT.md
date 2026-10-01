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

## Audit rule

For each row, record a concrete behavior, the tested map or asset, the saved
artifact if applicable, the visual result, undo/restore result, and any known
gap before treating it as reviewed. New findings go into this file and their
fixes into the existing handoff or editor documentation.
