# The Albion Atlas editor

Albion Atlas started as an exporter; the editor makes it the tool the community
used the leaked Lionhead debug build for, on retail data, with undo, and without
the crashes. The engine formats are written by FableForge's `forgecore`
(vendored; `tools/sync_forgecore.py` keeps it byte-identical with the upstream).

## What works now (things)

* **Selection**: click in the viewport (CPU ray against every instance's real
  mesh, so you hit what you see), or the *Objects in this map* list. Children
  spawned by a mesh's `CREATEOBJECT` dummies (doors, windows, Arena stands)
  select their parent thing and move with it.
* **Transform**: ImGuizmo gizmo (Q select, W move, E rotate, R scale, optional
  snap), or the numeric fields. While dragging only the preview moves; the
  document gets one undo step on release. Frames use the engine's own object
  matrix (`CalcObjectMatrix`), so what you see is what the game composes.
* **Duplicate / delete / place**: verbatim block copies with a fresh UID in the
  file's `0xFFFFFE00` namespace, `thingplacer` blocks in retail field order
  (`Object` for `OBJECT_*`, `Building` for `BUILDING_*`). Placement lands on
  the LEV terrain height (bilinear, like the retail placer).
* **Undo/redo**: snapshot based (128 steps); indices are re-derived from UIDs.
* **Save / deploy**: `Save .tng` writes the loose file; `Write into
  FinalAlbion.wad` replaces the archive entry through `forge::wad::repack`
  (same-size payloads are patched in place, larger ones appended; every byte
  the reader does not interpret is preserved). One-time `.atlas-orig` backups.
  The game loads levels from the WAD, so deploy is what makes edits visible
  in-game; saved games cache region entities, so start a new game or enter the
  region fresh to see them.

## Implementation map

| Piece | Where |
|---|---|
| Document, commands, undo, diff, save/deploy | `src/leveledit.{hpp,cpp}` (headless; `tests/test_export.cpp::testLevelDocument`) |
| Per-instance rendering, picking, outline | `gui/renderer.{hpp,cpp}` (`uploadThings`, `pick`, `screenRay`) |
| Gizmo, panel, shortcuts, instance sync | `gui/editor.cpp` |
| Thing index on preview instances | `foliageexport::Instance::thing`, set by `thingsexport` |
| Scripted tests | `tests/ui/editor.txt`, commands in `docs/AUTOMATION.md` |

Instance matrices are kept as `local * thingWorld`: a move recomputes the
world matrices of every instance of that thing (including spawned children)
without touching the GPU meshes. Structural edits (add/remove/undo of those)
reload the things layer from the in-memory `.tng` text.

## Next

1. Terrain: sculpt / walkable / theme paint through `forge::terrain` with a live
   heightfield re-upload; STB heightfield bake on save (`forge::stbbake`, same
   topology), nav rebuild (`forge::navmesh`).
2. Visible material paint: regenerate STB foreground passes for a new material
   region (the open RE gate; `GetMappingDirectionBlend` is already ported for
   the viewer).
3. World: WLD map/region editing, new-level-from-donor (`forge::worldworkspace`).
4. Live link to the running game through ForgeFSE (spawn/move/reload without a
   restart).
