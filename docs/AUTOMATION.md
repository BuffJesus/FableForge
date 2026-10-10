# GUI automation (`--auto <script>`)

`tools/test_mod_file_content.py --install <root>` independently verifies raw-file
baseline/contributor sizes and SHA256, identical edits, absent baselines and
composition under existing label choices. Normal/compact cards show the complete
name/hash inside their actual clipping rectangle. A same-size source update
leaves the cached snapshot alone until Check conflicts refreshes it; refreshing
  does not rewrite choices. Reviewed file choices reject changed content until
  reselection or clearing; the fixture verifies refusal and renewed review at
  both sizes. `mods_stale_file_choices` counts changed reviews in reported file
  conflicts. `--gui <exe> --cwd <directory>` supports packages.
`dump_widget` records `widget_clip_rect` for ordinary registered items as well
  as their bounds; `reveal` scrolls through nested child and parent windows,
  adjusting the eventual item position at each level.

`tools/test_dialogue_workspace.py --install <root>` checks the Dialogue picker,
head preview, playback, scrubbing, staged edits and independently read exports
at normal/compact sizes. A stopped seek to the slider's right edge must set
`dialogue_at_end`; explicit Play must restart. The slider displays two decimals
without rounding its stored position. `--gui <exe> --cwd <directory>` exercises
an extracted package from an unrelated directory. Exports stay in owned scratch.

`tools/test_texture_replace_failure_ui.py --install <root>` clicks the actual
replacement form with a sharing-locked PNG; `--invalid` uses undecodable input.
Both sizes verify a persistent `texture_replace_error` after toast dismissal and
closing/reopening the form, complete copied-install preservation, unlocked/fixed
same-process retry, independently decoded blue export pixels and exact Restore.
Selecting a different texture clears state `texture_replace_failed`. Input images
and source banks stay unchanged; `--gui <exe>` selects extracted-package binaries.

`--add` and `--add --invalid` exercise the actual Add form with a small valid
bank containing one retail record and two native seed textures. They check
`texture_add_failed`, `texture_add_name` and `texture_add_error`: a failed draft
survives asset-tab/browser-selection changes, edits clear its error, an accepted
destination change clears it, retry selects the added texture and clears its
name, decoded pixels are correct, and Restore returns the complete inventory.
`--small-bank` selects that fixture for replacement checks too; replacement
defaults still use the full copied retail bank. Compact input steps reveal the
field twice and assert the entered name before attempting Add.

`tools/test_mod_action_error_ui.py --install <root> --mode redeploy|report|missing`
checks actual Mods buttons at normal/compact sizes. A bad patch after a successful
deploy restores the baseline before failing; malformed or incomplete exit-zero
reports and missing tools retain a wrapped error after dismissing toasts,
reopening Mods and rescanning the same Data folder. State `mod_action_failed`
and widget `mods_action_error` expose the error. Changing the save folder clears
it; fixing the input/helper and retrying succeeds in the same process. Complete
copied-install inventories, source pack/bank hashes and unrelated files remain
preserved. `--gui <exe>` supports extracted-package checks.
`--mode fmp --fmp <package>` uses unsupported named-link metadata to exercise
the same refusal, persistent feedback, unchanged inputs and retry lifecycle.

`tools/test_egocore_unicode_paths.py --install <root>` compiles an owned failure
helper and checks its actual argv through Unicode application, compiler-env,
text-env and temporary paths. Its nonzero exit refuses even with both partial
bank files present. Missing/locked compiler startup and unlocked retry preserve
the complete install/output and unowned workspace inventories. `--exe <tool>`
supports extracted-package checks; the helper is built with this checkout's GCC.
`tools/test_egocore_workspace.py --unicode --mod <ControllerSupport-folder>`
checks concurrent real compiles at Unicode/metacharacter compiler, install,
output and temporary paths. It also checks failed build/deploy/conflicts,
malformed inputs and DLL-only compatibility. The legacy batch failure helper
uses a separate ASCII temporary tree in this mode.

`tools/test_texture_unicode_ui.py --install <root>` checks Unicode PNG inputs
with CLI add/export and GUI replacement/export at both sizes. Invalid and
sharing-locked inputs preserve the bank inventory; valid red/blue images are
decoded independently, and Restore returns the complete copied install exactly.
Image inputs and source bank hashes stay unchanged. `--gui <exe>` selects
extracted-package binaries; all working files remain under the owned build tree.

`tools/test_unicode_paths.py --install <root>` checks Windows CLI arguments and
packaged GUI helper/schema lookup with accented, Greek and CJK characters,
literal percent/ampersand characters and an unrelated working directory.
It copies only required definition banks into owned loose-level installs.
Actual Mods buttons deploy and undeploy an owned Unicode-path file pack at
normal/compact sizes, with complete inventory recovery and unchanged source
pack/bank hashes. `--gui <exe>` selects extracted-package binaries.

`tools/test_texture_export_failure_ui.py --install <root>` clicks Export PNG
while its existing output is held with a Windows sharing lock. Both sizes check
the wrapped `texture_export_error` after toast dismissal, unchanged output and
unrelated files, staging cleanup, and same-process retry with decoded green pixels.
Compact also verifies a different texture selection clears the error. State:
`texture_export_failed`. `--gui`/`--cwd` support extracted-package checks.

`tools/test_texture_destination_ui.py --install <root>` opens Textures before
changing the save folder. Both sizes check row/preview refresh for a different
bank, selection retention for the same bank and fallback to the selected install.
The fixture prepares copied banks first; GUI navigation changes no target files.
`--gui`/`--cwd` support extracted-package checks.
It gives the same texture name red pixels in the selected bank and blue pixels
in the other bank. Actual Export PNG buttons and screenshot pixels verify the
preview and export follow the selected, alternate and fallback bank.

`tools/test_mod_asset_refresh_ui.py --install <root>` loads asset views, then
clicks Mods deploy/undeploy for a cube recipe. Both sizes check retired selections,
one added/removed model and texture, definition users and exact bank recovery.
`--separate-root` writes to a copied save folder while preserving the selected
install's whole inventory. `--gui`/`--cwd` support extracted-package checks.
`--failed-redeploy` adds an invalid bank patch after deploying the cube. The
previous stage reverts, the new build fails, and asset views return to the restored
baseline while the failure stays in the log. Both destination variants are checked.

The Setup recovery fixtures below accept `--gui <exe>` and `--cwd <directory>`
for extracted-package checks; their scratch files and app-data remain owned
under this checkout's build directory. The stage fixture uses `forge-tools.exe` beside
the selected GUI executable. The package check exercises all recovery variants.

`tools/test_setup_confirmation_ui.py` checks Enter leaves Restore pending,
Escape cancels without closing Setup, and Continue or an accepted folder change
cancels the pending Restore. Rescanning the same root through Data preserves the
confirmation. Both sizes verify both installs' entire inventories are unchanged.

`tools/test_setup_stage_restore_ui.py` stages an owned LEV and takes an editor
original after staging, then checks Setup rebases and restores the older baseline.
`--orphan` preserves the manifest outside the install: Restore leaves all files
unchanged and shows a persistent warning. The fixture returns that exact manifest
and retries in the same process. Both variants cover normal and compact panels.

`tools/test_setup_restore_ambiguity_ui.py` clicks Setup Restore with either
different legacy/current originals or a creation marker plus an original.
Both sizes verify a visible refusal and unchanged whole-install inventory,
including an unrelated edited map. The fixture moves the conflicting record
outside the install, preserving its bytes, then retries in the same process.

`tools/test_setup_missing_bank_ui.py` restores an absent game.bin from its
tracked original. `--missing-levels` instead restores the only loose LEV;
`--wad <source.wad>` copies a stock WAD as the tracked original and verifies its
unchanged source hash. Both sizes click actual Setup buttons; compact selects
Data directly. Each case first checks refusal while an owned sleeping process
named Fable.exe runs at the install root, then recovery after it exits.

`tools/test_setup_restore_failure_ui.py` locks an owned LEV, clicks Setup Restore,
checks the persistent wrapped `setup_restore_error` message with toasts cleared,
then releases the lock and retries in the same hidden process. Both sizes check
unchanged failed-write bytes, temp cleanup and exact recovery; the compact case
also checks error clearing on a folder switch. State: `setup_restore_failed`.
`--partial` adds a second selected map that restores and reloads while the locked
target remains unchanged. The inline error survives that reload and retry
finishes the remaining target; every original is retained.

`tools/test_setup_asset_restore.py --install <root>` imports a cube into copied
graphics/textures/game/names banks, then uses Setup's Restore buttons at both
panel sizes. It checks retired model/texture selections and counts, refreshed
definition users, exact bank hashes, retained original backups and unchanged
source/loose-level bytes. `--gui`/`--cwd` support extracted-package checks; app-data
is owned by the fixture. `dump_state` writes state lines into the automation log.

`tools/test_mod_unreported_choices.py --install <root>` checks saved choices
absent from the latest report, including missing targets and uncontested edits.
Both panel sizes cover search and read-only preservation; real Clear buttons
cover individual removal, locked-save failure and a zero-conflict report. It
also removes every mod and verifies an empty-order report and search beyond
400 saved choices without changing source files. States:
`mods_unreported_choices`, `mods_unreported_matching`. Widgets:
`mods_unreported_help`, `mod_unreported_choice_<index>`,
`mod_clear_unreported_<index>`. It accepts `--gui`/`--cwd` and owns its app-data
directory for hidden-process isolation.

`tools/test_mod_long_values.py --install <root>` checks long Vector_J/CCharString
and signed CDefString reference baseline, current and contributor values at both sizes, exact byte escapes,
independent merged payload bytes and source hashes. Value targets:
`mod_field_value_<row>_<field>`, `mod_field_base_<row>_<field>` and
`mod_field_source_<row>_<field>_<source>`.
The field-details, long-value, conflict-search and unavailable-choice fixtures
accept `--gui <exe> --cwd <directory>` for extracted-package checks; scratch
evidence still belongs to the script's checkout.

`tools/test_mod_field_choice_contributors.py` checks direct definition merge,
direct mod merge and registered build refusal for unavailable contested scalar
choices, read-only review flags, exact selected/baseline values, inactive layouts
and preserved input/output inventories. `--complete-data` adds synthetic complete
definition banks; these checks do not launch the game.

`tools/test_mod_record_choice_contributors.py` checks unavailable active record
choices, single/no-contributor cases, baseline aliases and exact output/input
preservation in mod merge/build and both standalone definitions merge modes.
Its optional `--complete-data` fixture uses synthetic definition banks.

`tools/test_mod_fse_choice_contributors.py` checks quest-entry contributors,
baseline presence/absence, inactive choices and preserved input/output bytes.
`--complete-data` adds synthetic complete definition banks. Optional
`--gui <exe> --install <root>` also checks both-size warning clipping, actual
winner-menu clearing and independently read rebuilt quest-registry values.

`tools/test_mod_unavailable_choice.py --install <root>` proves the displayed
winner matches independently built output for an absent saved contributor.
It checks preservation, choice-name search, real dropdown clearing, an ordinary
contributor named base and an absent none choice. Definition baseline aliases
are checked in the field-details fixture. `mods_unavailable_choices` counts reported choices absent from
their contributors; `mod_unavailable_choice_<row>` reveals the explanation.

`mods_wins_<mod index>` / `mods_losses_<mod index>` report current contested
part counts. Visible badge widgets use `mod_wins_<mod index>` /
`mod_losses_<mod index>`; a zero count has no badge. The field-details and
401-file search fixtures check partial field wins and unchanged whole-file counts.

`tools/test_mod_conflict_search.py --install <root>` builds 401 synthetic file
conflicts and checks real typing, case-insensitive search, last-row choice/reset
and saved-choice filtering at both sizes, with source-byte preservation.
Widgets: `input_mod_conflict_search`, `mods_choices_only`, `mods_show_all_conflicts`.
States: `mods_conflicts_matching`, `mods_first_matching_conflict`, `mods_choices_only`.

`tools/test_mod_field_details.py --install <root>` uses a four-mod synthetic
definition stack to check field details, contributor fallback, vanilla scope,
clearing choices and independently decoded merged banks at both window sizes.
The fourth mod contributes only a matching independent edit; it has no contested
field wins/losses and choosing it retains actual-changer fallback for every conflict.
`mod_fields_<row>` expands details; `mod_field_<row>_<field>` reveals one field.
`mods_field_conflicts` counts retained field rows. `mods_first_field_<index>` and
`mods_first_field_winner_<index>` expose the first record's current field winners.
`mods_first_field_base_<index>` and `mods_first_field_value_<index>` expose its
baseline and current winning values when the report contains value comparisons.

`tools/test_ground_smear.py --install <root>` checks Ground Paint/Smear controls,
strict vertex cursors, frozen neighbour blends, Repeat, undo and draft reopen
on owned stock/loose copies at normal/compact sizes. `chip_ground_paint` /
`chip_ground_smear` switch modes; `ground_smear` reports the selection.
`assert_theme_blend x y t0 t1 t2 w0 w1 w2` checks exact palette slots and all
three effective strengths (the third strength is implicit in saved LEV data).

`tools/test_brush_pin.py --install <root>` checks Space-held brush pinning against
moving rays/cameras, release, Shift inversion, Escape and Ctrl cancellation,
clipboard exclusions, tool/map reset and exact stroke undo at both sizes.
States `brush_pinned`, `brush_pin_x` / `brush_pin_y` expose the latch;
`brush_pin_point_valid`, `brush_pin_held` and `viewport_hovered` report input context.
`key_down Space` / `key_up Space` exercise the real binding. `assert_shore x y
0|1` checks a vertex's authored shore flag, alongside snapshot/change checks.


`menu_view` / `menu_map_camera` expose the FOV submenu; `slider_camera_fov`
and `button_camera_fov_reset` exercise its real controls. `camera_fov_degrees`
reports the vertical angle. Session checks cover limits, typed input, reset,
clean document state and process restoration. They also check per-map camera /
render-mode retention across revisits and restart, malformed-view isolation and
install scoping.

`tools/test_session.py --install <root>` checks exact camera and map/tab/tool
restoration across processes, malformed camera fallback, missing maps,
install mismatch and default automation isolation at both sizes. An explicit
absolute `FABLEFORGE_TEST_SETTINGS` path enables startup/exit settings only in
that owned file. Without it, automated runs ignore and do not write ordinary
settings. State `terrain_mode` exposes the active terrain tool.


`tools/test_snapshots.py --install <root>` checks dated captures via Ctrl+B and
the File menu, process reopen, restore/undo/redo, retention after Save draft,
cancel without removing recovery, Escape cancellation and confirmed deletion
at normal and compact sizes. It uses an owned recovery root and sibling
snapshots directory. States `snapshots_open` / `snapshot_count` expose the
library. Both snapshot menu items and the library require an owned recovery
root in scripted runs; the harness also checks that unconfigured scripts cannot
open the library or capture via either shortcut/menu path. Menu widgets are `menu_snapshot_create` / `menu_snapshots`; library
widgets are `btn_snapshot_create`, `btn_snapshots_refresh`,
`btn_snapshot_restore_<index>`, `btn_snapshot_delete_<index>` and
`btn_snapshot_delete_confirm_<index>`. Historical restore shares the recovery
modal widget IDs; its discard widget is labeled Cancel.


`tools/test_recovery.py --install <root>` checks process reopen, captured worker
state, in-session menu capture/restore, recovery undo/redo, real modal controls,
capture-time visibility, timer enable/disable, successful
save cleanup and failed-save retention at both sizes. It reads the install and
writes archives/drafts only below its owned build directory.
`set recovery_root <owned path>` isolates archives; `set recovery_prompt 1` and
`set recovery_timer 1` opt scripted runs into the modal and timer independently.
`set autosave 0|1` and `set autosave_minutes <1..30>` configure the scheduler.
`autosave_now` starts a background capture, `wait_autosave` waits for completion,
`autosave_due` advances the scheduler deadline, and `assert_autosave_exists 0|1`
checks the current map's archive. States `recovery_pending`, `recovery_busy` and
`autosave_enabled` expose the state. Widgets `btn_recovery_recover` /
`btn_recovery_discard` expose modal actions; `menu_file`, `menu_recovery`,
`menu_autosave_now` and `menu_recovery_restore` expose the real menus.
`text_recovery_time` exposes the capture-time readout. Scripts need an owned
recovery root;
they do not use user-data archives or the automatic timer by default.

`tools/test_budget_areas.py --install <root>` checks captured Budget object UIDs,
half-open/reversed/degenerate boxes, real mouse drag/Shift/click/Clear/Escape,
visible overlays, map reset and unchanged document/terrain/camera at both sizes.
`set budget_scope 3` selects Areas; `budget_area <x0> <y0> <x1> <y1>` adds a box,
`budget_clear_areas` clears boxes and captured objects, and
`assert_budget_thing <script name> <0|1>` checks captured membership.
`mouse_move ground <x> <y>` projects a map-local ground point into the viewport.
States `budget_scope`, `budget_picking`, `budget_areas`, `budget_area_drag` and
`budget_things` expose survey state/counts. Widgets `btn_budget_pick`,
`btn_budget_pick_done`, `btn_budget_pick_clear`, `btn_budget_clear_areas` and
`seg_budget_scope` exercise the real controls.

`tools/test_terrain_drafts.py --root <install>` copies stock banks under build,
then tests stock/loose layouts at both sizes. It exercises the Save button,
Ctrl+S, F6/F7, map-switch prompt, process reopen, untouched active terrain banks,
draft/direct bake equivalence, game-write cleanup and Restore.
`save_draft` / `save_draft_refused` exercise the joint object/terrain transaction;
legacy `save_level` remains object-only. `terrain_draft_dirty` measures edits
since the authoring save; `terrain_dirty` measures terrain pending for game/pack.
`key_down` / `key_up` support F6 and F7.

`tools/test_passability_cursor.py --install <root>` checks modes 4/5/12/13:
rounded 1/9/21-vertex pens, zero Strength/time, real strokes, Walkable/Blocked
Shift inversion, preservation of other channels and exact undo at both sizes.
`snapshot_passability` stores walkability and camera flag arrays;
`assert_passability_changes <walk count> <camera count>` checks both against
that snapshot. `brush_cursor_vertices` reports small survey pen vertices.

`tools/test_shore_points.py --install <root>` checks mode 17's 1/9/21-cell
shore pen, real On/Off controls, Shift inversion, unchanged other channels,
visible overlays in four view modes and exact undo at normal/compact sizes.
`set shore_set 0|1` selects Off/On; `set shore_overlay 0|1` hides/shows red flags.
States `shore_set` and `shore_overlay`, widgets `chip_shore_on`, `chip_shore_off`
and `check_shore_overlay` expose them. `snapshot_shore` / `assert_shore_changes
<count>` check changed flag cells. `survey_overlay_kind` 3 means shore points.

`tools/test_environment_smear.py --install <root>` checks Paint/Smear controls,
empty/fractional sparse cursors, real mouse strokes, Ctrl sampling, preservation
of other channels and exact undo at normal/compact sizes.
`set environment_smear 0|1` and state `environment_smear` expose the option;
widgets `chip_environment_paint` / `chip_environment_smear` select it. Existing
`terrain_stroke`, environment snapshots and cell-change assertions also cover
Smear; cursor counts are unique cells although the pen may revisit them.

`tools/test_ground_stamp.py --install <root>` checks the Ground Full stamp
toggle, strict vertex cursor, full assignments with zero Strength, real clicks,
soft-brush compatibility and exact undo at normal/compact sizes.
`set paint_stamp 0|1`, state `paint_stamp` and widget `check_paint_stamp` expose
the option. `assert_theme_stamp <x> <y> <theme>` checks the live vertex's exact
{theme,0,0}/{255,0,0} blend, including during a stroke.

`tools/test_survey_overlay.py --install <root>` checks visible survey colours,
selected-sound filtering, controls, painting and exact undo at normal/compact
sizes. `set sound_overlay 0|1|2` chooses off/all/selected;
`set environment_overlay 0|1` sets the atmosphere overlay.
State `survey_overlay_kind` is 0/off, 1/atmosphere,
2/sound; `survey_overlay_filter` is -1/all or the selected sound index (0/None).
Widgets `combo_sound_overlay` and `check_environment_overlay` expose the controls.
Open theme pickers expose `colour_sound_<index>` and `colour_environment_<slot>`.

`tools/test_placement_grid.py --install <root>` checks the real placement-grid
toggle at 1440x900 and 800x600 / 1.5 scale, ground and fixed heights, pointer
placement, paste, special things, carry and exact undo without game writes.
`set place_snap_grid on|off` sets the option; state `place_snap_grid` and widget
`toggle_place_snap_grid` expose it. `assert_selected_xy <x> <y>` checks map-local
XY within 0.001; `assert_selected_cell_centre` checks grid alignment.
`assert_selected_ground [offset]` checks terrain-relative Z within 0.01.

`tools/test_wholemap_cameras.py --install <root>` saves baseline, raised and
lowered StartOakValeWest documents into separate owned scratch folders. It
independently verifies camera base and spline-key Z changes, unchanged directions,
timings and other rows, and exact document/terrain undo. The live install is read-only.

`tools/test_height_pick.py --install <root>` checks real Ctrl-drag sampling in
all six sculpt tools, Ctrl+Shift, Flatten's sampled target and active-stroke undo
at normal and compact sizes without saving. State keys `pen_target` and
`pen_target_from_stroke` expose the target; `text_paint_height` names its readout.

`tools/test_height_keys.py --install <root>` checks fixed-point Minus/Equal
strokes, all six sculpt tools, all other terrain modes, tool switches and exact
undo at both sizes. `height_key_active`, `height_key_x` and `height_key_y` expose
the active stroke and latched ground point.
The same check exercises the real Speed slider's endpoints in every sculpt
tool, zero-speed refusal to alter ground, nonzero strokes and undo. State keys
`pen_speed` and `height_key_step` expose the slider and its derived metre step.

`tools/test_height_cursor.py --install <root>` checks exact-step Raise/Lower
and Flatten/Smooth cursors at normal/compact sizes. `brush_cursor_vertices` counts
the projected affected-vertex dots (shown at radii <=4 cells); the check verifies
single/9-vertex discs and unchanged terrain/object snapshots.
It also captures the Noise square at single-vertex and three-by-three sizes
for visual inspection, without changing the document.
Height-key cursor cases cover radii 0.25/0.5/1/1.5/2, with 0/0/5/4/13 distinct
vertices, unchanged snapshots for empty footprints and exact undo for strokes.
The empty-footprint hint and fractional-radius dots are captured at both sizes.

`tools/test_sound_cursor.py --install <root>` checks actual Sound mouse strokes,
1/2/4-cell cursor outlines, Ctrl sampling and exact undo at normal/compact sizes.
`brush_cursor_sound_cells` counts queried cells outlined by small Sound cursors;
`sound_index` exposes the selected palette index. `set sound_index <n>` selects
a listed index (0 is none). `snapshot_sounds` captures the sound grid and
`assert_sound_changes <count>` checks the exact number of changed cells.

`tools/test_replace_cursor.py --install <root>` checks Replace's truncated
centre and strict 1/9-vertex pen through actual mouse clicks at both sizes,
preserving heights/sounds/objects and undoing exact theme blends.
`snapshot_themes` captures all ground indices and strengths;
`assert_theme_changes <count>` checks changed vertex blends. For Replace,
`brush_cursor_vertices` counts considered dots; theme matching can leave some
of those vertices unchanged.

`tools/test_environment_cursor.py --install <root>` checks Environment's strict
1/2/4-cell pen through real mouse strokes, sampling, clearing and exact undo at
both sizes. `brush_cursor_environment_cells` counts queried outlined cells;
`environment_index` exposes the selected slot. `set environment_index <n>` sets
it, and `add_environment <ENVIRONMENT_THEME_DAY name>` adds/selects a valid
definition in the map's atmosphere palette. `snapshot_environments` captures
all atmosphere indices/strengths; `assert_environment_changes <count>` checks
the exact number of changed cell blends.

`input_text <text>` sends UTF-8 text through ImGui to the focused input. Use
`key_down Ctrl`, `key_down F`, then release both keys to test search shortcuts.
`tools/test_asset_search.py --install <root>` checks the active asset browser's
search and recovery of hidden panels at normal and compact window sizes.

`tests/ui/attach_picker.txt` clicks the target-side Attach objects control,
resolves a named visible thing through the same viewport pick path, then checks
attach, detach and exact undo. It uses `select_script <name>`,
`pick_script <name>` and `assert_link_script <source> <field> <target|->`.
`tests/ui/attach_entrance.txt` uses MayorsHouseHallway's real meshless exit and
entrance. It rejects a self pick, then checks a valid pick, the serialized
`EntranceConnectedToUID` in each component present, and exact undo.

`tests/ui/creature_sex.txt` checks the definition-backed Father/Mother sex
lookup against retail male and female Oakvale villagers. The diagnostic state
key is `selected_creature_sex` (1 male, 2 female, unknown if unavailable).

`tests/ui/owned_movement.txt` selects a retail owner with a framed child,
checks the child's movement and exact undo, then disables the saved
`move_owned` toggle and checks that the child stays put. The commands are
`select_owned_parent`, `snapshot_owned_frame`, `assert_owned_moved dx dy dz`
and `assert_owned_frame_same`.
`tests/ui/owned_sections.txt` creates a quest section, moves two selected roots
and valid owned descendants, then checks their sections and byte-exact undo.
It uses `toggle_thing <index>`, `assert_owned_section <name>` and
`assert_selected_sections <name>`.
`tests/ui/owned_delete.txt` drives the three owner-deletion modal choices and
exact undo. It uses `owned_delete_popup`, `btn_owned_delete_all`,
`btn_owned_delete_only`, `btn_owned_delete_cancel`,
`assert_owned_snapshot_exists 0|1` and `assert_owned_snapshot_detached`.
`tests/ui/owned_duplicate.txt` checks copied parent-child links, a detached
single-child copy and exact undo. It uses `toggle_snapshotted_owned`,
`select_snapshotted_owned`, `assert_copied_owned_pair` and
`assert_selected_owner <uid>`.
It also verifies the `+N owned` hint as the selection changes, through
`selected_owned_count` and `text_owned_count`.

`tools/test_mods.py --keep` runs the local mod corpus and an adapted
`tests/ui/mods.txt` in a unique `build/mods-corpus-*` workspace. It records every
CLI command and verifies the complete scratch-file inventory after undeploy.
`tools/test_mods_masters.py --keep` uses `build/mods-masters-*` for dependency,
reorder and enable controls.
`tools/test_mod_master_labels.py` checks long literal names and both checkbox
and label clicks (`mod_requires_<row>_<master>`, `mod_requires_label_<row>_<master>`). Both capture hidden GUI output and screenshots;
shared `build/ui` screenshots and checked-in scripts are not overwritten.

The GUI can drive itself from a plain-text script. It is how the UI is tested:
real widgets get real synthetic clicks, state is asserted through the same
accessors the UI uses, and the backbuffer is saved as PNG for pixel checks.

```
FableForge.exe --auto tests/ui/smoke.txt [--install <root>] [--size 1440x900]
```

In a scripted run the real mouse never reaches ImGui (every WM_MOUSE* message is dropped in the window procedure), so moving the cursor over the window or having another app in the foreground cannot disturb a synthetic click. Exit code is 0 when every assertion passed. A log lands next to the script
(`<script>.log`) ending in `RESULT PASS` / `RESULT FAIL` plus the failures. Every
line starts with the seconds since the script loaded, so a `wait_*` line's stamp
is how long that job took (a warm boot to `wait_ready` is ~0.15 s; opening
Oakvale West through `wait_foliage` ~0.45 s).
Settings persistence is disabled under `--auto` so runs are deterministic.

## Commands

| Command | Effect |
|---|---|
| `wait_maps` | until the map list is read (from FinalAlbion.wad, or the loose `data/Levels/FinalAlbion/*.lev` of an install without one; state `levels_loose`) |
| `wait_ready` | until the map list is read **and** the texture context finished (ok or failed) |
| `wait_loaded` | until the selected map's preview is on the GPU |
| `wait_neighbours` | until neighbour previews are enabled and the selected map's neighbour job has finished |
| `wait_export` | until the running export finished |
| `wait_batch` | until a batch export finished |
| `frames <n>` | render n frames |
| `select <map>` | select by name (or key `file:<stem>` for loose files) through the state path |
| `filter <text>` | set the search box (empty clears) |
| `click <widget>` | synthetic click at the centre of a registered widget (3 frames: move, press, release) |
| `open <path.lev>` | open a loose .lev (same path as drag-and-drop) |
| `orbit <dyaw> <dpitch>` / `zoom <steps>` / `look <dyaw> <dpitch>` / `fly <fwd> <strafe> <rise> <secs>` | camera through its API |
| `camera <x> <y> <z> <yaw> <pitch> <dist>` | look at a Fable map-local point |
| `mouse_move <x> <y>` \| `mouse_move <widget>` (its centre) \| `mouse_move viewport\|selected_glyph`, `wheel <dy>`, `mouse_delta <dx> <dy>`, `mouse_down\|mouse_up left\|right\|middle`, `key_down\|key_up W\|A\|S\|D\|Q\|E\|F\|Shift\|Alt\|Ctrl\|Escape\|Minus\|Equal` | raw input through ImGui (tests the real control path); `selected_glyph` targets a visible meshless thing |
| `snapshot_camera` / `assert_camera_moved [min]` | camera position delta check |
| `wait_foliage`, `set preview_foliage 0\|1`, `set foliage 0\|1` | foliage preview / export |
| `mode textured\|wireframe\|walkable\|height` | view mode |
| `set <key> <value>` | export settings: `format glb\|obj`, `textures 0\|1`, `layers 0\|1`, `walkable 0\|1`, `texels n`, `tile f`, `gain f`, `up y\|z`, `world 0\|1`, `things 0\|1`, `outdir path` |
| `export` / `export_all` / `export_region` | start a single / batch / region export (state path; use `click btn_export` for the UI path) |
| `edit 0\|1` | switch the right panel between Export and Edit (opens the level document) |
| `gizmo 0\|1\|2\|3` | select / move / rotate / scale tool |
| `pick <u> <v>` | ray-pick at viewport-relative (u, v) in [0,1]; selects the hit thing |
| `select_thing <index>` / `select_def <DEFINITION>` | select by .tng index / first thing with that DefinitionType |
| `assert_selected_link_def <field> <DEFINITION>` | require the selected thing's link field to target the first placed thing with that definition |
| `selection_lock 0\|1` / `select_toggle_def <DEFINITION>` | toggle selection locks / add or remove the first matching definition from selection; state `selected_locked`, `gizmo_using` |
| `assert_component <CTC> <key> <value or ->` | check an admitted scalar override; `-` requires absence, not a guessed default |
| `snapshot_document` / `assert_document_same` | capture and compare byte-exact in-memory TNG text |
| `snapshot_selected` / `assert_snapshot_thing_same` | capture the selected UID/block and check it is unchanged, even after index shifts |
| `mouse_move selected_pivot` | move the virtual pointer to the selected object's projected pivot for actual gizmo gestures |
| `surface_thing` / `assert_selected_above_ground` | cycle visible surfaces / require an elevated object support |
| `assert_selected_height <z>` | require selected absolute Z within 0.001 |
| `snapshot_frame` / `assert_selected_lower` | capture selected frame / require lower Z with unchanged XY, orientation and scale |
| `move_thing <dx> <dy> <dz>` / `rotate_thing <deg>` / `scale_thing <factor>` / `ground_thing` | edit the selection (one undo step each) |
| `duplicate_thing` / `delete_thing` / `undo` / `redo` | structural edits on the whole selection (the objects layer reloads; `wait_foliage` waits for it) |
| `mods_tab 0\|1`, `mod_add <path> [name]`, `mod_remove <name\|index>`, `mod_move <name\|index> <to>`, `mod_enable <name\|index> 0\|1`, `mods_deploy`, `mods_undeploy`, `mods_conflicts`, `wait_mods`, `mod_pick <key\|*> <winner\|->`, `mod_pick_refused <key\|*> <winner\|->` | the Mods tab (state `origin_mods`, `origin_things` = the deploy's provenance for the open map; widgets `combo_origin`, `btn_thing_retail`): the save root's `forge_mods.json` load order; deploy/undeploy run `forge-tools.exe` and stream into the log, conflicts fills the Conflicts card from its `--json` report and `mod_pick` writes `forge_mods_picks.txt` (state `mods_mode`, `mods_count`, `mods_conflicts`; widgets `input_mod_path`, `input_mod_name`, `btn_mod_add`, `btn_mods_conflicts`, `btn_mods_deploy`, `btn_mods_undeploy`, `mods_asset_status`, `mod_winner_<row>`) |
| `textures_tab 0\|1`, `texture_select <name\|label\|id>`, `texture_export [png]`, `texture_replace <image>`, `texture_add <NAME> <image> [dxt1\|dxt3\|argb8888]` | the Assets tab's Textures page (`assets_tab 0\|1\|2` opens Textures / Models / Ground themes; state `textures_mode`, `textures_count`, `texture_selected`; widgets `list_textures`, `img_texture`, `btn_tex_*`) |
| `place_emitter <EFFECT> [scriptname]` | a PARTICLE_EMITTER_PLACEABLE playing the effects.big entry, half a unit above the ground at the view centre (state `effects` = names loaded; widgets `list_effects`, `btn_place_emitter`) |
| `set_entrance` | the map's region entrance (FinalAlbion.gtg, this map's WLD slot) at the view centre, facing the camera (state `entrance` = `x,y,h` or `-`; widget `btn_set_entrance`) |
| `preset_place <name>` / `preset_save <name>` | place a preset (shipped `presets/` or the user folder) at the view centre as a selected group; save the selection as a user preset (state `presets` = how many are listed) |
| `select_toggle <index>` / `select_added` / `copy` / `paste` | Ctrl+click a thing into/out of the selection; select the first thing the Changes list reports as added; copy the selection as a fragment; paste it at the view centre (state `selection_count`) |
| `place <DEFINITION> [scriptname]` | place a new thing at the camera focus, on the ground (`CREATURE_*` as the retail AICreature block) |
| `set place_def <DEFINITION>` / `place_at <u> <v>` | choose the palette definition and place it at viewport-relative pointed terrain coordinates; Shift+click tests the actual input path |
| `snapshot_frame` / `assert_selected_moved_xy` / `assert_selected_frame_same` / `assert_ground_offset_same` | capture a selected frame, then check XY movement, exact frame restoration, or preserved height above terrain |
| `ground_place_carry.txt` | synthetic Shift+click placement, plain and Ctrl+Shift ground drag, Ctrl+D cursor carry, grouped undo, fixed height, lock refusal and Escape cancellation |
| `place_village <VILLAGE_DEF> [scriptname]` / `village_member <uid\|scriptname\|0>` | place a Village thing at the camera focus; the selected thing joins/leaves a village (state `villages`, `selected_village`) |
| `place_fishing_spot [OBJECT_DEF\|-] [scriptname]` | a `MARKER_FISHING_SPOT` at the camera focus on the ground; the optional def is the first catch (`CTCContainerRewardHero`), empty or `-` = the game's fish table; the ScriptName is what the in-game `--things` probe looks up |
| `link_install` / `link_remove` / `link_go` / `link_spawn` / `link_ping` / `link_poll` | the ForgeFSE live link (state `link_installed`, `link_ready`, `link_hero_map`) |
| `reseat_things` | every object that stood on ground changed since the last save follows it (offset kept; one undo step) |
| `terrain_raise_all <d> [things 0\|1]`, `terrain_scale_pct <pct>`, `terrain_set_all <v>`, `wholemap_open 0\|1` | the Terrain tab's Whole-map heights window (vanilla console EditRaiseZ / EditResizeZPercent / EditSetZ); widgets `btn_wholemap_toggle`, `btn_wholemap_raise`, `btn_wholemap_scale`, `btn_wholemap_set`, `check_wholemap_things` |
| `world_open <map>` / `world_fit <map>` | the World tab's selected-map card: open the map in Edit, or on Terrain with Fit to neighbours open (refused while moves are pending) |
| `replace_from <slot>` | the Replace / Flood "Replace this theme" palette slot (the target is `paint_theme`); `click btn_replace_all` runs Replace all |
| `add_theme <ENGINE_THEME>` | add a ground theme from game.bin to a free LEV palette slot and select it for painting (state `paint_theme`, `palette_named`); one undo step |
| *(toasts)* | every warning / error / success log line also shows for 6 s in the viewport's top-right corner (state `toasts` = how many are up); the long jobs' busy button reads `<verb>: <stage>  (N s)` from the job thread |
| `def_search <text>` | fills the *Add an object* box (rows show a mesh thumbnail) |
| `theme_search <text>` | fills the *Add a ground theme from the game* box (the list shows a swatch per theme) |
| `drop <file>` / `drop_refused <file>` | what a file dropped on the window does (or must refuse, e.g. a missing file): `.glb`/`.gltf`/`.obj` opens Assets > Models with the import form filled (state `model_import_open`, `mesh_name`; nothing is imported), `.lev` opens as a loose map, an image opens Assets > Ground themes with the path in the custom-texture input (state `textures_mode 1`; `textures_tab 0` returns to the map editor, `assets_tab <n>` picks the page) |
| `set place_owner auto\|neutral\|0..3` | the owner combo (vanilla PLAYER_LIST_BOX) for the next placements; state `place_owner` |
| `set pen_exact 0\|1`, `set pen_step <m>`, `set pen_target <m>`, `set pen_target_from_stroke 0\|1`, `set pen_speed <0..1>`, `set pen_smoothness <0..1>`, `set pen_spiky <0..1>`, `set pen_magnifier <1..50>`, `set pen_spray 0\|1` | the vanilla Height Toolbox pens (Raise/Lower exact step, Flatten target and speed, Smooth, Noise; Spray) |
| `assert_height <x> <y> <expected> [tolerance]` | the ground height at a map-local point |
| `snapshot_heights`, `assert_heights_changed 0\|1` | remember every terrain vertex, then assert that some (1) or none (0) changed since |
| `set things_script_only 0\|1`, `set things_nearest 0\|1` | the Objects list switches (vanilla Scene Browser); state `things_first` (index of the first row), `things_shown` |
| `track_preview <eye #> <look #> <seconds>`, `wait_track_preview`, `assert_camera_back` | the Tracks card's Play preview (tracks by list index); state `track_preview`; the camera is back at `snapshot_camera` |
| `open_world <path.wld>` | File > Open world without the dialog (another world's maps join the list, grouped by region) |
| `clear_toasts` | drop the corner notices (clean screenshots for the guide) |
| `set world_3d 0\|1`, `set world_terrain 0\|1`, `wait_world_tiles`, `world_camera <x> <y> <height> <yaw> <pitch> <distance>`, `world_open_3d <map>` | the World tab's 2D map with ground tiles / the 3D fly-over of every map; state `world_3d`, `world_tiles`, `world_tiles_total`, `world_tiles_busy`, `world_hover` |
| `assert_world_ground <x> <y> <map or ->` | Assert the world tile picked at a Fable-space point, including pending moves; `-` means no tile. |
| `set world_detail 0\|1`, `wait_world_detail`, `set world_auto_detail 0\|1`, `set world_culling 0\|1` | Streaming, adaptive budget and diagnostic culling control. State: `world_detail_maps`, `world_water_batches` (resident overview water, independent of detail), `world_detail_budget`, `world_drawn_batches`, `world_culled_batches`. `set preview_water 0\|1` controls persistent world water; its checkbox remains available with detail off. |
| `wait_profiler`, `profile_mark <name>` | Wait for a local Tracy recorder (requires a profiling build); label a route phase. See `tools/profile_world.py` and `docs/PROFILING.md`. |
| `world_look <yaw> <pitch>`, `world_eye <x> <y> <height>` | Rotate in place or translate the eye directly without changing orbit focus distance. Angles are radians; eye coordinates use Fable axes. |
| `world_transition <0..1>` | Set the current detail transition fraction for the next frames (then normal animation resumes); used for midpoint screenshots. |
| `set world_detail_limit <1..12>` | Set the same map-count ceiling as the UI. Cache diagnostics: `world_detail_cached_maps`, `world_detail_cached_bytes`, `world_detail_cache_budget`, `world_detail_cache_hits`, `world_detail_loads`. Loads/hits are cumulative for the session. |
| `reveal <widget>` | scroll the panel holding a registered widget so it is on screen (before a `screenshot`) |
| `owner_apply` | vanilla O: the selection's Player becomes the owner combo's value (Auto = 4); state `selected_player` |
| `daynight 0\|1\|2` | the selected creature: day and night / day only / night only (moves it to `<quest>%DayOnly` / `%NightOnly`); state `selected_section` |
| `set section_day 0\|1`, `set section_night 0\|1`, `set section_hidden <NAME> 0\|1` | the Quest sections card's filters; state `section_day`, `section_night`, `selected_visible` |
| `click chip_grid` | the LEV cell grid overlay (state `grid`); state `cursor_ground` = `x,y,h` under the mouse when it is over the ground, `-` otherwise |
| `click chip_markers`, `mouse_move selected_glyph` | show/pick meshless things in the Edit viewport; state `thing_glyphs` is the visible overlay count |
| `set uiscale <0.8..1.5>` | the *Interface > Text size* factor (state `ui_scale`; fonts rebuild) |
| `tour <0..2\|-1>` | the first-run tour callout (state `tour_step`; widgets `btn_tour_next`, `btn_tour_skip`); starts by itself after the first Setup panel |
| `help 0\|1` | the keyboard/mouse cheat-sheet overlay (also `?` / F1 / the header `?`; state `help_open`, widgets `btn_help`, `btn_help_close`) |
| `edit_tab <0..3>` | the Edit panel's sub-tab: 0 Objects, 1 Terrain, 2 Actors, 3 Level (state `edit_tab`; widget `seg_edit_tab`). Terrain and the terrain tool follow each other; placing a spawner/village opens Actors, a viewport pick from Terrain/Level opens Objects |
| `dismiss_rule <creature\|spawner\|region>` | what the engine-rule notice's *Got it* does (state `rule_notice` = the key shown, `-` for none; widget `btn_rule_<key>`) |
| `compact_stb` / `wait_compact` | compact the static-map bank in the background (Setup panel's *Compact the bank*); log line `compact: A MB -> B MB, N payloads verified byte-identical` |
| `setup 0\|1`, `restore_all` | show Setup or run its restore action. Restore uses the `--install` root, not `set saveroot`; tests must pass an isolated `--install` root. `tests/ui/setup_restore.txt` clicks the confirmation in a synthetic loose-level install. |
| `scan_install <root>` | exercise a folder change without a native picker; use only scratch roots in automation. `tools/test_install_switch_ui.py` checks the unsaved-edit guard, invalid path and stale-preview cleanup. |
| `mesh_import <model> <NAME> [png]`, `wait_mesh_import` | the Assets tab's Models page (`assets_tab 1`): a .glb/.gltf/.obj into graphics.big + textures.big + game.bin (off the UI thread; states `mesh_import_busy`, `mesh_import_failed`; widgets `input_mesh_model`, `input_mesh_name`, `input_mesh_texture`, `btn_mesh_import`, `mesh_import_error` when present); the context reloads after (`wait_ready`), then `place OBJECT_<NAME>` |
| `wait_mesh_replace` | Wait for the selected Models entry's geometry replacement and preview refresh. Controls: `btn_model_replace`, `input_mesh_replace_model`, `btn_mesh_replace`; result widgets: `mesh_replace_error`, `mesh_replace_success`. States: `mesh_replace_busy`, `mesh_replace_failed`, `mesh_replace_succeeded`. The base definition/texture context stays loaded when writing to a separate save root. Changing that root clears result feedback. |
| `assets_tab 5`, `wait_texts`, `text_select <name|id>`, `text_select_refused <name|id>` | Read-only Text browser. Exact names take precedence over numeric IDs; ambiguous shortcuts refuse without losing selection. Widgets: `combo_text_language`, `input_textsearch`, `btn_text_refresh`, `text_content`, `text_member_<n>`, `text_entry_error`, `text_unsupported_entry`. |
| `assert_text_narrator <index> <name>` | Compare an indexed name in the selected narrator list. Widget `text_narrators`; state `text_narrators` counts names. Property links use `property_text_CTCActionUseReadable_GameTextDefName`, `property_text_CTCInfoDisplay_TextTag`, and `property_text_CTCInfoDisplay_TextTagBack`. |
| `text_export_path <new.tsv>`, `text_export <new.tsv>`, `text_export_refused <path>`, `wait_text_export` | Set the TSV path or start/check export of the entire loaded bank. Widgets `input_text_export`, `input_text_export_browse`, `btn_text_export`; states `text_export_busy`, `text_export_failed`, `text_export_succeeded`. Existing files and non-TSV destinations refuse. |
| `text_edit`, `text_edit_refused`, `text_edit_content <content>`, `text_save`, `text_save_refused`, `assert_text_edit <name\|->` | Start/check an existing string draft, set its UTF-8 content, and save/check refusal; assert its entry or that it is closed. Real controls: `btn_text_edit`, `input_text_edit_content`, `btn_text_save`, `btn_text_save_confirm`, `btn_text_edit_cancel`; failures register `text_edit_error`. Content edits preserve metadata and refuse stale/noncanonical banks. |
| `text_add`, `text_add_refused`, `text_add_name <new name>` | Start/check a draft from the selected donor and set its new name; use the shared content/save commands. Controls: `btn_text_add`, `input_text_add_name`. Donors remain intact; new strings receive a free ID and an identifier matching their new name. |
| `text_edit_speaker <name\|->`, `text_edit_speaker_refused <name>` | Choose/check a unique existing narrator for a string draft; `-` selects the empty speaker. Controls: `combo_text_edit_speaker`, `text_speaker_option_<index>`. Saving updates the speaker payload and header index together and refuses stale headers. |
| `text_edit_speechbank <value\|->`, `text_edit_speechbank_refused <value>` | Set/check a draft speech-bank reference. ASCII values shorter than 4096 bytes; `-` clears it. Control: `input_text_edit_speechbank`. Unsupported original bytes stay read-only. |
| `text_edit_identifier <value\|->`, `text_edit_identifier_refused <value>` | Set/check an existing string identifier under the same byte limits. Donor additions use their new name automatically. Control: `input_text_edit_identifier`. |
| `text_tag_set <index> <position> <name\|->`, `text_tag_set_refused ...` | Edit one ordered draft tag with a signed 32-bit stored position and bounded ASCII name; `-` clears its name. Controls: `combo_text_edit_tags`, `text_tag_option_<index>`, `input_text_edit_tag_position`, `input_text_edit_tag_name`. |
| `text_tag_add`, `text_tag_add_refused`; `text_tag_remove <index>`, `text_tag_remove_refused <index>` | Append an empty tag at position zero or remove the selected index. Controls: `btn_text_tag_add`, `btn_text_tag_remove`. Untouched legacy names remain preserved. |
| `text_search <query|->`, `text_language <language>`, `text_language_refused <language>`, `assert_text_content <full text>` | Search names/IDs/content, choose an available language, and compare the complete selected content including spaces and Unicode. States: `texts_count`, `texts_filtered`, `texts_busy`, `texts_decode_errors`, `text_language`, `text_selected`, `text_type`, `text_ready`, `text_tags`, `text_members`, `text_failed`. |
| `custom_theme <png> <NAME> [donor] [cliffPng]` | a ground theme from a PNG (textures.big + game.bin append), added to the palette and selected; the texture/def context reloads (`wait_ready`) |
| `custom_theme_refused <png> <NAME> [donor] [cliffPng]` | requires theme creation to fail; pair with `assert_log` to check the reason, e.g. a full map palette |
| `drag_gizmo <dx> <dy>` | press on the selected pivot and drag by (dx, dy) window pixels through the real gizmo |
| `frame_selected` | frame the camera on the selection |
| `terrain_mode <0-5>` | terrain tool: 0 raise, 1 lower, 2 flatten, 3 smooth, 4 walkable, 5 blocked (also selects the tool) |
| `brush <radius> <strength>` / `terrain_stroke <x> <y> <seconds>` | brush size; one stroke at a map-local point |
| `new_level <name> <x> <y> [region]` / `wait_new_level` | fill the "New level" card and install (BWD/WLD/WAD/STB under the install), wait for it; refuses unsaved map edits and retains edits made while the job runs. Origin widgets: `input_new_level_x`, `input_new_level_y` |
| `new_level_own_region 0\|1` | request an own region with the next `new_level`, using the card's New slot / Use filler mode (New slot by default); direct creation includes a minimap |
| `new_level_blank <theme slot> <height> [<w> <h>]` | switch the card to Blank with that ground theme / height (and a retail size) before `new_level` |
| `write_both` | the footer's *Write terrain and objects*: the terrain job, then the object write once it succeeded (state `write_both_pending`; widgets `btn_write_both`, `btn_write_both_confirm`); `wait_terrain` waits for both |
| `deploy_terrain` / `wait_terrain` | write a terrain snapshot under saveroot or pack destination (worker thread); `terrain_deploy_busy` reports the pending job |
| `set unsaved_prompt 0\|1` | scripted runs skip the unsaved-changes prompt unless opted in |
| `dump_log` | copy the activity log into the script log |
| `set saveroot <dir>` | where `save_level` / `deploy_level` write (default: the install) |
| `save_level` / `deploy_level` | write the loose .tng / replace the WAD entry under saveroot |
| `wait_file_job` | Wait up to 180 seconds for the active file operation and deferred mod refresh, including outside the Mods panel. |
| `save_level_refused`, `deploy_level_refused`, `compact_stb_refused`, `mods_deploy_refused`, `mesh_import_refused <model> <NAME> [png]` | expect the corresponding action to refuse; `file_job` reports `none`, `terrain_write`, `world_write`, `level_creation`, `bank_compaction`, `model_import`, `mod_preparation`, `mod_processing` or `mod_refresh` |
| `screenshot <png>` | save the next presented frame |
| `assert_file <path>` | file exists and is non-empty |
| `assert_file_contains <path> <text...>` | the file exists and contains the text (the rest of the line, e.g. `ScriptName UiNamedBarrel;`) |
| `assert_state <key> <value>` | see `dump_state` for keys |
| `assert_state_min <key> <n>` / `assert_state_max <key> <n>` | a numeric state key is at least / at most n (e.g. `texture_thumbs_made`, the Textures list thumbnails decoded so far) |
| `assert_widget <widget>` | the widget was drawn this frame |
| `assert_log <text>` | some app log line contains the text (background job notes, e.g. `1 stitched`) |
| `world_tab 0\|1`, `world_select <map>`, `world_move <map> <x> <y>`, `world_move_refused ...`, `world_owner <map> <region>`, `world_sees <region> <map> <0\|1>`, `world_stitch <0\|1> [feather]`, `world_revert`, `world_undo`, `world_redo`, `world_apply`, `wait_world` | the World tab: queue moves / region edits, stitch seams after the apply (feather -1 = auto), write them; state keys `world_*` |
| `dump_state` | write every state key to the log |
| `dump_widget <widget>` | log the current widget rectangle as x0 y0 x1 y1 for screenshot checks |
| `close`, `quit` | request the editor's close flow (`close_prompt` state) or force the scripted run to end |

`${TEMP}` and `${USERPROFILE}` expand inside a line. Waits time out after 60 s, except `wait_world` and `wait_file_job` (180 s), `wait_mods` (600 s) and `wait_world_tiles` (900 s); a stitched world apply varies from about 40 s to over 60 s.

## Registered widgets

`wait_world_tiles` waits for worker completion and overview texture uploads;
in 3D it also waits for overview geometry publication. `wait_world_detail` waits
for demand, worker preparation, staged uploads and all resident fades to settle.
Timing routes avoid screenshot readbacks. World capture and repeated streaming
scripts hide their windows unless passed `--show`, but still render on the GPU.

`btn_change_install`, `input_filter`, `group_<Group>`, `row_<key>`, `row_selected`,
`viewport`, `chip_textured|wireframe|walkable|height|reset`, `seg_format`, `seg_up`,
`toggle_textures`, `toggle_layers`, `toggle_walkable`, `slider_texels`, `slider_tile`,
`input_outdir`, `btn_export`, `btn_export_all`, `btn_cancel_batch`, `btn_open_folder`,
`seg_panel`, and in Edit mode `seg_gizmo`, `toggle_snap`, `drag_px|py|pz|yaw|scale`,
`btn_ground`, `btn_focus`, `btn_duplicate`, `btn_delete`, `input_thingsearch`,
`input_defsearch`, `btn_place`, `btn_undo`, `btn_redo`, `btn_save`, `btn_deploy`,
`btn_deploy_confirm`, `btn_revert`, `seg_terrain_category`, `seg_terrain_mode`, `slider_radius`,
`slider_strength`, `btn_terrain_deploy`, `btn_terrain_deploy_confirm`, `btn_unsaved_save`,
`btn_unsaved_discard`, `btn_unsaved_cancel`, and in the Mods tab `input_mod_path`, `input_mod_name`,
`btn_mod_add`, `btn_mods_conflicts`, `btn_mods_deploy`, `btn_mods_undeploy`.

A widget is only registered on frames where it was drawn, so expand a group
(`click group_Arena`) before clicking one of its rows.

## In-game (the retail engine as the oracle)

`python tools/ingame/ingame_terrain_test.py` runs the real game and compares its ground
heights with the LEV; see docs/EDITOR.md "Testing in the running game without a human".
`python tools/verify_engine_lzo.py` (in `check_all`) decodes our LZO frames with the
engine's own assembly decoder under emulation.

## Hidden world-flight diagnostics

The default `--route world` traverses the full 3D world across several regions,
with absolute heights from 50 to 7000, climbs, descents and turns. For example:
`python tools/test_world_flight.py --route world --continuous --focus-distance 20 --sample-step 12 --output build/world-flight-global`.
`--continuous` saves every live-route frame and matching world state, then ranks
temporary coverage losses and large changes at unchanged camera poses. It retains
only a three-frame image window during analysis. `--focus-distance 20` tests free
flight after zooming close and adds an identical-pose reference at orbit distance
8000. `--coarse-only` isolates terrain/water visibility from object streaming.
Add `--assert-focus-coverage` to fail on more than 64 missing pixels in any paired
focus-distance view (allows small silhouette shifts from floating-point camera
matrices). Reports also count colour changes above 40/255 to expose focus-dependent
haze; animated water means RGB equality is not required.

`capture_begin <path-prefix>` / `capture_end` capture consecutive rendered frames
without pausing command execution, up to a 2000-frame guard. An explicit screenshot
inside that interval is also saved, so it cannot leave a hole in the sequence.
`world_pose x y height [yaw pitch]` uses absolute coordinates without resetting
orbit distance or forcing detail demand. `wait_world_detail` refreshes demand once
on entry and accepts an empty settled working set at high altitude or over voids.
Screenshot readbacks alter timing; these captures cannot establish frame rates.

`python tools/test_world_flight.py --route start --output build/world-flight-start`
flies a repeatable ground-relative route across Start Oakvale. Use `--route oakvale`
for adult Oakvale, `--stress` for a one-map detail budget and repeated altitude
transitions with a long orbit distance, and `--sample-step 2` for denser captures.
Requires Pillow and NumPy. The child starts hidden at below-normal CPU priority,
1280x800, with `--auto-frame-ms 33` pacing (other automation defaults to 4 ms;
accepted range 4..100). It still uses the GPU. A 50 ms watchdog checks only the
child's windows and terminates it if any is visible or foreground. Its JSON report
records violations; it never sends OS input or changes game focus.

Each run saves its exact script, timestamped state log, live/reference screenshots,
contact sheet, GIF and JSON coverage report. Live frames are compared against both
coarse and settled terrain at the same poses. Coarse and detailed settled passes
are also repeated with culling disabled. A synthetic missing-patch positive control
checks the detector. Suspect pixels are highlighted red. These are diagnostic
candidates: silhouettes and map selection can differ, and screenshot sampling can
miss brief flashes. This is not an FPS benchmark or an adaptive-budget test.

`world_eye_ground x y clearance [yaw pitch]` positions the eye relative to overview
terrain, optionally turning it, without forcing the detail-demand timer. It fails
outside known terrain. Overview height is approximate; this does not imply
collision clearance from full-detail terrain or buildings. `dump_state` includes
eye position, ground clearance, yaw/pitch, active detail names, load/cache counts
and streaming state. Automation does not persist settings or modify the install.

## Suites

* `tests/ui/editor.txt` — Edit mode: select, move, rotate, scale, duplicate, undo back to
  clean, place, delete, a real gizmo drag, save into `build/ui_editor_install`.
* `tests/ui/smoke.txt` — happy path: install detected, textured preview, all view modes,
  orbit, filter, export via button. `tools/ui_smoke.py` runs it and adds GLB validation
  and screenshot pixel assertions. It keeps its script, screenshots, export and
  report in an owned `build/ui-smoke-*` directory; `--install` selects the source.
* `tests/ui/paths.txt` — tree/row/toggle clicks, OBJ + untextured export, loose file, batch.
* `tests/ui/controls.txt` — Unreal-style camera through injected input: RMB+W flies, RMB drag looks, MMB pans, F frames, Alt+LMB orbits.
* `tests/ui/foliage.txt` — two-stage foliage load, chip toggle, export with the Foliage node.
* `tests/ui/region.txt` — `Export region` writes every map of a region in world coordinates.
* `tests/ui/noinstall.txt` — run with `--install <bogus>`: honest empty state, no crash.

`python tools/check_all.py` runs the unit tests, the retail CLI smoke and all UI suites.

## Fixed material transitions

For fixed-pose material diagnostics, `world_transition_hold <0..1>` holds the
normalized transition progress without stopping residency bookkeeping (terrain
reaches full detail earlier than objects);
`world_transition_hold auto` resumes normal rendering. `set world_material_blend
0|1` is an automation A/B switch (normal default 1). The hidden diagnostic
`tools/test_world_material_transition.py --output <dir>` captures two world poses
in eleven steps with water hidden. Generate the baseline with `--legacy-material`,
then pass its directory via `--baseline` to check improved publication continuity
and pixel-identical full-detail endpoints. Its fixed window size avoids Windows'
variable default placement affecting the comparison. It is not an FPS test.

## GPU memory pressure injection

`world_memory_sample <budget_bytes> <usage_bytes>` supplies synthetic DXGI telemetry
to the whole-world inactive-cache policy. `world_memory_sample unavailable` tests
query failure; `world_memory_sample auto` restores real adapter queries. Each
command requests a sample on the next editor frame. This allocates no VRAM and
does not alter Windows budgets. Normal operation samples once per second.
`world_gpu_memory_*`, `world_detail_cache_budget`, and `world_memory_evictions`
in `dump_state` expose the result. `tests/ui/world_memory_pressure.txt` exercises
eviction, active-map retention, unavailable telemetry, and gradual recovery.
`tools/test_world_flight.py --memory-pressure` runs its visual flight comparisons
with a persistently exhausted synthetic budget, including the broad world route.


### Model browser and detail range (2026-09-29)

Effects inspection: `assets_tab 3` opens Effects, `effect_select <name|id>`
selects an entry, and `effect_search <text>` filters names/display names/ids (omit
the text to clear). State keys are `effects_count`, `effects_filtered`,
`effect_ready`, `effect_selected`, `effect_id`, `effect_display_name`,
`effect_parsed_fully`, `effect_systems`, `effect_sprites`, `effect_meshes` and
`effect_lights`. `tests/ui/effect_browser.txt` exercises searches, sprite texture
and mesh links, light inspection and refresh without writing game data.
`tests/ui/effect_background.txt` exercises the Background picker and the Dark,
Grey and Light preset buttons (`effect_background_color`, `btn_effect_bg_*`);
state `effect_background` reports the clear RGB values used by the renderer.

Effect timing: `effect_preview_duration <seconds>` sets a manual window from
1/30 to 300 seconds. `check_effect_auto_duration` restores the estimated window.
`effect_preview_position` includes the fractional tick; `effect_preview_time`
reports completed simulation ticks. `tools/test_effect_timing.py --install <root>`
checks short bursts, continuous effects, end replay and live scrubbing at two sizes.

Dialogue editing: `assets_tab 4`, `dialogue_select <bank 0..3> <Sound ID>` and
`click button_dialogue_load` open a line. `dialogue_preset <0..4>` selects a
retail head. Frame widgets include `button_dialogue_insert_frame`,
`button_dialogue_delete_frame`, `slider_dialogue_key_<index>` and
`combo_dialogue_add_phoneme`. `dialogue_export_path <file>` sets a new scratch
BIG path; `button_dialogue_export` writes the staged edits. State keys include
`dialogue_frames`, `dialogue_staged`, `dialogue_exported` and
`dialogue_head_ready`. `dialogue_subtitles_count` and
`dialogue_subtitle_name` expose the resolved text.big link;
`dialogue_search <query>` filters subtitle text, speaker and entry names within
the selected language and bank. `dialogue_search_results` and
`dialogue_search_first_id` expose its capped results; click
`dialogue_search_result_0` to load the first match.
`checkbox_dialogue_mute` switches to animation-only playback and
`dialogue_audio_muted` reports its state. `button_dialogue_reset_view`
restores the head camera; `dialogue_view <yaw radians> <pitch radians> <zoom>`
sets it for repeatable screenshots. `dialogue_head_yaw`,
`dialogue_head_pitch` and `dialogue_head_zoom` report the current view.
`dialogue_playing` covers both audio and muted
animation playback.
`tests/ui/dialogue_browser.txt` checks the retail Demon Door line at
ScriptDialogue Sound ID 5080. `tools/test_dialogue_edit.py` checks staged
edits across two banks and reparses the exported archive.
`pack_dest <pack folder>` selects
a Forge pack; `click button_dialogue_add_pack` saves staged lines as recipes.
`dialogue_pack_added` reports success, and the same test checks the manifest.
The test also runs `forge-tools mods build` on a scratch retail archive and
checks recipe versus whole-file precedence in both load orders. It checks
same-line recipe conflict reports and opens Mods > Check conflicts in the GUI.
It also checks the unsaved changes prompt and its *Review dialogue* action.

`tests/ui/selection_actions.txt` drives real RMB clicks, look/flight gestures,
the selection toolbar and popup actions. Diagnostics `selection_popup`,
`selection_inspector` and `selection_inspector_def` expose the visible state;
`assert_selected_ground` compares the primary selection height with terrain.
Widgets use `btn_selection_focus`, `btn_selection_properties`,
`btn_selection_actions`, `menu_selection_<focus|properties|duplicate|drop|delete>`
and `btn_selection_inspector_close`. The route changes only in-memory objects
and exercises the existing undo paths.

- `assets_tab 1`: open Models; `click btn_model_import` exposes the import form.
- `model_select <name|id>`: decode/select a render mesh; fails for a missing or undecodable entry.
- `model_search <text|->`: set the browser filter (`-` clears it).
- `model_orbit <yaw> <pitch> <zoom>`: deterministic preview pose in radians; pitch/zoom are bounded.
- `set world_detail_limit <1..32>`: set both manual and automatic map ceilings.
- `set world_detail_radius <100..1000>`: base radius in world metres.

`model_ready`, `model_selected`, `model_vertices`, `model_triangles`, `model_users`,
`model_wire`, `models_count`, `models_filtered` and `assets_tab` are state keys.
World diagnostics add `world_detail_memory_ceiling` and `world_detail_radius`
(the effective distance, including automatic scaling). Existing GPU telemetry
injection does not allocate memory or change system settings.

`set world_normal_blend <0|1>` toggles the terrain normal transition for matched
A/B captures (default 1); it adds no user-facing setting. Height blending remains
active. Material blending must also be enabled to exercise this diagnostic.

### Texture resources and object fades

`set world_texture_sharing <0|1>` and `set world_texture_mips <0|1>` are A/B
switches, both default 1. Set them before loading layers; they affect newly created
resources. Mips apply to opaque layer textures; cutout textures retain mip zero.
`dump_state` exposes `texture_pool_allocations`, `texture_pool_gpu_bytes`,
`texture_pool_identity_bytes`, `texture_pool_hits`, and `texture_pool_mipmapped`.
These are payload counts, excluding driver overhead and resources outside the pool.
`texture_retired_cpu_bytes` counts queued/in-flight identity pixels separately.
`wait_texture_cleanup` waits for that bounded CPU cleanup to finish, including
after switching away from the World viewport.

`set world_cutout_mips <0|1>` enables worker-prepared coverage-preserving mipmaps
for newly loaded world-detail cutouts (default 1). `texture_pool_cutout` counts
live allocations with that policy; `texture_pool_mipmapped` includes both opaque
and cutout mip chains. `set world_cutout_mask <0|1>` is a diagnostic white cutout-only
pass; hide water separately for coverage captures. `viewport_x`, `viewport_y`,
`viewport_width`, and `viewport_height` expose client-space capture bounds.

`python tools/test_world_cutout_quality.py --output build/world-cutout-quality`
compares slow pans and cutout-only coverage masks at Guild/Oakvale, checks cleanup,
and saves the report even if visual thresholds fail. The older texture-quality
tool explicitly disables cutout mips to keep its opaque-filtering comparison isolated.

`set world_smooth_objects <0|1>` compares the 0.6 s eased object transition against
the old 0.25 s linear transition. Both retain the 0.25 s terrain morph. With the new
transition, a held fraction of `elapsed_seconds / 0.6` corresponds to elapsed time;
use `/ 0.25` for the legacy diagnostic. Reversing progress retraces the same coverage.

Hidden, read-only checks (Pillow/numpy required):

- `python tools/test_world_texture_quality.py --output build/world-texture-quality`
  checks pixel-identical sharing, allocation savings, release back to overview totals,
  and reports a matched slow-pan filtering diagnostic.
- `python tools/test_world_object_transition.py --output build/world-object-transitions`
  compares held elapsed-time steps, peak large pixel changes and identical endpoints.
## World antialiasing diagnostics (2026-09-29)

`set world_cutout_cache 0|1` selects derived-mip reuse for the next map worker.
Disabling clears retained cache data on that worker. State snapshots from completed
workers: `world_cutout_cache_bytes`, `_entries`, `_hits`, `_builds`. Payload limit:
32 MiB, excluding container metadata and external upload/retirement owners.
`tools/test_world_cutout_cache.py --output build/world-cutout-cache` checks exact
cached/uncached pixels and unchanged GPU payload. Optional `--baseline-exe` runs
the uncached side with a saved earlier executable for algorithm comparisons.

Detail recovery diagnostics: `set world_detail_fail_prepare <map|->` throws from
that map's worker; `set world_detail_fail_upload <map|->` fails after a partial
upload attempt. `-` removes injection without clearing retry history.
`world_detail_failures` is a cumulative count; `world_detail_deferred` counts
currently wanted maps waiting out failure backoff. `wait_world_detail` requires
full recovery; `wait_world_detail_settled` permits deferred overview fallback.
`btn_world_retry_detail` clears retry delays without discarding healthy detail.
`tools/test_world_detail_recovery.py --output build/world-detail-recovery` checks
both injected failures, neighbour progress, resource cleanup and recovery.
Use `--cutouts` to keep foliage enabled and exercise derived-chain cache ownership
through those same failure/recovery paths.

Cancellation diagnostics: `set world_detail_hold_prepare <map|->` pauses that
map's worker after terrain preparation; `-` releases the hold. Cancellation also
releases it, including native shutdown. `wait_world_detail_held` waits for that
checkpoint. `wait_world_detail_idle` waits for preparation/upload/CPU retirement
to finish (use after disabling detail or leaving the view). State includes
`world_detail_worker_held` and cumulative `world_detail_cancelled`, counting
completed workers that actually observed cancellation, not every stop request.
`python tools/test_world_detail_cancel.py --output build/world-detail-cancel`
checks changed camera demand, content filters, detail off, 2D/tab exit, restored
pixels/resource accounting and native shutdown while held. The hold is a test
fixture, not a simulation of measured disk/decoding latency.

World content controls: `set world_objects 0|1`, `set world_creatures 0|1`,
`set world_plants 0|1`; changes invalidate streamed content, unchanged settings do
not reload. Widgets are `check_world_objects`, `check_world_creatures` and
`check_world_plants`. `world_detail_last_objects` / `world_detail_last_creatures`
count successfully placed root instances from the last uploaded map, excluding
attachments, and reset when detail is cleared. They are not residency totals.
`tools/test_world_content_filters.py --output build/world-content-filters` checks
checkbox combinations and changes during streaming against installed Oakvale data.

`tools/test_world_focus_precision.py --output build/focus-precision` requires
pixel-identical viewport captures across four orbit distances at fixed world poses.
It disables water and AA for this isolated comparison; `--exe` and `--baseline`
allow reporting a saved pre-fix executable without requiring equality.
`tests/ui/world_detail_underfoot.txt` checks one-map boundary priority and cached return.

`set world_aa 0|1|2|4` selects Auto, Off, 2x or 4x. State includes
`world_aa_mode`, `world_aa_samples`, `world_aa_support` (mask 1|2|4),
`world_aa_target_bytes`, `world_aa_rebuilds` and `world_aa_fallbacks`.
`set world_aa_test_limit 1|2|4` injects allocation failure above that sample count,
after partial allocation, to exercise cleanup/fallback. Default 4 disables injection.
`tests/ui/world_aa.txt` expects a test adapter supporting 4x colour and depth.

`set world_cutout_aa 0|1` compares hard texture cutoffs with alpha-to-coverage
on the same MSAA target. Default on; 1x rendering always uses the hard cutoff.
`python tools/test_world_cutout_aa.py --output build/world-cutout-aa` compares
1x/2x/4x images at Guild and Oakvale, foliage coverage, texture bytes, motion
energy/large pixel changes and held fade endpoints/monotonic coverage. Mean
absolute motion is retained as a diagnostic: smoothing can spread smaller changes
across more pixels without lowering their absolute sum. Requires 4x-capable hardware.
The fixture supplies simulated memory headroom; it does not allocate that memory.
`tools/test_world_flight.py --aa 4` holds the requested upper bound throughout
the route; normal device/memory fallback still applies.

World population and hover diagnostics (2026-09-29):

- `assert_world_ray ox oy oz dx dy dz <map|*|->` uses Fable Z-up coordinates.
  Require a named map, any hit, or a miss. Tests published overview triangles,
  including pending placements, not detailed object meshes.
- `set world_overview_batch_limit 1..32` changes the count guard while preserving
  the 2 ms soft budget. Default 32. `world_overview_upload_frames` and
  `world_thumbnail_upload_frames` count frames doing their respective upload work;
  `world_overview_first_map` names the first published 3D map.
- Run `python tools/test_world_population.py --exe build-review/FableForge.exe --output build/world-population-final`
  for guard 4/32 comparisons, nearest-map-first publication, exact final pixels and
  texture bytes, native rays and an actual cursor over void.
- `set world_detail_upload_ms 0|2|4` selects adaptive/default or fixed 2/4 ms
  slices. `world_detail_upload_frames` counts cumulative upload work frames.
  One driver allocation can exceed the slice. The fixed comparison in
  `build/world-lods-review` uses 67/36 upload frames with identical final pixels.

Mesh LOD diagnostics: `set world_object_lods 0|1`
selects base geometry or authored LOD/distance selection. Frustum culling retains
its independent switch. State includes `world_drawn_object_parts`,
`world_culled_object_parts`, `world_lower_lod_parts`, `world_object_draw_calls`
and `world_object_distance`. Parts are material ranges, including both levels
during a transition, not unique scene objects. Base-only meshes still participate
in size/distance visibility. Rules apply across loaded map tags; residency has
separate adaptive limits and a soft view-cone priority. Consult the current
handoff for validation evidence and limits.

`set world_terrain_lods 0|1` selects full indices or error-selected terrain patch
levels. `world_terrain_triangles`, `world_terrain_full_triangles` and
`world_terrain_coarse_patches` describe the currently submitted visible patches.
`tools/test_world_lods.py --exe build-review/FableForge.exe --output build/world-lods --upload-compare`
checks authored LODs, culling pixel parity, terrain reduction/disable, camera
recovery and fixed upload slices. No FPS claim follows from these diagnostics.

`world_label` reports the active hover name, falling back to selection. The label
is anchored at the viewport top center in both world modes and absent when both
names are empty. `tests/ui/world_label.txt` verifies precedence, void deselection
and selection persistence, with screenshots for placement review.

Outer scenery / explicit draw distance:

- `slider_world_detail_radius` now displays Draw distance.
  `set world_detail_radius 100..1000` sets requested visible range independently
  of near-detail map count; `world_object_distance` reports the renderer range.
  Scenery preload radius extends beyond that visible range.
- `set world_scenery 0|1` toggles the separate coarse scenery service for
  comparison, without disabling near detailed maps. Normal scenery requires
  World 3D and the World scenery and detail checkbox.
- `wait_world_scenery` waits for preparation/upload/retirement and resident fades.
  Memory/map-cap/retry constrained demand may remain; settled does not mean every
  requested map loaded.
- State includes `world_scenery_maps`, `world_scenery_pending`,
  `world_scenery_bytes`, `world_scenery_memory_limit`, `world_scenery_blocked`,
  `world_scenery_failures`, `world_scenery_drawn_parts`, `world_scenery_names`
  and `world_scenery_holds` (updates retaining outgoing detail while fallback
  coverage is unavailable).
  Bytes are accounted resources, not total process VRAM; parts are material ranges.
- `python tools/test_world_distance.py --exe build-review/FableForge.exe --output build/world-distance-final`
  exercises the actual slider at fixed 350-metre camera height without entering
  another map. Final PASS: scenery 10 -> 46 maps, 27,613 changed pixels and exact
  restored pixels. Hysteresis may retain additional maps on return.
- `python tools/test_world_scenery_lifecycle.py --exe build-review/FableForge.exe --output build/world-scenery-lifecycle-final`
  checks memory pressure, recovery, disable/re-enable, in-flight invalidation and
  content filters. The final run passes; an optional fully hidden coarse copy of
  the near map does not fail residency restoration.

`watch_world_coverage <map>` checks the named map on every automation tick;
`watch_world_coverage -` stops and reports watched frames. It requires aggregate
CPU coverage `detail + (1-detail)*preparedCoarse >= 0.999`. This detects handoff
state gaps, not pixel-level GPU holes or differences between authored silhouettes.
`tests/ui/world_scenery_handoff.txt` passes 129 watched frames with 29 recorded
fallback holds in the latest admission-final run (30 in the earlier run).
Begin watching an already-covered map; it is not a cold-loading
completeness assertion. See the handoff for resource limits and validation scope.
The existing cancellation fixture also passes with this integration in
`build/world-scenery-cancel-final`: five deterministic cancellations, held-worker
shutdown, zero failures and exact restored pixels/texture payload.

`world_scenery_priority_evictions` counts actual resident removals to admit a
higher-priority scenery request. Run
`python tools/test_world_scenery_priority.py --exe build/FableForge.exe --output build/world-scenery-priority-final --timeout 300`
to exercise a 256 MiB scenery tier using synthetic 1 GiB/zero-usage telemetry.
The route moves 150 metres and rotates 0.5 radians from a Guild view at height
350, then holds still for 240 frames. It requires an admission eviction, retained
overlap plus newly admitted maps, bounded resources and settled demand, followed
by identical resident names, bytes, drawn parts, eviction counter and pixels.
The first run exposed repeated admission/eviction and is retained separately;
the final run after feasibility-preflight fixes passes in
`build/world-scenery-priority-final/report.json`: two movement-triggered priority
evictions, then unchanged residency/resources/counter/pixels for 240 frames,
within the 268,435,456-byte limit. This is a stability/resource regression,
not a timing benchmark or simulated VRAM allocation.

Final normal-build reruns also pass in `build/world-distance-admission-final`,
`build/world-scenery-handoff-admission-final.log` and
`build/world-scenery-cancel-admission-final`. Distance response/restored pixels,
129-frame handoff coverage and five deterministic cancellations retain the
documented results after the admission and mesh-ownership fixes.

Particle playback: `effect_preview_play 0|1` pauses/resumes the selected effect,
`effect_preview_restart` resets time and random seed, and
`effect_preview_step <ticks>` pauses and advances 1?600 fixed 30 Hz ticks.
State keys: `effect_preview_time`, `effect_preview_particles`,
`effect_preview_supported`, `effect_preview_warnings`, `effect_preview_drawn`,
and `effect_preview_playing`. The viewport widget is `effect_preview_image`.
`tests/ui/effect_playback.txt` captures fixed-time, paused and replayed previews.

List-property checks: `text_input <text>` sends UTF-8 typing events to the focused
field (it appends, so select/clear existing text first). `assert_list <CTC> <base>
<count> [slot expected-definition]` checks the selected object's list. Widgets:
`list_add_<CTC>`, `list_entry_<slot>_<CTC>`, `list_remove_<CTC>_<slot>`,
`input_list_definition_search`, and `list_choice_<definition>`.
`tests/ui/container_lists.txt` exercises contents/family editing with real mouse
and keyboard events, undoing all in-memory changes without saving.
`tests/ui/effect_forces.txt` captures authored orbit/attractor previews at fixed ticks.

`assert_initial_position` checks that the selected creature's three saved
InitialPos fields match its physics position plus map XY origin.
`tests/ui/creature_initial_position.txt` exercises movement, rotation, scaling,
drop, duplicate-then-move and paste, then undoes every change without saving.

Component override regression: `tests/ui/component_overrides.txt` uses
`property_group_<CTC>`, `property_reset_<CTC>_<key>`,
`property_set_<CTC>_<key>`, `property_override_value`,
`property_override_bool`, `property_bool_TRUE/FALSE` and
`property_override_apply`. No install writes; full byte-exact undo.

Mesh playback: `tests/ui/effect_meshes.txt` and
`python tools/test_effect_mesh_pixels.py` verify mesh-only animation, paused and
restarted pixels, and switching back to sprites. `assert_effect_meshes` requires
simulated meshes plus submitted geometry. State adds `effect_preview_meshes`,
`effect_preview_meshes_drawn` and `effect_preview_mesh_triangles`; particle count
counts the shared live population rather than duplicated renderer instances.

`effect_preview_authored_meshes` counts distinct loaded models using valid authored
bounds. `snapshot_mesh_orientation` and `assert_mesh_rotated` compare the first
mesh particle quaternion for the selected deterministic rotating fixture.

Selection-height widgets: `menu_selection_height`, `btn_selection_height`,
`input_selection_height`, `btn_selection_height_apply`, `btn_selection_height_cancel`.
`selection_height_popup` reports modal visibility. `tests/ui/selection_height.txt`
uses actual Ctrl+H/menu and text entry with complete in-memory undo.

`menu_selection_palette` reveals the primary selection in the definition palette.
`palette_definition` and `palette_reveal_pending` expose the selected definition
and pending tree/scroll request; `tests/ui/show_in_palette.txt` covers object and
creature routes, Terrain tool transition and document preservation.

Light-volume coverage: `tests/ui/effect_lights.txt` and
`tools/test_effect_light_pixels.py`. Widget `check_effect_light_volumes` hides/shows
the wire overlay without changing the camera. `effect_preview_lights`,
`effect_preview_light_volumes` and `effect_show_light_volumes` expose live light
count, intersecting drawn volumes and the toggle. `btn_effect_frame` reframes.

Install destination checks expose `pack_destination` (generic path, empty for
direct game writes) and `mods_picks` (loaded conflict-pick count).
`tools/test_install_switch_ui.py` exercises different pack lists and picks across
two roots, an invalid root, a refused dirty switch and returning to the first root.
Compact asset navigation uses `combo_assets_tab` and `asset_page_0` through
`asset_page_4`; wide panels retain `seg_assets_tab`.

Mods rows expose `mod_row_<index>` (wrapped name/drag handle),
`mod_enabled_<index>`, `mod_up_<index>`, `mod_down_<index>` and
`mod_remove_<index>`. Right-clicking a pack name exposes dependency checkboxes
as `mod_requires_<row>_<other-row>`. Indices follow the current displayed order.

`selected_mesh_instances` counts uploaded mesh instances belonging to the
selected thing. The mesh-import UI check waits for scene loading and requires
one instance before capturing the cube, then verifies byte-exact placement undo.

Dialogue frame navigation: `input_dialogue_frame` accepts a one-based frame number;
`dialogue_frame` reports the selected frame. `key_down Enter` / `key_up Enter`
commit typed input in keyboard workflows.

Dialogue mouth shapes: `button_dialogue_remove_key_<index>` removes a shape from
the selected frame. `combo_dialogue_add_phoneme` stays registered at the four-shape
limit, where it is disabled. Its options use `dialogue_add_shape_<symbol>` (AH, EE,
MM, OH, SZ, WW); shapes already present are omitted. The pack-creation check also
covers empty/full frames, replacement, influence values and scratch export.

Dialogue undo: `button_dialogue_undo` / `button_dialogue_redo` and real Y/Z key
events exercise the current line's history. `dialogue_can_undo` /
`dialogue_can_redo` expose availability. The existing `undo` / `redo` commands
route to lip sync when its inline editor is open. `tools/test_dialogue_undo.py`
checks gesture grouping, shortcuts, line switches, reset and pack-save boundaries
against independently read archive/recipe data.

Definitions inspection uses `assets_tab 6`, `wait_definitions`, `definition_bank game.bin|frontend.bin|script.bin`, `definition_search <query|->`, `definition_select <exact-name|entry-index>`, and `assert_definition_field <field-name> <formatted-value>`. The corresponding `_refused` bank and selection commands check rejected inputs. Exact names take priority over numeric indices; duplicate names require selecting a row.

Definition scalar drafts use `definition_edit <field-index>`, `definition_value <value>`, `definition_save`, and `definition_cancel`. The edit/value/save `_refused` variants check refusals. `definition_editing` and `definition_edit_failed` report draft state. Saving reselects the explicit native index, even when another record has a numeric name.

CWideString drafts accept strict UTF-8 up to 127 bytes (not characters). Real
`input_text` includes supplementary input through 32-bit ImGui characters. C0/C1
controls refuse at Save; malformed UTF-8 and byte-overflow values refuse in the
automation setter. Narrow strings and clone names retain ASCII-only input.
`definition_wide` independently checks native UTF-16LE bytes and malformed-original
refusal. `definition_wide_retail` drives both window sizes against read-only retail
inputs, compares every record/table/name byte and restores only owned output.

`assert_font_glyph <body|bold|title|small> <hex-codepoint>` checks actual source
glyph availability without accepting the replacement glyph; `_missing` checks
absence. Malformed code points, surrogates and unknown roles fail the assertion.
`FABLEFORGE_AUTOMATION_FONT_DIR` overrides the font directory only when automation
is active. `font_fallback` checks all four roles across repeated scale/atlas rebuilds
and system/no-font/primary-only fixtures. Optional absent language/emoji groups are
explicitly reported in its `skipped_glyph_groups`; `--require-wide` requires the
installed CJK and emoji sources for a stronger local proof. No system fonts are
modified or bundled; only primary-font copies in owned scratch fixtures are used.

Definition clone drafts use `definition_clone` (or the real
`btn_definition_clone` control), `definition_value <new-name>`, `definition_save`
and `definition_cancel`. `definition_clone_refused` checks draft/load refusal;
`definition_cloning` distinguishes a clone from a field draft. Real name entry
uses `input_definition_edit_value`; `btn_definition_save` and
`btn_definition_save_confirm` exercise the Create confirmation. The
`definition_clone_ui` package workflow covers game/frontend/script banks,
numeric/unnamed/duplicate-name donors, folder/pack outputs, stale sources,
invalid names, cancellation, Close/Install guards and paired Restore at both sizes.

`definition_group` reports the validated game-bank group entry index, or `-`. `btn_definition_group` follows it by index after the current frame, protecting numeric names and the current decoded view.

Definition vector drafts expose `input_definition_vector_0`, `_1` and (for C3DVector) `_2` for real coordinate input. `definition_value` accepts the existing comma-separated writer syntax for these drafts. The dedicated `definition_vectors` package workflow independently verifies every other payload byte, exact no-ops and malformed input/layout refusals at both sizes.

Definition colour drafts expose `input_definition_colour_picker` (swatch), `definition_colour_popup` while its picker is open, and `input_definition_colour_0` through `_3` (R/G/B/A). Actual channel input is clamped to 0-255; `definition_value` uses the writer RGBA syntax and Save refuses out-of-range values. The definition_colour_edit workflow checks both sizes, actual picker saving, exact no-ops and native BGRA byte deltas.

Definition tree widgets: `definition_type_node_<type-index>`, `definition_group_node_<type-index>_<native-group-index|none>`, and `definition_row_<native-index>`. `definitions_grouped` counts confirmed group targets.

`definition_model_<field-index>` identifies an object model link. `definition_model` state reports the confirmed native model ID, or `-` for unsupported fields/banks.

`definition_texture_<field-index>` identifies a terrain-theme texture link. `definition_texture_links` counts eligible positive native texture references in the selected game-bank record.


`definition_name_reference_<field-index>` identifies a resolved CDefString name label. `definition_name_ref_<field-index>` reports the name or `-`; `definition_name_refs` counts resolved fields. Labels use owning-bank byte offsets, including unused or duplicate names, and refresh after bank reload.


`combo_definition_edit_name` opens the existing-name chooser for four-byte CDefString drafts. `input_definition_name_search` filters pool names or offsets; `definition_name_option_<byte-offset>` and `definition_name_option_none` select an existing name or -1. Duplicate pool names have separate offset choices. Saving refuses changed references that are not exact string starts in the captured owning pool.

`combo_definition_edit_group` opens the confirmed game-bank int32 GroupDef picker. `input_definition_group_search` filters target names or native indices; `definition_group_option_<native-index>` selects that explicit index, not a name lookup. Only confirmed owner/family combinations participate. Changed wrong-family, missing or stale target identities refuse; unchanged legacy bytes remain a no-op. Synthetic picker and read-only retail-copy workflows cover publication, exact deltas and explicit-output Restore.
