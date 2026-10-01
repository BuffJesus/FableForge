> Based on FableForge-legacy `docs/MOD_PACKS.md`, ported on 2026-09-19 as the design reference for milestone 0.20 (Mod packs v1) in `docs/ROADMAP_1.0.md`. Current Forge additions are documented below. The commands live in `forge-tools.exe` (`tools/forge-cli/main.cpp`; spelled `forge-tools` below, while the legacy repo called that exe `forge`).

# Mod packs, load order, and conflict resolution

EgoCore text compilation uses a separate temporary workspace per normalization,
including concurrent builds of the same mod into different outputs. Workspaces
are cleaned on success and compiler failure. This does not enable concurrent
writes to the same output or install. If a mod contains Data/Defs, unavailable
text sources or compiler failure stop build, deploy and conflict checks with an
error before emitting the incomplete mod. Existing build output stays untouched.
Mods without Data/Defs can still supply a DLL-only layer. Redeploy continues to
revert the earlier stage before building; failure can therefore leave the install
at its baseline with the previous mod removed.

## Recipe asset storage and failed edits

New model and ground-theme recipes keep their input files under
`assets/models/<NAME>/<role>/` and `assets/themes/<NAME>/<role>/`. For example,
a model and its texture use separate `model` and `texture` folders; a theme's
images use `base` and `cliff`. Files with the same basename can therefore belong
to different recipes or roles without replacing one another. Existing flat
`assets/` paths remain supported, and replacing a recipe preserves other recipes.

For `.gltf` models, recipe creation also copies external geometry buffers into
`model/buffers/<index>/` and adjusts their URIs in the packed JSON. Buffer bytes
and unrelated JSON fields are preserved; the source JSON is untouched. Embedded
data buffers remain embedded. External material images are not collected: use
the recipe's separate diffuse texture input. External buffers in `.glb` files
and percent-encoded file URIs are not covered by this packaging step.

Recipe adds stage their asset copies and manifest before replacing files. Missing
inputs and reported commit failures preserve the previous pack files; if rollback
itself fails, the error names retained recovery files. Manifest saves report write
failures. This recovery does not cover process termination or power loss. Adding
a recipe still requires Mods > Deploy before it affects game banks.

World/level capture also stages all changed world files, loose levels and static
map chunk/record pairs before replacing pack files. Read failures and reported
commit failures preserve the previous pack; failed reports list no applied
layers. This uses the same rollback boundary, excluding abrupt termination.
When a captured world file, level or static-map chunk matches the base again,
its previous pack override is removed in that commit. The capture report names
removed overrides. Files absent from the shadow leave existing pack entries alone;
unrelated recipe assets stay in place. The destination must differ from both
source directories.

Terrain writes use a snapshot of the draft taken when the write starts. Later
strokes stay unsaved. Each pack bake uses the source install's terrain and STB
together, so writing the pack again retains the full sculpt, foliage adjustment
and painted layers. The source install is unchanged; Mods > Deploy applies the
result. Stock WAD and extracted-level source layouts are supported.

## Lip sync recipes in Forge packs

Assets > Dialogue can save staged frame edits to a selected Forge pack. The
pack's `forge_pack.json` stores a `lipSync` array. Each entry identifies one
`language`, exact `bank`, and `soundId`, followed by `fps`, `durationBits`, a
viseme `dictionary` (`id` and `symbol`), and `frames` of `[id, weight]` pairs.
For example, `LIPSYNC_ENGLISH_MAIN` and Sound ID 2 identify one line in
`data/lang/English/dialogue.big`.

`mods build` applies each enabled pack's lip sync recipes to the current output
archive in load order. A later whole-file `dialogue.big` overrides earlier
recipes; a later recipe overlays the whole-file archive. Different lines and
banks combine, and the later pack wins when two packs edit the same line.
The build report explains when a later whole-file archive skips earlier
recipes. `mods conflicts --json` lists differing recipes for the same
language, bank and Sound ID, with the active load-order winner. The Mods panel
shows that winner and pack badges; reorder packs to change it. The base game
archive starts the build when no whole-file layer
supplied one. Use Mods > Deploy
to build and stage the composed archive. A scratch `dialogue.big` export is also
available for inspecting the edited file outside the pack workflow.

The problem: big overhaul mods (e.g.
[Fable: Aeon Edition](https://www.nexusmods.com/fablethelostchapters/mods/454)
by Alexander The Alright) replace whole files —
`game.bin`, `script.bin`, `text.big`, `graphics.big`, WADs. Stacking several
packs with naive file-copy means the last install wins the *entire file* and
silently discards every other mod's changes, even when they edited unrelated
records. FableForge's job is to let users combine packs the way xEdit/Wrye Bash
let Bethesda players combine plugins: isolated mods, explicit load order,
record-level conflict detection, record-level merging, and reversible deploy.

## What the real ecosystem actually does (researched 2026-07-19)

Grounding this in how Fable mods are really distributed and installed today:

- **`.fmp` (Fable Mod Package) is the community's distribution format.** Loaded by
  **Fable Explorer** (and the **ShadowNet** fork): File → Load Fable Mod Package →
  Actions → Save Mods and Run Fable. An `.fmp` is a set of **record-level edits**
  into `game.bin`/`*.big`/WAD entries — NOT (usually) whole-file replacement.
- **Two install methods, and their compatibility rule is the whole story:**
  - *Method 1 — direct file replacement* (drop a modified `game.bin`/WAD in):
    **incompatible with other file-replacing mods** — last one wins the file.
  - *Method 2 — `.fmp`*: **compatible with most other `.fmp` mods**, because each
    edits specific entries. Caveat from the community: a big overhaul "changes
    many entries, so install it first before other `.fmp`s" — i.e. **manual load
    order**, order-sensitive, no automatic conflict detection.
- **ShadowNet Fable Explorer already has "fmp merging" built in** — the community
  independently arrived at record-level merge as the answer, but it's manual and
  limited (no field-level merge, no conflict report, no reversible deploy).
- **Overhauls (Aeon Edition, Fable: The Lost Content) can touch** `game.bin`,
  `text.big`, `textures.big`/`graphics.big`, `script.bin`, and level files.
  Aeon Edition 5.03 ships loose level files, `FinalAlbionNoBarriers.wad`,
  `FinalAlbion_RT.stb`, graphics and texture banks, but no `FinalAlbion.wad`;
  do not assume every overhaul replaces the retail WAD.

**Takeaways that shape FableForge:**
1. The `.fmp` record-delta model *is* the record-level approach — the ecosystem
   proved it works. FableForge should **read and write `.fmp`** so it consumes the
   entire existing mod library (Aeon et al.) as first-class input, and so its
   output installs through the tools people already use.
2. ShadowNet's fmp-merge is prior art to beat: FableForge adds automatic
   conflict detection, explicit load order (replacing "install the big one
   first"), field-level merge via `def_schema.json`, and reversible staging.
3. Whole-file-replacement mods (Method 1) can be *converted* to record deltas by
   diffing them against vanilla (`forge-tools defs diff`) — turning an incompatible mod
   into a mergeable one.

Sources: Fable community forums / GameFAQs (fmp install + compatibility rules),
fablegame.info modding guide, fabletlcmod.com (fmp format, Fable Explorer).

## Why FableForge can do this and a generic manager can't

MO2/Vortex see opaque files, so their strongest tool is file-level override +
priority. FableForge parses the formats at the **record** level:

| Container | Records | Record key |
|-----------|---------|-----------|
| game.bin / script.bin / frontend.bin | 14,761 / 611 / 810 definitions | def name |
| FinalAlbion.wad | 796 per-level TNG/LEV blobs | entry name |
| *.tng | things (per level) | UID |
| text.big / dialogue.big | localized strings | string key |
| quests.lua / FinalAlbion.qst | quest registrations | quest name |

Because a mod's change to `game.bin` is really "changed defs {A, B, C}", two
mods that touch disjoint defs are *auto-mergeable*; only same-record edits are
true conflicts. A file-copier cannot see that; FableForge can.

## Architecture

### 1. Mods are isolated packages, never direct writes
A mod pack is a self-contained folder/archive of overrides (loose files and/or
whole containers). FableForge never edits the base install in place — the base
is the immutable source, mods are layers.

### 2. Load order
An ordered, user-arranged list of enabled mods. Order decides the winner of any
*true* conflict (later wins, or user-pinned). This is the one control users
already understand from every other modding scene.

### 3. Record-level diff (the atom)
For each mod, diff each container it touches against the base → the mod's
**change set** (records added / removed / modified). A general `diff` command is this
primitive (planned for 0.20; today `forge-tools wad diff` and `forge-tools tng conflicts`
cover WADs and TNGs). A mod's footprint is the union of its change sets.

### 4. Conflict detection
Intersect change sets across enabled mods per container. For each shared record:
- different records touched → **auto-merge** (both apply).
- same record, identical bytes → harmless duplicate.
- same record, different bytes → **conflict** → load-order winner (or user pick),
  surfaced in a conflict view (records, mods involved, winner).

For structured records (game.bin defs) the diff goes to the FIELD level using
`def_schema.json` (**shipped**, `forge-tools defs merge --fields`): two mods editing
different *fields* of the same def are field-merged, with only same-field edits
conflicting. See "Status" below.

### 5. Compose / build
Produce the final containers by applying, per record, the winning mod's version
over the base — a merged `game.bin`, a repacked WAD, merged registries. Emit via
`forge-tools stage` so it's reversible (`.forgebak` + manifest) and the base install
stays pristine. Uninstalling a mod = drop it from the order and rebuild.

### 6. Registry-aware merge
`quests.lua` / `FinalAlbion.qst` / `FSE_Master.lua` are parsed as registries and
merged entry-by-entry (dedupe, preserve order) — not blind text-append (the FQT
approach, which can duplicate/orphan across mods).

## How this improves on FQT's DeploymentService

FQT deploys quests by writing Lua into the FSE folder and text-patching the
shared registries directly in the live install — great for authoring one's own
quests, but:

| FQT direct-write | FableForge mod-pack model |
|------------------|---------------------------|
| Mutates the real game/FSE files | Base stays immutable; mods are layers |
| No load order | Explicit, user-arranged order |
| No cross-mod conflict view | Record-level conflict detection |
| Last write wins whole file | Record/field-level merge |
| Blind text-append to registries | Structured registry merge |
| Manual, partial uninstall | Rebuild from order; clean removal |

FQT's deployer becomes one *source* of a mod pack (a quest pack), not the
installer of record.

## Status (2026-07-19)
The record-level core is **shipped and proven on two real 2GB overhauls**:
- `forge-tools defs diff` / `forge-tools wad diff` — record-level change sets. **Done.**
- `forge-tools defs merge <base> <out> <bin> <mod>...` — compose non-conflicting
  changes from N mods, resolve same-record conflicts by load order, write a
  drop-in overlay. **Done.** Aeon Edition + Lost Content → 3,136 changes applied,
  2,503 new records, 53 conflicts; merged bin contains both mods' unique content
  and round-trips clean over 17,264 entries.

Also shipped: `forge-tools mods analyze` — read-only cross-mod conflict report over all
def bins (inspect-before-merge).

**Field-level def merge is now shipped** (`forge-tools defs merge ... --fields
<schema.json>`, task #12): the game.bin per-field serialization was cracked
(each field = `[4-byte CRC(name) seed-0 tag][value]`; FINDINGS.md "game.bin FIELD
ENCODING FULLY CRACKED"), so `forge::defdecode` splits a def payload into named
fields and composes two mods PER FIELD — a field only one mod changed (or both
changed identically) auto-merges; only same-field/differing edits are true
conflicts (load order / `--picks`). On Aeon + Lost Content: **53 record conflicts
→ 39 field-merged** (15 fields auto-merged that whole-record merge would have
dropped, 30 residual field conflicts) + 14 safe whole-record fallbacks.
Coverage: 196 of 249 game.bin def types resolve to a schema today (`resolveType`
maps `CREATURE`→`CCreatureDef` etc.); the 53 unresolved (OBJECT/UI/BUILDING/…)
need their base-class `Transfer` added to `def_schema.json` — see
`FableTLC/docs/DEF_SCHEMA_COVERAGE.md`.

Measured on three real overhauls (Aeon + Lost Content + Modpack), per container:
- **Defs** (record-level, shipped): 4,076 changes, **242 conflicts (~94% auto-merge)**.
- **TNG** placed things: 401 of 571 shared, **182 conflicting whole-file** — but
  most differ in only a few things → thing-level merge (task #18) auto-resolves
  the majority. This is the FableForge value-add over whole-file managers.
- **LEV** terrain grids: 401 shared, **394 conflicting** — hardest; whole-file
  load-order is the realistic baseline (grid merge is a later refinement).

This is the empirical case for record/thing-level merge: whole-file managers see
576/802 shared level files as conflicts; record-level sees the defs at 94%
mergeable and TNG mostly mergeable.

**Unified ingest + merge shipped.** Every mod format now normalizes into the one
field-level pipeline:
- `.fmp` (#13, **done**): `forge-tools fmp list/apply/extract` — an `.fmp` is a BIG
  archive (`forge::big`); `fmp apply` turns it into a drop-in game-root.
- bsdiff `.patch` (**done**): `forge-tools patch apply` (`forge::bunzip`+`forge::bspatch`)
  produces a modified game.bin from a `game.bin.patch`.
- `forge-tools mods merge <base> <out> --with <dir/.fmp/.patch>... [--fields] [--stage]`
  (**done**): normalizes each source to a game-root, then in ONE pass runs the
  field-level game.bin merge **and** the loose level-TNG thing-merge (per level:
  1 editor → copy; ≥2 → thing-merge by UID). Proven: HalsSword.fmp + Unofficial
  Patch game.bin.patch + Modpack overlay = 836 changes / 591 new records + a
  cross-format field conflict (load-order resolved), 15,352 entries, round-trips
  clean; Aeon + Lost Content also thing-merged BanditCampBoss.tng (308 → 325
  things, 32 conflicts).
- `.fmp` **export** (**done**): `forge-tools fmp export <base> <modded> <out.fmp>` writes
  a game.bin diff as an .fmp via the `forge::big` writer (both samples re-serialize
  byte-exact; export → apply round-trips to 0 diff).

Remaining: `.fmp`/`.patch` in the TNG merge (their level data is in a WAD bank —
extract-then-merge), `.fmp` GameBINLinkMetaData / names.bin fixups for playable
single-`.fmp` installs + Fable-Explorer-parity export, script/frontend BIN in
`fmp apply` (shared names.bin), LEV/text merge, wider field-merge coverage (more
def schemas — `DEF_SCHEMA_COVERAGE.md`), `.patch` *export* (bsdiff create needs a
bzip2 encoder), masterlist, GUI.

## Build order (each rides existing forgecore readers/writers)
1. `forge-tools defs diff <root-a> <root-b> [container]` — record-level change set
   (game.bin/script.bin **done**; WAD **done**; then TNG/text). **Foundational.**
2. **`.fmp` reader/writer** (`forge-tools fmp list/apply/export`) — consume the existing
   community mod library and emit installs the current tools understand. Format
   from fabletlcmod.com; Fable Explorer / ShadowNet are the reference impls.
3. Mod-pack manifest + load-order file (`forge-tools mods list/add/order`).
4. Conflict scan: change-set intersection across the order → conflict report.
5. Compose: merged containers → `forge-tools stage`. Field-level merge for defs via
   `def_schema.json` (two mods editing different fields of one def both apply).
6. Registry-aware merge for quests.lua / FinalAlbion.qst.
7. Convert Method-1 whole-file mods to deltas by diffing vs vanilla.
8. GUI: a conflict/load-order view (the xEdit "conflict" panel, Fable-native).

## 2026-09-20 status (FableForge branch `modpacks`)

`forge-tools mods list/add/remove/move/enable/disable/build` keep the order in
`<game-root>/forge_mods.json` (name, kind, source, sha256, enabled, note) and `mods build`
composes the enabled packs onto the retail baseline. Pack shapes handled: ChocolateBox `.fmp`
(contentType 510) and Fable Explorer `.fmp` (459), bsdiff `.patch` (against the pristine bytes:
`<file>.retail-bak` is tried when the install's file is a re-save), a game-root tree (records
merged, loose TNG/QST merged, every other file a whole-file layer), an EgoCore `Mods/<Name>/`
folder (DLL registered in `Mods.ini`, `.def` text compiled with `defc` into a field-level
layer), `text.big` key union, WAD repack of merged levels, and the GUI Mods tab (load order, deploy,
undeploy, conflicts through `forge-tools.exe`), one JSON conflict report over every stage with
per-row picks (`forge_mods_picks.txt`; the Mods tab's Conflicts card), EgoCore `.resource` bank
overrides (`Data/<path>/<bank>.big/[<SubBank>/]<Entry>.resource` [+ `.header`]: entry-level layers
over the banks, replaced or appended with the next id, applied in load order after the whole-file
layers so a whole bank a tree ships is their base), and thing provenance (`forge_mods_provenance.json`
from the build; the editor's badges / filter / *Back to retail*). GB packs (Project Seasons, 2026-09-20,
`tools/test_gbpack.py`): the parked `_FinalAlbion.wad` and a shipped `userst.ini` are not layers,
whole files identical to the install are skipped, and changed levels are repacked into the real WAD
(423 entries, 75 changed TNGs with provenance in the current baseline-aware test),
the new `ProjectAutumn/` folder and the whole STB / banks ride as
whole-file layers; UFP's bsdiff underneath composes without a conflict. FSE packs: `FSE/quests.lua`
is a key-level union (own bytes per entry, `fse:<key>` picks, id clashes reported); an FSE-only folder is a
tree source. EgoCore partial TNG mods (`[Settings]` + `Replace=true` / `DeleteUIDs:`, TngMerger.h) merge by
UID into the level, deletions applied, even as the only editor.

Stress case from the plan's matrix (2026-09-20): Ultimate Spell Pack (18 fmps) + mfvicli Mods (35 fmps)
on the same spell defs, `mods build --fields`: 54 mods, 897 changes, 605 records composed per field,
640 fields auto-merged, 35 same-field conflicts decided by load order, 177 whole-record (add-add of
identical new records), 8.5 s. `mods deploy` / `mods undeploy` round-trip a scratch root byte-identical.
GB-pack case: Unofficial Fable Patch (bsdiff) under Aeon Edition (a whole `Data/` tree: game.bin,
text.big, 421 loose LEV/TNG, WLD/BWD): 1,391 record changes (853 new), 5 records both touch (Aeon wins
by order), 791 strings, 392 levels, 436 whole-file layers, 3.6 s -- the bsdiff still lands underneath
the whole-file game.bin instead of failing on it.

Aeon Edition + Controller Support (2026-09-30): the author-provided install
sequence is Aeon first, Controller Support `.fmp` through a legacy package
editor, then only `FableControllerSupport.dll` in
`Mods/FableControllerSupport/`, enabled through EgoCore. EgoCore's optional WAD
decompile step should be skipped for this combination. Forge's equivalent
scratch order is the Aeon tree, `ControllerSupport.fmp`, then a DLL-only EgoCore
folder. `python tools/test_aeon_controller.py` builds this order over retail:
1,392 definition changes (854 added), two records merged per field, zero field
or whole-record conflicts; the controller scheme is Aeon's 70 bindings before
the package and 141 after it. The DLL is byte-identical in output and registered
in `Mods.ini`. With retail `FinalAlbion.wad` in the scratch input, Forge repacks
547 changed level files and appends 47 new level files; the output WAD lists
`BarrowFields.tng`. The two extra replacements are Aeon's root-level
`Data/Levels/creature_hub.lev` and `.tng`: both are also present in retail's WAD,
so Forge must repack them at that path. The test extracts the rebuilt WAD and
compares all 843 Aeon level payloads against the effective build (594 loose
overrides and the unchanged WAD entries). This proves
the composed files and order, not runtime behavior on
Retroid/Android or under EgoCore. The test never deploys to the real install.
The test includes Aeon's `graphics.big`, `textures.big`, `FinalAlbion_RT.stb`
and `Bones` files from the original 5.03 archive. A read-only
`forge-tools assets missing-mesh <built-root> <schema.json>` audit finds zero
missing `Graphic` model IDs against Aeon's output graphics bank. Auditing those
same definitions against the retail graphics bank alone reports 243 absent IDs,
which shows why checking an incomplete Aeon extraction gives a false alarm.
This audit checks direct `Graphic` references in `game.bin`; it does not prove
that map scenery, scripted visibility, meshes or textures render correctly.

The Mods tab's **Check conflicts** dry run also checks these direct model
references before deploy. Its report uses the composed definitions and the
graphics bank the build will use (a mod's replacement bank, or the base bank),
then compares with the base install. The panel names only *newly introduced*
missing references, including the definition and mesh ID; existing base
defects are counted separately. A missing bank or unavailable schema is shown
as "model check unavailable", not as a clean result. The read-only
`forge-tools assets missing-mesh` command lists every broken direct reference
for deeper inspection. `tools/test_mod_asset_health.py` injects one broken
OBJECT into a scratch load order and checks both the JSON report and Mods UI.

A later Discord troubleshooting exchange (2026-09-24) explains why **DLL-only**
is part of that recipe: leaving `Mods/FableControllerSupport/Data/` in place
causes EgoCore to recompile the controller text definitions, potentially over
Aeon's installed definitions. The author advised reinstalling Aeon if that
already happened, applying the controller `.fmp`, then enabling just the DLL.
One user reported success with Aeon, UltraPlus, draw-distance and EgoCore
graphics patches in an install sequence that included Freeroam; another user
reported a stuck Guild dormitory chest and a Picnic Area crash even after
retrying. These are user reports, not a proven general compatibility guarantee.
The local corpus has a separate `Freeroam.fmp` (76 LEV entries); the user's
`D:/Downloads/freeroam.zip` contains only `FreeRoam.exe`. Decompilation of that
executable shows its patched extraction option sets walkability and camera
passability to 1 in every cell of every extracted LEV. The FMP carries 76 already
patched LEVs; for example, Witchwood_9 changes from 504/4,225 walkable cells in
retail to 4,225/4,225. Forge now imports an FMP's `FinalAlbionWAD` level entries
as normal content layers and repacks them into its output WAD. The scratch test
`python tools/test_freeroam.py` confirms all 76 FMP levels remain fully walkable
in the rebuilt WAD. The two artifacts have different scope: the executable can
patch every extracted LEV, while this FMP names only 76. The corpus has no UltraPlus, DrawDistance, starfield
or particle-limit pack, so the scratch test does not cover that larger stack.

Aeon's own 5.03 `readme.txt` says to run Freeroam first, copy every package folder
except Guide into the game's `data` folder, and start a new game. It calls
Freeroam technically optional if `FinalAlbion.wad` is renamed/deleted and
`UseLevelWAD` in `userst.ini` is set to `FALSE`. Forge instead repacks the loose
levels into an output `FinalAlbion.wad`; extraction and payload comparison prove
the archive contains the intended built level bytes. Forge also detects and edits
an install already converted to loose levels. A stock-WAD build has not been
shown gameplay-equivalent to the author's loose-level install; keep that as a
separate compatibility gate. Installing FreeRoam.exe is not required to use
Forge's WAD build route, but a stock WAD alone does not provide Freeroam's
unrestricted traversal. A map must contain patched walkability bytes, supplied
by a content pack, the FMP, or Forge terrain edits. Aeon 5.03 already has all 76
maps named by the FMP fully walkable; applying this FMP after Aeon would replace
Aeon's other edits to those LEVs, so do not stack it as a whole-file override.

AlbionSecrets Modpack 492 is a close derivative of Project Seasons 122: their
ZIP manifests share 915 file paths; CRCs differ for 22 shared files, and the
Modpack adds one lighting file. Both pass `python tools/test_gbpack.py` on a
scratch retail root under the Unofficial Fable Patch, as does their combined
load order (`--pack seasons`, `--pack modpack`, `--pack both`). The current
composer compares TNGs against the retail WAD baseline and omits unchanged
copies. Seasons alone has 75 changed TNGs / 423 repacked WAD entries; Modpack
alone has 85 / 433. Their combined build reports 839 definition changes
(592 new records), 85 changed TNGs, three thing merges and 16 thing conflicts
resolved by load order. All 433 built loose level payloads match the extracted
WAD. That confirms build completion and output structure, not that
both visual overhauls' intended appearances survive their overlapping whole
banks and maps.

Expanded Chapters v1 needs both its content RAR and its separate graphics RAR.
`python tools/test_expanded_chapters.py` builds the combined tree on scratch
retail. All 798 source LEV/TNG payloads resolve to the expected WAD bytes,
and all 583 built loose level payloads match WAD extraction. Forge replaces
579 entries and appends four (`ArenaNew_Leadout_01` and
`DemonDoor_GreatwoodGrannysHouseNew` LEV/TNG pairs). Its 838 definition
changes include 761 new records; zero direct Graphic mesh IDs are missing
against its own graphics bank. In-game quests and visuals remain unverified.

Fable: The Lost Content 0.7.5 has a current full-tree scratch build. Its
`python tools/test_lost_content.py` check finds 1,002 source LEV/TNG files;
all match the effective rebuilt WAD bytes. Forge replaces 498 entries and
appends 206, leaves the pack's parked `_FinalAlbion.wad` and `userst.ini` out
of the output, and reports zero missing direct `Graphic` mesh references
against the pack's graphics bank. `python tools/test_lost_content.py
--with-aeon` now checks the current Aeon-first combined build: 3,085
definition changes (2,503 new), 38 records composed per field, 36 field
conflicts and seven whole-record conflicts; 293 changed TNG levels, 79
thing-merged and 1,812 thing conflicts. Forge replaces 568 WAD entries,
appends 247, and all 815 built loose level payloads match the extracted WAD.
The final Lost Content graphics bank has zero missing direct `Graphic` mesh
IDs, but matching IDs do not prove matching mesh content or intended textures.
The conflict count and whole-bank winner make this a manual compatibility
decision, not an automatically compatible pair.

Dragon Cliff Restored V2 has eight new maps directly under `Data/Levels/`
(16 LEV/TNG files), plus Hook Coast edits and whole asset banks. Its scratch
test (`python tools/test_dragoncliff.py`) passes: two Hook Coast WAD entries
replaced, all 16 root entries appended, extracted bytes equal the built loose
files, and zero missing direct `Graphic` mesh references against its bank.
Its world, static-map and quest behavior still needs an in-game pass.

The separate Discord question about `Gameplay.wad` and missing buildings is
unresolved: neither inspected local Fable install contains a `Gameplay.wad`
(both have `FinalAlbion.wad`), and no WAD or screenshots were supplied. A WAD
name alone cannot establish retail playability or asset coverage. The author
reported a separate mod for a Guild building and absent assets in a village
map; those are case-specific observations, not a general Forge capability.

### Undeploy recovery

Undeploy validates every original backup before restoring any target. A missing
`.forgebak` for an original is an error and leaves files unchanged. Backups and
the manifest remain until all restores succeed, so a locked file can be released
and undeploy retried without losing originals restored earlier. Newly added
files are removed. Stage prepares all original backups and checks the recovery manifest before
changing targets. If a target copy fails, use undeploy to restore that attempt
before retrying. An unowned backup is refused rather than reused. This does not
provide power-loss durability or coordination between competing processes.

Mod merge, deploy, conflict checks and binary-patch validation use individually
owned temporary directories. Separate commands on different installs can build
without deleting each other's scratch files. Commands that write the same
install or explicit build output must still run one at a time.
