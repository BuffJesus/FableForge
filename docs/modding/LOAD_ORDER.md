> Ported verbatim from FableForge-legacy `docs/LOAD_ORDER.md` on 2026-09-19 as the design reference for milestone 0.20 (Mod packs v1) in `docs/ROADMAP_1.0.md`; the commands it names live in `forge-tools.exe` (`tools/forge-cli/main.cpp`; spelled `forge-tools` below, the legacy repo called that exe `forge`).

# Load order & conflict optimization — lessons from LOOT/Wrye Bash

The user asked how FableForge can take inspiration from Skyrim's load-order tools
(LOOT et al.). Short version: borrow LOOT's **graph-based ordering** and
**community masterlist**, borrow Wrye Bash's **record-level merge**, and exploit
one structural difference in Fable to do *better* than either.

## How the Skyrim ecosystem splits the problem

Three separate tools, three separate jobs:

- **LOOT** — decides *order*. The Creation Engine resolves conflicts at runtime by
  load order (last plugin wins per record), so all LOOT has to do is pick a good
  order. It can't cheaply read every plugin's records, so it uses a **masterlist**:
  a community-sourced, plugin-independent database of rules ("A must load after
  B", "A is incompatible with C", "A requires D", "A has dirty edits"). It builds
  a **directed graph** (plugin → plugin edges from master flags, requirements,
  load-after metadata), then **topologically sorts** it, reporting cycles,
  incompatibilities, and missing requirements.
- **Wrye Bash (Bashed Patch) / Mator Smash** — decide *merged content*. They read
  records and merge compatible changes from many mods into one patch (leveled
  lists, stats, etc.), so non-conflicting edits from different mods all survive
  instead of one winning the whole record.
- **MO2 / Vortex** — manage *files* (isolation, virtual file system, deploy).
  Vortex embeds LOOT's sorting; MO2 keeps mods in separate trees.

## The structural difference in Fable that changes everything

Skyrim has **runtime load-order arbitration** — you can reorder plugins and the
engine sorts out who wins each record. **Fable has none.** Mods are *baked into*
`game.bin`/WAD/`.big` at install time (Fable Explorer / `.fmp`). Whoever writes
the file last wins the *entire file*. There is no order the engine will honor
later.

Two consequences:

1. **Ordering alone is not enough.** In Skyrim, a good order + the engine = done.
   In Fable, FableForge must *construct* the merged result itself — the
   Wrye-Bash job is mandatory, not optional.
2. **FableForge doesn't need a masterlist to find conflicts.** LOOT relies on a
   masterlist because reading every plugin's records for ordering is too
   expensive at scale. FableForge already reads Fable's containers at the record
   level (`forge-tools defs diff`, `forge-tools wad diff`), so it can **compute** most
   conflicts directly from each mod's change set. The masterlist shrinks to only
   what data can't reveal (semantic requirements, "this mod's script assumes that
   mod's quest exists", known-bad combos).

## What FableForge borrows, and what it improves

| From | Idea | FableForge |
|------|------|-----------|
| LOOT | Directed-graph + topological sort for order | Apply-order graph over TRUE record conflicts + masterlist rules + user pins; report cycles/incompatibilities |
| LOOT | Community masterlist, plugin-independent | A versioned, community-editable Fable masterlist for the *semantic* rules data can't compute (requires / incompatible / apply-after / dirty) |
| LOOT | Health warnings during sort | Warn on missing requirements, incompatibilities, version mismatch, dirty edits at build time |
| Wrye Bash | Record-level merge into one patch | Merge non-conflicting record/field edits across all mods → merged game.bin/WAD/script.bin; order only decides same-record conflicts |
| Mator Smash | General record conflict resolution | Field-level resolution via `def_schema.json` (two mods editing different fields of one def both apply) |
| MO2/Vortex | Mod isolation + reversible deploy | Isolated packages + `forge-tools stage`/`unstage` (`.forgebak`); base install stays pristine |

## The FableForge model

1. **Auto-compute conflicts from data.** Diff every enabled mod vs vanilla →
   change sets. Intersect → the exact conflict set, per record and (for defs) per
   field. No masterlist needed for the mechanical part.
2. **Order = a topological sort** (LOOT's algorithm) over: computed same-record
   conflicts, masterlist rules, and user pins. Cycles/incompatibilities are
   surfaced, not silently resolved.
3. **Build = merge, then bake.** Apply, per record/field, the winner over vanilla;
   emit merged containers via `forge-tools stage`. This is the step Skyrim delegates to
   the engine and Fable cannot.
4. **Masterlist supplements, never gates.** It carries only the semantic knowledge
   (requirements, known-bad combos, dirty-edit flags) that record diffing can't
   infer. Community-editable, versioned, shipped with FableForge.
5. **One tool, not three.** Because FableForge reads records, it fuses LOOT
   (detect + order) and Wrye Bash (merge) into a single pass — plus MO2-style
   reversible deploy.

## Concrete masterlist schema (adapted from LOOT, github.com/loot/loot)

LOOT is a C++ app over **libloot** (the sorting/metadata engine), plus helper
libs: `esplugin` (parse plugin headers), `libloadorder` (read/write the order),
`loot-condition-interpreter` (evaluate `condition:` strings). Its data is a
YAML **masterlist** (community, per-game) + **userlist** (local overrides). Per
plugin it stores: `group`, `after` (load-after), `req` (requirements), `inc`
(incompatibilities), `msg` (notes/warnings), `tag` (Bash Tags), `dirty`/`clean`
(ITM/UDR edit counts + which cleaning tool), `url`. Ordering is a topological
sort over a graph built from master-flags, masters, `groups` (each group loads
after named others; plugins belong to a group), and the `after`/`req` edges.

**What FableForge reuses (same YAML vocabulary — community already knows it):**

```yaml
groups:
  - name: overhaul        # coarse tiers, load earliest-first
  - name: content
    after: [overhaul]
  - name: tweaks
    after: [content]
mods:
  - name: 'Aeon Edition'
    group: overhaul
    inc: ['Fable Rebalanced']       # hard incompatibility -> error
    msg:
      - type: say
        content: 'Total overhaul; put content/tweak mods after it.'
  - name: 'Lost Content'
    group: content
    after: ['Aeon Edition']         # soft ordering edge
    req: []                          # required deps (missing -> warn)
```

**What FableForge DROPS from LOOT's schema** (doesn't apply): `tag` (Bash Tags —
Fable has no plugins), `dirty`/`clean` ITM/UDR counts (that's plugin-record
hygiene LOOT can't compute; FableForge computes real record conflicts directly),
`esplugin`/`libloadorder` (no plugin/loadorder files — mods are file overlays or
`.fmp`). Kept: `group`/`after`/`req`/`inc`/`msg`/`url` — the *semantic* metadata.

**Why the masterlist is SMALLER for FableForge:** LOOT needs a big masterlist
because it can't read plugin records to find conflicts, so every relationship is
hand-curated. FableForge *computes* conflicts from `forge-tools defs diff` /
`tng conflicts`, so the masterlist only carries what data can't reveal: intent
(groups/order), hard incompatibilities, required deps, and human notes.

**Portable pieces:** libloot's topological-sort-with-groups algorithm maps
directly to FableForge's apply-order; the YAML metadata format lets a
community-maintained Fable masterlist look and feel like a LOOT masterlist. The
condition-interpreter idea (`condition:` gates a rule on e.g. a file existing)
is worth keeping for "this rule applies only if mod X is present".

## Build order
1. `forge-tools defs diff` / `forge-tools wad diff` — change sets. **Done.**
2. Change-set intersection → conflict report (record + field level).
3. Apply-order graph + topological sort (LOOT's core), with user pins.
4. Merge/bake → `forge-tools stage`.
5. Masterlist schema + loader (semantic rules); community-updatable.
6. GUI: conflict/order panel (LOOT's list + xEdit's conflict view, Fable-native).
