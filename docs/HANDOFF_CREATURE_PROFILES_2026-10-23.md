# Creature placement profiles, 2026-10-23

The current automatic VillageMember scaffold in `Document::placeCreature` uses
`definition.find("VILLAGER")`. That does not describe the runtime component list.
The next correction should use the selected compiled definition's `Components`
value, preserving that list when a donor is cloned under a custom name.
No production placement change is included in this investigation.

## Native construction evidence

Debug executable: `D:/Documents/FableTLC/debug_build/FableWin.exe`,55,571,968 bytes,
SHA256 `a9d6d0977d9d7fc8242da4292ae7ed92e926ca80364a95c003d9b9f689e41845`.
PDB-name export: `D:/Documents/FableTLC/ghidra_out/fablewin_pdb_names.tsv`, SHA256
`d87957089ac064c8d43632114660795223965fd01675606a12163b9d8f8860ef`.
These are debug addresses, not retail addresses.

Targeted `analyzeHeadless ... -process FableWin.exe -readOnly -noanalysis`
queries used existing `DumpFuncBytesAndC.java`. They did not import, analyze,
edit types, save a program or reclaim a project lock. Eight complete function
byte ranges independently match the original PE. Seven decompile; creature
ConstructFromParams does not, so its owner branch is checked by direct disassembly.
Evidence root: `build/creature-placement-native-20261023`.

| Debug address | Function | Observation |
|---|---|---|
| 02994490 | CEditControlCentre::PaintInputPlaceThingAt | Dispatches a type-value switch to distinct creature/building/object constructors; supports the already accepted class-routing correction. |
| 01d6db30 | CThingAICreature::ConstructFromParams | At01d6db89, owner5 selects the definition's DefaultOwner byte at+0x12c. No pseudocode acceptance claimed. |
| 01b9d1b9 | CTCVillageMember constructor | Clears village pointer/UID state. |
| 01cf0320 | CThing::CreateSerialiseCreatableTCs | Parses explicit CreateTC entries and calls AddTC; distinct from default definition components. |
| 025758e0 | CTCVillageMember::OnCreate | Looks up the parent definition, then attempts GetSubDef<CVillageMemberDef>; not a name-substring test. |
| 02576e70 | CTCVillageMember::OnSerialise | Transfers VillageUID, defaults/clears to zero, and remaps nonzero UIDs. |
| 01cf3810 | CThing::CreateFromDef | Iterates GetNoComponents/GetComponent, resolves each name and calls AddTC with the entry's byte+8 flag. It does not skip component creation based on the definition's spelling. |
| 028b15c0 | CThingComponentSet::TransferBinaryIn | Reads count, raw entry-array bytes, then verifies trailing 28011726 (0x01ab6cce). |

Direct original-byte disassembly of GetByteSize at 028b21a0 ends with
`imul eax,eax,9` at 028b21af, confirming the record stride independently of the
retail reader. Each record is `{name offset:u32, parameter:u32, flag:u8}`.
This layout already exists in `src/cli/nav.cpp` and the core FMP link resolver;
reuse the core abstractions when implementing profile lookup.

## Independent retail observations

`tools/audit_creature_placement_profiles.py` reads the original names/game banks
and selected loose TNGs without launching an executable or writing the install.
Its malformed fixtures refuse truncated/miscounted lists, unknown name boundaries,
and a wrong verification word. The CDefs prefix/subdefinition table is validated
separately from the tagged runtime Components value.

Final receipt:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/creature-placement-profile-vbu6_x0p/result.json`.
All 517 CREATURE records decode; no refused records.344 list CTCVillageMember,
173 do not. Every observed Components value has the four-byte 0x01ab6cce trailer.
The name heuristic disagrees on 215 roots:214 have the runtime CTC without VILLAGER
in the name; CREATURE_RIVAL_HERO_BRIAR_ROSE_VILLAGER has the name but no such CTC.
Traders, hero family and guards are concrete non-prefixed examples; the chicken
has no VillageMember runtime CTC.

Thirty-two actual AICreature placements in StartOakValeWest/GuildExterior agree
with the runtime-component rule, including their VillageMember TNG block
presence. No sampled mismatch and no missing map. Original bank/map hashes
remain exact. This is a bounded sample, not proof about every saved TNG: explicit
CreateTC additions are supported by the engine and existing TNG edits must survive.

An initial hypothesis inspected CVillageMemberDef CDefs rows. That finds no direct
root rows, despite 12 CVillageMemberDef records existing. It is the wrong structure
for runtime-component presence. The corrected audit checks pool-referenced CTC
names in Components; do not implement the initial hypothesis. A broad scratch
file search was stopped after encountering protected unrelated fixture folders;
retail maps were read at their known install paths. No peer process was stopped.

## Implementation and acceptance next

1. Expose a read-only optional component-presence lookup through Context, backed
   by a validated Components value and original names-pool boundaries. Absent
   component, absent definition and malformed/unsupported metadata must remain
   distinguishable; no fallback to definition-name guesses.
2. Pass an explicit village-membership profile to the existing creature writer;
   keep automatic ownership and the world-space InitialPos behavior unchanged.
   Reuse the existing TNG serializer and recovery paths. Preserve existing TNG
   blocks, order, unrelated properties and undo behavior.
3. Test a custom-named creature with CTCVillageMember and a VILLAGER-named creature
   without it, plus malformed count/name/sentinel prewrite refusal. Independently
   inspect TNG blocks, undo/redo, exact bank/asset preservation and Restore.
4. Exercise both window sizes against a frozen old baseline and candidate under
   a fresh shared runtime lease. Do not repeat accepted import/native graph gates
   without a relevant change. Check every placeCreature caller, including CLI/
   direct-document tests, before changing its API.

The current immutable creature-placement checkpoint is unchanged. It includes
accepted material identity and compiled-class routing but retains this name-based
membership heuristic. Whole-creature assembly, animation and gameplay remain
separate unverified work. No new GPU/game/GUI window was used for this investigation.

Placement Windows CI 39964147797 for 91d284e completed success, verified directly;
that CI predates the new read-only audit and any future profile correction.


## Source correction prepared

Core defdecode now validates the canonical counted nine-byte Components list and
verification word before returning original name offsets. Context resolves every
reference against reconstructed original-pool boundaries (originalNames stores
CRCs plus strings, not offsets), returns optional presence with a diagnostic,
and does not guess from names. App preflights this before any document mutation.
Document::placeCreature now requires an explicit membership flag; the sole GUI
caller and sole direct unit-test caller were updated. InitialPos/owners and the
existing serializers/recovery paths are unchanged.

The existing placement fixture now includes a valid runtime list: GUI_ROOT has
CTCVillageMember; CREATURE_VILLAGER_DECOY does not. The independent TNG oracle
checks membership as well as class, owner, InitialPos and Restore. A new refusal
driver covers bad count, non-boundary name reference, wrong verification word
and missing Components at both sizes, requiring no document/file/recovery change.
Preparation-only receipts: placement-types-m9bie7kw and profile-refusal-ivlbjghy
under the 20261023 C evidence root. Core decoder malformed-list tests and explicit
Document profile/undo tests were added. Build is in progress; runtime acceptance
is not claimed yet. A targeted C++ Context audit mode will compare all517 native
roots with the independent Python observation, without writing native files.


## First source validation window

Canonical receipt:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/rp-0izfcq1z/result.json`.
Both affected executables pass: fableforge_defdecode_tests and fableforge_tests.
The C++ Context audit agrees with the independent Python audit on all517 native
roots, including344 VillageMember profiles. Original bank/map hashes remain exact.
Frozen candidate GUI SHA256:
`89a5aa62a1e3be33bc8ead71ce44dc610d403c1fed5c1e2bea7390bddad7a23f`.

Old baseline's GUI script passes its class/owner/undo actions but its independently
read TNG lacks membership on GUI_ROOT and adds it to CREATURE_VILLAGER_DECOY.
Baseline failure is exactly the new membership oracle; original TNG explicitly
Restored. Receipt `placement-types-eic8nmrm` within that window.
Candidate passes16 placements at both sizes, with exact membership, class, owner,
InitialPos, undo/redo and recovery: `placement-types-04pkjjce/result.json`.
All eight malformed-profile cases pass with unchanged document, fixture and
recovery inventory: `profile-refusal-5eridtq2/result.json`.
Own lease released; Native availability queued as
`01a1692a-b927-7740-9d98-34925f8df5ae`; receipt `rp-0izfcq1z/released-runtime-lease.json`.

An additional focused unit assertion now seeds an unrelated unknown TNG block,
requires it byte-exact after both profile choices, and requires undo to recover
the whole original source. Its rebuilt test executable has not run yet: shared
lease is owned by Texture Archive4GB, verified live PID13444/start1792765885.4409208,
and an actual Fable process is active. Both are left untouched. No new GUI gate
is needed for this test-only addition; run only the updated unit once a fresh
lease/process guard permits. CLI neighbors are rebuilding offline at -j2.
No production GUI/helper code changed after the successful comparison.


## Source correction accepted

Texture owner exited and its lease/game closed normally. Fresh process guard
and exclusive RuntimeLease allowed only the updated preservation unit to run;
it passed. Receipt:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/pu-2qfi4vr0/result.json`.
It proves the unknown existing TNG block is byte-exact for both profile choices
and undo restores the entire original text. No production code or GUI changed
since the prior successful comparison; no GUI/native audit was repeated.
All517 C++ audit rows have unique names and match all517 independent rows.
Core/GUI source hashes still match the frozen candidate; only the added unit
assertion differs from that snapshot's test source. Updated unit binary SHA is
pinned in its receipt. CLI neighbor builds pass.

Unit lease released, Native availability queued as
`01a16931-32c3-7081-9df8-5c8563fb6cfa`; receipt `pu-2qfi4vr0/released-runtime-lease.json`.
No owned runtime remains. New refusal workflow is registered in source/package
gates; source docs describe metadata-based membership and place_refused.

Acceptance now covers two affected unit executables, all517 read-only native
profiles, the old membership negative control,16 positive placements,8 malformed
profile refusals, exact recovery, and existing unknown-block preservation/undo.
No source bank/asset writer changed. The retained immutable ZIP is unchanged and
still has the old name heuristic. Next: record/push source correction, follow CI,
and validate the new profile/refusal workflows in a distinct extracted checkpoint
when batching the next authoring improvements. Whole-creature rendering remains
unverified; do not repeat unrelated accepted import/native graph gates.
