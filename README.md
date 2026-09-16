# Albion Atlas

Export **Fable: The Lost Chapters** maps — terrain, ground textures, grass, trees and
placed objects — to `.glb` (glTF binary) or `.obj`, straight from your Steam install.
Two small Windows executables, no dependencies:

* **`AlbionAtlasGUI.exe`** — pick your install, browse the 399 maps on the left,
  see the textured terrain in 3D in the middle, export on the right. Drag a `.lev`
  onto the window to open a loose file. Export one map or all of them.
* **`AlbionAtlas.exe`** — the same exporter as a command line tool.

Runs on anything with Direct3D 10-class graphics (falls back to the software
rasterizer if it has to).

![Greatwood in Albion Atlas](docs/screenshot_greatwood.png)
![Oakvale in Albion Atlas](docs/screenshot_oakvale.png)

```
AlbionAtlas list                             # every map in FinalAlbion.wad
AlbionAtlas info   Greatwood_1               # size, height range, ground themes
AlbionAtlas export Greatwood_1               # -> Greatwood_1.glb, textured
AlbionAtlas export Oakvale_1 --out oak.obj   # OBJ + MTL + PNG instead
AlbionAtlas export my_edited.lev --no-textures
```

## What you get

| Layer | Source | In the file |
|---|---|---|
| Heightmap mesh | `.lev` cell grid — one vertex per world unit, height = raw × 2048 | `POSITION` / `NORMAL` / `TEXCOORD_0`, two triangles per cell, Y-up (or `--up z`) |
| Ground texture | the per-vertex 3-theme blend → `ENGINE_THEME` defs in `game.bin` → `textures.big` entries, DXT-decoded and baked into one albedo PNG | `baseColorTexture` on a rough, non-metallic material |
| Splat layers (`--layers`) | same, unbaked | `_THEME_INDEX` / `_THEME_WEIGHT` vertex attributes, one PNG per theme in `<name>_themes/`, and `<name>.themes.json` |
| Walkability (`--walkable-colors`) | `.lev` walkable byte | `COLOR_0`: white = walkable, red = blocked |
| Foliage (`--foliage`) | baked local-detail instances in `FinalAlbion_RT.stb` — grass, flowers, bracken, bramble, stumps **and trees** (oaks, birches...) — + LOD0 meshes from `graphics.big` + their textures | one glTF mesh per plant (leaves/trunk as separate primitives), one node per instance under a `Foliage` root; cutout (`MASK`) materials. OBJ: baked into an extra object |
| Placed objects (`--things`) | the map's `.tng` — fences, walls, rocks, lamps, crates, buildings, chests — resolved through their `game.bin` definition's `Graphic` model id (or `GraphicOverride`) | same as foliage under a `Things` root; full orientation from `RHSetForward/Up`, `ObjectScale` honoured. Creatures only with `--creatures` (bind pose) |

The exporter never embeds retail data; it reads the textures from **your** install.

## Options

```
--out <path>        .glb (default, self-contained) or .obj (+ .mtl + PNG)
--install <root>    Fable TLC folder (default: auto-detect via Steam)
--no-textures       heightmap only; works without an install for loose .lev files
--foliage           add the baked grass/plants/trees as mesh instances (needs an install)
--things            add the placed objects from the map's .tng (needs an install)
--creatures         with --things: include creature meshes in bind pose
--layers            also write splat attributes + one PNG per ground theme
--texels <n>        baked albedo texels per cell edge (default 8)
--tile <units>      world units per texture repeat (default 4 — see "Known gaps")
--up <y|z>          y = glTF/Blender/Unreal convention (default), z = Fable native
--world             place the map at its WLD MapX/MapY so several exports line up in one scene
--origin <x,y>      explicit offset instead
--walkable-colors   COLOR_0 vertex colours
```

## Known gaps (honest list)

* **Texture tiling scale** — how many world units one ground texture repeat
  covers is not yet pinned from the engine. `--tile` defaults to 4; adjust to taste.
* **Cliffs** — the engine projects cliff textures along four horizontal
  directions; the bake approximates that by slope angle. `--layers` gives the raw
  inputs for an exact shader.
* **Foliage frames are found by grammar, not by directory** — every LZO frame
  in the map's STB chunk that parses as a cache-group collection is used (type-1
  grass batches and type-0 single meshes incl. trees). Type-2 z-sprite batches
  (distant impostors) are skipped and counted. Counts per mesh are in the log.
* **Terrain brightness** — Fable's base textures are authored dark (the engine's
  lighting brightens them in-game by a factor that is not pinned down); the export
  keeps the raw texels and offers `--gain` to brighten. Caves are heightfield floors
  with mesh walls — there are no holes to cut (verified against the STB frames).
* **No water plane** — sea themes export as their seabed texture.
* **No lights, particles, creatures (by default), scripts.**

## GUI

```
AlbionAtlasGUI.exe [--install <fable-root>]
```

Left: searchable map list grouped by the game's regions from `FinalAlbion.wld` (Ctrl+F);
`Export region` writes every map of the selected map's region in world coordinates so
they assemble themselves in Blender. Middle: the 3D view with
Unreal-editor controls — hold **RMB** to look around and fly with **WASD**
(**Q/E** down/up, **Shift** faster, wheel changes fly speed); **LMB** drag
dollies/turns, **MMB** drag pans, **Alt+LMB** orbits, wheel zooms, **F** frames
the map. Textured / Wireframe / Walkable / Height views, Foliage toggle. Right:
export settings, `Export <map>` (Ctrl+E), `Export all`, activity log. Settings
are remembered in `%APPDATA%\AlbionAtlas`.

## Building

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\albionatlas_tests.exe            # unit tests (synthetic .lev, no install needed)
python tools\retail_smoke.py --count 12  # exports real maps and validates every GLB
python tools\ui_smoke.py                 # drives the GUI, validates output + screenshots
python tools\check_all.py                # everything above
```

The GUI is tested by scripting itself (`--auto`, see `docs/AUTOMATION.md`): real
clicks on real widgets, state assertions, and pixel checks on backbuffer screenshots.

MinGW-w64 (WinLibs) or MSVC, C++20. The format parsers are a pinned snapshot
of [FableForge](https://github.com/BuffJesus)'s `forgecore` (see `vendor/VENDORED.md`).
MIT licensed; the shipped binaries contain no GPL code (LZO1X decoding is a
clean-room implementation verified against minilzo in the test suite).

## Credits

Format knowledge: the FableTLC decompilation project, FableForge, EgoCore (AeoN),
FableMod / ChocolateBox. Thanks to the Fable modding Discord.
