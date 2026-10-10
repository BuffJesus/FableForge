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
