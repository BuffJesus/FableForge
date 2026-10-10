# Controller Support with mixed mod stacks

Offline evidence, 2026-10-05. These checks exercise composition and saved-choice
behavior. They do not establish gameplay compatibility or a recommended load
order, and the live game install was not changed.

## Exercised combinations

Small additions are No Baldness with Horns, WaterWader and EnableSprint.
Controller Support counts as two sources: its FMP and its DLL folder.

| Baseline | Other sources alongside Controller Support | Total sources | Result |
|---|---|---:|---|
| Installed snapshot | Small additions | 5 | Pass, controller first and last |
| Retail definition backups | Ultimate Spell Pack's 18 FMPs and small additions | 23 | Pass, first and last |
| Retail definition backups | mfvicli Better's 8 modular FMPs and small additions | 13 | Pass, first and last |
| Retail definition backups | Ultimate + Better modular + small additions | 31 | Pass, first and last |
| Retail definition backups | Ultimate + OP's 7 modular FMPs + small additions | 30 | Pass, first and last |
| Project Seasons complete Data as baseline | Small additions | 5 | Pass |
| Project Seasons complete Data as baseline | Ultimate + Better modular + small additions | 31 | Pass |
| Albion Secrets complete Data as baseline | Small additions | 5 | Pass |
| Albion Secrets complete Data as baseline | Ultimate + Better modular + small additions | 31 | Pass |

The first five-source check used the current installed definition snapshot;
the eight larger retail-stack checks explicitly use the retail definition
backups. Other asset banks are copied from the available installed snapshot.
The large baselines preserve each mod's complete supplied Data directory.
This does not demonstrate layering multiple overhauls onto each other.

All twelve successful outputs in the modular/large-baseline probe independently
retain the original 141 controller bindings, both controller UI records, the
controller DLL and fourteen resolved references. Spell conflicts remain visible
for user selection. All eighteen Ultimate FMPs include alternative Slow Time
variants: this is deliberate conflict stress, not an installation recommendation.

## Lifecycle regression and fix

`tools/test_controller_stack_lifecycle.py` registers the 31-source retail stack,
moves Controller Support first/last, reverses the spell pack order, saves a
BetterBerserk SpeedMultiplier choice, then disables BetterBerserk. The old
07caf52 tool silently ignored that now-unavailable choice and built successfully.

The fix retains explicit scalar choices when contributors collapse to one,
agree, or stop changing the field. An unavailable selected contributor refuses
before output writes and remains reviewable. Baseline choices continue selecting
baseline bytes; unrelated or obsolete-layout keys retain their existing policy.
The regression verifies previous output/picks preservation on refusal, clearing
and re-enabling the spell, disabling/removing Controller Support against an
independent controller-free composition, and re-adding it. Source and baseline
bank hashes remain unchanged.

Validation: all forty core tests, 45 synthetic contributor checks in each of
ordinary and complete-Data modes, and the existing scalar-choice GUI checks at
1440x900 and 800x600 pass. The corpus lifecycle passes all seven successful
controller builds plus refusal, disable and removal checks.

## Known refused combinations

- Adding all 35 mfvicli FMPs alongside Ultimate and the small additions (58
  sources including Controller Support) refuses duplicate metadata ownership
  for `OBJECT_HERO_MASK_STEALTH / CAppearanceModifierDef`. That collection mixes
  complete packages, modular variants and item packs; it is not a sensible
  default installation.
- Combining Project Seasons and Albion Secrets still collides at game slot
  15,195: Seasons has `CREATURE_GENERATION_FAMILY / RATS_01`; Secrets has
  `CInventoryItemDef / CInventoryItemDef_F2_RUSTY_LONGSWORD`. Their diverging
  extensions require reference relocation, not another whole-mod winner.

The earlier blanket unnamed-entry refusals for each overhaul and UFP are now
superseded by the whole-bank path below. Other refused combinations remain open.

## Whole-bank follow-up

Whole-bank trees now reserve compatible record slots and name offsets, then
compose changes through the ordinary record/scalar-field conflict system. Source
order remains the default; it does not replace granular choices. Compatible
script/frontend banks participate with bank-scoped choices. Whole-bank text IDs
and valid narrator indices are preserved, including authored empty identifiers.
World-merge errors now refuse publication instead of returning partial success.

Both original overhaul trees compose with Controller Support first and last
against an owned scratch stock-WAD baseline. It uses the retail definition
backups plus the available installed asset/world snapshot. Independent audits
preserve all 15,196 Seasons / 15,368 Secrets original game slots and every
non-controller payload, exact script/frontend records, and all 29,085 / 29,161
text IDs, types, headers and payloads. Fourteen controller references and its
DLL pass; world composition completes. No gameplay test or live deployment.

The 32-source UFP lifecycle now passes order changes, saved field choices,
disable/remove/re-add and unchanged inputs. The GUI independently selects game,
script and frontend field winners at 1440x900 and 800x600, with exact output-byte
checks. Changed narrator lists, arbitrary raw-bank relocation and incompatible
overhaul namespaces remain outside this increment.

Each overhaul also passes as a source in a 32-source stack: Controller FMP/DLL,
all eighteen Ultimate spell FMPs, eight Better modular FMPs and the three small
additions, with Controller last. The independent audit limits permitted payload
changes to the actual FMP records (including No Baldness's eight numeric slots),
preserves the overhaul's remaining records and text IDs, and checks world merge.
This retains the alternative-spell stress-test caveat above.

Evidence under the root below: `whole-bank-overhaul-acceptance-hyisxwen` (first),
`whole-bank-overhaul-acceptance-8_d3b_p9` (last),
`controller-stack-lifecycle-7q1j3y_o` (UFP lifecycle), and
`mod-whole-bank-layout-uxf30xy2` (twenty GUI/synthetic namespace and world-refusal
checks). `whole-bank-overhaul-acceptance-k090si9f` contains the two 32-source
overhaul stacks. The existing corpus lifecycle also passes in
`D:/Code/FableForge/build/mods-corpus-etfehv_h`, including UFP over a shorter
retail prefix while preserving compatible baseline additions.
The earlier `whole-bank-overhauls-hayikdlh` exploratory outputs had an incomplete
world baseline and exposed the partial-success bug; they are not full-build
acceptance evidence.

## Retained local evidence

The subsequent relocation core checkpoint does not expand this accepted stack
matrix. It verifies a hypothetical repair of Secrets' sword component reference
and preserves all other payload bytes; all 41 core tests pass. Namespace
allocation and references outside the definition banks remain unfinished.
See [the sleep checkpoint](HANDOFF_WORLD_UI.md) for evidence and resume steps.

Under `C:/Users/Cornelio/FableForge-asset-checks-20261005/`:

- `controller-stacks-probe-0xyivsfk/result.json`: initial small/stress/layer probes.
- `controller-modular-probe-j_ly7gxn/result.json` and
  `controller-content-audit.json`: twelve successes, one UFP refusal, independent
  controller-content audit, using the extracted 07caf52 tool.
- `controller-stack-lifecycle-7kl83_p9/`: original-tool lifecycle failure.
- `controller-stack-lifecycle-in_ne6ca/result.json`: fixed-tool lifecycle and
  input hashes.
- `mod-field-choice-contributors-xf673831/result.json` and
  `mod-field-choice-contributors-iwy5wobz/result.json`: 45 checks per mode.

GUI evidence is in `build/mod-field-choices-5aahcrr1/` and
`build/mod-unreported-choices-uvzokxps/`; both pass at both window sizes, including
saved-choice read failures, individual clearing and same-process retry.
The existing 07caf52 ZIP also completes all 89 distinct package workflows across
`package-smoke-tttal12d` and `package-smoke-zxgpcwgl`; the latter's
`combined-package-acceptance.json` validates identical archive hashes and zero
uncovered/skipped workflows. That older ZIP still contains the newly discovered
scalar-choice bug; this fix requires a new package.
