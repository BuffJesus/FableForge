# Next session

## Done 2026-09-16 — placed-object orientation
- Thing composition was 90 degrees off (`-lx*forward + ly*right`). Now exactly the engine's
  `CEngineInternalPrimitiveMeshBase::CalcObjectMatrix` (retail `0x00bebaa0`, FableWin
  `0x02ee3a00`; row-vector rows `{-right, -forward, up, pos}`):
  `world = pos + lx*(-right) + ly*(-forward) + lz*up` (`thingsexport::thingBasis`, unit-tested).
  Evidence: Arena pit long axis E-W + N/S corridors + gate things at the corridor mouths +
  audience ring, all matching `MINIMAP_ARENA`; Oakvale fence lines join end-to-end instead
  of forming combs; the 180-degree sign is pinned by the audience billboards (front face =
  local -y) facing the pit 31/34 only with the engine's signs. Bowerstone Slums / Hook Coast /
  Oakvale eyeballed after the fix.
- The Arena "missing stands" — SOLVED (the earlier "no stand geometry exists" conclusion was
  wrong): meshes carry 3ds-Max dummy objects whose NAME is an instruction. The engine's
  `CTCMeshAutomaticEntityCreator::CreateChildThings` (FableWin `0x02570910`, retail
  `CreateObject` `0x0072ddb0`) parses every dummy name with CStringParser: `CREATEOBJECT <def>`,
  `CREATEBUILDING <def>`, `CREATEPARTICLE <fx>` and spawns the child at the dummy transform
  (`GetDummyObjectAssumingObjectAt` `0x01d0d7d0`). MESH_HERO_ARENA_CENTRE_01 carries dummies for
  BUILDING_ARENA_COMBAT_ENTRANCE_01, the podium entrance and the four INTERIOR_SECTIONs — all at
  identity (co-authored in the arena frame). 89 retail meshes use this (doors, windows,
  weathervanes, prison exterior sections, chained interiors). Dummy record = EgoCore
  `CDummyObject` {nameCRC, CMatrix3x4, bone}; names in the packed-names block by Fable CRC;
  the vendored meshpreview already decoded them as `Geometry::helpers`.
  Composition used: child rows = dummyR * parentR, child pos = parent pos + parentR * dummy_t
  (mesh cm). Verified: Arena stands/awnings/banners assemble around the pit, Bowerstone
  windows sit in their wall openings. Depth limit 4.
- DONE: particles — `src/effects.cpp` ports the effects.big grammar (1165/1165 parse fully,
  `AlbionAtlas effects` validates); CREATEPARTICLE dummies + PARTICLE_EMITTER_PLACEABLE things
  become tinted crossed-quad sprite proxies (additive sprites: luminance = alpha), RenderMesh
  systems the bank mesh scaled to its render size, CPSCLight a KHR_lights_punctual point
  light. Open question: RenderMesh size semantics (assumed: largest extent = size).
- `MeshHeightOffset` is 0 on every OBJECT/BUILDING def sampled (400/400) — not a factor.

## Debug build as the oracle
`D:\Documents\FableTLC\debug_build\FableWin.exe` (Lionhead editor build, unoptimised, PDB
names in `D:\Documents\FableTLC\ghidra_out\fablewin_pdb_names.tsv`). Bodies read like source;
every call goes through one incremental-link `jmp` thunk, follow it once. A 30-line capstone
script (`wdis.py <exe> <va> <len> [names.tsv]`) is enough — no Ghidra session needed.
Targets for the remaining gaps:
- DONE: texture tiling — foreground vertices carry no UVs; `CEngineLandscapePatch::RenderForeground`
  `0x02dfa0a0` feeds vertex-shader constants c2/c3 from `PositionToTextureUVTransformU/V`
  (initialisers `0x04008780` / `0x04008850`): dir 0 = (x/8, y/8); dirs 1-4 = (+-x or +-y)/8 with
  v = -z/8; plus a per-patch integer offset of floor(bboxMin/8). Default `--tile` is now 8.
- DONE: cliff mapping direction — the STB foreground frames ARE the engine's layer bake: per
  patch a list of passes {direction 0..4, texture, per-vertex blend + packed normal xy}.
  `stbterrain::loadLayers` reads them (`AlbionAtlas layers <map>` dumps), `buildScene` composites
  the passes with the engine's `GetMappingDirectionBlend` (FableWin `0x02cae000`: flatness
  t = clamp((asin(n.z)/(pi/2) - 0.5)/0.25); dir 0 -> t; dir d -> (1-t) * clamp(1 - (acos(n.xy .
  D_d)/(pi/2) - 0.25)/0.5), D = (0,-1),(0,1),(-1,0),(1,0)). `AlbionAtlas ground <map>` compares
  both bakes to the engine background bake (engine-pass bake wins on every map tried; the
  remaining MAE ~22-37 is the background's baked lighting, a known non-issue).
- DONE: water — `CEngineMap::PeekWaterHeight` `0x02d5dd80` = ground + `PeekWaterDepth`
  (sum blend*WaterHeight over the 3 slots), `PeekHasWaterFast` `0x02d5d620` (any slot WaterType != 0),
  `CWaterPatchMesh::FindCorrectWaterLevel` `0x02e67af0` (mean of non-zero heights in +-2 cells).
  `CWaterSeaGenerator::CalcSeaHeight` is only the curved far-sea disc (not exported).
- DONE: z-sprite trees — `CLocalDetailPrimitiveMeshZSpriteBatch::Load` `0x02edf3d0` gave the
  record (0x30 CMatrix3x4 + float + sphere, then float4 entries); decoded in
  `foliageexport.cpp::parseGroupFrame`. Side effect: type-0 placements after a type-2 primitive
  in the same frame were previously lost (Oakvale West 12 -> 20 trees).
- Any future placement doubt: `CalcObjectMatrix` `0x02ee3a00` / `Calc2DObjectMatrix` `0x02ee3850`.

## Correctness pass — DONE 2026-09-16 (see docs/SWEEP.md)
- All 399 maps screenshotted (contact sheets) + `tools/sweep_metrics.py` height-over-terrain
  audit. Fixed: texture-less physics-hull triangles rendering as black shells. The two
  ExecutionTree gate surrounds 31 m under the terrain are authentic leftovers (STB heights ==
  LEV heights there; docs/SWEEP.md).

## Other
- Creatures in bind pose (GUI toggle), water waves/shore foam. The two extra per-vertex bytes
  in the foreground frames (packed normal xy, 127 = 0) feed the runtime blend-table lookup and
  are not needed for the bake.
- The three synthetic-input UI suites (smoke/paths/controls) fail while retail `Fable.exe`
  is running (it holds the foreground); close the game before `check_all.py`.
