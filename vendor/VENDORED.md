# Vendored code

## `forgecore/` — FableForge `libs/forgecore` (MIT)
Full copy of the FableForge core library (parsers AND writers: LEV/TNG/WAD/WLD/
STB bake/nav/world workspace/defs/quests), synced from `D:\Code\FableForge`
with `python tools/sync_forgecore.py` (`--check` reports drift). Last synced
at commit `70522c7` (working tree, uncommitted forgecore changes)
Skipped: `audio.*` (miniaudio). Fixes made here are upstreamed to FableForge
first, then re-synced, so the copy stays byte-identical except for:
* `src/lzo.cpp` — the only local override: `forge::lzo` backed by the
  clean-room MIT LZO1X codec in `src/lzo1x.*` (decoder + optimal-parse
  encoder) instead of GPL minilzo / liblzo2. `compress` packs to 99.4% of
  retail lzo1x_999 output on the STB frames (2217/2223 fit their slot), so
  `compress999` maps to it as well.

## `embedded_schema.hpp`
Slice of FableForge `docs/re_reference/def_schema.json`: `CEngineThemeDef`
(LEV ground theme -> textures) plus every def type with a `Graphic` field
(OBJECT, BUILDING, MARKER, CREATURE, ... and class-named twins) so TNG things
resolve to meshes.

## `third_party/`
- nlohmann/json — MIT, Niels Lohmann
- miniz — MIT, Rich Geldreich / Tenacious Software (PNG encoder, zlib for bin)
- Dear ImGui (`vendor/imgui`) — MIT, Omar Cornut; Win32 + DX11 backends
- ImGuizmo (`vendor/ImGuizmo`) — MIT, Cedric Guillemet; the editor's transform gizmo

## Test-only
- `tests/third_party/minilzo` — GPL-2.0+, Markus F.X.J. Oberhumer. Linked ONLY
  into `albionatlas_lzo_tests` as the reference decoder that the shipped
  clean-room LZO1X implementation is verified against. Not part of any
  distributed binary.

Format knowledge: FableTLC decompilation project, FableForge, EgoCore (AeoN, MIT),
FableMod / ChocolateBox decompiled references.
