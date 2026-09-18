# Albion Atlas -> 1.0: what it should do, what it can't, and how we get there

## Resume here (2026-09-18 morning)

Where we are: **0.14 "runs anywhere" is 2/4 done, 0.15 "can't hurt you" is 4/4 done**,
all on `main`. `python tools/check_all.py` was ALL PASS (15/15, ~19 min) on the backups/
setup commits; every suite the 0.15 work touches (unit, editor, world/overworld, paths,
controls, wheel, foliage, region, no-install, ui smoke) re-run green after it.

Done today:
- 0.15 #2 engine-rule notices at the point of action: placing a creature / a spawner and
  installing a level with its own region slot show the rule under that button
  (`App::raiseRule` / `drawRuleNotice`, "Got it" hides it for the session; state
  `rule_notice`, auto `dismiss_rule <key>`).
- 0.15 #3 undo: the LEV palette rides in `TerrainState` (a palette add is one undo step
  and makes the terrain dirty, since it is written with the LEV); the World tab has an
  undo/redo stack over its pending moves / owner / visibility edits (Ctrl+Z/Y over the
  canvas, buttons next to *Revert all*, auto `world_undo` / `world_redo`, cleared on apply).
- 0.15 #4 "Write into FinalAlbion.wad" is the primary button; the loose .tng is "Save draft".
- FableForge: the tree did not link after `b7ea3c8` (stale `findTextureBuilder`
  declaration + CLI caller) -- fixed there, Atlas re-synced; `test_fse_native_overlay`
  now floors the manifest count (947) instead of pinning 933. 15/16 FableForge test
  binaries pass, `dirmask` skips without its retail fixture. **Still uncommitted there.**

Next in order:
1. 0.14 #3/#4: create the public GitHub repo (user), push, see CI go green; then the
   forgecore un-vendoring -- **needs the user to triage FableForge's uncommitted work**
   (`git -C D:\Code\FableForge status`: 37 modified + 63 untracked; the untracked set is
   11 new forgecore modules with tests, the GUI canvas + vendored imgui-node-editor, ~15
   docs; junk = `005fcb00`, `017d2463`, `017d6540`, `_wf.patch`, `build-debug/`,
   `build-wiring/`).
2. 0.15b UI/UX (panel sub-tabs, toasts, overlays), then 0.16 depth.
3. Tag 0.15 once #1's repo exists (release = zip in `dist/` + check_all green).

Install state: retail (`AlbionAtlas backups` -> 9 backed-up files, 0 differ). Saves:
`0atlas` and `1234234` carry their childhood autosaves, `0aa` removed, `Cornelio` is
the adult save (`--save-from Cornelio`). FSE has no Atlas hook installed.

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
| Water editing (lakes/rivers via theme WaterHeight)        | Themes carry water; painting a water theme already works, needs a dedicated UX + preview |
| Prefab / preset library (village, bandit camp, shop)      | Village + spawner + creature blocks exist; a preset = a TNG fragment with relative positions |
| Multi-select, copy/paste across maps, align/snap tools    | Editor is single-selection today |
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

## 1b. Decision (2026-09-17): Atlas becomes FableForge

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
3. **Viewport overlays**: grid at the LEV cell size, compass/north, map-local coordinates under
   the cursor, brush radius ring in world units (exists) + falloff, selection bounds; a
   "picture-in-picture" minimap of the whole map with the camera frustum.
4. **Pickers with pictures**: theme picker shows a 32px albedo swatch per palette slot (the
   texture cache already has the texels); *Add an object* shows a mesh thumbnail (render the
   preview mesh to a small RT once, cache by def); texture tab thumbnails.
5. **Drag & drop**: PNG onto the paint card = custom texture; GLB onto the viewport = import
   model (0.17); `.lev/.tng` already works.
6. **Discoverability**: a `?` shortcut cheat-sheet overlay (QWER, T, F, End, Ctrl+D, Del,
   [ ], Shift-invert), tooltips on every card header (most exist), first-run tour of the three
   tabs, empty-state hints ("Select a map on the left").
7. **Consistency pass**: one confirm-dialog pattern (the World footer and the Edit footer
   differ), one wording for install/write/deploy ("Write into the game"), consistent
   button hierarchy (primary = the one that touches the game), error text that says what to
   do next.
8. **Accessibility/perf of the UI**: font scale slider in Options, colour-blind-safe walkable
   overlay (red/white -> pattern), keep 60 fps with the objects list virtualised (`ImGuiListClipper`
   for 500+ things).

Verification: each item gets a `tests/ui/*.txt` script asserting widgets + a screenshot the
user reviews (the existing `screenshot` command), plus the DPI sweep 1024x700..2560x1440
that the theme pass already used.

### 0.16 — "Depth" (level design)
1. **Foliage brush**: scatter grass/trees into the local-detail tree. Writer for type-1
   repeated-mesh instances and type-0 meshes using the grammar in `src/stbrelocate.cpp`
   (`groupBody`) and forgecore's `encodeGroupContents` (`stbbake.cpp:1149`); bounds/spheres
   via the same helpers `zBox/zSphere` use. Preview via `foliageexport`.
2. **Presets**: save a selection as a TNG fragment with relative positions; place with one
   click (village + houses + villagers + guards; bandit camp = spawner + props). Ships with
   3–4 retail-derived presets.
3. **Region entrances / map travel**: parse and edit `FinalAlbion.gtg` (TNG text) so a new
   region gets a REGION_ENTRANCE_POINT and appears reachable from the world map. *(untested:
   what the map screen needs beyond the entrance thing — verify with the harness.)*
4. **Multi-select + copy/paste** in the editor (selection set on `App`, `Document::place`
   batches as one undo step).

### 0.17 — "Content" (meshes, creatures, textures, particles, Blender)

Everything here already exists as **decoded formats and Python/Blender tooling in the FableTLC
repo**; the work is bringing it into Atlas so a modder never sees Python. Sizing is honest:
this is the biggest milestone and the one with real unknowns.

| Piece | What exists (verified) | What Atlas needs | Size |
|-------|------------------------|------------------|------|
| **Custom static meshes** (props, buildings) | Compiled-mesh grammar round-trips; `compose_mesh` builds type-1 (static) meshes from arbitrary geometry; `big_write.rebuild(adds=)` appends to graphics.big (`FableTLC docs/formats/MESH.md`, `tools/blender_addon/io_scene_fable/mesh_rw.py`). EgoCore `MeshCompiler.h`/`GltfMeshImporter.h` is the C++ oracle. Atlas already *reads* meshes (`thingsexport`, vendored `meshpreview`). | A C++ mesh composer (port of `compose_mesh` against EgoCore) + a graphics.big append writer (shares the TOC writer from the 0.14 texture importer) + a **`OBJECT_*` def** clone with `Graphic.modelId` = the new TOC id (`bin::File::addEntry` + `defedit::setField`, same as `theme-add`) + an editor card "Import model (.glb)" that shows it in the preview and in *Add an object*. Needs the object's physics hull (CMESH/BBM `3DMF`, `PhysicsIndex`) or the thing is walk-through — *untested whether a null physics index is accepted*. | L |
| **Custom creatures / NPCs** | The whole chain was proven in July: skinned type-5 mesh (`clone_skeleton` + `encode_skin` against a donor skeleton), DXT texture, `CCreatureDef.Graphic.modelId`, crc0 names, self back-ref retargeting (`docs/DEF_LOAD_CONTRACT.md`, memory *custom-npc-pipeline*). ElevenLabs dialogue + lipsync (`tools/anim_build.py`, `lipsync_build.py`). | "New creature from a donor" card: clone `CREATURE_*` def (+ appearance/body refs retargeted), optional custom body mesh (skinned import via the mesh composer, donor skeleton), optional texture; then `place CREATURE_MY_NPC` already works. Voiced dialogue stays Blender/CLI-side for 1.x. | L |
| **Texture editing** (objects, creatures, UI) | 0.14's native importer covers PNG -> DXT1/DXT3 + Info + chunked LZO + GBANK append/replace; `texture_extract.py` covers the reverse (DXT decode already in Atlas `dxt1.hpp` + vendored `parse_texture` logic). | A **Textures** tab: browse textures.big by bank, preview, *Export PNG* / *Replace from PNG* (size-matched) / *Add*; a thing's material textures shown from the mesh's `TextureIDs[]` so "retexture this barrel" is two clicks. | M |
| **Particles / effects** | `effects.big` grammar decoded; Atlas `src/effects.cpp` *reads* it (1165/1165); EgoCore `ParticleCompiler.h` is the exact writer; `ParticleTypeName` in the TNG is plain text. | Placement already works (`PARTICLE_EMITTER_PLACEABLE`, opt-in preview). Add a picker of effect names when placing an emitter, and later an **Effect editor** (edit colour/size/rate fields of a cloned effect, append to effects.big via the shared big writer). | S (picker) / M (editor) |
| **Blender integration** | `tools/blender_addon/io_scene_fable` (import/export meshes, skin, bones; headless tests pass). Atlas exports GLB/OBJ of whole maps. | Ship the addon in the Atlas zip (it is MIT, same author); Atlas's "Import model" accepts the GLB the addon exports; document the loop *Atlas export -> Blender -> addon export -> Atlas import*. Later: a "Send to Blender / Bring back" pair that shells to the user's Blender for a selected thing. | S (bundle) / M (round trip UX) |
| **Custom animations** | `ANIM.md` §10: 3DAF writer + lipsync writer proven. | 1.x: attach a custom idle/anim to a placed creature through its def (`AnimationSet`); needs an in-game probe first. | L, *(untested in Atlas)* |

Order inside the milestone: textures tab (reuses 0.14) -> static mesh import -> creature clone
card -> effect picker. Each step gets the same treatment as everything else: scratch-install
script + one in-game probe (a custom barrel mesh standing in Greatwood, a custom NPC found by
`--things`, a retextured object visible in a screenshot).

### 1.0-rc — "Polish, docs, and a stranger's test"
1. Docs: a 10-minute "first level" walkthrough with screenshots, an "engine rules" FAQ
   (the *Can't* table above), the CLI reference generated from `main.cpp` usage text.
2. Performance: preview re-bake after each theme stroke is a full-map albedo bake
   (`terrainexport::buildScene`) — re-bake only dirty 16x16 patches; `chunk-audit --all`
   parallel over chunks.
3. Split `src/main.cpp` (1179 lines of `if (cmd == ...)`) into `src/cli/*.cpp` one file per
   command family; `gui/app.cpp` automation dispatcher into `gui/automation.cpp`.
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
