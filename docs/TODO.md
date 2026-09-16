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
- The Arena "missing stands": there is NO stand geometry in the data. Checked: Arena.tng
  (loose + WAD) places only the centre building, gates, traps and 140 audience things;
  `BUILDING_HERO_ARENA` has one Graphic (MESH_HERO_ARENA_CENTRE_01, r <= 22 m, no
  terraces in its radial section); `MESH_HERO_ARENA_SECTION_*` / `BUILDING_ARENA_INTERIOR_SECTION_*`
  defs are referenced by no .tng, not by Fable.exe strings, and the Arena region contains
  one map. The crowd (z 102.3 / 106.2) floats over sand at 92.5 behind the 2 m parapet.
- `MeshHeightOffset` is 0 on every OBJECT/BUILDING def sampled (400/400) — not a factor.

## Debug build as the oracle
`D:\Documents\FableTLC\debug_build\FableWin.exe` (Lionhead editor build, unoptimised, PDB
names in `D:\Documents\FableTLC\ghidra_out\fablewin_pdb_names.tsv`). Bodies read like source;
every call goes through one incremental-link `jmp` thunk, follow it once. A 30-line capstone
script (`wdis.py <exe> <va> <len> [names.tsv]`) is enough — no Ghidra session needed.
Targets for the remaining gaps:
- Texture tiling scale + cliff direction blend: `CEngineLandscapeMeshBuilder::GetPassFromTexture`
  `0x02cb0a10`, `BuildLayerMesh` `0x02cb12b0`, `GetMappingDirectionBlend` `0x02cae000`,
  `BuildMapDirMask` `0x02cae270`.
- Water plane: `CWaterSeaGenerator::CalcSeaHeight` `0x02e5da10`, `GenerateBuffers` `0x02e5ace0`,
  `CWaterGenerator::FindShorePointsInMap` `0x02e07490` (STB info block has ShorePointArray).
- Z-sprite trees: `CLocalDetailPrimitiveMeshZSpriteBatch` ctor `0x02ede6c0` (Load is a vtable
  slot, follow the thunk); retail ctor `0xBFAC70`.
- Any future placement doubt: `CalcObjectMatrix` `0x02ee3a00` / `Calc2DObjectMatrix` `0x02ee3850`.

## Correctness pass — remaining
- Systematic screenshot sweep of every level with `--things --foliage` from a fixed camera
  (only Arena, Oakvale West, Bowerstone Slums, Lookout Point, Hook Coast checked so far).
- Meshes with helper points / dummy objects: not composed (no evidence they affect placement).

## Other
- Texture tiling scale, water plane, creatures in bind pose (GUI toggle).
- The three synthetic-input UI suites (smoke/paths/controls) fail while retail `Fable.exe`
  is running (it holds the foreground); close the game before `check_all.py`.
