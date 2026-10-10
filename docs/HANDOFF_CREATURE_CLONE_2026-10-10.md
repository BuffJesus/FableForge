# Creature donor self-reference foundation, 2026-10-10

Owned C development checkout/branch unchanged; starting clean at d2678ed.
Previous turn is progress: recovered one Creature schema field, completed exact
source/extracted acceptance and committed/pushed. See
[schema handoff](HANDOFF_CREATURE_GROUP_2026-10-10.md).
RC2 and all existing development archives remain unchanged. No game/native writes.

## Acceptance question

The roadmap's distinct creature donor needs its self/owner references retargeted,
while shared component/appearance/body references must retain their existing indices.
Generic defedit::cloneEntry deliberately copies exact bytes and remains unchanged.
This foundation is reference analysis, not a completed creature-clone workflow.

Canonical core already supplies defrelocate, which rewrites only verified reference
slots and refuses candidates in unknown bytes. Reuse that instead of copying the
older value-wide replacement in FableTLC's DEFS.md. That historical experiment is
evidence for the self-index problem, not permission to rewrite every matching word.
The early APPEND_ALGORITHM.md also has superseded CRC text; use the later DEFS.md
crc0 contract, already implemented by the current paired publisher.

## Evidence and scope

Read-only evidence checkout D:/Documents/FableTLC:
`ghidra_out/def_transfers.c` CThingCreatureDef 00671600 calls CThingBaseDef 00676be0
before derived fields. FableWin donor exports show the same call at 01fb3ef8 via
01844d10 to 01fd5340; original-byte disassembly and executable hash are retained in
the preceding schema handoff. CREATURE is CThingCreatureDef, not the distinct
CCreatureDef component with its appearance/body prefix.

Current fmplinks::locate already describes CDefs as a five-byte header followed
by count × 12-byte {name CRC, game entry, inherited game entry} triples.
Independent raw retail checks now confirm all 517 Creature records have the
matching count/prefix boundary ending exactly at the Components tag. Only the
two game-index members are reference slots; the CRC/header and all other bytes
stay exact. The existing strict decoded/re-encoded equality check is retained.

defrelocate now recognizes CREATURE/CThingCreatureDef CDefs prefixes alongside
the already confirmed Object/Building owners. No other unknown field inference.
Malformed counts remain unresolved/refused. Synthetic owner cases exercise both
game-reference members, equal-valued CRC preservation and malformed counts.

New read-only test-executable mode `--creature-self-audit <root> <schema>` analyzes
each Creature donor mapping only its own native index to the hypothetical append
slot. `tools/audit_creature_self_refs.py` independently reconstructs every CDefs
slot from raw records and checks exact copied payload deltas for accepted analyses.
Native compiled hashes are retained and checked. Registered in source gate only;
the development package does not ship test executables.

Evidence: `creature-self-refs-tfiziza6` under the 20261010 C evidence root.
517 donors, 1,521 verified self slots; 509 relocation-eligible, eight unresolved.
This is not a claim that 509 clones are published or game-valid. Unresolved matches
fall outside the verified CDefs prefix, often on stride-nine Components rows.
Keep them refused until their domain is established; matching integers alone
do not distinguish a parameter from a definition reference.

## Next implementation

Affected relocation/namespace/FMP unit checks all pass in the isolated build;
Python syntax and whitespace checks pass. Picker head b49dcf2 Windows CI
38065042651 completed successfully; schema head d2678ed run 38065569483 is live.
Commit/push this foundation.
Then add a distinct creature-clone API using the existing canonical clone and
paired transaction: precompute verified self-only relocation before mutating;
preserve shared refs, original identities, raw pools and existing name offsets;
refuse unknown collisions/malformed donors before staging. Keep generic exact
clone semantics intact. Prove independent append/delta/Restore on synthetic and
retail copies before GUI promotion. Own appearance/body cloning is still part of
the roadmap, not completed by shared-component self-retargeting.

The dev/format-I/O/debug-evidence procedures selected the shared relocator,
native inheritance evidence and independent raw-byte audit. No new package,
full-suite, clean-machine or gameplay claim for this foundation.

## Distinct in-memory clone API continuation

Starting at committed/pushed e5d6e8e, defedit::cloneCreature now requires an explicit
native Creature donor index, clean exact decode/encode and a nonempty counted CDefs
prefix beginning before Components. It precomputes self-only defrelocate output,
so unknown candidates refuse before mutation; canonical clone validates name,
capacity and all existing ordinals before append. Only the appended payload gets
the moved data. Shared component/appearance/body entries, original payloads/slots
and loaded names remain unchanged. Generic exact clone is unchanged.

New synthetic CTest exercises both self-reference members, equal-valued CRC and
shared references, invalid indices/types/names, wrong counts, unknown matches,
empty listings, refusal non-mutation and unchanged generic cloning. Five targeted
clone/relocation/namespace/FMP CTests pass; final empty-list tightening rerun passes.
CTest inventory is now 56; no full-suite claim.

Optional test-executable retail mode writes only a fresh directory outside the
native install. It appends each accepted donor, verifies all prior records and
loaded names stay exact, saves and reopens every result. Independent Python gate
compares all 14,764 original records, exact identity rows/header, original raw pool
body, appended native ordinals/new-name crc0 and each self-only four-byte delta.
It verifies refusal counts against the input-hash-bound independent reference audit.
508 clones pass; eight unresolved donors plus empty NULLDEF_CREATURE refuse.
Donors whose CDefs are inherited with no own self slot keep those references exact.
This is a memory/container API check, not transactional publication or Restore.

Final combined source gate passes at `creature-self-refs-ydr_bbo3` and
`creature-clone-api-enor6zsk`; native input hashes unchanged. First API probe
`creature-clone-api-nia6lhpi` hit a Python assertion KeyError because the independent
reader omits ordinals. Corrected to read the raw identity table; subsequent
`creature-clone-api-weu1kk0z` and final gate pass. No core defect was inferred from
that harness failure. Source gate chains `audit_creature_self_refs.py --clone-api`
with its own verified report, avoiding fixed evidence-path dependencies.

Schema head d2678ed Windows CI 38065569483 passed; foundation head e5d6e8e
run 38065970643 is live. No new GUI package for this API-only continuation.
Next: commit/push, then expose this distinct mode through the paired definition
publisher/CLI and verify transactional backup/rollback/Restore before GUI promotion.
Own appearance/body cloning remains unfinished roadmap work, not replaced by
shared-component cloning. Project procedures required preservation and independent
raw-delta proof before promoting the in-memory API.

## Paired CLI publication continuation

Starting clean at committed/pushed 0e4eeaf. `defs clone-creature` requires an
explicit schema, game.bin source/output and the existing exact-name-or-native-index
selector. The shared pair publisher now has an optional explicit Creature donor
contract: recompute a verified clone from fresh source, compare its full appended
payload/type/ordinal, then use unchanged namespace/destination/offline/preparation/
rollback/recovery guards. Incomplete typed contracts refuse. Generic exact-clone
publication retains its exact-donor requirement. New GUI controls are not added yet.

Build of forge-tools/forge/FableForge passes (existing u8path warnings retained).
Five affected CTests pass. Final source CLI gate passes 30 selector/name/options,
self-only delta, empty/malformed/unknown donor, bank-scope, aliases, incompatible
pool/orphan, locked-rename rollback, fresh/existing/in-place and explicit Restore
cases: `creature-clone-cli-xfy96ui0`. Initial 22-case gate also passed at
`creature-clone-cli-n4ps4v9r`. Exact generic 35-case neighbor passes:
`definition-clone-7wsyj7g1`, including its owned hidden running-owner/junction helper.

Six read-only native-input cases pass: `creature-clone-retail-qh1ssvs7`.
Trader clones move exactly three self slots; inherited villager-base clones move
zero and preserve shared references. Fresh/existing paired outputs independently
preserve all 14,764 prior records/identity rows, original raw name-pool body,
new-name crc0 and native ordinal; Restore removes creations or restores exact
originals. Boss-bandit unresolved and empty NULLDEF refuse before output creation.
Native compiled hashes remain unchanged. Registered source and extracted gates;
the retail extracted gate explicitly uses the packaged schema path.

Exact checkpoint:
`dist/development/20261010-creature-clone-cli/FableForge-0.18.1-dev-win64.zip`
SHA256 `732db7d64d142e7f7f9138d26345aaf25c1a36d651269074bba8c43b4908fc92`.
Packaged at starting HEAD 0e4eeaf with scoped CLI/publisher/docs/tests dirty;
final notes updated afterward without replacing the tested archive.
`package-smoke-ptyqh3vy` passes 117 file identities, 141 local links and six
selected workflows: eight-map retail smoke, GUI smoke, all 35 generic CLI cases,
all 30 Creature CLI cases, six retail-copy Creature cases using packaged schema,
and all 44 existing GUI clone cases. 125 workflows not run; two unrelated missing
controller/Aeon and F2 corpus skips explicitly remain unexercised coverage.
No full-suite, clean-machine or gameplay claim.

Next: commit/push, then add a distinct guarded Creature GUI clone action using this
paired publisher. Own appearance/body cloning remains unfinished roadmap work;
generic exact GUI/CLI clones remain unchanged. Foundation head e5d6e8e CI
38065970643 passed; API head 0e4eeaf run 38066442857 remains live at this checkpoint.

## Distinct Creature GUI continuation

Starting at 20df28c, the game-bank CREATURE-only Clone creature action preflights
the strict core clone on a disposable copy before opening a draft. Draft mode
captures the donor/source/destination through existing clone guards. Save uses
the distinct core API and paired publisher contract; generic exact clones retain
their original payload contract. Both the draft and compact Create confirmation
state that components stay shared. Appearance/body cloning remains unfinished.

Source 36-case GUI gate passes at `creature-clone-ui-qrtdjqfc`: both-size actual
controls, numeric/indexed duplicate donors, folders/packs, Cancel/Close/Install,
stale bank/names, invalid names, malformed/unknown/empty donors and bank scope.
Retail GUI gates pass six cases each at `creature-clone-retail-3_4bx33n`
(800x600) and `creature-clone-retail-j06wlilk` (1440x900). Independent raw checks
preserve all prior records/table/pool bytes and only retarget verified self slots;
explicit-output Restore is exact and native compiled hashes remain unchanged.

After shortening the confirmation so shared-component wording remains visible
at 800x600, the GUI rebuilt successfully. Final compact retail gate passes at
`creature-clone-retail-tjf2twxu`; confirmation screenshot inspected. Focused
save/folder/Close review is retained at `creature-clone-ui-xsg8ss5h`.
Registered synthetic and both-size native GUI source/extracted workflows.
API CI 38066442857 and CLI CI 38067118481 passed. Extracted checkpoint is next;
no full-suite, clean-machine, gameplay or private appearance/body clone claim.

Focused final GUI gate passes all six cases at `creature-clone-ui-xsg8ss5h`.
Exact checkpoint `dist/development/20261010-creature-clone-gui/FableForge-0.18.1-dev-win64.zip`
SHA256 `51d135c1e95f4fb51cfe4a501f164a7064aa3c1577d4467c137c789a54ffca4e`.
Packaged at starting HEAD 20df28c with scoped GUI/docs/tests dirty; final evidence
notes updated afterward without replacing the tested archive. Extracted gate
`package-smoke-f7lkku3o` passes 117 identities, 141 links and eight workflows:
eight-map retail smoke, GUI smoke, 30 Creature CLI cases, six packaged-schema
retail CLI cases, 36 Creature GUI cases, six retail GUI cases at each size and
all 44 generic exact-clone GUI neighbors. 126 workflows not run; two unrelated
missing controller/Aeon and F2 corpus skips remain unexercised coverage.

Next: commit/push this GUI milestone, then recover a bounded private appearance/body
clone graph from verified CDefs ownership and native evidence. Do not rewrite all
numeric matches or silently clone shared gameplay components. RC2 and predecessor
archives remain unchanged; no full-suite, clean-machine or gameplay claim.

## Private visual graph evidence continuation

GUI head a04ba4c Windows CI 38068198733 passed. Current continuation is read-only
graph/relocation observation, not a private clone writer or eligibility claim.
`audit_creature_visual_graph.py` independently decodes native CDefs triples and
checks selected CCreatureDef/CAppearanceDef class targets, original-parent rows,
duplicate/missing rows and sharing. Synthetic malformed/range/type/duplicate,
inherited/shared and empty cases pass. Native `creature-visual-graph-c4db1bp_`
observes 517 Creature roots: 502 have both visual rows, fourteen have no appearance
row and NULLDEF has an empty listing. All observed visual targets match their
class keys; referenced original parents contain matching component rows.
CCreatureDef: 328 own/188 inherited rows, 261 shared rows. CAppearanceDef:
100 own/402 inherited rows, 442 shared rows. There are 428 unique visual components.
Sharing counts are Creature-root use observations, not a complete incoming-edge audit.

Bounded original-byte evidence `creature-cdefs-native-d4h9ut0j` records FableWin
SHA256 a9d6d0977d9d7fc8242da4292ae7ed92e926ca80364a95c003d9b9f689e41845
and matching FableWin PDB/layout identity. Native CParentDefClassBase save/load
030633f0/030634d0 writes/reads u16 count plus {u32 class CRC, i32 DefIndex,
i32 OriginalParentDefIndex}; base loader 03063850 consumes three flag bytes.
PDB CDefClassBase::CSubDefInfo is eight bytes with those two signed-long members
at +0/+4. GetSubDef<CCreatureDef> 01ac010e and appearance 01b207b4 call the
class-key lookup and resolve the first member through GetPDefFromGlobalIndex
030620e0. CompileSubDefinition 03060c70 compares OriginalParentDefIndex to the
current parent at 03060cf9..03060cff; a different parent takes instantiate/copy
before editing, equal parent reuses the existing component. This corroborates
copy-on-write ownership; private inherited visual rows need localized parent
reassignment, not remapping that old parent across shared gameplay components.
`probe_creature_cdefs_native.py` reproduces the bounded packet with optional local
pefile/capstone and the existing read-only PDB oracle; not a shipping dependency.
Earlier packet uco3u3aq lacked compiler/PDB capture; final d4h9ut0j includes both.
The preliminary Ego_r layout query was cross-checked with matching FableWin PDB.

Canonical optional `fableforge_defrelocate_tests --creature-visual-audit` analyzes
root/body/appearance maps in memory only. Combined independent gate passes at
`creature-visual-graph-bbhi1gb8`: all 502 three-node graphs round-trip all payloads;
2,435 verified root rewrites, zero component rewrites. 477 graphs have no unresolved
candidates; 25 have findings (eight root payloads, twenty appearance payloads,
with overlap). Raw checks verify every root CDefs rewrite and full payload deltas;
native bank hashes unchanged. Appearance examples hit long Animation payload
offsets, not necessarily model IDs. Unknown matches remain refused observations;
do not value-wide rewrite them or infer a reference domain from integer equality.
One affected relocation CTest passes; native observation gate registered.
No new GUI archive or native install copies for this analysis continuation.

Next: establish the unresolved appearance-field asset domains, then implement the
coordinated private root/body/appearance plan with explicit selected-row parent
updates and non-mutating refusal. Keep gameplay components and existing assets
shared until separately requested by the chosen import workflow. Still no private
visual writer, transaction, GUI or gameplay claim.

## Animation boundary recovery

Bounded packet `creature-animation-native-oo2dfwaa` uses the same hash-guarded
FableWin bytes and matching PDB. `probe_creature_cdefs_native.py --feature animation`
captures set/entry readers and writers, thirteen component reader/writer pairs,
allocation, Add, PostInit and rotation lookup. PDB confirms CAnimationEntry
BankIndex/AnimName/GroupName and CAnimationSet DefaultFlags/TransitionTime/Delay/
DefaultGroup. Matching PDB enum EAnimComponent is 0..12; queried with llvm-pdbutil
pretty --enums --include-types=^EAnimComponent$ and the DIA SDK on PATH.
Native Add 01fa0f8f takes AnimName.GetCRC for the map key. Rotation lookup
01fa0c78..01fa0c81 passes BankIndex to CMeshDataBank::PeekAnimation, not the
definition manager. Component 8/11 transfer CDefString; component 12 transfers a
u32-length inline byte string. Other component bodies use signed scalars, flags,
floats, booleans or a 2D vector; do not classify the whole field as one domain.

Independent `audit_creature_animation.py` synthetic truncation/count/unknown-type
cases pass. Native observations `creature-animation-audit-qcn0gzqq` decode all
320 appearance fields, 27,779 animation entries, zero layout failures; each ends
at the following OutlineEffectAlphaRef tag. Source bank hashes remain unchanged.
The 27 root-ID-equal windows across twenty affected root observations comprise
nineteen whole BankIndex fields in the separate animation asset namespace and
eight windows straddling name-offset/count or combo-value/component-type fields.
These are layout observations, not rewritten IDs or a clone eligibility claim.
Registered native/synthetic gates; no full-suite or new shipping archive claim.

Next: translate these verified primitive/reference boundaries into canonical
relocation coverage, testing name-offset remapping and strict malformed refusal.
Then implement the coordinated private visual plan with localized parent updates.

## Canonical animation relocation coverage

`defrelocate` now recognizes only CAppearanceDef.Animation/CAnimationSet. It
checks entry counts, all thirteen component shapes, duplicate/unknown selectors,
canonical booleans, inline string extents, defaults and exact end-of-value.
AnimName, GroupName, DefaultGroup and component 8/11 CDefStrings use the existing
NameOffset domain; CRC keys, animation-bank indices, flags, timings, enum/float
values and inline bytes stay exact. ComboStage/ComboID and RecoilAnimIndex have
verified primitive boundaries but whole matching integers remain refused until
their runtime domains are established. Nothing is inferred from integer equality.

Synthetic all-variant, simultaneous-domain, unknown-owner, count/duplicate/type,
boolean/string/trailer and every-truncation checks pass. Two initial fixture
mistakes (partial-word mutation and missing scan candidates in very short values)
were corrected; final affected CTest passes. Five affected/neighbor CTests pass:
defrelocate, defnamespace, binclone, fmplinks and binwrite. Not a full-suite claim.

Read-only `creature-visual-graph-d3hdld8y` checks all 502 three-node graphs:
494 now have no unresolved candidates; eight root payload findings remain.
All 1,506 payloads round-trip; 2,435 verified root rewrites and zero body or
appearance rewrites. Native bank hashes unchanged. The former twenty appearance
findings are resolved as asset fields or straddled primitive windows, not remapped.

`creature-animation-audit-nzpfyd8g` independently verifies canonical relocation
on isolated native Animation values from all 320 appearance entries. All 38,351
non-null native name-offset words relocate to synthetic in-memory destinations;
the full before/after bytes match only those independently located words.
Every actual non-null reference is covered by the audit map (native minimum
106,608; map includes names offsets >=65,536). Small-offset ambiguity is tested
synthetically, not claimed as fully classified. Other appearance fields are not
included in this name-rebase audit. Source banks unchanged; no installed outputs,
new archive, gameplay, transaction or private visual clone eligibility claim.

Next: implement the coordinated root/body/appearance append plan. Selected visual
rows must receive new component indices and localized new-root original parents;
unselected shared gameplay rows retain their original-parent bytes. Do not use
a value-wide old-parent map or mark borrowed components as privately owned.

## Coordinated private visual core API

Animation coverage head cdce763 Windows CI 38073837976 passed. New core API
`defedit::cloneCreatureVisuals` appends root/CCreatureDef/CAppearanceDef with
three explicit bounded ASCII names, native signed index limits and no new compiled
name-CRC ambiguity. Donor and selected original-parent rows must be clean counted
Creature listings with exactly one correctly typed/matching visual component.
Selected rows receive their new component index and new-root original parent.
All twelve bytes of every unselected CDefs row remain exact, including shared
gameplay data/parent references that might otherwise match the relocation map.
Other verified graph references relocate, while models/textures/animations remain
shared. A disposable File holds all payload/metadata appends until a noexcept
commit; any later component-name/capacity/ordinal/refusal leaves the caller exact.

Known-layout validation can now be requested even without matching moved IDs.
Private cloning uses it, so malformed recognized reference/Animation layouts
cannot bypass checks via a no-candidate no-op. It does not validate opaque fields
or certify all runtime behavior. An initial truncation fixture targeted an opaque
integer, which deliberately has no full layout validation; corrected to the
known reference field. Synthetic ownership/inheritance, clone-of-clone, namespace,
late-name, count/type/owner/layout and pre-mutation refusal cases pass.

Seven affected/neighbor CTests pass (new private API, prior shared Creature API,
generic clone, relocation, namespaces, FMP links and BIN serialization). Inventory
is 57 CTests; not a full-suite claim. Native `creature-private-api-asz7xrrz` accepts
494 donors and makes 23 expected non-mutating refusals: fifteen missing visual
shapes and eight unresolved root payloads. Output has 16,246 records (14,764 prior
plus 1,482 private additions). Independent raw audit checks exact selected-owner
deltas, all prior payloads, full original numeric-table prefix, original name-pool
records, new ordinals/name CRCs and reload. Source SHA256 unchanged; only paired
definition-bank evidence is emitted, not copied full installs or assets. Earlier
kk14wvqv packet lacked the integrated numeric-table/ordinal/CRC checks; final
asz7xrrz includes them. No private publisher, CLI, GUI, archive or gameplay claim.

Next: a typed paired publisher contract that recomputes the whole three-record
plan against the fresh source, followed by the private CLI and GUI controls.
Keep prior shared/exact modes and their tested checkpoint intact.

## Typed private paired publication

Core checkpoint e2a3c54 was pushed; its Windows CI 38075951769 was still running
at the last observation. `definitionwrite::publishClone` now accepts an explicit
`CreatureVisualCloneContract` containing schema, donor index and all three names.
It rejects mixed shared/private contracts and requires exactly three additions
in the loaded namespace. Before staging, it recomputes the canonical private plan
from the freshly reopened source and compares every record identity, ordinal and
payload. The existing PendingBanks pair, source/destination rechecks, offline
guards, alias refusal and recovery logic are reused, not replaced.

Extended private CTest passes fresh output, exact existing-pair replacement and
in-place independent fixtures with paired originals/creation markers. It refuses
tampering in each appended payload, a changed prior slot, wrong donor/name, mixed
contracts, incompatible already-appended output, orphan recovery records and a
hardlinked source-bank alias. Fixtures contain only small definition-bank pairs.
No live-game guard probe, injected rename-failure rollback, new native publisher
audit, CLI/private GUI wiring or release/package claim yet. Prior core native
494/23 evidence remains about core cloning, not this new publication path.

Next: build the existing publisher callers and verify shared/exact regressions,
then expose explicit private names through CLI and the compact GUI confirmation.

Existing GUI/CLI publisher callers rebuilt successfully (session 84749 terminal
success). Shared Creature CLI gate `creature-clone-cli-66tqgmhc` passes all thirty
cases, including paired rename-denial rollback and explicit Restore. Those checks
used the pre-private-command binary with the new publisher header.

Private command is now wired as `defs clone-creature-private <game.bin>
<donor-name-or-index> <root-name> <body-name> <appearance-name> --schema ... --out
<game.bin>`. It uses canonical core cloning and the typed publisher; JSON reports
all three indices/names, private visual components and shared assets. The rebuild
is still live in session 47337; do not claim this command exercised yet.
`tools/test_creature_private_clone_cli.py` is ready for that binary and audits
small independently encoded fixtures, selected owner/reference deltas, original
numeric-table/name-pool prefixes, selectors and invalid names/CRC collisions.
Next: observe the same build handle, run that gate, repeat shared CLI against the
new binary, then commit the publisher/CLI checkpoint and add private GUI mode.

Private command rebuild session 47337 completed successfully. Final independent
gate `creature-private-cli-syb42lx7` passes 28 cases, including exact existing and
in-place pair originals/Restore, a Windows name-pool rename denial with rollback,
and destination pool mismatch. The initial fixture 42wykc1a intentionally hit the
unknown-scalar guard because opaque Components scalar zero matched moved donor
zero; changed the synthetic scalar to 99, not the production guard. Earlier
favbp55q passed 24 cases before rollback/Restore additions. Seven scoped CTests
pass again; current-binary shared Creature CLI b98vm26u passes 30 cases and generic
clone `definition-clone-ascu504p` passes 35 including owned
sleeping-helper running-owner/alias checks. No real game process was launched.
Registered private gate in check_all; no full broad-suite claim. CLI documentation
now distinguishes shared components, private visual definitions and shared assets.
Next: commit/push this checkpoint, then private GUI mode and both-size UI evidence.

Publisher/CLI checkpoint committed and pushed as 0ec6697. Windows CI 38076414718
and prior core CI 38075951769 were still running at the last observation.

Private GUI implementation is now in progress: a separate Creature action keeps
the shared/exact buttons, preflights a disposable private plan with CRC-distinct
probe names, captures the same source pair and destination, and displays explicit
root/body/appearance names with shared-assets/gameplay notices. Save passes all
three names through the canonical core and typed publisher before selecting the
new root; existing stale-source/destination and draft guards remain. The live GUI
build is session 77516; do not restart just because its observation times out.
The latest root-label/success-message tweak was made while that build was live,
so a subsequent incremental build is required before claiming the final UI
compiled. No private GUI clicks/screenshots, compact-window or runtime claim yet.
Next: finish this build, incrementally rebuild the final touched GUI source,
exercise private controls at both sizes and repeat the shared GUI regression.

Core CI 38075951769 (e2a3c54) now passed; publisher CI 38076414718 last seen running.
GUI build 77516 completed; final incremental rebuild plus private gate 6661 also
completed successfully. `creature-private-ui-ghugbpwb` passes six native-input GUI
cases: save, cancel and duplicate-body-name refusal at 1440x900/scale1 and
800x600/scale1.5. Real buttons and all three input widgets are exercised. Independent
raw parsing plus canonical relocation observations check exact selected-row
ownership, other shared rows, all three output payloads, every prior record and
numeric-table/name-pool prefixes. Fresh pair recovery markers and Restore pass;
source game/name SHA256 unchanged. Outputs are definition banks only, no full
install or texture copies. Compact confirmation screenshot inspected: body and
appearance names, shared-assets notice and Create/Cancel fit; root field remains
reachable above in the scrolling card. No claim that all fields fit simultaneously.
Private stale-source/destination, close-review, missing-component and inherited
donor UI probes remain to broaden; core/CLI evidence is not a substitute for them.

Registered native private-GUI gate in check_all. Full existing shared Creature GUI
regression is live in session 90978, packet `creature-clone-ui-jd57wu7o`; initial
large-window save/numeric/indexed-duplicate cases passed. Observe this handle to
completion rather than restarting. No full-suite, new archive or gameplay claim.

Shared GUI session 90978 completed successfully: all 36 cases in jd57wu7o pass,
including both-size folder/pack, Close/Install, stale source, malformed/unknown
donors and exact recovery. Private extension session 62469 completed successfully:
`creature-private-ui-7c1azaou` passes ten new both-size empty-root/body/appearance,
destination-change refusal and close-review cases. Close Cancel retains the draft;
Review returns to the explicit confirmation, then save yields the exact three-node
plan and Restore removes its created pair. Six prior native GUI checks are reused,
not rerun. The default private gate covers all eight modes at each size; --only
allows scoped continuation. Every packet verifies source hashes unchanged.

Next: private stale-bank/name source checks on a small owned definition fixture,
missing/unresolved and inherited-donor GUI probes, then a new development package
with extracted checks. Existing shared GUI archive remains untouched.

GUI checkpoint 96e440c committed and pushed. Private stale-source gate now covers
owned game/name pairs with a screenshot barrier before deliberate header changes.
Session 43238 completed successfully: `creature-private-ui-wuxluix8` passes four
stale-bank/stale-names cases across both window sizes. Refusal retains the draft,
creates no output or originals and leaves the deliberately changed fixture pair
exact. Native source SHA256 remains unchanged. Each pair is 907,995 + 397,006 bytes;
only a tiny synthetic texture bank and empty level marker accompany it, not any
copied game/texture archives. The default gate now covers ten modes at both sizes;
sixteen prior private checks are reused, not rerun. Publisher CI 38076414718 still
live at last observation; no repeat build/test needed for this test-only change.

Next: missing/unresolved and inherited-donor GUI probes, then development package
validation. No gameplay or full new release claim.

Stale-source checkpoint a49ec80 push session 33618 completed successfully. An
overlapping ordinary Git status read hit shared D: loose-object permission errors;
use the established C: object directory plus D: alternate rather than deleting,
overwriting or repairing shared objects. Other safe task work continued.

Private donor-policy gate now selects supported inherited, missing-visual and
unresolved donors from the independently decoded source graph. `lx3e4x9z` passes
two inherited-donor saves: root 264 borrowed appearance from original parent 262;
the new selected parent is localized to the new root, all unselected triples and
prior records/pool stay exact, and Restore succeeds. `o2b2ybnl` passes two missing
visual-donor preflight refusals (root 113); `k4hez073` passes two unresolved-donor
preflight refusals. Both unsupported cases leave no draft/output and native hashes
unchanged. An initial unresolved selector accessed a missing-visual root absent
from canonical payload observations; restored the explicit shape check before the
lookup, then the gate passed. Production logic unchanged. Six new both-size cases
reuse the same GUI binary; prior twenty private checks are not repeated.

Registered all three donor-policy checks in the native optional gate. Next:
commit this test checkpoint and create a separately named private-GUI development
archive, then verify its identities, documentation and selected extracted checks.
The older shared-GUI archive and public RC2 must remain unchanged.

Donor-policy checkpoint 07a84cb committed/pushed. All three shipping targets are
current; forge.exe required relinking while GUI/tools were already up to date.
Added unrelated-cwd routing to the private CLI/GUI scripts and extracted-package
workflow registrations. Generated CLI.md unchanged: it documents forge.exe,
whereas private forge-tools syntax is in shipped EDITOR.md and tool help.

Fresh local package (no public publication):
`dist/development/20261010-creature-private-gui/FableForge-0.18.1-dev-win64.zip`
SHA256 6422b166165a6aac34e8d51dbacb72b0f40c0ca71fd896ac82574619be4746c9,
37.2 MB. Existing shared-GUI archive hash remains
51d135c1e95f4fb51cfe4a501f164a7064aa3c1577d4467c137c789a54ffca4e.
Package manifest accurately records checkout 07a84cb with tracked_changes=true
for three test-harness changes; it is not a clean-checkout/executable-provenance
claim. Archive will not be overwritten just to refresh that metadata.

Extracted gate is live in session 28722, packet `package-smoke-oey2hj9e`:
all 117 identities and 141 links pass, defs/shared CLI/private CLI pass so far.
Nine workflows were requested, including all private donor policies and shared
GUI/selector regressions. Observe this handle through completion, recording
omissions/skips accurately. No broad-suite/live-game/other-machine claim.

Extracted progress: private first/inherited/missing/unresolved GUI workflows now
all pass using packaged GUI/Restore/schema from an unrelated working directory.
That includes twenty first-donor controls/draft/stale-source checks plus six donor
policy cases. Together with defs and both CLI checks, seven requested workflows
have passed. Session 28722 remains live in shared GUI regression, with selector
still to follow; retain the same archive and handle. Do not report all nine or
final omitted/skipped counts before result.json exists. Test-harness changes are
ready to commit independently of the still-running final regression.

Package-gate harness checkpoint 390b5f1 committed/pushed. Session 28722 is now
terminal success: final oey2hj9e result.json proves nine selected workflows passed,
130 not run and two unrelated missing-corpus skips (controller/Aeon and F2 FMP).
All 117 shipped identities and 141 documentation links passed. Private package
checks exercise 28 CLI cases and 26 both-size GUI cases, including stale source,
inherited ownership and missing/unresolved preflight. Shared CLI/36 GUI cases and
the selector also passed using extracted executables. The archive hash stays
6422b166165a6aac34e8d51dbacb72b0f40c0ca71fd896ac82574619be4746c9; old shared
archive and RC2 are unchanged. No full-suite, clean-machine or gameplay claim.

Next roadmap step: make the root's verified body/appearance definitions directly
reachable in the browser so privately cloned components can be inspected/edited.
Reuse counted CDefs and explicit index navigation, reject ambiguous/wrong-class
targets and disable navigation during drafts. This is read-only navigation, not
private asset copying or completion of custom skinned mesh import. Graphic/body
asset binding and optional custom-body import remain real outstanding scope.

Read-only body/appearance navigation implementation is in progress. New
definitionread helper requires a clean matching CREATURE decode, exact counted
CDefs prefix and one correctly typed/in-range component row. Only CCreatureDef
and CAppearanceDef are supported; names never become indices. GUI queues explicit
record navigation for the next frame and disables both links during a draft.
Links live inside the scrolling detail panel with a shared-component edit warning.
No bank writes or private ownership/asset inference are introduced.

Focused definitionfield CTest passes valid numeric-name links and duplicate,
missing, wrong-class, out-of-range and malformed listing refusal. Initial malformed
fixture decode itself threw before link lookup; the test now mirrors the browser's
no-decoded-selection refusal path. GUI build is live in session 35899; no actual
link clicks or both-size navigation acceptance claim yet. The already validated
private archive remains unchanged, not rebuilt with this new UI work.
Next: observe that build, add independent native/synthetic navigation controls
checks, then proceed toward Graphic/body asset binding.

Navigation GUI build completed successfully. New native read-only gate
`tools/test_creature_visual_navigation_ui.py` passes at 1440x900/scale 1 and
800x600/scale 1.5, with eight independently parsed component-target jumps and
eight disabled-link draft guards across own/inherited roots. All compiled-bank
hashes remain unchanged. Evidence: external 20261010 packet
`creature-visual-navigation-ui-0459c39j`; compact body screenshot inspected.
Focused definitionfield CTest passes again. Registered the native gate in
check_all; broad suite not rerun. Numeric-name and malformed-reference coverage
is currently helper-unit coverage, not synthetic GUI acceptance. Source work
only: existing private/shared archives and public RC2 remain unchanged.

Synthetic navigation acceptance now passes both sizes in packet
`creature-visual-navigation-fixture-ui-qv727sfw`: deliberately crossed numeric
component names resolve by index, wrong-class/out-of-range/duplicate/empty/bad-count
roots offer no links, and a body-only root retains its valid body link. All tiny
fixture files remain exact. First packet g4vd_ybt failed because the automation
widget registry retains prior frames, not because invalid links were rendered.
Added explicit clear_widget_registry and assert_widget_absent commands and
corrected AUTOMATION.md's prior inaccurate this-frame assertion description.
Registered the synthetic gate; no broad-suite or packaged-navigation claim.

## Body mesh binding preparation

Starting from pushed navigation checkpoint 21315c4. The active schema exposes
CCreatureDef fields only after its composite prefix: there is no ordinary
Graphic field to patch. Existing counted RandomAppearanceMorph parsing in
forgecore/fmplinks already validates body-part/alternative model words, texture
pairs and skeletal name offsets; expression meshes share the MeshEntry domain
but are not body meshes. Prior original nonempty-array evidence is recorded in
HANDOFF_NAV's 2026-10-05 recovery (9,935 audited words across 186 records), not
new gameplay or native-disassembly proof in this turn.

CreaturePrefixLayout now exposes bodyModels {bodyPart, alternative, offset}
alongside its unchanged reference enumeration. It reuses the complete bounded
prefix parser, preserves native indices including empty body parts, and excludes
facial-expression meshes. No writer or asset mutation added. Targeted fmplinks
and defrelocate CTests pass; the mixed-domain fixture tests body-only membership
and an empty leading body part without renumbering. No broad-suite claim.
Next: independently audit these typed slots on retail components, then expose
body model selection/binding with exact four-byte deltas, mesh identity/type
validation, stale-source/destination protection and paired recovery. Skinned
custom import and runtime skeleton compatibility remain outstanding scope.

Independent retail body-slot audit passes in external 20261010 packet
`creature-body-model-audit-c7_5_3vw`: all 330 CCreatureDef records accounted for,
329 parsed, one unsupported prefix refused, 1,745 exact model words across 188
populated components. The independent parser matches body-part/alternative order,
offset and ID against canonical output, walks wound/texture/skeletal/expression
boundaries and later markers, and checks InitialActions at the decoded boundary.
All compiled-bank hashes unchanged. Stats itself remains opaque: the first audit
incorrectly assumed a four-byte Stats value; retail has a larger block, so the
diagnostic now explicitly stops structural interpretation at its marker.
No Stats grammar, graphics-bank identity/type, skeleton or runtime compatibility
claim. Targeted linker/relocation tests pass; registered the retail audit in
check_all without repeating the broad suite. Test executable mode is read-only.

Graphics identity follow-up passes in packet `creature-body-model-audit-faubfraq`.
The independent mmap directory walk preserves duplicate IDs instead of silently
collapsing them and reads no mesh payloads. All 1,745 body slots resolve uniquely
in MBANK_ALLMESHES, all with stored type 5. Graphics SHA256 checked before/after
by streaming; all compiled-bank hashes also unchanged. No large copies created.
Audit observations now include per-slot target name/type and missing/ambiguous
counts; check_all requests --graphics. These identities/types do not prove
skeleton compatibility or successful rendering. Existing selectModelById refuses
missing/ambiguous IDs before clearing preview, but a unique undecodable mesh can
still clear preview and fail decode; do not overstate that preservation contract.
Next: body-slot preview through this guarded selector, then typed binding with
independent exact four-byte delta and recovery checks. Custom skinned import is
still outstanding; no writer or package changed by this metadata audit.

Body-slot preview controls now reuse validated bodyModels and selectModelById.
They require clean exact decode/encode, game-bank CCreatureDef and a supported
counted prefix; malformed prefixes have no guessed links. Labels/widgets preserve
body-part and alternative indices. Drafts and asset writes disable links. Successful
selection opens Models; missing/ambiguous IDs use the existing refusal path.
No model writer, body rebinding or private asset copying introduced.
GUI build and three focused linker/relocation/definitionfield CTests pass.
Both-size native UI acceptance passes in `creature-body-preview-ui-28eh7100`:
CCreatureDef 9116, body 0 alternatives 0/1, IDs 4534/4537 select named meshes
and render; four draft guards retain state. Compact render visually inspected.
All compiled-bank and graphics hashes unchanged. This is individual body-part
preview, not assembled/animated NPC or skeleton compatibility. Registered the
gate; broad suite not rerun and existing archives/public RC2 unchanged.

## In-memory body model rebinding

defedit::setCreatureBodyModel now targets an explicit CCreatureDef entry index,
body-part index and alternative index using the validated canonical prefix.
The entire decode/encode must be clean/exact, and the complete prefix must parse
before changing anything. Both current and replacement IDs must resolve uniquely
to type-5 entries in exactly one MBANK_ALLMESHES bank. All checks and payload-copy
allocation precede mutation; only the selected four-byte word changes. Same ID
is a validated byte-exact no-op. Names, identities, other records and all mesh/
texture/animation bytes remain unchanged. This is memory only, not publication.

Targeted fmplinks and defrelocate CTests pass. Added exact intended-delta/no-op
checks plus twelve refusal cases for entry/class/body/alternative, missing ID,
wrong current/replacement types, duplicate ID/bank, absent bank, later malformed
marker and truncated component; every refusal verifies all records unchanged.
No runtime/skeleton compatibility claim follows from type checks. Publisher/CLI/
GUI promotion and independent retail output/recovery acceptance remain required.
Reuse existing-entry guarded publication, but add a typed recomputation contract
and stale graphics identity checks rather than treating arbitrary changed payloads
as a verified body binding. Existing packages and RC2 remain unchanged.

Typed body publication is implemented in src/definitionbody.hpp. Its contract
captures schema, graphics directory snapshot, exact loaded source bank/name bytes
and requested entry/body/alternative/model ID. It reopens source and recomputes
the canonical edit, compares every proposed record/name, and requires existing
outputs to match the loaded pair. Raw bank/name bytes and graphics directory
metadata are rechecked immediately before PendingBanks installation through a
new optional callback in the existing-entry publisher. Hardlink/cross-bank/asset
aliases, offline guards and creation-record protections remain enforced.
An existing destination graphics directory must also match; absent graphics uses
the loaded source fallback. This is directory identity validation, not a full
asset-payload hash or runtime/skeleton compatibility guarantee.

Synthetic publisher checks pass fresh and in-place output, exact name-pool
preservation, tampered-plan/source-byte/graphics-type refusals, late callback
refusal before installation, fresh-file Restore removal and exact in-place
original Restore. Three focused Creature/linker/relocation CTests pass. No native
input writes, CLI/GUI promotion, independent retail published delta, complete
race/rollback matrix or broad-suite acceptance yet. Next: broaden publisher
negatives and independently verify retail publication/Restore, then CLI/UI.

Retail publisher test mode --body-publish writes only a fresh output outside the
source install and invokes the typed contract, not raw serialization. Independent
Python gate passes in `creature-body-publication-3s59jcbe`: native components
9116/9118 select another existing alternative in their same body part. Both outputs
match all 14,764 records except the intended four-byte model word, preserve raw
header/identity tables and names.bin exactly, and contain both creation markers.
Explicit forge restore removes both created banks and markers. Compiled and
graphics hashes remain unchanged; no asset banks copied. This is offline binding
and recovery, not runtime or skeleton compatibility. Gate registered in check_all.

Six additional synthetic publisher negatives pass: wrong model contract, absent
alternative, unauthorized extra record, stale name-pool bytes, hardlinked source
output and incompatible destination graphics directory. Refusals retain output
existence and prior bytes. Initial compile caught a test-only assignment to a
reference-bearing contract; use a separately constructed proof copy instead.
Three affected CTests pass again. Complete race/locked-file rollback matrix and
CLI/UI promotion still remain; no broad suite or new package claim.

Windows partial-install rollback checks now use tiny owned prepared files held
without FILE_SHARE_DELETE: names.bin forces refusal after bank installation;
game.bin.forge-created forces refusal after both banks install. Both routes
verify the exception identifies the intended rename, no bank/name/creation marker
remains installed, and source bank/name bytes remain exact. This tests the shared
existing-entry transaction used by body publication, not a forced race hook in
the typed wrapper. Conditional Windows-only coverage exercised locally; other
platforms do not run these lock cases. No all-races/full-matrix claim.
The locking handle is released after observing refusal. Temporary fixture cleanup
remains confined to its exclusively created test directory. Next: explicit-index
body-binding CLI with required schema/graphics/output paths, then GUI drafts.

Body-binding CLI implemented: forge-tools defs bind-body-model <game.bin>
<entry-index> <body-part> <alternative> <model-id> --schema <schema.json>
--graphics <graphics.big> --out <game.bin> [--names <names.bin>] [--json].
Selectors are explicit complete unsigned decimal indices, not name lookups;
overflow, duplicate/unknown options and missing required paths refuse. The CLI
captures raw sources, uses the canonical setter and typed publisher, and reports
old/new IDs plus runtime_compatibility_verified=false. Assets remain shared.
GUI draft integration is still pending. Existing packages/RC2 unchanged.

New executable builds. CLI retail gate passes in packet
`creature-body-publication-749nycre`: two native four-byte publication/Restore
cases and fifteen selector/option/range/class refusals. Native compiled/graphics
hashes unchanged; no asset copies. Registered source gate and documented syntax
in EDITOR.md/tool help (generated CLI.md describes forge.exe, not forge-tools).
Neighboring private-clone CLI regression passes all 28 cases in packet
`creature-private-cli-56lfcfgv` using the same rebuilt executable. No broad-suite
claim; next is GUI body-binding drafts and save/cancel/stale-source coverage.

Body-binding GUI draft integration implemented. Verified CCreatureDef body slots
offer Bind body/alternative actions; a draft captures source bytes, definition
bank, graphics directory/path, slot and destination. Existing save/cancel/close
protection applies. Save requires a complete uint32 model ID, unchanged sources/
graphics, canonical type-5 edit and typed publication. Valid no-op saves write
nothing. Confirmation warns about shared users and unverified compatibility.
The initial UI uses an explicit model-ID field; a named model chooser remains
useful follow-up, alongside the larger custom skinned-import scope.

GUI build and twelve both-size cases pass in `creature-body-binding-ui-jizkwumk`:
save with independent exact intended record delta and Restore, cancel, invalid ID,
no-op, destination change and close review. All native compiled/graphics hashes
remain unchanged; no asset copies. First packet 8oxqjg_n failed because harness
input appended to the initial ID; screenshot confirmed the combined number was
refused. Corrected Ctrl+A typing, without weakening production validation.
Compact confirmation inspected: shared-user/compatibility warning and Save/Cancel
fit inside the scrolling card. Preview regressions pass in snyq9g6h and three
focused CTests pass. Registered source gate; no broad-suite/package claim.
Next: isolated stale bank/name/graphics UI checks, pack destination coverage and
named mesh choice before the next extracted package. Existing archives/RC2 stay
unchanged. No assembled-creature/animation/runtime compatibility claim.

Tiny independent stale-source GUI gate now passes eight both-size cases in
`creature-body-stale-ui-li7pdpxp`: save/Restore controls plus live draft bank,
names and graphics-directory changes. Synthetic source has one CCreatureDef,
two tiny type-5 graphics directory entries and one tiny texture slot; no retail
bank copied. A screenshot barrier after opening the draft precedes mutation.
Each refusal retains editing state, logs the specific expected stale bank/name
or graphics reason, creates no output/recovery files, and leaves every fixture
byte exact except the deliberate mutation. Both positive controls independently
check the one-word delta and Restore. Initial 549_zyuf packet passed state checks;
the final gate adds specific error-log assertions to exclude unrelated refusals.
Production code unchanged. Registered unconditional source gate; no rebuild or
broad-suite repetition needed. Pack destinations and named model chooser remain
next before new extracted-package acceptance. Existing packages/RC2 unchanged.

Body-binding pack GUI gate passes eight both-size cases in
`creature-body-pack-ui-j0m04p_v`: fresh publication with creation tracking and
Restore, existing-pack source preference and in-place Restore, destination switch
refusal, and a manifest removed after the draft barrier. Existing pack starts
with model 20 while base starts with 10; rebinding the pack to 10 must perform
a real edit, proving the pack donor is used rather than the base no-op donor.
Independent record checks require only the four-byte model word to change;
raw names, base files, manifest and unrelated pack files remain exact. Existing
bank backup remains exact after Restore, matching default keep-backup behavior;
fresh files and creation markers are removed. Refusals retain drafts, log the
specific reason and leave destinations unchanged except intentional manifest
removal. Initial harness packets lzoqsg4z/7xgrx3lb exposed incorrect expectations
for an unchanged names backup and default retained bank backup; corrected to the
actual preservation contract, with full restored inventory checks. Tiny fixture
helper accepts an initial model ID; default stale-source fixtures unchanged.
Registered unconditional gate. No production changes, rebuild, broad-suite or
package claim. Next: named model chooser and new extracted-package acceptance;
custom skinned import and skeleton/runtime compatibility remain outstanding.

Named body-model chooser implemented alongside the explicit numeric ID field.
It uses only the draft's captured MBANK_ALLMESHES directory, counts all entries
by ID (including other types), and lists unique type-5 IDs. Labels show ID plus
name or an unnamed placeholder; duplicate names remain distinct. Case-insensitive
name/decimal-ID search and a clipped list follow existing definition choosers.
Search resets on beginning a body draft. Selection sets only the existing draft
value; no new write path or relaxed publication/stale-source guard.

GUI builds; twelve both-size cases pass in
`creature-body-model-picker-ui-txdybjn1`: named selection, name search, ID search,
unnamed entry, duplicate names with different IDs, and exclusion of wrong types
plus an ID duplicated across type 5/type 1. Tests click actual popup controls,
clear widget registrations for filtered-absence assertions, independently check
the sole model-word delta and raw name preservation, then Restore. Compact
duplicate-name screenshot inspected: ID-labelled alternatives and search fit.
Three focused CTests pass. Rebuilt-GUI pack regressions pass eight cases in
`creature-body-pack-ui-0fh4qcac`; stale bank/names/graphics regressions pass eight
in `creature-body-stale-ui-0a_hgqog`. All fixtures tiny and source bytes exact;
no retail asset copies. Registered chooser gate and documented GUI semantics.
Existing immutable packages/RC2 do not include these changes. Next is a fresh
development package with extracted acceptance, then the outstanding custom
skinned-import/compatibility work. No broad-suite or runtime claim.
