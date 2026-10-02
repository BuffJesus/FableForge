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
  Raise also handles camera coordinate bases and spline key positions using the
  original editor's height rules.
- **Clear all sounds.** The Sound tool clears, or fills, a map's ambient sound in
  one step.
- **Small Sound brushes work at the pointer.** Sound painting uses the original
  editor's rounded vertex pen and outlines the affected 4x4 cells at small sizes.
- **Precise Replace pen.** Replace uses the original editor's truncated vertex
  centre and strict radius, with considered-vertex dots for small brushes.
- **Environment painting.** Each touched 4x4 cell takes the chosen environment
  fully in one application. Small brushes outline the cells, and choosing
  no environment clears them.
- **Draw Paths** now has the original editor's square ends: ground behind the
  start and past the end of the drag is no longer flattened.
- **Pick a height in every sculpt tool.** Ctrl+drag samples the ground height
  continuously in Raise, Lower, Flatten, Smooth, Noise and Paths. Each shows
  the sampled height, ready to use with Flatten.
- **Fixed-point height keys.** Hold `-` or `=` in a sculpt tool to lower or
  raise its starting spot. Moving the pointer keeps that spot fixed, and
  releasing the key finishes one undo step. Other terrain tools ignore the keys.
  Speed is available in every sculpt tool, with the key step shown in its tooltip.
- **Precise sculpt cursors.** Exact-step Raise/Lower, Flatten and Smooth show their
  rounded centre and affected vertices for small brushes, including a single
  vertex at a 0.25-cell radius.
  Noise outlines its possible random poke area as a square.
  Held height keys show their distinct vertices and ask for a larger Radius
  when the fractional-radius pen reaches none.
- **Gentler scaling.** The scale gizmo changes size about 0.3% per pixel instead of
  1%, grows and shrinks symmetrically, and Shift makes it five times finer. The
  Properties scale field drags at the same gentler rate.
- **Textures list thumbnails** for every visible row.
- **Drop a model file** (.glb, .gltf, .obj) on the window to open the import form
  with its path and name filled in.
- **World tab:** the selected map's card opens it in the editor or straight into
  Fit to neighbours.
- **Snap to cell centres.** A saved placement option puts new and pasted things
  on the centres of the 1 m grid cells. Carry uses the same grid while keeping
  the object's height above ground.
- **Escape** cancels any pending write confirmation without clearing the selection;
  Enter never confirms a write.

## Known limits

The Replace pen's footprint still differs slightly from the original editor's.
