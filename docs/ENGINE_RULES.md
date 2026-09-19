# Engine rules — what Fable itself decides

FableForge writes the real game files, so it is bound by what the engine does with them.
Every rule below was hit in the game, not guessed; the *Evidence* column says how. The
editor repeats the relevant rule next to the action it applies to (the note under the
button) and the Setup panel lists them all.

## Saves

| Rule | What it means for you | Evidence |
|---|---|---|
| **Saves cache the region table.** | A level installed with its own region is only named and drawn on the map screen in a game **started after** it was added (or in a save made after adding it). An older save never learns about it. | The "region 142 cap" of 2026-09-17 was this: a new game showed every new region. |
| **Saves cache a map's entities.** | Objects and creatures you add to a map only appear in a save that has never loaded that map, or in a new game. Existing saves keep the version they visited. | Placed barrels/creatures invisible in the old autosave, present on a fresh one (harness runs, 2026-09). |
| **Moved maps need a new game to walk.** | The overworld layout is read at world load; a save carries the old placement. | `world-move` verified in-game on a fresh game only. |

## Running game

| Rule | What it means for you | Evidence |
|---|---|---|
| **Nothing hot-reloads.** | Rewriting `FinalAlbion.wad`, `FinalAlbion_RT.stb`, `textures.big` or `game.bin` while Fable runs crashes it. FableForge refuses every write while the game is up (live-link heartbeat or the `Fable.exe` process). | Game died the moment the WAD/STB were rewritten under it (2026-09-17). |
| **The live link is not a reload.** | With ForgeFSE the editor can teleport the hero to the spot you look at and spawn the selected creature there -- through the game's own script API. It cannot push edited files into a running game. | `src/livelink.cpp`; `docs/EDITOR.md` "Live link". |

## Levels and regions

| Rule | What it means for you | Evidence |
|---|---|---|
| **The game reads the WAD, never loose files.** | A loose `data/Levels/FinalAlbion/<map>.tng` or `.lev` is only FableForge's working copy (*Save draft*). *Write into FinalAlbion.wad* / *Write terrain into the game* are what the game sees. | WAD repack is the only path that changed anything in-game. |
| **A region needs an entrance to be travelled to.** | The map screen and quest teleports drop the hero on the slot's `REGION_ENTRANCE_POINT` in `FinalAlbion.gtg`. Own-region levels get one at their centre; move it from the Level tab. | Retail: 151 of 399 slots carry one; format reversed 2026-09-18 (`src/gtg`). In-game travel to a new region through it: **not yet probed** -- do it on a fresh game. |
| **Terrain chunks are per map and absolute.** | Every coordinate in a map's STB chunk is world-space, so moving a map means translating (or re-baking) its chunk; FableForge does that for you. | `stbrelocate`, `chunk-audit`: 399/399 maps audit clean after moves. |
| **Navigation is the retail tree, patched.** | Walkable paint patches the cells you touched in the map's own quad tree. Freshly sculpted ground keeps the retail nav elsewhere; a whole-map nav regeneration is not done (the open "nav frontier"). | `navpatch`; walkable paint reaches the engine (2026-09 harness). |

## Creatures and spawners

| Rule | What it means for you | Evidence |
|---|---|---|
| **Enemy spawners need an adult hero.** | `MARKER_CREATURE_GENERATOR` things are disabled by the game's own quest logic until the hero leaves the Guild; in the childhood prologue nothing spawns. | Adult-save test vs childhood autosave (2026-09-17). |
| **Creatures carry world-space data.** | A creature block holds its world-space `InitialPos` and (for villagers) a village membership; FableForge fills both when it places one, which is why presets ship objects only. | `Document::placeCreature`; `tools/build_presets.py`. |

## Textures and themes

| Rule | What it means for you | Evidence |
|---|---|---|
| **A texture slot keeps its size and format.** | *Replace from image* resamples to the slot's allocated size and re-encodes in its pixel format (DXT1/DXT3/ARGB); every object that uses the texture changes. | `texturewrite` validated against 227/230 retail entries; checker theme rendered in-game. |
| **Ground themes are a palette of 256 per map.** | Painting is limited to the map's LEV palette; any `ENGINE_THEME` of the game (or a custom one from a PNG) can take a free slot, written with the next terrain save. | `Document::addGroundTheme`; custom theme rendered in-game (2026-09-17). |
| **Palette slot 0 does not draw.** | The debug editor starts at 2; a theme in slot 0 renders nothing. | Tried; documented in `leveledit.cpp`. |

## Not rules, just not done yet

Foliage painted onto a retail map, custom meshes / creatures / animations, effect editing,
water as its own tool. See `ROADMAP_1.0.md` for where each stands.
