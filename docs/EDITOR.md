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
  cell painting (saving patches the map's navigation quadtree for exactly
  those cells, see "Navigation" below), and **ground-theme paint** (`Paint ground` + a picker over
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
* `--start-map M` / `--transition`: begin in map M (the save's) and jump to
  the target map; `--transition` goes through ForgeFSE's
  `GoToMapSlotRetailTransition` (a real region load) instead of a bare
  entity teleport. The probe logs `region0`/`region` (`GetRegionName`)
  before and after.
* `--follow x,y` (with `--teleport`): the navigation probe. A creature is
  spawned at the map-local point and a second quest thread issues
  `GainControlAndMoveToPosition` to the hero's spot while the probe samples
  its distance every second; `--follow-expect near|far` turns that into the
  verdict (`min_distance` <= 2 / >= 2.5).

## Navigation (walkable paint reaches the engine)

The LEV walkable byte does nothing at runtime; the engine moves creatures on
the **CNavQuadTree** stored in the LEV's navigation sections (proven below).
`forge::navmesh::parseNavigation` / `emitNavigation` (`vendor/forgecore`
`navpatch.cpp`, upstreamed to FableForge) read and write those sections
node-for-node -- every layer, the 3-byte blocked-root markers, switchable
(door) leaves with their thing UIDs, the half-cell leaves carved around
placed objects, region ids, neighbour order -- and reproduce all 399 retail
LEVs byte-exactly (`forge_navpatch_tests`). The layout was read off
FableWin's `CNavQuadTree::SaveToFile/LoadFromFile` and the leaf overrides:
the loader reads `total` records in a flat loop and resolves indices through
a map, so indices only have to be unique; region ids are the connected
components of the leaf graph with the switchable leaves taken out.

When a walkable/blocked stroke is saved, `patchWalkability` edits only the
cells whose byte changed: a blocked cell loses its layer-0 leaves (bigger
leaves are split first, an emptied root becomes a marker), an opened cell
gets one full-cell leaf hooked into the tree, the touched leaves get their
neighbour lists recomputed as the edge-sharing leaves (the retail rule,
checked on every retail map) and the regions are re-derived so a cut-off
pocket gets its own id. Everything else stays as the retail build wrote it.
The LEV grows or shrinks, so the WAD entry is relocated by `wad::repack`.

**In-game verified** (2026-09-16), three runs of the harness with a creature
told to walk to the hero standing at Oakvale West (68.5, 196.5):

| install | creature's closest approach |
|---|---|
| retail | 1.25 (arrives) |
| 7x7 blocked ring painted around the hero, nav patched | 8.47 (never enters) |
| same ring in the LEV bytes, retail nav (control) | 1.22 (arrives) |

The control run is what shows the walkable byte alone is inert and the patched
tree is what the engine follows.

## The overworld (World tab)

The right panel's **World** tab shows every map of `FinalAlbion.bwd` as a box on a
2D grid (x right, y down, the engine's world units), coloured by the WLD region that
owns it. Wheel zooms about the cursor, right/middle drag pans, **F** refits. Click a
map to select it; drag it (or type X/Y, or nudge with the arrow keys) to a new
origin: it snaps to 32, the maps it would touch light up, and an overlap or a
misaligned spot is refused (red outline, reason on the canvas). Moves queue up
(orange outlines, *Pending moves* card) and **Move N maps in the game** writes them
all at once; *Revert all* / *Put back* drop them.

**Regions** live in the same panel: *Owned by region* changes the region that
loads the map (WLD `ContainsMap`; exactly one owner, the retail partition), and the
*Neighbours* card lists every map touching the selected one with two toggles per
row -- *seen from mine* (the selected map's region draws that neighbour) and *sees
me* (the neighbour's region draws the selected map; WLD `SeesMap` = loaded and
drawn while the player is in that region). A map moved next to new neighbours
wants both ticked across the new edge. Region edits queue with the moves and are
written by the same button; the WLD is edited line-precisely (`SeesMap` lines
stay grouped) and the BWD keeps every on-disk field except the `contains`/`sees`
slot lists, which are resolved from the edited WLD (forgecore's
`compileFromWld`; refused when the WLD and BWD already disagree on some region's
maps). CLI: `world-owner <map> <region>`, `world-sees <region> <map> <0|1>`,
`world --regions`. Note: the BWD keys maps by LEV stem; ten retail maps carry a
different script name (`BowerstoneSlumsWarehouses.lev` is scripted
"BowerstoneSlums") and Atlas keys on the stem everywhere.

**Seam stitching** (2026-09-17, in-game verified) -- *Stitch edges with
neighbours* in the pending-changes card (off by default) averages the shared
vertex row/column of each moved map and every map it now touches, feathers the
correction into both maps and re-bakes both terrain chunks (`Document::
setVertexHeights` + `deployTerrain`, one-time backups, loose `.lev` + WAD).
Feather is *auto* (one cell per unit of the largest step, 4..32, so the ramp
stays under ~45 degrees) or a fixed cell count. CLI: `world-move ... --stitch`,
`world-stitch <map> [<map2>] [--feather n|auto] [--dry-run]`. What retail does:
adjacent maps share their edge vertices exactly (Greatwood_2 x=96 == Greatwood_1
x=0 to the float) *except at corners where a third map meets* (steps of 2-4 units
there, hidden by the map's own geometry), so a moved map's fresh seam is the only
thing worth stitching; `--dry-run` reports every seam's largest step. Placed things that stood on
the old ground (within 1 unit) follow it, keeping their offset
(`Document::reseatThings`; the `.tng` goes loose + into the WAD); the chunk's
own trees and grass keep their Z (TeleporterGreatwood | OrchardFarm, a 19.6-unit
step, lifted OrchardFarm's edge into its own tree canopy -- open). A map whose region does not *see* the neighbour
still ends at a void beyond the seam -- tick the neighbour toggles too. The
engine's ground query answers 0 on the seam column itself when the neighbour is
not loaded (its cell belongs to the other map).

What a move writes -- `AlbionAtlas world-move <map> <x> <y> [...]` is the same
path from the command line, `AlbionAtlas world` lists the layout:

1. `FinalAlbion.wld` `MapX`/`MapY` (byte-identical otherwise) and the `.bwd` box, in
   all three copies the engine reads (`data/Levels`, `data/Levels/FinalAlbion`, root).
2. The map's static-map record in `FinalAlbion_RT.stb`: `WorldX`/`WorldY`, the
   camera bounds, and the terrain chunk **translated** to the new origin. The debug
   editor bakes every coordinate absolute, so `src/stbrelocate.cpp` walks the whole
   chunk and shifts each one: the foreground patch directory AABBs and the layer-mesh
   vertices and their water mesh (`CWaterPatchMesh`), every background patch (vertex
   grid, the four tessellation edge strips, the water sub-patch), the background-LOD
   tree node AABBs (`mapX/mapY` there are map-local), the foliage quadtree (node and
   group spheres, every cache group's primitive boxes/spheres/matrices/instances/
   subsection centres) and the record's foliage root sphere. Range-coded vertex blocks
   are decoded, shifted and re-encoded with the editor's own compressor; LZO frames
   are re-laid per file block (foreground run, LOD blocks, the foliage section) with
   every reference rewritten, and a block that no longer fits is appended to the
   chunk. Grammars come from the FableWin `Save` functions (`CLandscapeBackgroundPatch`
   0x2ce3220, `CPatchTesselationEdgeStrip` 0x2e03a80, `CEngineWaterBackgroundSubPatch`
   0x2e055e0, `CWaterPatchMesh` 0x2e68bf0, `CLandscapeBackgroundTreeNode::SaveHeader`
   0x2deabe0, `CLocalDetailCacheMap::CQuadTreeElement::SaveFileBlock` 0x2e3eb20 and the
   `CLocalDetailPrimitive*` writers).
3. Maps that touched the moved one before or after get their shared edges re-baked
   (best effort: fillers the baker cannot rebuild keep their retail chunk).
4. The map's `.tng` (loose and WAD): thing positions are map-local, but AI
   creatures carry `InitialPosX/Y` in world units (a scan of every retail TNG
   against its map box found no other world-space key), so those are shifted.

Placed objects (`.tng`) otherwise stay as they are. **Verified**: the audit
walk (`AlbionAtlas chunk-audit --all`: every coordinate inside its map's box) is
clean on all 398 retail chunks; `chunk-relocate <map> <dx> <dy>` checks every
coordinate site moved by exactly the shift on every map; TeleporterGreatwood moved
to (3680,2880) renders in-game with its birches at the new place and the engine
reports the expected ground heights (25/25).

Gotchas found on the way: a re-baked-for-a-new-origin chunk that only moves the
vertex grids draws WHITE in-game (the "cloned chunk white-out" of earlier
sessions was this, not a name/registration issue); forgecore's `parseQuadDir`
stops at the first cell without a foreground mesh, so the baker under-counts
foreground frames on the fillers ("expected 36 CLandscapeLayerMesh foreground
frames, found 9") -- the relocation walks the directory by cell count instead;
save games cache the region table, so start a new game to walk a new layout;
the engine's placement grid is a hard (0,0)-(8192,8192): `CWorld::Init` 0x4a6e30
constructs `CWorldMap` over that box and `SetMapPlacement` 0x4fc9c0 writes map slots
into a 32-unit cell grid with no bounds check (a map at y=9024 crashed the next
region transition), so moves must keep the box inside 8192 -- the canvas draws the
grid edge and `checkMove` refuses anything past it.

## Implementation map

| Piece | Where |
|---|---|
| Document, commands, undo, diff, save/deploy | `src/leveledit.{hpp,cpp}` (headless; `tests/test_export.cpp::testLevelDocument`, `testTerrainEditing`) |
| New levels (blank / copy) | `src/worldedit.{hpp,cpp}` over `vendor/forgecore` `worldinstall.{hpp,cpp}` + `stbbake::buildTerrainChunk64`; GUI card in `gui/editor.cpp::drawNewLevelCard`; `tools/test_newlevel.py` |
| Navigation patch | `vendor/forgecore` `navpatch.{hpp,cpp}` (`parseNavigation`, `emitNavigation`, `patchWalkability`); `Document::saveTerrainLoose` applies it for the changed cells; `tests/test_export.cpp::testNavPatch` |
| Terrain bake | `vendor/forgecore` `stbheightbake.cpp` (lifted from the forge CLI), `rangecodec::encodeNative`; `AlbionAtlas bake-terrain <chunk> <lev> <wx> <wy> <out>` bakes and verifies from the command line |
| Per-instance rendering, picking, outline | `gui/renderer.{hpp,cpp}` (`uploadThings`, `pick`, `screenRay`) |
| Gizmo, panel, shortcuts, instance sync | `gui/editor.cpp` |
| Thing index on preview instances | `foliageexport::Instance::thing`, set by `thingsexport` |
| Scripted tests | `tests/ui/editor.txt`, commands in `docs/AUTOMATION.md` |
| Distant-LOD textures | `src/lodbake.{hpp,cpp}` + `src/dxt1.hpp` (albedo at 16 texels/cell box-filtered to 64x64 DXT1 per background node; blank levels and theme-paint deploys); `AlbionAtlas lod-check <map>` compares against retail tiles |
| Overworld layout + moves | `src/overworld.{hpp,cpp}` (layout, `checkMove`, `applyMoves`), chunk translation `src/stbrelocate.{hpp,cpp}` (`relocateChunk`, `auditChunk`); GUI `gui/world.cpp`; `tools/test_overworld.py` + `tests/ui/world.txt`; diagnostics `chunk-audit`, `chunk-relocate`, `chunk-dump`, `chunk-extract` |
| Seam stitching | `src/stitch.{hpp,cpp}` (`sharedEdge`, `stitchEdges`, `stitchNeighbours`) over `Document::setVertexHeights` + `deployTerrain`; GUI toggle in `gui/world.cpp` (`world_stitch <0|1> [feather]`, `assert_log`); `world-stitch`, `world-move --stitch` |

Instance matrices are kept as `local * thingWorld`: a move recomputes the
world matrices of every instance of that thing (including spawned children)
without touching the GPU meshes. Structural edits (add/remove/undo of those)
reload the things layer from the in-memory `.tng` text.

## Enemy spawners (experimental)

The Edit panel's **Enemy spawner** card places a `MARKER_CREATURE_GENERATOR` thing
carrying the retail self-triggering `CTCCreatureGenerator` block (families from the
game's `CREATURE_GENERATION_FAMILY` defs, trigger radius, creature limit) at the view
centre, on the ground, in the TNG's NULL section. The data matches retail generators
byte for byte in shape, but generation itself has not been observed in the automated
childhood-profile tests yet (retail generators spawn on region load through
`CTCCreatureGeneratorCreator`, gated by the hero's level band); see `docs/PLAN.md`.
Scripted: `place_spawner <radius> <limit> <FAMILY[,FAMILY...]> [scriptname]`.

## New levels

The Edit panel's **New level** card adds a level to the world; a free
32-aligned origin is suggested (first slot right of the existing maps), the
owning region defaults to the selected map's, the name must be a bare stem.
Two modes:

* **Blank** (`AlbionAtlas blank-level <name> [--size WxH] [--theme
  <slot|name>] [--height h] [--template <map>] ...`): a level authored from
  scratch in any size a retail map has (32x32 .. 160x256; the size combo
  lists them). The LEV skeleton (header, palette) comes from a retail map of
  that size (the selected map when the size matches, else the first one)
  with the palette rebased to this install's game.bin by name; every cell
  gets the chosen ground theme, the flat height and walkable=1; the
  navigation tree is generated fresh; the `.tng` is empty; and the terrain
  chunk is built by forgecore's from-scratch builder (the one ForgeTest64
  proved in-game) from the LEV heights + palette materials with a solid
  distant-LOD colour. The builder's background LOD tree now follows the
  shape every retail chunk uses (non-power-of-two sides split off their
  largest power of two and carry no payload; power-of-two rectangles halve
  down to 16x16 leaves), so the size limit is gone. **In-game verified**
  (2026-09-16): a 64x64 `AtlasBlank` and a 128x224 `AtlasBig` both load,
  render textured (GROUND_FOREST_LEAVES / GROUND_GRASS_NO_LOCAL_DETAIL) and
  put the hero at the authored height (25/25 and 49/49 ground samples).
  Sculpt, paint, place and deploy work on them like on any map. A level in
  the hero's *starting* region loads with the game and must not be there
  while the region is being played (AtlasBig in StartOakVale exited the game
  at load; hosted by Greatwood it is fine).
* **Copy of this map** (`AlbionAtlas new-level <donor> <name> ...`): clones
  the selected map's current `.lev`/`.tng` and re-bakes its terrain chunk for
  the new origin. It installs and loads (hero at the right heights, 25/25),
  but the cloned chunk **draws white in-game** -- the same engine map-open
  issue FableForge hit with donor clones (bank/texture resolution by map, not
  fixed by the chunk bytes); a control teleport into the retail donor from the
  Oakvale save even exited the game. Kept as experimental; use Blank for a
  playable level.

What both do (`src/worldedit.cpp` over forgecore's `worldinstall::installLevel`,
the library form of `forge world install-level`, upstreamed to FableForge):

1. the level's `.lev`/`.tng` bytes become new WAD entries
   (`wad::appendClonedEntries` off the donor/template + `repack`);
2. the terrain chunk is appended to `FinalAlbion_RT.stb` with an
   origin-patched common record (the donor's record re-targeted, or the
   from-scratch `buildTerrainCommonRecord`);
3. the map is registered in `FinalAlbion.bwd` and `.wld` and added to the host
   region's `contains`/`sees` lists.

Ownership matters: the engine's region vector is capped at the vanilla 141
entries (live probe, 2026-08), so a dedicated region past that is never
reachable. The card therefore always attaches to an existing region; the CLI's
`--dedicated` keeps the old behaviour and warns. Refusals (duplicate name,
off-grid origin, overlapping box) happen before any file is touched; the four
containers get one-time `.atlas-orig` backups and are replaced with staged
temp files in one commit. `tools/test_newlevel.py` (in `check_all`) runs the
CLI and the card against a scratch copy of the install.

### Own region + minimap

**2026-09-17:** the minimap texture is now *appended* to `textures.big` under its own
name `MINIMAP_<LEVEL>` and registered in the `PLAYER_GUI_PC` /
`PLAYER_GUI_DEFAULT` defs' `MiniMapGraphics` map in `game.bin` -- that map is what
retail resolves a region's `MiniMapGraphic` through (`CTCInventoryBase::
GetMiniMapGraphic`). No retail slot is taken any more; `AlbionAtlas minimap-register
<name> <id>` does the registration alone. One-time `.atlas-orig` backups of
`textures.big`, `game.bin` and `names.bin`. (The paragraphs below describe the
earlier slot-replacement approach and why it was needed.)

The in-game minimap is per *region*, and the engine keeps only the first 141
regions (live probe, 2026-08), so a level that wants its own name on the map
screen and its own minimap takes over a retail **filler** region slot ("Own
region + minimap" toggle / `--own-region [<filler>] [--merge-into <filler>]
[--display <name>] [--no-minimap]`):

1. the filler's decorative maps are re-owned by another filler (their `sees`
   references elsewhere are untouched), the slot is renamed and re-labelled
   in place in the BWD and the WLD (`wld::File::setRegionText`), and it owns
   only the new map. The BWD is mirrored to the two other copies the engine
   reads (`FinalAlbion.bwd` at the root and under `data/Levels/FinalAlbion`).
2. the minimap is baked from the level (top-down albedo, north up, the
   region box stretched onto the square like retail's, a hillshade and the
   retail circular vignette) into a 256x256 DXT3 entry of `textures.big`. It
   **replaces an unreferenced retail `MINIMAP_*` slot** (retail ships a few
   that no region uses, e.g. `MINIMAP_PRISONCOURTYARD2`): an entry *appended*
   past the retail ids crashed the game at start-up (the engine indexes
   `GBANK_MAIN_PC` by a fixed-size table). The encoder is FableTLC's
   `texture_build.py` through forgecore's import driver.

**In-game verified** (2026-09-16): `AtlasOwn` (own region over slot 73,
`MINIMAP_PRISONCOURTYARD2` replaced) reached with `--transition`; the minimap
disc shows the baked terrain. `GetRegionName` still reported the filler's old
name -- the '0atlas' *save* caches the region table, so a renamed region
shows its new name from a new game.

Orientation was pinned on retail data: `MINIMAP_BARROWFIELDS` /
`MINIMAP_HOOKCOAST` match the LEV walkable mask with map +Y at the top and
the whole region box stretched to the 256x256 square (HookCoast is 160x256).
`CMiniMapDisplay::GetRelativePosOnMap` (landed byte-exact in FableTLC)
confirms the mapping: `(pos - regionMin) / regionExtent`.

## Next

See `docs/PLAN.md` (2026-09-16) for the ordered plan and the open
investigations (texture append resolution, region cap, villagers).

1. ~~Overworld: region editing, seam stitching, the extent question~~ done
   (2026-09-17). Open: re-seat the chunk's foliage Z after a stitch (things do).
2. A baked distant-LOD texture instead of the solid colour (the green band at
   the horizon of a blank level).
3. Cloned levels: run the donor chunk through `relocateChunk` (the white-out was
   the untranslated chunk, see above) so *Copy of this map* renders.
4. Live link to the running game through ForgeFSE (spawn/move/reload without a
   restart).
