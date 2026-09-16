# GUI automation (`--auto <script>`)

The GUI can drive itself from a plain-text script. It is how the UI is tested:
real widgets get real synthetic clicks, state is asserted through the same
accessors the UI uses, and the backbuffer is saved as PNG for pixel checks.

```
AlbionAtlasGUI.exe --auto tests/ui/smoke.txt [--install <root>] [--size 1440x900]
```

Exit code is 0 when every assertion passed. A log lands next to the script
(`<script>.log`) ending in `RESULT PASS` / `RESULT FAIL` plus the failures.
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
| `mouse_move <x> <y>` \| `mouse_move viewport`, `mouse_delta <dx> <dy>`, `mouse_down\|mouse_up left\|right\|middle`, `key_down\|key_up W\|A\|S\|D\|Q\|E\|F\|Shift\|Alt\|Ctrl\|Escape` | raw input through ImGui (tests the real control path) |
| `snapshot_camera` / `assert_camera_moved [min]` | camera position delta check |
| `wait_foliage`, `set preview_foliage 0\|1`, `set foliage 0\|1` | foliage preview / export |
| `mode textured\|wireframe\|walkable\|height` | view mode |
| `set <key> <value>` | export settings: `format glb\|obj`, `textures 0\|1`, `layers 0\|1`, `walkable 0\|1`, `texels n`, `tile f`, `gain f`, `up y\|z`, `world 0\|1`, `things 0\|1`, `outdir path` |
| `export` / `export_all` / `export_region` | start a single / batch / region export (state path; use `click btn_export` for the UI path) |
| `screenshot <png>` | save the next presented frame |
| `assert_file <path>` | file exists and is non-empty |
| `assert_state <key> <value>` | see `dump_state` for keys |
| `assert_widget <widget>` | the widget was drawn this frame |
| `dump_state` | write every state key to the log |
| `quit` | end the run |

`${TEMP}` and `${USERPROFILE}` expand inside a line. Waits time out after 60 s.

## Registered widgets

`btn_change_install`, `input_filter`, `group_<Group>`, `row_<key>`, `row_selected`,
`viewport`, `chip_textured|wireframe|walkable|height|reset`, `seg_format`, `seg_up`,
`toggle_textures`, `toggle_layers`, `toggle_walkable`, `slider_texels`, `slider_tile`,
`input_outdir`, `btn_export`, `btn_export_all`, `btn_cancel_batch`, `btn_open_folder`.

A widget is only registered on frames where it was drawn, so expand a group
(`click group_Arena`) before clicking one of its rows.

## Suites

* `tests/ui/smoke.txt` — happy path: install detected, textured preview, all view modes,
  orbit, filter, export via button. `tools/ui_smoke.py` runs it and adds GLB validation
  and screenshot pixel assertions.
* `tests/ui/paths.txt` — tree/row/toggle clicks, OBJ + untextured export, loose file, batch.
* `tests/ui/controls.txt` — Unreal-style camera through injected input: RMB+W flies, RMB drag looks, MMB pans, F frames, Alt+LMB orbits.
* `tests/ui/foliage.txt` — two-stage foliage load, chip toggle, export with the Foliage node.
* `tests/ui/region.txt` — `Export region` writes every map of a region in world coordinates.
* `tests/ui/noinstall.txt` — run with `--install <bogus>`: honest empty state, no crash.

`python tools/check_all.py` runs the unit tests, the retail CLI smoke and all UI suites.
