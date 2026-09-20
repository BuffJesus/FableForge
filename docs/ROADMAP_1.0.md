# Albion Atlas -> 1.0: what it should do, what it can't, and how we get there

## Resume here (2026-09-18, evening -- after the rename)

This repo IS FableForge now (`D:\Code\FableForge`, formerly Albion Atlas; the old toolchain repo
is `D:\Code\FableForge-legacy`). Exes: `build\FableForge.exe` (GUI), `buildorge.exe` (CLI,
`src/cli/*.cpp`), `buildorge-tools.exe` (the legacy CLI, 133 commands, `tools/forge-cli`).
`libs/forgecore` is canonical. Version 0.16.0. `tools/check_all.py` = 16 checks, ALL PASS after
the CLI split (the evening run; see the last commit for the run after the thumbnails).

**2026-09-19 (evening):** three commits on top of the rename: the cutscene/title tooling from
the Oakvale Reborn session (`forge-tools script cutscene-*`, `title add`); a **foliage fix**
(the frame probes read a 4-byte lattice and missed ~75% of every map's baked foliage, incl.
Oakvale's town-square oak -- `docs/TODO.md` foliage block); and a **perf fix** in the shared
frame walker (23 GB of memset per map; boot + open Oakvale 3.65 s -> 0.79 s, the suite's
deploy checks ~35% faster). Suite ALL PASS 16/16 after each. Known flake: the synthetic-click
suites (`ui paths` textures checkbox, `ui foliage`) occasionally miss a click under the full
run and pass alone.

State: 0.14 done except the public repo (user), 0.15 4/4, 0.15b 8/8 + tour, 0.16 3/4,
0.17 2/6 (Textures tab, effect picker), 1.0-rc: docs 2/3 (walkthrough, CLI reference),
perf (parallel albedo bake, parallel chunk-audit), main.cpp split + automation.cpp,
ENGINE_RULES.md, object-palette mesh thumbnails. Scoped-not-started: foliage brush (harness),
mesh/creature import (L), Blender addon bundle (vendor its Python first), STB compaction,
texture-tab thumbnails, PiP minimap, brush falloff ring.

Next in order (unchanged by the 2026-09-19 evening; water and fishing spots are filed below,
not queued ahead of 1.0):
1. The user: public GitHub repo `FableForge` -> push -> CI green; `python tools/package.py`
   (runs the suite) -> `git tag v0.16.0`.
2. In-game probes the user drives: map-screen travel to an own-region level through its
   entrance (fresh game); a retextured barrel; a placed preset; a placed emitter; **a placed
   fishing spot** (Actors card, new 2026-09-19); Oakvale's square oak now in the viewport.
3. Fold forge-tools families into forge / the GUI as they get UI (quests first: the legacy
   `apps/forge-gui` node canvas is the reference; `docs/re_reference/quest_node_defs.json`).
4. With the harness: foliage brush (its step (a), decoding the chunk's existing groups, is
   what the 2026-09-19 lattice fix delivered for the exporter). Without: **STB compaction
   (1.0-rc #4) -- in progress 2026-09-19**, chunk-audit --all parallel.
5. Post-1.0: **0.18 Water** (below) -- the RE is done, the paint side works today; the
   writer waits until 1.0 ships.

Install state: retail (`forge backups` -> 9 backed-up files, 0 differ). Saves: `0atlas` and
`1234234` carry their childhood autosaves, `0aa` removed, `Cornelio` is the adult save
(`--save-from Cornelio`). FSE has no live-link hook installed.

## Context

Albion Atlas (`D:\Code\AlbionAtlas`, v0.13.1, no public remote, no CI) started as a map
exporter and grew in three days into a working replacement for the leaked Lionhead debug
editor: things, terrain, textures, new levels, regions, overworld, creatures, spawners and a
live link to the running game — every feature verified in the real engine by the unattended
harness. The user wants it to reach a **1.0 for the Fable modding community first**, then
keep growing as a power tool. "Improve" therefore means three things at once: make it
*safe and self-contained for strangers*, *deeper as a level design tool*, and *healthy as a
codebase* — and know exactly where the engine draws the line.

Everything below was checked against the code and the in-game evidence gathered in this
session; nothing is speculative unless marked *(untested)*.

---

## 1. What 1.0 should be (definition of done)

A zip someone on the Fable Discord downloads, unpacks, points at their Steam install, and
uses to **build a playable custom level with its own region, terrain, textures, objects,
NPCs and enemies — and walk into it — without touching a command line or breaking their
game**. Concretely:

| Must (1.0)                                                                   | Status today |
|-------------------------------------------------------------------------------|--------------|
| Runs on a clean machine: no Python, no FableTLC/FableForge checkout           | **Broken** — custom textures + minimap bake shell out to `python texture_build.py` (`vendor/forgecore/src/terraintex.cpp:497-504` hard-codes `D:/Documents/FableTLC/tools`) |
| One-click *restore my install to retail* and visible backup state             | Missing — backups are `.atlas-orig` files the user must move back by hand (we lost two loose TNGs that way today) |
| Refuses to damage a running game                                              | Done (deploy guard on the link heartbeat) — but only when the link is installed |
| New level end-to-end from the GUI (blank/copy, own region, minimap, textures, objects, NPCs, spawner) | Done, verified in-game |
| Tells the user the engine's rules where they bite (new region needs a new game; saves cache entities; spawners need an adult hero) | Partly (notes in the log); needs to be in the UI at the point of action |
| Export still works as before (GLB/OBJ)                                        | Done |
| Documented for a newcomer (install, first level, FAQ of engine rules)         | README is 206 lines of feature paragraphs; no "first level in 10 minutes" |
| Public repo, tagged releases, CI that builds + runs the offline suite         | Missing (no remote, no workflow) |

| Could (post-1.0, "power")                                | Notes |
|-----------------------------------------------------------|-------|
| Foliage brush (grass/trees) into the STB local-detail tree | `reseatFoliageZ` already walks every primitive; a writer for new instances = the "environment brush" memory item; no RE blocker |
| Water editing (lakes/rivers via theme WaterHeight)        | RE'd from the debug build 2026-09-19 (`FableTLC docs/engine/WATER_RE.md`): water = depth-theme paint (`WATER_LAKE_{0..16}` etc., surface = ground + Σ blend·WaterHeight) — the paint side works today and gives gameplay water; the *visible* surface is a baked STB mesh retail only loads, and our deploy writes `hasWater=0`. Plan: water brush (altitude → depth mix) → `CWaterPatchMesh` writer (layout known, zeroed shore) → background sub-patch/shore/sea |
| Prefab / preset library (village, bandit camp, shop)      | Village + spawner + creature blocks exist; a preset = a TNG fragment with relative positions |
| Multi-select, copy/paste across maps, align/snap tools    | Editor is single-selection today |
| Fishing spots                                              | Done 2026-09-19 (Actors card + `place_fishing_spot`; retail `MARKER_FISHING_SPOT`, optional first catch); untested in-game |
| Region entrance points (`FinalAlbion.gtg`) for map-screen travel | Format is plain TNG text (REGION_ENTRANCE_POINT things) — parseable with `forge::tng` |
| Custom ENGINE_THEME editing (cliff PNG, bump map, water) | `theme-add` covers base+cliff; the rest are `defedit::setField` calls |
| STB compaction after many edits                            | Chunks grow by the old foliage section per re-layout; the STB grows a chunk per replace |
| Custom meshes, creatures, object textures, particle effects, Blender round trip | All formats decoded and written from Python/Blender in FableTLC; native Atlas versions = milestone 0.17 below |

| Can't (engine facts — say so in the UI, don't promise)   | Evidence |
|-----------------------------------------------------------|----------|
| Live reload of an edited region                            | Game dies the moment WAD/STB are rewritten under it (2026-09-17 test) |
| New regions visible in an existing save                    | Saves cache the region table (region 146 test) |
| Spawners in the childhood prologue                         | Disabling quest until the Guild (adult save test) |
| Loose `.tng` as a modding delivery                          | Engine loads the WAD; loose files are only the editor's working copies |
| Engine-quality nav for freshly sculpted terrain             | Walkable paint patches the retail tree per cell; wholesale regeneration is the open "nav frontier" |

---

## 1b. Decision (2026-09-17): Atlas becomes FableForge -- LANDED 2026-09-18

The user's call: *Albion Atlas absorbs FableForge* — the Atlas repo is the product and takes
the FableForge name; the 1.0 is the **level/world/content editor** described here; the
quest/script tooling (FQT byte-exact compilers, quest cards, node editor, ForgeFSE Lua)
joins as a 1.x tab, not in 1.0. Consequences, folded into the milestones below:

- **0.14 also does the merge groundwork**: un-vendor `forgecore` — it moves in as
  `libs/forgecore` (its own CMake target, history imported with `git subtree` or a squash
  commit that names the source commit); `tools/sync_forgecore.py` and `vendor/VENDORED.md`
  retire; the one local override (`src/lzo.cpp` -> clean-room `lzo1x`) becomes the library's
  default (drops the GPL minilzo path entirely, which the public repo needs anyway).
  FableForge's 100 uncommitted working-tree files must be triaged *before* the import
  (commit what the user wants kept there, on a branch, in the FableForge repo).
- The `forge` CLI (`apps/forge`) is not merged into the GUI now; its level-side commands are
  already superseded by `AlbionAtlas` CLI commands. It moves over as `tools/forge-cli` so
  the quest commands keep working from one checkout; the half-built `apps/forge-gui`
  (ax::NodeEditor canvas) is archived on a branch for the 1.x quest tab.
- **Naming**: product "FableForge", the app executable `FableForge.exe`, the exporter tab
  keeps the name *Atlas* (people know the zip by it); CLI `forge` = today's `AlbionAtlas`
  commands + the quest commands. Rename lands at 0.15 so 0.14's portability work is not
  entangled with a rename diff.
- Memory/docs to update when this executes: the *Albion Atlas* and *FableForge Creation-Kit
  toolchain* memory notes, FableForge's `docs/`, and the CLAUDE.md sibling-repo line.

**Landed 2026-09-18 (2):** the legacy `forge` CLI (10.7k lines, 133 commands: quests, mods, saves,
defs, scripts, UI, STB tooling) builds here as `forge-tools.exe` from `tools/forge-cli/main.cpp`
against `libs/forgecore` unchanged, with `docs/re_reference/` (schemas, manifests) copied over;
its families fold into `forge` as they get a GUI. Not carried: the legacy `apps/forge-gui`
(node canvas), its tests, the 13 MB `fse_native_overlay.json` (stays in the legacy repo).

**Landed 2026-09-18:** product, exe and settings rename (`FableForge.exe` GUI, `forge.exe`
CLI, `%APPDATA%\FableForge` with a one-time copy of the old `AlbionAtlas` folder, presets
read both header tags, `.atlas-orig` / `.atlas-created` / the live-link hook tag kept for
existing installs); forgecore un-vendored into `libs/forgecore` (canonical; `tools/sync_forgecore.py`
retired); the repo folder is `D:\Code\FableForge`, the old repo `D:\Code\FableForge-legacy`
(its GUI canvas / quest tooling / ~100 uncommitted files are merged from there piece by piece).

## 2. Process: milestones to 1.0

Each milestone = a tagged release with the zip in `dist/`, `tools/check_all.py` green, and
the in-game probes it touches re-run (documented in the release notes). Order chosen so the
**portability blocker lands first** (nothing else matters to a stranger if textures need
Python), then safety, then depth.

### 0.14 — "Runs anywhere" (portability)
1. ~~**Native texture importer** replacing the Python shell-out.~~ DONE 2026-09-17
   (forgecore `texturewrite`, upstream `b7ea3c8`; validated against the Python
   reference and in-game). Remaining text kept for the record. Pieces already in tree:
   `src/dxt1.hpp` (DXT1 encode), `src/lzo1x.cpp` (LZO1X encoder incl. the engine-safe
   M2/M3/M4 rule), `forge::big` (reader with `Entry::data` writer field). To write:
   - DXT3 encoder (minimaps use DXT3; ~80 lines next to dxt1),
   - Fable texture payload = 34-byte Info + chunked-LZO mip 0 (with the `[0xFFFF][u32]` escape
     when a raw chunk >= 64 KiB — the rule found 2026-09-17, port of `lionhead_lz_compress.py`)
     + raw mips (`texture_build.py:311-464` is the spec),
   - GBANK_MAIN_PC append/replace writer (port of `bigb_write.py` entry rewrite, 700 lines of
     Python but the TOC logic is small).
   Upstream it to FableForge's forgecore first (`terraintex.cpp` `importPng`), then sync the
   vendor copy — same rule as every forgecore fix today.
   Verification: byte-compare our payload against `texture_build.py` output for the same PNG
   (PSNR=inf on the DXT blocks is not required; the Info header, chunk framing and TOC must be
   identical), then the existing `custom_theme_deploy.txt` + `atlas_check.png` in-game probe.
2. ~~**Install detection & first-run**~~ DONE 2026-09-17 (Setup panel, GOG/Steam candidates on C..H).
3. **CI + public repo** (workflow committed 2026-09-17; the public remote is the user's call): GitHub Actions building with the same MinGW/CMake profile and
   running `check_all.py --no-install` subset (unit, LZO cross-check, UI scripts that don't
   need an install — `noinstall.txt` exists). Retail-dependent tests stay local.

### 0.15 — "Can't hurt you" (safety + UX honesty)
1. ~~**Backup manager**~~ DONE 2026-09-17 (`src/backups`, CLI `backups`/`restore`, Setup panel). Was: one place listing every `.atlas-orig` (Levels, CompiledDefs,
   textures.big, FSE/PartyMode.lua, loose LEV/TNG), with *Restore everything to retail* and
   per-file restore, refusing while the game runs. Reuse `backupOnce` sites (`src/worldedit.cpp:44`,
   `src/leveledit.cpp:452`) by routing them through one registry (`src/backups.{hpp,cpp}`)
   that records what was backed up and when.
2. ~~**Engine-rule prompts at the point of action**~~ DONE 2026-09-18 (`raiseRule`/`drawRuleNotice`
   under the Place / spawner / new-level buttons, "Got it" per session). Was: (not the log): new region -> "start a new
   game"; placing a creature/spawner -> "existing saves won't show it; spawners need an
   adult hero"; deploy while game runs -> already refused.
3. ~~**Undo coverage**~~ DONE 2026-09-18 (palette in `TerrainState`; World tab undo/redo stack).
   Was: palette add (`Document::addGroundTheme`) and region edits are outside
   the undo stack today; put them in (snapshot the LEV palette; World tab already has Revert).
4. ~~**Loose-file honesty**~~ DONE 2026-09-18. Was: rename "Save .tng (loose file)" to "Save draft" and make *Write
   into FinalAlbion.wad* the primary action, since the engine never reads the loose copy.

### 0.15b — "Feels like a tool" (UI/UX polish; runs alongside 0.15 and 1.0-rc)

The bones are right (dark theme, purple accent, 3-pane, `theme::S` DPI scaling, Unreal
camera + QWER). What a stranger hits is *density and feedback*:

1. ~~**Right panel structure**~~ DONE 2026-09-18: sub-tabs *Objects | Terrain | Actors | Level*
   under the Tool card (`editTab_`, remembered in settings.json; Terrain <-> terrain tool
   follow each other; actions open the tab that owns their card). Paint lives inside Terrain
   (the brush card), so no separate Paint tab. Was: one long scroll of cards.
2. ~~**Feedback**~~ DONE 2026-09-18: toasts (every warn/error/success log line, 6 s, viewport
   top-right); the long jobs report their stage through `editor::ProgressFn`
   (`NewLevelRequest::progress`, `BlankLevelRequest::progress`, `applyWorldEdits(...,
   progress)`, `Document::deployTerrain(..., progress)`) and the busy button reads
   `Installing: baking the minimap  (12 s)`; the header status turns amber `writes -> <root>`
   when the save root is redirected. Not done: a real progress *bar* (the stages have no
   fixed weights; the label + elapsed time is the honest version).
3. **Viewport overlays**: DONE 2026-09-18 -- *Grid* chip (LEV cell lines, heavier every 8 =
   one terrain patch, screen-space width, per-cell lines fade out when a cell is under ~6 px),
   map-local `x y h` under the cursor (bottom-left pill; state `cursor_ground`), compass
   (Fable north) bottom-right; brush radius ring existed. Still open: falloff ring, selection
   bounds, the picture-in-picture minimap with the camera frustum.
4. **Pickers with pictures**: theme picker swatches DONE 2026-09-18 (the palette combo, the
   game-theme search list and a "painting with this theme" row show the base albedo, 64x64
   box-filtered from the texture cache, `Renderer::swatch`); *Add an object* mesh thumbnails DONE
   2026-09-18 (`Renderer::thumbnail`: the def's mesh rendered once into a 96 px target from a
   three-quarter view, one decode per frame for the visible rows, cached by def). Still open:
   texture-tab thumbnails (the list is 6,300 long; the preview covers the selected one).
5. **Drag & drop**: PNG/JPG/TGA onto the window = custom ground texture (DONE 2026-09-18:
   lands in the Terrain tab's custom-texture input with a `GROUND_<file>` name, auto `drop
   <file>`); `.lev` already opened as a loose map. GLB onto the viewport = import model (0.17).
6. **Discoverability**: ~~a `?` shortcut cheat-sheet overlay~~ DONE 2026-09-18 (`?` / F1 /
   header button; camera, objects, terrain, World, everywhere), empty-state hints exist
   ("Pick a map on the left", "Nothing selected", "This map has no .lev"), ~~first-run
   tour~~ DONE 2026-09-18 (three callouts after the first Setup: maps, tabs, viewport).
7. ~~**Consistency pass**~~ DONE 2026-09-18: one `App::confirmRow` (amber question, *Yes, ...* /
   *Cancel*) for the WAD write, terrain write, world apply and the backup restore; every
   button that touches the game reads "... into the game" / "Write into FinalAlbion.wad"
   and is the primary of its card; each confirm names the files and the backup.
8. ~~**Accessibility/perf of the UI**~~ DONE 2026-09-18: *Interface > Text size* slider
   (80..150 %, on top of DPI + window size, `settings.uiScale`), blocked cells in the
   walkable view are orange AND diagonally striped (reads without the hue), the objects
   list was already `ImGuiListClipper`-virtualised.

Verification: each item gets a `tests/ui/*.txt` script asserting widgets + a screenshot the
user reviews (the existing `screenshot` command), plus the DPI sweep 1024x700..2560x1440
that the theme pass already used.

### 0.16 — "Depth" (level design)
1. **Foliage brush**: scatter grass/trees into the local-detail tree. Writer for type-1
   repeated-mesh instances and type-0 meshes using the grammar in `src/stbrelocate.cpp`
   (`groupBody`) and forgecore's `encodeGroupContents` (`stbbake.cpp:1149`); bounds/spheres
   via the same helpers `zBox/zSphere` use. Preview via `foliageexport`.
   *Scoped 2026-09-18, not started:* forgecore already authors a whole local-detail section
   from placements (`buildType0LocalDetailSection`, type-1 grass proven in-game on
   ForgeTest64; type-0 trees are the open "type-0 strategy"), but only for authored 64x64
   chunks. A brush on a retail map needs (a) decode the chunk's existing local-detail
   groups into placements (`stbrelocate.cpp` walks them; a full decode is the gap),
   (b) add the painted ones, (c) re-encode the section for the map's real size and splice
   it into the chunk on deploy (`Document::deployTerrain` re-bakes heights/layers but keeps
   the section verbatim). Each step is byte-checkable offline (re-encode retail = retail),
   but the result needs the in-game harness before it ships -- do it with the user present.
2. ~~**Presets**~~ DONE 2026-09-18: `src/presets` (a preset is a valid loose .tng with a
   two-line header; shipped in `presets/` next to the exe, yours in `%APPDATA%\AlbionAtlas\presets`),
   Actors-tab card (click to place at the view centre as a selected group; *Save N selected
   objects as a preset*), 4 retail-derived presets built by `tools/build_presets.py` (bandit
   camp fire, Oakvale fence + gate, cottage furniture, graveyard corner; Objects only --
   creatures/villagers need the placer's world-space InitialPos, so a camp's spawner is added
   from the spawner card).
3. **Region entrances / map travel**: `src/gtg` DONE 2026-09-18 -- `FinalAlbion.gtg` is one
   TNG-shaped section per WLD map slot (`NEWMAP n` ... `ENDMAP`, CRLF, 151 of 399 slots
   present in retail; parsed and re-serialised byte-exact). An own-region level now gets a
   REGION_ENTRANCE_POINT + `<Level>HSP` start at its centre on install; the Level tab's
   *Region entrance* card and `AlbionAtlas entrance <map> [x y [z]]` show / set / move it.
   *(still untested in-game: whether the map screen needs anything beyond the entrance thing
   + the region's minimap -- verify with the harness on a fresh game.)*
4. ~~**Multi-select + copy/paste**~~ DONE 2026-09-18: Ctrl+click (viewport / objects list)
   toggles; the gizmo and nudges move the set as a rigid group about the primary; Del /
   Ctrl+D / Ctrl+C / Ctrl+V act on the set, each one undo step (`Document::beginBatch`,
   `extract` / `paste` fragments with positions relative to their centroid, pasted at the
   view centre on the ground). The fragment is also the preset format for #2.

### 0.17 — "Content" (meshes, creatures, textures, particles, Blender)

Everything here already exists as **decoded formats and Python/Blender tooling in the FableTLC
repo**; the work is bringing it into Atlas so a modder never sees Python. Sizing is honest:
this is the biggest milestone and the one with real unknowns.

| Piece | What exists (verified) | What Atlas needs | Size |
|-------|------------------------|------------------|------|
| **Custom static meshes** (props, buildings) | Compiled-mesh grammar round-trips; `compose_mesh` builds type-1 (static) meshes from arbitrary geometry; `big_write.rebuild(adds=)` appends to graphics.big (`FableTLC docs/formats/MESH.md`, `tools/blender_addon/io_scene_fable/mesh_rw.py`). EgoCore `MeshCompiler.h`/`GltfMeshImporter.h` is the C++ oracle. Atlas already *reads* meshes (`thingsexport`, vendored `meshpreview`). | A C++ mesh composer (port of `compose_mesh` against EgoCore) + a graphics.big append writer (shares the TOC writer from the 0.14 texture importer) + a **`OBJECT_*` def** clone with `Graphic.modelId` = the new TOC id (`bin::File::addEntry` + `defedit::setField`, same as `theme-add`) + an editor card "Import model (.glb)" that shows it in the preview and in *Add an object*. Needs the object's physics hull (CMESH/BBM `3DMF`, `PhysicsIndex`) or the thing is walk-through — *untested whether a null physics index is accepted*. | L |
| **Custom creatures / NPCs** | The whole chain was proven in July: skinned type-5 mesh (`clone_skeleton` + `encode_skin` against a donor skeleton), DXT texture, `CCreatureDef.Graphic.modelId`, crc0 names, self back-ref retargeting (`docs/DEF_LOAD_CONTRACT.md`, memory *custom-npc-pipeline*). ElevenLabs dialogue + lipsync (`tools/anim_build.py`, `lipsync_build.py`). | "New creature from a donor" card: clone `CREATURE_*` def (+ appearance/body refs retargeted), optional custom body mesh (skinned import via the mesh composer, donor skeleton), optional texture; then `place CREATURE_MY_NPC` already works. Voiced dialogue stays Blender/CLI-side for 1.x. | L |
| **Texture editing** (objects, creatures, UI) | 0.14's native importer covers PNG -> DXT1/DXT3 + Info + chunked LZO + GBANK append/replace; `texture_extract.py` covers the reverse (DXT decode already in Atlas `dxt1.hpp` + vendored `parse_texture` logic). | ~~A **Textures** tab~~ DONE 2026-09-18: `src/texturebrowse` + `gui/textures.cpp` (4th panel tab: browse all banks, search, preview, *Export PNG*, *Replace from image* (slot size + format kept, validated), *Add a texture*; the selected object's textures from its mesh parts on top); CLI `textures` / `texture-export` / `texture-replace` / `texture-add`; `tools/test_textures.py` round-trips a barrel (mean diff 0.02). In-game: not yet (a retextured barrel screenshot is the user's probe). | M |
| **Particles / effects** | `effects.big` grammar decoded; Atlas `src/effects.cpp` *reads* it (1165/1165); EgoCore `ParticleCompiler.h` is the exact writer; `ParticleTypeName` in the TNG is plain text. | Placement already works (`PARTICLE_EMITTER_PLACEABLE`, opt-in preview). ~~Add a picker of effect names when placing an emitter~~ DONE 2026-09-18 (Actors tab *Particle effect* card: search the 1,165 effects.big names, `Document::placeEmitter` writes the retail CTCDParticleEmitter block; auto `place_emitter <FX> [script]`), and later an **Effect editor** (edit colour/size/rate fields of a cloned effect, append to effects.big via the shared big writer). | S (picker) / M (editor) |
| **Blender integration** | `tools/blender_addon/io_scene_fable` (import/export meshes, skin, bones; headless tests pass). Atlas exports GLB/OBJ of whole maps. | Ship the addon in the Atlas zip (it is MIT, same author); Atlas's "Import model" accepts the GLB the addon exports; document the loop *Atlas export -> Blender -> addon export -> Atlas import*. Later: a "Send to Blender / Bring back" pair that shells to the user's Blender for a selected thing. *Checked 2026-09-18: not a plain copy -- `io_scene_fable` imports FableTLC's `tools/` Python (parse_bigb, the LZ decoders...) via `sys.path`; it has to be made self-contained (vendor those modules into the addon package) before it can ship.* | M (bundle, after the untangling) / M (round trip UX) |
| **Custom animations** | `ANIM.md` §10: 3DAF writer + lipsync writer proven. | 1.x: attach a custom idle/anim to a placed creature through its def (`AnimationSet`); needs an in-game probe first. | L, *(untested in Atlas)* |

Order inside the milestone: textures tab (reuses 0.14) -> static mesh import -> creature clone
card -> effect picker. Each step gets the same treatment as everything else: scratch-install
script + one in-game probe (a custom barrel mesh standing in Greatwood, a custom NPC found by
`--things`, a retextured object visible in a screenshot).

### 0.18 — "Water" (post-1.0; RE complete 2026-09-19, see `FableTLC docs/engine/WATER_RE.md`)

How retail does it (from the debug build): water is **painted as depth themes** (`WaterType`
1 lake / 2 river / 3-5 sea / 8 ice, `WaterHeight` ladder 0..16; surface = ground + Σ blend ·
WaterHeight, `CEngineMap::PeekWaterHeight`) and the **visible surface is a baked STB mesh**
(`CWaterPatchMesh::Save` in the foreground frame after `hasWater`, a background sub-patch in
the patch trailer) that retail only loads. Gameplay water follows the paint alone; our deploy
writes `hasWater = 0` for regenerated patches, so a painted lake is invisible until (2).

1. **Water brush** (Terrain tab): surface altitude + body family (lake / river / sea / ice);
   each cell with ground < altitude gets a two-theme mix from the family's depth ladder so
   Σ blend · WaterHeight = altitude − ground, the ground theme kept in the third slot.
   Preview = the exporter's existing water surface (same formula). Ships gameplay water.
2. **Foreground writer**: `CWaterPatchMesh::Save` per touched patch (289 x 66 B, layout and
   every constant known; shore arrays zeroed, `distToShore` large). Gate: retail patches must
   round-trip byte-exact through the same encoder first. Expected in-game: the surface within
   the foreground radius, no foam.
3. **Background sub-patch + edge strips** (RE `CWaterGenerator::BuildStaticMapBackgroundBuffers`,
   the 0x38 `CTVertexWaterBackground`, `TesselateEdge`), then the shore generator (foam),
   then sea bodies (`__ENGINE_SEA_STATIC_MAP_BANK_FILE__*`; a lake never needs one).

Fishing spots are done (2026-09-19, Actors card + `place_fishing_spot`): a `MARKER_FISHING_SPOT`
thing with an optional `CTCContainerRewardHero` first catch -- no water dependency.

### 1.0-rc — "Polish, docs, and a stranger's test"
1. Docs: ~~a 10-minute "first level" walkthrough with screenshots~~ DONE 2026-09-18
   (`docs/FIRST_LEVEL.md` + `docs/walkthrough/*.jpg`, harness-captured, shipped in the zip;
   ends with the *cannot yet* table), ~~the CLI reference generated from `main.cpp` usage text~~
   DONE (`tools/gen_cli_reference.py` -> `docs/CLI.md`, regenerated by `package.py`), ~~an
   "engine rules" FAQ page~~ DONE (`docs/ENGINE_RULES.md`, each rule with its evidence; shipped).
2. Performance: the albedo bakes (`terrainexport::buildScene`, both the STB-pass and the
   LEV-blend paths) run row-parallel since 2026-09-18 (byte-identical output; 128x224 map at
   32 texels/cell 6.9 s -> 3.5 s, at 16: 1.8 -> 1.1 s). Measured: the paint preview bakes
   at 4 texels/cell and was already under a second, so a dirty-patch re-bake is not worth
   its complexity now. `chunk-audit --all` audits the 399 maps on every core (report byte-identical,
   88 s -> 70 s only: the audit is allocation-bound on the CRT heap, so more threads buy little).
3. ~~Split `src/main.cpp` into `src/cli/*.cpp`~~ DONE 2026-09-18 (`common` / `levels` /
   `textures` / `install` / `world` / `chunks` / `export`, each family a
   `std::optional<int> runX(cmd, args)`; the duplicated backups block went; outputs diffed
   identical against the pre-split binary). The automation dispatcher is `gui/automation.cpp` too.
4. STB compaction: a `chunk-compact` pass re-laying fg run / LOD blocks / LD section
   contiguously (all three reference sets are already collected in `stbrelocate::run`), and
   an STB-level compaction that drops superseded payloads.
5. A "stranger's test": fresh Windows VM or a second PC, retail Steam install, the zip only —
   follow the walkthrough; every step that needs a workaround becomes a bug.

### 1.0
Tag, zip, Discord post; `docs/PLAN.md` becomes `docs/ROADMAP.md` (post-1.0 items) and the
current PLAN's history moves to `docs/journal/`.

---

## 3. Verification (how each milestone is proven)

- **Offline**: `python tools/check_all.py` (unit, LZO vs minilzo and vs the engine's asm
  decoder, retail smoke, 8 UI scripts, new-level + overworld scratch suites). New features
  add a scratch-tree script under `tests/ui/` the way `theme_deploy.txt`, `custom_theme_deploy.txt`
  and `world.txt` do.
- **In-game** (user-driven or via `tools/ingame/ingame_terrain_test.py`): each milestone
  re-runs the live probes it touches (`*_live.txt` scripts) — childhood save for terrain/things,
  `--save-from Cornelio` for spawners, `--new-game` for new regions. Screenshots are the
  evidence; the user reviews them critically.
- **Portability**: the 0.14 importer is byte-compared against the Python reference on the
  same PNGs before the Python path is removed; the "stranger's test" in 1.0-rc is the final gate.
- **Restore discipline**: every probe ends with the backup manager's restore (or today's manual
  `.atlas-orig` copy-back) and a `cmp` against the backups — the mistake from this session
  (deleting the backups instead of moving them back) becomes impossible once restore is a
  button that only moves.

## 4. Critical files

- Texture import: `vendor/forgecore/src/terraintex.cpp` (`importPng`, `findTextureBuilder`),
  `src/dxt1.hpp`, `src/lzo1x.cpp`, `vendor/forgecore/include/forge/big.hpp`; reference
  `D:\Documents\FableTLC\tools\texture_build.py`, `lionhead_lz_compress.py`, `bigb_write.py`.
- Backups: `src/worldedit.cpp:44` and `src/leveledit.cpp:452` (`backupOnce`), `src/livelink.cpp:154`.
- Editor/UI: `gui/editor.cpp` (cards), `gui/app.cpp` (automation + state dump), `gui/world.cpp`.
- Foliage: `src/stbrelocate.cpp` (`groupBody`, `ldLayout`), `vendor/forgecore/src/stbbake.cpp:1149`.
- CLI split: `src/main.cpp`.
- Docs: `README.md`, `docs/EDITOR.md`, `docs/PLAN.md`, `docs/AUTOMATION.md`.
- Content pipeline references (FableTLC): `docs/formats/MESH.md`, `MESH_COMPOSE.md`,
  `BIG_WRITER.md`, `TEXTURE_WRITER.md`, `EFFECTS_FORMAT.md`, `DEF_LOAD_CONTRACT.md`,
  `ANIM.md`; tools `tools/blender_addon/io_scene_fable/{mesh_rw,fable_core}.py`,
  `tools/big_write.py`, `tools/texture_build.py`; C++ oracle EgoCore
  (`MeshCompiler.h`, `GltfMeshImporter.h`, `ParticleCompiler.h`, `BankEditor.h`).
