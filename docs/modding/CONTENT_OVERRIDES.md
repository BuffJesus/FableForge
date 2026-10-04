# Fine-grained mod overrides: investigation proposal

Recorded 2026-10-02 at the user's request for future FableForge/mod-manager work.
Status: **broader design backlog; comparison increments are recorded below,
not a claim of complete override UI or format support**.
Related: [Mod packs](MOD_PACKS.md), [Load order](LOAD_ORDER.md),
[FMP formats](FMP_FORMAT.md), [0.20 roadmap](../ROADMAP_1.0.md).

## User problem and intended behavior

A user with many installed mods should be able to keep Aeon Edition's world edits,
another mod's combat balance, and an FMP's weapon appearance without moving an
entire overhaul above or below every other mod. Whole-mod order provides defaults;
explicit choices select contributions inside those mods and survive unrelated
load-order changes. FableForge constructs the merged game files at build time.

Example: inspect Sword of Aeons and choose damage from one mod, augmentations from
another, and a third mod's model/texture. The UI must distinguish an unchanged
base value, a mod's actual edit, an identical edit, and a conflicting edit. It
must show the source of the resulting value and explain why that source wins.

## Existing foundation, checked in this checkout

- `gui/mods.cpp`: conflict rows, `modPick`/`setModPick`, and persisted choices in
  `forge_mods_picks.txt`; the default winner follows mod order. Several record,
  placed-thing, quest and string families have picks. Lip-sync rows currently
  instruct users to reorder packs instead.
- The definition pipeline already supports schema-based field merging. Do not
  confuse automatic field merging with a complete per-field comparison/picking UI.
- `forge-tools` accepts several mod source forms, but FMP normalization still has
  format/installation gaps. Broader browsing must expose unsupported content
  instead of presenting partially imported mods as complete.

Extend the existing composer and conflict choices. Do not add a second installer
or invent a runtime plugin load order that the retail game does not provide.

## Proposed user workflow

1. Keep the ordinary mod list for add/enable/order/deploy.
2. Add a searchable content/conflict view: records, assets, strings, placed things
   and supported script/quest contributions. Search by readable name and internal
   identity; display container and originating mod to disambiguate duplicates.
3. Select an item to compare baseline, each contributor and the final result.
   Show actual changed values or asset previews where parsers support them.
4. Choose a contributor or baseline, exclude a contribution, or return to normal
   load-order behavior. Explain the scope before applying a choice.
5. Save choices as a profile-specific compatibility patch/recipe, validate it,
   preview the result, then use the existing reversible deployment workflow.

For large orders, support filters for unresolved conflicts, explicit choices,
mod, content family and affected item. Bulk choices should apply to a previewed
selection of identities, with undo and a count of affected/dependent items.
Avoid requiring users to hand-order thousands of internal records.

## Choice granularity and precedence

| Scope | Example | Required behavior |
|---|---|---|
| Mod | Aeon before weapon pack | Existing default order |
| Component | Keep weapons; exclude appearance edits | Author-declared groups first; inferred groups labelled as such |
| Record/asset | Choose a weapon, mesh, texture, string or placed thing | Exact, container-qualified identity and source |
| Field | Damage from A; price from B | Only decoded, supported fields; preview the composed record |

Proposed precedence: normal order, component policies, explicit record choices,
then explicit field choices. An explicit whole-record replacement and child-field
choices need an explained rule and visible resulting record. Broad rules should
not silently override more specific picks. Excluding one mod's contribution lets
the next eligible source win; restoring baseline is a separate choice.

Persist stable identities, mod/source fingerprints, baseline identity and schema
version. Renaming an FMP file must not break choices. On mod removal/update,
missing targets or changed selected content, mark affected choices for review;
never silently bind them to another record or contributor. Preserve profile
choices on rebuild and support exporting a portable recipe without bundling mods.

## Normalizing FMPs and large replacement mods

Use one contribution model regardless of source packaging:

- FMP: resolve record identities and dependency/link metadata, retaining source
  provenance and a report of unsupported or unresolved contributions.
- Whole-file overhaul (Aeon, Lost Content, etc.): diff against a verified matching
  clean baseline. Include actual changes, additions and supported deletions;
  unchanged records bundled inside replacement files are not overrides.
- Existing modified installation without source packages: capture its diff as an
  **Existing installation** layer. This preserves current content but generally
  cannot reconstruct the original mod attribution or installation order. Prefer
  rebuilding from original packages when available; do not guess provenance.

Record keys need container/bank/language/level context as applicable. Unnamed
records and cross-container IDs must be resolved using supported format evidence,
not a filename or unstable import index. Binary regions with no reliable parser
remain indivisible overrides with an explicit limitation.

## Dependency-aware composition

A weapon definition may require appearance/weapon subdefinitions, mesh, textures
and text. Offer the referenced assets with a choice, explain dependencies, and
validate the final references after relocation. Do not automatically drag in every
change from the source mod. Lists, linked definitions, scripts and world changes
can require coordinated choices; byte-valid or reference-valid composition is
not proof of compatible gameplay. Present unsupported combinations honestly.

The comparison view should distinguish additions, removals, reference changes,
identical edits and genuine conflicts. Detect incompatible field/layout changes
and use a whole-record fallback instead of splicing undecodable bytes.

## Useful evidence and fixtures

The user's 2019 fabletlcmod.com mirror was audited in the separate FableTLC
workspace: `D:/Documents/FableTLC/docs/journal/2026-10/FMP_CORPUS_2026-10-02.md`;
local database and originals: `work/fmp_corpus_20261002/` in that workspace.
Do not hard-code this path into Forge tooling or commit the binary collection.

The audit found 1,142 distinct source packages: 73 compressed wrappers, 132
packages with unnamed game records, many linked model/texture edits, and rare
script/frontend/STB contributions. 1,133 packages passed reading after necessary
wrapper normalization and semantic rewriting; that was not an install or gameplay
test. Nine layouts remain unsupported. Shared targets such as CREATURE_HERO and
GHOSTLY_SWORD provide overlap cases, not automatically proven conflicts.

Use this corpus together with clean baselines, small known mods and representative
overhauls to test mixed-source overrides. Readable share filenames are conveniences;
the original-byte hashes and provenance remain authoritative.

## Investigation sequence and acceptance evidence

1. Audit which existing picks and merge families actually support the proposed
   scopes; document gaps and reuse their identity/provenance representation.
2. Prototype a read-only item comparison with baseline, contributors and winner.
3. Extend persisted record choices, then supported field choices; preview and
   undo changes, export a profile recipe and report stale selections.
4. Add dependency validation and scratch builds; follow with component groups,
   bulk choices and performance work for large orders.

Acceptance should demonstrate: a mixed FMP/overhaul stack with deliberate
per-record and per-field winners; independent edits surviving composition;
unchanged bundled records not overriding earlier edits; reliable identity for
unnamed records; required assets and relocated references resolving; unsupported
data visibly blocked or left indivisible; picks surviving rebuild/reorder but
being flagged after incompatible updates; no writes to source mods or baseline;
and successful deployment/undeployment on scratch copies. Include performance
measurements for large orders and targeted in-game checks for selected combinations.

Open decisions: extend picks versus a richer patch manifest; stable source IDs;
supported field families; component metadata; update/rebase behavior; deletion
semantics; dependency boundaries; and how much provenance can be recovered from
an already-modified installation.

## 2026-10-03 source audit: what existing choices actually mean

Read-only audit of feat/editor-ui-shell; these are implementation findings,
not completed mixed-stack acceptance tests. This audit describes 330d4b5 before
the comparison increments below; retain it as the starting implementation boundary.

- `tools/forge-cli/main.cpp::splitModsPicks` routes definition record names,
  level-qualified TNG thing keys, file-qualified QST keys, text/FSE and
  lowercased whole-file paths. It has no component or field-choice namespace.
  Definition keys are record names, without a bank qualifier or fingerprint.
  Winners are source labels; the file stores no content/baseline/schema hashes.
- `mergeDefs` skips mod records byte-identical to baseline, and tries schema
  merging for changed named records with aligned definition types. Its report
  already includes each residual `field_conflicts` entry's field name,
  contributors and winner. Cleanly combined records are omitted from conflict
  rows, so that report is not an inventory of all contributed content.
- `forge::defdecode::mergeFields` applies a record choice only to fields with
  differing edits from multiple contributors. Unique and identical field edits
  still merge automatically. Choosing vanilla therefore restores contested
  fields, rather than the entire record, when field merging succeeds. A named
  contributor that left a contested field at baseline does not win that field;
  the last actual changer wins. Undecodable records use whole-record choices.
- `gui/mods.cpp::collectConflicts` retains the record-level `field_merged` flag
  but drops the report's individual field rows. The UI currently offers one
  winner combo and caps rendering at 400 conflicts. It lacks field comparison,
  individual field choices, contributor/baseline values and stale-choice review.
  The current combo needs to explain the narrower scope of field-merged picks.
- Choice writes already use `detail::PendingBanks`, reread the current choices
  and preserve cached display state after a failed write. Reuse that transaction
  path. Existing locked-write/external-edit tests cover it; this audit did not
  rerun those tests or prove source identity stability.

The first useful increment is to retain and display the existing field conflict
rows, with the chosen scope stated beside the record choice. A richer comparison
will need decoded baseline/contributor/result values and provenance in the
composer report. Keep per-field persistence, component filtering and portable
profile recipes as separate implementation work; the current data does not
establish those behaviors.

## 2026-10-03 delivered comparison increments

Primary Forge now retains residual definition field conflicts and explains that
record choices resolve contested fields while independent edits still merge.
The expanded view shows the field type, baseline, actual contributors and current
winning value, including a selected mod's unchanged-field fallback and vanilla.
The composer JSON carries those values and numeric part identities for prefixes
and repeated unnamed fields. Non-ASCII/control string bytes use exact display
escapes so strict JSON can report retail records without guessing an encoding.

Conflict search covers names/identities, kinds, contributors and field names,
and Saved choices only filters explicit choices. Filtering happens before the
400-row cap, so later matching conflicts can be reached and chosen. Neither
filter edits choices; changing the destination resets the filters.
Field-aware badges count actual contested wins/losses, excluding independent
edits. A saved choice absent from a current conflict's contributors is now
identified for review while its displayed result follows the composer fallback;
the saved choice is preserved and can be found by its name or cleared in the UI.
Other saved choices also exposes selections whose targets are absent from the
latest conflict report, with search and individual clearing. Such selections
can still affect builds: a single-contributor file baseline choice can suppress
that file without creating a conflict row. This review does not infer missing
source identities or fingerprint validity. CDefString values display signed
name-table references; only inline CCharString bytes use text/byte escapes.

Implementation through 169608b was reviewed in an isolated owning checkout:
synthetic three-mod banks independently verify contributor/result values,
vanilla scope and preservation of independent edits; a real 401-file conflict
stack verifies search and choices beyond the display cap. Both GUI sizes pass
and source/baseline bytes stay unchanged. Retail Sword of Aeons now decodes as
valid JSON with its source bank hash preserved. Exact evidence and primary
integration checks live in [HANDOFF_WORLD_UI](../HANDOFF_WORLD_UI.md).

This is a residual-conflict comparison, not a complete contributed-content
inventory. Values are snapshots from the last check. Individual field choices,
bank-qualified/stable source identities, fingerprint-based stale-content review,
component policies, portable profiles and the mixed-FMP/overhaul acceptance
sequence remain backlog. The existing picks transaction/composer remain in use.

## 2026-10-03 changed-source choice audit

At primary checkpoint 27755ec, an independent tiny file-pack audit in the owning
choice-review checkout demonstrates the existing contract: choose PackA's
data/Misc/shared.txt, change that source from version 1 to version 2, then build.
The saved label selects the new bytes. The order/choice files remain byte-exact;
mods list still reports the SHA256 captured at registration, not a fresh hash.
build/source-choice-audit-n_jm3p5h/result.json retains both independently computed
folder hashes and before/after composer reports. No live/corpus files changed.

This is label-based selection, not evidence of fingerprint validation. The
registration hash supports source deduplication; buildSources reclassifies
current source contents and resolves paths without comparing that hash. Current
whole-file conflict rows show source labels and the winner but no content digest.
The choice file has no choose-time baseline/source-content snapshot, so existing
data cannot distinguish an update before the user chose from an update afterward.
Rehashing a whole source against registration would also flag unrelated changes;
the actual Expanded Chapters registration reads 2.49 GB and took over seven
minutes in the frozen gate. Avoid promising cheap, precise stale-choice review
from a whole-folder hash alone.

A bounded next increment is current whole-file comparison evidence (actual
contributor bytes/hashes and baseline identity) for contested files. This can
support a later choose-time content snapshot without hashing an entire overhaul
again. It must remain visibly a snapshot, preserve existing label-based choices,
and not claim automatic stale-content detection. Definition fields require
separate stable symbolic-reference/schema evidence; their raw relocated IDs are
not sufficient fingerprint identities. Per-field choices/profiles and validation
of changed selected content remain the broader backlog above.

## 2026-10-03 isolated raw-file comparison prototype

Contested whole-file rows in JSON reports now include file_content with a current
baseline snapshot and source versions (mod, present, bytes and SHA256). This is
raw source-file evidence: recipe/resource edits can change the built file later.
Only contested files in JSON reports are hashed; non-JSON composition and the
existing choice format/selection semantics stay in use. This does not store
choose-time fingerprints or detect stale choices automatically. Baseline means
the supplied composition root, not proof of a clean retail installation.

The Mods File versions tree displays these snapshots, marks the currently
selected source and updates that mark when choices change. It accepts older
reports without snapshots. Complete names, byte counts, digests and missing-file
messages wrap inside the card. Test automation now centres reveal in the actual
clipped region and dumps that clipping rectangle, so screenshots can verify
visibility rather than only screen bounds.

Owned review evidence: build/mod-file-content-6vbo30q0 passes normal/compact
real controls, independently computed hashes, an absent baseline, identical
contributor bytes, same-size source refresh, preserved picks and exact output
bytes. Compact source names/hashes were visually inspected. Existing field,
long-value, unreported-choice and dialogue workflows all pass in
build/file-content-regressions-thcoz76f. One local 128 MiB source-file comparison
in build/file-content-perf-jthne9fm took 0.297 s before and 0.740 s with snapshots;
this warmed-cache sample is not a large-corpus performance guarantee.
The source/package gates register the new workflow (39 package runtime checks).
The 7b1c043 package passes all 39 extracted runtime workflows without skips;
55e20dc is promoted and rebuilt in primary with focused file/named-link checks.
Exact evidence is in HANDOFF_WORLD_UI. No new whole-source-gate result or
gameplay compatibility claim.
