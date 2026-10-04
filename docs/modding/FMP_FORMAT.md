> Ported verbatim from FableForge-legacy `docs/FMP_FORMAT.md` on 2026-09-19 as the design reference for milestone 0.20 (Mod packs v1) in `docs/ROADMAP_1.0.md`; the commands it names live in `forge-tools.exe` (`tools/forge-cli/main.cpp`; spelled `forge-tools` below, the legacy repo called that exe `forge`).

# .fmp (Fable Mod Package) format — reverse-engineered

2026-10-04 historic wrapper support: 73 packages in the user's 2019 forum
attachment corpus start with little-endian `12345` followed by one zlib stream
containing a BIG FMP. The CLI's FMP list/extract/apply/rewrite paths now accept
these originals directly. Inflation is capped at 256 MiB and requires a complete
stream, no trailing bytes and a BIG magic. The source package is never changed;
rewrite/export still emit ordinary BIG FMP bytes. All 73 original listings match
their independently normalized copies; one payload rewrite matches, synthetic
wrapped/unwrapped apply and merge outputs match, malformed wrappers refuse, and
the existing 16 FMP record/refusal cases pass. This is
container access, not proof that the packages' linked definitions, asset banks
or gameplay are supported.

2026-10-03 linking correction: the importer previously skipped link metadata.
Controller Support's raw camera IDs did not match even normalized retail output;
12 of its 14 named references were wrong. Record names and binding counts alone
did not establish a valid import. The linker now resolves the package's 14
named references through retail, Aeon 5.03 and their composed banks. Its bounded
NRBF reader treats class names as inert data. Supported game-index scalar and
flat-vector paths and CDef listing references change only their four-byte words;
the composer carries symbols with the source chosen for each field. Unsupported
metadata is refused before definition output is written. F2 Melee's multi-target
lists, nested structures and asset destinations still need separate format
coverage; its definition package is now refused instead of silently ignoring
those links. The corpus smoke checks that refusal and continues supported
load-order/rollback checks with its separate level fixture. This does not prove
a playable F2 installation. Offline linking is not gameplay proof.

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
(`forge-tools fmp list`) parses ControllerSupport.fmp + HalsSword.fmp with zero trailing
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

- **Ingest:** `forge-tools fmp list <x.fmp>` → the exact records a mod changes, per
  container, by hash. That is a ready-made change set — no diff-against-vanilla
  needed for `.fmp` mods (only for whole-file Method-1 mods like Aeon/LC).
- **Interop:** `forge-tools fmp apply` installs through the same target files; `forge-tools
  fmp export` emits a `.fmp` so FableForge output loads in Fable Explorer / the
  tools people already use.
- **Merge:** `.fmp` sections drop straight into the record-level merge engine —
  each section is a per-container change set, exactly what `forge-tools defs merge`
  consumes. `.fmp` mods and Method-1 overlays merge through one pipeline.
- **The hash link:** the `.fmp` entry hash == the game.bin entry/field hash,
  now **CRACKED** — reflected CRC-32 of the name with seed 0, no final XOR
  (FINDINGS.md "game.bin FIELD ENCODING FULLY CRACKED"; impl
  `forge::defdecode::fieldTag`). So `.fmp` hashes now map back to names (build a
  name→CRC table from `names.bin` / def field names), making `.fmp` records
  human-readable and letting FableForge author `.fmp`s from scratch.

## Build order (task #13) — SHIPPED
1. ~~Header + footer parse~~ → `forge-tools fmp list [--json]` (banks, per-entry name /
   def-type / payload size). Shows a mod's footprint by container. **Done.**
2. ~~Inflate entries~~ → not needed: BIG entry `data` is stored raw (the earlier
   "zlib sections" read was pre-SOLVED misparse; the 0x78-9c hits were graphics
   payload internals). `forge-tools fmp extract <x.fmp> <outdir> [bank-filter]` dumps
   every entry payload. **Done.**
3. ~~Hash → name~~ → moot for `.fmp`: BIG entries carry plaintext DevSymbolNames
   and def-type subheaders; no hash resolution required. **Done.**
4. `forge-tools fmp apply <base-root> <x.fmp> <out-root>` (non-destructive: builds a
   drop-in root, never mutates the base — strictly safer than an in-place apply
   with `forge::stage` backups) and `forge-tools fmp export <base> <modded> <out.fmp>`;
   `.fmp` sources feed `forge-tools mods merge`. **Done.**

Validated against `HalsSword.fmp` (88,408 bytes, 17 entries / 6 non-empty banks):
byte-exact re-serialize (`forge-tools fmp _rewrite`), and every GameBINEntries payload
decodes against the 100% def schema with zero leftover bytes after
`fmp apply` + `forge-tools defs decode`.

Note: a stale pre-SOLVED prototype (`forge::fmp` — custom footer parser assuming
u32-length-prefixed section names, 12-byte hash/offset/size entries, zlib record
streams) was discarded 2026-07-20; `forge::big` is the single `.fmp` reader.

## Named historic corpus check (2026-10-04)

`tools/audit_named_fmp_stack.py` reads original packages from the named 1,142-FMP
archive into scratch space, checks their catalogue SHA-256 values, and composes
against this machine's installed TLC data without changing the archive or install.
The 14-case run at `build/named-fmp-stack-syhvax35/result.json` establishes:

- `100Multiarrow.fmp`, `Oak Vale bed.fmp`, `Berserk Scale 1.5.fmp`, and
  `Small Berserk.fmp` compose offline. Oak Vale bed repacks one WAD entry and
  produces `OakValeWest_v2.tng` plus provenance for five UIDs. After staging and
  opening that map, those things use the same viewport and Objects list as retail
  things; the provenance file supplies their mod badges.
- The two Berserk packages conflict on `SPECIAL_ABILITIES_BERSERK_DEF_INSTANCE` /
  `BerserkScale`. Both orders compose; the reported field winner follows the last
  package, and the output `game.bin` hashes differ. This is a real named-package
  check of field-level load order, not a synthetic conflict.
- The other selected packages refuse cleanly: Ghost Sword variants need verified
  nested weapon references; Young Hero Retexture needs `maintextures` support;
  Working Stick needs `FrontEndBINEntries`; the staff package has an unresolved
  multi-target link; 0 Silver Keys has ambiguous link metadata. Refused packages
  leave the requested output untouched.

This is an offline compatibility sample, not an in-game validation or a claim
that every package in the archive is supported. New `OBJECT_*` definitions still
need their graphics bank entries before the editor can show their thumbnails.

The full importer scan in `tools/audit_named_fmp_coverage.py` classifies all
1,142 catalogue rows by bank support and then tries every eligible package in
scratch. With the 2026-10-04 linker, 577 had nonempty banks on the current
allowlist; 117 imported offline, 450 refused on metadata/reference or schema
constraints. Another 556 carry unsupported bank types, and nine have no
recognized entries. This is importer coverage, not a claim that 117 packages
have been composed in stacks or played. The result is at
`build/named-fmp-coverage-b8woptao/result.json` in this checkout.

One verified expansion is `Vector_VCDefString__`: its four-byte values are
offsets into `names.bin`, including the sound arrays in Divine and Unholy
Powers. `tools/test_fmp_string_vector_links.py` independently checked 16
resolved targets in the output names table; the package also completed the
full one-mod composer. This change raised the eligible importer count from
100 to 116. `forge-tools fmp links <package> --json` exposes the decoded named
link metadata for further corpus investigation.

The `Components` link field has a narrower verified path too. Native format
notes in `D:/Documents/FableTLC/docs/engine/HERO_APPEARANCE.md` describe the
counted nine-byte CTC records: a `names.bin` offset, four-byte parameter and
one-byte flag, followed by an opaque tail. The linker now changes only the
name-offset word for `CTC List.<index>.CTC Name` and accepts the observed
four- or 104-byte tail lengths. `No Quiver.fmp` passes a full one-mod compose;
`tools/test_fmp_component_links.py` checked all five referenced component names
and byte-for-byte preservation of every parameter, flag and tail byte. This
raised eligible importer coverage from 116 to 117. Ghost Sword packages still
refuse on separate nested weapon references after passing the component field;
their unresolved links are not silently omitted.
