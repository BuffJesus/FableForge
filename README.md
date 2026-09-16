# AlbionTerrain

Export **Fable: The Lost Chapters** terrain to `.glb` (glTF binary) or `.obj`,
straight from your Steam install. Two small Windows executables, no dependencies:

* **`AlbionTerrainGUI.exe`** — pick your install, browse the 399 maps on the left,
  see the textured terrain in 3D in the middle, export on the right. Drag a `.lev`
  onto the window to open a loose file. Export one map or all of them.
* **`AlbionTerrain.exe`** — the same exporter as a command line tool.

Runs on anything with Direct3D 10-class graphics (falls back to the software
rasterizer if it has to).

```
AlbionTerrain list                             # every map in FinalAlbion.wad
AlbionTerrain info   Greatwood_1               # size, height range, ground themes
AlbionTerrain export Greatwood_1               # -> Greatwood_1.glb, textured
AlbionTerrain export Oakvale_1 --out oak.obj   # OBJ + MTL + PNG instead
AlbionTerrain export my_edited.lev --no-textures
```

## What you get

| Layer | Source | In the file |
|---|---|---|
| Heightmap mesh | `.lev` cell grid — one vertex per world unit, height = raw × 2048 | `POSITION` / `NORMAL` / `TEXCOORD_0`, two triangles per cell, Y-up (or `--up z`) |
| Ground texture | the per-vertex 3-theme blend → `ENGINE_THEME` defs in `game.bin` → `textures.big` entries, DXT-decoded and baked into one albedo PNG | `baseColorTexture` on a rough, non-metallic material |
| Splat layers (`--layers`) | same, unbaked | `_THEME_INDEX` / `_THEME_WEIGHT` vertex attributes, one PNG per theme in `<name>_themes/`, and `<name>.themes.json` |
| Walkability (`--walkable-colors`) | `.lev` walkable byte | `COLOR_0`: white = walkable, red = blocked |

The exporter never embeds retail data; it reads the textures from **your** install.

## Options

```
--out <path>        .glb (default, self-contained) or .obj (+ .mtl + PNG)
--install <root>    Fable TLC folder (default: auto-detect via Steam)
--no-textures       heightmap only; works without an install for loose .lev files
--layers            also write splat attributes + one PNG per ground theme
--texels <n>        baked albedo texels per cell edge (default 8)
--tile <units>      world units per texture repeat (default 4 — see "Known gaps")
--up <y|z>          y = glTF/Blender/Unreal convention (default), z = Fable native
--origin <x,y>      add a world offset to every vertex (for stitching maps)
--walkable-colors   COLOR_0 vertex colours
```

## Known gaps (honest list)

* **Texture tiling scale** — how many world units one ground texture repeat
  covers is not yet pinned from the engine. `--tile` defaults to 4; adjust to taste.
* **Cliffs** — the engine projects cliff textures along four horizontal
  directions; the bake approximates that by slope angle. `--layers` gives the raw
  inputs for an exact shader.
* **Holes / caves** — the mesh is the full grid. The engine's masked 16×16
  patches live in the STB and are not consulted yet.
* **Foliage and props** are not exported yet (planned: baked local-detail
  instances + TNG things as glTF nodes).

## GUI

```
AlbionTerrainGUI.exe [--install <fable-root>]
```

Left: searchable map list grouped by area (Ctrl+F). Middle: orbit with the left
mouse button, pan with the right, zoom with the wheel; Textured / Wireframe /
Walkable / Height views. Right: export settings, `Export <map>` (Ctrl+E),
`Export all`, activity log. Settings are remembered in `%APPDATA%\AlbionTerrain`.

## Building

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\albionterrain_tests.exe            # unit tests (synthetic .lev, no install needed)
python tools\retail_smoke.py --count 12  # exports real maps and validates every GLB
python tools\ui_smoke.py                 # drives the GUI, validates output + screenshots
python tools\check_all.py                # everything above
```

The GUI is tested by scripting itself (`--auto`, see `docs/AUTOMATION.md`): real
clicks on real widgets, state assertions, and pixel checks on backbuffer screenshots.

MinGW-w64 (WinLibs) or MSVC, C++20. The format parsers are a pinned snapshot
of [FableForge](https://github.com/BuffJesus)'s `forgecore` (see `vendor/VENDORED.md`).

## Credits

Format knowledge: the FableTLC decompilation project, FableForge, EgoCore (AeoN),
FableMod / ChocolateBox. Thanks to the Fable modding Discord.
