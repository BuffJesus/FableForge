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
- Treating Project Seasons or Albion Secrets as another definition layer over
  the retail snapshot refuses respectively 33 or 203 unnamed changed/new
  entries. The successful complete-baseline cases above use a different route.
- The Unofficial Fable Patch definition patch plus the 31-source stack (32 total)
  against the retail definition backups refuses 25 unnamed changed/new entries.

These refusals preserve prior output. They remain unsupported combinations;
successful Controller Support composition elsewhere does not resolve them.

## Retained local evidence

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
