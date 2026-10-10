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
