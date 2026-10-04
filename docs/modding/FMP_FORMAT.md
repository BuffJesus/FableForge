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
- The other selected packages originally refused: Ghost Sword variants need verified
  nested weapon references; Young Hero Retexture needed `maintextures` support
  (now added and checked below);
  Working Stick needs `FrontEndBINEntries`; the staff package has an unresolved
  multi-target link; 0 Silver Keys has ambiguous link metadata. Refused packages
  leave the requested output untouched.

This is an offline compatibility sample, not an in-game validation or a claim
that every package in the archive is supported. New `OBJECT_*` definitions still
need their graphics bank entries before the editor can show their thumbnails.

The importer scan in `tools/audit_named_fmp_coverage.py` classifies all
1,142 catalogue rows by bank support. With `maintextures` added, 746 have
nonempty banks on the current allowlist: 117 non-texture packages import
offline, 450 refuse on metadata/reference or schema constraints, and 169
texture-capable packages are reserved for the large-bank audit below. Another
387 carry unsupported bank types, and nine have no recognized entries. The
report is `build/named-fmp-coverage-km2oevq6/result.json` in this checkout.
The earlier pre-texture importer scan is
`build/named-fmp-coverage-b8woptao/result.json` (117/577 accepted).

2026-10-04 numeric FMP record update: `GameBINEntries` with blank names now use
their package record ID only when that index exists in the selected baseline,
is also blank-named, and has the same definition type. Ordered link metadata
for those records is attached to the verified numeric owner. `mods merge`
tracks them under internal `@unnamed:<index>` keys, so load order, field
conflicts, and saved picks work without changing their names in `game.bin`.
Ordinary loose/overhaul directories still refuse changed or moved unnamed
records, and FMP export still refuses them. The current full eligible scan is
`build/named-fmp-coverage-by4g8egf/result.json`: 821 packages have only
currently recognized banks; 222 of the 577 small-bank candidates apply
offline, 336 refuse on link constraints, 11 need a definition schema, and
eight fail record identity checks. The other 244 packages have large texture
and/or graphics banks and were deferred from this apply scan. This raises
small-bank acceptance from 197 immediately before numeric support to 222;
the 112 packages with plausible anonymous identity in the metadata audit were
only candidates, not 112 successful imports. `tools/test_fmp_anonymous_layers.py`
checks archived Bandit Beard and both lamp packages, including both orders,
field conflicts, and a saved winner. `tools/test_fmp_anonymous_picks_ui.py`
checks the real conflict and deployment in the Mods UI. Neither is a gameplay
test.

2026-10-04 effect-reference follow-up: the `CChestDef.OpenParticleEffect`
metadata path `714092448.unknown` resolves against `PARTICLE_MAIN_PC` in the
selected `data/Misc/pc/effects.big`. Retail `CHEST_OPEN_01` is ID 391, matching
the stock chest field. Both the original `0 Silver Keys Needed.fmp` and
`No Silver Key Need (Variant 1).fmp` now apply in the full eligible scan:
224 accepted, 334 link refusals, 11 schema refusals, eight record refusals,
and 244 deferred large-bank packages (`build/named-fmp-coverage-i22yc968/result.json`).
The real Silver Keys direct apply and final composer produce byte-identical
`game.bin` and `names.bin`; missing `effects.big` refuses before writing.
`tools/test_fmp_anonymous_layers.py` checks these cases. Other effect link
shapes remain subject to their own layout and target verification.

2026-10-04 creature decay-effect follow-up: ChocolateBox control `2CF7F120`
appears as `754446624.unknown` in FMP links. In `CREATURE`, its four-byte
`EffectID` follows a four-byte float and the nested control tag inside the
schema's `RecoverEvadeHitsPercent` field. The linker checks that exact 12-byte
layout before writing the ID from `effects.big`. The archived
`Alwaysthere001.fmp` links `DEATH_CRUMBLE_HUMAN` to retail ID 909; direct
apply and final composition produce byte-identical definition banks. The full
small-bank scan at `build/named-fmp-coverage-xnelc49n/result.json` accepts
320 of 577 attempted packages, 96 more than before this field was supported.
The remaining 238 link refusals have other causes; 244 packages with large
texture or graphics banks were deferred. This remains an offline import test,
not proof that the mods work during gameplay.

2026-10-04 ranged-weapon follow-up: `CREATURE` metadata path
`706745797.Weapon3ID` is the first four-byte value of the existing guarded
28-byte `RangedWeaponDef` field. It links to a `game.bin` `OBJECT` record.
The original `No body Rots.fmp` replaces 510 records and carries 47,328
symbolic links; direct apply and final composition produce byte-identical
`game.bin` and `names.bin`, and its bandit archer's crossbow points to retail
record 5595. `tools/test_fmp_creature_weapon_links.py` checks that result.
The full eligible scan at `build/named-fmp-coverage-8zjp2lak/result.json`
accepts 338 of 577 attempted small-bank packages, 18 more than before this
field was supported. It still refuses 220 on other link constraints, 11 on
schema availability and eight on record identity, while 244 large-bank
packages were deferred. This remains an offline import check.

2026-10-04 hero morph follow-up: `CHeroMorphDef` link metadata now resolves
`TextureMorphs` base/applied texture IDs through `maintextures`, skeletal
bone config names through `names.bin`, and particle/idle particle helper names
and effect IDs through `names.bin` and `effects.big`. Each array has an exact
size and index check before rewriting. `tools/test_fmp_hero_morph_links.py`
checks the original archived `Clean Hero.fmp` (144 links) against both direct
apply and final composition, including retail texture IDs 1250 and 1025;
the output `game.bin` and `names.bin` bytes match. The full eligible scan at
`build/named-fmp-coverage-u89uksmq/result.json` accepts 354 of 577 attempted
small-bank packages, including 16 hero morph packages that previously failed.
It still refuses 204 on other link constraints, 11 on schema availability and
eight on record identity; 244 large-bank packages were deferred. This is
offline import evidence, not an in-game compatibility claim.

2026-10-04 special-effects follow-up: `CSpecialEffectsDef.SpecialEffects`
contains a count followed by eight-byte action/effect pairs. ChocolateBox's
`CSpecialEffectsDefList.{i}.CSpecialEffectsDefLink` points to the second word
of each pair in `effects.big`. Forge validates the complete array and index
before patching. `tools/test_fmp_special_effect_links.py` checks the archived
`Fireberserk.fmp` against direct apply and composition, and independently
checks its first two targets against retail effect IDs 149 and 217. The full
eligible scan at `build/named-fmp-coverage-9_08ga8h/result.json` accepts 355
of 577 attempted small-bank packages, one more than before; 203 still refuse
on other link constraints, 11 on schema availability and eight on record
identity. The 244 large-bank packages remain deferred. Several packages with
this field now pass it but still need separate link types such as SoundMap.

2026-10-04 sound-map follow-up: `CEntitySoundDef.SoundMap` in the archived
hero packages has zero single mappings, then one eight-byte criteria pair.
The metadata path `1791516475.unknown.0.Default Fire Effect` addresses the
second word of that pair, a `names.bin` offset for `ON_FIRE`. Forge accepts
only that verified no-single layout and checks its count, size and index.
`tools/test_fmp_sound_map_links.py` verifies the original archived `Full Hero
JOB Playable Set (Variant 4).fmp` through direct apply and composition: both
definition banks are byte-identical and the patched offset resolves to
`ON_FIRE`. The full eligible scan at `build/named-fmp-coverage-mb94v1xt/result.json`
accepts 360 of 577 attempted small-bank packages, five more than before.
Two other packages with this link still refuse later unsupported references.
The scan still defers 244 large-bank packages and does not verify gameplay.

2026-10-04 hero text follow-up: native `HERO_ABILITY` fields use texture IDs
for the HUD icon and `text.big` IDs for the name, level descriptions, and
maxed-out description. `HERO_STATS.StatLevels` has a bounded nested array of
ability descriptions and final-level text IDs. Forge resolves these symbolic
targets from the baseline English `text.big`, checks the array counts and
field tags, and refuses a missing target. The archived `EXP And Hero Mod.fmp`
resolves 217 named links in direct apply; its direct and one-mod merged
`game.bin` and `names.bin` files match byte-for-byte. The full eligible scan
at `build/named-fmp-coverage-ry3g5gyz/result.json` accepts 375/577 attempted
small-bank packages, 15 more than before, with 183 link refusals remaining.
The 244 large-bank packages remain deferred. These results do not establish
gameplay compatibility.

2026-10-04 new-text follow-up: a linked FMP `Text` entry can now add a new
symbol. Direct apply plans its ID from the English baseline, validates the
selected link targets in every available language, and serializes every
language before writing output banks. The composer prepares its text union in
scratch before linking definitions, so final TextIDs account for earlier
text-only folders, later FMPs and saved text picks. Selected links must resolve
to the same ID in each available language. The synthetic archived-hero
fixture in `tools/test_fmp_new_text_links.py` verifies direct ID 28914,
load-order-shifted ID 28915, exact four-byte hero-field relocation, text-only
source contribution, vanilla-pick refusal, malformed payload refusal before
output, two-language agreement and locale mismatch refusal. The updated full
small-bank scan at `build/named-fmp-coverage-b_evlyty/result.json` retains
375/577 accepted, 183 named-link refusals and 244 deferred large-bank packages.
These are offline bank checks; game behavior and authoring of a successor
package remain open.

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

`maintextures` now maps to `data/graphics/pc/textures.big/GBANK_MAIN_PC` by
entry name and numeric id. Replacements require the retail id and type; new
entries require an unused id. Payload and 34-byte texture header come from the
FMP; other entries retain their original payloads. In a mod stack, FMP entries
compose in load order over the selected whole-file texture bank. Disjoint edits
survive together; different edits to one entry are reported as texture conflicts
with the later winner. An FMP before a later whole-file bank is reported as
masked. The Mods conflict list lets users pick either contributing mod or the
install baseline for each contested texture. The saved choice uses
`texture:<entry name><TAB><mod name|vanilla>` in `forge_mods_picks.txt`; it
survives reordering. Without a choice, the later mod wins. `vanilla` restores
an existing entry from the selected install baseline or omits an entry added
only by mods. A saved choice for a mod that no longer contributes is reported
as unavailable and load order applies.

`tools/test_fmp_texture_layers.py` checked direct replacement from Young Hero
Retexture, four new entries from Blue Ass Beta Textures, disjoint Apprentice
plus Young Hero changes, both orders of a two-entry Apprentice conflict,
whole-file masking, earlier-winner and baseline picks across reorder, an
id-mismatch refusal, and unchanged source bytes. The
output BIG entries were read independently. `tools/audit_named_fmp_textures.py`
checks metadata for all 169 texture-capable packages without writing 169 copies
of a 533 MB bank: 165 have compatible identities (838 replacements, 34
additions in total); four claim a retail-occupied id for a new name and must
refuse. These 165 are metadata-compatible candidates, not 165 completed builds
or gameplay tests. When an earlier EgoCore `.resource` and a later FMP target
the same texture, the current composer refuses that mixed order instead of
silently giving the EgoCore layer the last write; the scratch test covers this
and preserves prior output. A scratch GUI automation check confirms the
texture rows and saved winner control. Full mixed-source ordering remains open;
an EgoCore `.resource` targeting an entry with a saved FMP texture choice
currently refuses composition.

The `graphics` FMP bank now maps to `data/graphics/graphics.big/MBANK_ALLMESHES`.
Existing meshes retain their numeric id and type; new mesh names require an
unused id. Forge preserves the package's mesh payload and Info subheader, composes
disjoint entries, reports same-mesh conflicts, and masks FMP entries before a
later whole-file graphics bank. The Mods conflict list and
`mesh:<entry><TAB><mod name|vanilla>` saved picks select a contributing mesh or
the install baseline. `vanilla` omits a mesh added only by mods. Graphics link
metadata is decoded as named texture references; the target name must exist in
the composed main texture bank and its numeric id must appear in the mesh Info
header. An earlier EgoCore `.resource` targeting the
same later FMP mesh is refused until mixed-source ordering is implemented.

`tools/audit_named_fmp_graphics.py` inspected all 314 named packages with a
`graphics` bank against this install without writing full graphics banks. It
found 261 with compatible mesh name/id/type identities (136 replacement and
786 added entries across the whole set); 53 have mesh collisions or malformed
metadata. Of those 261, 186 also have available named textures with compatible
texture identities. Seventeen name textures absent from both the package and
install, and 61 carry texture id/type collisions (three are in both groups).
All 3,180 graphics links
decoded: 2,675 base, 360 specular and 145 bump texture references. The report
is `build/named-fmp-graphics-audit.json`. These are metadata candidates, not
186 completed builds or gameplay checks.

`tools/test_fmp_graphics_layers.py` independently walked output BIG entries
after direct apply (including Hollow Wip's mesh plus new texture), disjoint
composition, both orders of the Half Mask/Lava JOB
Mask conflict, an earlier-winner pick, a baseline pick, and removal of a new
mesh via `vanilla`. It checked a missing texture refusal, occupied-id refusal,
untouched output on refusal and unchanged source hashes. The scratch Mods GUI
check in `tools/test_fmp_graphics_picks_ui.py` shows the named mesh conflict and
persists its winner. New `OBJECT_*` definitions still need a compatible mesh
and other referenced assets; in-game rendering remains unverified.

Named game link inspection now preserves complete target lists through
`forge-tools fmp links <package> --groups --json`. The read-only corpus audit
`tools/audit_named_fmp_links.py` decoded metadata from all 897 packages carrying
game or object links. It counted 345,785 single-target groups and 17,007 groups
with two to six targets across 219 packages. The writer refuses unrecognized
multi-target fields until each field's byte layout is established; for example,
an augmentation effect list and a flourish effect list have distinct layouts.
The audit is `build/named-fmp-link-audit.json`.

The `CEngineGraphic.Model ID` named link now resolves against the planned
`MBANK_ALLMESHES` entries when applying or merging FMPs. It changes only the
four-byte model id inside the field. `tools/test_fmp_named_mesh_refs.py` merged
the archived `Huge Mushroom.fmp` into a scratch install and independently
checked all seven `OBJECT_*` fields against the seven exact mesh entries in the
output BIG. No new missing assets were reported, and the source archive and
install files were unchanged. `tools/test_fmp_named_object_ui.py` then deployed
the same package into a separate scratch install through the Mods panel. Forge's
Models browser listed the new mesh with `OBJECT_HUGE_SHROOM_01_BIT_01` as its
user and displayed its textured preview. The editor placed that object in a
scratch map and showed its selected geometry in the viewport. Undeploy removed
the new model from the browser and restored the original banks byte for byte.
Screenshots are retained under `build/fmp-named-object-ui-2ysxv1z6/` in this
checkout. This verifies Forge's editor path for this package; the game and
other archived packages have not been checked.

One verified exception to the multi-target refusal is the two repeated
`MaleTeacher` controls in `CVillagePeopleDef`. The archived `Knothole Glade
Children.fmp` records both names under one `VillagePeopleID` path; the linker
keeps their order and addresses the two distinct four-byte fields. It also
handles that package's nested `CREATURE` primary/secondary weapon and brain
links after validating the complete local tag sequence. The same guarded
layout now resolves the torso texture, body/eye meshes and combat IDs through
their composed banks. A 17-byte `Graphic` field in the village record also
resolves its named marker mesh. The unit test checks that only those words
change and rejects a missing texture map or malformed nested tag.

`tools/test_fmp_knothole_children.py` merged the original named package against
the installed TLC baseline in scratch space: four definitions applied, three
added, no conflicts and no newly missing mesh assets. It independently read
the resulting game definitions and checked both ordered village targets, the
male/female child entries, all 18 named creature references and the village
marker against the output game/asset identities. The source archive and install
banks were unchanged. The scratch GUI test `tools/test_fmp_knothole_ui.py`
deploys the package,
places both new children and confirms each has one selected viewport mesh with
its named texture list. Undeploy restores the original banks byte for byte.
The editor shows skinned LOD0 in bind pose; animation and gameplay are still
unverified. Other creature layouts and texture/mesh winner picks need separate
coverage.
