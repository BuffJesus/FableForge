# Vendored code

## `forgecore/` — slice of FableForge `libs/forgecore`
Pinned snapshot of the parsers this tool needs, copied verbatim from
`D:\Code\FableForge` at commit `8d5ab30` (working tree, 2026-09-15):
`lev`, `terrain`, `terraintex`, `big`, `bin`, `defschema`, `defdecode`, `lzo`,
`env`, `wad`. Do not edit here — fix upstream in FableForge and re-copy.

## `embedded_schema.hpp`
The `CEngineThemeDef` entry of FableForge `docs/re_reference/def_schema.json`
(the ENGINE_THEME field order recovered from the FableWin donor Transfer). It
is the only def type needed to resolve a LEV ground theme to its textures.

## `third_party/`
- nlohmann/json — MIT, Niels Lohmann
- miniz — MIT, Rich Geldreich / Tenacious Software (PNG encoder)
- minilzo — GPL-2.0+, Markus F.X.J. Oberhumer (Lionhead's texture/chunk LZO1X)
