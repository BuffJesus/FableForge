> Ported verbatim from FableForge-legacy `docs/FMP_FORMAT.md` on 2026-09-19 as the design reference for milestone 0.20 (Mod packs v1) in `docs/ROADMAP_1.0.md`; the commands it names live in `forge-tools` (`tools/forge-cli/main.cpp`).

# .fmp (Fable Mod Package) format — reverse-engineered

Reversed from a real sample (`ControllerSupport.fmp`, 46,852 bytes) 2026-07-19.
The `.fmp` is the community's mod distribution format (created/loaded by Fable
Explorer / ShadowNet). It is a **structured record-delta package**: it stores the
specific game-container entries a mod changes, referenced by hash, with
zlib-compressed payloads — i.e. exactly FableForge's record-level model. Reading
it lets FableForge ingest the entire community mod library as first-class deltas.

## Container layout

```
offset 0  u32 magic          = 0x42 (66)
offset 4  u32 version        = 0x65 (101)   // format version
offset 8  u32 footerOffset                  // -> the section directory (VERIFIED 46290)
offset 12 u32 entryCount                    // = 510 in the sample (NOT the footer entry
                                            //   total, which is 10; meaning TBD)
offset 16 ...header tail / body begins
...
[ body: zlib-compressed payloads + per-section entry tables ]
...
[ footer @ footerOffset: section directory ]
```

Body payloads are zlib streams (37 in the sample; 0x78 9c/da headers); FableForge
already vendors miniz, so inflation is free.

## Footer: section directory (VERIFIED against ControllerSupport.fmp)

```
u32 sectionCount                     // = 16 in the sample
sectionCount x {
    char name[]                      // NUL-terminated, e.g. "GameBINEntries"
    u32 index                        // 0..sectionCount-1
    u32 count                        // entries in this section
    u32 offset                       // start of this section's entry table (abs file offset)
    u32 size                         // byte length of that table
    u32 flag                         // = 1 (meaning TBD)
}
```
The 16 sections, in order: `GameBINEntries`, `GameBINLinkMetaData`,
`ScriptBINEntries`, `ScriptBINLinkMetaData`, `FrontEndBINEntries`,
`FrontEndBINLinkMetaData`, `names`, `graphics`, `graphicsLinkMetaData`,
`maintextures`, `guitextures`, `frontendtextures`, `effects`, `text`,
`FinalAlbionWAD`, `FinalAlbionSTB`. Section `offset`/`size` are **contiguous**
(each section's table ends where the next begins), pointing into the region just
before the footer. Empty sections have `count=0`, `size=4`.

## Entry table (per section) — CORRECTS the earlier "hash-only" claim

`.fmp` entries store **plaintext names**, not hashes. In `ControllerSupport.fmp`,
`GameBINEntries` (count 5) held, per entry, a `[name][def-type]` pair:

| # | name | def-type |
|---|------|----------|
| 1 | `CAMERA_MANAGER_SET_TEMPLATE` | `CAMERA_MANAGER_SET` |
| 2 | `FABLE_XBOX_CONTROL_SCHEME_BASE` | `CONTROL_SCHEME` |
| 3 | `PLAYER_GUI_PC` | `PLAYER_GUI` |
| 4 | `PLAYER_GUI_DEFAULT` | `PLAYER_GUI` |
| 5 | `CAMERA_MANAGER_PC_MAIN_COMBAT` | `CAMERA_MANAGER` |

The def-type strings match game.bin's `definition` categories (`CONTROL_SCHEME`,
`PLAYER_GUI`, `CAMERA_MANAGER`), so `.fmp` entries map directly onto game.bin
records by (name, definition) — no hash resolution needed for these sections.

**SOLVED — the `.fmp` IS a Lionhead BIG archive.** The decompiled ChocolateBox
source (`FableMod.ContentManagement/ModPackage.cs` = a `BIGFile` wrapper;
`SilverChest.Formats.Big/BigReader.cs`) gives the exact container, and `forge::big`
(`forge fmp list`) parses ControllerSupport.fmp + HalsSword.fmp with zero trailing
slack. The "footer" above is the BIG **bank directory**; the header word I called
`entryCount` is actually `contentType` (510 = FMP_VERSION). Entry-table grammar:

```
entry table @ bank.entryStart:
  u32 typeCount, skip typeCount*8 bytes
  per entry:
    u32 magic (0 in old .fmp, 42 in retail .big), id, type, length, dataOffset,
    u32 devFileType, u32 nameLen, char[nameLen] name (DevSymbolName),
    u32 devCrc, u32 devSourceCount, devSourceCount x { u32 len, char[len] },
    u32 subHeaderLen, byte[subHeaderLen] subHeader
```
For BIN banks the `subHeader` is the ASCIIZ **definition-type** string and the
entry `data` (@dataOffset, `length` bytes) is the raw compiled-def payload — so an
`.fmp` entry maps directly onto a game.bin record by `(name, definition)`. Asset
banks (graphics/textures) carry binary subheaders. Full layout in
`libs/forgecore/include/forge/big.hpp`.

## Sections present (the full record-delta surface)

| Section | Targets |
|---------|---------|
| `GameBINEntries` / `GameBINLinkMetaData` | game.bin definition records + link metadata |
| `ScriptBINEntries` / `ScriptBINLinkMetaData` | script.bin (cutscenes/region scripts) |
| `FrontEndBINEntries` / `FrontEndBINLinkMetaData` | frontend.bin (UI defs) |
| `names` | names.bin string-table additions |
| `graphics` / `graphicsLinkMetaData` | graphics.big models |
| `maintextures` / `guitextures` / `frontendtextures` | texture.big sets |
| `effects` | particle/effect data |
| `FinalAlbionWAD` | level TNG/LEV entries |
| `FinalAlbionSTB` | static-map table |

## Why this matters

- **Ingest:** `forge fmp list <x.fmp>` → the exact records a mod changes, per
  container, by hash. That is a ready-made change set — no diff-against-vanilla
  needed for `.fmp` mods (only for whole-file Method-1 mods like Aeon/LC).
- **Interop:** `forge fmp apply` installs through the same target files; `forge
  fmp export` emits a `.fmp` so FableForge output loads in Fable Explorer / the
  tools people already use.
- **Merge:** `.fmp` sections drop straight into the record-level merge engine —
  each section is a per-container change set, exactly what `forge defs merge`
  consumes. `.fmp` mods and Method-1 overlays merge through one pipeline.
- **The hash link:** the `.fmp` entry hash == the game.bin entry/field hash,
  now **CRACKED** — reflected CRC-32 of the name with seed 0, no final XOR
  (FINDINGS.md "game.bin FIELD ENCODING FULLY CRACKED"; impl
  `forge::defdecode::fieldTag`). So `.fmp` hashes now map back to names (build a
  name→CRC table from `names.bin` / def field names), making `.fmp` records
  human-readable and letting FableForge author `.fmp`s from scratch.

## Build order (task #13) — SHIPPED
1. ~~Header + footer parse~~ → `forge fmp list [--json]` (banks, per-entry name /
   def-type / payload size). Shows a mod's footprint by container. **Done.**
2. ~~Inflate entries~~ → not needed: BIG entry `data` is stored raw (the earlier
   "zlib sections" read was pre-SOLVED misparse; the 0x78-9c hits were graphics
   payload internals). `forge fmp extract <x.fmp> <outdir> [bank-filter]` dumps
   every entry payload. **Done.**
3. ~~Hash → name~~ → moot for `.fmp`: BIG entries carry plaintext DevSymbolNames
   and def-type subheaders; no hash resolution required. **Done.**
4. `forge fmp apply <base-root> <x.fmp> <out-root>` (non-destructive: builds a
   drop-in root, never mutates the base — strictly safer than an in-place apply
   with `forge::stage` backups) and `forge fmp export <base> <modded> <out.fmp>`;
   `.fmp` sources feed `forge mods merge`. **Done.**

Validated against `HalsSword.fmp` (88,408 bytes, 17 entries / 6 non-empty banks):
byte-exact re-serialize (`forge fmp _rewrite`), and every GameBINEntries payload
decodes against the 100% def schema with zero leftover bytes after
`fmp apply` + `forge defs decode`.

Note: a stale pre-SOLVED prototype (`forge::fmp` — custom footer parser assuming
u32-length-prefixed section names, 12-byte hash/offset/size entries, zlib record
streams) was discarded 2026-07-20; `forge::big` is the single `.fmp` reader.
