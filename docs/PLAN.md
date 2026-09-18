# Albion Atlas -- plan (2026-09-16)

Where the editor stands, what was turned over today, and the order of work
from here. Evidence lines cite the retail/debug binaries (`Fable.exe`
addresses; `FableWin.exe` = the leaked debug editor with PDB names) or an
in-game harness run (`tools/ingame`).

## State (v0.13.0, all in-game verified)

| Capability | Status |
|---|---|
| Thing editor (place/move/dup/delete, loose .tng + WAD deploy) | shipped |
| Terrain sculpt + walkable paint + ground-theme paint, STB re-bake in place | shipped |
| Navigation quadtree patched per touched cell (door nodes, layers kept) | shipped |
| New level: blank, any retail size, from-scratch chunk | shipped |
| New level: copy of a map (donor chunk translated + re-baked) | shipped (0.7.0, AtlasTGCopy renders in-game) |
| Own region (filler take-over + baked minimap; dedicated regions past 141 work too with a NEW GAME) | shipped |
| Unattended in-game harness (teleport, real region transition, follow, crash catcher) | shipped |
| Overworld: World tab + `world-move`, terrain chunk fully translated to the new origin | shipped (2026-09-17) |
| Region editing (owner, per-neighbour sees) in the World tab / CLI | shipped (2026-09-17) |
| Minimap textures appended + registered (PLAYER_GUI.MiniMapGraphics); no retail slot taken | shipped (2026-09-17) |
| Distant-LOD textures baked from the level albedo (blank levels, theme-paint deploys) | shipped (2026-09-17) |
| Seam stitching between newly adjacent maps (World toggle, `world-stitch`, `world-move --stitch`) | shipped (2026-09-17, in-game verified both sides) |
| Paint any ENGINE_THEME of the game (palette add from game.bin) | shipped (2026-09-17, in-game verified) |
| Custom ground texture from a PNG (textures.big + ENGINE_THEME append) | shipped (2026-09-17, in-game verified) |
| Creatures placed as retail AICreature things (NPCs, animals) | shipped (2026-09-17, in-game verified) |
| Villages: VILLAGE_* thing + per-thing membership | shipped (2026-09-17; loads in-game, behaviour unverified) |
| Enemy spawner (MARKER_CREATURE_GENERATOR) | shipped; in-game verified with an adult save (2026-09-17, 4 hornets) |
| Live link to the running game (ForgeFSE Lua thread: go here, spawn, follow) | shipped (2026-09-17, in-game verified) |

## Rocks turned today

### 1. Appending a texture entry -- SOLVED 2026-09-17
* Retail resolves a region's `MiniMapGraphic` string through the **PLAYER_GUI def's
  `MiniMapGraphics` map** (`Map_JVCCharString__`: `[u32 count]` then `[name NUL][u32
  GBANK_MAIN_PC id]`, 95 retail entries in `PLAYER_GUI_PC` and `PLAYER_GUI_DEFAULT`,
  game.bin) -- `CTCInventoryBase::GetMiniMapGraphic` (FableWin 0x236e25f) does
  `player_gui_def.MiniMapGraphics.find(region.GetMiniMapGraphic())`. Not the bank
  TOC symbols (a renamed symbol is not found), not a compiled-in table.
* A new name registered there resolves (proved with a retail id, then with an
  appended id): `AlbionAtlas minimap-register <name> <id>` /
  `editor::registerMinimapGraphic` (forgecore `defedit::setFieldBytes`, upstreamed).
* The appended texture's mip-0 chunk header must use the `[0xFFFF][u32 clen]`
  escape form when the raw chunk is >= 64 KiB (retail: 3082 of 3542 such chunks;
  the short `[u16 clen]` header made the engine drop the texture -- the LZO stream
  itself decodes fine in the emulated asm decoder). Fixed in FableTLC
  `tools/lionhead_lz_compress.py`; `ImportRequest.rawMip0` stays as an escape hatch.
* Atlas now appends `MINIMAP_<LEVEL>` + registers it for every own-region level
  (in-game: blank level AtlasMM's disc shows its bake); no retail slot is taken.
  The same recipe should unblock card art and ground splats: append + register in
  whatever def map the consumer looks the name up in.

### 1 (history). Appending a texture entry
* An appended `GBANK_MAIN_PC` entry (id 6294, `texture_build.py add`) **does
  not crash the game by itself**: launched and continued clean under the
  crash catcher; the earlier crash coincided with the first appended entry
  being referenced by a region *and* built through the `--dims` path -- not
  reproduced since, so it stays unexplained rather than blamed on the append.
* An appended entry referenced by a region's `MiniMapGraphic` is **not found**
  (the minimap falls back to a stock image), exactly the ForgeTest64 note.
  Registering it in `data/defs/RetailHeaders/pc/textures.h` (the BankCreator
  symbol header named by `banks_dvd.ini`) did not help.
* The debug build's lookup path: `CBankFile::InitEntry` (0x2f9c380) inserts
  `crc(symbol)` -> index into a `CVectorMap` **only when
  `OpenFlags & BANK_SEARCHABLE_SYMBOL_CRCS`**; `PostInitEntries` sorts it;
  `CreateSymbolMap` (0x2f9c130) builds the GUI `CSymbolMap` from every entry
  whose symbol has no `[`. So by the debug build's rules an appended entry
  *should* resolve -- which means retail resolves `MiniMapGraphic` some other
  way (a precomputed table or a different bank/symbol set). **Next
  experiment** (cheap, decisive): rename an unreferenced retail slot's TOC
  symbol (`MINIMAP_PRISONCOURTYARD1` -> `MINIMAP_X`, same id/payload) and
  reference the new name. Found -> TOC names are the source and the append
  path has a different defect (stats header / id table); not found -> retail
  ignores TOC symbols, find the table via the string
  `CurrentRegionMinimapGraphicName` (0xe36aa0) xrefs in `Fable.exe`.
* **Rename experiment done (2026-09-17): NOT FOUND.** `MINIMAP_PRISONCOURTYARD1`'s
  TOC symbol renamed in place to `MINIMAP_PRISONCOURTYARDX` (same length, same
  id/payload) and Greatwood's `MiniMapGraphic` pointed at it: the minimap disc
  fell back to the stock image, on the '0atlas' save AND on a new game (the
  baseline shows `MINIMAP_GREATWOOD`). So retail does not resolve the string
  against the TOC names at runtime, and it is not a compiled-in table either
  (Fable.exe holds only 3 `MINIMAP_*` strings, 76 of the 6290 bank names) nor a
  name hash in the TOC `crc` field (crc0/crc32 of the name never match). Next
  lead: a live breakpoint (pybag) on `CBankFile::CreateSymbolMap` 0x9cc530 /
  `SplitSymbol` 0x9cbaa0 / `FindIndexByFilename` 0x9ccdf0 during the region
  transition, argument = the graphic name, to see which map answers it.
* Until then Atlas keeps replacing unreferenced retail `MINIMAP_*` slots
  (retail ships several; each new level takes one).
* **Note for the next session -- how to find retail's real resolver:**
  1. In `Fable.exe`, xref the strings `CurrentRegionMinimapGraphicName`
     (0xe36aa0) and `MINIMAP_` (0xe58ae4) and decompile their users
     (headless: `tools/ghidra_scripts/FindCallersDecomp.java` needs the
     retail program analysed first -- run auto-analysis on `Fable.exe` in
     the project once, references are missing today).
  2. Decompile retail `CBankFile::FindIndexBySymbol` (retail address via
     `ghidra_out/functions.tsv`; debug 0x2f97650) and see what feeds it when
     the CRC map is empty: a per-bank name index, `names.bin`, or the
     `RetailHeaders` enum parsed through `CDefinitionManager`.
  3. Set a live breakpoint (pybag, `tools/ingame/trace_bp_stack.py`) on
     `CBankFile::FindIndexBySymbol` during the region transition of an
     own-region level and read the argument/return: it tells whether the
     lookup runs at all for `MiniMapGraphic` and against which bank.
  4. The rename experiment (above) with a small `big_write` rename edit.
  Land whatever function turns out to be the resolver in FableTLC's
  byte-pure lane (it is small accessor code, the auto-RE crawl handles it).

### 2. Regions past 141
* Live probe (2026-08): the region vector is capped at 141 real entries;
  `CWorldMap::LoadFromFile` 0x00507c30 is the load loop. Two lanes:
  1. **Own region by take-over** (done): repurpose a filler slot.
  2. **Lift the cap natively** through ForgeFSE: the loader either sizes the
     region array from a constant or truncates at 141 -- RE the loop, then a
     ForgeFSE detour (its safe naked-stub idiom, entry hooks only) that
     allocates for the BWD's real count. Everything downstream indexes
     regions by slot, so this is a single-site fix if the array is
     heap-allocated; if it is an inline array in `CWorldMap`, the fix is a
     relocation of that member (bigger struct, all offsets shift) and not
     worth it. Deliverable: a yes/no from the decompile before any code.
* Region *names* are cached in save games: a renamed region reports its old
  name until a new game (harness `GetRegionName` evidence).

### 3. Villagers and creatures (how retail/the debug editor do it)
* Villagers are ordinary placed things: `CREATURE_*` `NewThing`s carrying a
  `CTCVillageMember { VillageUID }` that points at a `VILLAGE_*` thing
  (`CTCVillage`: `HasBeenInitiallyPopulated`, guard/crime state); houses are
  `CTCOwnedEntity { OwnerUID }`. Enemies are `CTCCreatureGenerator` things
  with `CreatureFamilies[n]` and a generate type
  (`ECreatureGeneratorGenerateType`), triggered by
  `CTCActivationReceptorCreatureGenerator`. The debug editor authors all of
  these as thing components (`CTCVillage`, `CTCVillageMember`,
  `CTCCreatureGenerator` in FableWin) -- no separate system.
* So in Atlas this is thing-editor work, not engine work: presets that
  place a village thing, villagers bound to it (UID wiring), owned houses,
  and a creature generator with a family picker (families come from
  game.bin defs). The FSE harness can verify: spawn count after a fresh game.

### 4. Script extender as an "append" tool
* ForgeFSE already gives: quest threads, entity scripts, creature spawn,
  region transition (`GoToMapSlotRetailTransition`), region probes.
* What only a native hook can add: the region cap (above), per-region
  minimap fallbacks (hook `InitialiseMiniMapFileLoading_Region` 0x829d90 to
  load our TGA for any region -- the debug editor's own TGA path), and
  texture symbol resolution if retail turns out to use a fixed table.
* Rule kept from the FableTLC repo: hooks at function entries only, data
  first, native second.

## 2026-09-17: the overworld move, and what it taught

* **Every coordinate in a static-map chunk is absolute.** Moving a map means
  translating: the foreground directory AABBs + layer vertices + water mesh, every
  background patch (vertex grid, 4 edge strips, water sub-patch), the LOD tree
  AABBs, the whole foliage quadtree (node/group spheres, primitive boxes/spheres/
  matrices/instances, subsection centres) and the record's foliage root. Done in
  `src/stbrelocate.cpp`; audit clean on 398/398 retail chunks, translation verified
  site-by-site on every map, TeleporterGreatwood moved in-game (trees included).
* The **cloned-chunk white-out** (plan item 7) is explained: the old re-bake moved
  only the vertex grids/quad-dir, leaving edge strips, LOD tree, water and foliage
  at the donor origin. Feed the donor chunk through `relocateChunk` before
  `bakeHeightfield` and *Copy of this map* renders (wired in 0.7.0; AtlasTGCopy
  in-game PASS).
* forgecore bug to upstream: `stbbake::parseQuadDir` stops at the first cell with
  no foreground mesh (zero frame pointer, but not the terminator) -> the baker
  under-counts foreground frames on the fillers. The relocation walks the directory
  by `(w/16)*(h/16)` cells.
* A move far outside the retail world (y=9024) crashed the region transition:
  the engine's placement grid is the fixed box (0,0)-(8192,8192) (`CWorld::Init`
  0x4a6e30 -> `CWorldMap::CWorldMap` with a 32-unit cell grid, `SetMapPlacement`
  0x4fc9c0 unchecked). `checkMove` enforces it; the canvas draws the edge. Retail
  uses (32,640)-(5216,8160), so ~2.5k x 8k units are free for new maps.
* Harness: post-transition tutorial boxes pause the script thread; the harness now
  clicks their Next button when the probe log stalls.

## Order of work

1. ~~Overworld editor~~ DONE (extent bound pinned to the 8192 grid; region
   owner + sees editing in the panel and CLI; seam stitching 2026-09-17:
   `src/stitch.cpp`, opt-in, auto feather; retail seams are exact except at
   three-map corners). Placed things follow stitched/sculpted ground
   (`reseatThings`, also the editor's *Re-seat objects* button) and so does the
   chunk's foliage (`reseatFoliageZ`, in-game verified).
2. ~~Distant-LOD bake~~ DONE 2026-09-17: blank levels get a 64x64 DXT1 tile per
   background node baked from the level's own albedo (`src/lodbake`, forgecore
   `BackgroundTextureProvider`), and a theme-paint deploy re-bakes them in place
   (`HeightfieldBakeOptions.backgroundTextures`, same size as the tile it
   replaces). Texture row 0 = lowest map Y (pinned with `AlbionAtlas lod-check`
   against retail: Greatwood_1 0.65 vs 0.49 flipped, HookCoast 0.58 vs 0.38).
   Retail tiles are lit renders with STB-foreground textures, so they differ from
   a LEV-theme bake by design. NOTE: the grey band at a lone level's horizon is
   NOT the background texture (it stayed grey with green tiles): it is the void
   beyond the map -- retail hides it with filler/sea maps around every level.
2 (old). **Distant-LOD bake** for blank/new levels (the green horizon band): bake
   the composed background patches' inline textures from the level's albedo
   instead of the solid colour.
3. ~~Villages & creature generators~~ DONE 2026-09-17: creatures (`place
   CREATURE_...`), villages (Village card + membership), and the enemy spawner
   VERIFIED with an adult save (harness `--save-from Cornelio`): four hornets
   from an Atlas WASPS spawner in GreatwoodTeleport. The childhood profile was
   the only blocker (a disabling quest stays active until the Guild).
4. ~~Texture append RE~~ SOLVED (PLAYER_GUI.MiniMapGraphics + raw mip 0).
5. ~~Splat texture paint~~ DONE 2026-09-17: any retail ENGINE_THEME can be
   painted on any map (palette add from the game) and any PNG becomes a ground
   theme (`theme-add` / the paint card: textures.big append + ENGINE_THEME def
   append, donor copy), both in-game verified. The per-vertex blend is what
   the theme brush already paints (the bake turns LEV slots into layer
   passes), so a separate "splat" tool is not needed.
6. ~~Region cap~~ **THERE IS NO 141-REGION CAP** (2026-09-17). The BWD loader
   (`CWorldMap::LoadWorldFromBinaryFile`, FableWin 0x1c7f9b0, decompiled
   headless into FableTLC `ghidra_out/bwd_loader_fablewin.c`) is
   `count = ReadSLONG; resize(count); for i in 1..count-1: LoadBinary()`.
   Live: `new-level TeleporterGreatwood AtlasCap --dedicated` = map slot 400,
   region 146 (all three BWD copies in step). From the existing '0atlas' save
   the hero arrives but the level draws WHITE and `GetRegionName` is "" --
   because SAVES CACHE THE REGION TABLE: a region younger than the save is
   nameless and unrendered. Hosted by region 142 (in the save) it renders and
   is named; with `--new-game` region 146 renders completely, is named
   AtlasCap and shows its minimap. The August ForgeTest "cap" probe and the
   filler-takeover workaround were both artefacts of continuing an old save.
   Rule: a new dedicated region needs a new game (or a save made after it was
   added); the own-region UI should say so instead of taking over fillers.
   One caveat kept: the very first transition on the BWD that
   `worldinstall::installLevel` wrote crashed the game; after any Atlas region
   edit (WLD->BWD recompile of contains/sees) it loaded -- the installLevel
   region record differs somewhere (to diff). `region-props` sets a region's
   RegionDef / minimap / display name / world-map flag in WLD + BWD.
7. ~~Cloned-chunk white-out RE~~ explained (untranslated chunk) and fixed:
   `relocateChunk` runs in the donor-copy path (0.7.0).

## Known gotchas to keep

* **Restoring the live install after a probe: `.atlas-orig` files ARE the
  user's originals.** Put them back (`mv X.atlas-orig X`), never delete the pair.
  A loose `OrchardFarm.tng` / `TeleporterGreatwood.tng` (identical to the WAD
  entries) was lost that way on 2026-09-17 and re-extracted from the WAD.

* **A chunk's local-detail section is only "the last thing" in retail.** After a
  relocation appended foreground frames or LOD blocks behind it, the section's
  room ends at the first foreign frame; `ldLayout` now measures that (`limit`)
  and moves the whole section to the end of the chunk when it no longer fits
  (every triple is absolute, so the tree just follows; old foliage slots are
  zeroed). Before the fix, a grown section overwrote the appended frames and the
  second edit of a moved map broke its foliage tree ("implausible local-detail
  group count"). Chunks grow by the old section on such a move; the STB grows by
  a whole chunk per variable-size replace anyway. `chunk-zcheck <map> 10 --seam`
  is the regression check (a seam-shaped Z ride on a twice-moved chunk).

* A new level hosted by the hero's *starting* region loads with the game and
  crashed it; host it elsewhere.
* The engine reads the BWD from three places (root, `data/Levels`,
  `data/Levels/FinalAlbion`); Atlas mirrors them.
* Saves cache the region table and region entities; use a new game to see
  region renames and .tng changes.
* The `0atlas` save profile (Oakvale childhood) is not a valid baseline for
  cross-region teleports into retail maps (a bare teleport into
  TeleporterGreatwood exited the game); use `--transition`.
