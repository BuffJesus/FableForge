# Vendored code

## `forgecore/` — slice of FableForge `libs/forgecore` (MIT)
Pinned snapshot of the parsers this tool needs, copied from `D:\Code\FableForge`
at commit `8d5ab30` (working tree, 2026-09-15): `lev`, `terrain`, `terraintex`,
`big`, `bin`, `defschema`, `defdecode`, `lzo`, `env`, `wad`, `foliage`,
`meshpreview`, `stb`, `stbinfo`, `rangecodec`, `tng`, `wld`, `textformat`.
Local edits (keep when re-copying):
* `src/lzo.cpp` — decoding routed through `src/lzo1x.cpp` (clean-room MIT
  LZO1X) instead of GPL minilzo; compression throws (not needed here).

## `embedded_schema.hpp`
Slice of FableForge `docs/re_reference/def_schema.json`: `CEngineThemeDef`
(LEV ground theme -> textures) plus every def type with a `Graphic` field
(OBJECT, BUILDING, MARKER, CREATURE, ... and class-named twins) so TNG things
resolve to meshes.

## `third_party/`
- nlohmann/json — MIT, Niels Lohmann
- miniz — MIT, Rich Geldreich / Tenacious Software (PNG encoder, zlib for bin)
- Dear ImGui (`vendor/imgui`) — MIT, Omar Cornut; Win32 + DX11 backends

## Test-only
- `tests/third_party/minilzo` — GPL-2.0+, Markus F.X.J. Oberhumer. Linked ONLY
  into `albionatlas_lzo_tests` as the reference decoder that the shipped
  clean-room LZO1X implementation is verified against. Not part of any
  distributed binary.

Format knowledge: FableTLC decompilation project, FableForge, EgoCore (AeoN, MIT),
FableMod / ChocolateBox decompiled references.
