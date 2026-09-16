# Next session

## Done 2026-09-16 — placed-object orientation
- Thing composition was 90 degrees off (`-lx*forward + ly*right`). Now
  `world = pos + lx*right + ly*forward + lz*up` (`thingsexport::thingBasis`, unit-tested).
  Evidence: Arena pit long axis E-W + N/S corridors + gate things at the corridor mouths +
  audience ring, all matching `MINIMAP_ARENA`; Oakvale fence lines join end-to-end instead
  of forming combs. Bowerstone Slums / Hook Coast / Oakvale eyeballed after the fix.
- The Arena "missing stands": there is NO stand geometry in the data. Checked: Arena.tng
  (loose + WAD) places only the centre building, gates, traps and 140 audience things;
  `BUILDING_HERO_ARENA` has one Graphic (MESH_HERO_ARENA_CENTRE_01, r <= 22 m, no
  terraces in its radial section); `MESH_HERO_ARENA_SECTION_*` / `BUILDING_ARENA_INTERIOR_SECTION_*`
  defs are referenced by no .tng, not by Fable.exe strings, and the Arena region contains
  one map. The crowd (z 102.3 / 106.2) floats over sand at 92.5 behind the 2 m parapet.
- `MeshHeightOffset` is 0 on every OBJECT/BUILDING def sampled (400/400) — not a factor.

## Correctness pass — remaining
- Systematic screenshot sweep of every level with `--things --foliage` from a fixed camera
  (only Arena, Oakvale West, Bowerstone Slums, Lookout Point, Hook Coast checked so far).
- Meshes with helper points / dummy objects: not composed (no evidence they affect placement).

## Foliage
- Type-2 z-sprite batches (distant tree impostors) still skipped. `?Load@CLocalDetailPrimitiveMeshZSpriteBatch`
  is in FableTLC `ghidra_out` labels (FableWin 0x017e0522 is the debug symbol; retail ctor 0xBFAC70).
  Decompile the retail Load and add the body grammar to `foliageexport.cpp::parseGroupFrame`.

## Other
- Texture tiling scale, water plane, creatures in bind pose (GUI toggle).
