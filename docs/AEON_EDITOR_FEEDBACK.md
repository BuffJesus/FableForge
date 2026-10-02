# Aeon's editor feedback: 2026-09-27 checkpoint

AeoN (AlbionSecrets) reviewed an early Forge build and advised finishing the
vanilla editor's workflows before widening the feature set. This is a check of
his points against the 0.18.0-rc.2 source, not a claim that every path has had
an in-game validation. Continue depth work in [FEATURE_DEPTH_AUDIT.md](FEATURE_DEPTH_AUDIT.md).

| Feedback area | Current Forge state | Next depth check |
| --- | --- | --- |
| UI hierarchy and side panels | The top bar is File, View and Help; left and right panes collapse independently (`Ctrl+[` and `Ctrl+]`). Compact menus fit 800 x 600 at 1.5x scale. | Review keyboard focus, live resize and popups across all tabs. |
| Ground texture quality | The editor has texture-detail controls and the World view loads near scenery. | Compare ground mip choice and filtering against a retail frame at equal camera distance. |
| Object and texture import inside editing | Imports remain available through Assets and tool commands; placing existing definitions is separate. | Test the whole import, placement, undo, backup and game-load path before simplifying either workflow. |
| Entity CTC properties | Properties are grouped by component, including `CTCDoor` and `CTCChest`; known scalar overrides and container contents are editable. | Inventory every retail component family and mark missing defaults, field types and round trips. |
| Object scale sensitivity | Measured 2026-10-02: the gizmo added 1% per screen pixel at every zoom (10 px = x1.1; 100 px left reached 0). Now exponential per logical pixel: about 0.3% (100 px = x1.35 or /1.35), Shift five times finer, snap rounds to 0.1 (`tests/ui/scale_drag.txt`). | Ask AeoN whether the new rate feels right; the Properties scale field now drags at 0.003 per pixel too. |
| Ground theme sampling and replacement | `Ctrl+click` samples a theme, `Ctrl+Shift+click` samples the replacement source. Brush, connected-patch and Replace All modes exist. | Verify blend slots, local detail and undo on retail maps. |
| ENGINE_THEME palette and local detail | The palette browses grouped `ENGINE_THEME` definitions. | Confirm the selected theme's local detail appears after deployment and in a running game. |
| Alternate worlds | File > Open world and `.wld` drag/drop load other world files. | Exercise an actual non-FinalAlbion world through map open, preview and save; check the World tab's editing limits. |
| Actors | The Actors tab lists all discovered `CREATURE_` definitions grouped from the defs. Presets remain an optional separate tool. | Compare grouping and placement defaults with the vanilla editor. |
| 3D whole-world view | World has textured 2D and streaming 3D flyover; nearby terrain, water and scenery load, and a selected map opens in Edit. | Compare transitions, missing assets and authored gaps at more map boundaries. |
| Terrain path and fractals | Dragging a two-point Path tool and the vanilla-derived fractal generator are present. 2026-10-02: the path's rule re-read from FableWin `EditDrawPathPenUndoable` (0x02975ad0): width = the pen radius from the line, height interpolated start to end, hard edge, and only 0 <= t <= 1. Forge had round end caps that flattened ground behind the start and past the end; now square ends like vanilla (unit test). | Compare fractal output to the vanilla tool; whether a softened path edge is wanted is a design question (vanilla has none). |
| Quest sections and links | Quest sections can be added/toggled; ownership, creature, track and region entrance links have editors. | Verify quest activation and link effects in a running game. |
| Environment and sound themes | Both can be painted on the map grid. | Check saved grids and in-game ambience; add a theme-aware sky/colour preview only with reliable engine evidence. |
| Navigation | NAVIGATION_SEED parsing and navmesh generation code exist, and points have visible glyphs. | Audit the user workflow for regeneration and painting, then test game pathfinding; a complete navigation editor is not yet established. |

The guiding order remains: complete and verify a vanilla workflow, refine its
presentation, then add features that the retail editor did not provide.

For source credit and the EgoCore repository, see [README credits](../README.md#credits).
