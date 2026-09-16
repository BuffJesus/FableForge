# The Albion Atlas editor

Albion Atlas started as an exporter; the editor makes it the tool the community
used the leaked Lionhead debug build for, on retail data, with undo, and without
the crashes. The engine formats are written by FableForge's `forgecore`
(vendored; `tools/sync_forgecore.py` keeps it byte-identical with the upstream).

## What works now (things)

* **Selection**: click in the viewport (CPU ray against every instance's real
  mesh, so you hit what you see), or the *Objects in this map* list. Children
  spawned by a mesh's `CREATEOBJECT` dummies (doors, windows, Arena stands)
  select their parent thing and move with it.
* **Transform**: ImGuizmo gizmo (Q select, W move, E rotate, R scale, optional
  snap), or the numeric fields. While dragging only the preview moves; the
  document gets one undo step on release. Frames use the engine's own object
  matrix (`CalcObjectMatrix`), so what you see is what the game composes.
* **Duplicate / delete / place**: verbatim block copies with a fresh UID in the
  file's `0xFFFFFE00` namespace, `thingplacer` blocks in retail field order
  (`Object` for `OBJECT_*`, `Building` for `BUILDING_*`). Placement lands on
  the LEV terrain height (bilinear, like the retail placer).
* **Undo/redo**: snapshot based (128 steps); indices are re-derived from UIDs.
* **Save / deploy**: `Save .tng` writes the loose file; `Write into
  FinalAlbion.wad` replaces the archive entry through `forge::wad::repack`
  (same-size payloads are patched in place, larger ones appended; every byte
  the reader does not interpret is preserved). One-time `.atlas-orig` backups.
  The game loads levels from the WAD, so deploy is what makes edits visible
  in-game; saved games cache region entities, so start a new game or enter the
  region fresh to see them.

## What works now (terrain)

* **Brushes**: raise / lower / flatten / smooth on the LEV heightfield
  (`forge::terrain::applyBrush`, smooth radial falloff), walkable / blocked
  cell painting, and **ground-theme paint** (`Paint ground` + a picker over
  the map's LEV palette): `forge::terrain::applyThemeBrush` blends the chosen
  slot into the cell's three blend slots keeping the retail invariant
  (weights sum to 255). After a theme stroke the preview re-bakes the ground
  albedo from the LEV (`engineLayers=false`) so the painted material shows.
  Live preview: the renderer rebuilds the terrain vertex buffer
  in place with recomputed normals every frame of a stroke; the brush ring is
  projected onto the ground. One undo step per stroke, on the same stack as
  the object edits.
* **Deploy** (`Save terrain into the game`), in this order:
  1. loose `data/Levels/FinalAlbion/<map>.lev` (`lev::File::save`, only the
     cell bytes change);
  2. the `.lev` entry in `FinalAlbion.wad` (`wad::repack`, same size so it is
     patched in place);
  3. the map's terrain chunk in `FinalAlbion_RT.stb`: `stbbake::bakeHeightfield`
     resamples every composed background patch and foreground layer mesh from
     the LEV (heights, normals, direction masks), re-encodes the vertex blocks
     with the engine's own `CRangeCompressor` (`rangecodec::encodeNative`, a
     port of the FableWin debug build; 325/325 retail blocks reproduce
     byte-exact) and the frames with our lzo1x encoder (within 1% of
     lzo1x-999), re-lays the foreground frames in their 2048-aligned slots,
     rewires the patch directory and refits the quadtree Z bounds and the
     camera height bounds in the common record. The neighbouring maps (WLD
     regions that own this map: contains + sees, overlapping placement) are
     loaded from the WAD so the shared-edge vertices sample them like the
     retail bake did. The chunk keeps its size and is written in place.
  One-time `.atlas-orig` backups of all three files. Baking StartOakValeWest
  (128x224, 113 patches + 112 layer frames) takes about 4 s.
* **Verified**: the identity bake reproduces every vertex height of the
  retail chunk; a sculpted hill bakes with max |dh| = 0 against the LEV on
  all 94k vertices; baking the result again works (a second edit after a
  deploy). Structural checks pass (`forge stb foregroundinfo --verify-roundtrip`,
  `backgroundtreeinfo --validate-only`, directory spans == frame lengths).
  **In-game verified** (2026-09-16) with `tools/ingame/ingame_terrain_test.py`:
  a +6 hill deployed into the real install, the running engine's own ground
  query (`CWorldMap::GetGroundSizeZAt`) returns the edited heights at 121/121
  sample points and the hero teleported onto the hill stands at the new height.
  Saves cache region state, so enter the region fresh to see an edit.
* **Theme paint deploy**: when ground themes were painted the bake runs with `rebuildTopology`
  (`ReadThemesAndCreateLayers`: every foreground layer mesh is regenerated
  from the LEV themes so a new material region gets its own passes) and
  `rebuildDirectionMask`; the palette's ENGINE_THEME materials come from the
  texture context's `ThemeLibrary`. **In-game verified** (2026-09-16):
  GROUND_PATH_DRYMUD_GREEN painted into Oakvale West, deployed, the harness
  loads the region without a crash and ground heights still match 121/121.

## The engine's LZO decoder (why 0.4.0 crashed)

The landscape loader does not use LZO's C decoder: `LoadCompressed`
(0x00BE8920) calls `lzo1x_decompress_asm_fast` (0x00C069D0, LZO's i386
assembly decoder). It differs from the C decoder in one place: after an
*initial* literal run of 1..3 bytes (first byte 18..20) the C decoder reads a
normal match token (so a token < 16 is a 2-byte M1 match) while the assembly
decoder takes the after-literal-run path (a token < 16 is the 3-byte copy from
2049+). Retail lzo1x-999 output never has an M1 there; our optimal parser did,
minilzo accepted it, and the game decoded garbage and crashed in
`SetupPrimitiveDesc`. The encoder now allows only M2/M3/M4 after an initial
short run, and `tools/verify_engine_lzo.py` (part of `check_all`) decodes every
frame we produce with the engine's real decoder under Unicorn emulation
(`tools/ingame/retail_lzo_emu.py` maps `Fable.exe` and calls the function).

## Testing in the running game without a human

`tools/ingame/` drives the retail game end to end (fullscreen, DirectInput
mouse via relative motion, keyboard):

* `ingame_terrain_test.py` -- installs a Lua probe into ForgeFSE's PartyMode
  quest, launches `FSE_Launcher.exe`, clicks through title -> profile `0atlas`
  -> Continue Game -> AutoSave, waits for the probe (`ATLAS_PROBE|z|x|y|h`
  lines from `Quest:GetGroundHeightAt`), screenshots, kills the game, restores
  the script and the FSE log, and compares with the LEV. `--teleport` stands the
  hero on the centre point; `--catch-crash` attaches the dbgeng crash catcher
  (faulting address + stack scan in the report); `--trace-lzo` / `--trace-bp`
  log decoder calls / a breakpoint's stack. Needs a `0atlas` save profile
  whose AutoSave is in the target map.
* `--new-game --things NAME,...` starts a fresh game (profile `0aa` is recreated
  through New Profile; saves cache region entities, so a loaded save would not
  show .tng changes) and checks that each ScriptName exists in the running
  game at the position the loose .tng says. Verified 2026-09-16: a barrel
  placed and written into FinalAlbion.wad is found at exactly its position.
* `restore_install.sh` -- puts the `.atlas-orig` backups back and removes only
  the loose files Atlas created (`.atlas-created` marker).
* `patch_stb_chunk.sh` -- write a same-size chunk into `FinalAlbion_RT.stb`
  (bisecting a bad bake: splice donor/baked regions and test each in-game).
* `crash_catcher.py`, `trace_lzo_calls.py`, `trace_bp_stack.py`,
  `gamewin.ps1` -- the pieces.
* **Not done**: visible material (theme) painting still needs the layer
  topology rebuild (`--rebuild-topology` exists in the bake but wants theme
  materials wired through); nav trees are not regenerated (the terrain-only
  generator would drop the retail collision lines).

## Implementation map

| Piece | Where |
|---|---|
| Document, commands, undo, diff, save/deploy | `src/leveledit.{hpp,cpp}` (headless; `tests/test_export.cpp::testLevelDocument`, `testTerrainEditing`) |
| Terrain bake | `vendor/forgecore` `stbheightbake.cpp` (lifted from the forge CLI), `rangecodec::encodeNative`; `AlbionAtlas bake-terrain <chunk> <lev> <wx> <wy> <out>` bakes and verifies from the command line |
| Per-instance rendering, picking, outline | `gui/renderer.{hpp,cpp}` (`uploadThings`, `pick`, `screenRay`) |
| Gizmo, panel, shortcuts, instance sync | `gui/editor.cpp` |
| Thing index on preview instances | `foliageexport::Instance::thing`, set by `thingsexport` |
| Scripted tests | `tests/ui/editor.txt`, commands in `docs/AUTOMATION.md` |

Instance matrices are kept as `local * thingWorld`: a move recomputes the
world matrices of every instance of that thing (including spawned children)
without touching the GPU meshes. Structural edits (add/remove/undo of those)
reload the things layer from the in-memory `.tng` text.

## Next

1. Nav: regenerate only the cells a stroke touched, keeping the retail
   collision lines elsewhere.
2. World: WLD map/region editing, new-level-from-donor (`forge::worldworkspace`).
3. Live link to the running game through ForgeFSE (spawn/move/reload without a
   restart).
