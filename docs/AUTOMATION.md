# GUI automation (`--auto <script>`)

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
| `wait_maps` | until the map list is read from FinalAlbion.wad |
| `wait_ready` | until the map list is read **and** the texture context finished (ok or failed) |
| `wait_loaded` | until the selected map's preview is on the GPU |
| `wait_export` | until the running export finished |
| `wait_batch` | until a batch export finished |
| `frames <n>` | render n frames |
| `select <map>` | select by name (or key `file:<stem>` for loose files) through the state path |
| `filter <text>` | set the search box (empty clears) |
| `click <widget>` | synthetic click at the centre of a registered widget (3 frames: move, press, release) |
| `open <path.lev>` | open a loose .lev (same path as drag-and-drop) |
| `orbit <dyaw> <dpitch>` / `zoom <steps>` / `look <dyaw> <dpitch>` / `fly <fwd> <strafe> <rise> <secs>` | camera through its API |
| `camera <x> <y> <z> <yaw> <pitch> <dist>` | look at a Fable map-local point |
| `mouse_move <x> <y>` \| `mouse_move <widget>` (its centre) \| `mouse_move viewport`, `wheel <dy>`, `mouse_delta <dx> <dy>`, `mouse_down\|mouse_up left\|right\|middle`, `key_down\|key_up W\|A\|S\|D\|Q\|E\|F\|Shift\|Alt\|Ctrl\|Escape` | raw input through ImGui (tests the real control path) |
| `snapshot_camera` / `assert_camera_moved [min]` | camera position delta check |
| `wait_foliage`, `set preview_foliage 0\|1`, `set foliage 0\|1` | foliage preview / export |
| `mode textured\|wireframe\|walkable\|height` | view mode |
| `set <key> <value>` | export settings: `format glb\|obj`, `textures 0\|1`, `layers 0\|1`, `walkable 0\|1`, `texels n`, `tile f`, `gain f`, `up y\|z`, `world 0\|1`, `things 0\|1`, `outdir path` |
| `export` / `export_all` / `export_region` | start a single / batch / region export (state path; use `click btn_export` for the UI path) |
| `edit 0\|1` | switch the right panel between Export and Edit (opens the level document) |
| `gizmo 0\|1\|2\|3` | select / move / rotate / scale tool |
| `pick <u> <v>` | ray-pick at viewport-relative (u, v) in [0,1]; selects the hit thing |
| `select_thing <index>` / `select_def <DEFINITION>` | select by .tng index / first thing with that DefinitionType |
| `move_thing <dx> <dy> <dz>` / `rotate_thing <deg>` / `scale_thing <factor>` / `ground_thing` | edit the selection (one undo step each) |
| `duplicate_thing` / `delete_thing` / `undo` / `redo` | structural edits on the whole selection (the objects layer reloads; `wait_foliage` waits for it) |
| `textures_tab 0\|1`, `texture_select <name\|label\|id>`, `texture_export [png]`, `texture_replace <image>`, `texture_add <NAME> <image> [dxt1\|dxt3\|argb8888]` | the Textures tab (state `textures_mode`, `textures_count`, `texture_selected`; widgets `list_textures`, `img_texture`, `btn_tex_*`) |
| `place_emitter <EFFECT> [scriptname]` | a PARTICLE_EMITTER_PLACEABLE playing the effects.big entry, half a unit above the ground at the view centre (state `effects` = names loaded; widgets `list_effects`, `btn_place_emitter`) |
| `set_entrance` | the map's region entrance (FinalAlbion.gtg, this map's WLD slot) at the view centre, facing the camera (state `entrance` = `x,y,h` or `-`; widget `btn_set_entrance`) |
| `preset_place <name>` / `preset_save <name>` | place a preset (shipped `presets/` or the user folder) at the view centre as a selected group; save the selection as a user preset (state `presets` = how many are listed) |
| `select_toggle <index>` / `select_added` / `copy` / `paste` | Ctrl+click a thing into/out of the selection; select the first thing the Changes list reports as added; copy the selection as a fragment; paste it at the view centre (state `selection_count`) |
| `place <DEFINITION> [scriptname]` | place a new thing at the camera focus, on the ground (`CREATURE_*` as the retail AICreature block) |
| `place_village <VILLAGE_DEF> [scriptname]` / `village_member <uid\|scriptname\|0>` | place a Village thing at the camera focus; the selected thing joins/leaves a village (state `villages`, `selected_village`) |
| `place_fishing_spot [OBJECT_DEF\|-] [scriptname]` | a `MARKER_FISHING_SPOT` at the camera focus on the ground; the optional def is the first catch (`CTCContainerRewardHero`), empty or `-` = the game's fish table; the ScriptName is what the in-game `--things` probe looks up |
| `link_install` / `link_remove` / `link_go` / `link_spawn` / `link_ping` / `link_poll` | the ForgeFSE live link (state `link_installed`, `link_ready`, `link_hero_map`) |
| `reseat_things` | every object that stood on ground changed since the last save follows it (offset kept; one undo step) |
| `add_theme <ENGINE_THEME>` | add a ground theme from game.bin to a free LEV palette slot and select it for painting (state `paint_theme`, `palette_named`); one undo step |
| *(toasts)* | every warning / error / success log line also shows for 6 s in the viewport's top-right corner (state `toasts` = how many are up); the long jobs' busy button reads `<verb>: <stage>  (N s)` from the job thread |
| `def_search <text>` | fills the *Add an object* box (rows show a mesh thumbnail) |
| `theme_search <text>` | fills the *Add a ground theme from the game* box (the list shows a swatch per theme) |
| `drop <file>` | what a file dropped on the window does: `.lev` opens as a loose map, an image lands in the custom-texture input (Terrain tab, paint mode) |
| `click chip_grid` | the LEV cell grid overlay (state `grid`); state `cursor_ground` = `x,y,h` under the mouse when it is over the ground, `-` otherwise |
| `set uiscale <0.8..1.5>` | the *Interface > Text size* factor (state `ui_scale`; fonts rebuild) |
| `tour <0..2\|-1>` | the first-run tour callout (state `tour_step`; widgets `btn_tour_next`, `btn_tour_skip`); starts by itself after the first Setup panel |
| `help 0\|1` | the keyboard/mouse cheat-sheet overlay (also `?` / F1 / the header `?`; state `help_open`, widgets `btn_help`, `btn_help_close`) |
| `edit_tab <0..3>` | the Edit panel's sub-tab: 0 Objects, 1 Terrain, 2 Actors, 3 Level (state `edit_tab`; widget `seg_edit_tab`). Terrain and the terrain tool follow each other; placing a spawner/village opens Actors, a viewport pick from Terrain/Level opens Objects |
| `dismiss_rule <creature\|spawner\|region>` | what the engine-rule notice's *Got it* does (state `rule_notice` = the key shown, `-` for none; widget `btn_rule_<key>`) |
| `compact_stb` / `wait_compact` | compact the static-map bank in the background (Setup panel's *Compact the bank*); log line `compact: A MB -> B MB, N payloads verified byte-identical` |
| `custom_theme <png> <NAME> [donor] [cliffPng]` | a ground theme from a PNG (textures.big + game.bin append), added to the palette and selected; the texture/def context reloads (`wait_ready`) |
| `drag_gizmo <dx> <dy>` | press on the selected pivot and drag by (dx, dy) window pixels through the real gizmo |
| `frame_selected` | frame the camera on the selection |
| `terrain_mode <0-5>` | terrain tool: 0 raise, 1 lower, 2 flatten, 3 smooth, 4 walkable, 5 blocked (also selects the tool) |
| `brush <radius> <strength>` / `terrain_stroke <x> <y> <seconds>` | brush size; one stroke at a map-local point |
| `new_level <name> <x> <y> [region]` / `wait_new_level` | fill the "New level" card and install (BWD/WLD/WAD/STB under the install), wait for it |
| `new_level_own_region 0\|1` | take over a filler region slot (+ minimap bake) with the next `new_level` |
| `new_level_blank <theme slot> <height> [<w> <h>]` | switch the card to Blank with that ground theme / height (and a retail size) before `new_level` |
| `deploy_terrain` / `wait_terrain` | write .lev + WAD + STB chunk under saveroot (worker thread) |
| `set unsaved_prompt 0\|1` | scripted runs skip the unsaved-changes prompt unless opted in |
| `dump_log` | copy the activity log into the script log |
| `set saveroot <dir>` | where `save_level` / `deploy_level` write (default: the install) |
| `save_level` / `deploy_level` | write the loose .tng / replace the WAD entry under saveroot |
| `screenshot <png>` | save the next presented frame |
| `assert_file <path>` | file exists and is non-empty |
| `assert_file_contains <path> <text...>` | the file exists and contains the text (the rest of the line, e.g. `ScriptName UiNamedBarrel;`) |
| `assert_state <key> <value>` | see `dump_state` for keys |
| `assert_widget <widget>` | the widget was drawn this frame |
| `assert_log <text>` | some app log line contains the text (background job notes, e.g. `1 stitched`) |
| `world_tab 0\|1`, `world_select <map>`, `world_move <map> <x> <y>`, `world_move_refused ...`, `world_owner <map> <region>`, `world_sees <region> <map> <0\|1>`, `world_stitch <0\|1> [feather]`, `world_revert`, `world_undo`, `world_redo`, `world_apply`, `wait_world` | the World tab: queue moves / region edits, stitch seams after the apply (feather -1 = auto), write them; state keys `world_*` |
| `dump_state` | write every state key to the log |
| `quit` | end the run |

`${TEMP}` and `${USERPROFILE}` expand inside a line. Waits time out after 60 s.

## Registered widgets

`btn_change_install`, `input_filter`, `group_<Group>`, `row_<key>`, `row_selected`,
`viewport`, `chip_textured|wireframe|walkable|height|reset`, `seg_format`, `seg_up`,
`toggle_textures`, `toggle_layers`, `toggle_walkable`, `slider_texels`, `slider_tile`,
`input_outdir`, `btn_export`, `btn_export_all`, `btn_cancel_batch`, `btn_open_folder`,
`seg_panel`, and in Edit mode `seg_gizmo`, `toggle_snap`, `drag_px|py|pz|yaw|scale`,
`btn_ground`, `btn_focus`, `btn_duplicate`, `btn_delete`, `input_thingsearch`,
`input_defsearch`, `btn_place`, `btn_undo`, `btn_redo`, `btn_save`, `btn_deploy`,
`btn_deploy_confirm`, `btn_revert`, `seg_terrain_mode`, `seg_terrain_walk`, `slider_radius`,
`slider_strength`, `btn_terrain_deploy`, `btn_terrain_deploy_confirm`, `btn_unsaved_save`,
`btn_unsaved_discard`, `btn_unsaved_cancel`.

A widget is only registered on frames where it was drawn, so expand a group
(`click group_Arena`) before clicking one of its rows.

## In-game (the retail engine as the oracle)

`python tools/ingame/ingame_terrain_test.py` runs the real game and compares its ground
heights with the LEV; see docs/EDITOR.md "Testing in the running game without a human".
`python tools/verify_engine_lzo.py` (in `check_all`) decodes our LZO frames with the
engine's own assembly decoder under emulation.

## Suites

* `tests/ui/editor.txt` — Edit mode: select, move, rotate, scale, duplicate, undo back to
  clean, place, delete, a real gizmo drag, save into `build/ui_editor_install`.
* `tests/ui/smoke.txt` — happy path: install detected, textured preview, all view modes,
  orbit, filter, export via button. `tools/ui_smoke.py` runs it and adds GLB validation
  and screenshot pixel assertions.
* `tests/ui/paths.txt` — tree/row/toggle clicks, OBJ + untextured export, loose file, batch.
* `tests/ui/controls.txt` — Unreal-style camera through injected input: RMB+W flies, RMB drag looks, MMB pans, F frames, Alt+LMB orbits.
* `tests/ui/foliage.txt` — two-stage foliage load, chip toggle, export with the Foliage node.
* `tests/ui/region.txt` — `Export region` writes every map of a region in world coordinates.
* `tests/ui/noinstall.txt` — run with `--install <bogus>`: honest empty state, no crash.

`python tools/check_all.py` runs the unit tests, the retail CLI smoke and all UI suites.
