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
