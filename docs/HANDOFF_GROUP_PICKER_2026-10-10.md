# Confirmed GroupDef picker continuation, 2026-10-10

Owned C development checkout/branch are unchanged from the
[font handoff](HANDOFF_GUI_FONTS_2026-10-10.md), starting clean at `91c9352`.
Font source is pushed in draft PR 2; its CI passed at 38063688200.
RC2 and prior validated development archives remain unchanged. No native writes.

## Contract and implementation

The existing reader resolves game-bank four-byte int32 GroupDef entry indices.
A read-only retail inventory found 2,799 Object, 319 Building and 504 Creature
records targeting THING_GROUP; 462 terrain themes target ENGINE_THEME_GROUP.
Another 78 records retain unresolved index-zero values. This is corroborating
retail evidence for the existing placement/theme family contract, not a universal
reference schema. A first inventory assumed a literal ENGINE_THEME schema key and
failed; that category resolves to CEngineThemeDef. Native CLI decoding confirms
GROUND_BLEACHEDLONGGRASS GroupDef is int32, raw `24090000` (entry 2340).

GUI drafts for confirmed owner categories now offer a searched, virtualized group
chooser with explicit native indices. Numeric, duplicate and unnamed labels do not
reinterpret indices as names. New targets must exist in the same loaded game bank
and have the confirmed family. Changed wrong-type, wrong-family, negative and
out-of-range values refuse before staging/backups. No-op unresolved originals retain
their exact bytes. No invented "no group" sentinel. Other categories/banks keep
their existing raw scalar behavior; no blanket inference from integer fields.
The raw CLI wire-format editor remains unchanged.

The draft captures eligible target identities (native index, type, name and ordinal,
without target payload copies). Fresh source validation must match the selected
identity before publication. Unrelated target payload edits can compose, while a
renamed/reordered identity refuses. Existing owner/namespace/destination/game/pack
guards and transactional publication/Restore remain in use. Only the four selected
GroupDef bytes change; every other payload, table identity and pool stays exact.

## Evidence so far

- GUI and draft unit target build; definition-draft CTest passes.
- Six focused GUI save/family/no-op cases pass at both sizes:
  `definition-group-picker-8uwd68nw`.
- The prior exact font archive accepts a wrong-family Object reference; the new
  refusal regression fails against it before the guard:
  `definition-group-picker-6lrxv_eb`. All writes are owned fixture copies.
- Initial full 28-case picker run passes: `definition-group-picker-tt9gwqh5`.
  Existing group/member/edit/Restore neighbors pass: `definition-groups-ui-3cn6o5zr`.
- Four retail picker cases pass for thing/theme owners at both sizes:
  `retail-definition-group-picker-ksicjt1p`. Independently compare all 14,764 records,
  exact identity tables/names and four-byte field delta; explicit-output Restore
  removes created outputs, and native compiled hashes remain exact.
- A newly added renamed-target regression fails against the initial implementation:
  `definition-group-picker-0_q_981_`. The final identity-snapshot guard now passes
  six focused stale-target/hex-no-op/repeated-save cases at both sizes:
  `definition-group-picker-f_7ouafe`; affected CTest also passes.
- Final full 32-case picker run passes: `definition-group-picker-_kr23xgg`.
- Expanded retail probe found CREATURE_VILLAGER_TEMPLATE GroupDef decodes as
  unknown type, not int32. It remains read-only; no speculative schema change.
  The initial expanded gate stops at that assertion after four successful cases:
  `retail-definition-group-picker-knx6_rru`. Final six-case Object/Building/theme
  gate passes: `retail-definition-group-picker-xkcf76bo`, preserving all 14,764
  records except each selected four-byte delta, exact identities/names, Restore
  and native input hashes. Compact 800x600 picker screenshot inspected.
- New synthetic/retail workflows are registered in source and package gates.

## Extracted checkpoint

`dist/development/20261010-def-group-picker/FableForge-0.18.1-dev-win64.zip`
SHA256 `cf6d013ef97fd8fe3fcf128f3dcebf8c156a3d272a32d852bed4750b8cc733dd`.
Packaged from starting HEAD 91c9352 with the scoped feature dirty, after final build
and source checks. Final notes updated after packaging; archive remains unchanged.
`package-smoke-j8vk1zaf` passes 115 file identities, 136 local documentation links
and eight selected workflows: retail eight-map smoke, GUI, existing-name picker,
definition tree, groups, all 34 scalar drafts, all 32 new picker cases and six
retail-copy picker cases. 120 workflows not run; two unrelated corpus skips:
controller/Aeon and F2 inputs absent at the supplied corpus root. These are not
exercised coverage. No full-suite, gameplay or clean-machine claim.
Project dev/format-I/O procedures selected affected tests and required independently
decoded exact deltas, untouched records/identities/names and explicit-output Restore.

Next: commit/push this scoped milestone. Cross-family rules for other categories, all other reference types,
multi-record clone retargeting, gameplay and clean-machine validation remain separate.
Continue Forge work until asked to stop.
