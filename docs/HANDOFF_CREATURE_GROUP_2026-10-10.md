# Creature GroupDef schema recovery, 2026-10-10

Owned development checkout and branch remain as in the
[picker handoff](HANDOFF_GROUP_PICKER_2026-10-10.md), starting clean at b49dcf2.
The preceding turn made concrete progress: committed/pushed guarded GroupDef
choices and completed exact extracted acceptance. CI run 38065042651 is still live.
RC2, previous checkpoints and native game files remain unchanged.

## Acceptance question and evidence

Why does retail CREATURE_VILLAGER_TEMPLATE GroupDef decode as unknown, and can
it use the existing confirmed Thing-group picker without guessing other fields?
The literal CREATURE schema is a reconstructed derived-chain alias of
CThingCreatureDef. Its GroupDef was `?`; CThingBaseDef already declares int32.
Do not confuse CThingCreatureDef (placed creature) with CCreatureDef (component).

Read-only evidence checkout: D:/Documents/FableTLC. No changes there.
FableWin.exe SHA256:
`a9d6d0977d9d7fc8242da4292ae7ed92e926ca80364a95c003d9b9f689e41845`.
Existing exports `ghidra_out/donor_def_transfers.c` lines 9633/9711 and 10156/10326
show derived CThingCreatureDef transfer calling CThingBaseDef and the latter
passing GroupDef to the signed-long Transfer overload. Retail exports
`ghidra_out/def_transfers.c` show the same base call at CThingCreatureDef 00671600
and the base implementation at 00676be0. Decompiler offset units differ; these
are not serialized field offsets.

Bounded Capstone disassembly of original FableWin bytes confirms:
- 01fb3ef8 calls 01844d10, whose jump reaches base transfer 01fd5340.
- 01fd5932 adds 4c to the owner's address; 01fd5935 pushes that address;
  01fd5936 pushes 041536a0, whose bytes are `GroupDef\0`.
- 01fd593e calls 017b28e8, whose jump reaches 018951e0.
- `ghidra_out/fablewin_pdb_names.tsv:926` identifies 018951e0 as
  `??$Transfer@J@CPersistContext@@QAEXPBDAAJ@Z` (signed long on Windows).
  Its bounded body uses the referenced scalar and returns with two arguments.

This establishes the inherited type, not every unknown type or runtime rule.
An independent native-bank inventory finds all 517 Creature payloads have a
unique GroupDef tag and exactly four bytes before RenderFadeDistance's tag.
All 517 decoded signed values match those raw bytes, and each resolves to
THING_GROUP or the preserved unresolved zero. Evidence:
`creature-group-schema-d6qh0lxm` under the 20261010 C evidence root.

## Scoped change and validation

Only CREATURE.GroupDef changes from `?` to int32. Adjacent unknown fields,
VILLAGE.GroupDef and other categories remain unchanged. No executable writer
or CLI-wire behavior changes; the existing picker and stale/family/no-op guards
now apply to this decoded field. New read-only schema regression checks all
Creature records and keeps adjacent unknown fields unknown. Package invocation
accepts an explicit extracted schema path, not the source checkout schema.
Registered in source and extracted gates.

Eight retail picker cases now pass for Object, Building, Creature and terrain
theme at both sizes: `retail-definition-group-picker-o9oas4v9`. Each independently
compares all 14,764 records, exact identity rows/names and only the selected
four-byte field delta; explicit-output Restore removes tracked creations and
native hashes remain unchanged. No full-suite, clean-machine or gameplay claim.

Both affected CTests (defdecode and definitionfield) pass. Compact 800x600 Creature
chooser screenshot inspected. Python syntax and whitespace checks pass.

Separate checkpoint:
`dist/development/20261010-creature-group-schema/FableForge-0.18.1-dev-win64.zip`
SHA256 `495961b1ab015cc6c6fab6c1508050184be1b9144fa715ad4c93eaad176effb7`.
Packaged at starting HEAD b49dcf2 with this scoped schema/docs/tests dirty;
final evidence notes updated afterward without replacing the archive.
`package-smoke-r0e8438x` passes 116 identities, 138 local documentation links
and five selected workflows: definition tree, groups, all 32 synthetic picker
cases, eight retail-copy edits and the 517-record check using the extracted schema.
124 workflows not run; two unrelated controller/Aeon and F2 missing-corpus skips
remain explicitly unexercised. Previous scalar acceptance is unchanged and retained
in the picker checkpoint, not rerun or claimed as new extracted coverage.

Next: commit/push. Creature donor
self/appearance/body-reference retargeting remains distinct unfinished work.
The debug-evidence procedure required original-byte/PDB confirmation before
promotion; format-I/O required independent retail boundaries and exact deltas.
