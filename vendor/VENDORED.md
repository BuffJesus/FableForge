# Vendored code

## `../libs/forgecore/` — forgecore (MIT), canonical here since 0.16
The FableForge core library (parsers AND writers: LEV/TNG/WAD/WLD/STB bake/nav/
world workspace/defs/quests). It started as a vendored copy of the old FableForge
repo's `libs/forgecore` (last synced at its commit `8793a44` + working tree); with
the rename this tree became the canonical FableForge, so the library moved to
`libs/forgecore` and is edited in place (no sync script any more). The old repo is
kept as `D:\Code\FableForge-legacy` for its GUI canvas / quest tooling, to be
merged piece by piece. Local differences from the legacy copy:
* `src/lzo.cpp` — `forge::lzo` backed by the clean-room MIT LZO1X codec in
  `src/lzo1x.*` (decoder + optimal-parse encoder) instead of GPL minilzo /
  liblzo2. `compress` packs to 99.4% of retail lzo1x_999 output on the STB
  frames (2217/2223 fit their slot), so `compress999` maps to it as well.
* `audio.*` (miniaudio) was never carried over.
`vendor/third_party/stb/stb_image.h` (public domain) is forgecore's PNG reader for
the native texture importer.

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

## defc (jamen/fable-defs) -- shipped as `defc.exe` next to the exes, not vendored as source

The Fable def compiler (Rust, Zlib license, https://github.com/jamen/fable-defs): compiles a text
`Data/Defs` tree into the CompiledDefs bins byte-deterministically. FableForge runs it as a separate
process for the EgoCore pack type (`.def` text overrides) and never links it. Found next to the
running executable, then `FORGE_DEFC`, then PATH.
