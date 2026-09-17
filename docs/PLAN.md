# Albion Atlas -- plan (2026-09-16)

Where the editor stands, what was turned over today, and the order of work
from here. Evidence lines cite the retail/debug binaries (`Fable.exe`
addresses; `FableWin.exe` = the leaked debug editor with PDB names) or an
in-game harness run (`tools/ingame`).

## State (v0.6.0, all in-game verified)

| Capability | Status |
|---|---|
| Thing editor (place/move/dup/delete, loose .tng + WAD deploy) | shipped |
| Terrain sculpt + walkable paint + ground-theme paint, STB re-bake in place | shipped |
| Navigation quadtree patched per touched cell (door nodes, layers kept) | shipped |
| New level: blank, any retail size, from-scratch chunk | shipped |
| New level: copy of a map (donor chunk re-baked) | installs, **draws white** (engine map-open issue) |
| Own region under the 141 cap (filler slot take-over) + baked minimap | shipped |
| Unattended in-game harness (teleport, real region transition, follow, crash catcher) | shipped |

## Rocks turned today

### 1. Appending a texture entry
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
* Until then Atlas keeps replacing unreferenced retail `MINIMAP_*` slots
  (retail ships several; each new level takes one).

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

## Order of work

1. **Overworld editor** (the "lay terrains next to each other" ask): a
   World tab drawing every map box coloured by region on a 2D grid, drag a
   map (snap 32, overlap refusal, neighbours highlighted), save = WLD
   `MapX/MapY` + BWD box + STB info-block origin + chunk re-bake for the
   new origin (all pieces exist: `wld::relocateMap`, `bwd` boxes,
   `stbbake::bakeHeightfield` retarget). Then seam stitching between
   neighbours (`forge lev stitch` semantics) so adjacent terrains meet.
   Debug-editor model: `CEditWorldMap`; FableForge plan: world P2.
2. **Distant-LOD bake** for blank/new levels (the green horizon band): bake
   the composed background patches' inline textures from the level's albedo
   instead of the solid colour.
3. **Villages & creature generators** as thing presets (section 3), verified
   with the harness on a new game.
4. **Texture append RE** (section 1's experiment) -- unblocks unlimited
   custom textures for minimaps, ground splats and card art without stealing
   retail slots.
5. **Splat texture paint** (STB foreground layers: texture triple +
   per-vertex blend) -- the real "paint any texture on the ground".
6. **Region cap lift** via ForgeFSE (section 2, gated on the decompile
   verdict).
7. Cloned-chunk white-out RE (`CEngineLandscapeMap::OpenStaticMap`
   0x00BDD0E0) -- lowest priority now that blank levels work.

## Known gotchas to keep

* A new level hosted by the hero's *starting* region loads with the game and
  crashed it; host it elsewhere.
* The engine reads the BWD from three places (root, `data/Levels`,
  `data/Levels/FinalAlbion`); Atlas mirrors them.
* Saves cache the region table and region entities; use a new game to see
  region renames and .tng changes.
* The `0atlas` save profile (Oakvale childhood) is not a valid baseline for
  cross-region teleports into retail maps (a bare teleport into
  TeleporterGreatwood exited the game); use `--transition`.
