> Ported verbatim from FableForge-legacy `docs/MOD_PACKS.md` on 2026-09-19 as the design reference for milestone 0.20 (Mod packs v1) in `docs/ROADMAP_1.0.md`; the commands it names live in `forge-tools.exe` (`tools/forge-cli/main.cpp`; spelled `forge-tools` below, the legacy repo called that exe `forge`).

# Mod packs, load order, and conflict resolution

The problem: big overhaul mods (e.g. Aeon Edition) replace whole files —
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
- **Overhauls (Aeon Edition, Fable: The Lost Content) touch** `game.bin`,
  `text.big`, `textures.big`/`graphics.big`, `script.bin`, and
  `FinalAlbion.wad`/level files — the full record set across every container.

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
