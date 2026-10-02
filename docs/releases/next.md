# FableForge, next release (unreleased)

Changes on `main` since 0.18.0. This page becomes the release notes when the
version is chosen; validation is recorded at release time, not here.

## Changes

- **User guide.** Short task pages under `docs/guide/` (paint the ground, Fit to
  neighbours, mod packs and load order, dialogue lip sync), linked from the README
  and shipped in the ZIP beside it.
- **Write terrain and objects together.** When the ground and the objects on it both
  changed, one button writes the terrain first and the objects only after that
  succeeded. If the terrain write fails, nothing else is written.
- **Safer painting.** Painting is refused until a real ground theme is picked; the
  engine's placeholder theme is no longer offered as a paint target.
- **Water ladders replace as one theme.** Replace and Flood treat every depth of a
  lake or sea (for example WATER_BWLAKE_0 to _16) as one theme, as the original
  editor does, and say which depths they include before Replace all.
- **Whole-map heights.** Raise (optionally with every object), scale or flatten a
  whole map's ground in one step (Terrain > Whole-map heights...).
- **Clear all sounds.** The Sound tool clears, or fills, a map's ambient sound in
  one step.
- **Draw Paths** now has the original editor's square ends: ground behind the
  start and past the end of the drag is no longer flattened.
- **Gentler scaling.** The scale gizmo changes size about 0.3% per pixel instead of
  1%, grows and shrinks symmetrically, and Shift makes it five times finer. The
  Properties scale field drags at the same gentler rate.
- **Textures list thumbnails** for every visible row.
- **Drop a model file** (.glb, .gltf, .obj) on the window to open the import form
  with its path and name filled in.
- **World tab:** the selected map's card opens it in the editor or straight into
  Fit to neighbours.
- **Escape** cancels any pending write confirmation without clearing the selection;
  Enter never confirms a write.

## Known limits

The Replace pen's footprint still differs slightly from the original editor's.
Whole-map Raise does not yet move camera-point targets.
