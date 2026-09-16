# Next session

## Correctness pass over all levels (user-reported, 2026-09-15)
- **Arena** is missing things and the **crowd objects are misplaced** in the preview/export.
  Suspects: thing orientation composition (RHSetForward/Up -> -fwd/right/up), meshes with
  `GraphicOverride`, things with `ObjectScale`, multi-primitive meshes, or defs whose
  Graphic model id points at a different LOD/holder mesh. Verify per-thing against the
  game: compare a few Arena things' world positions with the debug build / FableWin
  behaviour (Ego_r/FableWin PDB symbols: CThing placement, CGraphic model attach point).
- Systematic pass: export every level with `--things --foliage`, screenshot each from a
  fixed camera, and eyeball for missing/misplaced objects; keep a checklist per region.
- Check the debug build's handling of thing placement (mesh pivot / attach offsets,
  MeshHeightOffset from the def) — a def-level Z offset would explain crowd height errors.

## Foliage
- Type-2 z-sprite batches (distant tree impostors) still skipped. `?Load@CLocalDetailPrimitiveMeshZSpriteBatch`
  is in FableTLC `ghidra_out` labels (FableWin 0x017e0522 is the debug symbol; retail ctor 0xBFAC70).
  Decompile the retail Load and add the body grammar to `foliageexport.cpp::parseGroupFrame`.

## Other
- Texture tiling scale, water plane, creatures in bind pose (GUI toggle).
