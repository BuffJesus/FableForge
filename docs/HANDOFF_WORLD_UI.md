# World view and UI continuation - 2026-09-29

## 2026-10-03 extracted current recovery package checkpoint

Source 7336194 is packaged locally at
dist/development/20261003-7336194/FableForge-0.18.0-dev-win64.zip (34.8 MB),
SHA-256 2665872f3349cc0ad8f6e78d8412cad0f2de03555581d0734dce5b6765428fc0.
The guide ZIP is beside it. Older archives remain unchanged; nothing was
published. build/package-smoke-h_h0u27z/result.json records all 19 extracted
runtime workflows passing with 115 Markdown links and four guide images checked.
Executables run from a path containing literal percent/ampersand characters,
using an unrelated working directory. Existing retail exports/GUI pixels/mod
reviews pass alongside real-bank asset restoration and every new Setup recovery
variant. This verifies stripped packaged binaries, not just the build directory.

The previous preparation section describes its pre-extraction state; this is
the completed result. The whole-suite checkpoint remains frozen afe9e55 with
110 passing checks; no fresh whole gate or actual game validation is claimed for
7336194. Next depth review: asset views across Mods deploy/undeploy transitions.

## 2026-10-03 current recovery package preparation

All Release targets are current (bounded -j2 build, no work remaining). Setup
recovery fixtures now accept --gui/--cwd; the stage fixture uses forge-tools
beside the selected GUI. test_package includes nine more recovery variants,
for 19 runtime workflows including the existing asset-bank restore.
Unrelated-cwd confirmation (build/setup-confirmation-g9jx0bms), active-stage
restore (build/setup-stage-restore-1uu4mkty) and missing-bank recovery
(build/setup-missing-bank-qan313wm) pass both sizes. The forthcoming local ZIP
still needs extraction verification; this section does not claim that result.

## 2026-10-03 Setup confirmation scope and cancellation

Baseline build/setup-confirmation-wachucng proves Restore remained globally
pending after Continue closed Setup and after accepted valid/invalid folder
changes. Enter already left the confirmation pending and Escape already
cancelled without closing Setup. Pending Restore now clears whenever Setup is
closed and on accepted folder changes after the existing unsaved/busy guards.
A same-root rescan retains it; no file-write behavior changed.

Owned Release GUI build and build/setup-confirmation-f67lhy5u pass both sizes:
Enter ignored, Escape cancels, Continue clears pending state, valid/invalid
switches clear it, and a Data-folder rescan of the same root retains it. Whole
inventories of both scratch installs stay unchanged. Existing install-switch
and orphan-preserve/recovered-manifest retry (build/setup-stage-restore-gayegfng)
pass. The confirmation fixture is wired into check_all. No fresh whole gate or
archive is claimed; the core checkpoint and previous development ZIP are unchanged.

## 2026-10-03 staged restore and orphan-backup status

Baseline build/setup-stage-restore-ps2k5wcy confirms a .forgebak without its
manifest is left unchanged and logged, but Setup did not report a persistent
incomplete result. After core restore, the GUI now checks for differing staged
backups that remain and explains that their deployment manifest is needed.
The action returns an incomplete result while such backups remain; this only
changes GUI status. Core restore policy, CLI behavior and file writes are unchanged.

Owned Release GUI build passes. build/setup-stage-restore-szdne3qq passes both
sizes: actual buttons preserve the whole install with the manifest absent,
show the warning after toast dismissal, then restore exactly in the same process
after the fixture returns its preserved manifest. Compact orphan screenshot
reviewed. build/setup-stage-restore-uq2asiur passes both sizes with an editor
original taken on top of staging: the original rebases to the older staged
baseline, the open map reloads from height 7 to 4, and stage recovery files are
consumed as intended. Partial locked restore/retry remains passing in
build/setup-restore-failure-lruk5mm3. Both stage variants are wired into check_all.
No new whole gate or archive is claimed; earlier frozen boundaries still apply.

## 2026-10-03 ambiguous restore records through Setup

No product change was needed. build/setup-restore-ambiguity-hwm01za1 passes
normal and compact actual controls for different .atlas-orig/.forge-orig
originals and for .forge-created plus .forge-orig. Preflight refuses before
changing any file, including an independent modified LEV. The persistent error
identifies the conflicting records and asks for an intended baseline; compact
duplicate-original screenshot reviewed and error bounds checked in every case.
The fixture explicitly preserves the conflicting record outside the install,
then retries in the same GUI process. Both maps restore exactly; originals,
other files and the preserved conflict stay byte-identical. This checks manual
resolution, not an automatic choice or a new GUI baseline selector. The fixture
is wired into check_all. Whole-gate and archive boundaries remain unchanged.

## 2026-10-03 partial restore and absent level-container coverage

No additional product changes were needed after missing-bank recovery.
test_setup_restore_failure_ui.py --partial passes both sizes in
build/setup-restore-failure-1l0lmxzy: a second selected LEV restores from height
9 to 4 and reloads while the locked LEV and its original remain unchanged.
The durable error survives the reload and toast dismissal; unlocking and
retrying restores the remaining file. Compact failed screenshot reviewed.

test_setup_missing_bank_ui.py --missing-levels passes both sizes in
build/setup-missing-bank-6p02baze, restoring the only loose LEV when no levels
were initially discoverable. --wad against a read-only retail source passes
both sizes in build/setup-missing-bank-jopkg517, restoring the absent stock
FinalAlbion.wad exactly and loading Greatwood_1 through the WAD route. Original
backups, unrelated files and the retail source hash stay unchanged. Both
variants also exercise target-root running-process refusal before repair,
including the compact Data-folder selection. Helpers are owned sleeping
executables, not actual game launches. All variants are wired into check_all;
stock coverage requires its existing retail-install availability condition.
These are focused checks, not a rerun of the frozen whole gate or a new archive.

## 2026-10-03 Setup recovery with a missing game bank

Baseline build/setup-missing-bank-5i0v7hvt proves the backup section and restore
buttons were hidden when game.bin was missing; the fixture then timed out waiting
for maps. Setup now scans tracked backups in the selected existing folder even
when required game data is missing. Restore requires tracked backups rather than
a currently valid install. Core restore policy and its running-game guard are
unchanged. Data-folder normalization uses the CompiledDefs directory, so a missing
game.bin does not leave the guard pointed at the Data subfolder.

Owned Release GUI build and build/setup-missing-bank-e1jrqxg_ pass at 1440x900
and 800x600 / 1.5 scale. Actual buttons restore an absent game.bin exactly from
its retained original, preserve all other files and reload the loose map. The
compact case selects Data directly. Both cases also refuse while an owned
sleeping helper named Fable.exe runs in the install root, preserve all bytes,
and succeed after that helper exits. No actual game was launched. Compact
restored screenshot reviewed. Existing Setup success/13-backup/created-preview,
locked failure/retry (build/setup-restore-failure-_ljy3rw2), and install-switch
fixtures pass. The new fixture is wired into check_all. No new full gate or
development archive is claimed; the afe9e55 core checkpoint and 9745443 archive
remain the earlier validation boundaries. Partial restore remains to review.

## 2026-10-03 persistent Setup restore failures and retry

Locked-target baseline build/setup-restore-failure-3cn619bi preserved the LEV
and successfully retried after unlocking in the same process, but Setup had no
durable failure result: only Activity/transient toasts contained the reason.
The new inline wrapped error identifies the target and filesystem failure.
It remains after toast dismissal, clears on successful retry or an accepted
folder switch, and also records existing busy/invalid-install refusals. File
restore/backup policy is unchanged; partial failures retain their error after
the existing map reload. This does not claim an all-file atomic restore.

Owned Release GUI build passes. build/setup-restore-failure-wlsuijho passes
normal/compact actual buttons, failure-message bounds/wrapping with toasts
cleared, exact current/original bytes while locked, temp cleanup, same-process
retry and recovered heights/bytes. Compact also switches folders and verifies
the other install remains unchanged. Compact failed/retried screenshots were
reviewed. Existing Setup success/13-backup/created-preview and install-switch
fixtures pass, with separate app-data directories. The failure fixture is wired
into check_all. Core source/binaries are unchanged from the afe9e55 110-check
checkpoint; no fresh whole gate is claimed for this UI increment. The 9745443
development archive predates this latest message and remains unchanged.

## 2026-10-03 Setup restore after a real asset-bank write

The feature-depth audit asked whether Setup restores a modded definition bank
and refreshes active asset views. Source already retires caches through
scanInstall/startContextLoad; a real mesh-import/Setup workflow now validates
that path without a product-code change. tools/test_setup_asset_restore.py
copies four source banks and creates an owned tiny loose level, imports a cube,
opens its texture/model, then clicks Setup Restore and confirmation at normal
and compact sizes. Model/texture rows each drop by one, the removed model preview
clears, texture selection retires, and the restored barrel shows 96 vertices,
188 triangles and two definition users. All four bank hashes return exactly to
their copied originals; original backups are retained and hash-identical.
Loose-level and source-install bytes stay unchanged.

Primary build/setup-asset-restore-pbveasuo passes both sizes; compact imported/
restored screenshots were reviewed. Earlier harness runs correctly passed the
GUI but exposed two test assumptions: Setup keeps backups (keepBackup=true),
and dump_state logs state rather than writing a supplied filename. The final
fixture checks those actual contracts. build/setup-asset-restore-7m4qmn28 also
passes both sizes against the stripped 9745443 package extracted under the
percent/ampersand path, from its unrelated cwd. This is additional coverage of
that same archive, separate from its original nine-check result.json; no archive
was regenerated or changed. New fixture is wired into check_all and future
package checks. Frozen afe9e55 still supplies the 110-check whole-gate scope.
The remaining Setup audit item is GUI write-failure/retry visibility.

## 2026-10-03 full 110-check checkpoint and saved-choice promotion

Frozen primary source afe9e55 completes tools/check_all.py --no-build with
110 PASS, zero failures and ALL PASS. Evidence:
build/check_all_current_20261003.log (PowerShell UTF-16 log). All available
integration checks and corpora were exercised; no integration check was skipped.
The 291 empty/non-type-1 lipsync records remain a data-scope exclusion, not a
skipped check. This includes the corrected foliage-isolated smoke fixture,
37 core tests, the four recent comparison/search/choice workflows, stock/loose
terrain, world edits, imports, recipes and large mod corpora. Preserve the older
330d4b5 failure report below as historical evidence.

After the gate exited, clean primary received reviewed isolated commits as
d63e6fd (saved-choice review), 7fe93db (empty-order reports) and 7e99017
(package evidence). The combined primary Release build passes. Focused primary
build/mod-unreported-choices-xc4pw1tj passes both panel sizes, exact preservation,
locked-save refusal, individual clearing/expected next-build output, empty-order
reporting and choice 401 access. build/mod-field-details-ro7hxd3b passes both
sizes and independent merged-bank values. Docs commands pass (32 forge,
37 forge-tools) and whitespace checks pass. No core source changed after afe9e55;
the 110-check statement applies to afe9e55, not these later UI/CLI increments.
Isolated source/validation evidence is recorded in the following sections.

The validated 9745443 application/guide archives were copied without overwrite
to primary dist/development/20261003-9745443/. Application ZIP SHA256 remains
7a6ef9eb2ceafa4586a6eae60bedf27f9230b67410992e7ca442547ace93277d.
Its nine extracted workflows and 115 links passed in the isolated worktree;
the archive was built from the promoted code, and its docs record
the package-time checkpoint. No publication or live-game writes occurred.

## 2026-10-03 isolated development package with saved-choice review

Built from isolated source 9745443 after focused validation, with owned Release
GUI/forge/forge-tools binaries. Archive at this worktree's
dist/development/20261003-9745443/FableForge-0.18.0-dev-win64.zip is 34.8 MB;
SHA256 7a6ef9eb2ceafa4586a6eae60bedf27f9230b67410992e7ca442547ace93277d.
The separate illustrated guide ZIP is also generated. Expanded extraction check
build/package-smoke-cbjqubd9 passes all nine runtime workflows plus 115 local
Markdown links and guide images: defs, eight retail maps, GUI pixels/GLB,
literal-path mod operations, field details, actual long CCharString/vector and
CDefString references, 401-conflict search, unavailable contributors and
unreported saved choices including an empty order and failed saves. Executables
run from a path with spaces, percent signs and an ampersand, from unrelated cwd.
App-data for this package test is owned by the isolated worktree.

This package supersedes the earlier 096f7d8 development artifact for current
typed-value/choice workflows; that earlier ZIP remains unchanged. The primary
whole gate at afe9e55 is still running independently. No tag, push, publication
or live-install writes are part of this package check.

## 2026-10-03 empty mod order keeps saved-choice review available

The added empty-order boundary exposed a real gap in the review increment:
mods conflicts rejected zero enabled sources, so removing the final mod left
no loaded report and hid all saved choices. Reproduction is isolated
build/mod-unreported-choices-n4v77tdf. The command now passes an empty source
list through its existing dry-run/report pipeline. Build/deploy behavior is
unchanged; the report is read-only and uses the same summary/asset-check logic.

Isolated Release tools rebuild and build/mod-unreported-choices-q6z0uatp pass.
The fixture now removes both mods, independently checks a zero-source/all-zero
summary, then reaches saved choice 401 through search at both panel sizes.
Exact source hashes and file inventories remain unchanged apart from the
explicitly edited order/choices files. The previous individual clear/locked-save/
next-build tests also pass. Packaged runtime checks now include this fixture;
the next development archive still needs generation/extraction validation.
The primary afe9e55 whole gate remains frozen and running separately.

Additional retail presentation check in this worktree decodes Fireheart entry
10516: MoonFX is "GATEWAY_IDLE_01", SunFX is "FLOURISH_WISP_SHORT_01" at
build/retail_typed_value_10516_20261003.json. This agrees with FableTLC
docs/formats/DEFS.md's native/byte evidence for NUL-terminated CCharString and
confirms the signed-reference correction preserves actual inline strings.

## 2026-10-03 review saved choices outside the conflict report

The frozen primary afe9e55 gate is running separately. This increment is in the
owned D:/Code/FableForge-choice-review worktree, based on that checkpoint.
Previously saved choices absent from residual conflict rows were invisible;
baseline fixture build/mod-unreported-choices-v7g1jwdo confirms missing review
widgets in the primary binary. Other saved choices now lists those entries,
searches target/contributor before its 400-row cap and clears each through the
existing guarded PendingBanks transaction. Choices are snapshotted before
rendering so a successful clear cannot invalidate the loop. Zero-conflict
reports retain the search and review controls; their summary now wraps.

Absence from a conflict report does not prove a choice is inactive. Independent
file-composer evidence shows a single-contributor vanilla choice can suppress
that file despite no reported conflict. The initial fixture expectation was
wrong (build/mod-unreported-choices-mxsehjo4); the final message explains this,
and the fixture independently checks the expected next-build change after
clearing it. No deployment occurs. Existing reported choices remain intact.

Isolated Release GUI/tools builds pass. build/mod-unreported-choices-1hotw3xe
passes normal/compact missing/uncontested review, choice-name search, exact
read-only preservation, individually clicked clears, a locked-choice-file
failure with unchanged disk/display state and no leaked pending directory,
zero-conflict recovery, and 401-choice last-match access at both sizes. Compact
help/long-name screenshots were reviewed at the preceding r3wew08p run; its UI
binary is unchanged. Existing field details (mod-field-details-fmhw_ij7) and
401-file conflict search (mod-conflict-search-au1bzhlo) pass both sizes with
source preservation. Test processes own separate app-data directories.
No new whole-gate/core pass is claimed for this isolated UI increment.

## 2026-10-03 correct CDefString reference presentation

The byte-safe presentation increment avoided a JSON failure but incorrectly
treated CDefString as inline text. Forge's existing defedit encoder identifies
it as a four-byte reference; the locally referenced fable-defs source confirms
signed i32 TablePos in packages/defs/src/wire.rs, with an empty-string -1
sentinel in packages/def-compiler/src/lower.rs::eval_def_string. The formatter
now displays that signed reference, retaining raw hex for malformed widths.
It does not resolve names.bin or change decoding, merging or serialized bytes.
CCharString retains the exact byte escapes introduced earlier.

The former long-value CCarryingDef fixture used artificial inline CDefString
bytes, so its passed checks did not establish native type correctness. It now
uses SPECIAL_ABILITIES_SUMMON_SPELL_DEF with actual Vector_J/CCharString fields
and a separate CDefString conflict (-1 baseline, 256/512 references). Synthetic
reference offsets are display fixtures, not valid name-table/runtime claims.
build/mod-long-values-22ue6tvb passes both sizes, per-value bounds/wrapping,
independently expected merged bytes and unchanged source-bank hashes. Compact
reference and escaped-text screenshots were reviewed. Full Release build and
37/37 CTest pass (21.77s). Retail Sword of Aeons DefaultScriptName/Data now
display -1 at build/sword_aeons_reference_decode_20261003.json; source game.bin
SHA256 remains a0cc28242e6c3fc572143af4ba3cdd93f205d85edfa3fe85e1035e0932e66f2c.
The earlier development ZIP remains unchanged and predates this correction.

## 2026-10-03 local development package and document closure

Local archive under dist/development/20261003-096f7d8 initially failed extracted
link checks: the newly linked content-override proposal pointed to the absent
ROADMAP_1.0.md (build/package-smoke-c0u89imq). The packaging step now follows
reachable local Markdown links from the user guides and copies missing docs/
vendor document/assets/licence dependencies at their relative locations. It
rejects missing, escaping or unsupported dependencies before writing the ZIP.
Existing exported/rewritten pages are retained. Current closure adds eight
documents and checks 34 pages rather than copying unrelated work/build trees.

Development archive dist/development/20261003-package-docs/
FableForge-0.18.0-dev-win64.zip is 34.8 MB, SHA256
a6827a4aafe1f2f589891be17f7be83bc3ae3bb7b589776550e8abc1b624c872.
The separate illustrated guide is also generated. Extracted package smoke passes
at build/package-smoke-sqyw6lyg: 115 Markdown links, guide images, retail defs,
eight exported maps, GUI GLB/pixels and literal-path mods under an extraction
directory containing spaces, percent signs and an ampersand. The field fixtures
now accept --gui/--cwd so the package check also proves schema/value/search/
choice behavior from that unrelated cwd. Expanded run at
build/package-smoke-e7_mdsoq passes all four new feature workflows with the
stripped packaged executables; the archive hash is unchanged. No release/tag/
push/publication or live-game writes occurred. Whole-gate scope remains the
frozen checkpoint recorded below; this is extraction/runtime-resource coverage.

## 2026-10-03 composer-specific baseline aliases

Writer audit found the unavailable-choice increment at 416d23c normalized
base/none/- to vanilla for every conflict family. That was too broad: definitions
reserve those aliases, whereas TNG/text/FSE/file composers recognize vanilla
and otherwise look for an actual contributor. The original baseline-alias GUI
check lacked an independent file-composer assertion and therefore missed this.

Display/availability now use conflict-family-specific baseline semantics. A
whole-file contributor actually named base remains an ordinary source; absent
none falls back and is flagged for review. Definition aliases still display
vanilla and preserve the field-merge scope. Composer/persistence remain unchanged.
Owning Release GUI build passes. Final unavailable-choice fixture at
build/mod-unavailable-choice-3q0_4ck0 verifies a real base contributor's displayed
winner/badges and independently built bytes, absent none's CLI fallback, plus
normal/compact view preservation, search/wrapping and dropdown clearing. The
extended field fixture at build/mod-field-details-n4sw2tua checks base/none GUI
values and independent merged banks for base/none/- as well as vanilla; all
three contested fields restore while the independent fourth survives. Source
bank hashes remain unchanged. No new whole-gate/core run is claimed.

## 2026-10-03 long field comparison validation

Added individual baseline/result/contributor automation anchors so values larger
than one field group's visible area can be reached and assessed separately.
The existing indented TextWrapped layout already handles long values; no layout
rewrite was needed. Tiny schema-aligned CCarryingDef fixtures exercise Vector_J
and CDefString, including exact non-UTF-8/control/quote/backslash escapes. The
later reference-presentation correction above replaces this artificial inline
CDefString coverage with actual CCharString fields and signed references. The
independent writer's scalar path is unchanged; it can now accept raw test values
and explicit definition/field identities for this fixture.

Owning Release GUI build passes. Baseline build/mod-long-values-3y_t1raz confirms
valid CLI values and exact merged payload, but lacks per-value GUI anchors.
Final build/mod-long-values-hflwah34 passes both sizes, each baseline/result/
contributor's wrapping and screenshot bounds, independent expected merged
payload bytes and unchanged source bank hashes. This validates the recently
added comparison view, not native runtime coverage for these artificial values.
The fixture is wired into check_all; no new whole-gate/core run is claimed.

## 2026-10-03 unavailable saved-choice review

The async-order/destination audit found existing active-job guards already block
changes during conflict checks. A different reproduced mismatch remained:
an external saved whole-file winner absent from the reported contributors made
the GUI display RemovedPack and mark both actual contributors as losing, while
the composer independently built PackB's file. Baseline fixture:
build/mod-unavailable-choice-fwurpman.

Display now falls back to the actual contributor (or field-merged result) and
identifies the absent saved choice with ! and an explanation. The choice stays
persisted for review; search includes saved choice names. Baseline aliases
display as vanilla. This uses the last report's contributor membership, not
fingerprints, and does not detect changed content from a still-present source
or choices for records omitted from the residual-conflict report. Composer and
choice-write behavior are unchanged.

Owning Release GUI build passes. Final long-name fixture at
build/mod-unavailable-choice-56zcdwoo passes both sizes: independent composer
output, actual GUI winner/badges, preserved source/baseline/choice bytes, search,
actual dropdown clear and baseline alias handling. Message rectangle bounds and
wrapping are checked; normal/compact captures inspected. Extended field fixture
at build/mod-field-details-9rlx0sac verifies unavailable record choices, actual
field fallback and no false flag for an independent-only contributor. Existing
winner/reset/deploy check passes at build/mod-winner-refresh-66mxvbvo.

Intermediate build/mod-unavailable-choice-n1vzg0v4 caught a new map::at crash
when clearing a choice because the explanation used its pre-combo availability
flag; availability is now rechecked after the action. That recovery path passes.
Earlier 0zvsp8c8/u8c7etd4 captures showed default/available-width wrapping clipping
the message; explicit card-width wrapping fixes it, and the final long-name
geometry assertions prevent recurrence. Docs-command/diff checks pass. The new
fixture is wired into check_all; no new whole-gate/core run is claimed.

## 2026-10-03 independent-only contributor boundary

Extended the field fixture with PackD, which leaves every contested field at
baseline and contributes the same independent RegionDangerLevel edit as PackB.
Its record contribution is present, but field-conflict contributors exclude it
and its contested wins/losses stay zero. Choosing PackD leaves all contested
fields at their actual load-order winners while its independent edit survives;
vanilla still restores only the three contested fields. Independently decoded
merged-bank values verify load order, PackA, PackD and vanilla with source hashes
preserved. Both sizes pass at build/mod-field-details-6f3joc3b; compact fallback
capture inspected. This extends boundary coverage; no implementation/build changed.

## 2026-10-03 field-aware wins/loses badges

Baseline GUI at bdd5955 showed every mod losing the synthetic field-merged
record, because its record winner was the literal "field-merged". Reproduction
script/capture: build/review_field_badges_before_20261003.txt and
build/field-badges-before-20261003.png. The shared badge/state calculation now
counts each actual contested field contributor against the current field winner;
whole-record/file and other families retain their per-row counts. A mod can win
and lose different fields. Tooltips name the affected record/field; independent
auto-merged edits are not counted as conflicts. No composer or choice writes changed.

Owning Release GUI build passes. Extended normal/compact fixture at
build/mod-field-details-s8nvlafc verifies all three mods' win/loss counts under
load order, PackA, vanilla and reset; merged-bank/source-byte checks still pass.
Normal/compact mixed-badge tooltip captures inspected. The 401-file fixture
at build/mod-conflict-search-vs89q84e verifies preserved whole-file counts and
filter independence. Docs-command and diff checks pass. Prior core/whole-gate
coverage remains scoped to the checkpoint below; no repeat whole-gate run.

## 2026-10-03 primary integration checkpoint

Primary feat/editor-ui-shell through 169608b integrates the reviewed isolated
smoke correction, field details, byte-safe string formatting, field values and
conflict search. The pre-existing content-override proposal/links were preserved
through a scoped stash and clean restoration. Its audit now distinguishes the
starting implementation from the delivered increments and remaining backlog.

Frozen 330d4b5 broad gate: 105 passing checks, one failed ui-smoke pixel fixture,
no skipped integration checks. Log: build/check_all_ground_smear_write_labels_20261003.log.
Failure evidence build/ui-smoke-pcd0wpzn has walkability/height differences 0.22/
0.23 against unchanged 0.25 thresholds because foliage occluded the terrain.
The fixture waits for foliage and hides the object/foliage layers for mode
comparisons; dedicated layer workflows retain their coverage. The original
whole run remains recorded as SOME CHECKS FAILED, not relabelled ALL PASS.

After integration: owning full Release build passes, 37/37 CTest targets pass
(22.10 s), corrected ui smoke passes at build/ui-smoke-lvkc987a with GLB/pixel
checks. Field values/search/picks/independent merged bytes pass normal/compact
at build/mod-field-details-qxbzinie; 401-file search/choice/reset/source hashes
pass at build/mod-conflict-search-1pfa029e. Existing saved-winner/reset/deploy
passes at build/mod-winner-refresh-18gk5mpr. Docs-command checks pass (32 forge,
37 tools). Retail Sword of Aeons JSON at build/sword_aeons_decode_20261003.json
parses and source game.bin SHA256 is unchanged. This is focused post-integration
coverage, not a fresh whole-gate run for the newer source. No live-game writes,
runtime validation, publication or native-format coverage expansion occurred.

## 2026-10-03 searchable mod conflicts

Conflicts now have case-insensitive search over identity/label, kind, contributor
names and field names, plus Saved choices only and Show all conflicts. The cap
applies after filtering; a matching row beyond index 399 can now be chosen in
the UI. Counts/no-match messages explain the result. Filtering changes no picks
and resets when the destination changes. Existing row indices/widget identities
remain stable; lip-sync and informational rows retain their existing policies.

Owning Release GUI build passes. Real 401-file two-pack fixture at
build/mod-conflict-search-r50eca8f passes normal/compact real typing, mixed-case
search, contributor search, row-400 vanilla choice and dropdown reset, saved-
choice/no-match filtering and reset. Source mods and baseline bytes are unchanged.
Compact last-row capture inspected. Initial build/mod-conflict-search-chdujmm3
passed normal but missed the first compact checkbox click; revealing it again
after scrolling and asserting the checkbox state fixes the fixture. Field-name
search and unchanged value/merge checks pass at build/mod-field-details-6n_q1npr.
The search check is wired into check_all; no new whole-gate run was claimed.

## 2026-10-03 field conflict value comparisons

The composer report now includes each residual field's type, baseline and
actual contributor values. A numeric part index identifies prefix and repeated
unnamed fields without a name lookup. Core merge behavior and output bytes are
unchanged. The GUI retains optional values (older reports remain supported) and
shows baseline/contributors plus the value selected by the current record pick,
including fallback and vanilla. These are snapshots from the last conflict
check; there is no new per-field policy, source freshness check or decoder.

Owning Release tools/GUI/core test builds pass. Core tests cover prefix and
duplicate unnamed identities and an independent expected merged payload;
the existing core suite also passes (two CTest targets). Expanded synthetic
workflow at build/mod-field-details-dge1kznp passes both sizes, exact CLI
baseline/contributor values, current winner values across picks/vanilla/reset,
and independently decoded output banks/source hashes. Compact last-field capture
inspected: all values fit and every field remains scroll-accessible. Initial
attempt build/mod-field-details-kfl8ul5q passed CLI assertions but GUI launch was
blocked by concurrent executable relinking; completed build was awaited before
the passing rerun. Primary gate remains against its frozen 330d4b5 version.
Final reviewed-source rerun: build/mod-field-details-psnyrym6. The existing
saved-winner/dropdown-reset/deploy check passes at build/mod-winner-refresh-8ek50z_x;
docs-command and whitespace checks pass. The original conflict list is retained
for text reports and row filtering alongside the new JSON comparison details.

## 2026-10-03 byte-safe definition value presentation

An actual `defs decode ... OBJECT_SWORD_OF_AEONS game.bin --json` failed
in the 330d4b5 primary binary with invalid UTF-8 byte 0xff. `formatValue`
copied CCharString/CDefString bytes directly into a display string; strict JSON
serialization therefore rejected some valid game records. Presentation now
escapes quotes, backslashes, newline/carriage-return/tab and remaining control
or high bytes as exact ASCII byte escapes. NUL termination and printable ASCII
remain unchanged. This infers no encoding and changes no payload or writer.

The new fableforge_defdecode_tests first reproduced the failure for both string
types, then passed explicit escapes/termination, all non-NUL bytes and strict
JSON round-trip checks. The owning Release tools/GUI build passed. Retail
Sword of Aeons now produces parseable JSON at isolated
build/sword_aeons_decode_20261003.json; the scratch source game.bin SHA256 is
unchanged. This is formatting evidence, not an expansion of decoder coverage.

## 2026-10-03 isolated continuation: inspect mod field conflicts

Owned checkout D:/Code/FableForge-field-review, branch
feat/forge-field-conflicts-20261003 from 330d4b5. Primary Forge's broad gate runs
against its fixed source/binaries; no primary code or game-install files were
changed by this tranche. The primary content-override proposal's source audit
identified that composer reports already contain field conflicts, but the UI
dropped those rows and left the scope of a record choice unexplained.

The Mods card now retains/expands those field names, actual contributors and
current winners. Record choices resolve contested fields; independent edits
still merge. A chosen mod only wins fields it changed. A field that the selected
mod did not change displays its load-order winner and an explanation. Vanilla
is labelled for conflicting fields. Clearing a choice recomputes winners from
actual contributors, avoiding a stale cached report. Existing persistence and
composition are reused. This does not implement individual field choices,
baseline/contributor values, component policies or source fingerprinting.

Clean owning Release configuration/build completed; source root in CMakeCache
is this checkout. Baseline field fixture at build/mod-field-details-jct8f7ik
confirmed three real CLI field conflicts but failed the missing UI states and
widgets. Final candidate at build/mod-field-details-xddy71xf passes normal/compact
controls and scrolling, picks/vanilla/reset/recheck, plus independently decoded
merged-bank values for load order, PackA and vanilla. The fourth, independent
field remains merged even with vanilla chosen. Baseline and all source mod
banks remain byte-identical. First candidate run: build/mod-field-details-8zjnemnt.
Normal/compact expanded and fallback captures inspected; every field is
scroll-accessible. The actual vanilla dropdown selection is tested and its
compact capture inspected. Earlier complete scrolling run:
build/mod-field-details-n1pw0p4y. The test is wired into check_all and documented
in the guide. Docs-command and diff checks pass after building this lane's forge.

Existing winner-reset/deploy and locked-add/remove tests pass at
build/mod-winner-refresh-k2yh3b6q and build/mod-pick-recovery-zcrmz01c. External-
edit/locked-read preservation passes at build/mod-pick-refresh-0u_374eb.

Separate fixture correction: tests/ui/smoke.txt waits for foliage completion
and hides objects/foliage during terrain colour-mode comparisons. The unchanged
25% pixel threshold was obscured by trees in the primary gate's failing captures;
both Height and Walkable actually rendered correctly. The revised script passes
on the primary 330d4b5 binary (build/ui-smoke-m2lgqnz_) and this checkout's baseline
binary (build/ui-smoke-af2pztar) and final field UI binary (build/ui-smoke-jsnyxhws);
core CTest 1/1 passes here. Original primary
failure remains in its broad-gate log. No full-gate result is reclassified.

Next: integrate these reviewed commits after the primary broad gate completes.

## 2026-10-03 continuation: readable write actions at compact sizes

Ground Smear was committed as 0d1161e. Its compact captures exposed clipped
combined-write text, especially with long pack names. Footer write actions now
wrap their labels and grow vertically to fit. Confirmation rows likewise wrap
Yes/Cancel and share the larger required height. Default button rendering stays
unchanged outside callers requesting wrapping; hidden ImGui ID suffixes remain
hidden. Existing actions, enabled-state handling and confirmations are reused.

Full Release rebuild succeeded with cmake --build build -j 2. Final normal/
compact visual checks at build/write-labels-gpbbo7_y pass game and long-pack
labels, increasing button heights, real confirmation clicks and Escape. Both
layouts and compact confirmation captures were inspected; all captions fit.
Earlier captures before confirmation wrapping: build/write-labels-07tjak0w.
The owned review helper is work/review_write_labels_20261003.py.

Final success/failure/keyboard checks at build/write-routes-apm765ez pass all six
cases: write_both, write_both_fail and confirm_escape at 1440x900 / scale 1 and
800x600 / scale 1.5. Failed terrain writes preserve the scratch WAD; Escape
preserves selection and dirty state, and Enter does not confirm. Helper:
work/review_write_routes_20261003.py. Earlier tools/test_overworld.py --keep
passed relocation, terrain, combined-write/failure, theme and custom-theme
workflows on its owned build/ui_overworld_install copy.

Next: run the broad gate on this completed source. Content-override source audit
is recorded in docs/modding/CONTENT_OVERRIDES.md; its next useful increment is
showing existing field-conflict details and explaining choice scope. No live
game mutation, package refresh, push or release performed.

## 2026-10-03 continuation: Ground Smear validation

Resumed at the user's request to continue Forge work until stopped. Active
checkout D:/Code/FableForge, feat/editor-ui-shell at 463893a. Preserved the
uncommitted content-override proposal and the separate FableTLC lanes.

Ground Smear's existing WIP uses a frozen 3x3 neighbourhood per application,
merges eligible palette weights, retains three distinct strongest themes and
projects strengths to exactly 255. Empty/NULL slots are excluded; an empty
eligible neighbourhood is a no-op. Radius is truncated with a minimum of 1,
using the strict vertex disc; no target, Strength or elapsed-time input is
required. Repeat applies on held frames; the stroke remains one undo step.
This is corrected Forge behavior, not native duplicate/overflow compatibility.
Native evidence and the 610 selection-block comparisons remain recorded in
the historical bedtime section below. Neighbouring maps are outside its scope.

Reviewed the passability serializer guard: theme-only changes preserve camera
bytes rather than imposing walkability's OR; explicit passability changes retain
the existing saver rule. Added an independent full-LEV byte comparison: only
the three theme indices and two stored strengths change, including on a fixture
with walkable=true/camera=false. Existing adverse, frozen-neighbour, uniform
no-op, channel preservation and Undo/Redo tests pass.

Release targets FableForge, forge-tools and fableforge_tests rebuilt.
ctest --test-dir build --output-on-failure: 36/36 passed, 153.32 seconds.
tools/test_ground_smear.py now uses owned stock/loose copies at 1440x900 / scale 1
and 800x600 / scale 1.5; checks real clicks, repeat, 1/9-vertex cursors, exact
blends, undo, draft reopen and unchanged install hashes. First complete run:
build/ground-smear-o65fq577, all four workflows passed. Loading notices were
cleared for later visual captures; final run build/ground-smear-u782iq_x passes
all four workflows and both layout captures were inspected.
Normal and compact controls/cursor captures inspected. Scratch fixtures omit
graphics.big, so placed mesh/foliage preview is not exercised by this harness.

Walkable/Blocked/CameraPass/CameraBlock UI checks pass both sizes on the loose
copy at build/passability-cursor-4583lg97 and the stock copy at
build/passability-cursor-kn_aue45: rounded pens, Shift, channel preservation,
cursor and exact undo. Docs-command and diff checks pass. Smear is wired into
check_all and documented in the editor reference and painting guide.
No broad gate, in-game test, package refresh, push or release claimed yet.
Next: fix the compact combined-write label exposed during visual review, then
run the broad gate on the completed changes.


## 2026-10-02 bedtime checkpoint: stopped at user request

The user asked to update docs and stop for tonight. No further feature work is
running. Last completed implementation commit is 38d9056. This continuation
committed snapshot/session recovery, Space-held brush pinning (c8d0ef3), the
camera FOV control (cdecfab), per-map camera/render-mode retention (b132d50),
the whole-map fixture correction (91615e9), and compact help wrapping (38d9056).

The latest broad gate remains the frozen b132d50 run: **104 PASS, 1 FAIL,
0 skipped integration checks**, including all 36 passing CTest cases. The one
whole-map UI fixture failure was corrected and its complete workflow passed
stock/loose at normal/compact sizes. No full gate was rerun afterward; do not
reclassify the original result. Details and evidence are below.

### Ground Smear is preserved as uncommitted work in progress

Modified paths: gui/app.cpp, gui/app.hpp, gui/automation.cpp, gui/editor.cpp,
libs/forgecore/include/forge/terrain.hpp, libs/forgecore/src/terrain.cpp,
src/leveledit.cpp, src/leveledit.hpp, tests/test_export.cpp; new untracked
harness tools/test_ground_smear.py. These edits are deliberately outside the
completed implementation commits. Primary Release binaries currently include
this WIP and must not be treated as binaries built from stable HEAD.

The proposed Forge behavior blends a frozen 3x3 neighborhood, merges palette
weights, chooses three distinct eligible themes, and projects exactly 255 total
strength. Empty/NULL theme slots are excluded; no eligible weights is a no-op.
It uses the existing integer, minimum-one-radius, strict-disc footprint. The UI
adds Paint/Smear chips, no target requirement for Smear, and optional Repeat.
This is corrected Forge behavior, not a claim of native bug compatibility.

Native investigation executed the actual x86 selection/normalization block
0297c94e..0297cd32 with narrowly stubbed lookup calls in Unicorn: 305 cases with
identity and reversed global IDs, **610 comparisons passed** against the literal
model. The native algorithm can select duplicate classes and overflow raw
strengths; its second pass reads their low bytes. This probe does not verify the
full native neighborhood loop or live-game behavior. Evidence is owned scratch
build/theme_smear_native_probe.py and build/theme-smear-emulated-probe.txt;
static exports are build/theme-smear-native-full.txt,
build/theme-smear-native-evidence.txt and build/theme-smear-offsets-native.txt.
FableWin SHA256: A9D6D0977D9D7FC8242DA4292AE7ED92E926CA80364A95C003D9B9F689E41845.

The Release rebuild completed. Final bedtime check of the current WIP:
ctest --test-dir build -R '^fableforge_tests$' --output-on-failure passed (1/1).
It includes frozen-neighborhood projection, no-op/malformed-input cases,
independent saved-LEV reads, unchanged neighboring arrays/TNG, and exact Undo.
The latest serializer guard preserves untouched camera-passability bytes for
theme-only edits while retaining existing OR behavior when passability changes.
That guard needs focused UI review before promotion.

The stock Smear GUI harness passed normal/compact layouts against the preceding
serializer iteration (build/ground-smear-gwfokm4x). It has not been rerun against
the final guard; loose-install Smear checks and visual review are outstanding.
The harness is not wired into check_all, and user-facing documentation/release
notes have not been updated for this unfinished feature.

On resumption: review the passability guard, run focused passability and Smear
GUI checks on stock/loose at both sizes, inspect screenshots, document the
corrected contract, wire the harness into the gate, and commit only once ready.
Do not infer a clean broad gate or retail equivalence from these focused checks.
No push, merge, release, or live-game mutation was performed.


## 2026-10-02 follow-up: readable compact shortcut sheet

Visual review of the new shortcut entries exposed an older fixed-column issue:
the help sheet clipped descriptions horizontally at 800x600 / scale 1.5. Groups
now use full-width two-column tables with wrapped keys and actions, stacked in
one scrollable sheet. Normal and compact captures show readable camera/object
rows and the full bottom section including Ctrl+B, panel controls and Close.
The editor fixture reveals Close before clicking, so it also handles taller
help content without depending on its initial viewport position.

Release rebuild, normal/compact help open/reveal/close and the full editor
workflow pass (build/help-wrap-7nt4f951). Top and bottom compact screenshots
reviewed. Docs-command and diff checks pass. Text/layout only; the preceding
104-pass/one-fixture-failure full run is not reclassified as a clean full gate.



## 2026-10-02 checkpoint: pinning and per-map camera views

Frozen source b132d50 / primary Release binaries: tools/check_all.py --no-build
completed with **104 PASS, 1 FAIL, 0 skipped integration checks**. Evidence:
build/check_all_pin_camera_20261002.log (PowerShell UTF-16 capture). All 36 CTest
cases pass, all available mod corpora were exercised, and all new pin/session/FOV
checks pass. Lipsync's 291 empty/non-type-1 records remain internal exclusions,
not skipped integration checks. Source/binaries stayed fixed through the run.

The one failure was tests/ui/wholemap_heights.txt clicking the whole-map toggle
below the visible panel after the new terrain hint moved it down. The test now
uses the existing reveal command to scroll before clicking. The actual window's
Raise-with-objects, Scale, Set and exact Undo assertions pass on stock/loose,
normal/compact layouts; evidence build/wholemap-reveal-cc9tf3o7. The full gate was
not rerun after this focused fixture correction; its initial failure is retained.

F1 help now includes Space pin, Ctrl+B snapshot, sculpt Ctrl-drag height sampling
and fixed-point -/= keys. Compact help open/reveal/close checks pass in the same
evidence directory, and its screenshot was reviewed. No core behavior changed
in this follow-up. No live-game runtime verification, push, merge or release.



## 2026-10-02 continuation: retain each map's view

Returning to a visited map now restores its complete camera (eye, yaw/pitch,
focus distance, FOV and fly speed) and Textured/Wireframe/Walkable/Height mode.
A new map still uses normal framing. Texture arrivals and terrain rebakes keep
the active view. Capture happens only for the selected, loaded document and
matching uploaded preview; an old camera is never attributed to a pending map.
Map views persist as settings metadata scoped to the current install. Accepted
install switches clear them; a different startup install ignores them. Default
scripted runs still skip settings I/O, while in-process map revisits retain views.

Camera JSON validation is shared with the previous session reader. One malformed
view is ignored without dropping valid peers or unrelated settings. Missing
last-map keys still allow surviving views to be used on later map selection.
The older single-camera record remains readable. This per-map convenience extends
native current-view SaveSettings behavior; no native per-map-library claim.

Expanded tools/test_session.py passes stock/loose at normal/compact sizes;
final evidence build/session-ui-ii2y2a5p / build/session-ui-k2s6qjx4. It verifies
camera/FOV/render-mode round trips, exact camera retention after restart,
first-visit framing, malformed legacy and per-map records, surviving valid peer
views, missing last maps, install isolation and the prior FOV input checks.
Core unchanged. Named bookmarks and World-tab session state remain open.



## 2026-10-02 continuation: map-camera field of view

View > Map camera now provides a vertical FOV slider in degrees (30-100),
Ctrl+click typed input with the same bounds and reset to Camera's 0.9-radian
default. The submenu is disabled outside a loaded map view. Existing map-scoped
session settings persist the chosen angle; no map-document writes are involved.
Automation registers menu_view, menu_map_camera, slider_camera_fov and
button_camera_fov_reset, with camera_fov_degrees state.

Native ConsoleEditSetCameraFOV 02053730 accepts one integer and calls
SetCameraFOV 0204f3a0. That divides the value by the confirmed double constant
360.0 at 0402a5e8 before CCamera::SetFOV, using native turn units. Forge's renderer
uses radians and converts the degrees control accordingly; no native FOV range
or camera-point serialization equivalence is asserted. Read-only exports:
build/camera-fov-native.txt and build/camera-fov-setter-native.txt; FableWin identity
matches the preceding pin investigation.

Expanded tools/test_session.py checks both slider limits, reset, typed 82-degree
input, clean object/terrain document state and exact process-restored camera,
plus prior invalid-camera/missing-map/install-isolation cases at both sizes.
Final stock/loose evidence: build/session-ui-msr1_i_h / build/session-ui-oaot1_3p.
Compact menu screenshot reviewed. Core unchanged; no new full-gate claim.
Camera bookmarks, per-map view libraries and World-tab session restoration
remain separate follow-ups.



## 2026-10-02 continuation: hold Space to pin terrain brushes

Space now latches the last valid map ground point in terrain mode and skips the
moving ray until release. Painting follows that point through camera movement
and viewport drags. The cursor shows an amber pin and "Pinned (Space)". No prior
hit means no pin. Escape, Ctrl/Alt, text/popup context, tool/map changes and leaving
terrain editing cancel it; Copy/Paste modes consume Space without pinning.
Existing Shift inversion and theme replacement remain available. This deliberately
uses Space rather than vanilla Shift; Alt already owns viewport orbit.

Read-only FableWin SHA256 remains A9D6D0977D9D7FC8242DA4292AE7ED92E926CA80364A95C003D9B9F689E41845.
ProcessInput 0294dbe0 checks held Shift; Stick 02991680 writes centre+af8;
GetCurrentPointedAtPos 0204b2d0 returns centre+8dc without a ray while stuck.
Update 02035ac0 refreshes that stored point after successful unpinned hover.
Exports are build/terrain-pin-native.txt, terrain-pin-map-native.txt and
terrain-pin-update-native.txt. Forge keeps map-local XY and resamples cursor
height; native stores XYZ. No native runtime equivalence claim.

Release GUI rebuild and tools/test_brush_pin.py pass on stock/loose installs,
each at 1440x900 and 800x600 / scale 1.5. Evidence: build/brush-pin-pgm0wzlf
and build/brush-pin-7aq9u_t6. Assertions distinguish pinned from moving-ray
height/shore writes, Shift inversion, no-hit, release, Escape/Ctrl cancellation,
clipboard/tool/map changes and exact undo. Compact screenshot reviewed.
Input ownership prevents Space activating a nav-focused viewport control;
physical key state preserves the pin when a drag claims keyboard ownership.
Existing controls and selection_actions scripts also pass against the final GUI
(build/brush-pin-final-controls.log and brush-pin-final-selection_actions.log).
Docs-command, Python compile and diff checks pass. Core code is unchanged.
The harness joins check_all; the 104-check full gate remains scoped to a987019.


## 2026-10-02 follow-up: isolate scripted snapshot browsing

Scripted runs now need an explicit owned recovery root before either manual
snapshot menu item can act. The library also closes and clears pending Delete
when the root is absent or its tool context closes. This matches the existing
capture and rotating-archive isolation guards and avoids implicit app-data
history browsing by default scripts.

Release GUI rebuild and expanded snapshot harness pass stock/loose at both
sizes (`build/snapshots-ui-aeb70h4f` / `build/snapshots-ui-j9f0e4z7`). The new
unconfigured-script case checks Ctrl+B, both menu paths, no worker and no open
library. All prior owned capture/restore/delete cases pass. Core unchanged;
the full 104-check checkpoint remains scoped to source a987019.


## 2026-10-02 full checkpoint: snapshots and restored sessions

Frozen source `a987019`, primary Release binaries: `tools/check_all.py --no-build`
completed ALL PASS / exit 0 with **104 PASS, 0 FAIL, 0 skipped checks**.
Log: `build/check_all_snapshots_session_20261002.log`. All 36 CTest cases pass.
The two new dated-snapshot and session harnesses run in the broad gate; focused
stock/loose evidence remains in their preceding entries. Coverage includes all
terrain/draft/recovery/Budget and editor UI workflows, retail exports/dialogue,
scratch new levels/world moves, textures, imports/recipes, loose installs,
backups/restore/install switching and level/world pack writes.

All available mod corpora were exercised: Aeon + Controller Support, Freeroam,
Project Seasons + AlbionSecrets, Dragon Cliff, Lost Content and Expanded
Chapters. Lipsync checks 20,214 entries byte exactly; its 291 empty/non-type-1
records are exclusions within an exercised check, not skipped gate checks.
Source and binaries stayed fixed for the entire run. No live-game runtime check,
push, merge, deployment or release was performed.

While the gate ran, read-only native brush-pin evidence was exported to
`build/terrain-pin-native.txt`, `terrain-pin-map-native.txt` and
`terrain-pin-update-native.txt`; findings are in `build/terrain-pin-findings.md`.
Update 02035ac0 refreshes centre+8dc from successful pointed positions when
+af8 is false; Stick 02991680 only sets that flag; GetCurrentPointedAtPos
0204b2d0 returns the stored point without a ray cast when stuck. Binding remains
an implementation choice: Alt inversion would conflict with Alt-drag orbit.
An owned incomplete snapshot-directory probe passed without hiding valid points
(`build/snapshots-ui-tcxgd2d2/1440x900/partial-directory-probe.txt.log`).


## 2026-10-02 continuation: restore the last map and camera session

Normal startup now reopens the last known map in the same install, remembers
View/Edit, editing tab and terrain tool, and applies its saved camera once after
preview upload. Camera state includes position, yaw/pitch, distance, FOV and fly
speed. Further rebakes retain the restored view. Missing maps and different
installs skip restoration; malformed/nonfinite or invalid camera values are
ignored without dropping unrelated settings. A failed document open does not
restore Edit. Session data is metadata only and does not save map edits.

Automated runs still ignore ordinary settings. Explicit absolute
FABLEFORGE_TEST_SETTINGS opts a script into an owned settings file for startup
and exit; main calls saveSettings in both modes and that method enforces the
isolation guard. Recovery remains separately isolated through its existing
owned-root controls. No normal user settings were read or written by the tests.

Release GUI build passes. tools/test_session.py exercises process capture/reopen,
exact complete camera equality, tab/tool/Edit restoration, default automation
isolation, malformed camera fallback, missing map and install mismatch, and
switching to another map with a fresh camera. Stock and loose sources pass both
sizes in `build/session-ui-06gwo595` / `build/session-ui-wd8y1p4i`.
Dated snapshot regression passes both sizes in `build/snapshots-ui-tcxgd2d2`;
controls pass in `build/session-controls-regression.log`. Documentation-command,
Python syntax and diff checks pass. Core and file formats unchanged; no broad
gate rerun or in-game claim.


## 2026-10-02 continuation: dated current-map snapshot library

File > Recovery snapshots > Create dated snapshot / Ctrl+B freezes objects and
terrain to an owned app-data restore point. Saved snapshots lists times newest
first, with Restore and confirmed Delete. The library is scoped by install,
map, world file and save destination; points survive draft saves and restarts.
Restore uses the existing validated archive and full-state undo/redo. Cancel
preserves both history and the rotating recovery archive. Deletion removes only
the selected archive and its empty reserved directory. Escape cancels the
confirmation before closing the tool. World-tab, dialogue and asset changes
remain outside these current-map captures.

Capture uses the existing background recovery worker and publication helper;
active strokes/batches/file jobs defer capture. Exclusive directory reservation
keeps captures separate. Timestamped directory names provide deterministic
newest-first ordering for filesystem times tied at Windows stat precision.
The GUI's same-label Delete/confirmation ID conflict was found and fixed during
testing; automation now recognizes B to exercise the actual shortcut.

Release GUI build and tools/test_snapshots.py pass on stock and loose sources,
normal 1440x900 and compact 800x600 at scale 1.5. Evidence:
`build/snapshots-ui-4nzs843t` / `build/snapshots-ui-n1rqrefk`.
Checks cover both capture paths, process reopen, both restore states through
undo/redo, Save draft retention, Cancel preserving recovery, Escape cancellation,
confirmed single deletion and map-switch reset. Compact layout was visually
inspected. Existing recovery stock checks pass both sizes in
`build/recovery-ui-5mrpfd5y`; controls and selection actions pass in
`build/snapshots-<name>-regression.log`. Python syntax, docs commands and diff
checks pass. The broad gate now includes this harness; it has not been rerun,
and the prior 102-check result retains its frozen-source scope. Core/archive
format unchanged. No live game writes, push or deployment.


## 2026-10-02 continuation: restore a snapshot in the open map

File > Recovery snapshots > Recover latest snapshot opens the same recovery
prompt without closing the map. Snapshot now can therefore serve as a deliberate
restore point. The prompt shows the archive's local capture date/time. Recover
remains one undoable edit; undo restores the later object/terrain state, and redo
returns to the capture. Snapshot now replaces the one rotating archive; this is
not a dated history library. Open world/level retain their leading File-menu
positions, and interval labels use normal singular/plural wording.

Primary Release GUI rebuild passes. The expanded recovery harness clicks the
actual File/submenu/Snapshot now/Recover latest controls, changes heights and
object positions afterwards, checks capture-time visibility, and verifies both
states through undo/redo and final Save draft cleanup. Stock and loose copies
pass at both sizes: `build/recovery-ui-h6vcxdvr` / `build/recovery-ui-yihfmc26`.
The compact dated prompt was visually inspected. Existing controls and selection
UI regressions pass (`build/recovery-menu-controls-regression.log`,
`recovery-menu-selection_actions-regression.log`). Documentation-command and
Python syntax checks pass. Core and archive format are unchanged; the prior
36-test result and scoped 102-check checkpoint retain their stated source scope.
No live game writes, native runtime comparison, push or deployment.


## 2026-10-02 continuation: recovery publication and exact bytes

Recovery now stages its complete archive in one owned sibling directory and
publishes by one same-volume replacement. Windows uses MoveFileExW with
REPLACE_EXISTING / WRITE_THROUGH and no COPY_ALLOWED fallback; POSIX uses rename.
The old archive stays at its public name until replacement, and a locked-file
failure retains its bytes. The recovery LEV changes a camera byte only when its
boolean differs from the authored channel, preserving an untouched 0x80 true
byte. Timed capture quietly defers batches/gizmo edits as well as brush strokes.

Standalone local Windows probe (`build/recovery-atomic-static-probe.txt`):
500 replacement attempts with a concurrent reader produced zero missing-name
or partial-data observations. 298 attempts were rejected with Win32 5 under
reader contention; the previous archive remained available. The previous
two-rename pattern completed 500 writes but exposed 7,441 missing-name reads.
These are local filesystem observations, not power-loss durability guarantees.
The initial dynamically linked probe had loader error 0xc0000139; a standalone
static build resolved it. Primary Release binaries had no loader error.
Microsoft's [MoveFileExW documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)
provides flag semantics. [ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew)
without a backup has documented failure cases that remove the replaced name;
that operation was not selected as a fallback.

Focused core tests pass exact unedited LEV payload bytes, preserved noncanonical
camera state after edits, an actual Blocked edit with its original navigation
baseline, locked publication failure followed by a successful later replacement,
recovered later heights, and active-batch/stroke refusals. The first focused
rebuild exposed Windows near/far header macros and an ambiguous JSON binary
comparison in the new tests; both are corrected. Full primary Release rebuild
passes. Stock and loose GUI recovery harnesses pass at both sizes:
`build/recovery-ui-kcgq0rh_` / `build/recovery-ui-_qmacr6n`. All 36 CTest cases and
documentation-command checks pass after the refinement. The broad 102-check gate below
covers source 22cbbd6 before these focused refinements. No live game writes.


## 2026-10-02 full checkpoint: Budget and crash recovery

Frozen source `22cbbd6` and primary Release binaries passed
`python -u tools/check_all.py --no-build`: 102 PASS, zero FAIL, exit 0 / ALL PASS.
No checks were skipped. Evidence: `build/check_all_recovery_20261002.log`.
All 36 CTest cases, new Budget/recovery harnesses, draft/terrain tools, retail
and GUI workflows, world moves, loose/tall levels, imports, recipes, pack writes,
backup/restore/install switching and documentation checks were exercised.
Aeon/Controller, Freeroam, Seasons/AlbionSecrets, Dragon Cliff, Lost Content and
Expanded Chapters corpora all ran. Lip-sync empty/non-type-1 entries are corpus
exclusions, not skipped checks; 20,214 eligible entries remain byte exact.

Source and binaries stayed frozen for the whole run. The publication review
was confined to ignored scratch (`build/recovery-publication-review.txt`,
`build/recovery-atomic-static-probe.txt`). Follow-up: replace the recovery
archive with one filesystem operation instead of two renames, preserve an
unchanged noncanonical camera byte, and strengthen the navigation-baseline
fixture. These refinements are not covered by this checkpoint yet. No live game
write, native runtime comparison, push, release or deployment was performed.


## 2026-10-02 continuation: timed crash recovery

Automatic recovery snapshots default to every five minutes while the current
map has unsaved authoring edits. File > Recovery snapshots offers an on/off
control, 1/5/10/30-minute intervals and Snapshot now. The steady clock drives
the interval; strokes, file jobs and a pending recovery decision defer it.
A private Document::recoveryWriteSnapshot copies objects, world origin and LEV
before launching the worker; later edits and map switches cannot enter its data.

Archives live under the settings directory's `autosave` folder, separated by
canonical install identity and map/world/save-root identity. Game files and
saved drafts remain untouched. Opening a map with an archive offers Recover /
Discard. Recover applies objects and terrain as one undoable edit, refreshes
foliage, and leaves authoring edits pending. Failed recovery retains the archive.
Save draft removes it only after success. Object-only/game saves remove it once
all authoring work is saved; pack exports retain it. Explicit Discard removes it.
Close/Discard waits for an active recovery worker so it cannot recreate a retired
archive after the UI exits. Closing a pending recovery decision preserves it.
Manual Snapshot now uses the rotating recovery archive; a dated snapshot library
and whole-world snapshots remain separate backlog work.

Automation stays isolated: no automatic timer or user-data archive access unless
explicitly configured with an owned recovery root; the timer and modal have
separate test opt-ins. `tools/test_recovery.py --install <root>` passes normal and
compact sizes on stock and renamed-WAD copies. It tests new-process reopen,
real Recover/Discard buttons, private object/terrain capture during later edits,
exact document/height undo and redo, enabled/disabled elapsed timers, successful
save cleanup, and failed joint save preservation of archive bytes and dirty state.
Evidence: `build/recovery-ui-xq_2rjba` (final stock),
`build/recovery-ui-ldkjmhn9` (loose).
The compact modal was visually inspected (`build/recovery-ui-b53u0_vc`).
Full Release rebuild and all 36 CTest cases pass, including the new frozen
Document snapshot test. Documentation-command and Python syntax checks pass;
existing Budget, controls and selection-action suites pass with hidden GUI windows
(`build/recovery-budget-regression.log`, `recovery-controls-regression.log`,
`recovery-selection_actions-regression.log`).
The earlier 100-check broad gate predates Budget/recovery. No native runtime
comparison, live game write, push, release or deployment was performed.

## 2026-10-02 recovery archive foundation (timer/UI still pending)

Document::saveRecovery writes one staged CBOR archive containing TNG text,
serialized LEV bytes and navigation-baseline bytes. It does not clear object,
authoring or game dirty state, write game files, patch navigation or bake STB.
The recovery LEV preserves independent camera flags, reversing the game saver's
walkability OR on a private copy. Sound names, environment, shore and palettes
travel with the LEV; unknown LEV bytes use the existing preservation writer.
Document::recover validates version, name, world origin, map UID/grid and nav
baseline before replacing the document. It retains original save baselines,
invalidates stale terrain workers and adds one undo step. Recovery undo/redo
stores private LEV/navigation copies so sound-list replacement is reversible;
ordinary edit snapshots keep their compact representation. This is an editor
archive format, not native backup filename compatibility.

Native evidence (same FableWin SHA256 as the Budget section):
`build/recovery-native.txt` confirms CreateBackupFile 0204b690 sets backup mode,
calls SaveAllLevels(1,0), then clears it. SaveSingleLevel 0296e150 appends the
strings at 04349e0c/04349e14; `build/recovery-native-strings.txt` independently
reads both as `.backup`. The Update slice beginning 0296a8ba increments the
counter, triggers at 0x34bc / 13,500 frames, resets it before checking enabled,
and saves changed editable maps under names with `_a` inserted before the dot.
This slice is not the complete Update function. No native runtime was used.

The focused core executable passes recovery of placed objects, heights, shore,
sound names/cells and walkable cells with authored Camera no; exact sound-name
undo/redo; preserved game bytes and dirty baselines; stale-worker rejection;
wrong-map, malformed navigation and truncated archive rejection; blocked
publication; and active-stroke refusals. Full primary Release build, all 36
CTest cases and documentation-command checks pass.
Timed background saves, install-scoped archive paths, lifecycle cleanup and
Recover/Discard UI are still pending. No user-facing crash recovery is claimed.


## 2026-10-02 continuation: Budget area survey

Budget now has an Areas scope. Pick areas in view temporarily hides the report;
drag boxes, Shift to accumulate, or click an object, then Done returns to the
report. Done/Clear and the hint fit below the map title at both window sizes.
Escape clears all picks. The picker suppresses selection, gizmos and terrain
strokes while retaining camera navigation. Changing maps clears the survey.

Boxes use minimum-inclusive, maximum-exclusive bounds and normalize reverse
drags. Object membership is captured by stable UID when an area is added;
moving an object into or out of an old box does not recapture it. A matching
press/release object is added independently of the box, with no invented click
distance threshold. Local detail is filtered against the union of boxes.
Reports refresh after document edits. This remains a current-map bank survey;
LOD0/texture estimates are not native live-renderer primitive statistics.

Read-only FableWin.exe SHA256:
`A9D6D0977D9D7FC8242DA4292AE7ED92E926CA80364A95C003D9B9F689E41845`.
Exports `build/budget-area-native.txt`, `budget-area-filter-native.txt`,
`budget-area-boundary-pick-native.txt`, `budget-area-input-native.txt` and
`budget-area-cost-native.txt` cover Press/Held/Release 0204f4c0/0204f570/0204f5a0,
AddThing 02051180, AddArea 02051310, ContainsPoint 01f95410 and ProcessInput
0295c650. `build/budget-area-boundary-static-probe.txt` independently checks
nine boundary/interior/outside cases and an empty degenerate box. No native
runtime comparison or live game write was performed.

Primary Release GUI rebuild passed. `tools/test_budget_areas.py` passes at
1440x900 and 800x600 / 1.5 scale: half-open/reversed/degenerate rectangles,
captured membership after moves, real drag/Shift/click/Clear/Escape, overlays,
map reset and unchanged document, terrain and camera during picking. Final
fixture: `build/budget-areas-yq7ke415`. Existing budget, controls and selection
UI suites also pass (`build/budget_area_validation.log`). These focused checks
follow the frozen 100-check checkpoint below; that gate predates this feature.

## 2026-10-02 full checkpoint: drafts and survey tools

Source `3351548` passed `python -u tools/check_all.py --no-build`: 100 PASS,
zero FAIL, exit 0 / ALL PASS. No checks were skipped. The 291 empty/non-type-1
lipsync entries are corpus exclusions; 20,214 eligible entries are byte exact.
Evidence: `build/check_all_drafts_20261002.log`. Source and primary Release
binaries stayed frozen throughout the gate; Budget area investigation/candidates
were confined to ignored build scratch.

This covers all 36 CTest cases, environment Smear, shore/passability pens,
authoring drafts and existing retail/UI, imports, world moves, loose/tall levels,
pack recipes/writes, backups/restore/install switching and documentation commands.
Aeon/Controller, Freeroam, Seasons/AlbionSecrets, Dragon Cliff, Lost Content and
Expanded Chapters were all exercised. The full draft harness is at
`build/terrain-drafts-xecfi99m`; its direct-pack assertion explicitly confirms
`terrain_draft_dirty` remains 1 when unsaved authoring work is exported to a pack.

Additional focused regressions passed after the broad gate:
`tools/test_terrain_async.py` on stock and loose copies, including the new
`save_draft_refused` during a file job. Later edits, switches, repeated writes,
full height/theme pack deltas and snapshot-byte equivalence all pass. Evidence:
`build/terrain_draft_async_checks.log`, `build/terrain-async-sea358zb` (stock),
`build/terrain-async-hp9h49t1` (loose). The real terrain-only Save-on-close button
also passes: doc_dirty 0, authoring terrain dirty, Save work closes successfully,
new-process reopen restores the draft, active LEV/WAD/STB hashes stay unchanged,
and Restore removes it exactly (`build/terrain-draft-close-check`).

No live game files were written, and no native editor runtime comparison,
release, push or deployment was performed. Budget area authoring was subsequently completed above; timed crash recovery
remains separate backlog work.


## 2026-10-02 continuation: terrain authoring drafts

Save draft, Ctrl+S and F6 now save dirty objects and terrain as one staged
transaction; F7 saves and reopens the current document. Terrain-only work can
be saved from the footer or map-switch/close prompt. A successful authoring save
clears `terrain_draft_dirty`, while `terrain_dirty` and Write terrain remain.
The document restores `<map>.lev.forge-draft` after opening the normal game LEV.
This suffix is ignored by both stock and renamed-WAD loose layouts. External
worlds use the same suffix beside their actual LEV; redirected external saves
remain refused. Object drafts retain the existing TNG path/guard behavior.

The saved authoring baseline is independent of `savedTerrain_`, which still
anchors game mesh/theme/foliage bakes. Loading validates UID and grid dimensions,
keeps the bank baseline, and changes the terrain session to reject stale worker
completion. Terrain serialization/navigation validation and both destination
preflights finish before backups/publication; PendingBanks rolls publication
back on failure. Document state changes only after install succeeds. Existing
created/original recovery semantics remain. Game writes retire the cache in
the same transaction; Restore --forget also clears its leftover creation marker.
Pack exports retain the install cache and do not clear unsaved authoring edits.
Legacy automation `save_level` is still object-only; new `save_draft` and
`save_draft_refused` cover the joint workflow.

Validation: full build and all 36 CTest cases pass. Core fixtures independently
parse saved heights/shore/sound names and flags, preserve active LEV bytes and
bake-baseline identity, check draft reload/session/undo and reject wrong grids.
Blocked object destinations and unsupported navigation serialization leave
both edits pending without publishing a terrain file or changing the old TNG.
The initial fixed-name fixture failed on a repeat run; it now uses an owned
TemporaryDirectory and the repeat CTest run passes.

`tools/test_terrain_drafts.py` passed stock and loose installs at both window
sizes under `build/terrain-drafts-1cigiumi`: real Save button, Ctrl+S, F6/F7,
map-switch Save prompt, process reopen, byte-identical active terrain banks and
Restore. Reopened draft bakes match all three direct-reference pack files byte
for byte on both layouts, then game writes retire the cache and exact Restore
returns the original banks. The final broad-gate run also asserts unsaved
terrain authoring state survives direct pack export (see checkpoint above).
Running-game checks with the new joint/terrain-only commands
pass for stock (`build/loose-save-guard-zxb63ner`) and loose
(`build/loose-save-guard-ezvgsj5c`) copies. No live install writes, native runtime
comparison, release or deployment was performed. Timed crash recovery and
World-tab pending-edit persistence remain separate backlog work.


## 2026-10-02 continuation: rounded Passability pens

Walkable/Blocked and Camera ok/no now share Shore's rounded regular-cell pen,
replacing the old cell-centre test. Radius 0.25 reaches one vertex, radius 1
reaches nine and radius 2 reaches 21. Small cursors show the same vertices;
extra boundary vertices are excluded. Strength/time do not affect these pens,
and repeated unchanged applications no longer advance the terrain revision.
Walkable/Blocked Shift inversion remains. Existing navigation patching, stroke
undo and the camera saver OR are unchanged.

Read-only `build/passability-pen-native-full.txt` covers PassabilityInput
02051d50, EditSetPassability 02970060, EditSetCameraPassability 02970630 and
per-map setters 029a9630/029a9940. Executable SHA256 matches the Smear evidence
below. The caller rounds the float world point through GFFloatToLongNear. Both
pens use floor/ceil inclusive bounds and sqrtf(float(dx*dx+dy*dy))-0.5 < radius
(the already-verified double 04022eb8 is 0.5), filtered to editable map cells.
The existing rounded disc helper satisfies this contract; the document's shared
query is now named `surveyBrushVertices` and includes world-origin rounding.
Forge edits only the current map; no native runtime comparison was performed.

Independent core fixtures exercise all four modes with explicit 3x3 expected
masks, zero Strength/time, no-op revisions, one-vertex pens and boundary refusal.
Saved LEVs match exact expected byte deltas: +15 for walkability, +16 for camera,
and the confirmed +16 OR when setting walkable on previously camera-blocked
cells. Other terrain/game-grid fields are unchanged. Existing terrain/navigation
fixtures now target exact vertices with radius 0.25 instead of old half-cell
centres; their original saved-cell and two-cell navigation connectivity
assertions remain, and the core target passes.

`tools/test_passability_cursor.py` passes normal/compact checks for all four pens:
1/9/21 projected vertices, actual mouse strokes, Walkable/Blocked Shift inversion,
zero-time automation dispatch, unchanged heights/themes/environment/sound/shore/
objects and exact undo. Evidence: `build/passability-cursor-8vtpdn7g`; compact
single-vertex Camera no screenshot inspected. Full Release build passes.
All 36 CTest checks pass. The shared Shore check passed both sizes at
`build/shore-points-ln6quppa`; `tests/ui/editor.txt` and `tests/ui/parity.txt`
also finished RESULT PASS, including scratch object saving and terrain undo.
Regression log: `build/passability_pen_regressions.log`. Documentation command
checks, Python syntax and diff whitespace checks pass.
The new harness is registered in check_all; the earlier 96-check checkpoint is
not a result for this source change. The live game install remains read-only.


## 2026-10-02 continuation: authored shore-point flags

Terrain > Passability > Shore paints On/Off with Shift inversion, a Show shore
points toggle and one undo step per stroke. Strength/time are ignored. Small
pens show regular-cell vertices; extra boundary vertices stay untouched.
Set flags appear red/alpha128 through the existing survey texture, now parameterized
for 1-unit shore cells or 4-unit atmosphere/sound cells. Tool exit clears it.
This edits LEV flags, independently of visible water shoreline generation.

Read-only debug exports `build/shore-point-native-full.txt` cover ReflectionsInput
02052620, EditSetReflections 029712e0, EditSetReflection 029a9860 and
ReflectionsOverlayInfo 029aad40. `build/shore-point-save-native.txt` traces
CMap::SaveToFile 02234c60 through its final return. Executable identity is the
same SHA256 recorded in the Environment Smear section below.

The caller rounds the float world pointer via GFFloatToLongNear; native pen
bounds are floor/ceil inclusive, with sqrtf(float(dx*dx+dy*dy))-0.5 < radius.
The referenced double 04022eb8 is confirmed 0.5. Existing rounded disc queries
already implement this test; shore queries clip to width/height regular cells
and preserve world-origin rounding. Native edits +9 bit 2, skips unchanged flags
and updates the red overlay. Its saver copies that bit to the 21-byte record's
+19 byte at 02235748..51. No sound/environment definition lookup or game grid
is needed for shore overlays. Forge handles only the current map.

Core fixtures verify half-even ties, strict radius boundaries, corner clipping,
excluded extra vertices, live red/transparent RGBA, no-op revision handling,
clear/set/undo, noncanonical true-byte preservation, old levels without a game
grid and rejected out-of-range File coordinates. A direct File set/clear emits exactly
one byte delta; a saved Document stroke emits exactly the nine expected shore
bytes with every other byte preserved. The fixture's camera bytes are canonical:
the existing walkable/camera OR rule otherwise legitimately changes +16.
Noncanonical true shore bytes survive unrelated edits because only boolean
changes invoke the setter. No native runtime or in-game shoreline comparison
has been performed.

Initial normal/compact harness passed at `build/shore-points-84w_wfpm`: 1/9/21
cursor vertices, zero Strength/time, real On/Off chips, Shift clearing, unchanged
heights/themes/environment/sound/objects and exact undo. Rendered pixel changes
were checked in Textured, Height, Walkable and Wireframe. Normal red overlay and
compact one-vertex screenshots were inspected. Full Release build, all 36 CTest
checks and docs commands passed; the expanded legacy/noncanonical fixtures also
passed the core target. Shared survey controls/pixels and Environment Smear
regressions passed both sizes (`build/survey-overlay-tl80u0yc`,
`build/environment-smear-r6hhzbq2`, log `build/shore_point_regressions.log`).
The compact tool row was then adjusted to keep Shore's label readable on its
own row; the final two-size brush/control/overlay check passed at
`build/shore-points-xem24q97`. A real compact tool-selection click also passed
at `build/shore-tool-hmvutqlw`; its readable Shore button was visually inspected.
check_all registers
the new harness; the prior 96-check checkpoint was not repeated. Live game
files stayed read-only.


## 2026-10-02 continuation: Environment Smear

Environ. offers Paint / Smear, with Paint the default. Smear hides the target
picker/search, keeps Ctrl sampling and Show colours, ignores Strength/time and
uses the same stroke draft/save/undo path. Small cursors outline distinct reached
4x4 cells; an empty footprint asks for a larger Radius. Ground Smear stays deferred.

Native evidence was recovered read-only from the debug FableWin.exe (SHA256
A9D6D0977D9D7FC8242DA4292AE7ED92E926CA80364A95C003D9B9F689E41845).
Owned exports: `build/environment-smear-native-full.txt` (Blur 0297b910 through
0297c13a), `replace-dispatch-native-evidence.txt` (ATMOS dispatch 02993c4d),
`environment-smear-world-readers-native.txt`, `environment-smear-palette-reader-native.txt`,
`environment-smear-insert-native.txt`, `environment-smear-tree-insert-native.txt`
and `environment-smear-tree-order-native.txt`. Distance/root/sqrt/sqrtf/import
exports trace GFHypoteneuse's float-rounded squared sum and the imported sqrt.
These are static reconstruction evidence, not a native runtime comparison.

The float world point truncates before the inclusive integer counter loop
0..floor(2r), with r=max(size,1). Truncated sample coordinates must be globally
4-aligned, while distance uses the counter offsets. World origin and repeated
samples matter: (0,0), radius 1.25 at origin zero visits four times; origin 1024
visits once. The bounded-counter optimization matched a direct-loop static probe
on 3,000 cases (`environment-smear-bounded-probe.txt`). The cursor deduplicates;
the core preserves visit order/repetitions.

All three slots of nine neighbours participate, including None/zero-weight keys.
The ATMOS getter returns palette bytes. Weight ties prefer the higher index;
normalization truncates the first two strengths and leaves the remainder in the
third. Unused indices survive, and each cell writes immediately. The previous
backlog's simple cell-disc, rounding and zero-padding proposal was corrected.
Forge reads only this map; out-of-map neighbours contribute key 0/weight 0.
Native reads can cross world maps. Zero-sum malformed cells stay unchanged;
nonfinite/unrepresentable coordinates and radii are refused.

Independent core fixtures check ordered cross visits, empty/fractional pens,
world-origin differences, ties {127,127,1}, four revisits {239,15,1}, uniform
areas, unused slots and malformed weights. A five-cell half-plane fixture has
strengths {170,85,0}, {179,75,1}, {168,86,1}, {235,19,1}, {166,88,1}; scratch
saved LEV values, unrelated channels and exact undo are checked. Standalone
candidate fixtures passed before source promotion (`atmos_smear_candidate_test.exe`).

`tools/test_environment_smear.py` passed normal 1440x900 and compact 800x600 /
1.5 scale: real Paint/Smear chips, empty/fractional cell outlines, real strokes,
Ctrl sampling, zero-time automation dispatch, unchanged other channels and exact
undo. Evidence: `build/environment-smear-tsxuh92w`; compact empty hint and normal
painted overlay screenshots inspected. Existing Environment paint checks passed
both sizes (`build/environment-cursor-behz78uz`). Full Release build and docs
command/Python syntax checks and all 36 CTest checks passed. Existing survey
colour/selected-filter/paint/undo checks also passed both sizes
(`build/survey-overlay-btoxbxyd`). The new harness is registered in check_all;
the earlier 96-check checkpoint remains scoped to source 6a9c992 and was not
rerun for this change. Live install files stayed read-only.


## 2026-10-02 full validation checkpoint

At source commit `6a9c992`, the owned Release build passed
`python -u tools/check_all.py --no-build`: **96 PASS, 0 FAIL, ALL PASS**, exit 0.
Log: `build/check_all_stamps_overlays_20261002.log`. No check was skipped.
The lip-sync traversal's 291 empty/non-type-1 entries are data exclusions;
20,214 retail entries were checked byte-exact. Corpus-dependent pack checks
ran with their local archives, including Aeon/Controller, Freeroam, Seasons,
AlbionSecrets, Dragon Cliff, Lost Content and Expanded Chapters.
Coverage includes all 36 CTest checks, sculpt/picking/cursor/grid/camera work,
Ground stamp and survey overlays, retail assets, normal/compact UI, scratch
new levels/world moves, imports, loose/tall terrain, pack level/world edits,
backup/restore/install switching and documentation commands. No live writes,
deployment or in-game native editor comparison was part of this gate.
Tracked source remained frozen throughout. Read-only Environment Smear
investigation and isolated candidate fixtures were prepared under `build`.

## 2026-10-02 continuation: Ground full stamp option

Ground now has **Full stamp** alongside its existing soft blending (the default).
It assigns {target,0,0}/{255,0,0} to every considered height vertex in one
application, independent of Strength/time. Theme 0 is refused. Strength is hidden
while stamping; small cursors show the same strict truncated vertex disc used
by Replace and Environment. Existing palette/placeholder guards still apply.
The API flag is TerrainBrush::themeStamp; `fillPen` supplies both pointer and
automation strokes. Actual painting and cursor reuse `themeBrushVertices`;
repeated identical stamps do not advance terrain revision.

Native `EditPlaceEngineThemeUndoable` 0x029799d0 rejects ENGINE target 0
at 0x029799f1..0x02979a0b, uses strict squared distance at
0x02979aa0..0x02979afb, writes the target and clears slots 1/2 at
0x02979ba2..0x02979bf4, and writes blends 255/0 at 0x02979bf6..0x02979c49.
The caller `PaintInputEngineThemeMap` 0x02993a80 truncates through ToC2DCoordI;
the earlier lround proposal was wrong. Same FableWin identity below; reused
`build/sound-env-footprint-native-evidence.txt` and theme caller/centre exports.
No new native execution or evidence checkout changes.

`testGroundThemeStamp` checks independently listed vertices, fractional centres,
strict radius boundaries, corners and empty brushes, mixed blends becoming full
assignments at zero strength/dt, repeat idempotence, theme-0 refusal, the old
soft brush respecting Strength, saved scratch LEV values and exact undo.
Heights, walkability, sound and atmosphere remain unchanged.
Normal/compact real toggle, zero-Strength pointer strokes, 1/9 cursor vertices,
live and final full-blend assertions, soft-mode return and exact undo pass at
`build/ground-stamp-gkcywzdz`. Compact radius-2 capture was inspected.
Full Release build and all 36 CTest checks pass. Replace cursor (both sizes),
paint-target guards and existing brush scripts pass; combined output is
`build/ground_stamp_regressions.log`. Docs command/Python checks pass.
No live writes, deployment or in-game ground-material comparison.

## 2026-10-02 continuation: Sound and environment survey colours

Sound now offers Off / All sounds / This sound only; Environment defaults to
showing colours and has its own toggle. Theme pickers carry selectable colour
chips. A game-cell RGBA texture at shader t3 uses integer Load at floor(local
ground XY/4), preserving sharp boundaries without changing terrain vertices.
Only the active ground pass is tinted; object/water/world passes and model
previews remain separate. All four map view modes show the overlay.
Document queries read live strokes and rebuild on revision, selection, map,
tool or context changes. Colours decode once per palette entry per rebuild,
rather than per cell. Install/context replacement and renderer clearing drop
the resource. Terrain repaint, fill, palette additions and undo refresh it.

Fresh full disassembly `build/survey-overlay-native-full.txt` covers
SoundOverlayInfo 0x029aa550 through its return at 0x029aaba6, and
UpdateOverlayAtmos 0x02971d80 through 0x029721be. Source is read-only
`D:/Documents/FableTLC/debug_build/FableWin.exe`, SHA256
`A9D6D0977D9D7FC8242DA4292AE7ED92E926CA80364A95C003D9B9F689E41845`.
Sound copies EditColour RGBA at definition +0x44, preserving alpha
(0x029aa89f..0x029aa8ae / 0x029aab26..0x029aab35). Layer 0 means all in
native code; Forge's adapter uses -1/all and 0/selected None, transparent.
Atmosphere uses float(strength)/255 and truncates RGB after EACH slot addition
(0x02971fdc..0x0297213c), then sets A=128 (0x02972152..0x0297215a).
None is black. Missing colours contribute black/transparent; malformed overfull
RGB sums clamp to 255. This corrects the historical rounded-final-sum proposal.

The initial rendered check found the embedded schema lacked both definition
types. Its slice now includes repository SOUND_THEME / CSoundThemeDef /
ENVIRONMENT_THEME_DAY, and Context::defColourField reads named RGBA fields.
Retail decode confirms HAUNTED EditorColour ffff00ff and SECRET_PASSAGE
EditColour 04800080. Definition offsets are evidence, not retail read offsets.

Core fixtures independently check sound alpha, all/selected/None/missing-colour
behavior, three-slot truncation (2/100/255 at thirds gives 0/99/255), active-stroke
refresh and exact undo. Full Release build and all 36 CTest checks pass.
`tools/test_survey_overlay.py` passes rendered normal/compact pixels, controls,
colour-chip selection, all four view modes, real paint and exact undo at
`build/survey-overlay-9dcw58ir`. Normal sound and final compact environment
captures were inspected. Before the added chip-widget assertions, all-mode
coverage also passed at `build/survey-overlay-d3nnlnj3`.
Existing Sound and Environment real-click/sample/clear/undo regressions pass
at `build/sound-cursor-sqkv30hm` / `build/environment-cursor-0y4i_gqi`.
World 2D/3D/open-map and compact layout scripts pass with RESULT PASS;
combined output is `build/survey_overlay_regressions.log`. Docs command and
Python syntax checks pass. An early launch collided with the Windows linker
(WinError 32); rerunning after the build finished passed.
No live writes, deployment, audible sound or in-game atmosphere comparison.

## 2026-10-02 continuation: Environment stamp and cell cursor

Environment now sets each touched 4x4 game cell to {target,0,0}/{255,0,0}
in one application; target 0 clears it. Its centre truncates and its disc is
strict, unlike Sound's rounded expanded disc. Small cursors (radii <=4) outline
the actual cells. The Strength control is removed because strength/time no
longer affect this tool. `themeBrushVertices` (renamed from
`replaceBrushVertices`) shares the verified strict geometry with Replace;
`environmentBrushCells` and Sound share only the vertex-to-game-cell mapping.
Ground Paint retains its soft blending behavior; Environment Smear is still open.

Native call path: `PaintInputEngineThemeMap` 0x02993a80 selects ATMOS (type 1)
when GetEditMode is 5 (0x02993aa4..0x02993abb), truncates via ToC2DCoordI,
and calls `EditPlaceEngineThemeUndoable` 0x029799d0 from 0x02993d03. The
separately named `PaintInputEnvironmentThemeMap` 0x029940c0 is only a stub.
The real pen tests strict squared distance (0x02979aa0..0x02979afb), writes
the target to slot 0, clears 1/2 and sets blends 255/0
(0x02979ba2..0x02979c49); ATMOS target 0 is explicitly allowed
(0x029799f1..0x02979a0b). Transactions route through
`ActionSetEngineTheme` 0x0295f160 / `SetThemeAtBlock` 0x024a41d0 to
`SetEngineThemeAt` 0x02237b90. Its ATMOS branch divides both coordinates by
four and calls `SetAtmosTheme` (0x02237cf3..0x02237d37); the blend path at
0x02237fd5..0x02238019 does likewise. This confirms that the historical
strength-weighted Environment proposal was incorrect.

Exports: `build/environment-dispatch-native-evidence.txt`,
`build/environment-world-setter-native-evidence.txt`,
`build/environment-cell-setter-native-evidence.txt`, plus the theme caller,
centre, and `build/sound-env-footprint-native-evidence.txt` above/below. Same
read-only FableWin identity below. `forge-tools defs list` confirms
ENVIRONMENT_HAUNTED is retail ENVIRONMENT_THEME_DAY index 2351.

`testEnvironmentBrushFootprint` checks independently listed 1/3/4-cell sets,
fractional centres, strict boundaries, corners and empty discs. Mixed blends
become full assignments even with zero strength and dt; 0 clears, repeat is
idempotent, scratch LEV indices/strengths and sound values match, and exact undo
restores all blends. Heights, ground themes and sound stay unchanged.
Full Release build and all 36 CTest checks pass. Real normal/compact paint,
1/2/4-cell outlines, Ctrl sampling, clear and separate stroke undo pass at
`build/environment-cursor-4h4l1tdp`. Normal four-cell and compact single-cell
captures were inspected. Docs command/Python checks pass. No live install
writes, deployment or in-game atmosphere comparison was exercised.
After the shared query/mapping refactor, normal/compact Sound and Replace
regressions pass at `build/sound-cursor-1fmcu_8h` and
`build/replace-cursor-6nbsu1ee`, including exact grid/blend undo.

## 2026-10-02 continuation: strict Replace vertex pen

Replace now uses a truncated vertex centre and strict squared distance instead
of a raw-pointer cell-centre test. Radius 1 considers only the centre vertex;
radius 2 considers nine vertices and excludes the four vertices exactly on its
boundary. `Document::replaceBrushVertices` supplies both the paint loop and
small cursor dots (radii <=4). Dots mean considered vertices; only matching
slots change. Water-family matching, zero-strength matching and Forge's existing
duplicate-slot weight merge remain unchanged. Flood/Replace all are unchanged.

Fresh native evidence corrects the backlog's proposed rounding: the caller
`PaintInputEngineThemeMap` 0x02993a80 calls `C3DVector::ToC2DCoordI` at
0x02993bb8; its implementation 0x01addc10 uses `_ftol2_sse`, truncating both
coordinates. `EditReplaceEngineThemeUndoable` 0x0297a6c0 uses truncated lower
loop bounds (0x0297a723..0x0297a79a) and rejects squared distance >= radius^2
(0x0297a7a0..0x0297a7ef). Exports: `build/replace-dispatch-native-evidence.txt`,
`build/replace-centre-native-evidence.txt`,
`build/replace-footprint-native-evidence.txt`; same FableWin identity below.
This is geometry recovery, not a claim of native blend serialization parity.

`testReplaceBrushFootprint` uses independently listed vertices for fractional
centres, strict radius boundaries, corners and empty discs, plus family-slot
merge results and exact undo. The first fixture assertion incorrectly expected
unchanged duplicate weights; it was corrected to the existing {200,0,55} merge
rule, without altering that algorithm. Scoped tests then pass; full Release
build and all 36 CTest checks pass too. Actual normal/compact Replace clicks,
cursor counts, unchanged heights/sounds/objects and exact theme undo pass at
`build/replace-cursor-83hhbceg`. Normal radius-1 and compact radius-2 captures
were inspected. `tests/ui/replace_water_family.txt` passes; output in
`build/replace_cursor_family_regression.log`. Docs/Python checks pass. The docs
command check was retried after its CLI executable finished linking.
No live install writes or in-game texture comparison was exercised.

## 2026-10-02 continuation: Sound vertex pen and cell cursor

Sound now paints each 4x4 game cell touched by a rounded height-vertex disc,
instead of requiring that cell's centre to lie under the raw pointer circle.
A 0.25-cell click therefore paints its containing cell. Radius-1 clicks near
boundaries can paint two or four cells. `Document::soundBrushCells` supplies
both the unique-cell paint loop and the small (radius <=4) cell outlines;
larger cursors show the rounded pen disc. Invalid sound indices remain refused.
Environment keeps its existing pen pending separate evidence.

Fresh read-only disassembly: `SoundsInput` 0x02052230 rounds via
`GFFloatToLongNear` 0x018c4020 (`fistp`, half-even). `EditSetSounds` 0x02971010
uses bounds from that rounded centre (0x02971022..0x0297109a), then
`hypot - 0.5 < radius` (0x02971112..0x0297113d), with editable/map bounds checks.
The backlog's proposed raw-pointer bounds were incorrect. `EditSetSound`
0x029a97c0 maps the vertices with `HeightMapUnitToGameMapUnit` 0x022342c0,
which divides by four, and skips an already-set value. Exports:
`build/sound-env-footprint-native-evidence.txt`,
`build/sound-setter-native-evidence.txt`,
`build/sound-cell-conversion-native-evidence.txt`. Same FableWin identity below.
The Environment routine 0x029799d0 instead tests strict squared distance;
the historical shared Sound/Environment proposal is not sufficient to port it.

`testSoundBrushFootprint` verifies independently listed single/multiple cells,
half-even centres, clipped corners, repeated applications, invalid indices,
scratch LEV sound values and undo. Heights, ground themes and atmosphere remain
unchanged in the fixture. Full Release build and all 36 CTest checks pass.
Actual normal/compact clicks, 1/2/4-cell cursor counts, Ctrl sampling, unchanged
heights/objects and exact sound undo pass at `build/sound-cursor-406xord9`.
Normal four-cell and compact single-cell captures were inspected. Sound hint
now describes painting; Radius tooltips distinguish Sound/sculpt/other modes.
Docs command and Python syntax checks pass. No live install writes, deployment
or in-game sound playback was exercised.
After the hint/tooltip edit, both sizes pass again at
`build/sound-cursor-gnzirp48`; existing `tests/ui/clear_sounds.txt` passes too
(`build/sound_cursor_clear_regression.log` and the script's RESULT PASS log).

## 2026-10-02 continuation: fractional height-key cursor

Held height keys now show their distinct affected vertices at radii <=4, using
the same sample visitor as the actual pen. Their fractional/truncated positions
differ from mouse pens: interior radii 0.25/0.5/1/1.5/2 reach 0/0/5/4/13 vertices.
Empty footprints display "Increase Radius to reach a vertex." while held.
The query deduplicates dots; the actual pen preserves repeated applications:
at the corner, radius 1.5 modifies vertex (0,0) four times. Larger outlines are
still approximate. No pen strength, sampling, latch or undo rules changed.

Fresh static evidence: `EditPlaceHeightAlterationUndoable` 0x029796b0, sample
loops 0x029796ef..0x02979737, truncated positions 0x02979775/0x02979789 and
distance check 0x029797b5..0x029797d9; export
`build/height-key-footprint-native-evidence.txt`. Its random gate is enabled by
the pen parameter; the height-key caller 0x02993840 sets that parameter to zero,
as recorded in `build/height-keys-native-evidence.txt`. Same read-only FableWin
identity as below; these are instruction checks, not native runtime comparisons.

Full Release build and all 36 CTest checks pass. Normal/compact cursor cases,
unchanged empty-footprint snapshots and actual stroke undo pass at
`build/height-cursor-hbkjkj1l`. Normal/compact empty-hint and normal four-dot
captures were inspected. No install files were written.
The full fixed-point/Speed/six-tool/other-mode/undo regression passes at both
sizes in `build/height-keys-6_w4iusg`; docs command and Python syntax checks pass.

## 2026-10-02 continuation: Noise cursor square

Noise now outlines the half-cell envelope of its possible random vertices,
around the rounded centre, clipped to the map. It retains a centre marker;
vertex dots are not used because the pen selects random pokes. The existing
noise/RNG/height/undo implementation is unchanged, including its initial
disc-average sea-level gate. Height-key input in Noise keeps the key cursor.

Fresh FableWin EditGenerateNoisePenUndoable 0x02975f00 disassembly reads radius
and calls GFFloatToLongCeil then GFRandom for each coordinate at
0x02976268..0x02976296, followed by random sign selection. GFRandom 0x018cf370
returns unsigned division remainder (0x018cf3b8), so its upper bound is
exclusive. Possible offsets therefore extend to ceil(radius)-1, not the full
ceil(radius) square in the old proposal. Export:
`build/noise-footprint-native-evidence.txt`, same FableWin identity below.
The cursor envelope adds half a cell around those possible vertex centres.
Release GUI build passes. Normal/compact cursor checks and captures pass at
`build/height-cursor-94wn4fdw`; normal three-by-three and compact single-vertex
Noise captures were inspected. Height pens (including actual Noise and undo)
pass too; combined log `build/noise_cursor_checks.log`. Core is unchanged from
the preceding all-36-pass Smooth build. No install writes/live probes ran.

## 2026-10-02 continuation: Smooth cursor footprint

Smooth now rounds its cursor centre and uses its actual integer-radius disc,
with vertex dots at small sizes. Radius clamps to >=1 before truncation; a
radius below 2 considers only the centre vertex. The query and actual Smear
pen reuse the same distance predicate. Smoothing, spike filtering, two-pass
height updates, clamping and undo are unchanged.

Fresh FableWin EditSmoothPenUndoable 0x0297b3c0 disassembly confirms radius
clamp/truncation at 0x0297b3ff..0x0297b433, 2r loop bounds, and the strict
distance comparison at 0x0297b535..0x0297b54a. Thus offsets at both +/-r are
excluded: the affected vertex set is symmetric despite the half-open bounds.
The backlog's suggested (-0.5,-0.5) cursor shift was incorrect and was not
used. Export: `build/smooth-footprint-native-evidence.txt`, same FableWin
identity recorded below; this is an interpretation of confirmed bounds/test.

Checkerboard core fixtures compare actual changed vertices to the query,
including half-even centres, fractional radius truncation, 1/9/25-vertex discs
and clipped corners. Full Release build passes. Normal/compact cursor checks
pass at `build/height-cursor-mznkul22`, preserving terrain/object snapshots.
No install writes or in-game probes were exercised.
All 36 CTest checks pass after the full rebuild. Normal nine-vertex and compact
single-vertex Smooth screenshots were inspected; docs command check passes.

## 2026-10-02 continuation: exact-step / Flatten cursor footprints

Change Height (exact-step Raise/Lower) and Paint Height (Flatten) cursors now
round their centre half-to-even, draw the native disc envelope at radius +0.5,
and show affected vertex dots for radii <=4. The dot query shares the existing
pen distance predicate, rather than maintaining a separate cursor equation.
Underlying once-per-stroke, opacity, clamping and undo behavior is preserved.
Height-key strokes retain their pinned cursor; Smooth/Noise cursor recovery
remains open because those pens have different footprints.

This uses the already recovered Change Height / Paint Height contract
(FableWin 0x02975710 / 0x029753d0, same executable identity below).
Core checks compare the query to actual output of both pens across half-even
centres, 0.25/0.5-cell radii, the strict 0.5 boundary and its next float,
one/nine/37-vertex discs, and clipped map corners. Full Release build and all
36 CTest checks pass. The first normal/compact GUI check passes at
`build/height-cursor-mcoq_v4s`; both one- and nine-vertex captures inspected.
The harness is then refined to reveal the tool/radius in compact captures.
No game writes or in-game probes are part of these checks.
Final captures/checks pass at `build/height-cursor-vq34hk1p`; the compact
Flatten nine-vertex capture was inspected with the radius/hint visible.
Actual size-slider/bracket controls, single-vertex strokes and undo pass at
`build/terrain-brush-controls-gt4v7jz8`; Height pens also pass. Combined log:
`build/height_cursor_final_checks.log`. The new cursor check is in check_all.

## 2026-10-02 investigation: ground-theme Smear proposal needs revision

Fresh full FableWin disassembly confirms the two-pass nine-neighbour brush and
strict disc, but contradicts the backlog's simplified proposed algorithm.
EditPlaceEngineThemeBlendPenUndoable 0x0297c510 clamps radius to at least 1
before truncation. MapAround9 lives in BSS; its initializer 0x03ff9370 creates
centre and the eight adjacent offsets. There are two passes so output cannot
feed later cells in the same application. ENGINE weights use definition-class
indices; LEV palette ordering and stale stored global indices cannot stand in
for that ordering. Thresholding at 0x0297c960 only visits class indices 0..2,
not all weights. Selection's shift at 0x0297ca50 advances from the insertion
point, overwriting subsequent selected entries; normalization then updates
the shared weight vector in place, so duplicate selected classes are rescaled
again. The second pass writes only two blend bytes; the third is implicit.

Literal arithmetic probe with class weights [0,500,700,100] selects [2,1,1],
produces scratch strengths [169,11,11], and persists [169,11,75]. This is a
static-instruction probe, not a live native execution or retail equivalence
claim. Keep implementation deferred until the safe Forge blending contract or
native quirk compatibility is resolved; copying the backlog pseudocode would
not recover the observed debug algorithm. Exports: `build/theme-smear-native-full.txt`,
`build/theme-smear-offsets-native.txt`, `build/theme_smear_selection_probe.py`.
No Forge core, GUI or install files changed for this investigation.

## 2026-10-02 broad grid/camera validation

Primary Release binaries at `d935c6c` passed the complete `check_all.py
--no-build` gate: 88 checks, zero failures, exit 0 / ALL PASS. This includes
all 36 CTest checks, both grid/camera additions, retail and normal/compact UI,
scratch new levels/overworld, supported mod corpora, imports, restores and pack
level/world writes. No check was skipped; lip-sync fixture traversal omits
291 empty/non-type-1 entries while comparing 20,214 retail entries byte exactly.
Log: `build/check_all_grid_cameras_20261002.log`. The sculpt-control changes
below were tested separately on owned binaries during the gate, preserving
its build identity. No additional game deployment or live probes were run.

## 2026-10-02 continuation: shared sculpt Speed control

Speed is now visible in all six sculpt tools. The original slider ID and
automation widget are retained. Its tooltip gives the actual Minus/Equal step
in metres per held frame and explains its additional Flatten target blend.
Native CPaintMapDialog::GetOpacity 0x02907840 reads a shared dialog slider and
calls GFGetWeightedFractionSlowStartFastEnd 0x02907a70. The height-addition
routine independently reads GetOpacity, with no selected-pen condition.
Local export: `build/sculpt-speed-native-evidence.txt`, same FableWin identity.
This UI change retains the existing pen and key algorithms.
Normal/compact checks pass at `build/height-keys-ipl14p48`: actual slider
endpoints in each sculpt tool, zero-speed strokes preserving both snapshots,
full-speed strokes with exact undo, and the prior fixed-anchor/tool-gating
cases. Both Speed screenshots inspected; docs command check passes.
Primary Release GUI rebuilt after the broad gate ended. Final Ctrl-drag checks
pass at `build/height-pick-1xri_5eu` with shared Speed and fixed-point keys;
Height pens, Paths, theme-target guard and compact layout also pass
(`build/sculpt_final_regressions.log`). No repeated broad gate was needed for
these GUI-only changes after the complete stable grid/camera gate.

## 2026-10-02 continuation: fixed-point sculpt height keys

Minus/Equal now latch the ground point when a key stroke starts, continue there
while held even if the pointer leaves the viewport, and finish one undo step on
release or exit from sculpt tools. The brush cursor stays at the anchor. Paint,
Passability and Region ignore these keys. Leaving terrain editing also clears
the key-stroke flag so a later edit cannot reuse a stale anchor.

Native evidence uses the same FableWin SHA256 below. ProcessInput 0x0294dbe0
tests GetEditMode()==3 at 0x0294dd29 / 0x0294dd61, calls StickWithLastPoint(true)
then height addition, and releases the stick plus InsertUndoPartition on key-up.
StickWithLastPoint 0x02991680 sets byte +0xaf8. GetCurrentPointedAtPos 0x0204b2d0
returns cached position +0x8dc when that byte is set. Height addition 0x02993840
reads brush size/opacity and calls EditPlaceHeightAlterationUndoable.
Local exports: `build/height-keys-native-evidence.txt` and
`build/height-keys-point-native-evidence.txt`.

The old backlog proposed excluding Paths. Fresh PaintInputPaintMap disassembly
0x029916a0 calls GetDrawPathsFlag at 0x0299182f before Smear/Noise/PaintHeight
dispatch, in the same Land input path. Therefore Paths is included among the
six sculpt tools; there is no pen-specific exclusion in the key handler.
Dispatch export: `build/paintmap-dispatch-native-evidence.txt` and resolved
calls in `build/paintmap-dispatch-calls.txt`.

Normal/compact real-key checks pass at `build/height-keys-bcyc0iqy`: all six
sculpt modes, Minus as well as Equal, moving the camera/pointer away from the
anchor, leaving the viewport, all eleven other terrain modes ignoring both
keys, switching to Paint while held, and full document/height undo snapshots.
The owned Release build was used while the grid/camera broad gate continued
on stable primary binaries. No install writes or in-game checks exercised here.
The final hint-text build passes Height pens, Paths, brushes, theme-target guard
and compact-layout regressions (`build/height_keys_regressions.log`).

## 2026-10-02 continuation: Ctrl-drag sculpt height picker

Raise, Lower, Flatten, Smooth, Noise and Paths now sample ground height on
Ctrl+LMB press and every held frame. Ctrl+Shift samples too. Sampling sets
Flatten's explicit target and each sculpt tool shows Paint height. Existing
active brush/path edits retain control if Ctrl is pressed midway through them.

Read-only native evidence uses the FableWin SHA256 recorded below. ProcessInput
0x0294dbe0 checks Ctrl before dispatching press/held LMB events to thunk
0x017b3fb8 (PaintInputPickupHeight 0x029940d0), without a pen-specific condition.
The Land branch reads ground height into SetLeftAltitude. Local disassembly:
`build/height-pick-native-evidence.txt`; symbols from FableTLC's editor manifest.

The first normal/compact real mouse checks pass at `build/height-pick-s8t6w3kl`:
all six tools sample a 30 m patch and then a 20 m patch with LMB still held,
terrain/object snapshots remain exact, Ctrl+Shift samples, and Ctrl pressed
mid-stroke preserves the edit and undo. Both Paint height screenshots inspected.
An owned `build-height-pick` Release build keeps the broad gate's primary
binaries fixed at the grid/camera milestone while this UI change is tested.
No game writes or in-game probes are part of this change's checks.
Final checks at `build/height-pick-gq7rqbko` also prove Flatten consumes the
sampled target and undo restores both snapshots. Height pens, theme-target
guard, Paths, brushes and compact-layout regressions pass on the owned build
(`build/height_pick_regressions.log`). The new picker check is in check_all.

## 2026-10-02 continuation: whole-map Raise camera fields

Raise with Move objects too now includes camera coordinate bases and spline key
positions in the existing terrain/object undo step. The old "camera-point targets"
limit was imprecise: scripted StartPos/EndPos and look directions are unchanged by
native SetHeightDeltaZ. Malformed vectors and duplicate component/coordinate rows
are preserved; absent camera fields are not invented.

Native evidence, same FableWin SHA256 as the grid entry below:
`CEditRaiseZOnThing` 0x0298e5a0 calls camera vtable +0x8c independently of physics;
base 0x024f4900 adds d to CoordBase.Z (+0x1e) only under UsingRelativeCoords (+0x3a).
`ghidra_out/struct_layouts_egor.tsv` confirms these fields. Scripted override
0x025ce9d0 calls the base through thunk 0x017b4d5f. Spline override 0x025cea00
also adds d to each key's Position via GetPosition/SetPosition (0x025c7f50 / 0x025c7d40).
Base OnSerialise 0x024f3e20 converts CoordBase to/from parent-relative coordinates
under IsCoordBaseRelativeToParent; both bool persistence defaults are false.
Consequently the saved base delta is `(UsingRelativeCoords ? d : 0) -
(physicsMoved && IsCoordBaseRelativeToParent ? d : 0)`. This formula is inferred
from the native state change and serialization branches, not from field names.
Spline serialization 0x025cb1e0 uses `KeyCameras[n].Position` and NumKeyCameras;
retail StartOakValeWest supplies the independent field spelling/shape check.
Local disassembly: `build/wholemap-camera-native-evidence.txt`; extracted retail
fixture: `build/camera-point-retail-evidence/Data/Levels/FinalAlbion/StartOakValeWest.tng`.

Focused tests cover all four base-flag combinations, no-physics cameras, spline
keys, orphan rows, malformed coordinates, lowering with terrain clamping, Move
objects off and exact undo/redo. The scratch GUI test independently reads saved
baseline/+3/-10 TNGs: 49 base changes and 51 spline key changes per direction.
Every other camera row and section byte is preserved; existing InitialPos
synchronization retains its float spelling tolerance. First parser check found
that already-existing InitialPosX synchronization trims one micro-unit on a retail
villager, so the verifier now permits only its validated numeric spelling delta.
No game deployment or in-game camera playback has been exercised.
Full build and all 36 CTest checks pass. Final scratch output is retained at
`build/wholemap-cameras-sw4a8rb5`; the verifier was corrected to include native
two-word `Holy Site` types and rechecked against the same saved bytes.
Whole-map heights, terrain-following objects and owned-movement UI regressions
pass. Both new checks are part of the broad gate.

## 2026-10-02 continuation: cell-centre placement

Added a saved *Snap to cell centres* option beside Fixed height in the Objects
and Actors placement controls. It snaps new definitions, pointer placement,
clipboard/preset anchors, spawners, villages, fishing spots, emitters and region
entrances to the 1 m cell centres, then samples ground there. Carry snaps after
its grab offset and keeps height above terrain. Fixed height applies after the
terrain sample; emitters retain their half-unit lift. Group paste preserves
relative layout around the snapped anchor. Gizmo Snap, track placement, terrain
pens, raw height sampling and live-link teleport retain their existing rules.

Evidence: FableTLC `ghidra_out/fablewin_editor_symbols.tsv` names
`CEditControlCentre::FindNearestSnappedPos` at `0x02049280`; fresh read-only PE
disassembly confirms two `_ftol` conversions, addition of `{0.5,0.5,0}` and then
the ground query. The constant at `0x0401de04` is 0.5. Executable SHA256
`A9D6D0977D9D7FC8242DA4292AE7ED92E926CA80364A95C003D9B9F689E41845`;
local export `build/placement-grid-native-evidence.txt`. Core helper uses
truncation toward zero, also checked for negative inputs.

Build and all 36 CTest checks pass. Normal 1440x900 and compact 800x600 / 1.5
real-control, unsnapped/snapped XY, ground/fixed-height, pointer, paste, special
thing, carry-offset and exact undo checks pass at `build/placement-grid-abpoao3i`.
Both control captures inspected. First test run clicked below the nested card
before scrolling settled; a second reveal fixes the harness, with no game writes.
The check is included in `check_all.py`. No in-game deployment was exercised.
Existing placement, ground/clone carry, owned duplication and terrain-brush UI
regressions pass with snapping off.

## 2026-10-02: 0.18.0 built, validated and published

Commit `4ca5a62` ("Release FableForge 0.18.0") carries the stable version, README,
guide link, screenshots and validation notes (`docs/releases/0.18.0.md`). Full suite:
all checks passed except two load-only flakes, which passed rerun alone (Dialogue workspace
x2, overworld moves). In-game probes (things, compact, region, restore) all pass
from a fresh New Game. The first region run raced the compact stage's game shutdown; the
probe runner now waits for Fable.exe to exit. Before the probes, the shared install
and My Games/Fable were snapshotted to `D:\tmp\fable_install_snapshot_20261002`,
reset with `forge restore`, then put back. SHA256 trees match pre-probe state
(1,912 + 1,217 files; 18 files still differ from backups, as before).
`dist/FableForge-0.18.0-win64.zip` (+ `.sha256`, guide ZIP) passes `test_package.py`.

Published later that day: tag `v0.18.0` on `4ca5a62`, CI green, GitHub release
marked Latest with the app ZIP, its `.sha256` and the guide ZIP. `main` now tracks
`feat/editor-ui-shell` (fast-forwarded and pushed together); the suffix is "-dev".

## 2026-10-02: post-release work on main (all pushed, CI green)

- `docs/guide/`: task pages (paint, Fit to neighbours, mod packs, lip sync) with
  captures from `tests/ui/guide_shots.txt`; packaged beside README (87 links pass).
- Edit footer: *Write terrain and objects* (terrain first, objects only after it
  succeeded); `write_both.txt` + `write_both_fail.txt` run inside `test_overworld.py`.
- Escape cancels any inline write confirmation without deselecting
  (`confirm_escape.txt`); Enter never confirms a write.
- Paint refuses palette slot 0 and INVALID_THEME_STANDIN as targets (`paint_target_guard.txt`).
- A dropped `.glb`/`.gltf`/`.obj` fills the Models import form (`drop_model.txt`).
- Textures list thumbnails, 4 ms/frame budget, invalidated on file change (`texture_thumbs.txt`).
- Scale gizmo: exponential ~0.3% per logical px, Shift finer (AeoN's sensitivity report;
  `scale_drag.txt`); the Properties scale field drags at 0.003.
New automation: `write_both`, `drop_refused`, `assert_state_min`, `assert_state_max`.
Full `check_all.py` after these: ALL PASS, 82 checks (build/check_all_postrelease.log).
- Draw Paths now has vanilla square ends (t in [0,1], from FableWin 0x02975ad0); unit-tested.

## 2026-10-01 continuation: lip-sync Undo and Redo

The editor has Undo/Redo buttons and Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z shortcuts.
Each completed slider drag is one edit; add/remove shapes, insert/delete frames
and Reset this line are undoable. Up to 64 edits are retained per line, including
across line/bank switches. Undo restores frame data, duration bits, dictionary,
selection time and whether the line was pending; playback pauses. Export keeps
history. Save to pack clears that archive's pending history. Reloading a line
whose disk baseline changed clears incompatible history while retaining staged
edits. Install changes clear history. Reset clamps a playhead beyond the restored
line's duration.

Real UI checks pass at build/dialogue-undo-46adtlvv for normal/compact layouts:
multi-move drag grouping, all shortcuts, frame/shape/reset undo, two-bank history,
protected export retry and save boundaries. Independent archive/recipe reads
verify the expected data and source SHA256. Restore/baseline invalidation passes
at build/dialogue-restore-bs6d_w9k. Workspace, two-bank export/recipe composition
and empty/full frame creation pass at build/dialogue-workspace-5_rc04j5,
build/dialogue-edit-97pqme60 and build/dialogue-pack-create-cek1q5xu.
The first undo harness run (build/dialogue-undo-q5k004ra) found unmapped Y/Z test
keys; their real-input mappings were added and the workflow then passed.

The user has requested stable 0.18.0 publication and stopping for the night once
it is released. Scope is frozen after this edit; proceed with release checks and
only fix failures needed for that release. Separate-machine validation was
previously deferred; record its actual status rather than assuming it passed.

## 2026-10-01 continuation: clear lip-sync frame shape limits

An empty frame explains that mouth shapes can be added. The Add mouth shape
control remains visible and disabled at the native four-shape limit, with a
remove-first explanation. Remove buttons and shape choices have automation IDs.
The existing pack-creation check now inserts an empty frame, adds four shapes,
adjusts influence, replaces one shape, exports and saves the updated recipe.
Both normal and compact checks pass at build/dialogue-pack-create-zv7mgh3_.
Independent manifest checks verify four unique shapes and their weights; the
exported frame count and source archive SHA256 are checked. Empty/full captures
were visually inspected. Initial harness failures were a dictionary-schema
assumption and clicks before compact scrolling settled; retained failed runs
are build/dialogue-pack-create-lvdif5bz, ewrsrehn and m5cc2p67.

## 2026-10-01 continuation: centred Effects completion message

The empty-preview status is now centred horizontally and vertically using its
measured text bounds. This includes "Finished. Press Play to replay." and the
no-particles status. Normal and compact finished captures were visually checked
in build/effect-timing-jidfhwjw; timing, replay and live scrubbing checks passed.
The application build passed. This is a presentation-only change.

## 2026-10-01 continuation: compact centred Effects viewport

The user reported that the Effects viewport was unnecessarily wide. The preview
now uses a centred 4:3 area bounded by available width and window height, with a
600-logical-pixel maximum width. Transport, timeline and background controls
share that column. Technical details continue below it. At 1440x900 the captured
image is 510 by 382.5 pixels, replacing the former approximately 1044 by 229 strip.
Compact controls wrap within the preview width and the page remains scrollable.

Normal/compact workspace checks pass at build/effect-workspace-w5z878dj: transport,
grid reset pixels, both framing actions, controls and asset links. Added aspect,
width and centring assertions pass against those captures. Timing/replay/live
scrubbing and air-glow visibility pass at build/effect-timing-vy5xwi8h. Wide air
glow and compact blood-pool captures were visually inspected. The change affects
viewport layout, not the effect simulation or game files.

## 2026-10-01 continuation: direct lip-sync frame navigation

The inline editor now accepts a one-based frame number, with one/ten-frame
spinner steps. Inputs clamp to the valid range; Previous and Next disable at
the endpoints. Navigation does not stage an edit. Normal and compact real-input
checks pass at build/dialogue-workspace-8plo73a1: jump to 10, step to 9 and back,
clamp zero to 1, then insert/export as before. Muted/audible playback checks
remain included. The compact capture retains the first mouth-shape slider.
Automation now accepts Enter; the initial harness failure was an unsupported
key name, not a frame-navigation failure.

## 2026-10-01 continuation: create lip-sync packs inside Dialogue

Dialogue now reuses the existing pack destination/creation controls, with its
pack-only mode. A user can create a named pack and save staged lines without
leaving the editor. Models and Ground themes retain their direct-import option.
Pack labels and selection compare normalized paths so newly created Windows
folders display the friendly load-order name despite separator differences.

Normal and compact GUI creation tests pass at build/dialogue-pack-create-y54meg1o:
create a pack, add its load-order entry, save the expected sound/bank recipe and
clear staged edits without deploying a dialogue archive. The compact success
capture was inspected. An initial compact script clicked before nested scrolling
settled; it now reveals the input after layout settles. Existing two-bank export,
recipes, conflicts and order tests pass at build/dialogue-edit-tgu7ho4z. The shared
Models form passes normal/compact failure/retry, completion and next-draft checks
at build/model-import-form-tc10k8on. All writes were confined to owned scratch.

The preceding development package through 0aac81d has CRC-checked SHA256
62dab2b01291a50169b19ff050b3aee4c00a67c871dcc91ede735cad86fccbed.
Inline pack creation requires the next package refresh.

## 2026-10-01 continuation: muted dialogue scrubbing pauses motion

The slider previously paused audio but left the separate muted/audio-free
animation clock running. Both the time slider and large timeline now pause that
clock when scrubbed. The regression reproduced at dialogue-workspace-pux19olu;
normal and compact actual-input flows pass at build/dialogue-workspace-ob9qqceh,
including muted slider drag before mouse release, muted timeline seeking, audible
replay, editor entry and scratch archive export. The input archive is unchanged.

The development ZIP through 75ebdbb passes build/package-smoke-xn27ttp0, with
SHA256 02964f41d178fc592d7404ebf61ea3c02eea0e0d82e5ae4bb85383f895ff820c.
This muted-scrub follow-up is pending the next package refresh.

## 2026-10-01 continuation: dialogue refresh after Setup restore

A new scratch workflow reproduced stale loaded dialogue after a successful Setup
restore: archive bytes were original, but the preview retained the deployed
2-frame line until manual reload. Restore now stops dialogue playback, retires
the loaded line/subtitle/search cache, returns to browsing and refreshes the mod
report. Separately staged lip-sync edits survive. Corrected the Mods footer,
which incorrectly claimed Setup Restore excludes staged deployments.

The initial failing evidence is build/dialogue-restore-2vjt3duv. The final test
at build/dialogue-restore-u8t9atq_ deploys a two-frame recipe into a copied retail
archive, uses the real Setup confirmation controls, verifies exact original
archive bytes and removal of the stage manifest, then loads the original
45-frame line. A second compact/high-scale run inserts a staged third frame,
restores the disk archive and verifies the staged three-frame edit survives.
No writer or format algorithm changed; the live install remained byte-identical.
The test is included in check_all.

Additional head-layout checks pass at build/dialogue-layout-ciw588ik for
1440x900, 1280x720/1.5 and 1024x600/1.5, including covered-eye and four visible
mouth-pose pixel checks. Ctrl+F navigation evidence remains asset-search-iplat6d9.

## 2026-10-01 continuation: search from the lip-sync editor

Ctrl+F now returns from inline lip-sync editing to the dialogue tree and focuses
search. Staged edits remain intact. Normal and compact actual keyboard tests
pass at build/asset-search-iplat6d9, along with the existing asset-search and
hidden-panel recovery checks. This corrects a focus regression introduced by
the inline editor; there is no archive-format change.

The development package through mesh-framing commit 8eec69d passes extracted
package checks at build/package-smoke-k9gkb_0h (69 documentation links, definitions,
eight retail exports, GUI pixels and literal-path mod workflow). Its SHA256 is
12fa6deb909d0cc42e7f805c1e77fc8ea8862651ab86d7a346730668b316cd19.
The Ctrl+F follow-up requires the next development repackage. Published rc.3
assets remain unchanged.

## 2026-10-01 continuation: tighter effect mesh framing

Frame effect and Frame current now bound transformed mesh geometry rather than
expanding the largest scale into a sphere. Thin, stretched meshes no longer
force excessive camera distance. Bounds use the same quaternion/scale basis as
the renderer, including authored radius and centring. Missing meshes contribute
no visible geometry. Sprite framing and particle behavior are unchanged.

The initial texture-strip hypothesis was ruled out: TextureRow already contains
per-frame dimensions. Read-only AIR_GLOW_01 probes at
build/effect-bounds-probe-78zbkyh5 identified thin mesh 435 with large Z scale.
The new normal capture is visibly larger, with 4,471 visible pixels (compact
2,889) at build/effect-timing-s2oe8bcq. WARP renderer tests pass, including scaled
planar bounds, offset origins and rotation. Both workspace sizes pass at
build/effect-workspace-3thhk7a2. The old Frame-current 15% enlargement assertion
failed because the whole-motion view improved; it now requires 5% plus over
500 changed pixels. Actual gains are 1,708 to 1,842 and 572 to 679 pixels.

This supersedes the preceding framing limitation for the inspected air-glow
example. Full game rendering parity remains unverified.

## 2026-10-01 continuation: effect timeline length and replay

Short effects now use supported lifetime estimates instead of a fixed ten-second
window. Continuous effects retain that window; manual Length overrides Auto
length. Play at the end restarts, dragging seeks immediately, fractional end times
are honored, and empty previews explain completion. Simulation algorithms and
camera framing are unchanged. Some faint effects still occupy little of the
whole-motion view; Frame current and zoom remain available.

Retail particle checks pass 86 cases. Activation bursts estimate 0.533333 seconds
and AIR_GLOW_01 1.233333. Normal/compact replay and live-scrub tests pass at
build/effect-timing-0rgksa1i, including an exact 0.51-second manual end. Existing
camera/rendering tests pass at build/effect-workspace-b2487sqj. All 36 CTest suites
pass after rebuilding every target (19.78 seconds). Estimates cover supported
preview behavior, not full game parity. No live game files were changed.

## 2026-10-01 continuation: dialogue tree and visible lip-sync editing

Replaced the flat 20,088-line dropdown with a persistent speaker tree (357
retail English speakers). Groups show counts; their leaves show subtitles, with
full text and source on hover. Search still spans all banks and expands small
matching groups automatically; leaf identity retains bank plus Sound ID.
The legacy bank/ID lookup remains available.

Edit lip sync is now beside the Dialogue heading as well as in the fixed footer.
It pauses playback, opens the timeline and switches the right panel to editing,
keeping the face visible. Frame navigation precedes mouth-shape controls;
weights display percentages and shape descriptions. Less common insert/delete
commands follow the shapes. Browse dialogue / Escape returns to the tree, and
staged edits survive switching lines. Archive export and pack recipe behavior
are unchanged. The earlier floating dialogue editor is superseded.

Normal and compact browse/search, tree collapse/expand, playback, automatic pause,
frame insertion and exported frame counts pass at `build/dialogue-workspace-i9t1xo9c`.
Final compact capture was inspected: the first mouth-shape slider is visible
without hiding the face. `build/dialogue-edit-me_d587o` passes two-bank export,
line switching, reset, Escape, close/review behavior, pack recipes, conflicts and
load-order composition. Existing native Browse test navigation was updated for
the tree; the OS picker implementation did not change and was not rerun in this
step. The first compact workspace script clicked a scrolled-out header; it now
reveals that action before clicking. User documentation and the Dialogue image
were updated. The development ZIP is still the prior build until repackaging.

## 2026-10-01 continuation: precise terrain brush controls

The radius slider now uses the already-recovered native 2^size mapping
(GetBrushSize 0x02907170 / forge::heightpen::sizeToRadius), exposing the 0.25-cell
minimum while retaining Forge's 60-cell maximum and six-cell default. Bracket
shortcuts step by sqrt(2); values below two cells display two decimal places.
Hover help explains small strokes and automatic prop slope following. The
underlying sculpt algorithms are unchanged.

`tools/test_terrain_brush_controls.py`, now in check_all, drives actual slider
endpoints and key events at 1440x900/1 and 800x600/1.5. Both quarter-cell and
half-cell exact-step strokes change exactly one vertex; undo restores the exact
document and heights; bounds remain 0.25..60. Evidence and inspected compact
capture: `build/terrain-brush-controls-b4ihv1nt`. The first compact attempt clicked
before its scrolling layout settled; the harness now waits for foliage and
reveals the control again. GUI build passes. The existing unit coverage for
sizeToRadius remains applicable; no terrain algorithm was changed in this step.

## 2026-10-01 continuation: isolated development package

`tools/package.py --output-dir` now places both ZIPs and its validated staging
folder in the chosen destination, including guide-only mode. This permits local
iteration without overwriting the published standalone guide or its checksum.
The existing default remains dist. The CLI reference now reflects the current
development binary rather than rc.3.

`dist/development/FableForge-0.18.0-dev-win64.zip` contains the compass correction,
UI workspace/import/search improvements, dialogue playback fix and terrain prop
slope following. Extracted-package validation at `build/package-smoke-erc5r0b2`
passes 69 documentation links, definition lookup, eight retail exports, rendered
GUI smoke and literal-path mod workflows from an unrelated working directory.
Application and guide CRC checks pass and SHA256 sidecars were written. Separate
guide-only output under `build/package-guide-output-check` has the six expected
members and keeps the tested rc.3 download link. Published rc.3 application and
guide ZIP hashes remain exactly unchanged. No new GitHub release was published;
other-machine and in-game validation are still pending.

## 2026-10-01 continuation: props follow edited terrain slopes

Terrain following previously changed only Z and skipped edits with unchanged
height at an object's pivot. Automatic strokes and direct height edits now
rotate grounded, unlocked Object props by the change between canonical terrain
frames. These use the derivative of the same bilinear height surface used for
placement, with one-sided cells at map borders. Relative authored lean, heading
and scale are preserved; canonical frames avoid cumulative yaw through repeated
slope changes. Creature, Building, Marker and navigator roots retain orientation.
Owned descendants follow their parent's rigid transform, including locked or
floating children; roots deliberately floating/buried or locked remain unchanged.
Terrain and all object changes share one undo step. Older-draft repair stays Z-only
because its saved baseline cannot safely identify already-applied rotations.

This automatic tilt is a user-requested Forge enhancement, not claimed native
parity. Revisited FableWin `CEditWorldMap::EditSetGroundSizeZAtBlockUndoable`
0x0297aa90 in the existing `lanes/water/f_0297aa90.txt` disassembly under
`C:/Users/Cornelio/AppData/Local/Temp/claude/D--Documents-FableTLC/130e9b68-87fa-44cd-90d1-1630b5eeb085/scratchpad`.
The inspected setter adds height transactions and refits water/themes; it does
not establish a prop slope policy. Existing native uniform-Z behavior is recorded
in PARITY_BACKLOG at CEditRaiseZOnThing 0x0298e5a0. No engine placement policy was
inferred from a symbol name. Evidence executable identity remains the FableWin
SHA256 recorded in HANDOFF_NAV.

All 36 CTest suites pass (22.29 seconds). Expanded locked-things coverage includes
slope-only changes, both slope axes, boundary vertices, upright classes, locked
roots and owned children, floating roots, authored lean/scale, 30 reshape/flatten
cycles, exact undo/redo, repair idempotence and actual brush completion.
`tools/test_terrain_slope.py` is included in check_all: at
`build/terrain-slope-wpcvthkc` a Greatwood prop rotates after sculpting, its saved
scratch TNG contains a normalized tilted basis, and one undo restores its frame
and heights. Visible captures were inspected using the same flow at
`build/terrain-slope-ujxryjrb/visible.txt.log`. Earlier harness attempts used the
wrong snapshot verb or a camera below terrain; neither was counted as visual
validation. Source install was read-only; game runtime remains untested. The
published rc.3 remains unchanged and does not contain this fix.

## 2026-10-01 continuation: readable compact editor selectors

Objects/Terrain/Actors/Level and Select/Move/Rotate/Scale/Terrain now switch from
segmented rows to dropdowns when full labels would not fit. Wide rows retain
their existing layout and keyboard shortcuts. Snap has a short visible label;
its movement/rotation/scale increments are available on hover. Existing automation
aliases remain available for either selector layout.

At `build/editor-selectors-gffglbmx/800x600`, actual dropdown clicks exercise all
four sections and five tools, including Terrain following the terrain tool and
returning to Objects on Move; the document remains unchanged. The 1024x600 / 1.5
layout fits full row labels and intentionally retains segmented controls. An
initial test incorrectly expected dropdowns there; `wide.txt.log` verifies the
wide branch, and both screenshots were inspected. GUI rebuild passes.

## 2026-10-01 continuation: compact selected-object actions

The selected-object toolbar previously disappeared below a viewport width of
280 scaled pixels. Narrow viewports now show one full-width Object actions
button that opens the existing Focus/Properties/actions menu; wide viewports
retain the three separate buttons. No selection or edit algorithms changed.

`build/compact-object-actions-a96fe88s` passes at 800x600 / 1.5, 1024x600 / 1.5
and 1440x900 / 1.0. Actual clicks focus the selection, open its matching Properties,
duplicate it and undo back to zero document changes. The action button's recorded
bounds fit the viewport. The compact screenshot was inspected. Changes remained
in memory and were undone; no game files were written.

## 2026-10-01 continuation: tool headers remain reachable

Long tool windows previously scrolled their title and Close control out of view
(for example the compact model-import completion capture). The shared tool shell
now keeps its header outside a bounded scrolling body, and caps width against
the application window as well as the viewport. The tool content width accounts
for the body's scrollbar. Close controls have stable automation identifiers.

`build/tool-headers-khbgbuwv` exercises normal and compact captures. Six compact
tools (model import, dialogue edit, Generate, Fit, Budget and Properties) keep
identical close-control bounds before/after revealing lower content, and actual
Close clicks work. Properties uses its viewport context menu at compact size;
the old three-button selection toolbar is absent when the viewport is too narrow.
That independent layout gap remains for the next pass. Existing tool-context
closure checks pass. Dialogue browser/playback/edit/export passes again at
`build/dialogue-workspace-j01ockif` for both sizes after the shared shell change.
Compact import, terrain and properties screenshots were inspected.

## 2026-10-01 continuation: import feedback and next-draft preservation

Model import now confirms completion inside the form: pack additions identify
the object and destination and point to Mods > Deploy; direct imports identify
the object and Add an object. Starting another attempt or changing install/save
roots clears old completion feedback. The import job snapshots the three form
fields and only clears an unchanged submitted form on success. A draft entered
while the worker is running is preserved as a group.

The complete GUI workflow passes at `build/model-import-form-c1n_4er1` using
`tools/test_model_import_form.py`: both window sizes create a pack, reject a
missing model, retain entered values through a page switch, copy an OBJ/PNG with
spaces in their filenames exactly, and display completion. Copied definition
banks remain unchanged for pack additions. On copied game banks, a second draft
entered while the first import runs survives closing/reopening the form, imports
successfully next, and an unchanged completed form clears normally. Compact
completion screenshots were inspected. The check is included in `check_all.py`.

An initial reproduction used Escape directly in the edited name field; that can
legitimately cancel the text edit and is not evidence of draft loss. The final
regression explicitly commits the field by changing focus before closing, and
asserts the job is still busy when the next draft is entered. No live game writes.

## 2026-10-01 continuation: search shortcut follows the current browser

`build/asset-search-before-rfmjpo1d` reproduced Ctrl+F doing nothing in Models:
text input left all 3294 models displayed because focus was queued for the hidden
map search. Ctrl+F now reveals and focuses search in Models, Textures, Effects or
Dialogue; other pages reveal/focus the map list. Asset searches leave the map
filter unchanged. The Help shortcut description and editor guide reflect this.

`tools/test_asset_search.py` passes at `build/asset-search-an66l4o7` for 1440x900
and 800x600 / 1.5, sending actual Ctrl+F plus UTF-8 input, checking model-ID,
effect-name, dialogue-word and texture-name searches, then hidden map-list
recovery. Its first post-fix run correctly found two barrel-name matches, so the
model assertion uses exact ID 169 for its one-result expectation. The check is
included in `check_all.py`. Automation's new `input_text` command feeds ImGui's
normal text input path instead of mutating application search strings.

## 2026-10-01 continuation: compact Effects workspace

The compact capture at `build/asset-workspaces-lzbsz58e/800x600-effect.png`
showed Frame current, playback speed and background/grid controls clipped to the
right, with the preview below the fold. Effects now puts its image directly under
the selected name, with height adapted to the viewport. Transport and timing
controls wrap at the available width. Background uses a colour swatch/picker
instead of always-visible numeric fields. Counts and decode details follow the
preview controls and wrap. Rendering, simulation and framing algorithms are
unchanged.

`tools/test_effect_workspace.py` passes at `build/effect-workspace-vhu9ekk9` for
1440x900 and 800x600 / 1.5. It uses owned captures for deterministic transport,
complete control widths, grid toggle/reset, selected-tick framing and effect to
texture/model links. Grid toggles change 17065/3220 preview pixels and restore
exactly. Current framing increases visible pixels 1539 to 1842 and 414 to 679.
Pixel checks now read actual registered preview rectangles, replacing obsolete
fixed crops and fixed window dimensions; the framing gain threshold is 15% over
the complete preview rather than 30% over a partial historical crop. The compact
screenshot was inspected with the effect visible above controls. `check_all.py`
uses the owned workspace check. The renderer/simulation were not changed.

## 2026-10-01 continuation: model import window and asset preview space

At 800x600 / 1.5, the old Import model toggle revealed a form below the visible
model list (`build/asset-controls-nvws_h0o`). Import now opens a bounded, scrollable
tool window with native Browse fields, destination and import controls. Escape
closes it; leaving Models closes it without clearing entered field values. The
model-import implementation and write destinations are unchanged. Detailed model
bank information is available through the existing More hint.

Models and Effects now share Dialogue's standalone preview layout: the map list
and its strip are temporarily hidden, preserving the user's preference for return
to Maps. The Map list menu and shortcut cannot silently toggle that preference
while hidden. Normal and compact interaction checks at
`build/asset-workspaces-lzbsz58e` pass open/Escape/context closure, model display,
effect display and both shown/hidden map-list preference restoration. Screenshots
were inspected. Existing model/material/effect texture/model navigation passes
at `build/asset-links-t7i91m3j`. Compact Effects still needs its transport layout
reviewed; no claim that all effect controls fit is made in this milestone.

## 2026-10-01 continuation: dialogue seek-to-end playback

A new silent PCM transport check reproduced seeking to the end restarting the
line while playing, even with Loop disabled. `DialogueAudioPlayer::seek` now
parks at the final sample without calling Play's explicit rewind path. Explicit
Play still restarts, mid-clip seeking continues playback, paused seeking stays
paused, and Stop clears the position. Exact end seeks avoid floating-point
round-down of the final frame; successful seeks clear stale output errors.

All 36 CTest suites pass (21.41 seconds), including actual Windows audio output
with silent samples. The audio test explicitly skips with code 77 if no output
device is available. The real Dialogue workspace also passes at
`build/dialogue-workspace-fyz13zm8` (1440x900 and 800x600 / 1.5): dragging the
slider past its end stops playback, Play restarts, and subsequent timeline,
character, staged edit and byte-checked export workflows remain usable.

## 2026-10-01 continuation: standalone guide download link

Guide packaging now takes the application release link from the walkthrough,
rather than constructing a GitHub tag from the current development version.
This keeps the attachment README usable after returning the checkout to `-dev`.
An isolated guide package at `build/guide-dev-link-ohsvlosg` passes ZIP CRC and
README link checks against the published rc.3 URL. The delivered ZIP and its
published checksum were not replaced.

## 2026-10-01 continuation: terrain recipe input preflight

A missing terrain `.record` companion previously failed after the old deployment
had been reverted (`build/mod-picks-inputs-vwisfoms`, published rc.3 binary).
Recipe preflight now checks every top-level `stb/*.chunk` and its companion for
regular-file availability and readability before touching deployed or build files.
Absent optional `stb` folders remain valid. The extended failure regression passes
at `build/mod-picks-inputs-lso7tcqm`: missing records and Windows locks on either
file preserve the previous installation and output sentinel, with valid retry and
undeploy still passing. The Windows regression is included in `check_all.py`.
A valid terrain/theme/object pack also passes at
`build/pack-levels-preflight-vjrpamy5`, checking the rebuilt STB chunk, WAD level,
placed object, resolved theme palette and untouched scratch source files.
Later content/decode failures still refuse staging but may leave the previous
installation at its restored baseline; this is not transactional replacement.

## 2026-10-01 continuation: compass shared with world flight

The 3D World view now uses the same heading compass as the map viewport, with
its own camera yaw. The shared drawing helper keeps the complete label inside
the viewport; map controls reserve their footer space, while world flight uses
the free lower corner. Camera navigation and projection are unchanged.

The expanded `tools/test_compass.py` passes at `build/compass-uu1oa7h_`:
56 captures per window size (1440x900 and 800x600 / 1.5), 112 total. In addition
to the map pitch/travel/zoom/rotation cases, it checks cardinal and diagonal world
headings at downward/level/upward pitch and transfers a focus inside
TeleporterGreatwood into map-local editing without changing the needle. These
pixel checks validate the compass, not terrain rendering at every test pose.
The compass changes remain local after the published rc.3 release.

## 2026-10-01 continuation: stable heading compass

The user reported wrong directions and loss of orientation while moving the
camera. `build/compass-before-ke8gzxpz` reproduces the old compass disappearing
at a level north-facing view and reversing when looking upward. It projected two
nearby ground points, so pitch could collapse/flip the vector and distant eye
positions could lose precision during subtraction.

The compass now derives north from horizontal camera yaw (yaw zero looks along
render -z, which is Fable +y). Pitch, eye coordinates, focus distance and zoom no
longer affect its heading. Its complete label bounds stay inside the viewport and
above the view controls, including the two-row layout. Camera navigation and game
coordinates are unchanged. Automation can register the drawn overlay rectangle
without adding an interactive item.

`tools/test_compass.py` passes at `build/compass-e_blqnzu`: 40 poses each at
1440x900 and 800x600 / 1.5 scale. Rendered pixels verify cardinal/diagonal needle
directions, pitch through both near-vertical limits and the horizon, distant
positions, travel, extreme zoom, twenty full turns, and contained overlay bounds.
The map/export regression also passes at `build/ui-smoke-4a3cdn2a` with checked
GLB geometry and rendered pixels. No game writes. This correction is after the
published rc.3 tag and is not included in that ZIP.

## 2026-10-01 release handoff: rc.3 and Discord-sized walkthrough

Prepared `v0.18.0-rc.3` at 711eeec from this lane, with the user's explicit GitHub
release authorization. The full Windows ZIP is 29,320,644 bytes; the standalone
`FableForge-Aeon-Controller-Guide.zip` is 452,511 bytes, below the requested 20 MB
Discord limit. The guide-only ZIP contains AEON_CONTROLLER.html, four images and
a README linking the application release. No application or third-party mod files
are included in the small attachment. `tools/package.py --guide-only` reproduces
it; full packaging produces both ZIPs.

The rc.3 build passed all 35 core suites (27.59 seconds). Extracted-package
checks passed at `build/package-smoke-vl5q9ii0` (69 documentation links, guide
images, defs, eight retail exports, GUI pixels and literal mod paths). The guide
was separately extracted and browser-checked at `build/guide-package-dlzuzmyv`
with all four images loaded. Subsequent wording explicitly separates the guide
attachment from the application download. ZIP CRC and local-link checks pass.
A final valid recipe-pack build/order/deploy/byte-exact undeploy regression passes
at `build/release-recipes-q1fiunzf` after the recipe-failure guard.

Both uploaded ZIPs were downloaded again and matched local SHA-256 checksums at
`build/release-download-1udn9vbj`. The GitHub tag CI run is 36938872938;
it passed build, core tests, no-install GUI and packaging. Published the prerelease
at https://github.com/BuffJesus/FableForge/releases/tag/v0.18.0-rc.3.
The latest final release remains 0.17.1.
The user will send the Discord message and small ZIP; no Discord message was sent.
The copyable message is retained at `dist/Perodis-message.txt`.

## 2026-10-01 continuation: reject incomplete recipe deployments

A malformed Forge pack previously printed a recipe error but returned success and
staged the partial build. Reproduced at `build/mod-picks-inputs-9h3c5h1x`.
Pack manifests and required model/theme image sources are now checked before
reverting an existing deployment or writing build output. Missing and locked
inputs leave that deployment byte-identical. Later recipe or static-map failures
return nonzero, include errors in JSON, and never stage the incomplete output.

`tools/test_mod_picks_inputs.py` passes at `build/mod-picks-inputs-tpvdeycb`:
malformed/locked manifests, missing model/texture/theme/cliff files, prior picks
and load-order failures preserve the entire deployed tree and build sentinel.
A malformed existing OBJ and missing static-map record report failure without
staging; valid retry and undeploy restore exact original bytes. Later build
failures can still leave a previously deployed install at its restored baseline;
this is earlier validation and refusal of partial output, not transactional
replacement of the entire previous deployment. Explicit build folders may retain
incomplete output for diagnosis. No live install writes.

## 2026-10-01 continuation: conflict winner refresh and Dialogue delivery

Clearing a saved Mods winner now immediately displays the last contributor in
load order and updates win/loss badges. Previously it fell back to the stale
winner embedded in the last conflict report. Field-merged records keep their
merge label; lip sync conflicts still use their non-pickable report winner.
The dropdown also snapshots its picked state instead of retaining a map iterator
across saving a new picks map.

`tools/test_mod_winner_refresh.py` passes in the owned scratch install
`build/mod-winner-refresh-levegluf`: a saved PackA choice is cleared through the
actual dropdown, immediately shows PackB, handles vanilla/new picks and resets,
agrees with a fresh report, and deploys PackB's exact file bytes. The cleared
screenshot also verifies the badges visually. No live install writes.

Dialogue delivery is `dist/FableForge-0.18.0-dev.perodis.20261001.4-win64.zip`
(29.3 MB), built at 1856799 before this winner-display fix. Extracted-package
checks pass at `build/package-smoke-84tge2kl`: 69 local documentation links,
four guide images, defs lookup, eight retail map exports, GUI pixels and literal
mod paths from an unrelated working directory. It includes Dialogue, Browse and
the DLL/loader explanation; open AEON_CONTROLLER.html beside FableForge.exe.

## 2026-10-01 continuation: Dialogue workspace and expanded previews

Dialogue now opens with a visible line dropdown, immediate browsing and search
across all banks by words, speaker, entry name or exact Sound ID. Selecting a
result selects its bank; advanced bank/ID lookup remains available. The unrelated
map explorer is hidden without changing its saved preference. Preview character,
playback and a single optional timeline occupy the main workspace. Edit & save
stays accessible in the footer and opens a separate tool window; Review dialogue
from the unsaved prompt reopens that tool. Missing preview assets remain explicit.

Expanded the original five heads to 18 using retail adult villager variants and
the Oracle's own phoneme assets. Original indices and animation evaluation remain
unchanged. Adult hierarchy checks match bone names rather than local slot order;
child variants with different hierarchies were excluded. All 18 presets resolve,
pass core pose checks and render in the GUI. This is a preview-model selection,
not a change to the speaker assigned in-game.

Search and preset tests pass in normal and AddressSanitizer builds (existing
use-after-return workaround); all 35 CTest suites pass. Retail evidence:
- `build/dialogue-asan-faux9k8s`: targeted search/preset/headpose ASan checks.
- `build/dialogue-regression-1ii15tpm`: all 18 rendered previews.
- `build/dialogue-browser-hrvt_qu5`: browser, search and bank/ID fallback.
- `build/dialogue-edit-_pmalmyk`: multi-bank staging/export/reset, unsaved review,
  pack recipes and conflict order. Exported records independently checked.
- `build/path-browse-i6nf72ht`: real native dialogs and explicit scratch export
  at 800x600 / 1.5 scale.
- `build/dialogue-workspace-3w3pwww_`: actual picker, character, playback,
  timeline, frame insert and independently checked export at 1440x900 and
  800x600 / 1.5 scale.
- `build/dialogue-layout-57k43pgz`: normal and two compact head layouts, visible
  phoneme movement and the bandit's covered/uncovered eye. Tests crop the actual
  registered preview rectangle from their own captures, never stale shared files.
  Compact timeline review exposed a scroll-origin bug; it is fixed, and opening
  the timeline retains a useful minimum preview size with scrollable overflow.

The earlier parked Dialogue patch has been reapplied and integrated with Browse.
The editor guide includes the new workflow and a current screenshot. The Perodis
walkthrough now explicitly distinguishes Forge copying/enabling the DLL from
EgoCore loading it; manually installing that DLL is also valid. No live game
writes or runtime validation, including Retroid, are claimed.

## 2026-10-01 continuation: native Browse fields and clearer mod guide

Path fields now pair editable text with Browse: mod sources offer file/folder
choices; model and image imports filter their formats; dialogue export uses
Save As. Narrow fields stack the button. Cancel preserves the typed/selected
path, selection alone never applies a mod or writes an export, and overlong
paths are refused instead of truncated. The common picker now decodes UTF-8
initial paths and opens their parent folder; its returned UTF-8 buffer includes
space for the terminator. Other filesystem paths retain their existing narrow
Windows behavior; this is not a claim of end-to-end Unicode support.

`tools/test_path_browse.py` drives real Windows dialogs in only its own hidden
GUI process. File/folder selection with spaces, percent and ampersand paths,
Cancel preserving the source, and Save As followed by explicit BIG export pass
at 1280x900 (`build/path-browse-cu0mxe6s`) and 800x600 / 1.5x
(`build/path-browse-3zvavjpk`). The compact script first reveals the path field
then its button, since offscreen auto-sized cards do not lay out every item.
Actual Aeon GUI conflict/deploy capture reran successfully in 96 seconds; key
outputs still match the independent build. No live install writes or runtime
game checks. All three normal binaries rebuilt against the committed core.

The Perodis guide replaces its plain folder tree with three responsive source
cards explaining the folder/file distinction and FMP vs DLL roles. Step 4 uses
Browse; screenshots show current controls. Browser rendering inspected at
`build/guide-cdp-vw7ZTJ/sources.png` (subsequent wording change adds Browse).

Historical checkpoint (superseded by the Dialogue milestone above):
Dialogue redesign was temporarily preserved in
`build/dialogue-wip-1q844bsq/changes.patch` plus full file copies, based on
7a611e7. It is intentionally absent from the Browse delivery while its compact
layout was unfinished. That owned patch has since been reapplied, preserving
the new path fields. Earlier dialogue captures/tests are in
`build/dialogue-workspace-p8ecdp2k`.

## 2026-10-01 continuation: illustrated Aeon/controller handoff

Added `docs/walkthrough/aeon-controller/index.html`, a Windows preparation guide
for Perodis with four real GUI screenshots. Packages expose it at
`AEON_CONTROLLER.html`. It explains clean Steam TLC input, complete Aeon data,
FMP then DLL-only order, skipping EgoCore WAD extraction, runtime loading and
Retroid handoff. Windows PC access is assumed; Pocket 5 runtime remains untested.
No game/mod payloads are bundled and nothing was sent externally.

`python tools/test_aeon_controller.py --keep` passed at
`build/aeon_controller_bxensofm`: 70 -> 141 bindings, 843 Aeon level payloads
compared against effective output, 547 WAD replacements / 47 appends, no missing
direct model references. `tools/capture_aeon_guide.py` then used actual GUI
Check conflicts and Deploy buttons on that owned scratch root; the deployed
BIN, WAD, DLL and Mods.ini match the independent build. Four captures reviewed.
The first capture attempt exposed the ordinary automation 60-second limit;
whole-install `wait_mods` now allows 600 seconds. Final run passed in 74 seconds.
The scratch fixture includes retail graphics/textures so its base asset audit
is available. Package tests now require the guide and all four linked images.
No live-game or Retroid validation, no live install writes, no push.

Shareable package: `dist/FableForge-0.18.0-dev.perodis.20261001.2-win64.zip`
(29.2 MB), with explicit no-FreeRoam guidance. Extracted-package checks passed
at `build/package-smoke-sqngvhcl`: all 67 documentation links, four guide images,
definitions, retail exports, GUI pixels and literal-path mod deployment.
This supersedes the first Perodis ZIP, whose guide only named WAD extraction.


## 2026-10-01 continuation: bounded, literal dependency labels

`build/mod-master-labels-2se7x78k/popup.png` showed a long dependency-popup
heading clipped off-screen and a checkbox label truncated at an embedded ##.
The popup now has a viewport-bounded width/height, a wrapped heading and complete
wrapped labels beside stable checkbox IDs. Both checkbox and label clicks toggle
the requirement; the checkbox keeps keyboard interaction.

`tools/test_mod_master_labels.py` passes at 800x600/1.5
(`build/mod-master-labels-curf7m6t`) and 1280x720/1.0
(`build/mod-master-labels-9x7ekobs`). It verifies label add, reorder warning,
checkbox removal, label re-add and exact persisted requirement. The compact
capture at `build/mod-master-labels-4bm7xm41/popup.png` was visually checked:
full heading/name, including ##, fit on-screen. The original dependency/drag
workflow still passes at 800x600/1.5 (`build/mods-masters-wg7en7d9`). GUI build
passes; unchanged core/CLI gates are reused.

## 2026-10-01 continuation: extracted-package workflow and linked docs

`tools/test_package.py` extracts a local zip into an owned path containing spaces,
percent signs and an ampersand, then runs the packaged tools from an unrelated
working directory. It checks archive CRCs/paths, shipped files, linked local docs,
retail definition listing, eight GLB exports, GUI pixel/export assertions and the
literal-path mod conflict/deploy/undeploy/failure workflow. The existing GUI smoke
and mod-path harnesses accept an explicit process working directory.

The first package ran successfully but its notice linked outside the archive to
profiling docs. Following linked guides also found seven broken screenshot paths
in the nested docs/EDITOR.md. Packaging now includes the referenced profiling
and research guides, fixes the notice path and adjusts the nested editor image
paths. CLI.md was regenerated and now reflects the current 0.18.0-dev binary.

`build/package-smoke-15uuybi0` passes all 66 local documentation links and every
runtime check on `dist/FableForge-0.18.0-dev.local42709cf.3-win64.zip` (28.7 MB).
GUI evidence: `build/ui-smoke-rpanzfrd`, 18,721 vertices/36,864 triangles with
384x768 albedo and pixel checks; mod workflow: `build/mod-gui-paths-ingy_4rm`.
The earlier broken-doc evidence is `build/package-smoke-t6tzc4s0` and
`build/package-smoke-j3q6zeq7`. Packaging used --no-check with the preceding
normal/ASan, core and corpus gates; the extracted archive was then tested directly.
No release was published. A separate-machine/VM and live-game pass remain open.

## 2026-10-01 continuation: launch mod commands with literal paths

`build/mod-gui-paths-c9sz4pvk` reproduced shell expansion of a percent-delimited
folder component: the GUI displayed one save root while the mod tool received
a different scratch root. Mod commands now use a direct hidden Windows process,
quoted argv values (including trailing backslashes), inherited stdout/stderr
capture and the child's exit code. No command shell interprets the selected path.
Output is read as complete lines without inserting breaks at the old 2048-byte
read-buffer boundary.

`tools/test_mod_gui_paths.py` passes at `build/mod-gui-paths-5xmh8jfh` with spaces,
percent signs, ampersand and a trailing native backslash: conflict inspection,
chosen-winner deployment, exact undeploy, unchanged decoy install and a captured
stderr/exit-1 failure. The deployment screenshot was visually checked. Existing
choice-write recovery passes at `build/mod-pick-recovery-k8ihai2a`; dependency UI
passes at `build/mods-masters-vbjh4fui`. The normal GUI build passes. This launch
change reuses unchanged core/CLI sanitizer and 35-suite gates; it does not claim
new non-ASCII path support beyond the existing Windows narrow-path convention.

## 2026-10-01 continuation: complete mod corpus in owned workspaces

The broader corpus and dependency UI checks now use unique owned workspaces,
validated cleanup paths, adapted GUI scripts, hidden windows and retained logs.
The corpus harness propagates its discovered EgoCore compiler/text-tree settings
to later conflict/build calls; its first owned run at
`build/mods-corpus-2luay9wl` exposed that old test-environment omission. Undeploy
now checks the whole scratch file inventory, not only game.bin.

`build/mods-corpus-ulepfym_` passes all 46 CLI commands and the full GUI script:
Unofficial Fable Patch, Special Melee, F2 Melee definitions/levels, Controller
Support and WaterWader, resource replacement/addition, TNG winner/vanilla picks,
FSE quest union/id clashes, partial TNG deletion, repeat deploy and byte-exact
undeploy. The EgoCore compiler and text sources were present and exercised.
The GUI verifies provenance for 212 ArenaHallOfHeroes things, conflict choices,
reordering, enabling and removal. Its minimal graphics fixture limits visual
coverage: workflow state passes, but texture/model rendering is not asserted.

Dependency context-menu, enable, reorder and drag controls pass at 1280x720/1.0
(`build/mods-masters-i969gloy`) and 800x600/1.5
(`build/mods-masters-lalb0ime`). Both warning screenshots were visually checked;
the warning and reorder controls fit the card. Production code is unchanged by
this harness pass, so the preceding normal/ASan core gates are reused.

## 2026-10-01 continuation: preflight enabled mod sources

`build/mod-picks-inputs-lsfiuld_` reproduced a missing source error occurring
only after the previous stage had been removed. Resolving enabled sources now
rechecks their supported shape before returning the build list. Missing and
unsupported sources fail before deployment reversal. A single-file wrapper must
still contain one recognized pack; an ambiguous folder no longer picks its first
file. Disabled missing sources are ignored.

Synthetic checks cover missing/disabled sources and valid/ambiguous wrappers.
The scratch decision-input regression preserves a deployed winner through
missing-source deploy, build and conflicts failures: normal
`build/mod-picks-inputs-k_16gump`, ASan `build/mod-picks-inputs-53acxizb`.
Both focused sanitizer checks pass; normal all-target rebuild and all 35 suites
pass (29.98 seconds). This checks source availability/shape, not the validity of
every contained record; later build failures can still leave baseline files.

## 2026-10-01 continuation: unreadable orders are errors, not empty orders

`build/mod-picks-inputs-3ndk7ij5` reproduced deploy treating a read-locked
forge_mods.json as an empty order and reverting all staged files. Core load now
returns an empty order only when the file is absent; non-files, failed opens and
read failures throw. Existing callers surface the error before saving or deploy
stage reversal.

The mod-pack unit suite covers missing, directory and read-locked orders with
unchanged bytes, normally and under ASan. The extended decision-input scratch
check refuses locked-order deploy/list/add without changing the deployed root:
normal `build/mod-picks-inputs-fi7sdg_9`, ASan `build/mod-picks-inputs-xxt7n_om`.
Normal all-target rebuild and all 35 suites pass (20.41 seconds). Sanitizer CLI
and focused mod-pack builds/checks pass. The absent-file first-use behavior is
unchanged; no live game files were touched.

## 2026-10-01 continuation: preserve externally edited conflict choices

`build/mod-pick-refresh-3k185jt1` reproduced the GUI overwriting a choice added
externally after its conflict report loaded. Each choice edit now reads a fresh
candidate from disk and only adopts it after the existing recoverable save
succeeds. Failed reads preserve the cached display and refuse the edit. Switching
save roots explicitly clears old-root choices. Parsing matches CLI whitespace
and equals/tab separator handling.

`tools/test_mod_pick_refresh.py` passes at `build/mod-pick-refresh-jo7qlaiy`:
external choices survive an edit, and a lock that denies reads while allowing
replacement refuses an edit with byte-exact file and cached-choice preservation.
The failure capture at `build/mod-pick-refresh-jl9bgerq/unreadable.png` was visually
checked: prior winner and a readable error remain. The earlier locked-write,
retry and removal regression passes at `build/mod-pick-recovery-x8uq2f07`.
Normal GUI build passes; unchanged core/sanitizer gates are reused. Concurrent
writers during the brief read-to-replacement interval remain unsupported.

## 2026-10-01 continuation: keep deploy JSON parseable

`build/mod-cli-inputs-bz52ybl2` reproduced a successful redeploy whose stdout
could not be parsed as JSON: the restored-stage message preceded the report.
JSON mode now sends that diagnostic to stderr and emits a JSON result when no
mods are enabled. Human-readable commands retain their existing output.

The extended `tools/test_mod_cli_inputs.py` passes at
`build/mod-cli-inputs-x6wwtlgl`: full stdout JSON parsing for redeploy and empty
orders, expected stage counts, winner content, byte-exact undeploy, plus all 37
malformed-input preservation cases. The normal CLI build passes. This output-only
change reuses the immediately preceding sanitizer/core gates.

## 2026-10-01 continuation: read mod decisions before reverting a stage

`build/mod-picks-inputs-bak5gyhx` reproduced a missing --picks file silently
rebuilding the deployed stage with load-order defaults. The shared CLI choice
reader now rejects non-files, failed opens and failed reads. Deploy loads the
order, sources and choices before reverting its current stage, and passes the
loaded choices into the build instead of reopening the file after reversal.
An absent implicit default remains an ordinary no-choice build.

`tools/test_mod_picks_inputs.py` passes missing/directory/locked explicit choices,
a directory at the default path, malformed order preservation, a valid explicit
winner retry, no-choice fallback and byte-exact undeploy. Direct defs/qst merge
also refuse missing choices without touching existing outputs. Evidence:
normal `build/mod-picks-inputs-xjpxbl3c`, ASan `build/mod-picks-inputs-_bwwh_d5`.
The 37-case mod argument regression still passes at `build/mod-cli-inputs-lktsvt0x`.
Both CLI builds pass; unchanged core gates are reused. Later deployment failures
can still leave the install at baseline; this is decision-input preflight, not
a transactional deployment rewrite.

## 2026-10-01 continuation: validate mod commands before mutation

`build/mod-cli-inputs-j0np6i7b` reproduced `mods undeploy --typo`
silently reverting a deployed stage. The load-order command family now validates
all arguments before metadata edits or stage reversal. Unknown/extra options,
missing values and partial/overflowing decimal indices fail with usage status 2.
Supported JSON output works regardless of option order.

`tools/test_mod_cli_inputs.py` passes 37 refused inputs with exact deployed-root
hash inventories, plus valid list/build/conflicts, undeploy, add, move and remove:
normal `build/mod-cli-inputs-ondb9tz_`, ASan `build/mod-cli-inputs-8po7e9a1`.
Concurrent independent conflicts/deploy/undeploy and malformed-patch cleanup pass
at `build/mod-workspaces-oxvui_r0`. Both CLI builds pass. This parser-only change
reuses the preceding 35-suite core gate. Deploy still reverts its old stage before
building; this change does not make the complete deployment transactional.

## 2026-10-01 continuation: preserve the mod order on save failure

The new synthetic mod-order check reproduced JSON serialization failure erasing
the prior forge_mods.json. `build/mod-order-baseline-l46acp_i/test.log` retains
the failure. Core modorder::save now serializes before filesystem mutation,
closes a complete candidate in an owned .forge-mod-order-* directory, then
replaces the target. If replacement fails after saving the old file, it restores
that file; failed rollback retains and reports recovery files. Directory targets
are refused, and unrelated temporary filenames are never reused.

The mod-pack unit suite passes invalid UTF-8 refusal, a replacement lock that
still permits in-place writes, unchanged file inventories and valid retry,
normally and under ASan. `tools/test_mod_order_recovery.py` passes locked CLI
add/remove/move/disable and retry: normal `build/mod-order-recovery-6laonr4q`,
ASan CLI `build/mod-order-recovery-kvfmupsv`. The normal run also clicks the GUI
enable and reorder controls while locked, then retries after unlocking. The
failure screenshot was visually checked: both original enabled rows remain in
order with visible save errors. All copied metadata bytes and the unrelated
.tmp file survive refused edits. Normal writer builds and all 35 suites pass
(19.08 seconds). No game banks or live runtime were changed.

## 2026-10-01 continuation: Mods diagnostics fit narrow cards

The mod-pick failure screenshot exposed an unwrapped model-health warning
running beyond the Conflicts card. Load-order errors and model-health diagnostics
now wrap to the card width. Winner controls are bounded by that width; long
winner names are available on hover, and read-only lip-sync winners use a fitted
label with their full name in the tooltip.

`build/mods-warning-layout-uy7wnozm` passes hidden GUI captures requested at
1440x900/1.0, 1024x600/1.5 and 800x600/1.5. Separate compact captures scroll the
winner fully into view and show its long-name tooltip. Screenshots were visually
checked: the complete warning fits when revealed, the combo stays within the
card and the full winner name is readable on hover. The GUI rebuild passes.
This presentation-only pass reuses the preceding behavior/core checks.

## 2026-10-01 continuation: mod picks commit before the UI changes

`build/mod-pick-recovery-wmnpwe4s/locked_add.txt.log` reproduced a locked picks
file still reporting success and changing mods_picks from 0 to 1. Saving conflict
choices now prepares and closes an owned candidate, or stages removal for an
empty selection, through PendingBanks. The GUI adopts the candidate choices
only after persistence succeeds. Errors retain the earlier displayed choice and
file, and reach the Activity log. Back to retail also checks that result before
reporting success. The existing running-game/write guard remains in place.

`tools/test_mod_pick_recovery.py` creates two small conflicting packs in an owned
scratch root. `build/mod-pick-recovery-axfkjif7` passes locked add, retry, locked
last-pick removal and successful removal through four hidden GUI sessions.
State assertions verify the old choice count stays unchanged on refusal; exact
picks bytes and an unrelated .tmp file are preserved, and no owned staging
workspace remains. The locked-add screenshot was visually checked: PackB remains
the load-order winner, with a visible save error. This minimal scratch fixture
does not contain graphics.big, so model-health checking is unavailable there;
that warning is unrelated to the exercised file conflict. The GUI rebuild
passes; preceding core/ASan gates remain applicable to unchanged core code.

## 2026-10-01 continuation: GUI smoke owns its output evidence

`tools/ui_smoke.py` now copies its automation script into a unique build/ui-smoke-*
workspace and redirects screenshots/GLB there. It no longer deletes files in
shared build/ui or rewrites the checked-in script log. Source install and binary
paths can be explicit; the hidden GUI's captured output and a JSON report stay
with its evidence.

`build/ui-smoke-cl6q3jru` passes the complete button-driven smoke, all screenshot
pixel assertions and structural Greatwood_1 GLB checks (18721 vertices, 36864
triangles, 384x768 albedo). Hashes/inventory of all 326 pre-existing shared UI
files remain unchanged. The normal GUI/tools rebuild also passes. No production
behavior changed in this test-harness pass.

## 2026-10-01 continuation: explicit origins move foliage with the map

`build/export-origins-y7i1xnng` reproduced --origin 16,-32 leaving OrchardFarm
foliage at its baked world coordinates while terrain and placed things used the
requested offset. The first Y-up foliage translation changed from
(73.2373,46.9174,-6.0686) to (3273.2373,46.9174,-3174.0686), rather than the
requested (16,0,32) delta. Explicit-origin exports now load map-local foliage
and add the same Fable-space offset used for terrain and things. The loader
comment distinguishes baked world coordinates from arbitrary export offsets.
Default local and --world behavior retain their existing branches.

`tools/test_export_origins.py` compares every terrain vertex and every foliage/
thing node translation in both Y-up and Z-up GLBs, preserving rotations/scales.
It also checks --origin retains precedence when --world is supplied. Normal
`build/export-origins-gv80waea` and ASan `build/export-origins-7q3g796s` pass:
17148 foliage nodes and 227 placed-thing nodes per axis. These are read-only
retail exports into owned build directories. This CLI-only behavioral change
reuses the preceding full core gate; both CLI builds pass.

## 2026-10-01 continuation: OBJ water no longer corrupts later layers

A synthetic terrain/water/ice/foliage/object fixture reproduced two OBJ bugs:
water's position-only vertices shifted subsequent geometry, but appendObjLayer
used one shared position/UV/normal base; and the terrain map_Kd line followed the
ice material declaration, leaving terrain untextured. The failing export-suite
log is retained in `build/obj-water-baseline-d90h9n1s/test.log`.

OBJ append now tracks position and attribute bases separately across every
instance and layer, counting emitted water positions only in the former. The
terrain albedo declaration now precedes water/ice material declarations. The
synthetic test verifies distinct indices through two appended layers, bounds,
and terrain material ownership. The focused suite passes normally and under
ASan. No geometry generation, GLB output or game-file writer changed.

`tools/test_obj_water_export.py` exports TeleporterGreatwood with water and
foliage, checks every OBJ index, opens every referenced diffuse PNG and loads
the result independently through trimesh. Normal `build/obj-water-export-l0vf5w5x`
and ASan `build/obj-water-export-mq7kjuj8` pass: 148985 positions, 148257 UVs and
normals, 728 water-only positions, 123550 appended faces and 17 loaded geometries.
Both writer builds and all 35 normal suites pass (28.72 seconds). Tests read the
retail source and write only owned build outputs; no live-game check was run.

## 2026-10-01 continuation: model geometry and sidecars publish together

`build/model-export-recovery-_pg0ai4m/glb_locked_metadata.log` reproduced export
success despite a locked themes.json, after replacing the GLB and writing PNGs.
Both terrain-only and terrain-plus-layer GLB/OBJ writers now prepare their
complete declared output set through a shared publishExport helper. It preserves
basenames and relative material/image references, checks every stream write and
close, rejects outputs outside its owned workspace, deduplicates declared paths
and publishes through PendingBanks. Failure rolls back earlier replacements;
failed rollback retains and reports recovery files. Existing unrelated files
are preserved. Exports do not create install backups. This is one-map output
publication; multi-map batches remain separate exports.

`tools/test_model_export_recovery.py` passes blocked metadata for GLB and OBJ,
exact previous-file/inventory preservation, retry, structural GLB checks,
independent OBJ loading, deterministic overwrite and copied-source preservation.
Normal `build/model-export-recovery-mvnw4rgf` and ASan
`build/model-export-recovery-8wx7oko_` pass, as does the focused export suite in
both builds (including textured synthetic foliage OBJ append).
`build/model-export-smoke-bllysgse` passes eight textured retail maps with
independent trimesh loading. Every GLB is byte-identical to the retained outputs
from before this publication change.

Hidden GUI export `build/model-export-gui-_dvdkcy2` passes and produces a valid
Greatwood_1 GLB (18721 vertices, 36864 triangles, 384x768 albedo). Its export
screenshot was visually checked. A locked-sidecar GUI check in
`build/model-export-gui-refusal-vze2o3ct` reports export_ok=0 and Export failed,
retains both prior outputs and leaves no new files; its screenshot shows the
failure and filename in the activity/toast area. All 35 rebuilt normal suites
pass in 18.84 seconds. No live-game files or runtime behavior were exercised.

## 2026-10-01 continuation: complete numeric inputs for map exports

`build/export-inputs-yoisa43h/invalid_0.json` reproduced `--texels oops`
being accepted and replacing the previous GLB. The export parser now requires
complete signed 32-bit integers for texel/texture limits, finite complete floats
for tile/gain, exactly two finite origin coordinates and a y/z up axis.
Value-taking options reject empty, missing or following-option values before
install lookup or output work. Existing numeric clamps/defaults are unchanged.

`tools/test_export_inputs.py` passes 49 refusals preserving the old GLB, then
valid signed/scientific inputs. Its GLB passes container, accessor, index and
bounds checks (4225 vertices, 8192 triangles), including the requested Z-up
origin (16,-32). Copied input hashes/inventory remain unchanged. Normal
`build/export-inputs-ipsg_jkz` and ASan `build/export-inputs-khfbpafk` pass.
The retail smoke runner now accepts an explicit source install;
`build/export-numeric-smoke-epyylx5z/smoke.log` passes eight textured maps,
including two with things/foliage, and independent trimesh loading for all eight.
This CLI-only parser change reuses the preceding 35-suite core gate.

## 2026-10-01 continuation: publish ground diagnostic PNGs together

`build/ground-exports-qysgbxcd/locked_first.log` reproduced ground returning
success and announcing all three PNGs despite a locked first output. The CLI
now encodes and closes all three files in an owned .forge-ground-export-*
workspace before publishing through PendingBanks. A later replacement failure
rolls back earlier ones; success is printed only after the complete group lands.
The existing output names, current-directory destination and image encoding stay
unchanged. These standalone exports do not create install backups.

`tools/test_ground_exports.py` passes first/last output locks, a directory at
the last destination, retry, deterministic overwrite, unrelated temporary-file
preservation and exact source hashes/inventory: normal
`build/ground-exports-w7zqmxz1`, ASan `build/ground-exports-8wf_h4py`.
The retail background check now owns a unique output directory;
`build/world-background-nea4plro` passes with all 192512 uncovered pixels
matching the LEV fallback and zero false black pixels. This CLI-only change
reuses the preceding full 35-suite gate; both CLI builds pass.

## 2026-10-01 continuation: strict and bounded stitch feather inputs

`build/stitch-workspace-zjwn5uwt/invalid_0.log` reproduced `--feather oops`
being accepted as zero. world-stitch now requires complete signed 32-bit decimal
integers or auto, and rejects unknown, empty and incomplete options. Negative
integers retain automatic selection. The feather loop stops beyond both map
grids, retaining the requested falloff; its denominator uses wide addition.
Automatic selection clamps before converting to int. The seam averaging and
ordinary falloff remain unchanged.

The extended `tools/test_stitch_workspace.py` passes eleven invalid cases and
six valid boundaries (including both signed limits) without dry-run writes.
It then deploys a seam, verifies matching edge heights, preserves unrelated
scratch files and Restores the exact original inventory. Stock normal
`build/stitch-workspace-jb_r807m` and extracted ASan
`build/stitch-workspace-1h7aua5a` pass. Six normal seam samples match the retained
pre-change `stitch-workspace-fvp941k4` report exactly. All 35 rebuilt normal
suites pass in 28.34 seconds. This is offline validation; multi-map stitch
deployment still consists of separate map writes.

## 2026-10-01 continuation: prepare donor chunks before minimap writes

`build/donor-prepare-rx7gp3se/failure_report.json` reproduced failed donor
translation changing textures.big and game.bin and leaving three backup files.
Copy creation now completes the existing terrain translation before baking its
minimap. No translation or minimap encoding algorithm changed.

`tools/test_donor_preparation.py` substitutes a truncated terrain chunk in an
owned STB, verifies refusal leaves every file and hash unchanged, restores the
valid donor, creates its own-region copy with a minimap, then checks exact
Restore. Normal `build/donor-prepare-npa47atr` and AddressSanitizer
`build/donor-prepare-4b49343g` pass. The rebuilt normal 35-suite gate passes in
28.26 seconds. Later creation stages remain separate operations; this is an
earlier preparation boundary, not a transaction across all creation stages.

## 2026-10-01 continuation: full texture-tab round trip and Restore

`tools/test_textures.py` now owns a unique scratch install, script, PNG outputs
and screenshot, passes the source root explicitly to the GUI, requires its
RESULT PASS marker, and verifies exact Restore of every copied bank. It no
longer deletes a shared fixed scratch tree. Export by name and ID must be
byte-identical, and replacement still checks decoded pixel drift.

`build/textures-ui-ib1ah798` passes the hidden GUI browse, selected-object texture,
PNG export, slot replacement and DXT3 append workflow. The retained screenshot
`ui/t1_textures.png` was visually checked: the new 512x256 ATLAS_UI_TEX is selected
with the barrel texture preview and scratch write destination visible. The
appended ID is 6292; export by label and ID matches exactly. Mean absolute RGB
replacement difference is 0.020551, below the established 2.0 threshold. Restore
returns all copied files and inventory exactly (`report.json`). The final
name/ID comparison was also checked against the retained outputs. No production
code changed in this pass; the preceding 35-suite gate remains applicable.

## 2026-10-01 continuation: region text uses the core WLD writer

`build/region-text-inputs-96l54dcy/invalid_0.json` reproduced region-props
accepting an embedded quote and changing the world banks. Region display names,
definitions and minimap graphics now go through wld::File::setRegionText before
staging. The existing quoted-text rejection is reused, with NUL also refused.
Bare region values reject whitespace, quotes, semicolons and NUL; empty values
retain the core clear-field contract. Prepared WLD read-back also checks the
requested field values, beyond preserving its serialized text. Flag/framing
line edits and the rollback group remain unchanged.

The core worldinstall suite checks rejected strings leave the in-memory WLD
unchanged, including embedded NUL, and verifies apostrophes/quoted semicolons and
empty minimap clearing round-trip. It passes normally and under ASan.
`tools/test_region_text_inputs.py` passes twelve invalid cases, valid punctuation
and exact Restore: normal `build/region-text-inputs-by1qrual`, ASan
`build/region-text-inputs-6e9zbya0`. Existing region rollback, unrelated-WLD-byte,
optional-mirror and exact-Restore checks pass in normal
`build/region-recovery-sdeav56f` and ASan `build/region-recovery-u9ivee17`.
All 35 rebuilt normal suites pass (26.61s); editor/CLI/tools builds pass.

The full own-region blank workflow passes in
`build/newlevel-workspace-3u29cnrk`, including filler takeover, minimap and
entrance generation, hidden GUI palette loading and exact Restore. This
exercises empty minimap preflight followed by the populated minimap token.

## 2026-10-01 continuation: reject malformed minimap framing

`build/minimap-inputs-4fnkjjv4/0.json` reproduced minimap-bake accepting
`--framing 1,0,0tail` and replacing the prior PNG. Framing now requires exactly
three complete finite numbers and a positive scale. The command rejects
unknown/incomplete options, extra arguments, empty required values and combined
--region/--framing before lookup or export. Signed/scientific values remain
supported. bakeMinimapImage separately rejects invalid framing before template
work, including values loaded from a WLD or supplied by other callers.

`tools/test_minimap_inputs.py` passes 23 refusal cases while preserving an old
export, then verifies default and signed explicit framing produce identical PNGs
and region lookup succeeds. Normal `build/minimap-inputs-ul63jbyd` and ASan CLI
`build/minimap-inputs-xvi78an3` pass with unchanged copied banks. The worlddraft
suite covers six API framing refusals before file/pixel creation and passes in
normal and ASan builds. All 35 rebuilt normal suites pass (20.77s), and normal
editor/CLI/tools builds pass. No minimap transform or pixel algorithm changed.

## 2026-10-01 continuation: strict texture-command options

`build/texture-cli-inputs-di0nm4pm/0.json` reproduced texture-add silently
accepting `--typo`, returning success and changing textures.big. The texture
list/add/replace/export parser now refuses unknown options, missing or empty
values, extra positional arguments and empty required names/paths before install
lookup or writes. `--bank` is accepted only for list/add, and `--format` only for
add, matching their documented behavior; they no longer silently do nothing for
replace/export.

`tools/test_texture_cli_inputs.py` checks 43 rejected argument combinations,
requiring unchanged source hashes, file inventory and a prior export file. It
then exercises valid add, replace, export and bank-filtered listing, followed by
exact Restore. The core texture writer and GUI behavior are unchanged.

Normal `build/texture-cli-inputs-lw16wal2` and ASan CLI
`build/texture-cli-inputs-hyhj07cu` pass. Both CLI builds pass. The preceding
35-suite export gate (20.50s) covers the unchanged core; it was not repeated
for this CLI-only argument change.

## 2026-10-01 continuation: checked standalone PNG publication

`build/image-exports-86s5wk4_/minimap-bake_locked.log` reproduced minimap-bake
printing "wrote" and returning success when its destination was locked against
writes. A shared terrainexport::writePng helper now encodes to an owned candidate,
checks write and close, and uses PendingBanks to publish the complete file.
Texture export (including the GUI action) and minimap-bake use it. A failed
replacement keeps the previous destination; the shared rollback-failure path
retains recovery files and reports their location. No PNG encoding or minimap
pixel algorithm changed, and no backup is added for a standalone export.

`tools/test_image_exports.py` checks locked output, directory targets and a
regular file occupying the parent path for both commands. It requires no false
success message, preservation of prior output and unrelated siblings, successful
retry and deterministic overwrite. Successful PNGs are checked independently
for chunk CRCs, IEND, dimensions and complete zlib row data. Copied source
banks must remain byte-identical and owned staging must be cleaned.

Normal `build/image-exports-2soa519w` and ASan CLI
`build/image-exports-kcypwqpo` pass. Editor/CLI/tools builds and all 35 rebuilt
normal suites pass (20.50s). The GUI uses the same texture-export function;
its file picker was not exercised in this pass.

## 2026-10-01 continuation: validate world registration before minimap writes

`build/newlevel-preflight-e5pjgq27/new-level_0.json` reproduced duplicate-name
creation returning failure after changing textures.big and creating seven
backup files. Core `validateRequest` now executes the same read-only container,
name, bounds, collision and in-memory region-registration checks as installLevel,
returning before staging, backup creation or creation-marker callbacks. Both
editor creation routes invoke it before minimap writes. Blank creation supplies
its authored common record and payloads so validation uses its actual dimensions.
The installer repeats the checks at write time; this is not a slot reservation
or a prediction of later filesystem failures.

`tools/test_newlevel_preflight.py` checks duplicate names, invalid stems,
overlap and missing takeover regions in both donor and blank creation, with
additional loose-file collisions for extracted installs. Every refusal must
leave all original hashes and the entire file inventory unchanged, including
minimap banks. A valid own-region blank creation and exact Restore follow.
No world-registration or minimap encoding algorithm changed. Later I/O or
terrain-preparation failures can still require Restore for separate stages.

Normal packed `build/newlevel-preflight-0858ycd3` passes eight refusal cases;
ASan loose `build/newlevel-preflight-hzmzbfxo` passes ten, including unrelated
loose-file collisions. Both also pass valid own-region creation and exact
Restore; the loose run preserves its renamed WAD. All 35 rebuilt normal suites
pass (23.13s), with editor/CLI/tools builds successful. Remaining stale cap and
retail-texture-slot workaround paragraphs in EDITOR.md are reconciled with its
2026-09-17 findings; PLAN.md labels its old cap investigation as superseded.

## 2026-10-01 continuation: strict new-level numeric options

`build/newlevel-inputs-e3p6bfxh/0.json` reproduced `new-level --at
6400,6400tail` succeeding and changing the copied install. The two creation
commands now parse complete decimal origin pairs before install lookup. Blank
creation also parses complete size pairs, finite heights and bounded numeric
palette slots. Trailing text, overflow, whitespace, incomplete pairs and empty
explicit values are refused; leading plus signs and leading decimal zeroes
remain valid. Named themes retain the existing palette lookup.

The editor creation API separately refuses non-finite heights and theme slots
outside -1 (automatic) or 0..255 before template work. The worlddraft suite
checks those API refusals with an empty owned scratch root and no theme library,
requiring no files or success notes. Normal and ASan focused suites pass.
`tools/test_newlevel_inputs.py` checks 43 malformed options against file hashes
and inventory, then creates a valid signed/scientific-height blank level and
requires exact Restore. No terrain serialization or bake algorithm changed.

Normal `build/newlevel-inputs-swqn1qvb` and ASan
`build/newlevel-inputs-6mblu59s` pass all 43 refusals, valid creation at
(+6400,06400) with size +64x064 and height +1.25e1, and exact Restore. Normal
editor/CLI/tools builds and all 35 regression suites pass (27.98s).

## 2026-10-01 continuation: one level installer for the legacy CLI

`build/legacy-worldinstall-7tr_cais` reproduced partial WLD/BWD/WAD replacement
and deletion of unrelated `.tmp` / `.tmp2` files after a locked-STB failure in
`forge-tools world install-level`. Its duplicated writer is replaced by a
Request adapter over forgecore worldinstall. Existing command options retain
their meaning, including dedicated-region creation and default `.bak` backups.
The command now inherits bounded placement, overlap/name checks, existing BWD
mirror updates, owned staging and rollback, and extracted-level routing.
Coordinates require complete decimal integers. Explicit optional inputs must
be readable, nonempty regular files; empty inputs cannot silently select donor
bytes in the core API.

The old core 141-region-cap notes and their editor suppression are removed.
The dedicated-region note now describes save caching, reusing the existing
2026-09-17 EDITOR.md/ENGINE_RULES.md findings (CWorldMap binary loader and the
successful fresh-game region-146 AtlasIsle probe). No region-allocation or
filler-selection algorithm changed, and no new runtime probe was performed.

The new `tools/test_legacy_worldinstall.py` checks a late file-lock failure,
malformed coordinates, missing/empty custom inputs, custom TNG preservation,
mirror equality, successful retry and exact manual `.bak` recovery in packed
and loose layouts. `test_pack_world.py` now owns a unique scratch directory;
it no longer deletes a fixed directory that another test run may own.

Packed ASan `build/legacy-worldinstall-xhs_2oc9` and normal loose
`build/legacy-worldinstall-6hmxizq4` pass, including byte-exact custom TNG
preservation and six/five baseline backups respectively. Both retry logs carry
the corrected save-cache note. Normal editor/CLI/tools and ASan tools builds
pass; all 35 rebuilt normal suites pass (22.21s).

The full own-region blank workflow also passes in
`build/newlevel-workspace-2ptwk4ao`: creation, entrance, minimap bake, hidden GUI
palette check, unowned-file preservation and exact Restore.

`build/pack-world-mh87u_5p` passes the full world-pack test: A/B/AB/BA
composition, byte-identical expected BWD output, new WAD/STB entries, hidden
GUI world move and new-level export, source preservation, and reverted-override
cleanup. Its GUI script reports PASS; the retained `pack_world_view.png` was
visually checked for the selected (2048,8064) map and PackG destination.

## 2026-10-01 continuation: rollback for the core level installer

`build/worldinstall-recovery-nx0wdl36` reproduced changed world files and lost
fixed staging-file sentinels when the final STB replacement failed. The core
installer now prepares WLD, primary BWD and existing mirrors, WAD or new loose
LEV/TNG files, and STB in an exclusively owned directory. Candidates are checked
before replacement; requested backups must succeed and existing backup paths
must be regular files. A failed commit rolls back preceding replacements. If
rollback itself fails, the error reports a retained recovery directory.
TemporaryDirectory's explicit retention behavior has a focused unit check.

`tools/test_worldinstall_recovery.py` checks a locked final STB, original hashes,
unrelated staging occupants, successful retry, BWD mirror equality and exact
Restore. Packed normal `build/worldinstall-recovery-szqxx52z` and ASan
`build/worldinstall-recovery-ri5q6fju` pass, including refusal of a directory at a
backup path and byte-exact recovery from all six core `.bak` files. Those `.bak`
files are restored explicitly by the test; editor Restore does not consume them.

Loose normal `build/worldinstall-recovery-ooso6fax` also passes, preserving the
renamed WAD and checking both new loose files on retry. Existing blank-level
late-failure and marker-refusal checks pass in `build/newlevel-recovery-dpkcvfkp`
and `build/newlevel-recovery-lci3a6my`; the late-failure assertion now requires
new loose files to be rolled back, with exact Restore clearing their markers.

Normal editor/CLI/tools builds and all 35 regression suites pass (18.88s).
The focused worldinstall and temporary-directory suites also pass under ASan.

This groups the core world installation only: earlier minimap and later region
or entrance work remain separate stages. Failed rollback retention is unit
checked, but an actual rollback failure was not injected. No power-loss or
concurrent-writer guarantee is claimed. The standalone legacy forge-tools
installer still has its duplicated writer and is the next follow-up.

## 2026-10-01 continuation: bounded new-level placement

`build/worldinstall-bounds-before.log` reproduced suggestOrigin putting a donor
at the right edge beyond the world grid. The blank-level case in
`build/newlevel-placement-t2vxa_mp` reproduced an unaligned request changing
minimap banks before final installation refused it. A shared core placement
check now enforces positive dimensions, 32-unit origin alignment and the existing
8192-unit engine boundary. Both editor creation routes call it before minimap
writes; installLevel validates its actual common-record dimensions before world
mutation. The editor's extent constant now aliases the core constant. The origin
helper bounds its preferred scan, searches the remaining grid if needed, and
reports a full grid instead of returning an invalid fallback.

This reuses the established CWorld::Init / SetMapPlacement evidence documented
in EDITOR.md (0x4a6e30 / 0x4fc9c0 and the prior y=9024 transition failure); no new
engine limit or terrain algorithm is inferred. The new worldinstall unit suite
checks edge placement, invalid/extreme dimensions, alignment, full-grid refusal
and donor-width overflow. It passes normally and under ASan; all 35 rebuilt normal
suites pass (26.15s).

`tools/test_newlevel_placement.py` verifies eight donor/blank placement refusals
leave all copied banks and inventory unchanged. Normal
`build/newlevel-placement-a9ki2yiv` and ASan CLI `build/newlevel-placement-bam2l__u`
pass. Both fixtures also successfully create EdgePlacementProbe at (8128,8128)
with far corner (8192,8192), then Restore exactly (`edge_report.json`). Editor,
CLI and tools builds pass. This is placement preflight, not a transaction covering
all creation stages; later failures can still need Restore. No live-game boundary
transition was attempted.

## 2026-10-01 continuation: strict world visibility flags

`build/world-flag-inputs-gn_c43if/invalid_0.json` reproduced `region-props
--worldmap oops` hiding Greatwood and returning success. Region properties and
world-sees now require the documented literal 0 or 1. Region-property parsing
also refuses an option without a value, even after valid earlier edits, before
any install lookup/write.

`tools/test_world_flag_inputs.py` checks 23 malformed flag/incomplete-option
cases against exact copied-bank/inventory hashes, then both valid flag values
and exact Restore. Normal `build/world-flag-inputs-w0ifnvir` and ASan CLI
`build/world-flag-inputs-wlfyj6n1` pass. This change is confined to CLI argument
validation; the preceding 34-suite core gate remains applicable.

## 2026-10-01 continuation: prepare complete terrain deployments

`build/terrain-deploy-recovery-1u13l9lp/failure_report.json` reproduced a locked
STB consuming unrelated WAD/STB .atlas-tmp files despite the old LEV rollback.
Terrain deployment now prepares the loose LEV, optional WAD entry and STB together
in PendingBanks. The navigation serialization helper leaves the live document
state alone; saved terrain, navigation baseline, sound-list state and success
notes publish only after installation. WAD entry and STB chunk read-back checks
precede replacement. New loose files keep creation-marker semantics, and game
process checks bracket preparation. Pack writes use the same rollback group for
LEV/chunk/record, replacing their manual rewrite-based undo. Terrain/nav/bake
algorithms are unchanged; external-world loose saves retain their existing route.

`tools/test_terrain_deploy_recovery.py` verifies locked-STB refusal, dirty draft,
original hashes, unowned temporary files, packed/loose LEV agreement, retry and
exact Restore. Normal new-loose `build/terrain-deploy-recovery-p1i_bhsh`, normal
extracted `build/terrain-deploy-recovery-ez6vkno6`, and ASan existing-loose
`build/terrain-deploy-recovery-bqdemjek` pass. Pack last-record rollback/retry and
unchanged source hashes pass in `build/terrain-pack-recovery-1uxy_4fk` and ASan
`build/terrain-pack-recovery-qfaipla1` via `tools/test_terrain_pack_recovery.py`.

All nine background terrain cases pass (`build/terrain-async-ff8_n56e`): later
strokes, map switches, competing writes, repeated saves, separate sculpt saves,
and painted theme output agree with references. Repeated game terrain/TNG saves
and two exact Restores pass (`build/created-restore-oy65n_v6`). All 34 rebuilt
normal suites pass (20.95s); core and locked-things suites pass under ASan. The
locked-things unit expectation now checks untouched caller notes/error/dirty
state instead of requiring the removed unconditional pack rollback message.
Normal and ASan editor/CLI builds pass. These are offline/hidden-editor checks;
no new live-game terrain or navigation validation was performed. Multi-map seam
stitching remains a sequence of separate terrain deployments.

## 2026-10-01 continuation: world coordinate parsing and overflow bounds

`build/world-cli-inputs-twex1g9p/invalid_0.json` reproduced `2176tail` moving
OrchardFarm as coordinate 2176. `build/world-bounds-before.log` reproduced an
aligned near-INT_MAX X coordinate passing checkMove when its right edge overflowed.
The CLI now consumes complete signed decimal integers before looking up an install.
Move bounds and box contact/overlap additions use 64-bit arithmetic; zero/negative
map dimensions are refused. The established 32-unit alignment and 8192-unit world
extent are unchanged.

The world-draft suite checks legal edge placement, extreme X/Y, negative origins
and invalid dimensions. `tools/test_world_cli_inputs.py` checks 16 malformed
inputs, three extreme bounds, plus/leading-zero decimal input, a valid placement
and exact Restore. Normal `build/world-cli-inputs-904xe856` and ASan CLI
`build/world-cli-inputs-uuyyvh5_` pass, as do the focused normal/ASan unit suite.
All 34 rebuilt normal suites pass (24.88s); editor/CLI builds pass. The CLI test
uses copied WLD/BWD without terrain banks; packed/loose movement and the World tab
were exercised in the preceding transaction milestone.

## 2026-10-01 continuation: world-edit file group rollback

`build/world-move-recovery-rcetgvgd/failure_report.json` reproduced a locked STB
leaving OrchardFarm's WLD/BWD copies, packed and loose creature positions changed,
and consuming both unrelated .atlas-tmp files. applyWorldEdits now prepares every
affected WLD/BWD, loose TNG, WAD and STB in one PendingBanks workspace. WLD/BWD,
replacement WAD entries and changed STB chunks are read back before backup and
replacement. A late failure rolls back earlier replacements. Invalid region edits
leave no recovery metadata; success notes publish after commit, and the game
process is checked at entry and before commit. Placement/terrain/TNG algorithms
are unchanged. Unaffected WAD/STB files no longer receive redundant backups; the
older overworld test now requires backups for every changed bank.

`tools/test_world_move_recovery.py` tests late-STB refusal, original hashes,
unowned temporary files, invalid owner/visibility requests, retry, WLD/BWD/STB
placement, packed/loose TNG agreement, creature coordinate deltas and exact Restore.
Normal packed `build/world-move-recovery-ii4pgkha`, normal extracted
`build/world-move-recovery-nrhv_nnn`, and ASan packed
`build/world-move-recovery-zdjg2yva` pass (the latter two include the added invalid
region and coordinate-delta assertions). All 34 rebuilt normal suites pass (26.50s).

The hidden World tab workflow also passes on ii4pgkha's restored copy: move
refusal, queued undo/redo, batch apply/move-back, ownership and visibility changes,
and exact Restore. Its `world_gui.txt.log` records RESULT PASS; w3_applied.png was
visually inspected and shows the new selected coordinates and mapped overview.
The fixture adapts the 400-map source count and disables optional seam stitching.
Stitching remains a separate subsequent operation, and no live-game transition
was performed. This rollback handles reported file errors, not power loss.

## 2026-10-01 continuation: region-property bank rollback

`build/region-recovery-ez_o4rb2/failure_report.json` reproduced a locked final BWD
mirror leaving the WLD, primary BWD and root mirror changed. Region-property edits
now prepare and read back the WLD and each existing BWD copy in one PendingBanks
workspace, then back up all originals and install with rollback. Missing mirrors
remain absent. Notes appear only after commit; the shared writer checks close
errors, and the selected-install game guard runs before work and before commit.
The existing field-edit and binary serialization algorithms are unchanged.

`tools/test_region_recovery.py` verifies last-mirror refusal, all original hashes,
retry, unrelated WLD bytes, matching BWD copies, optional mirrors and exact Restore.
Normal `build/region-recovery-nbwtfrfc` and ASan CLI
`build/region-recovery-jil6pt_n` pass. All 34 rebuilt normal suites pass (20.09s),
and editor/CLI builds pass. No in-game region transition was exercised.

## 2026-10-01 continuation: strict minimap registration arguments

`build/minimap-cli-inputs-r8tfr13j/invalid_0.json` reproduced `minimap-register`
accepting `oops` as texture ID zero and changing game.bin. The CLI now parses the
entire unsigned 32-bit value and rejects unknown/trailing options before install
lookup or writes. Decimal, hexadecimal, octal and a single leading plus retain
their prior valid meaning; zero and UINT32_MAX remain representable.

`tools/test_minimap_cli_inputs.py` checks 17 malformed ID/option cases against
exact scratch-bank/inventory hashes, then six valid forms with exact Restore.
Normal `build/minimap-cli-inputs-qvbyzcty` and ASan CLI
`build/minimap-cli-inputs-we8i1kob` pass. This validates argument syntax and range,
not whether an arbitrary supplied ID names a suitable minimap texture.

## 2026-10-01 continuation: minimap texture/registry rollback

`build/minimap-recovery-932ujh3_/failure_report.json` reproduced a locked game.bin
leaving a new texture installed and consuming an unrelated textures.big.atlas-tmp.
Minimap baking now prepares the texture and both definition banks together. The
registry helper writes and reads back only prepared files; PendingBanks backs up
all originals before replacement and rolls back prior replacements if a later
bank fails. Standalone registration stages the definition pair the same way.
Success notes are published after installation, and the game process is checked
before committing. Minimap pixels, framing and registry serialization are unchanged.

`tools/test_minimap_recovery.py` locks the last bank, checks original hashes and
unowned-file preservation, retries full stock new-level creation, and verifies
exact Restore; it also exercises standalone registration refusal/retry/Restore.
Normal `build/minimap-recovery-d3f9nosd` and ASan CLI
`build/minimap-recovery-qshtarhw` pass. All 34 rebuilt normal suites pass (28.92s),
and the editor/CLI build passes. This is a three-bank minimap transaction, not a
transaction for all new-level stages; later failures can still require Restore.
No new GUI visual or in-game minimap rendering check was performed.

## 2026-10-01 continuation: prepare texture imports before recovery metadata

`build/texture-recovery-yxqv_6_1/invalid_report.json` reproduced an invalid PNG
creating an original backup and deleting an unrelated textures.big.atlas-tmp.
The add/replace wrapper now imports and validates into an owned PendingBanks
workspace, then checks the game process again and installs with backup preflight.
Validation failure does not publish a bank or create recovery metadata. Read/file
exceptions return through bool/error rather than escaping the GUI/CLI call. The
core native importer also checks its output stream close; texture encoding and
bank serialization are unchanged.

`tools/test_texture_recovery.py` copies real game.bin/textures.big, preserves an
unowned temporary-file sentinel, refuses invalid images and both read/rename
locks, retries add + replace, verifies an unrelated exported texture byte-exact,
and Restores the full bank and inventory exactly. Normal
`build/texture-recovery-doioront` and ASan CLI `build/texture-recovery-i3chtle9` pass.
Normal editor/CLI targets build. This tests the shared file workflow; no new GUI
visual inspection or live-game texture rendering was performed.

## 2026-10-01 continuation: owned STB compaction output

`build/compact-workspace-before.log` reproduced compaction consuming an unrelated
`<bank>.compact-tmp`. The install wrapper now prepares its compacted bank inside an
owned PendingBanks workspace, keeps the existing full payload comparison, prepares
the original backup only after verification, and replaces with rollback-aware file
operations. It checks for a game from that install both before work and before
replacement. The compaction algorithm and table/payload layout are unchanged.

The synthetic core test verifies exact compacted bytes, the original backup,
unowned-file preservation and a second already-compact result. All 34 normal CTest
suites pass (16.93s), and the core suite passes under clang-cl ASan. Normal editor
and CLI builds pass. `tools/test_chunk_write_recovery.py --compact` exercises real
copied-bank compaction, locked replacement refusal, retry and two exact Restores,
including inventory and owned-workspace cleanup. Normal evidence is
`build/chunk-write-recovery-8l_8ihxw`: 426 payloads verified, 578.1 -> 574.5 MiB.
The same real-bank check passes under ASan at
`build/chunk-write-recovery-xnv7iwgc`.
The source install is untouched; live runtime on the compacted bank was not tested.

## 2026-10-01 continuation: recover diagnostic STB writes

`build/chunk-write-recovery-mky8mf52/write_report.json` records all three baseline
facts: chunk-zcheck --write changed STB, created no original backup, and consumed
an unrelated `<bank>.atlas-tmp` file. The command now prepares output in an owned
PendingBanks workspace, reparses and verifies the intended chunk/record before
commit, rechecks the running-game guard, and installs with the original-backup
preflight. Missing prepared map/entry or mismatched bytes/audit failure reports
an error without publishing the prepared bank. Locked replacement keeps the prior
bank and cleans only owned temporary files.

`tools/test_chunk_write_recovery.py` uses real copied game.bin/STB plus an unowned
temporary-file sentinel. Greatwood_1's one-unit foliage ride exercises relayout
(3,357,180 -> 3,359,217 chunk bytes), with exact chunk/record readback and zero
audit issues. It checks the backup, byte-exact Restore, locked refusal, retry and
second exact Restore, including file inventory and owned-workspace cleanup.
Normal `build/chunk-write-recovery-kl161ie8` and ASan CLI
`build/chunk-write-recovery-q4mt3nq1` pass. No relocation, compression or
foliage algorithm changed; no live game data was written. Same-size replacement
remains on the existing core writer path and was not separately forced here.

## 2026-10-01 continuation: reject malformed entrance CLI coordinates

`build/entrance-preserve-vihzqumy/invalid_0.log` records `oops 2 3` being silently
converted into an entrance at `(0,2,3)`. The entrance CLI now parses each entire
coordinate with locale-independent from_chars, requires finite in-range floats,
and accepts exactly two/three coordinates for a write or none for a read. Missing
--install values report usage errors. Explicit plus signs and decimal exponents
remain accepted; malformed double signs, suffixes and extra/missing coordinates
are refused with exit 2 before install lookup or writes.

`tools/test_entrance_preserve.py` now checks 14 invalid argument sets against exact
bank hashes and inventory before its successful create/customize/move/idempotent
update/Restore path. Final normal `build/entrance-preserve-gdmnu978` and ASan CLI
`build/entrance-preserve-56724y6j` pass. This change is limited to entrance argument
parsing; other CLI numeric options retain their existing parsers.

## 2026-10-01 continuation: guard high-level CLI install writes

`build/cli-write-guard-isjj_or9/failure.json` records `forge entrance` returning
success and changing GTG while a synthetic Fable.exe from that scratch root ran.
The CLI now shares an explicit install-specific process preflight before writes:
blank/new level, entrance set, region properties, world move/owner/sees/stitch,
minimap registration, STB compaction, chunk-zcheck --write, and asset imports.
Existing lower-level asset/Restore guards remain in place. Entrance/world/backups
reads and compact/stitch dry runs remain available.

`tools/test_cli_write_guard.py` builds a hidden inert helper, launches only its own
scratch Fable.exe, checks all 15 command refusals and unchanged hashes/inventory,
then exercises five read/dry-run paths. After its own helper exits, entrance write
and exact Restore succeed. A similarly named sibling install process allows both
write and Restore. Final normal `build/cli-write-guard-7konlyo7` and ASan CLI
`build/cli-write-guard-wvas9mvu` pass; the initial own-root-only check also passed
at `build/cli-write-guard-6w0c4hdo`. The real game process is never stopped.

This is command preflight, not synchronization against a game launched during a
long operation. Explicit-path forge-tools container commands were not changed.

## 2026-10-01 continuation: preserve entrance object customizations

`build/gtg-preserve-before.log` reproduced custom fields disappearing when an
existing entrance moved. The writer now identifies the player-start by parsed
ScriptName plus HOLY_SITE_PLAYER_START definition, then edits only position and
orientation fields through forgecore's TNG editor. The adjacent preceding entrance
is updated with it. Other properties, components, UIDs, comments and formatting
remain intact. An unpaired player-start receives a new entrance and retains its
own data; missing physics is added. Quoted names are matched without reformatting.
Duplicate names or the same name on another definition are refused. Incidental
ScriptName text inside ScriptData no longer selects an unrelated object.

Expanded GTG core tests pass normal/ASan, including byte-exact expected deltas,
quoted names, unpaired starts, duplicate names and unrelated script text. The core
export suite also passes. `tools/test_entrance_preserve.py` copies real WLD/BWD/GTG
and game.bin, creates/moves an entrance with custom fields, checks that only the
requested position bytes change, repeats idempotently, and Restores exact bytes
and file inventory. Normal `build/entrance-preserve-b5vw_pve` and ASan CLI
`build/entrance-preserve-pdyjkzue` pass. Normal writer targets build. No live game
files were changed and no runtime entrance behavior is claimed.

## 2026-10-01 continuation: refuse incomplete region-entrance inputs

`build/gtg-before.log` reproduced an unterminated GTG map accepted for editing.
Round-trip equality alone accepted the whole broken map as a preserved tail.
The entrance writer now validates complete, positive/unique/ascending map slots,
thing-section framing and complete parsed things/components before backup or
writes. This is writer preflight; the existing read-only GTG parser is unchanged.
It rejects non-finite placement/direction, invalid level-name tokens and exhausted
UID allocation. Direction normalization uses double hypot to avoid overflow for
finite float inputs.

Empty-section comments are preserved when adding the first entrance. GTG output
is staged and checked through close before replacement; locked/read failures
preserve the old file, and success notes are emitted only after commit. Exceptions
are returned through the existing bool/error API. This does not establish new
engine semantics or full validation of arbitrary TNG property values.

New `fableforge_gtg_tests` covers malformed maps, duplicate/order errors, incomplete
things/components, invalid input/UID exhaustion, comment preservation, idempotent
updates, locked/unreadable files and successful retry. All 34 normal CTest suites
pass (19.71s); core export + GTG focused ASan tests pass (1.81s). Normal editor and
CLI builds pass. `tools/test_newlevel_workspace.py --loose` also passes at
`build/newlevel-workspace-y0_rogew`: real copied-bank own-region creation, GTG
entrance, minimap, GUI palette, renamed-WAD preservation and byte-exact Restore.
The source install was not changed; runtime region travel remains untested.

## 2026-10-01 continuation: prepare loose LEV navigation before publishing

`build/terrain-save-failure-before.log` reproduced a refused navigation patch that
had already written edited cells and cleared the terrain dirty state. A synthetic
32x32 LEV with a valid directory but unsupported navigation block version exercises
this path for both an existing output and a new output.

`Document::saveTerrainLoose` now serializes and patches navigation in an owned
PendingBanks workspace. Only after preparation succeeds does it prepare recovery
metadata and replace the target. The in-memory navigation baseline, saved terrain,
sound-list state and success notes update after replacement. Invalid navigation
leaves the original/absent target and metadata untouched, keeps the draft dirty,
and supports exact Undo. LEV save now also checks stream close errors. Serialization
and navigation algorithms are unchanged; unrelated LEV bytes retain their existing
preservation contract.

Normal 33-suite CTest passes (15.68s); core export/navigation and locked-things
checks pass under clang-cl ASan (1.38s). All normal writer targets build. Repeated
real copied-bank terrain/TNG deployment and two byte-exact Restores pass at
`build/created-restore-ifa3d56z`, including created-file markers and clean inventory.
The first post-fix run hit baseline leftovers in the new test's initially shared
fixture; that fixture now owns a unique TemporaryDirectory and the rerun passes.
No live game writes or in-game navigation claims.

## 2026-10-01 continuation: validate live-link log records and freshness

`build/livelink-status-before.log` reproduced a minute-old log initially reported
as freshly live. Polling now seeds heartbeat age from log modification time and
keeps observations per normalized install path. Re-reading the same heartbeat,
including after unrelated log activity, cannot reset its observed age. A changed
valid heartbeat starts a new observation. This is log-based status, not proof of
a running game; the process guard remains independent.

Only complete newline-terminated records are parsed. The clipped first line of
the 64-KiB tail is skipped. Hero records require a valid sequence, map and three
fully parsed finite coordinates before replacing the prior valid position. Ack
IDs/booleans are validated; message text retains internal pipes and loses only
the CRLF terminator. Unreadable logs report no live heartbeat.

Expanded offline tests cover stale/fresh logs, identical beats in separate roots,
unrelated log activity, NaN/infinity/malformed/partial records, clipped tails,
full ack messages and locked logs. Normal writer builds, focused checks and
clang-cl ASan checks pass. Live FSE execution and UI camera-follow were not run.

## 2026-10-01 continuation: complete live-link commands and owned writes

`build/livelink-command-before.log` reproduced a successful send with truncated
definition/script names. Teleport/spawn/reload now build full strings, keep the
existing three-decimal coordinate format with a fixed decimal locale, and refuse
NaN/infinite coordinates before touching the pending command. Lua strings escape
control bytes with three decimal digits, including embedded NUL followed by a
digit; quotes/backslashes retain their existing escaping. Syntax follows the
[Lua 5.0 lexical contract](https://www.lua.org/manual/5.0/manual.html#2.1).

Commands use an owned PendingBanks workspace rather than overwriting the shared
`cmd.lua.tmp`. Locked command replacement reports failure and preserves the old
command; a retry succeeds and owned workspaces are cleaned. The unowned temporary
file is untouched. The protocol is still a single latest-command slot, not a queue.

Expanded `fableforge_livelink_tests` passes normally and under clang-cl ASan:
long spawn/map strings, exact control-byte escapes, finite-coordinate refusals,
locked replacement/retry, unrelated temporary-file preservation and cleanup,
plus all earlier hook cases. Normal editor/CLI targets build. These are offline
file/protocol checks; game-side execution was not exercised.

## 2026-10-01 continuation: preserve user script around live-link hooks

Offline regression `build/livelink-before.log` reproduced removal truncating all
Lua appended after the hook. Removal now validates and removes only the complete
generated hook, preserving surrounding bytes. LF/CRLF and old embedded install
paths are recognized; modified or duplicate hooks are refused without writes.
Host paths must stay beneath FSE and cannot target the AtlasLink worker directory.

Install checks host reads and the original backup result, then stages worker/host
replacements through PendingBanks. Removal stages the host edit and command-file
deletion together. Failed commits roll back earlier replacements; checked close
errors are reported. Existing backup and retained-worker behavior is unchanged.
Same-install concurrent writers and power-loss atomicity remain unsupported.

New `fableforge_livelink_tests` uses an owned synthetic FSE tree. It covers exact
tail preservation, CRLF, old paths, reinstall, duplicate/modified hooks, invalid
backup/command paths, unreadable hosts, and Windows locks forcing rollback after
worker or host replacement. Successful retries restore the expected bytes. Normal
writer builds and the 33-suite core gate pass (16.31s); final added lock/path checks
also pass, as does the complete focused test under clang-cl AddressSanitizer
(existing nested-catch workaround). No live game or FSE runtime was exercised.

## 2026-10-01 continuation: Restore includes the standard graphics bank

Real GLB+texture import changed all four banks, but Restore omitted
`data/graphics/graphics.big` and left its backup (`build/mesh-restore-8vsljipr`).
Backup scanning now includes `data/graphics` as well as its `pc` subdirectory.
This applies to backup listing, conflict preflight and Restore.

`tools/test_mesh_restore.py` imports the existing synthetic cube fixture into
real copied banks, verifies all four backups/changes, then lists and restores
all banks byte-exactly with no extra files. Normal standard graphics path
(`build/mesh-restore-ye00564z`), normal `--graphics-pc` fallback
(`build/mesh-restore-drkfyyqr`) and ASan CLI standard path
(`build/mesh-restore-uyghjdof`) pass. Each import creates 24 vertices/12 triangles,
its collision hull, texture and OBJECT definition. All three normal writer targets
and the ASan CLI build; source install files remain untouched.

## 2026-10-01 continuation: require multiple originals to agree

Restore consumed a live stage plus differing modern/legacy originals and reported
success (`build/restore-originals-dpgglctc`). Its preflight now compares every
ordinary/legacy/overlay original for the same target. Different or unreadable
copies stop Restore before any stage or target change; identical copies remain
accepted and `--forget` clears both after recovery. Diagnostics name the records
that need an explicit baseline decision.

All three convention pairs pass with differing and identical contents, including
full-state preservation, stage retention and resolved retries: normal
`build/restore-originals-b7d7z5t0`, ASan `build/restore-originals-8843yfds`.
Existing creation conflicts, stage ownership/preflight and lock/retry/forget
scripts pass normally (`restore-conflicts-0ztti2_g`, `restore-stage-plan-y6nxlgzj`,
`restore-failures-0zmg4k99`) and under ASan (`restore-conflicts-rhgik8o0`,
`restore-stage-plan-pnt5tghg`, `restore-failures-tzhywks5`), all under `build/`.
All three normal writer targets and ASan CLI build.

## 2026-10-01 continuation: validate original-backup path types

A directory at `.forge-orig` was treated as an existing original, allowing a new
untracked loose TNG to be saved (`build/creation-marker-a7ijwtxp`). Original
lookup now validates both modern and legacy paths as regular files and reports
filesystem errors. Creation-marker preparation validates originals even if a
marker already exists.

Expanded normal (`build/creation-marker-tffaeycq`) and ASan GUI/CLI
(`build/creation-marker-6qmvlxrz`) checks pass for modern/legacy creation-marker
and original-backup directories, both new and existing target refusal, dirty
draft preservation, unchanged directory occupants, successful retry, existing
valid original retention and exact Restore/inventory. All three normal writer
targets and ASan CLI/GUI build. Regular backup contents are not inferred from
filename alone; differing backup conventions are being checked separately.

## 2026-10-01 continuation: roll back failed WAD/loose TNG deployment

A locked loose TNG was silently left stale after the WAD changed, while the GUI
reported success and cleared its draft (`build/tng-deploy-failures-gaywh3ra`,
including `baseline_report.json`). Object deployment now uses the existing
PendingBanks helper: prepare the WAD and checked loose text, complete backup
preflight, then install both. A failed target replacement rolls back earlier
replacements; dirty state is cleared only after the pair succeeds. Scratch
outputs use the helper's owned directory instead of a shared WAD temp filename.

Normal (`build/tng-deploy-failures-b662b0o7`) and ASan GUI/CLI
(`build/tng-deploy-failures-zq85g8th`) pass early locked-WAD and late locked-TNG
failures with exact prior target hashes, dirty-draft retention, workspace cleanup,
successful retry with matching WAD/loose payloads and exact Restore. The repeated
terrain/TNG creation-marker workflow still passes two exact Restores
(`build/created-restore-wyy666el`). Full build and all 32 suites pass (17.77 seconds).
As with other PendingBanks users, a failed rollback retains its recovery directory
and reports its path; this is not a power-loss transaction.

## 2026-10-01 continuation: guard active loose TNG draft saves

On an extracted install, Save draft wrote the active TNG and cleared its dirty
state while Deploy correctly refused the same running-install write
(`build/loose-save-guard-c1kr3ou9`). Save now applies the shared game-write guard
when the destination uses loose levels or the document is an external world.
Stock installs keep their separate loose draft behavior.

Normal loose GUI (`build/loose-save-guard-bf5bbo95`) and ASan GUI/CLI
(`build/loose-save-guard-p_ave4uw`) pass: both save/deploy refuse while a scratch
helper named Fable.exe runs, dirty state and the complete install remain intact,
save succeeds after that helper exits, and Restore returns exact original hashes
and inventory without recreating the WAD. Normal stock GUI
(`build/loose-save-guard-plyn_8fn`) permits the loose draft while still refusing
WAD deploy and also restores exactly. The initial stock retry reused an existing
ScriptName; the fixture now uses distinct names per run. No real game was started,
stopped or written. Normal and ASan GUI builds pass.

## 2026-10-01 continuation: running-install directory boundaries

The process guard compared raw path prefixes, so `install-other/Fable.exe`
blocked Restore for `install` (`build/install-guard-tn1n417_`). It now includes
the directory separator in the normalized install prefix. Own-install and
nested processes remain blocked; the conservative fallback when a process path
cannot be queried is unchanged.

`tools/test_install_guard.py` builds a small waiting helper in scratch, names
only those helper copies Fable.exe, and stops only its own process handles.
No real game process is launched or stopped. Normal (`build/install-guard-x1dnohu8`)
and ASan CLI (`build/install-guard-xcaxtmpe`) pass: sibling Restore succeeds,
own/nested Restore refuses without changing target or backup, uppercase/forward
slash/trailing-separator aliases remain blocked, and retry after helper exit
restores/forgets exactly. All three normal writer targets and the ASan CLI build.

## 2026-10-01 continuation: reject malformed text overrides before compilation

Removing the final closer from a Controller Support definition still produced a
successful partial build (`build/egocore-workspace-jla6w9fv`): the merge omitted
that override before defc saw it. Block extraction now rejects missing closers,
a new definition before the prior closer and missing type/name headers. Errors
from normalization identify the source file. Comments and quoted strings are
masked without changing byte offsets so their directive text cannot split blocks.

Evidence: EgoCore `Mods/ModManagerBackend.h:316` supplied the original merge
pattern; the local fable-defs compiler's `defs/src/text/mod.rs` treats a new
block before its closer as an error, and `lexer.rs:468` ends raw quoted strings
at the next quote (backslashes are literal). The block merger remains a limited
extractor, not a replacement for the full compiler grammar.

The new definition test covers invalid overrides/baselines, replacement/template
addition, comments, quoted directive text and exact untouched prefix/suffix
preservation. Normal and ASan tests pass. Full build and all 32 suites pass
(19.26 seconds). Real concurrent compiles and the malformed-mod refusal pass
normally (`build/egocore-workspace-51bydlfr`) and under the ASan host
(`build/egocore-workspace-0i789iru`); malformed input emits no build output.
Successful game.bin/names.bin hashes match the prior implementation
(`51bydlfr/baseline_comparison.json`).

## 2026-10-01 continuation: report EgoCore copy and resource I/O failures

EgoCore's tree copier discarded copy errors, allowing a locked DLL to be reported
as copied and registered (`build/egocore-io-before.log`). Resource reads could
silently become empty payloads; an unreadable source bank was also downgraded to
a warning (`build/egocore-bank-io-before.log`). Tree iteration/copies now throw on
I/O errors, resource/header reads check open/read status, and text/bank writes
check open and close/flush. An existing bank that cannot be opened fails the build.
Missing target banks and unsupported resource placements retain their existing
reported-skip behavior.

The new `fableforge_egocore_io_tests` passes normally and under ASan: locked DLL,
Mods.ini, source bank, resource, header and output-bank failures; successful retry;
correct replacement payload/header; preserved unrelated entry and source bank.
Full build and all 31 suites pass (23.59 seconds). Real concurrent text compiles,
failure refusal and DLL-only compatibility pass normally
(`build/egocore-workspace-v143znqa`) and with the ASan host
(`build/egocore-workspace-7nypzgv9`). Both output bank pairs match the pre-I/O-change
hashes (`v143znqa/baseline_comparison.json`). No native resource layout changed.
A failed build may leave already-written files in an explicit build output;
this is error reporting and deploy prevention, not transactional output rollback.

## 2026-10-01 continuation: fail incomplete EgoCore text builds

A deliberately failing compiler still produced a successful DLL-only build
(`build/egocore-workspace-srzlriok`). Mod merge now returns failure when a mod
contains Data/Defs but its text layer cannot be normalized. It stops before
writing output banks or staging its DLL; diagnostics retain the compiler reason.
Mods without Data/Defs can still build their DLL-only layer.

Normal real concurrent Controller Support compiles pass
(`build/egocore-workspace-dcoqiri5`): 14 changed records, 43 fields, one added
record, identical output banks and preserved source banks. The failure-only
probe (`build/egocore-workspace-_jw0wtg5`) verifies nonzero build/conflicts/deploy
status, no partial new output or stage, unchanged existing output, missing-text
refusal and DLL-only compatibility. ASan host runs all these cases plus real
concurrent compiles (`build/egocore-workspace-pahpvari`); the external Rust compiler
itself is not instrumented. Owned workspaces clean up on every exercised exit.
Both forge-tools builds pass. Existing redeploy ordering is unchanged: it reverts
an earlier stage before building, so a failed redeploy can leave the baseline
install with the previous mod already removed.

## 2026-10-01 continuation: recovery metadata before loose-level commit

A locked STB at the last commit step left the newly committed loose LEV/TNG
without creation markers; Restore retained both files
(`build/newlevel-recovery-axfje_04`). The core installer now offers a preparation
callback after staging and before target replacements. Both copied and blank
editor-level creation use it to prepare checked markers, so even a late failure
leaves enough metadata for Restore. The core API no longer describes its
multi-file commit as atomic; failure still requires Restore.

Normal late-failure recovery passes (`build/newlevel-recovery-t9z5fuvu`) and ASan
passes (`build/newlevel-recovery-sdfg42zu`), with exact original file inventory
and hashes after Restore. A directory blocking the second marker refuses the
commit, preserves originals and the occupant, and cleans the first marker via
Restore: normal `build/newlevel-recovery-4ylvpwmf`, ASan
`build/newlevel-recovery-ci_bz431`. The first marker-test runs redundantly hashed
the whole install inside a per-file loop and were stopped; the corrected test
hashes it once. Successful own-region loose blank creation, entrance, minimap,
GUI palette and exact Restore pass (`build/newlevel-workspace-84p9xln8`). Full
build and all 30 core suites pass (27.34 seconds). This adds recovery tracking,
not rollback of every already-committed bank or power-loss atomicity.

## 2026-10-01 continuation: checked creation markers

A directory at the creation-marker path made loose TNG Save report success and
clear the draft, despite leaving the new file untracked by Restore
(`build/creation-marker-nej29q4j`). Marker validation now rejects non-files and
marker writes check close/flush success. Loose saves report the error before
writing their target; the draft remains dirty. Existing regular markers are
retained, and an existing original backup remains the baseline when a missing
loose file is written again (no contradictory creation marker is added).

Normal and ASan GUI checks pass (`build/creation-marker-40nt1v6t`,
`build/creation-marker-6u88aspt`): modern/legacy marker-directory refusal,
draft retention, successful retry/Restore, modern/legacy original preservation,
and unchanged eight-bank hashes/inventory. All normal writer targets and ASan
CLI/GUI build. New-level installation currently adds markers after its core
commit; late commit failures are being investigated separately.

## 2026-10-01 continuation: validate stage ownership before baseline preparation

A missing staged original could be discovered only after an editor original was
rebased (`build/restore-stage-plan-ubvv1fjs`). forgecore now exposes its existing
whole-plan validation as `stage::inspectRecovery`; Restore uses that read-only
plan before any baseline change, and stage revert revalidates before applying it.
Only originals belonging to the validated stage are eligible for the existing
mtime-based rebase. An unrelated orphaned .forgebak cannot replace an editor
original just because another stage manifest exists.

A stage-created target paired with an original/legacy/overlay backup is now
reported as conflicting recovery data before writes. Restore cannot establish
whether that backup predates the stage, so it preserves both records for explicit
resolution instead of removing then resurrecting the staged file.

`tools/test_restore_stage_plan.py` passes normally
(`build/restore-stage-plan-xudnjwox`) and under ASan
(`build/restore-stage-plan-jkiak2as`): missing-plan preservation/retry, unowned
staged backup preservation and three stage-created conflicts with resolved retry.
Normal conflict/failure checks pass (`build/restore-conflicts-r_ekojt2`,
`build/restore-failures-r503rzon`); ASan failure and stage unit checks pass
(`build/restore-failures-4xtjrn8v`). Full build and all 30 suites pass (21.53 seconds),
as does the mixed-convention/staged-rebase check. The mtime heuristic within a
valid stage is unchanged; concurrent writers to one install remain unsupported.

## 2026-10-01 continuation: conflicting Restore metadata

Restore now refuses a creation marker paired with an ordinary/legacy original
or overlay backup for the same target, before any stage recovery or file change.
The diagnostic names both records. It cannot infer which record represents the
intended baseline, so it retains both for explicit resolution. Missing targets
are checked too; Windows target matching ignores case.

The pre-fix fixture (`build/restore-conflicts-wq8b7w0n`) consumed a stage and both
contradictory records while reporting success. All 12 marker/backup/missing-target
combinations now preserve the complete install and recover after explicit marker
removal: normal `build/restore-conflicts-ho0s42n8`, ASan
`build/restore-conflicts-pvder3r6`. Existing lock/retry/forget checks pass normally
(`build/restore-failures-vf8oap6b`) and under ASan
(`build/restore-failures-5xrdtzqa`); mixed conventions and staged rebase pass via
`tools/test_backups.py --keep`. All three normal writer targets build. This check
does not resolve editor originals taken after a stage created a new file.

## 2026-10-01 continuation: repeated edits retain created-file baselines

Repeated terrain writes and loose TNG synchronization used to add an original
backup to files already marked as created. The normal GUI reproduction
(`build/created-restore-rk2oohdg`) left both files after two Restores. The shared
backup helper now honors modern and legacy creation markers; TNG save/deploy
uses that helper too.

Full build and all 30 core suites pass (28.32 seconds). The actual repeated
terrain/TNG GUI workflow and two exact Restores pass normally
(`build/created-restore-2zss1uol`) and under ASan
(`build/created-restore-tyhsj1pp`): all eight original banks match and no created
files or backup artifacts remain. The first ASan run exceeded the ordinary
60-second terrain wait; the test now uses the existing 180-second file-job wait.
Already contradictory metadata from older versions is not repaired by this change.

## 2026-10-01 continuation: EgoCore compiler workspace ownership

Concurrent real Controller Support builds collided in the shared compiler tree;
one emitted only its DLL and warned that its text overrides were not applied
(`build/egocore-workspace-75z4ocxg`). EgoCore now owns a unique compiler workspace.
The directory utility lives in forgecore; the application's existing wrapper
retains its FableForge parent directory and API.

Normal and ASan host tests pass (`build/egocore-workspace-lhlmflwp`,
`build/egocore-workspace-dwr2n4jv`): both builds apply 43 fields across 14 changed
records plus one new record, produce identical banks, copy the expected DLL,
preserve original banks/old-path marker and clean owned directories. A failing
compiler fixture also cleans up. Existing compiler-failure behavior is unchanged:
the build warns and can still emit the DLL without its text overrides.

The new directory test covers unique live children, nested cleanup, exception
cleanup, unsafe-prefix rejection and sibling preservation in normal/ASan builds.
Full build and all 30 core suites pass (29.91 seconds); the full GUI fit/export/
400-tile/World-detail probe passes after utility promotion
(`build/gui-level-workspace-__bpjevw`). Remaining direct temp-path construction
in the GUI is the user-facing dialogue export destination, not scratch cleanup.
Same-output or same-install concurrent writers remain unsupported.

## 2026-10-01 continuation: definitions roundtrip workspace

Two concurrent definitions roundtrips collided writing names.bin in the shared
diagnostic directory (`build/defs-workspace-mmzmwnbk`). The diagnostic now owns
its output directory and cleans only that directory on every exit. Normal and
ASan forge-tools builds pass the concurrent semantic roundtrip check, retain an
old-directory marker and preserve source banks (`build/defs-workspace-fznpb5fw`,
`build/defs-workspace-l8_e4p6g`). Serialization behavior is unchanged.

## 2026-10-01 continuation: owned new-level helper workspaces

Own-region blank creation and the GUI palette probe overwrote all five shared
files: new-level LEV, template palette LEV, entrance LEV, minimap LEV and PNG
(`build/newlevel-workspace-1r5nu3r6`). Each helper now owns its workspace; every
scratch write, including the generated navigation replacement, is checked.
The baseline fixture was corrected to read stock-created LEVs from the WAD,
then finished its palette check and exact Restore on the same scratch tree.

`tools/test_newlevel_workspace.py` passes normal stock
(`build/newlevel-workspace-tdml8n1s`), ASan CLI+GUI stock
(`build/newlevel-workspace-kcetwpdb`) and normal loose
(`build/newlevel-workspace-j1vpt0cq`). It creates a 64x64 own-region level at
6400,6400, verifies its entrance, bakes its 256x256 minimap, opens the GUI palette,
preserves old-path markers, cleans owned folders and restores exact original
inventory/hashes. Loose mode retains the renamed WAD. Normal and ASan authored
LEV/PNG hashes match the baseline. Full build and all 29 core suites pass
(19.45 seconds). No native creation/minimap algorithms changed.

## 2026-10-01 continuation: owned GUI level and tile extraction

Preview/document/fit/export loading overwrote shared LEVs in
`build/gui-level-workspace-58r5xe19`. All resolveLevPath callers now retain a
shared workspace through their reads; neighbour/fit jobs and World-detail
futures carry ownership across the thread boundary. World overview workers
separately overwrote/deleted shared tile LEVs (`build/gui-level-workspace-mhbfevt9`);
each tile now owns a checked extraction workspace too.

`tools/test_gui_level_workspace.py --world` passes stock
(`build/gui-level-workspace-yd__eo9d`), loose (`build/gui-level-workspace-p0hwg086`)
and ASan stock (`build/gui-level-workspace-vytndxj_`). It checks fit/undo/redo,
four neighbour previews using the new `wait_neighbours`, GLB export, all 400
fresh overview tiles, one detailed terrain map, cleanup and source hashes.
Old-path markers survive; loose mode keeps the WAD renamed. The GLB is
byte-identical before/after (582060 bytes), and all 400 tile payloads match with
only per-install source-revision keys excluded (`tile_comparison.json`).
Fit and World screenshots were inspected: expected textured ground, shoreline
and water are present. The trimmed fixture excludes graphics.big; objects and
plants are not claimed. Normal and ASan GUI builds pass. No terrain/rendering
algorithm changes or live-game writes.

## 2026-10-01 continuation: navigation diagnostic extraction

`nav-lines` reproduced overwriting the shared navlines LEV
(`build/cli-level-workspace-nqpfvgzo`). Both navigation diagnostics now use the
owned CLI resolver. `tools/test_cli_level_workspace.py --navigation` passes on
normal and ASan builds (`build/cli-level-workspace-qy2xs88c`,
`build/cli-level-workspace-7blfphln`): nav-lines/nav-compare on TeleporterGreatwood
agree between WAD and loose layouts, old-path markers survive and owned folders
are removed. The nav-lines output is byte-identical before/after this change.
All eight source-bank hashes are checked after diagnostics; no navigation
algorithm or parity claim changed. This small map has no placed hull lines.
The initial loose fixture lacked the separately named GreatwoodTeleport TNG;
extracting the related script files corrected the fixture.

## 2026-10-01 continuation: owned CLI level resolution

Two concurrent `forge info TeleporterGreatwood` calls reproduced a truncated
header error from their shared extracted LEV (`build/cli-level-workspace-qn4ixe38`).
CLI level resolution now returns a path backed by a caller-owned optional
workspace, with checked extraction writes. Export/info, chunk diagnostics,
minimap and template-theme callers keep ownership through their reads; the old
single-file export deletion guard is removed. Loose and explicit paths remain
direct reads.

Normal and ASan tests pass at `build/cli-level-workspace-w8cdtx3o` and
`build/cli-level-workspace-915qi5t7`: concurrent info outputs agree, old shared
markers survive, a missing-template-theme early return cleans up, and loose/
explicit-file info matches WAD info. The fixture's initial early-return attempts
lacked WLD/BWD donor metadata and were corrected before claiming that coverage.
Full build and all 29 core suites pass (20.55 seconds). No source bank changes.
This does not cover other fixed paths such as navlines
or editor preview extraction.

## 2026-10-01 continuation: owned seam-stitch extraction

A dry-run seam check overwrote both shared stitch LEVs in
`build/stitch-workspace-9zy3oggh`. Stitch now keeps an owned workspace through
both Document loads and checks extraction writes; the in-memory documents
remain usable after cleanup. Seam algorithms and feathering are unchanged.

`tools/test_stitch_workspace.py` passes stock (`build/stitch-workspace-fvp941k4`)
and loose (`build/stitch-workspace-6lgg752f`) layouts: move TeleporterGreatwood and
OrchardFarm adjacent, inspect the seam, stitch it, compare six shared heights,
recheck tightness, and Restore to the exact original file inventory and hashes.
Old-path markers survive and owned workspaces are removed. Writer builds pass.
The same stock move/stitch/restore test passes under ASan
(`build/stitch-workspace-3hklio9s`), including document use after scratch cleanup.

## 2026-10-01 continuation: owned terrain neighbour extraction

A stock Greatwood_1 terrain save overwrote 11 unrelated LEVs under the shared
`FableForge/neighbours` folder (`build/terrain-async-zaq5m_x4`). Neighbour loads
now own a separate temporary directory for the bake; loaded LEVs retain their
in-memory data after extraction cleanup. Terrain algorithms are unchanged.

All nine background terrain cases pass on stock and extracted/renamed-WAD
layouts (`build/terrain-async-du8hg1k_`, `build/terrain-async-7l3vfo5n`). Every
old-path marker survives, owned directories are cleaned, source hashes remain
unchanged, and no active WAD appears in the loose install. The stock reference
pack's three files are byte-identical before/after this change (see
`baseline_comparison.json` in the stock result). All three writer binaries
build. Other extraction helpers still have fixed paths; no live-game writes.

## 2026-10-01 continuation: Restore comparison and cleanup

Rebase follow-up: a locked editor baseline reproduced losing the stage manifest
and retail backup before baseline preparation failed. Restore now prepares each
affected baseline from its staged original through a temporary file before
reverting the stage. A locked baseline leaves the stage and target intact; a
subsequent locked-target failure retains the prepared retail baseline plus stage
recovery. Both retry successfully. Normal and ASan fixtures pass at
`build/restore-failures-og237xq2` and `build/restore-failures-qt3y03yd`; the existing
mixed-convention backup test passes. Full build and all 29 core suites pass
(19.30 seconds). This resolves the rebase-failure concern
recorded in the preceding follow-up below. Power-loss and concurrent-writer
durability are not claimed.

Follow-up: a missing staged original reproduced ordinary Restore continuing
after stage recovery failed, overwriting the edited target with staged content
and deleting its ordinary backup under `--forget`. Restore now stops at that
failure. The fixture retains target, ordinary backup and manifest, then succeeds
after the missing staged original is supplied (`build/restore-failures-9g2sxp31`).
All three writer binaries build; the existing mixed-convention/stage-rebase
backup regression passes. This does not resolve failures during rebasing after
a successful stage revert; that path remains under review.

Restore now treats failed file reads as differing, preserves the actual replacement
error across temporary-file cleanup, and reports failed backup/creation-marker
deletion. `--forget` also removes verified unchanged backups; ordinary Restore
retries orphaned creation-marker cleanup. Unowned staged backups remain intact.

Windows lock fixtures pass in normal and ASan CLI builds:
`build/restore-failures-poc6_dkw` and `build/restore-failures-lw0xojgl`.
They cover unreadable equal-size files, locked targets, locked backups, locked
creation markers, retry, unchanged originals and orphaned staged backups.
The earlier real WAD backup is now removed with all seven source-bank hashes
preserved (`build/world-asan-jh8pspe2/forget_cleanup.json`). This supersedes the
retained-backup limitation recorded below. Full build and all 29 core suites
pass (24.05 seconds).
Live-game data was not written. Concurrent same-install writers and interrupted
stage recovery remain separate concerns.

## 2026-10-01 continuation: owned World LEV extraction

The World-save regression reproduced overwriting an unrelated LEV in the shared
`FableForge/overworld` temp directory. World move extraction now uses a separately
owned directory until File::open finishes reading its in-memory representation;
loose-level reads retain their existing path.

All four `tools/test_world_async.py` cases pass with an old-path marker preserved
and no owned LEV workspace left behind (`build/world-async-n4ar6hcf`). The complete
build and all 29 core suites pass (33.78 seconds). An ASan CLI move on a separate
seven-bank scratch install passes, then normal Restore returns every bank to its
source hash; the marker survives and extracted workspaces are gone. Evidence:
`build/world-asan-jh8pspe2/report.json`, `move.log`, `restore.log`. The first inventory
assertion was too strict: `restore --forget` retains the unchanged WAD backup.
That extra file was verified byte-identical and is recorded separately.

This isolates World move extraction, not every temporary-file path. Neighbour
bakes, stitch, level creation/minimap/entrance helpers and preview extraction
still have fixed paths to audit. No live-game files were written.

## 2026-10-01 continuation: World edits during background writes

A real pack save accepted a later move plus cancelled owner/visibility edits,
then completion cleared all three and their undo. Successful completion now
rebases current edits and only the undo steps accepted during the job against
the saved layout. Removed submitted edits mean the old baseline was requested;
they become compensating edits when the earlier values land. A monotonic undo
serial handles truncation of the 128-entry history while a write runs.

`tools/test_world_async.py` passes later edits, Put back/Revert, 132 edits during
a save, and a second save of the later draft. Independent WLD checks verify the
first saves contain only their submitted values and the repeated save contains
the final values. All eight source-bank hashes remain unchanged. Evidence:
`build/world-async-nhf7zg7r`. Focused normal/ASan WorldDraft tests pass, and all
29 CTest suites pass (26.91 seconds). The GUI build passes. These new GUI probes
write packs; direct-install and in-game paths were not newly exercised.

## 2026-10-01 continuation: World drafts during level creation

A valid new-level pack creation reproduced loss of an already queued World move
when completion invalidated and reloaded the layout. Creation now reloads the
base layout while preserving the World draft, selection and undo, using the
same separation as mod refresh.

`tools/test_mod_refresh_ui.py --create-only` passes new-map creation with pending
move/owner/visibility edits and their undo/redo/revert at
`build/mod-refresh-qdzayuje`. All eight source banks are unchanged. Independent
WLD checks confirm the created pack excludes all three queued World edits: the
TeleporterGreatwood map block is byte-identical, OrchardFarmEast retains its
owner, and Greatwood does not acquire the queued OrchardFarm visibility edge.
GUI build passes; the preceding five-case deployment test and 28-suite/ASan core
gate remain applicable. This new probe covers creation into a pack, not a new
in-game run or a direct-install creation with a World draft.

## 2026-10-01 continuation: World drafts across mod refresh

A queued World move reproduced a stale World layout after deploying a new map:
Maps grew from 400 to 401, but World stayed at 400 even after reverting the draft.
Mod completion now reloads the base layout independently of pending moves,
ownership/visibility edits, selection and undo snapshots. Loading uses a
separate layout value so a failed read does not erase the last loaded model.

The expanded `tools/test_mod_refresh_ui.py` passes all five cases at
`build/mod-refresh-j86g3qsw`. The new World-draft case verifies map counts,
pending move/owner/visibility edits and undo/redo/revert across deploy/undeploy.
It also retains a move for a map removed by undeploy: Apply refuses before
writes, then undo removes the queued edit. All eight source banks restore
byte-exactly and the pack remains unchanged. The GUI build passes; core code
is unchanged from the preceding 28-suite and focused ASan gate. This supersedes
the earlier requirement to reload World manually after clearing a draft.

## 2026-10-01 continuation: CLI mod workspace ownership

Two concurrent `mods conflicts` calls on separate scratch installs reproduced
one process deleting the other's fixed output folder. After isolating merge/
conflict/deploy folders, a valid no-op binary patch reproduced the same race in
`forge_patch_probe`. All four paths now use the existing exclusively owned
TemporaryDirectory helper, with cleanup on both return and exception.

`tools/test_mod_workspaces.py` passes concurrent conflict reports, deployments
with distinct payloads and byte-exact undeploys on two roots. A no-op game.bin
BSDIFF40 exercises BIN probes; malformed-patch and invalid-result-BIN cases
exercise failure cleanup. Sentinels in all four old shared folders remain intact,
and no owned workspace survives completion. Evidence: `build/mod-workspaces-kg3u29we`.
The affected forge-tools build passes; core code is unchanged from the preceding
28-suite/ASan gate. Concurrent writes to the same install/output remain outside
this isolation guarantee. The separate defs-roundtrip diagnostic still has a
fixed temporary folder and was not part of these mod command paths.

## 2026-10-01 continuation: failed staging and redeploy refresh

A locked target reproduced stage/apply changing earlier files without publishing
a recovery manifest. Apply now preflights targets and unowned backups, copies
all originals, and checks manifest write/close before changing any target. A
backup-read failure leaves targets unchanged and removes recovery files created
by that attempt; a later target-copy failure retains the complete plan for
unstage/undeploy. This is recoverability after reported failure, not power-loss
durability or atomic replacement of the whole install.

Normal and ASan stage tests pass failed-copy recovery, unreadable-original
refusal/cleanup, stale-backup refusal and successful replace/add/undeploy round
trips. A malformed binary patch reproduced the separate GUI bug: redeploy
restored all original banks and failed, while Maps still listed the removed map
and kept its preview. Completion now refreshes after failed writes too, preserving
the failure report. The permanent refresh script passes this case plus the three
successful cycles at `build/mod-refresh-_6r2i3pm`; source banks and the pack keep
their expected hashes. Case aliases of recovery filenames also reproduced a
backup overwrite; preflight now rejects them and duplicate Windows targets.
Focused normal/ASan tests cover these aliases. The complete build and all 28
CTest suites pass (21.89 seconds).

The first two failure-fixture attempts did not fail: requires is advisory, and
raw definition banks in a Forge pack do not take the tree merge path. Their
successful stages were undeployed and the scratch root checked against source
hashes before the actual binary-patch failure probe. No live files were changed.

## 2026-10-01 continuation: mod refresh and retryable undeploy

A valid new-level pack exposed stale map/preview state after mod jobs completed
outside the Mods panel. Completion now runs globally. Deploy/undeploy waits for
active bank readers, defers new asset/preview reads, then refreshes maps and
contexts. World workers retire before bank replacement. Unsaved map drafts and
undo remain available; removed clean maps lose their preview. Pending world
edits are retained with a reload message; that path and redirected save-root
refresh have not received the full integration probe yet.

World view also reproduced a locked STB during undeploy. The previous restore
consumed earlier backups, and retry could delete an original whose backup was
gone. Revert now preflights required originals and retains all backups plus the
manifest until restoration completes. Missing originals refuse before writes;
a later locked file leaves recovery data for retry. This does not make stage/
apply failure-atomic or serialize separate processes.

`tools/test_mod_refresh_ui.py` passes valid pack creation and three same-window
cycles: clean, World active, and unsaved object/terrain edits with undo. All eight
source banks restore byte-exactly and the pack stays unchanged. Evidence:
`build/mod-refresh-_92nue4r`; the inspected screenshot shows the added map's
textured terrain and water. The fixture omits graphics.big; no in-game claim.
Focused normal/ASan recovery checks pass missing-backup refusal, a locked second
target followed by retry, new-file removal and path-escape refusal. The complete
normal build and all 28 CTest suites pass (26.58 seconds).

Correction to earlier writer evidence: `guarded.txt.log` used an invalid 6500
origin, so creation failed. It proves busy refusal and retained edits through
failure, not successful creation. The new refresh script uses 6400 and explicitly
asserts creation success before deployment.

## 2026-10-01 continuation: overlapping GUI file operations

Several actions checked only their own future; a GUI probe confirmed that an
object pack save was accepted while new-level capture was active. The timing
probes did not demonstrate lost data, but both operations can replace/remove
the same pack paths. The GUI now shares a busy check across terrain/world writes,
level creation, compaction, model import and mod processing. Draft/object saves,
asset/pack writes, mod-order/conflict changes and save-folder changes refuse
until the active job is consumed, naming that job in the log. Editing the draft
and browsing remain available. This serializes one GUI instance, not other
processes or CLI writers.

The expanded terrain script passes refusal of draft/object saves, compaction,
mod deploy, imports, level creation and save-folder switching during a terrain
write, then successfully saves a placed object afterward. The pending terrain
still matches the reference byte-for-byte (`build/terrain-async-ebpyus7u`).
`build/write_overlap_depth/guarded.txt.log` verifies object/terrain edits made
during creation remain dirty, overlapping saves refuse, then both save to the
pack after creation completes, without changing source banks.
`mods_busy.txt.log` verifies refusals during a real mod deployment, successful
order changes afterward and byte-exact original-bank restoration on undeploy.
The install-switch regression passes. GUI build passes; core code is unchanged
from the preceding full 27-suite and focused ASan gate.

## 2026-10-01 continuation: repeated terrain pack writes

Writing two height strokes in separate pack saves reproduced differing chunk
and record bytes from writing both together, despite identical LEVs. Each pack
bake reads the install's STB, but foliage deltas and the theme-rebuild decision
used the previous saved draft as their baseline. Pack writes now read the LEV
from that same source install via levelstore, compare heights/themes/palette
against it, and reject mismatched dimensions before output. The format and bake
algorithms are unchanged. The existing owned pack-workspace helper is shared as
`TemporaryDirectory` for the short-lived baseline LEV; cleanup stays scoped to
the exclusively created child.

The expanded `tools/test_terrain_async.py` passes eight GUI cases on stock and
extracted scratch roots, including no-change repeat, two strokes saved together
versus separately, and painted themes written twice. All compared LEV/chunk/
record bytes match. Source-bank and extracted-file hashes are unchanged; the
renamed WAD stays untouched and no active WAD appears. Evidence remains at
`build/terrain-async-m_luzikk` (stock) and `build/terrain-async-thl88qq_` (loose).
A redirected missing-source probe preserves existing pack bytes and leaves the
draft dirty. Full build/all 27 CTest suites pass (22.58 seconds); focused normal
and ASan pack/snapshot checks pass. These are offline results, not in-game proof.

## 2026-10-01 continuation: terrain writes while editing

A Greatwood_1 scratch probe reproduced an inconsistent pack: sculpt, start a
terrain write, then sculpt again while it runs. The LEV matched a single-stroke
reference, but the chunk and record differed. The worker shared the mutable
active Document and read the later stroke during baking.

Terrain writes now own an independent LEV and immutable terrain states. On
successful completion the main thread advances the saved baseline; later edits
and undo remain intact. A document session token rejects completion after a
map is reopened. The current LEV/navigation baseline stays together so later
navigation patches can be reapplied without overwriting current edits.

The focused core checks pass snapshot independence, later-edit undo/redo and
reopened-document refusal. `tools/test_terrain_async.py` passes the reference,
later stroke and switch-away/reopen cases: all three pack files are byte-identical
to the reference and all six scratch source banks retain their hashes. Evidence
is retained at `build/terrain-async-37dka_zc`. The normal full build and all 27
CTest suites pass (17.70 seconds); the focused ASan checks pass too.
This isolates document memory;
separate operations committing to the same files still need a write-busy audit.

## 2026-10-01 continuation: new-level pack deployment

The full pack path passes on the stock-layout scratch install: create
ForgeDeployDepth from TeleporterGreatwood into a pack, deploy, list/read layers,
open the deployed map in the GUI, then undeploy. The capture contains WLD/BWD,
LEV/TNG and a static-map chunk/record. Deployment changes the expected four
world banks; undeploy restores all eight original bank hashes. The pack remains
byte-identical. `build/pack_deploy_depth/report.json` records the checks and
`deployed_level.png` shows textured terrain and water. The fixture omits
graphics.big, so this is not object/foliage or in-game verification.

## 2026-10-01 continuation: reverting captured overrides

The CLI reproduced a stale layer: capturing changed world bytes, then capturing
the original bytes, left the changed override in the pack. Capture now stages
removal of represented world/level/static-map overrides that match the base.
The transaction helper retains removed files for rollback until commit succeeds.
Reports name removals and clear them on failure. Capturing onto the base or
shadow directory itself is refused before writes. Absent shadow files still
leave existing pack entries alone; this is reversion of represented content.

Normal and local ASan pack tests pass world/level reversion, unrelated recipe
preservation, source/destination alias refusal, and rollback after a world
override was removed before a later locked-file failure. The CLI probe now
reports/removes its stale world layer. The expanded world-pack integration
passes real world + static-map chunk/record removal with an unrelated asset
preserved, alongside its existing merged builds and GUI captures. The custom
theme transaction regression also passes valid creation and both failure paths.
The final full build and all 27 CTest suites pass (28.06 seconds).

## 2026-10-01 continuation: isolated pack workspaces

A nested-operation fixture reproduced the fixed `pack_shadow` directory being
deleted by a second operation. Every pack edit now creates an exclusively owned
temporary directory; the World view uses a separate process-owned directory.
Cleanup removes only that owned child, on completion or exception. This isolates
working copies; it does not serialize simultaneous commits into the same pack.

Normal and local ASan pack checks pass nested-operation independence, exception
reporting, unchanged destination files and cleanup. The complete world-pack
integration passes with the new paths, including GUI world view, move and new
level; its operation/view directories were absent after the GUI exited.

## 2026-10-01 continuation: world-pack capture failures and loose levels

Capture used to replace changed world files before parsing the shadow's WAD/STB.
A malformed-archive fixture reproduced a partial overwrite of an existing pack.
World files, loose levels and chunk/record pairs now stage before the shared
commit/rollback step. Reads report failures instead of becoming empty byte arrays,
and stream completion is checked. Failed reports clear applied file/map lists.
Shadow-preparation and operation exceptions return errors through the bool API.

Normal and local ASan pack tests pass malformed archive, successful capture,
missing chunk record, and late locked-map replacement with earlier world-file
rollback. `test_pack_world.py` passes individual and combined world merges in both
orders, new WAD/STB content, and sequential GUI map move/new-level capture into one
pack. The scratch world's BWD/WLD stay unchanged. The final full relink and all
27 CTest suites pass; one earlier GUI relink waited for the active integration
process to release the executable. Recovery still excludes abrupt termination.

`test_loose_install.py` also passes extracted-level listing/export, new-level
creation, duplicate refusal and GUI object deployment. No active WAD is recreated
and the renamed WAD's hash stays unchanged. These were scratch-file checks;
new live-game verification and pack deployment/undeployment of these world edits
remain separate.

## 2026-10-01 continuation: new-level edits and restore

The compact New level form previously left no space for X/Y values beside
their step buttons. Origins now stack when necessary; shorter own-region labels
fit, and the create action names the selected game/pack destination. New-slot
eligibility no longer depends on spare filler regions (the core already treats
dedicated slots separately); exhausted-filler UI input was not separately staged.

Creation now refuses unsaved map edits. A reproduced asynchronous loss also
showed edits made during creation being discarded when completion selected the
new level. Completion now refreshes Maps but keeps the edited map and its undo
history. `test_newlevel.py` covers both refusal and edits during the worker,
alongside its copied/blank maps, sizes, entrances, minimap framing and GUI opens.
The expanded scratch suite passes; no live-game check occurred.

Actual 800x600 / 1.5-scale clicks created/opened `AtlasClicked` in a dedicated
region, then Setup > Restore restored all eight bank/container original hashes.
This exposed stale terrain left in the viewport after the selected created map
was removed. Restore now clears terrain, cached scene/selection and world state
before reloading any surviving selected map. `test_setup_restore_ui.py` passes
three size/scale cases plus created-map removal, including `preview_has_mesh=0`.
Its first added case incorrectly waited for foliage in a bankless fixture; that
unavailable-content wait was removed, and the full script then passed.

Inspected artifacts: `build/newlevel_coordinates_small.png`,
`build/newlevel_region_small.png`, `build/newlevel_restored_small.png` and
`build/ui/setup_restore_created_removed.png`. The actual-click scratch probe is
`build/newlevel_click_restore.py`; hashes are in `build/newlevel_restore_hashes.json`.
Remaining scope includes new-level pack deployment, loose-layout creation,
world movement/seams and new in-game verification.

## 2026-10-01 continuation: readable import failures

At 800x600 / 1.5 scale, a failed model pack add only exposed a truncated footer
line while its empty preview showed no toast. Model/theme cards now retain a
wrapped last-attempt error below the action. Pack failures, direct asynchronous
model failures and theme preflight failures populate it; retry clears it and
install/save-root changes clear prior errors. The activity log retains its
existing details. Automation exposes `mesh_import_failed`, `custom_theme_failed`
and the conditional `mesh_import_error` / `custom_theme_error` widgets.

Actual compact UI clicks reproduce missing-file failures and then successful
retries for both cards. Screenshots `build/import_failure_small.png` and
`build/theme_failure_small.png` were inspected: complete paths fit within the
card. `build/import_error_reset_probe.py` verifies direct error completion and
install-switch reset with unchanged fixture bytes. Existing full-palette and
two-root install-switch UI checks pass. No live install writes occurred.

## 2026-10-01 continuation: integer glTF triangle indices

Triangle indices now retain unsigned integer precision instead of passing through
float storage. Following the [glTF mesh contract](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes),
the reader rejects normalized, signed or non-scalar index accessors, reserved
maximum indices, incomplete triangles and references outside the vertex array.
Attribute/accessor references also require nonnegative integers before lookup.
The regression covers all three unsigned widths and an exact `16777217` error.

All 384 model-input checks pass in normal and local ASan builds; all 27 CTest
suites pass after a full relink. The import integration's CLI uses `forge.exe`
in addition to `forge-tools.exe`; it was rerun after relinking both, passing
OBJ/GLB bank import, collision, definitions, failure preservation and GUI
placement/undo. Earlier targeted rebuilds covered the new parser via the unit
target and GUI but had left that CLI alias stale; use both CLI targets for future
iterations. This is bounded input validation, not full glTF conformance.

## 2026-10-01 continuation: malformed OBJ input

A short vertex record was accepted with an unread coordinate. OBJ coordinate
reads now require complete finite values, including finite positions after unit
conversion. Face indices must consume the full token, be nonzero and reference
existing positions/UVs/normals; invalid optional references no longer silently
become missing attributes. Faces need at least three vertices. Relative indices,
inline comments and one-coordinate texture entries remain supported. Attribute
index resolution avoids signed narrowing of the available element count.

All 371 model-input checks pass in normal and local ASan builds. The scratch
OBJ/GLB import integration also passes geometry, collision, definition references,
backup/failure preservation and GUI placement/undo. No live game check was run.

## 2026-10-01 continuation: glTF pack geometry buffers

A `.gltf` recipe previously copied only its JSON, leaving relative binary
buffers behind. Recipe creation now stages every external buffer in an indexed
subfolder and rewrites only its URI in the packed JSON. Source JSON and binary
bytes are unchanged; other JSON fields survive. A two-buffer fixture with
matching basenames reproduces the old failure and now loads the expected
geometry from the pack. A missing second buffer leaves all pack files unchanged.
Normal and ASan pack checks pass (using the documented local ASan workaround).
The recipe integration test now builds a separate-buffer glTF cube alongside a
legacy OBJ pack; both orders, deployment and byte-exact undeployment pass on
scratch copies. The initial explicit root was the source checkout and lacked
banks; the successful run used the script's detected Steam install as its copy
source. No live install writes or in-game checks occurred.

This step collects `.gltf` geometry buffers, not external material images or
external buffers in GLB containers. Percent-encoded paths retain the direct
importer's existing limitation. Embedded data URIs need no relocation.

## 2026-10-01 continuation: entrance UID sequencing

The Clang build warned about two increments of `uid` in one concatenation
expression when creating an entrance/HSP pair. UID allocation now occurs in
two separate statements before serialization. The entrance receives the first
new UID and its start the next; existing authored pairs keep their IDs. Clang
rebuilds this file without the sequencing warning. The existing GTG scratch
tests cover two fresh IDs, moving without duplication, unrelated section bytes
and LF/CRLF preservation.
The full normal build and all 27 registered CTest suites pass after the change.

## 2026-10-01 continuation: pack edits preserve existing assets

A small file-only fixture reproduced a failed second `model.obj` add overwriting
the first model before discovering a missing texture. Recipe adds now load the
manifest first, stage each input under its recipe/role directory, then commit
the assets and manifest together using the shared replacement/rollback helper.
Manifest saves check stream completion and report replacement failures; create
and recipe-add entry points return errors instead of letting parse/write errors
escape the GUI. Existing flat asset references remain supported.

`fableforge_modpack_tests` passes missing model/theme inputs, matching filenames
across models and base/cliff images, an existing pack, a malformed manifest,
a manifest-path directory, a Windows rename lock after changed asset bytes have
been installed, and successful recipe replacement. Failed operations preserve
all prior pack files. Lip-sync pack tests, compact actual Mods interactions and
mixed legacy/nested-path recipe builds in both orders also pass, including
byte-exact undeployment. This remains reported-failure recovery, not crash atomicity.

The expanded sanitizer run isolated a toolchain issue: a standalone nested
exception probe crashes with clang-cl 22.1.8 / MSVC 14.51 and default ASan
instrumentation, matching [LLVM #215376](https://github.com/llvm/llvm-project/issues/215376).
The probe passes with `/clang:-fsanitize-address-use-after-return=never`. That
flag is now in the local `build-clang-asan` C/C++ flags; other ASan checks remain
enabled. The runtime DLL directory still needs to be on PATH. No shipping build
flags or application exception handling were changed for this toolchain issue.
Both the expanded pack test and all 356 model-input checks pass with that local
configuration. Actual compact-UI model/theme adds also pass; manifest paths are
distinct and every stored asset matches its source bytes.

## 2026-10-01 continuation: mod rows and interaction lifetime

An actual New pack click at 800x600 / 1.5 scale created the expected manifest,
relative load-order source and selected destination. Its Mods row exposed a
clipped name behind four controls. Names now use their own wrapped drag-handle
row; controls, kind/notes, conflict counts and dependency warnings have room
below. A long name is fully readable in the inspected compact capture.

Enable and dependency-checkbox actions previously reloaded the order while the
draw loop retained references into it. Both mutations are deferred until all
rows finish drawing. `tools/test_mods_masters.py` now exercises the actual popup
checkbox, enable checkbox, reorder arrow and drag handle, checks persisted order,
enabled flags and requires metadata, and still checks the composer's dependency
report. It passes at 1280x720/default scale and 800x600/1.5 scale. No game-bank
deployment is involved in these interaction checks.

## 2026-10-01 continuation: custom ground-theme failure recovery

The existing writer installed the base texture before attempting an optional
cliff image and definition save. A malformed cliff PNG reproduced a partial
write. Theme creation now uses the model importer's shared `PendingBanks`
replacement/rollback helper, prepares textures and definitions first, and reads
the definitions back before commit. Successful output still appends texture and
definition entries, changes only the donor's texture/bump fields, and retains
existing indices and one-time backups. In-place theme writes now check the
target install's running-game guard before preparation and before commit.
Recovery applies to reported failures, not process termination or power loss.

The new `tools/test_custom_theme.py` verifies a two-texture theme's definition
references and backups, then unchanged file hashes for a malformed cliff PNG
and a late Windows rename failure on game.bin. Both cases failed against the
old writer and pass with staging. The GUI checks for a free map palette slot
before creating the asset; `tools/test_custom_theme_ui.py` passes on a synthetic
full 256-slot palette with no file changes. Texture failures now include the
native image decoder's diagnostic instead of an empty external-tool message.
The full model-import suite still passes through the shared helper. Recipe-pack
checks pass both load orders, model/theme references, deployment and byte-exact
undeployment. Core export and all 356 model-input checks pass.
The 800x600 / 1.5-scale form review found a clipped heading and pack-name input.
The theme heading/placeholders are shorter, examples are available on hover,
and pack creation stacks its field/button when their labels cannot fit inline.
The selected direct destination reads `Game files (direct)` and hovering reveals
the write path. Inspected compact captures show complete labels and controls;
the install-switch and full-palette GUI checks still pass.
The 1280x720 / 1.5-scale capture also fits with inline pack controls. The final
full build succeeds and all 26 registered CTest suites pass (19.81 seconds).

## 2026-10-01 continuation: explicit save-root destinations

Changing the automation save root reproduced stale mod count, conflict picks and
pack destination. `setSaveRoot` now refreshes these through the same reset as an
accepted install switch. Reassigning the same normalized effective root preserves
an explicit pack choice. The expanded two-install UI check passes both overrides
and clearing the override, with every fixture file unchanged. It remains valid
to choose a pack outside the current order explicitly.

## 2026-10-01 continuation: scene traversal

glTF scene traversal now uses an explicit stack and rejects cycles/repeated nodes
and missing node references. It preserves depth-first instance order and inherited
transforms without recursive stack growth. Node transform arrays are checked for
shape and finite values; overflowing world matrices are rejected. An explicitly
empty or meshless scene no longer falls back to every unused mesh in the file.
Assets without scenes retain the existing mesh-library import behavior.
The model-input suite passes 356 checks in normal and AddressSanitizer builds,
including a 12,001-node chain, sibling mesh instances, inherited transforms,
mirrored winding and malformed scenes. The sanitizer initially crashed in the
JSON library's nested out-of-range exception path for a missing child; checking
the node index explicitly now produces the importer error and passes that case.
The full scratch OBJ/GLB import and reported-failure recovery suite also passes.

## 2026-10-01 continuation: accessor ranges

A triangle with a 12-byte buffer view and 36 bytes of POSITION data reproduced
an accepted out-of-view read. The reader now validates logical buffer lengths,
view ranges, accessor ranges, integer counts/offsets, component alignment and
strides before allocation and decoding. Subtraction/division bounds checks
avoid wraparound; large counts no longer narrow through an int default. Sparse
accessors are explicitly rejected instead of silently ignoring substitutions.
All 334 model-input checks pass in normal and AddressSanitizer builds, including
valid interleaved positions with separate view/accessor offsets. The full scratch
model-import suite passes, including the two unchanged-file failure cases.
Scene traversal remains a separate follow-up.

## 2026-10-01 continuation: GLB container boundaries

The model reader now checks the GLB version, total length, complete chunk headers,
aligned chunk lengths, JSON-first ordering and duplicate JSON/BIN chunks before
reading chunk data. Previously short headers could read beyond the file and an
oversized BIN chunk was silently truncated. Well-formed unknown extension chunks
remain accepted, following the Khronos glTF 2.0 GLB container specification.
The new retail-independent `fableforge_meshinput_tests` target passes 313 checks:
valid triangle geometry, unknown chunks, malformed headers/chunks and every
truncated prefix of the fixture. Normal and clang AddressSanitizer runs pass;
the latter needs LLVM's `lib/clang/22/lib/windows` on PATH for its runtime DLL.
The full scratch `tools/test_meshimport.py` also passes after this change.
Accessor validation and scene traversal remain separate follow-up work.

## 2026-10-01 continuation: failed model imports preserve bank files

A scratch import with valid definitions/texture and an invalid graphics bank
failed after replacing textures.big. The importer now prepares all changed
banks in a private destination-side directory before replacement, reads back
the staged definitions, retains one-time backups, and rolls back prior renames
if a later replacement fails. A rollback failure retains recovery files and
names their directory in the error. This is reported-failure recovery, not a
cross-file guarantee against power loss or process termination. No bank format
or imported geometry is intentionally changed. Mesh-entry metadata is copied
before appending the physics entry, avoiding use of an invalidated vector pointer.

The expanded `tools/test_meshimport.py` passes normal OBJ/GLB import, texture,
geometry, collision, definition references and backup checks. Invalid graphics
input preserves every file; a Windows handle denying rename of game.bin forces
a late commit failure and confirms restoration of the preceding three banks,
unchanged existing backups and no staging-directory leaks. The same invalid
input changed textures.big on the pre-fix executable and preserves it now.
`tools/test_recipe_packs.py` passes both model load orders, model/theme references,
scratch deployment and byte-exact undeployment with the final read-back check.

The import UI script now returns from Assets to Edit, frames the inserted object
and checks byte-exact placement undo. Its first inspected capture exposed a
scratch-root preview bug: definitions came from the redirected import root,
while the mesh lookup still read the base install's graphics.big. Map things,
baked foliage, thumbnails and the model browser now resolve the same graphics
bank, falling back to the install when the context root has no bank. The scene
loaders accept an explicit graphics override without redirecting level/STB reads.
The final mesh-import run checks one rendered mesh instance and its inspected
Arena screenshot shows the brown cube. Placement undo is byte-exact. The core
export test, texture round-trip suite and eight-map retail export smoke also
pass, including independent trimesh reads. No in-game import proof was attempted.

## 2026-10-01 continuation: install-specific mod destinations

Switching installs used to retain the previous mod order and selected pack.
A two-root synthetic probe reproduced `mods_count=1` after switching to a root
with no mods. An accepted switch now clears the pack selection, conflict report
and provenance, reloads the destination's order and conflict picks, and lets the
pack picker choose from that order. A refused switch leaves the current state.
An explicitly selected pack absent from the order is also labelled as a pack,
rather than incorrectly displaying "Directly into the game".

The expanded `tools/test_install_switch_ui.py` passes with distinct packs and
conflict picks in both roots, a dirty-edit refusal, an invalid intermediate root,
and a return to the first root. It checks every fixture file and the file set
remain unchanged. The destination screenshot shows SecondPack in the second
root. The GUI build and all three Setup restore size/scale cases pass.
No game-install files were changed. Save-root overrides used by automation are
a separate path; this change addresses the install-folder picker.

## 2026-10-01 continuation: texture previews and compact asset controls

Texture previews now preserve the decoded image's aspect ratio when constrained
by the preview height. Previously only height was capped, widening tall images.
The preview uses decoded surface dimensions, matching PNG export even when a
texture's allocated surface differs from its frame size. Texture metadata wraps;
Export and Replace stack when their labels cannot fit side by side.

Narrow Assets panels now offer a full-name page menu instead of five abbreviated
tabs. The model's Reset view button wraps below Wireframe when necessary.
The GUI build and existing `tests/ui/model_browser.txt` pass. A scrolled compact
variant covers model search, wireframe, material-to-texture navigation and refresh
at 800x600 / 1.5. The initial unscrolled variant missed offscreen controls; that
was not a successful compact check. `build/asset_page_depth.txt` clicks the new
page menu, Wireframe and Reset view at that size; its captures were inspected.

Texture captures cover BRAZIER_POLE_24 (64x256) and BARREL_BRACED_1_24 (512x256)
at requested 1280x720 and 800x600 / 1.5. The Export PNG button produces a verified
64x256 image. `python tools/test_textures.py` passes scratch replacement, append,
backup and export checks, with mean absolute round-trip pixel difference 0.02.
Evidence is under `build/ui/texture_*depth*`, `build/ui/model_*small_fixed.png`
and `build/ui/asset_page_menu_small.png`; scratch scripts are in `build`.
Checks used hidden editor windows. No game-install files were changed.

## 2026-09-30 continuation: terrain follows placed things and foliage

Grounded, unlocked `.tng` things now keep their height offset when a brush stroke
or direct vertex edit changes the ground below them. The move is part of the
terrain edit's undo step; locked and deliberately floating/buried things stay.
The older manual repair action is idempotent for an already moved small lift.
Baked STB foliage in the map preview now receives the saved-to-current ground
delta after a stroke or undo, matching the existing deployment re-seat path.
Terrain-triggered foliage refresh decodes foliage alone; repeated placed-object
import warnings are suppressed per map. `fableforge_lockedthings_tests` passes;
`tests/ui/terrain_follows_objects.txt` raises StartOakValeWest ground, observes
2,514 re-seated preview instances, undoes, and observes zero offsets without
writing to the game. The broader editor, height-pen and core export tests pass.
Terrain and object game writes remain separate; terrain write warns when object
edits still need the object write below. See `FEATURE_DEPTH_AUDIT.md` for the
broader feature review.

## 2026-09-30 continuation: linked editor tools and compact layout

The selected thing's inspector card now follows the tool card in Objects/Actors,
ahead of Quest sections, so the active object and its actions are visible without
scrolling past unrelated content. Meshless edit points are drawn within 75 world
units of the camera, while a selected point remains visible; the Objects/Actors
viewport explains why points are absent at overview distance. Terrain and Level
do not show that hint.

Floating tools are tied to their source section. Fractal and Fit close on leaving
Terrain, Budget closes on leaving Level, and Object properties closes on leaving
Objects/Actors. They also close on leaving Edit or unloading the document. Terrain
and Budget window subtitles identify their map and section; tool settings remain
available when reopened. The Fractals tool is presented as "Generate terrain" and
explains that it creates hills and valleys from a repeatable pattern. Floating
tools center over the map and narrow to its width; Generate terrain stacks its
controls and preview when space is tight. The bottom viewport controls use
aligned Show/View rows when they wrap, and coordinates and point legends move
above both rows. `tests/ui/tool_window_context.txt` passes. The GUI build and
all eight default UI-scale tour combinations pass (1280x720/1.0,
1366x768/0.8 and 1.5, 1920x1080/1.0 and 1.5, 2560x1440/0.8 and 1.0,
3840x2160/1.0 requested). Windows clamps the last request to the current
2560x1440 monitor, so true 4K remains unverified. The tour captures the Fit,
Budget and Properties windows too.

Generate terrain now names its colour image as a relative-height pattern and
shows the current ground height range alongside the actual range Apply will
write, including the engine's height clamp. It warns when the result is much
taller than the current map and reminds the user that placed objects may need
repositioning. StartOakValeWest, for example, shows current 0.0..45.1 versus
generated 243.4..624.9 at the vanilla 1000 scale. Compact, high-scale and
desktop popup captures pass.

## 2026-09-30 continuation: delete objects inside a ground rectangle

The Terrain > Region > Copy tool now retains its last rectangle with an orange
outline and a "Delete N objects inside (Del)" button. Delete uses that rectangle
while Copy is active. `Document::thingsInRect` shares the inclusive, normalized
map bounds with terrain copying; `removeThingsInRect` skips locked objects and
uses `removeWithOwned(..., false)` for a single undo step, outside-child detach,
track repair and incoming link cleanup. Core `fableforge_ownedgraph_tests` reports
178 checks passed, including an exact undo, a locked object, a child outside the
rectangle and a linked survivor. `tests/ui/clip_delete.txt` passes on retail
MayorsHouseHallway by button and Delete key, undoing each to the byte-exact
original. The before screenshot shows the retained rectangle and count.
FableWin was not launched for the requested live comparison: after the old
Fable.exe closed, a new Fable.exe process opened from the same install.

## 2026-09-30 continuation: selected radius circles

Selected switches, exits, spawners, lights and other things with a saved positive
numeric Radius field now show terrain-following 64-segment range circles in the
Edit viewport. The caption names the exact field and value; an elevation line
connects an elevated selected point to the ground centre. TriggerRadius uses
vanilla teal, while other fields use stable distinct colours. The renderer reads
current property rows each frame and hides the circles with the thing's section.
The retail MayorsHouseHallway exit has Exit Radius 2.50; its circle, caption and
elevation line were visually checked at 1024x768. Its entrance has no positive
radius. `tests/ui/radius_rings.txt` checks both selections and is in check_all.
The GUI target builds. Fable.exe remained open, so the live FableWin comparison
was not attempted; native DrawSwitchRadius evidence supports the switch case.

## 2026-09-30 continuation: meshless point meaning

The Edit viewport now distinguishes meshless points by shape and letter as well
as colour: yellow diamond M for markers, cyan circle E/I for region exit/entrance,
and purple squares with C/S/N for camera/switch/navigation or ? for another
meshless thing. A small in-view legend names them; the selected or hovered point
gets a role and name label. Hover also shows its role, script name, exact definition
and how to inspect it. The Markers chip tooltip explains the same controls.
`tests/ui/thing_glyphs.txt` passes with the retail MayorsHouseHallway region
exit/entrance selection and link workflow, and its screenshot was visually checked
at 1024x768. The debug editor was not relaunched: retail Fable.exe was running
from the same install. Static native evidence confirms per-thing edit drawing and
nearest-thing selection; the prior live FableWin run could not render things due
to a graphics-bank assert, so its exact visual labels remain unverified.

## 2026-09-30 continuation: object keyboard transforms

The Edit viewport now accepts arrow keys to nudge selected unlocked things by
0.05 Fable units, or 0.5 with Shift. Ctrl+arrows sets the four cardinal facings;
A faces the ground point under the cursor. Comma/period and PageDown/PageUp
lower/raise by 0.2, or 0.01 with Shift. Brackets rotate by 2 degrees about
world Z; Shift+brackets use Y and Alt+brackets use X. Ctrl+brackets remain the
panel shortcuts. The operations use the existing batched frame path, so selected
groups and valid owned descendants follow and one undo restores a key action.
The F1 key sheet and Objects hint list the controls. A retail
`StartOakValeWest` GUI script checks exact nudge and height deltas, cardinal
facing, three rotation axes, pointer facing and undo; it is in `check_all.py`.
The viewport Markers chip tooltip now explains its overlay colours: yellow
markers, purple other meshless things, cyan region points, with hover for names.

## 2026-09-30 continuation: eye socket depth check

With the authored blue eye meshes, a direct EYE_SET bind transform leaves the
Bandit Lieutenant's visible iris mostly behind its face; a 1.3-unit socket
recess still exposes only 8 blue pixels in the sampled region. The 0.5-unit
socket-front recess makes the villager eyes more prominent than their eyelids. The preview
now targets 0.7 units behind the closest socket surface, which leaves the
Bandit's covered eye hidden and its uncovered iris visible (0 versus more than
40 blue pixels). Neutral and voiced captures for all four human presets were
reviewed.
This depth is a visual preview estimate; no native attachment offset has been
recovered from the retail definition.

## 2026-09-30 continuation: authored lip sync eye graphics

The earlier Dialogue eye attachment used `MESH_BIPED_EYE_BROWN` at an estimated
45% scale. Retail creature definitions reveal an `EyeGraphic` embedded in the
`Graphic` field: Bandit Lieutenant, the two Bowerstone adult villagers and the
boy all name mesh ID 8112 (`MESH_EYE_BLUE_DARK`). Their `RenderSizeX` values are
1.21, 1.34, 1.34 and 1.30 respectively. The head presets record that exact
mesh and the corresponding authored size. The decoded body mesh IDs also match
the selected head presets' body families (4520, 5120, 5149, 5109). The retail
definition checker parses these values from installed `game.bin`, while the
head preset test confirms graphics.big resolves the named eye asset. All four
retail neutral and voiced head captures were inspected; the Bandit's patch
still hides its covered eye (0 blue pixels in its sampled region, over 40 on the
uncovered side). The eye depth remains a preview approximation because its
runtime attachment offset has not been recovered. This substantially reduces
the child's red socket exposure, though the red tint is present in the shipped
face texture.

## 2026-09-30 continuation: FX ground grid and head eye sizing

Assets > Effects now has a Grid checkbox beside the preview background colour.
It draws a one-unit XY ground grid behind particles, with axis colours and
contrast that respond to the selected background. It starts off to preserve
existing preview captures and saves a user's toggle in Forge settings. The FL10 renderer test checks the grid changes
rendered pixels; retail `BLOOD_POOL` automation checks toggle state and a
pixel comparison confirms 12,771 changed viewport pixels when enabled and an
exact match after disabling. The grid check is gated on retail effects in
`tools/check_all.py`.

The previous Dialogue head eye mesh was oversized and sat too prominently in
the socket. Its front was recessed 0.5 mesh units behind nearby socket geometry.
The subsequent authored eye graphic correction above supersedes that temporary
45% scale and generic brown mesh.
The same retail UI script now captures a mid-line phoneme pose for all four
human presets. Its pixel check compares each mouth with neutral: 3,985 Bandit,
3,093 female, 3,279 male and 3,104 child pixels change in the sampled regions.
All stay in frame. The shipped child face texture has a red eye surround;
the authored blue eye graphic covers more of it than the earlier preview eye.

## 2026-09-30 continuation: FX playback transport

EgoCore `300f949` is still the latest fetched `origin/master` on 2026-09-30;
its earlier particle renderer exposes speed, loop, duration and timeline seek.
Forge's Effects viewport now has those controls alongside Play, Restart and
Step. The speed choices are 0.25x, 0.5x, 1x and 2x; duration defaults to 10
seconds and can be set from 0.5 to 300 seconds. The timeline seeks on release
through a deterministic reset and fixed-tick resimulation, retaining the
fractional tick for resumed playback. Loop resets the seeded simulation at the
selected duration; Once stops there. Retail `BRAZIERFIREFINAL` GUI automation
checks 0.25x and 2x advancement, Once, Loop, and a half-duration click seek.
The simulation test checks seek versus explicit steps and fractional resume;
76 synthetic particle checks pass. A 300-second seek on both
`BRAZIERFIREFINAL` and `LARGEWATERFALL` completed in under half a second per
GUI automation process in this checkout. Existing effects browser, mesh playback and
framing checks remain green.

## 2026-09-30 continuation: current-tick FX framing

The existing Frame effect camera fits sampled particles across 90 ticks; a
short-lived effect can occupy very few pixels at the selected tick. Assets >
Effects now offers Frame current beside it, fitting only the particles and
optional light volumes currently shown. The retail `ACTIVATE_SKILL_01` UI
capture at tick 1 has 495 visible FX pixels with the full-path camera and 798
after Frame current, with 2,599 changed viewport pixels. The UI script and
pixel checker are included in `tools/check_all.py` when retail effects are
available. `CHEST_OPEN_01` remains too faint at its first tick even after
current framing; this control is a view aid rather than proof that every
effect becomes visible.

## 2026-09-30 continuation: lip sync eye attachments

The user's head-preview review exposed blank red/pale eyes. Those pixels exist
in the shipped face textures, but the head meshes also contain `EYE_SET_L/R`
bones and `graphics.big` has separate eyeball meshes. The four human presets now
resolve the eye mesh named by their retail creature definitions and skin attached eyes with the face pose. The
Bandit attaches only the uncovered eye; the other human heads attach both. The
eye mesh faces the opposite local Y direction from the
heads, so the preview turns it around. Its depth is set from nearby socket
vertices in the bind mesh and is now slightly recessed behind the socket front.
The retail eye mesh and definition size are now recovered, but the precise game actor
attachment offset remains a preview approximation. The Demon Door has no
`EYE_SET` pair and keeps its original face preview. `tests/test_headpose.cpp`
checks both attachments and independent bone following; the retail preset and
head pose checks pass. `tests/ui/dialogue_head_eyes.txt` captures all four human
heads in `build/ui/head_eyes_*.png`; the pixel check confirms the Bandit's
covered socket has no pale eyeball pixels while its uncovered eye remains
visible (0 versus more than 40 blue pixels in the sampled eye regions).

The subsequent visual review found the eye balls too large in the human head
previews. The `EYE_SET_L/R` dummy in each retail head supplies the model-space
eye mount; `attachEyes` now uses it when present and keeps the associated bone
for animation. Its position is close to the bind mount on these four heads, so
the major visible correction was using the eye mesh at native size in the
Dialogue preview, matching EgoCore's head renderer. The retail `RenderSizeX`
values remain recorded in the presets as definition data, but are not a proven
preview transform. Fresh neutral and posed captures were inspected; the Bandit
still hides the covered eye and all four human mouths visibly move. The pixel
checks now use proportions of the captured viewport so desktop resolution does
not decide whether the test passes. Exact in-game eye appearance remains to be
compared with a live retail capture.

## 2026-09-30 continuation: random mesh orientation preview

The native `CPSCUpdateNormal::UpdateAddParticle` branch uses a random unit
axis and 0..1 turn for initial orientation, and a random unit axis with the
sampled angular increment for spin when those flags are set. Forge now previews
both branches at its existing 30 Hz particle tick. A separate seeded mesh RNG
keeps the established sprite/emission sequence unchanged; native RNG sequence
parity is not claimed. Direction/game-driven orientation remains a warning.
Synthetic checks cover nontrivial spin, repeatable reset, random initial pose,
unchanged sprite output, and split-frame determinism. Retail `effects.big` has
383 mesh renderers with random initial rotation and 390 with random spin axes.
The focused FL10 WARP test now verifies rendered pixels change for simulated
random initial pose and subsequent spin, with an exact reset image. A retail
`CHEST_OPEN_01` UI probe submitted a mesh and changed its quaternion, but its
screenshots were too faint for a retail pixel claim.

## 2026-09-30 continuation: searchable dialogue lines

The Dialogue page now searches the resolved text.big index by subtitle text,
speaker or entry name, scoped to the selected language and exact lip sync bank.
Choosing a result sends its Sound ID through the existing Load line path, so
the audio, subtitle, editable frames and head remain paired. Search is capped
at 100 visible results and cached until the query, bank or language changes.
The synthetic index test checks same-ID bank isolation; the retail test and
GUI script find the Demon Door's “beefy” line in ScriptDialogue Sound ID 5080
and reject it in Dialogue's main bank. Screenshot: `build/ui/dialogue_search.png`.

## 2026-09-30 continuation: head material audit

All five retail head presets have textured materials; no bare material is
discarded by Forge's collision-hull filter. The female and male village heads
share mouth texture 2030; their face/hair materials also resolve. Exporting the
retail female face texture 1885 and child face texture 1837 shows the same
red/pale eyes seen in Forge's captures, so that colour is in the shipped art.
The hair edge was visibly coarse at the Dialogue panel's native resolution;
the head render target now uses twice the panel side before ImGui downsamples it.

## 2026-09-30 continuation: front-facing head framing

The lip sync camera inherited the Models page's 0.8-radian yaw and 0.55-radian
downward pitch. On the Bandit head this shows a near profile and hides the
mouth shape; the same angle is less useful for the other four retail presets.
Retail GUI captures at yaw 0, pitch 0.15 and zoom 0.8 show each face and the
whole Demon Door. The Dialogue preview now starts at that front view, and
Reset view restores it. `dialogue_view` makes the camera reproducible in UI
scripts; the Dialogue browser test verifies a changed view resets exactly.

## 2026-09-30 continuation: animation-only dialogue playback

EgoCore `300f949` added a lip sync viewport with an independent animation
transport, audio toggle and camera reset. Forge's Dialogue transport now plays
the pose timeline when a paired `.lut` clip is absent or muted; available audio
still drives the head from the device sample position. Switching mute during
playback preserves the current timeline position. The head viewport also has
a Reset view button. `tests/ui/dialogue_browser.txt` checks audible and muted
playback, pause, scrub, and the new controls on a retail line.

## 2026-09-30 continuation: head preview correction

The Dialogue head looked distorted because `headpose::evaluate` used 3DAF
quaternions directly in column-vector local matrices. Fable stores them for
row-vector transforms; conjugating the sampled quaternion matches EgoCore's
lip sync renderer. Before the change, the Demon Door's first phoneme moved its
mesh from Z=106..493 to Z=-457..-70, leaving it mostly outside the viewport.
After the change it stays at Z=106..493 and is fully framed. The Bandit
Lieutenant's face is also no longer stretched. The retail pose test checks
neutral identity and bounds for all 30 phoneme tracks; the Dialogue browser
script captures both the Bandit and Demon Door previews.

## 2026-09-30 continuation: 3DAF animation read path

Added `forge/animation.hpp` and `animation.cpp`, a bounded read-only 3DAF
decoder for graphics.big animation types 6, 7 and 9. It decompresses the raw
LZO image, walks nested ANRT/AOBJ/HLPR/MVEC chunks, and retains XSEQ/SEQ0 bone
names, hierarchy, quaternion and position pools, and frame palettes. A shared
`evaluate(track, seconds)` handles palette lookup and adjacent-frame pose
interpolation for the eventual skinned renderer. Fixed malformed and
interpolation fixtures pass. All 30 unique phoneme animations from the five
head presets match the independent FableTLC parser in a canonical field CRC:
1,518 object tracks. The full retail graphics.big animation corpus also parses
3,435/3,435 entries, 210,743 object tracks and 2,985 helper tracks, exactly
matching FableTLC Python. `tools/check_all.py` runs this retail pass when the
bank is installed. Bone pose blending, GPU skinning and the head viewport
remain open.

## 2026-09-30 continuation: retail head skeleton and skin decoding

`forge::meshpreview::Geometry` now retains each bone's global animation ID,
name, local parent and normalized inverse bind matrix, plus four local joint
slots and weights per skinned vertex. The reader resolves animated-block bone
palettes and vertex partitions instead of skipping them. A separate retail
test compares all five head presets with the independent FableTLC Python
`mesh_rw.clone_skeleton` and `fable_core` skin decoder: 127 bones and 4,568
skinned vertices match by canonical CRC32 across the five meshes. The old mesh
bounds test still passes. `tools/check_all.py` runs this corpus comparison when
graphics.big is installed. Animation tracks and the skinned head renderer are
the next dependencies.

## 2026-09-30 continuation: lip sync head preset audit

Added `forge/lipsync_preset.hpp` and a five-preset manifest from EgoCore's
2026-09-30 head preview. `inspectHeadPreset` resolves exact mesh and animation
names in graphics.big/MBANK_ALLMESHES, checks the retail type-5/type-9 classes,
and reports missing or ambiguous assets. The Dialogue tab lets the user choose
a preset and see its availability while the viseme timeline remains usable.
The standalone retail test finds all five meshes and 31 pose references (30
unique animations);
the Demon Door explicitly maps AH to AI and SZ to ST. The UI script switches
to that preset and checks mesh ID 3985. `tools/check_all.py` includes the
retail asset audit when graphics.big is present. Skeleton/animation decoding
and the animated head renderer remain open.

## 2026-09-30 continuation: device-clock dialogue playback

Assets > Dialogue now decodes a selected .lut clip to PCM16 and plays it through
the Windows waveform output device (`gui/dialogueaudio.*`). Play/Pause, Stop,
Loop and both timeline scrubbers share the player's sample position; the lip
sync pose follows that device clock during playback. Loading or choosing a new
bank stops the old clip. If the clip or output device is unavailable, the
viseme timeline still works. The retail GUI script proves playback starts,
device samples advance, pause stops the transport, scrub changes the pose and
Stop resets it; it also checks exact-bank lookup. The official waveform API
reference says `waveOutGetPosition` returns per-channel samples and resets its
position to zero when the device is opened or reset:
https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutgetposition
Head mesh animation, subtitles and authoring remain open.

## 2026-09-30 continuation: dialogue lip sync browser

Assets > Dialogue is a read-only browser for an explicitly selected language,
one of the four dialogue .lut banks, and a Sound ID. It loads the paired
LIPSYNC sub-bank, shows frame counts and audio/lip durations, and provides a
scrubbable per-viseme timeline in the center viewport and a compact sidebar
strip. Missing audio leaves the lip sync curve inspectable. A missing ID in the
selected bank does not search another bank. `tests/ui/dialogue_browser.txt`
checks retail English Dialogue.lut ID 2, ScriptDialogue2.lut ID 3, and the
missing ID 2 in the latter bank; `tools/check_all.py` runs it when that retail
archive is available. The GUI build and automated retail UI run pass; the
rendered screenshot is `build/ui/dialogue_browser.png`.

## 2026-09-30 continuation: lip sync pose sampling

`forge::lipsync::sample(entry, seconds)` now interpolates weighted keys between
adjacent frames and reports the unused closed-mouth weight for the preview.
It normalizes keys when their combined weight exceeds one, as EgoCore's head
pose code does; a retail scrub screenshot exposed this case.
It clamps before the first and after the last frame, and treats empty frames as
silence. Fixed two-frame and silence fixtures pass; the retail codec/upsert
suite still passes all 20,214 nonempty English entries. The browser now passes
the audio player's position to this function while playing and the scrubber
time while paused. The head renderer remains open.

## 2026-09-30 continuation: dialogue Xbox ADPCM decode

Added `forge/xboxadpcm.hpp` and `xboxadpcm.cpp` for Xbox IMA ADPCM (WAVE tag
0x0069), plus `forge::lut::File::pcm16(index)` to decode only the requested
clip's data. `File::wavPcm16(index)` wraps that PCM in a standard RIFF/WAVE.
The decoder accepts mono and stereo 36-byte channel blocks and
returns 64 interleaved PCM16 frames per block; it rejects partial blocks and
unsupported layouts. Fixed mono/stereo fixtures and one clip from each of the
four retail English dialogue banks match the independent FableTLC Python
decoder's sample counts and PCM CRC32. The retail test also checks the WAV
header and first sample. `fableforge_lut_tests` and its retail pass succeed.
The Sounds page remains open.

## 2026-09-30 continuation: dialogue LUT reader and lip sync join

Added `forge/lut.hpp` and `lut.cpp`, a lazy reader for the four dialogue .lut
banks. It validates the 44-byte header, each 36-byte clip record, RIFF chunks,
the tail lookup count and every lookup offset/size. `riff(index)` reads only the
requested audio blob. `dialoguePair` maps a text.big SpeechBank and selected
language to exactly one .lut, one LIPSYNC sub-bank and one snds.bin; unknown
names are rejected without cross-bank or cross-language fallback.

The fixed fixture and all four installed English banks pass. The counts are
12,134 + 1 + 5,310 + 2,769 = 20,214 clips (the FableTLC AUDIO.md aggregate
"20,213" is an arithmetic error). Every audio index joins a nonempty entry in
its paired dialogue.big sub-bank. Across all 20,214 joins, the maximum difference
between the .lut duration and lipsync Info is 0.000233 seconds. The retail join
is in `tools/check_all.py` when the English bank is installed.

## 2026-09-30 continuation: lip sync archive upsert

`forge::lipsync::upsert` now edits or adds a type-1 record in an explicitly
named LIPSYNC sub-bank. Sound IDs repeat across MAIN and SCRIPT banks, so the
API never searches globally. New records derive the name prefix from a valid
same-bank donor and clone its metadata; edited records keep their own name and
dependencies. A scratch rebuild of retail English dialogue.big changed one
MAIN entry, added Dialogue_12135, and preserved the other 20,504 records' raw
payloads, Info and relevant metadata. Forge re-read the scratch archive; the
independent FableTLC Python reader round-tripped its 20,215 nonempty entries
byte-exact. This is an in-memory archive edit; the Sounds UI and mod-pack
output flow remain open. The optional third argument to
`fableforge_lipsync_tests.exe` keeps the scratch archive for inspection.

## 2026-09-30 continuation: retail lip sync payload codec

Added `forge/lipsync.hpp` and `lipsync.cpp` for the dialogue.big type-1
dictionary, FPS, weighted frame keys and four-byte Info duration. The decoder
bounds each read and rejects truncated data and trailing payload bytes; the
writer retains the original duration bits. `fableforge_lipsync_tests` passes
fixed bytes and malformed inputs. Against the installed English dialogue.big,
all 20,214 nonempty type-1 entries decode and re-encode byte-exact; the
independent FableTLC Python tool reports the same count and result.
`tools/check_all.py` now runs the retail pass when that bank is available.
Audio bank decoding, the Sounds UI and preview remain to do.

## 2026-09-30 continuation: target-side attachment and EgoCore review

The selected thing now offers native-style attachment modes by target type,
with a viewport picker and incoming-link list. The document checks source and
target types, existing destinations and owner cycles before changing a link;
owner attachment can add or remove CTCOwnedEntity. The GUI and core suite build,
`fableforge_tests` passes, and `tests/ui/editor.txt` and
`tests/ui/attach_picker.txt` pass. The latter clicks the mode control, picks a
named visible barrel twice to attach/detach, and checks exact undo; it is in
`tools/check_all.py`. Native evidence:
ignored `build/attach_modes_native.c` and `build/attach_filter_native.c`.
`tests/ui/attach_entrance.txt` also passes on retail MayorsHouseHallway: the
selected exit rejects a self pick, connects to the visible entrance, serializes
the CTCDRegionExit field and undoes exactly. This exit has no
CTCActionUseScriptedHook block; the separate door does. The synthetic core
fixture checks both fields when they occur on one thing.

EgoCore `300f949` (2026-09-30) adds a lip sync head renderer. Forge now has
the five retail presets, phoneme interpolation, MM resting pose, audio controls,
mesh/animation readers and a textured animated head viewport. Forge uses the
audio clock for animation and scopes sound ID lookup to the selected language
and dialogue bank. The head pose test checks neutral identity and movement on
all five retail skeletons; `tests/ui/dialogue_browser.txt` visits all five and
captures the Bandit Lieutenant head above the timeline. EgoCore's `55bdc10`
particle simulator and renderer were already assessed for Forge's Effects
preview. Frame/key editing now stages multiple lines across banks and exports
a new verified language-local `dialogue.big`; `tools/test_dialogue_edit.py`
checks a two-bank retail scratch export. Lip sync mod-pack integration uses
record recipes: the Dialogue page
adds staged lines to a selected pack, and the composer overlays each bank and
Sound ID in load order. The synthetic two-pack test checks separate banks and
same-line precedence. Audio-bank writing remains open.
`tools/test_dialogue_edit.py` now also runs `forge-tools mods build` on a scratch
retail archive. It checks that a later whole-file `dialogue.big` wins over an
earlier recipe and that reversing the order makes the recipe win. The JSON
build report names skipped recipes when a later whole-file layer wins.
Same-line recipes with different payloads now appear in `mods conflicts --json`
and the Mods panel, with the effective load-order winner and pack badges.
The retail harness checks both winner orders and opens that panel; the lip sync
row directs users to reorder packs rather than offering a winner pick that the
composer cannot apply.
The Dialogue page now resolves the selected Sound ID back through the matching
`data/Defs/*snds.bin` CRC table to its language-local `text.big` entry, then
shows speaker and subtitle above playback. `fableforge_dialoguetext_tests`
joins 20,088 retail English voiced lines and checks Demon Door ScriptDialogue
5080 exactly; `tests/ui/dialogue_browser.txt` opens that line in the GUI.

## 2026-09-30 continuation: document-level parent validation

Document::linkTargetFits and setLink now reject a Father or Mother target when
an attached definition lookup returns the opposite sex. The editor attaches
its live game.bin lookup to the document on map open. Context::defIntField now
works with definitions loaded without textures, so that guard remains
available in defs-only sessions. Missing or undecodable Sex stays permitted
with a UI warning. Unit checks verify a rejected link leaves the exact .tng
unchanged; a native GUI test places male and female Oakvale villagers and
reads sex values 1 and 2. The new creature_sex.txt test is in check_all.py.
The GUI build, fableforge_tests and 168 owned-graph checks pass.

## 2026-09-30 continuation: optional creature links

The Links card now offers Home, Work, Father, Mother and Spouse on every
AICreature even when its .tng block omits those fields. Setting one inserts a
top-level line in FableWin's save order; clearing it removes the optional line.
Spouse links write or clear both creatures in one undo step and reject a
partner already linked elsewhere. Deleting a linked creature removes incoming
optional lines. The parent picker checks the target's decoded CREATURE Sex
(1 male for Father, 2 female for Mother); it warns if the definition cannot be
decoded. Document callers can attach the same definition lookup; known
mismatches are rejected before mutation. Read-only FableWin Save and attachment
decompilations are in ignored build/creature_save_native.c and
build/creature_attach_native.c. The rebuilt GUI, fableforge_tests,
168 owned-graph checks and editor.txt pass; core tests cover insertion order,
reciprocal spouse writes, conflicts, deletion cleanup and exact undo.

## 2026-09-30 continuation: unique names for new tracks

Linking nodes now follows FableWin FinishLinkingTracks at 0x020302d0:
it reads the joined chain's head name and replaces an empty, INVALID,
NULL or TrackTempName placeholder with a fresh TrackTempName number.
Existing explicit names remain. The document scans loaded track names to
advance its counter, and unlinking uses the same collision-checked allocator.
Track names and the selection summary read the last saved ScriptName field,
matching retail nodes that contain both an early and a trailing copy.
Read-only native decompilation is in ignored build/finish_track_native.c.
The core suite and track_preview.txt pass; core checks cover two fresh chains,
a placeholder head linked to an explicitly named chain, and an existing
TrackTempName30 producing TrackTempName31.

## 2026-09-30 continuation: safe copy of track and special things

Selection copy, terrain "Copy things", paste of old fragments/presets, and
preset loading now use the native edit-brush exclusions: Village, Switch,
PhysicalSwitch, Marker, TrackNode, and things with CTCCreatedEntity
(ETCInterfaceType 0x32). Copy reports the number skipped. An all-rejected
paste adds no undo step. Direct TrackNode duplication makes a new standalone
node with zero links, both end flags set, and matching NULL ScriptName fields;
direct Village block duplication is refused. Group duplication rejects a
Village before editing any member. Read-only FableWin
CThingFilter_IsEditBrushCopyable at 0x02957910 and the PDB enum provide the
filter evidence. The rebuilt GUI and both core suites pass:
169 owned-graph checks, fableforge_tests, track_preview.txt and editor.txt.

## 2026-09-30 continuation: owner transform propagation

Deletion now clears unambiguous incoming UID fields on surviving things,
including both saved `EntranceConnectedToUID` copies on a region exit. It also
unlinks a deleted track node and repairs the surviving chain before removing
its block. Duplicate target UIDs and ambiguous repeated link fields are left
untouched; Forge cannot identify one target or field safely in those cases.
`removeWithOwned` uses the same cleanup for each deleted child and reports only
fields cleared on survivors in the Activity log. Core ownership tests now pass
158 checks, including incoming owner/village/home/exit links, duplicate
UIDs, track-node deletion, LINK/TRACK validation and exact undo.
`fableforge_tests`, `owned_delete.txt`, `locked_things.txt`, `track_preview.txt`
and `editor.txt` pass after the change. No game install file was written.

The selection card now shows a cached `+N owned` hint for valid descendants of
all selected roots. The cache refreshes when the map, document revision or
selection changes. `tests/ui/owned_duplicate.txt` checks counts 10, 9 and 0
before and after selecting a child and copying the pair. The visible placement
was inspected in `build/ui/owned_count_hint.png`.

Owner-aware duplication now clears `OwnerUID` on a single copied child. When a
selected parent and child are copied together, the new child points to the new
parent; nested selected chains reconnect in order. Originals and other block
fields are preserved. `fableforge_ownedgraph_tests` passes 131 checks, including
full and partial copied chains with exact undo. `tests/ui/owned_duplicate.txt`
passes on Oakvale West; `editor.txt`, `ground_place_carry.txt`,
`owned_movement.txt` and `owned_delete.txt` remain green. This is a deliberate
safe-copy rule within Forge's verbatim-block duplication. Read-only FableWin
decompilation of `PaintInputCloneNearestThing` at `0x02996750` shows the native
command calls `PaintInputPlaceThingAt` at `0x0184ae68` to create a fresh thing
from the selected definition, then copies appearance seed, light settings,
scale and script name when applicable. It does not perform a verbatim TNG block
copy. The decompilation is in ignored `build/clone_native.c` and
`build/clone_place_native.c`; full native clone parity and other copied UID
links remain separate work.

Owner deletion now asks whether to delete all valid owned descendants or keep
them and clear surviving direct children's `OwnerUID` links; Cancel edits
nothing. Derived children follow native deletion even if locked in place, while
directly selected locked roots remain protected. `removeWithOwned` batches link
edits and block removals into one undo step. The modal is centered and its three
buttons are automation targets. `fableforge_ownedgraph_tests` passes 117 checks,
including nested/locked deletion, detached survivors, mixed-case fields and no
LINK warnings. `tests/ui/owned_delete.txt` passes on read-only Oakvale West for
Cancel, Only selection, Delete all, and byte-exact undo. The modal screenshot
was inspected at `build/ui/owned_delete_modal.png`. `locked_things.txt`,
`selection_actions.txt`, `editor.txt` and `owned_sections.txt` remain green.

Quest-section propagation now follows valid `OwnerUID` descendants. Document
snapshots UIDs before moving blocks so changing indices cannot redirect later
moves; each thing's block text is preserved. The Quest sections card moves every
selected root in one undo step and retains selection. Day/night section changes
use the same Document path. `fableforge_ownedgraph_tests` covers the section path;
`tests/ui/owned_sections.txt` passes on Oakvale West with two selected roots,
ten owned descendants, section assertions and byte-exact undo.
`owner_daynight.txt` and `parity.txt` remain green.

The strict `OwnerUID` graph now supplies parent-before-child rigid frame deltas
for movement and rotation. Selected roots remain independent; unselected owned
descendants follow their direct parent, including nested chains. The editor uses
this path for gizmo and carry previews/commits, numerical transforms, Move,
Rotate, Set height, Drop to ground and surface cycling. Scale does not move
children. Directly selected locks remain protected; a child locked in place can
still follow its owner. The Objects card has a saved **Move owned things with
parent** checkbox, on by default.

`fableforge_ownedgraph_tests` covers nested, locked and rotated
children, scale-only moves and undo. `tests/ui/owned_movement.txt` passes on a
read-only Oakvale West document: parent movement, child delta, exact undo and
the disabled checkbox. `selection_height.txt`, `surface_cycle.txt`,
`ground_place_carry.txt` and `locked_things.txt` pass after the shared-path change.
No game install file was written.

## 2026-09-30 continuation: adjustable FX preview background

Assets > Effects now has a custom Background colour picker and Dark, Grey and
Light presets, responding to the EgoCore Discord suggestion that dim effects
disappear against the fixed dark preview. The chosen RGB clear colour is saved
in Forge settings and only changes the offscreen preview. The former dark blue
grey remains the default. `tests/ui/effect_background.txt` passes with a paused
BrazierFireFinal preview showing 17 sprites; captured background pixels are
`6,9,13`, `89,89,89` and `229,229,229` for the three presets. The established
`effect_browser.txt` regression also passes. No effects.big or install file was
written.

## 2026-09-30 continuation: pointer placement and ground carry

With a definition selected in the Add an object palette, Shift+click in the Edit
viewport places it at the pointed terrain position. Select or Move mode also lets
an unlocked thing be picked up with a plain left drag. The preview preserves its
height above terrain and its offset from the grab point; selected extras follow
with their own ground offsets. Release commits one undo step. Escape restores the
start frames. The existing fixed-height setting applies to the primary, and a
drag that returns to the start creates no document edit. The selected pivot acts
as a pick handle when another mesh covers it.

`tests/ui/ground_place_carry.txt` passes with scripted viewport Shift+click,
single and grouped carry, terrain-offset checks, fixed height, exact undo and
Escape cancellation. It also checks a drag back to its start creates no edit and
a locked thing refuses carry. Ctrl+Shift+drag clones the picked thing or selected
group and carries the copies in one undo step. Ctrl+D clones the selection into a
cursor-follow carry; the next ground click drops it, and Escape removes the copy.
The context menu and Actions button still provide an immediate Duplicate here.
The UI run edits only the in-memory document.
`selection_actions.txt`, `locked_things.txt` and `placement.txt` regressions pass.
Owner transforms now propagate through the shared editor path described above.

## 2026-09-30 continuation: safe close and failed World writes

Window close and File > Exit now request the existing unsaved-changes dialog when
the current document has object/terrain edits or the World tab has pending edits.
Cancel keeps the edits, Discard exits, and Save writes the loose object draft and
starts any queued World write. Closing waits for that asynchronous World result;
terrain edits still require their separate terrain-save action. A failed World
write now retains pending changes and World undo history, and keeps the close
dialog open instead of silently discarding the failed edit.

`tests/ui/quit_prompt.txt` passes on an in-memory moved barrel. The established
`tests/ui/editor.txt` map-switch prompt regression passes. The focused
`tests/ui/world_write_failure.txt` run uses a nonexistent save root: both the
ordinary write and a Save from the close prompt fail while retaining the queued
move and undo; that root remains nonexistent. No game-install write, commit or
push was made in this continuation. Owner-child transform propagation remains
open: serialized OwnerUID edges alone do not prove the native live owner list.

## 2026-09-30 continuation: meshless things

The Edit viewport now draws small coloured glyphs for placed things with no
uploaded mesh, including markers and region exits. The same visible glyph set
drives ordinary selection, Ctrl selection, link targets and right-click context
selection. The Markers chip controls glyph visibility and picking; quest section
filters and scene reloads also govern both. The Objects list includes markers and
has an All things / Objects / Markers filter. Focus frames a meshless thing's saved
position. Track nodes keep their separate controls. Projection rejects points
behind the camera, outside the viewport or outside the depth range.

Region-exit links now appear when the source has `CTCDRegionExit`, even without a
scripted-hook component. If both components exist, setting the entrance UID updates
both in one undo step; a single row uses the dedicated component's value. The
core link test covers mismatched starting values, paired writes, undo and the
dedicated-only case. The UI test reaches that link picker, rejects the source
marker as its own target, then links it to a visible region entrance.

The focused `tests/ui/thing_glyphs.txt` run on retail MayorsHouseHallway passes
toggle/section hiding, viewport and Ctrl picking, context selection, a valid
exit-to-entrance link with its target UID checked, and byte-exact undo.
Screenshot: `build/ui/thing_glyphs.png`. The glyphs are editor
overlays with explicit hit priority where they cover scene geometry; they do not
model native occlusion or replace authored editor meshes. Switching maps and back
passes. The vanilla parent-anchored attachment workflow remains separate.
This continuation made no game-install writes, commits or pushes; existing
uncommitted work on `feat/editor-ui-shell` was preserved.

Checkout: `D:/Code/FableForge`, branch `feat/editor-ui-shell`, continued from
Desktop `forge.txt` and the pending edits after `f7d7be4`.

## Bedtime handoff ? paused at user request, 2026-09-29

User: "Bedtime. Update docs etc so I can go to bed." Work is stopped after this
handoff. Earlier marathon/continuous-work instructions do not authorize automatic
overnight continuation. All research agents have completed; no build or UI test
is left running. Resume when the user asks.

The current executable is `build/FableForge.exe`. Latest GUI build:
`build/build-light-volumes-final.log`. The workspace remains uncommitted on
`feat/editor-ui-shell`; preserve the accumulated changes. No commits, pushes or
game-install writes were performed. Hidden UI tests were sequential and builds
used BelowNormal/-j2 while the user gamed.

Completed and validated in this continuation: Ctrl+H absolute selection height,
H surface cycling, locked-object handling, scalar component overrides, container
and creature-family lists, creature InitialPos synchronization, Show in palette,
a conservative owned-descendant query, and selected-effect sprite/mesh playback
with authored bounds/fixed-axis rotation plus animated component-light volumes.
See the detailed sections below for native evidence, limits, screenshots and logs.

Latest checks: 27 height checks,68 owner-graph checks,41 light checks; relevant
native height/surface/lock/palette/effect scripts pass. Light volume toggle changes
1319 pixels; animation changes4060; paused/restarted images match exactly. Sprite,
pool and rotating-mesh pixel regressions also pass. Whitespace check is clean.

Next proposed work: non-mesh thing glyphs and picking. Research only is complete;
no marker feature or partial declarations were left in source. The existing object
list still excludes Marker and ordinary/context picking still requires a mesh.
Implementation should share one visible glyph set across drawing, hover, ordinary
selection, link targets and context selection. Cache instance ownership from actual
renderer instances at bind/upload, invalidate on clear/reload, and preserve track
nodes' separate interaction. Explicit editor overlays can take priority within the
visible glyph; this is a UX policy, not native occlusion parity. Renderer::project
only rejects nonpositive W, so also reject behind-camera/off-viewport/depth-invalid
points. Respect sections, scene-stale state, toggles and linkTargetFits. Focus must
fall back to a valid frame for things without instance bounds. Start with retail
MayorsHouseHallway / REGION_EXIT_POINT; test toggle/section hiding, Ctrl-select,
context actions, links, document switching and undo. Native IsDrawable0x02037620
and IsSelectable0x01f964e0 test independent reason counters at+0x10/+0x14.

Other remaining work: owned-child deletion/section propagation, random/game-driven
particle orientation, scene lighting and in-world particle playback. Performance
work stays paused per user request. ArenaHallOfHeroes remains accepted based on
the documented retail-content audit; do not apply speculative asset fixes.

## Component-light volumes: validated

All serialized light fields are retained without changing legacy export colour or
radius. Separate bounded timers do not require emitters, alter particle counts or
consume their RNG. Native-confirmed life/respawn/timeline, linear colour/radius,
cosine fades and max-axis scale now animate wire volumes over the effect image.
The toggle preserves the camera; Frame effect includes volumes when enabled.
Projection rejects nonfinite/near-plane points and clips segments to the image.
Actual scene illumination, external position parameters and scene alpha remain
unsupported and are stated in the preview. Expanded light inspector is read-only.

41 light checks plus existing particle checks pass. Retail scan:1165 complete,
zero partial,150lights. BURNING_HANDS_POWER_UP native UI passes ticking, volume
hide/show, expiry and switching to light-free BLOOD_POOL. Pixel proof:1319 overlay
pixels,4060 changed across tick15/30, pause/reset exact. Sprite, pool and spinning
mesh native/pixel regressions all pass. Independent core review found no blockers.
Logs: `build/build-light-volumes-final.log`, `build/build-light-tests-final.log`,
`build/effect-lights-native-first.log`, `build/effect-light-pixels.log`,
`build/effect-light-regressions.log`. Screenshot inspected/copied:
`walkthrough/w22_light_volumes.png`. Full evidence: EGOCORE_PARTICLES_20260929.

Next proposed feature is non-mesh thing visibility/picking; see the bedtime handoff.

## Palette reveal and conservative owner graph: validated

Actions > Show in palette chooses the selected definition, opens the group and
scrolls both palette and settings panel. Creatures route to Actors; switching
from Terrain uses setEditTab to restore the normal transform tool. This is
read-only. A first test caught the palette remaining offscreen; corrected parent
scrolling is visually inspected and native object/creature checks pass.

Document::ownedDescendants now supplies the surface-cycle exclusion query.
It traverses a stable BFS with cycle guards and excludes supplied roots. Both
UID endpoints and the child OwnerUID declaration must be unambiguous; malformed,
zero, duplicate or overflowing identifiers cannot silently resolve to a target.
This queries serialized links only; it does not move children or claim native
runtime ownership equivalence. 68 core checks pass; surface-cycle regression
passes. Logs: `build/build-light-volumes-palette.log`,
`build/palette-ownergraph-native-final.log`. Screenshot:
`walkthrough/w21_show_in_palette.png`.

## Fixed selection height: validated

Ctrl+H / Set selection height now opens an absolute-height modal. Each unlocked
selected object clamps to its local terrain floor, retaining XY/orientation/scale;
existing creature InitialPos follows the move. One batch gives exact undo. No-op
height retains history/redo. Map+UID selection snapshot disables Apply if changed.
Opening refuses gizmo/terrain strokes; any popup blocks starting a background gizmo.
Native SetThingZ evidence and limitations are in PARITY_BACKLOG. Owned children
now follow through the shared transform path documented at the top of this file.

27 core checks pass. Hidden native selection_height covers actual Ctrl+H/menu,
text entry, terrain clamp, cancel/no-op, changed selection, locked mixtures,
InitialPos and background-gizmo blocking. Surface-cycle and locking regressions
pass. Logs: `build/build-selection-height-modal.log`,
`build/selection-height-native-first.log`, `build/selection-height-native-final.log`.
Inspected screenshot: `walkthrough/w20_selection_height.png`. No install writes.

Component-light timing and the owned-descendant query were subsequently completed;
see their validated sections above.

Native owner research (same FableWin identity): OnEditorMove0x025739b0 translates
children, then rotates about owner's new position using a world-Z angle delta.
CMoveThing0x02575190 recursively propagates translation; CRotateThing0x025752c0
rotates position and complete basis, then recursively propagates derived deltas.
These test alive state, not child editor locks. Native child-lock semantics differ
from Forge's explicit selection protections. Movement now follows the shared
transform path documented at the top of this file.

2026-09-30 read-only Ghidra check of the FableTLC retail runtime closes the
activation gap for valid links. `CTCOwnedEntity::OnSerialise` at `0x007e8460`
transfers `OwnerUID` into its 64-bit field at `+0x18`. `InitialActivate` at
`0x007e8820` resolves a nonzero UID and calls `SetPOwner` at `0x007e8590`.
`SetPOwner` adds `CTCThingOwner` to the parent when absent and inserts the child
into its live object list. `CTCThingOwner::OnKill` at `0x0071b200` visits that
list. Thus a valid, unambiguous serialized OwnerUID is evidence for a runtime
parent-child relationship; this is an inference across the retail runtime and
the FableWin editor movement code. Forge's `ownedDescendants` already excludes
zero, malformed, duplicate and unresolved UID edges. Forge now uses those edges
for owner transforms, treating child locks according to native owner movement
while protecting direct selections. Decompiled evidence
is in ignored `build/owner_native.c` and `build/owner_serialise_native.c`.

## Surface cycling: validated

Added H / Cycle surfaces below in selection controls and context menu, leaving
End as direct terrain drop. Queries visible placed-object triangles with selected
things, generated mesh children, explicit OwnerUID descendants and visual effects
excluded. Locks, stale geometry and active gizmos are guarded; all candidate heights
are computed before one frame-edit batch. Existing InitialPos updates are retained.
Ground-only wrap uses native-evidenced +150; otherwise searches from currentZ-0.1.
This is central/double-sided visual-ray support, without the native radius/physics
sweep. Owned children now follow the selected parent after surface placement.
Limitations and corrected prior claims are in PARITY_BACKLOG.

No-window WARP-backed CPU picking checks pass, including transformed supports,
selected/child/proxy/hidden filtering, ray-inside-AABB-but-outside-triangle, updated
transforms and misses. Native surface_cycle passes actual H/menu actions: barrel
at(103.993,94.307) reaches roof Z16.328, then terrain Z8.395; second step preserves
XY/orientation/scale. Locked mixtures, creature InitialPos and full byte-exact undo
pass. Lock and selection-action regressions also pass. Logs:
`build/build-surface-cycle-final.log`, `build/build-surface-picking-final.log`,
`build/surface-cycle-native-final.log`. Screenshot inspected/copied to
`walkthrough/w19_surface_cycle.png`. No install writes.

Fixed-height follow-up is complete; see the latest section above.

## Fixed-axis mesh orientation follow-up: validated

Native-confirmed XYZ initialization and fixed-axis quaternion updates are now
implemented. Authored units are turns; initialize Qx*Qy*Qz and multiply the
per-update delta on the right. Zero axis keeps identity delta. Bounded math and
normalization avoid numerical drift; existing sprite RNG/behavior is preserved.
Random initial/axis and direction/game orientation remain explicit warnings.
Evidence, binary hash and native addresses are in EGOCORE_PARTICLES_20260929.md.

70 CPU checks pass. Native DAZED01STAR retains one long-lived mesh, verifies its
quaternion changes, submits128 triangles, and passes screenshot comparison:
280visible/485changed pixels, exact paused/reset frames. BLOOD_POOL/authored bounds
and original sprite playback regressions also pass unchanged. Logs:
`build/build-mesh-orientation.log`, `build/mesh-orientation-native.log`,
`build/mesh-orientation-pixels.log`, `build/mesh-orientation-pool-pixels.log`,
`build/mesh-orientation-sprite-pixels.log`. No install writes.

Next investigation: terrain-only Drop to ground behavior on indoor/tabletop
objects. Researching native support casts and existing renderer ray APIs before
changing it. User still wants continued work until told to stop.

## Authored mesh bounds follow-up

Geometry now retains a validated optional bounding sphere from the mesh payload,
with descriptor fallback. Layout is independently confirmed by EgoCore, Forge's
mesh writer and FableTLC MESH.md. Renderer prefers it to geometry bounds; fallback
is now reported only for unavailable/unusable metadata. Tiny/extreme spheres and
nonfinite composed transforms are guarded. Radius normalization and centring no
longer always use the geometry estimate.

21 meshbounds checks pass including real MESH_DUST_CIRCLE_01 (435): authored
centre Z2.42376447 versus derived1.71620929. WARP bounds/overflow checks pass.
Main native BLOOD_POOL test confirms one authored-bounds mesh; same1215visible /
158changed pixels, exact pause/reset. Logs `build/build-authored-mesh-bounds.log`,
`build/authored-bounds-native.log`, `build/authored-bounds-pixels.log`.

Next in progress: native-confirmed fixed mesh orientation. Evidence in
`build/native-particle-orientation.txt` contradicts EgoCore's radians/Z-only
initialization: native uses XYZ turns, Hamilton Qx*Qy*Qz, raw per-update delta on
the right, zero-axis identity. CPU implementation/tests being validated; root
is wiring a native rotating-fixture test. Keep existing sprite RNG/behavior.

## 2026-09-29 mesh particles: implemented and validated

Selected Effects playback now composites bounded mesh particles and sprites.
Parser preserves final per-system config/index and static export fields; CPU
shares populations between mixed renderers and supports mesh size/tint/fades.
Renderer caches immutable meshes, applies reference basis/radius normalization,
centring and materials, and sorts translucent meshes/sprites together. Added
bounded resources, first-frame atlas inset and counters. Root loads selected
resources and frames ground-facing meshes from an elevated angle.

61 CPU checks and exporter suite pass. All 1165 retail effects still decode with
unchanged sprite/mesh/light counts. Persistent `fableforge_particlepreviewrenderer_tests`
uses WARP feature level10 and passes transformation/blend/atlas/composition/cap
checks. Native mesh-only BLOOD_POOL passes pause/restart/count checks and pixel
verification (1215 visible,158 changed pixels). Sprite playback, orbit/attractor
and browser navigation all pass unchanged; sprite replay remains pixel-identical
on pause/reset,5488 changed pixels over animation. See EGOCORE_PARTICLES_20260929
for log paths and limitations. No install writes.

Remaining mesh approximations are explicit: fixed orientation, computed geometry
bounds rather than authored sphere, no skeletal animation/trails/flicker/external
size parameters. Authored bounds and fixed-axis rotation were subsequently completed;
see their validated sections above. Performance work stays paused.

## 2026-09-29 next completed tranche: omitted component overrides

Added knownComponentProperties/setComponentOverride/resetComponentOverride,
leaving the explicit-only propertiesOf contract unchanged. Seven verified
component classes admit unique scalar fields only. Serialized bool/int/float
validation rejects malformed/nonfinite/out-of-range values; radii documented as
native floats remain fractional despite integer-widget metadata. No general-field
synthesis, new components, coupled light/script controls, or invented defaults.

Properties now offers Set value for omitted fields and Reset on admitted saved
fields. Real bool selection, typed numeric values, range rejection, removal and
byte-exact undo all pass `tests/ui/component_overrides.txt`, including an omitted
retail field in MayorsHouseHallway. 43 core checks pass. Native log:
`build/component-overrides-native-first.log`; locking/list/selection regressions:
`build/component-overrides-native-regressions.log` (all three pass). Screenshot
inspected and copied to `walkthrough/w17_component_overrides.png`. Read-only review
found no blocking defects. Initial compile diagnostic from missing c_str calls
is retained in `build/build-component-overrides.log`; corrected build is
`build/build-component-overrides-second.log`.

Continuing with mesh particles in the selected Effects preview. Parser/CPU and
D3D renderer work delegated separately; root owns resource loading, framing and
UI regression. Pinned EgoCore 55bdc10 is the reference, with fixed orientation and
explicit warnings for unsupported behavior in the initial tranche. No native
parity claim without native evidence; static export fields must stay unchanged.

## 2026-09-29 continued work while the user games: object locks

Implemented Locked in place throughout the document and selection controls:
Ctrl+L, Properties checkbox, Actions menu and list marker. Existing CTCEditor
blocks only; missing/empty flags default unlocked. Core guards protect transform,
scale, physics-property edits, delete and terrain reseat. Content/owner edits and
copying remain allowed. Group operations skip locked objects and preserve their
selection after mixed deletion. Reseating unlocked creatures also syncs existing
InitialPos. Locking mid-gizmo cancels its internal capture and preview; explicit
native input regression verifies IsUsing clears and the document stays exact.

Main-build lock/creature/container core tests pass. Native lock check including
mid-drag cancellation: `build/locked-things-native-drag.log`; selection actions:
`build/locked-editor-regressions.log`; creature/list regressions:
`build/locked-editor-regressions-extra.log`. Lock screenshot inspected at
`build/ui/locked_properties.png`. Initial fixture typo (nonexistent barrel
cupboard definition) is retained in `build/locked-things-native-first.log`;
correct fixture is OBJECT_CUPBOARD_TALL. No assertions relaxed.

Native evidence and intentional group/scale-policy differences are recorded in
PARITY_BACKLOG. Builds use BelowNormal priority and two workers; native tests
are hidden and sequential. No game-install writes or visible launches. The user
said to continue until told to stop. Next active task: explicit overrides for
known scalar fields absent from existing components; never invent defaults.

## 2026-09-29 final continuation: creature saved initial positions

Fixed the next high-value backlog gap after contents/particle work: moving
creatures left InitialPos at their former world position. Shared frame writing
now syncs each existing axis with local XY plus map origin when position changes;
paste always rebases to the destination origin. Direct navigator position-property
edits share the same rule/undo step. Rotation/scale preserves independent values,
missing axes remain absent, and openText resets a previous document's world origin.

Read-only native disassembly confirmed drag's type1/2 guard and SetInitialPos
call; corrected the backlog's earlier unsupported claim about the drop path.
Evidence and exact binary SHA: `build/creature-initialpos-native.txt`, also
recorded in `VANILLA_EDITOR_INVENTORY.md`. Core regression, container-list and
25-check creature-frame tests pass in the normal build. Native movement, rotate,
scale, ground, duplicate-then-move and paste test passes with every edit undone:
`build/creature-initial-position-native.log`. Build log:
`build/build-creature-initial-position.log`. No install files changed.

The prior continuation's Contents/family picker and particle-force work remain
validated; effects browser additionally passed
`build/continuation-effect-browser-regression.log`. Next substantive gaps remain
missing/default component fields, mesh/light particle rendering, in-world animated
attachment, and the broader editor backlog. Performance remains paused by request.

## 2026-09-29 further continuation: contents lists and particle forces

User again requested continued marathon work. Implemented container/family list
editing for existing CTC blocks, including empty ones, through a grouped searchable
definition picker in both property surfaces. Add/replace/remove each use existing
undo; removing an entry keeps contiguous indices. Malformed/duplicate lists keep
raw fields and refuse structured rewriting. No absent-component creation or
native template-flag filtering yet. Separate CPU fixture tests byte-identical
undo, unrelated fields and malformed rejection.

Native `container_lists.txt` passes actual typed searches, adding/replacing/removing
retail apple items, empty/final-entry family editing, and full undo to zero changes.
First run exposed automation overwriting a focused text field; replaced it with
real typing events. Second run established the fixture's assumed OBJECT_APPLE
symbol was not present: available entries are OBJECT_APPLE_GREEN_01/RED_01 and
OBJECT_APPLEPIE. Corrected the fixture without relaxing assertions. First evidence
remains in `build/container-lists-native-first.log` and `-second.log`; final pass:
`build/container-lists-native-retail.log`. Screenshots inspected and saved as
`docs/walkthrough/w15_container_contents.png` and `w16_creature_families.png`.

Particle support now retains/simulates orbit and attraction with explicit upstream
approximation warnings. Corrected ordinary/single size fading, max-axis preview
scale (static export sizes preserved), single rotation and alpha-testing. Texture
aspect influences framing; selecting the same paused effect preserves playback.
50 CPU preview checks, core/parser tests and new container-list tests pass.
Native forces, playback and selection-actions scripts pass in
`build/continuation-native-regressions.log`; pause/reset are pixel-identical.
Source/scoping details: [EgoCore follow-up](EGOCORE_PARTICLES_20260929.md).
Main build: `build/build-editor-lists-final.log`, final input change:
`build/build-list-input.log`. No install writes, commits or pushes. Performance
work remains paused; Arena layout remains unchanged.

## 2026-09-29 evening continuation: selected-effect sprite playback

User accepted ArenaHallOfHeroes if its available retail assets match. The content
and placement audit found matching retail backups; no speculative geometry or
orientation change was made. Performance remains paused.

Implemented isolated animated sprite preview in Assets > Effects, using EgoCore
55bdc10 as a behavior reference. Includes deterministic 30 Hz CPU simulation,
Play/Pause, Restart, Step, automatic framing from sampled motion, orbit/zoom,
sprite frame sheets/raw arrays, and explicit unsupported-component/texture
messages. GPU resources belong to a separate preview renderer. No installed data
writes, full in-world particle simulation, mesh/light preview or effect editing.
See [the source review and scope](EGOCORE_PARTICLES_20260929.md).

Validation: main GUI and CPU targets build; core, 25 preview and frame-decoder
checks pass. Native playback and browser regression scripts pass, with actual
button clicks. Pixel check proves pause/reset stability and visible progression
(1,055 changed pixels between ticks30/60). Retail bank decoding retains all1,165
entries; GPU WARP featurelevel10 tests include partially transparent additive
blending and animation frame selection. Final logs: `build/build-particle-preview-verified.log`,
`build/effect-playback-final-native.log`; run `python tools/test_effect_playback_pixels.py`
after the native playback script. Remaining work is in-world attachment, mesh/light
simulation, unsupported components, interpolation and effect editing/writing.


## 2026-09-29 continuation: resume editor features, audit ArenaHallOfHeroes

The user paused performance work and asked to continue the unfinished editor
features. They reported ArenaHallOfHeroes looking incomplete, without identifying
a particular missing object. Do not claim every asset type in every level is
rendered: runtime spawning, animation, particle simulation and game lighting are
not reproduced by the world viewer.

Implemented contextual object actions: a stationary RMB click picks the object
underneath, preserving a multiple selection when the hit is already a member.
The menu provides Focus, Properties, Duplicate, Drop to ground and Delete through
existing operations/undo. RMB drag, flight keys and wheel suppress the menu.
A compact viewport toolbar opens the same actions and a floating Properties
window. The window follows selection, reuses the existing property grid, shows
an empty state after deselection/deletion, and closes when leaving the editor.

Implemented Assets > Effects, searchable by name/display name/id, with active
sprite, mesh and light details, sprite thumbnails, texture/model links and
explicit partial-decode status. This is a read-only inspector, not the EgoCore
animated particle simulator. The user asked specifically about that integration:
the upstream simulator/renderer were reviewed and Enabled-flag correctness was
adopted earlier; full playback, particle editing and writing remain unfinished.

Effects banks now resolve by install root. Parsed effects use immutable shared
ownership, so replacing/refreshing a bank does not invalidate active readers.
Refresh can force a reload even when size/timestamp match. Old bank snapshots
are released instead of accumulating on every refresh. Install changes clear
browser/texture selection state. CPU tests cover two roots sharing an effect
name, concurrent reads, forced reload and last-reader release.

Validation: normal GUI/core build passed; `fableforge_tests` passed in 0.54 s.
`tests/ui/effect_browser.txt` passed searches, sprite texture/model navigation,
light inspection and refresh; screenshots were inspected. Selection's first
fixture hit a tavern roof in front of its barrel and clicked an offscreen close
button; evidence is retained in `build/selection-actions-first-native.log`.
The corrected fixture uses a close overhead camera and reveals close buttons;
the actual picking and action assertions remain. `selection_actions.txt`,
`controls.txt` and `model_browser.txt` all pass (`build/selection-actions-second.log`,
`build/selection-controls-assets-regression.log`). The selected-barrel inspector
was visually checked. No install writes, commits or pushes.

Arena audit: all six hall mesh sections resolve, as do all five attached objects;
the default world-equivalent export reports no unknown definitions or missing
meshes. Seven particle emitters are excluded by the world's current particle
option. The hall's omitted sentinel triangles are zero-area degenerates, so
restoring them cannot fill missing structure. Level and World captures reproduce
the separated-looking hall/props even with culling and LOD disabled
(`build/arena-content-{editor,world-full}.png`). Hall TNG and mesh payloads match
installed backup copies. Native basis/load/upload evidence has not justified a
transform correction; the visual discrepancy remains unresolved. Keep this
separate from the passing asset-browser tests and do not flip global orientation
without stronger evidence.

Detailed counts, hashes and native addresses are in
[the Arena Hall content audit](ARENA_HALL_CONTENT_AUDIT.md).

## 2026-09-29 continuation: scenery beyond the near-map budget

The user reports little visible change at maximum Detail distance, including at
camera heights of 250-500 metres without entering another map. The prior object
LOD implementation worked inside loaded maps, but six initial near-detail slots
still prevented other visible levels from acquiring object meshes. Increasing
radius could retain the same maps; automatic expansion required sustained settled
frame samples. Thus working LOD selection did not provide the requested coverage.

**Implemented and tested:** Draw distance explicitly requests
100-1000 metres independently of the adaptive near-detail map count. A separate
scenery service prepares the last usable authored mesh level for visible/padded-
cone maps beyond those slots. It reuses foliage/placed-mesh loaders, materials,
textures and cutouts; assets lacking a chain retain base geometry. Distant objects
do not require full terrain refinement or a camera boundary crossing. Static
effects remain static; this does not add particle simulation.

Demand uses minimum three-dimensional distance to expanded map bounds, including
camera height, and preloads to `drawDistance * 1.15 + 32`. Resident range hysteresis
adds 32 metres. Near-detail residency preference is capped at 45 metres instead
of growing with the slider. Individual objects still undergo renderer visibility
tests; terrain-derived scheduling bounds remain an approximation for overhangs.

Scenery is bounded to 64 maps and at most 1 GiB of accounted GPU resources,
further limited by one quarter of adapter budget and measured headroom after
reserving the larger of 256 MiB or one fifth of adapter budget. Unknown
telemetry permits 64 MiB. These are not total process VRAM guarantees. Preparation,
paced upload, invalidation/cancellation and payload retirement have separate
lifecycle handling. Coarse scenery and full-detail meshes use complementary
dither intervals during handoff. Coarse-only preparation retains common full-chain
bounds so the two paths agree. Terrain refinement remains separately budgeted.

The intended fixed-camera regression raises distance from 250 to 1000 metres at
350 metres altitude and requires farther scenery without entering another chunk.
The first actual-slider GUI run (`build/world-distance-first`), with one full-detail
slot, increased scenery residency from 10 to 37 maps and distant drawn parts from
0 to 827, including 445 lower-LOD parts; 11,680 pixels changed, and restoring the
slider restored identical pixels. Its checker failed because it incorrectly
required zero pending demand and exact resident-name restoration: nine maps were
budget-blocked and hysteresis retained three additional maps on return. Evidence
is retained rather than reported as a passing run. The final comparison now
checks memory bounds, pending-versus-blocked demand and exact restored pixels
under the revised headroom-based cap.

Final actual-slider comparison **PASS**, `build/world-distance-final`: distance
250 -> 1000 -> 250 with unchanged camera and the same single near map,
GuildExterior. Scenery increases **10 -> 46 maps**; the high setting draws 4,709
scenery parts and 2,897 lower-LOD parts, accounting for 723,205,836 bytes below
the 1 GiB allowance. Pending, blocked and failures are zero. **27,613 pixels
change** at maximum distance; returning to 250 restores pixel-identical imagery.
Thirteen maps remain resident on return because of hysteresis; identical resident
sets are deliberately not required. This proves visible control response at the
fixed 350-metre camera height, not every viewpoint or hardware configuration.

Lifecycle comparison **PASS**, `build/world-scenery-lifecycle-final`: memory
pressure releases all residents, including empty payload residents; recovery,
disable/re-enable, in-flight invalidation and filter restoration give identical
settled pixels. With creatures off the fixture has 46 scenery maps, 603,647,848
accounted bytes and 4,484 drawn parts. Residency comparison permits an optional
fully hidden coarse copy of the near map. These are resource/lifecycle checks,
not frame-time measurements.

Native handoff route **PASS**: 129 watched frames, no aggregate CPU coverage gap,
30 fallback holds. When leaving a full-detail map before its coarse replacement
is prepared, the detailed representation is retained until fallback coverage is
available. `world_scenery_holds` counts those held updates. The watcher evaluates
map coverage state, not actual GPU pixels or arbitrary mesh silhouettes; it does
not prove all visible transitions are seamless. Extended-range validation passes
with a synthetic 16 GiB budget (telemetry override, not allocated memory).

All six CTest suites, the normal near-LOD check and label/world-view/underfoot/
memory-pressure/water UI checks pass. Final cancellation regression also passes
(`build/world-scenery-cancel-final.log` and matching artifact directory): five
deterministic cancellations cover demand/options changes, disabling detail,
leaving 3D and leaving World; held-worker shutdown exits cleanly. Zero failures,
pixel-identical restored views and identical 177,118,448-byte texture payload.
Far scenery uses the coarsest usable authored LOD; near refinement remains
budgeted. The 64-map/GPU limits, approximate scheduling bounds and serial scenery
preparation remain, so unlimited or instantaneous coverage is not promised.
No commit, push, install write or FPS conclusion belongs to this pass. The earlier
85%-of-load-radius policy below is superseded by the explicit draw-distance request.

Final review found two additional issues. MeshCache raw pointers could outlive a
bank invalidation while exporters were loading. Meshes now have immutable shared
ownership; a deterministic concurrent cache-close/reopen CPU regression passes.
The normal build passes all six CTest suites in 13.74 seconds after that fix.

Under constrained memory, lower-priority demanded scenery could block a more
important map. The first priority-admission GUI run
(`build/world-scenery-priority-first`) exposed repeated eviction/reload instead
of settling: 448 priority evictions by 136 seconds, from a baseline of zero,
with waits at 75/136 seconds. The owned hidden process was stopped after retaining
evidence; this was a failed run, not a passing timeout workaround.

Admission now preflights whether the exact victim group can reclaim enough bytes,
including shared textures, and permits a ready higher-priority map to displace
less useful residents only when feasible. The updated CPU policy suite passes
232 checks. The rebuilt normal GUI passes the native priority regression in
33.68 seconds (`build/world-scenery-priority-final/report.json`). Before movement:
34 maps, 265,581,812 bytes, zero priority evictions and 12 blocked requests.
After movement: 39 maps, 267,818,616 bytes, two priority evictions and 12 blocked
requests, below the 268,435,456-byte limit. Another 240 stationary frames preserve
the eviction counter, names, bytes, drawn parts and exact pixels. No failures.
This exercises useful replacement without stationary churn under the test budget.
Final normal-build distance-control retest **PASS** in 26.20 seconds
(`build/world-distance-admission-final`): the same 46 scenery maps, 4,709 drawn
parts, 723,205,836 bytes and 27,613 changed pixels, with pixel-identical restoration.
The elapsed test duration is not an FPS benchmark. Final handoff **PASS**:
129 watched frames and **29 holds** in this latest run (the earlier run had 30),
with no aggregate CPU coverage gap. Evidence:
`build/world-scenery-handoff-admission-final.log` and
`tests/ui/world_scenery_handoff.txt.log`. Final cancellation **PASS** in
`build/world-scenery-cancel-admission-final`: five cancellations, identical restored
pixels and 177,118,448 texture bytes. Earlier lifecycle evidence remains above.

The latest normal interactive executable was launched with the retail install
using `--install` (PID 13540). Window verification confirms `Responding=True`,
title `FableForge` and nonzero handle 23004592. Remaining limits are serial coarse scenery
preparation, 64-map/hardware memory caps and available authored LOD chains. Missing
chains retain base meshes, and far scenery uses the coarsest usable authored level.
No FPS or instantaneous world-population claim follows.

## 2026-09-29 continuation: population, void picking and cross-level LOD

The user reports slow population both on first opening and during flight, and
asks for LOD behavior across every visible mesh and level. Validation uses the
separate `build-review` Release tree without replacing a running user's executable.
No game-install writes.

**Validated before LOD integration:** world hover now intersects actual overview
triangles. The old ray march could enter a tile footprint below its surface and
count that side entry as a hit, naming unrelated maps around the void. The new
helper rejects tile boxes, traverses crossed cells and tests their triangles;
pending placement and nearest published overview selection are respected. This
remains overview-surface picking, not exact detailed-mesh picking. CPU tests cover
diagonals, shortened edge cells, underside/side rays, distance limits and an
independent planar oracle. Native checks cover downward hits, sky/underworld
misses and an empty actual cursor hover over the void.

Overview thumbnail/geometry uploads retain their 2 ms soft budgets but raise the
count guard from 4 to 32. Geometry publication prioritizes nearby map boxes; the
HookCoast fixture publishes HookCoast first. `build/world-population-final`, at
33 ms pacing, compares guards 4/32: thumbnail upload frames **100 -> 15**, geometry
upload frames **115 -> 92**, identical final pixels and 21,669,456 GPU texture
payload bytes. Both sides use the new ordering. These are upload-frame counts,
not FPS gains or an old-executable timing comparison. The pre-LOD build passes
all four CTest suites and world-view, underfoot, memory-pressure and water routes
(`build/world-hover-regression-final.log`). The world-view fixture now specifies
an eye pose; its previous orbit-target pose also failed on the earlier executable.

**Implemented and native-validated:** world objects retain per-instance,
per-material index ranges within material batches. Authored lower mesh variants
are selected by projected size, with complementary screen-space transitions,
per-object frustum rejection and a distance fade. The scene path covers foliage,
buildings, objects, creatures and attachments produced by the existing loaders;
it does not imply animation or particle simulation. Retail research finds multiple
authored levels in 2,447 of 3,294 meshes. Missing/unusable chains retain base
geometry with size/distance visibility. Terrain uses separate 32-cell patches,
with four indexed resolutions and full-resolution patch boundaries. Selection
limits projected vertical error to one pixel; bounds and error include both
overview-morph endpoints. Water retains its existing persistent geometry.

Shared material vertex/index buffers remain. CPU range metadata and an immutable
GPU metadata buffer (two float4 entries, 32 bytes per range) supply bounds and
thresholds. GPU metadata is charged to batch resource bytes. Adjacent selected
ranges merge into draw calls. Counters measure material parts rather than unique
objects; transition pairs can count both levels. Additional authored geometry,
metadata and draw calls can increase costs. No performance gain is claimed.

Cross-map demand scans all map bounds, including pending placements, with a soft
padded view-cone bonus. Underfoot is first, residents get hysteresis, and the near
ring remains useful in every direction. There is no hard cone exclusion. Existing
radius, altitude and adaptive map/memory limits remain. Object draw distance is
85% of loading radius, leaving a preload band without guaranteeing preparation
finishes before arrival. Standalone demand tests pass 176 checks. Whole-map
publication and a serial preparation worker remain; this is not spatial chunk
streaming and cannot guarantee every visible level fits the budget.

The adaptive detail-upload policy requests 4 ms for responsive frame samples
(at least 45 Hz), otherwise 2 ms. Individual allocations remain uninterruptible.
The fixed 2/4 ms six-map comparison (`build/world-lods-review`) used 67 versus
36 upload frames, identical settled images at every sampled pose, and identical
204,125,040 GPU texture bytes. This is upload pacing, not an FPS benchmark.
The near pose draws 3,266 material parts, including 499 lower-LOD parts, in 691
merged object draws. Disabling frustum culling preserves exact viewport pixels;
moving far/close and returning restores exact pixels and texture payload.
At the far pose, two coarser terrain patches reduce submitted terrain triangles
from 38,912 to 36,208; the near pose keeps all visible terrain at full detail.
The expanded six-suite CTest run passes, including malformed LOD descriptors,
object bounds/ranges, terrain topology/error and cross-map demand tests.

The world chunk label now anchors at the viewport's top center in 2D and 3D.
Hover takes precedence over selection, selection survives moving off terrain,
and neither active means no label. Text is fitted/clipped to the viewport.
Native label assertions and screenshots pass (`build/world-label-regression.log`),
including hover precedence and selection fallback. World view, underfoot cache,
memory pressure and water pass (`build/world-lod-ui-regression.log`); the 18-map
range/orbit-focus-invariance route passes (`build/world-lod-range-regression.log`).
The normal `build/FableForge.exe` has also been rebuilt with this implementation.
Its final seven-capture LOD check passes (`build/world-lods-final`), including
terrain LOD disable at the same far pose. Deterministic cancellation/re-entry
and held-worker shutdown also pass (`build/world-lod-cancellation`), restoring
exact viewport pixels and 177,118,448 GPU texture bytes. The rebuilt normal
editor was relaunched visibly for user review after validation.

Remaining limits: missing authored LODs are not synthesized; loading still
publishes complete maps serially, with finite residency. Water has no new mesh
LOD chain. The original overview/full-terrain diagonal mismatch can still cause
a small publication morph difference; new patch edges preserve the original
full-grid boundary. Object LOD switching uses Forge projected-size thresholds,
not an unverified interpretation of the native descriptor's LODErrors floats.

## 2026-09-29 ultra marathon: cancel obsolete detail preparation

Detail preparation previously finished every stage after camera demand or
content settings made its map obsolete. Added per-job shared atomic cancellation,
checked before terrain work, between terrain/foliage/placed-object decoding,
between geometry preparations, and between cutout mip chains. Changing settings,
leaving the view or clearing detail requests cancellation immediately; camera
demand requests it at the existing demand refresh. An individual decoder/mip
build is not interruptible, so this does not eliminate cold-load latency.

Cancelled results never enter GPU upload or failure/retry backoff. Existing
overview/resident handling remains in place. Prepared payloads use the existing
CPU retirement worker; incomplete scene temporaries destruct on the preparing
worker. Keep cache mutation serial. Move detail polling after overview polling
in the main frame so disabled detail, 2D and other tabs also collect and retire
completed obsolete work. Shutdown requests cancellation before member destruction
joins the future. No game-install writes.

`tools/test_world_detail_cancel.py` deterministically holds a worker after terrain
preparation, then exercises camera relocation, filter invalidation, detail off,
2D and tab exit, and shutdown without manually releasing the hold. Final evidence
`build/world-detail-cancel`: five observed cancellations, zero detail failures,
identical settled before/after pixels and GPU texture payload (177118448 bytes
in this fixture). The held initial worker has performed zero cutout mip builds.
Native exit joins the held worker successfully. This is a control-flow regression,
not a loading-time benchmark. All four CTest suites pass; foliage-enabled failure
and recovery still passes (`build/world-recovery-cancel`).

One-map stress flight (`build/world-flight-cancel`, 4x AA): 494 continuous frames,
16 comparison poses; zero sampled suspect pixels, coarse/detail culling differences
or focus-distance differences. One worker observed cancellation during ordinary
flight (no diagnostic hold), with zero detail failures. No transient candidates;
four stationary changes at HookCoast contain 162, 162, 249 and 309 large-change
pixels while the neighbouring filler foliage fades and HookCoast loads. Frame315
was visually inspected. These residual loading changes are not claimed fixed.
Window watcher: 3630 polls, zero visible/foreground samples. Content-filter tests
and exact restored views pass in `build/world-content-cancel`.
Underfoot-priority, inactive-cache return and memory-pressure UI checks all pass
(`build/world-cancel-ui.log`). Python syntax and final whitespace checks pass.

## 2026-09-29 ultra marathon: smooth cutout edges at 2x/4x

World MSAA previously left foliage texture edges as a hard alpha test. Added
alpha-to-coverage with a derivative-width ramp around the authored 0.5 cutoff.
Evaluate texture/alpha derivatives before the spatial transition discard. The
blend state preserves the viewport's cleared alpha of one, preventing a second
coverage multiplication when ImGui displays the resolved image. Water retains
its own blend state. Actual 1x targets or missing coverage blend state use the
hard test. No extra textures/render targets; no game-install writes.

`world_cutout_aa` is an automation comparison switch, default on. The existing AA
tooltip now mentions leaf/grass edges. `tools/test_world_cutout_aa.py` compares
Guild/Oakvale slow pans, silhouettes and held fades at 1x/2x/4x. Final fixture:
`build/world-cutout-aa-fades` PASS. 1x toggle comparisons are pixel-identical;
2x/4x held fades have monotonic per-pixel coverage and identical settled endpoints.
Texture payloads match in each pair. Weighted silhouette coverage changes by
less than 1.3%. At 4x, squared motion decreases 29.2%/40.0% at Guild/Oakvale;
mean large changes (>80 RGB levels) decrease from 46/66.18 to 1.82/4.55 per frame.
At 2x, both squared motion and large changes also decrease.

The original `build/world-cutout-aa` run failed its mean-absolute-motion gate:
Guild's mean rises slightly despite lower energy and far fewer large changes.
Evidence is retained. The final fixture reports that metric too, but gates on
energy and abrupt changes because AA distributes smaller changes across pixels.
These small-pan diagnostics are not FPS or general perceptual-quality scores.
Guild and Oakvale screenshots were inspected. Saved pre-change executable:
`work/cutout_aa_20260929/FableForge-before.exe`. All four CTest suites pass.

Broader validation: `build/world-flight-cutout-aa` passes 494 continuous frames
and 16 comparison poses, with actual 4x at every dumped state. Zero sampled
suspect pixels, culling/focus coverage differences, transient candidates or
stationary changes in this run. Window watcher: 3665 polls, no visible/foreground
samples. This does not establish that every streaming transition is invisible;
the prior 1x flight's tiny candidates remain documented below. AA allocation
fallback, memory pressure and model-browser scripts each pass on the final build.
Saved-executable comparison `build/world-cutout-aa-baseline` also passes: all four
1x world poses remain pixel-identical to the pre-change executable, with equal
GPU texture payloads. Python syntax checks and final whitespace check pass.

## 2026-09-29 ultra marathon: reuse cutout preparation

Fresh profile `build/profiles/ultra-baseline` identified cutout mip preparation as
1.53 s of 3.50 s total serial map-worker time across the route. It was rebuilding
the same foliage textures on neighbouring maps despite GPU texture sharing.

Added `cutoutmips::Cache`, used only by the serial world-detail worker. Its LRU
holds at most 32 MiB of source/derived RGBA payload and 256 entries. Identity uses
name, dimensions and exact source bytes, so same-name replacements rebuild.
Oversized textures are built without cache admission. Immutable shared chains
remain valid for upload/retirement after cache eviction. Cache mutation, eviction
and source-pixel cleanup stay on the worker; UI state receives a completed snapshot.
The pixel budget excludes small container metadata and any chains still owned by
an in-flight payload. This is a CPU cache; GPU texture policy/allocation is unchanged.

Also stop alpha-coverage search once its error is exactly zero. The search only
updates its selected scale for strictly smaller error, so subsequent scans cannot
change the output. Saved pre-shortcut binary/source: `work/ultra_cutout_20260929/`.

Four-pose cached/uncached comparisons at Guild, Start Oakvale, HookCoast and a Guild
return are pixel-identical, with identical GPU texture payloads. Reuse avoids 185
of 429 mip builds; observed cache snapshots remain below 32 MiB. CPU tests cover
replacement invalidation, old-chain lifetime, LRU ordering, eviction, zero budget,
oversized inputs, invalid images and cache bypass. Evidence:
`build/world-cutout-cache` (cache alone), `build/world-cutout-cache-search`
(saved executable versus cache plus search shortcut). All four CTest suites pass.

Matched 24-map profiles before the search shortcut: cache off/on reduces cutout
preparation from 1512.38 to 909.23 ms and total map preparation from 3475.14 to
2887.20 ms. Median cutout preparation falls from 52.77 to 0.743 ms. Captures:
`build/profiles/ultra-cache-{off,on}`. These paced route results are not an FPS
benchmark; first-use decoding and cache misses still cost time.

Final profile with the exact-coverage shortcut (`build/profiles/ultra-cache-search`)
records 905.65 ms cutout preparation and 2804.03 ms map preparation across 24 maps;
no profiler diagnostics. The shortcut's additional timing difference is small and
is not claimed as a separate measured speedup. Editor CPU p99/max: 3.558/4.968 ms;
paced upload p99/max: 3.428/4.253 ms. Frame/GPU timings remain capture-specific,
and first-use map preparation still reaches 554.6 ms on the worker.

Full flight `build/world-flight-ultra-cache`: 494 continuous frames, 16 comparison
poses, zero coarse/detail culling mismatches and **zero focus-distance coverage
changes**. The latter also confirms the earlier direct-direction camera fix across
the full route. The native process passes; 4375 window-watch polls record zero
visible/foreground samples. Transient candidates are 4 and 1 pixels; one stationary
change is 176 pixels at HookCoast. Tiny transition candidates remain, so this is
not a claim that all loading transitions are invisible.

Final ownership/regression checks pass: foliage-enabled failure/recovery in
`build/world-detail-recovery-cache`, exact restored content-filter views in
`build/world-content-cache`, and memory-pressure/extended-range UI checks in
`build/world-cache-memory-ui.log`. The failure fixture compares identical fallback
pixels and texture accounting while derived chains remain cached; retained pixel
data stays within 32 MiB. The flight's 176 stationary-change pixels occupy the
lower-right foliage region (x 775..907, y 593..756), with AA still 1x and no detail
failures. Worst transient frame 305 was visually inspected. No install writes.

## 2026-09-29 continued marathon: recoverable detail failures

World detail previously retried the nearest failed map immediately. A terrain
decode/build failure or failed GPU upload could repeatedly occupy the single
worker, delaying healthy neighbours and repeating logs. Added per-map backoff:
2, 4, 8, 16, then 30 seconds. Successful loads remove their retry state; clearing
world detail (including content changes) resets it. Failed maps keep their overview
visible, while other wanted maps continue loading within the same budget.
Preparation exceptions now reach the UI as a failure result rather than escaping
from the future. Optional foliage/placed-object subloads retain their prior handling.

The panel reports maps deferred to overview and offers **Retry detail now**.
Logs retain the map, reason and next delay. Fault injection is automation-only and
does not alter the installed files: preparation throws within the worker; upload
fails after attempting a batch so cleanup of partial GPU resources is exercised.
`tools/test_world_detail_recovery.py` checks a failed HookCoast map alongside the
healthy Hookcoast_Filler_02 neighbour, bounded retries, automatic recovery, the
retry button, identical overview pixels and texture-resource accounting after
preparation versus upload failure. The initial image comparison included different
error toasts; the fixture now clears them before comparing world pixels.

`wait_world_detail` remains strict: every wanted map must finish, including retries.
The new `wait_world_detail_settled` permits the deliberate overview fallback during
backoff, for recovery tests only. Existing visual tests are not weakened by failure
fallback. No game files changed; tests use synthetic failures against read-only data.

Validation: all four CTest suites pass; recovery passes with strict default waits
in `build/world-detail-recovery-strict` (pixel-identical fallback captures and equal
GPU/CPU texture payload accounting). Exactly one preparation failure and one upload
failure occurred; each recovered with one additional load. Underfoot priority,
cache return and memory pressure checks pass (`build/world-recovery-ui.log`).

## 2026-09-29 continued marathon: content filters and 60 Hz AA recovery

Fixed Objects/Creatures coupling in world detail. The UI already had independent
checkboxes, but the worker only passed `creatures` to `thingsexport::load`; that
loader always included non-creature roots. `Options::objects` defaults true for
existing exporter/editor callers and is now passed explicitly by the world worker.
Creature attachments follow their parent. Terrain and foliage remain independent.

`tools/test_world_content_filters.py` exercises the actual checkboxes, all four
content combinations, rapid changes during worker activity, restored content and
unchanged setters. StartOakValeWest has 601 rendered object roots and 26 creature
roots. The checked counts are respectively 601/26, 0/26, 0/0, 601/0, 601/26,
then 0/26 after rapid changes. Reapplying those same settings does not reload.
Final evidence: `build/world-content-filters-settled/report.json` and matched
screenshots. Restored both/creatures views are pixel-identical, and each visible
group differs from the empty-content view. The first pixel comparison caught
incomplete overview GPU upload in the fixture; it now waits for world tiles after
entering 3D as well as before it. Initial diagnostic captures are retained in
`build/world-content-filters-final` (not passing pixel evidence).
State counts describe the **last uploaded map**, not aggregate active/cache content.

Also fixed Auto AA recovery on 60 Hz displays: normal `Present` is VSync-limited,
so the previous strict faster-than-60 threshold could not recover quality at a
steady 60 Hz. Recovery now requires ten eligible seconds faster than 55 FPS,
matching the existing detail controller's headroom tolerance. The 45 FPS reduction
threshold, memory checks and hardware support limits remain. CPU regression checks
cover 60 Hz recovery, neutral 50 FPS and interruption of the recovery window.
Final four CTest suites pass, plus AA fallback, underfoot priority and memory
pressure UI scripts (`build/content-aa-ui.log`). No installed files changed.

## 2026-09-29 continued marathon: camera precision and boundary detail

Reviewed the previous flight's HookCoast frames 311/312: changed pixels lie on
trees fading in at the lower right, not a terrain hole or MSAA target transition.
Diagnostic overlay: `build/hookcoast-transition-diff.png`.

Fixed a separate camera precision defect: rendering reconstructed direction by
subtracting the float world-space focus point from the eye. Short orbit distances
lost direction precision at world coordinates. `Camera::view` now builds directly
from eye and the stored yaw/pitch direction, matching the direction used by picking.
Navigation/orbit semantics remain the same; model-thumbnail look-at keeps its own
target-based wrapper. CPU checks cover view-matrix independence and centre-ray
agreement. Source/executable baseline: `work/camera_precision_20260929/`.

`tools/test_world_focus_precision.py` compares focus distances 0.5, 5, 200 and 8000,
with water and AA disabled to isolate the camera. Four overview poses plus detailed
Guild/Oakvale poses now have **zero changed viewport pixels** across all distances.
The saved baseline differs in every case (69,152 to 370,879 pixels, exact RGB
comparison; this is a precision test, not a perceptual quality percentage).
Evidence: `build/focus-precision-before/report.json` and
`build/focus-precision-after/report.json`.

Also reproduced a map-budget starvation defect: with one map allowed, moving from
(930,4220) to (927,4220) across the HookCoast boundary retained the filler map's
resident preference and never detailed HookCoast. The map under the camera now
outranks resident neighbours, within the same radius/height/map/memory limits.
Other neighbours retain distance hysteresis and ordering independent of orientation.
`tests/ui/world_detail_underfoot.txt` checks the crossing and cached return trip;
the old executable fails the crossing (`build/world-underfoot-before.log`).

Final validation: all four CTest suites and six hidden UI checks pass (underfoot
priority, detail range, cache return, memory pressure, antialiasing and model
browser). UI log: `build/camera-underfoot-ui.log`. The crossing loads exactly two
maps, and returning records one cache hit with no third load. No install writes.

## 2026-09-29 continued marathon: adaptive MSAA and particle review

World view now offers Auto / Off / 2x / 4x geometric antialiasing. Auto starts
at up to 4x, drops after sustained slow frames and cautiously restores quality
after sustained fast frames. Loading/unfocused frames do not train the controller.
Every mode checks RGBA8/D24S8 support and DXGI budget headroom; unknown or exhausted
telemetry uses 1x. Target byte accounting includes depth, colour and resolve.
Targets are created transactionally with 4x -> 2x -> 1x fallback, preserving the
last complete target if allocation entirely fails. Failed higher-sample requests
are cached until dimensions/request/test-limit change, avoiding retry every frame.
The resolved single-sample texture feeds ImGui; model thumbnails remain independent.
Memory sampling also runs when world full detail is disabled.

CPU policy checks and all four CTest suites pass. Hidden `world_aa.txt` exercises
4x/2x/1x, injected partial allocation failures, stable fallback, pressure, unknown
telemetry and overview-only memory sampling. It requires a 4x-capable test adapter.
AA, memory-pressure and water UI checks all pass (`build/world-aa-check.log`).
Guild screenshots: `build/world-aa-4x.png`, `build/world-aa-off.png`.
Cutout comparison explicitly disables AA to keep its texture-only metric isolated.
Baseline: `work/world_aa_20260929/`. No new FPS or image-quality percentage claim.

Continuous flight `build/world-flight-aa-20260929` passes: 494 frames, 16 sampled
poses, zero coarse/detail culling coverage mismatches, focus-distance missing
coverage max 48 pixels (limit 64), 4219 hidden-window polls with zero visible or
foreground samples. Transient candidates were 5, 3 and 1 pixels. Two stationary
changes (282/172 pixels) remain at frames 312/313 while HookCoast detail is loading;
sample count stays 1x and target rebuild count stays 5 throughout those frames,
so these are not AA target changes. Do not claim that all streaming transitions
are eliminated. This paced screenshot flight is not an adaptive-AA FPS benchmark.

User flagged EgoCore's new particles. Fetched and reviewed upstream `55bdc10`,
dated September 29; see `EGOCORE_PARTICLES_20260929.md`. Fixed ignored Enabled
flags in Forge's static effects decoder; synthetic tests and all 1165 retail
effects pass. Animated particle simulation is still a separate unimplemented feature.

## 2026-09-29 continued marathon: cutout filtering and scaled-mesh lighting

The next user-requested marathon addresses foliage minification and a separate
normal-transform defect. Source/executable baseline: `work/cutout_filtering_20260929/`.

World-detail cutout textures now receive worker-prepared partial mip chains.
Downsampling weights RGB by alpha, includes odd/narrow edges, and leaves the authored
base untouched. Alpha scaling targets the base's measured bilinear coverage at the
renderer threshold (0.5), with wrapping and RGBA8 quantization included. Each level
must stay within three percentage points AND 10% of covered/uncovered area; otherwise
the chain stops at the last acceptable level. Chains also stop at a largest dimension
of four texels or fewer. This avoids erasing sparse shapes or filling the entire leaf card
when a small mip can no longer represent it. Unrepresentable textures retain mip zero.
This supersedes the earlier opaque-only filtering limitation for streamed world detail.

Coverage-scaling reference: [Microsoft DirectXTex documentation](https://github.com/microsoft/DirectXTex/wiki/ScaleMipMapsAlphaForCoverage).
Implementation here is independent, with a conservative chain cutoff and explicit
bilinear/quantized coverage checks. Preparation stays on the detail worker; immutable
GPU upload uses the existing sharing pool, with a separate sampling-policy identity
and exact partial-chain byte accounting. No export/install files are rewritten.

Final build `build/world-cutout-quality-final` compares 12 matched slow-pan captures
at each pose (including the normal-transform correction):

| Pose | Mean motion delta, mip zero -> filtered | Cutout-mask pixels, before -> after | Extra GPU texture bytes |
| --- | --- | --- | --- |
| Guild | 5.01600 -> 3.30430 | 76,564 -> 75,590 | 13,229,280 |
| Oakvale | 4.30082 -> 2.38285 | 122,215 -> 121,170 | 13,770,784 |

The 34-45% reduction is this capture's frame-to-frame pixel-change diagnostic,
not a general quality/FPS claim. Cutout-only rendered coverage changes <2%; the
filtered Guild capture was inspected. The tool uses recorded viewport bounds,
excluding its top overlay, rather than assuming a desktop size. It requires
nonempty masks, matched viewport bounds, lower motion delta, and coverage within
15% of the baseline. Both runs clear detail and wait for zero retired CPU pixels.

Scaled/sheared meshes previously transformed normals with their position matrix.
Both baked world geometry and editable/model-preview shader paths now use an
inverse transpose (up to a common positive scale). Cofactor normalization avoids
overflow for tiny transforms and preserves reflection direction. This corrects
lighting; source topology/LOD and exporter behavior are unchanged. CPU tests verify
surface perpendicularity through the actual world preparation path, shear,
reflection, tiny/degenerate transforms, coverage, sparse-leaf fallback, transparent
RGB rejection, authored-base preservation, and odd/narrow mip edges.

Normal and clang profile builds pass. All four CTest suites pass.
`build/profiles/world-cutout-normals`: 24 prepared maps; cutout preparation worker
median/max 54.508/224.436 ms, cutout upload max 0.865 ms, paced upload p99/max
2.964/4.127 ms, editor update/viewport CPU p99/max 3.975/5.214 ms, GPU viewport
p99/max 0.929/1.019 ms. No profiler diagnostics. Capture paced at 33 ms with no other
owned rendering checks running; do not infer FPS or before/after GPU speed from it.

Five hidden UI checks pass on the final build (`build/cutout-ui-checks.log`): model
browser, extended detail range, inactive cache/cleanup, synthetic memory pressure,
and water. Full route `build/world-flight-cutout-20260929`: 494 continuous frames,
16 sampled poses with zero culling coverage mismatch, one transient one-pixel
candidate, no stationary colour-change candidates above threshold, and focus
coverage max 30 (threshold 64). The watchdog recorded 3,596 polls with no visible or
foreground window; native exit zero. These timing-dependent counts are acceptance
results, not controlled before/after improvements. Python syntax, docs commands
and diff whitespace checks pass. Initial narrower-window comparison remains in
`build/world-cutout-quality`; do not compare its counts against the larger final
window. Each individual A/B run checks its own matching viewport bounds.

Remaining: texture chains that cannot preserve coverage deliberately retain more
aliasing; distant geometric edges still have no multisample antialiasing. Map-level
residency/population transitions, shared object geometry, specific authored low-poly
assets, actual weak/integrated hardware and total RAM cost remain follow-up work.

## 2026-09-29 marathon: shared textures, filtering and object fades

Continued on the user's "marathon" request. Layer textures now share GPU resources
when name, dimensions, sampling policy and exact RGBA pixels match. Weak pool
entries do not retain unused allocations. Overview albedo references used by
terrain morphs share ownership too. Cache and automatic-distance accounting only
subtract textures exclusively owned by the active/inactive set from DXGI usage;
per-map cache charges remain conservative when maps share textures.

Opaque layer textures now have GPU-generated mip chains and the existing 8x
anisotropic sampler. Payload accounting includes all mips, including odd/narrow
sizes. Alpha-tested foliage/fences retain mip zero: ordinary averaging would shrink
or erase their silhouettes. Coverage-preserving cutout filtering remains open.
Exact equality retains one CPU pixel copy per live unique image; this is an
explicit RAM cost, not a claim of lower total system memory consumption.
Retired identity pixels are freed through a bounded CPU cleanup worker: at most
64 MiB queued plus one 64 MiB worker payload. Oversized/busy overflow falls back
to synchronous release rather than growing the queue. GPU buffers/SRVs remain
render-thread-owned; cleanup is polled even when no 3D viewport is drawn.

Objects ease through screen-door coverage over 0.6 s, independently of terrain's
0.25 s morph. A reversible progress value avoids jumps when demand reverses;
terrain stays detailed for the early part of retirement. No extra vertex
attributes, render passes or texture allocations for the longer object fade.

`tools/test_world_texture_quality.py --output build/world-texture-quality` passes:
18 detailed maps, 12 matched slow-pan captures per mode; sharing is pixel-identical
in every capture. Texture payload 465,114,112 -> 238,801,920 bytes without mips;
with opaque mips, 305,168,000 bytes (34.4% below unshared mip-zero baseline).
Live allocations 1,049 -> 687. Unique identity pixels cost 238,801,920 CPU bytes.
All three modes return exactly to their initial overview allocation/byte totals
when detail is disabled. Fixed-crop mean frame-to-frame max-channel change falls
6.74383 -> 5.84126 with filtering; this is a motion diagnostic, not a general image
quality or performance score. Filtered Guild capture was inspected.

`tools/test_world_object_transition.py --output build/world-object-transitions`
passes held 50 ms steps at Guild/Oakvale. Peak pixels changing by >80 channel levels:
561 -> 434 and 413 -> 311. Both initial and settled endpoints are pixel-identical.
This softens arrival/retirement; it does not eliminate object screen-door noise,
map-level streaming boundaries or authored low-poly geometry.

Full hidden route `build/world-flight-marathon-20260929`: 494 continuous frames,
3,637 watchdog polls with no visible/foreground window, zero culling coverage
mismatches at 16 samples, at most one transient missing-pixel candidate and no
stationary colour-change candidate above the diagnostic threshold. Focus coverage
passes (maximum 30 pixels, existing threshold 64). Inspected snowy-region frame
305; authored map borders and object screen-door noise remain. Route timing is
not controlled across builds, so these are acceptance results, not A/B scores.

Retirement investigation: `build/profiles/world-marathon-retirement` isolated
CPU identity deallocation inside the map-retirement cost (up to 5.38 ms/map).
After bounded deferred cleanup, `build/profiles/world-marathon-cleanup` records
map retirement max 1.24 ms, editor update/viewport CPU p99/max 3.612/4.556 ms,
paced upload p99/max 2.946/3.022 ms, GPU viewport p99/max 3.919/4.329 ms.
Both are 33 ms paced captures, not FPS benchmarks; no other owned rendering check
ran during either capture. The final trace covers 24 prepared maps / 304.9 MiB
geometry total (not peak residency), with no profiler diagnostics. Bounded overflow
was exercised: 39 synchronous fallback releases, max 1.048 ms. No claim of lower
GPU time across captures; observed GPU timings varied.

Final normal and clang profile builds pass. All four CTest suites pass, with
CPU coverage for exact sharing, replacement pixels, sampling/dimension mismatches,
failed/invalid uploads, final-owner destruction, mip payload sizes and reversible
fade curves. Five hidden scripts pass (`build/marathon-ui-checks.log`): model_browser,
world_detail_range, world_detail_cache, world_memory_pressure and world_water.
The cache script now waits for CPU texture cleanup after leaving World view.
The final-build texture comparison (`build/world-texture-quality-final`) repeats
the identical-pixel/savings checks and verifies zero retired CPU bytes after
detail shutdown. Python syntax, documentation commands and diff whitespace pass.

Next useful work: coverage-preserving cutout filtering, shared/instanced object
geometry, and specific remaining mesh-quality cases. Longer map-level fades are
not a replacement for finer spatial streaming. Actual weak/integrated hardware
and total RAM impact remain unvalidated; do not infer those from synthetic DXGI
pressure checks or the GPU-payload reduction above.

Source/executable baseline: `work/world_marathon_20260929/`. Changes remain local.

## 2026-09-29 follow-up: terrain lighting transition

User requested continued work. Recovered the remaining lighting discontinuity:
height and albedo blended, but the detailed terrain normals replaced overview
normals immediately. Shared overview vertex-normal construction and triangle
interpolation now supply a packed coarse normal to detailed vertices. The shader
blends it over the same fraction; full detail bypasses the blend. One extra packed
32-bit vertex attribute raises vertex-buffer storage 36 -> 40 bytes. No additional
textures/draw calls; resource accounting automatically includes the larger stride.

Matched hidden Guild/Oakvale runs (`build/world-normals-before`, `...-after`) hold
fractions 0..1, disable animated water, and differ only in the normal-blend toggle.
Mean max-channel change at publication: Guild 0.68794 -> 0.17283 (74.9% reduction);
Oakvale 2.42430 -> 0.23765 (90.2%). Full-detail endpoints are pixel-identical for
both. The Oakvale publication screenshot was inspected. This measures the entry
lighting discontinuity, not elimination of all geometric/object transitions.

Normal Release and clang profile builds pass. All four CTest suites pass, including
new coverage for both triangle halves, planar/shortened-edge sampling, empty input,
normal packing/clamping and the vertex layout. Five hidden scripts pass at 16 ms
pacing: model_browser, world_detail_range, world_detail_cache,
world_memory_pressure and world_water (`build/normal-blend-ui-checks.log`).

`build/world-flight-normals-20260929`: 494 continuous frames, 3,947 watchdog polls,
no visible/foreground window, zero sampled culling coverage mismatch, largest
transient missing-pixel candidate two and stationary colour-change candidate 243.
Focus-distance coverage check passes (at most 48 pixels under the existing 64-pixel
threshold). The worst stationary frame near Hook Coast was inspected: object
arrival/dithering remains. These timing-dependent route counts are not a matched
A/B improvement claim; use the held-pose comparison above for that.

Clang profile `build/profiles/world-normal-blend`: 24 prepared maps / 304.9 MiB
geometry in total (not peak residency); editor-update/viewport CPU p99/max
4.013/5.642 ms; GPU viewport p99/max 0.927/1.204 ms; paced upload p99/max
2.615/2.912 ms; no profiler diagnostics. Captured at 33 ms pacing, with no other
owned rendering check running. This is an overhead sanity capture, not an FPS
benchmark or controlled before/after performance comparison. Docs command check,
Python syntax check and diff whitespace check pass.

Next: per-object detail transitions/shared geometry and texture resources; weak-
hardware validation remains open. The abrupt terrain normal swap is now addressed,
while interpolation error, silhouettes and object dithering remain visible.
Source baselines and the pre-change executable are under
`work/normal_blend_20260929/`. Changes remain local/uncommitted; installation and
published package unchanged.

## 2026-09-29: model browser and world detail distance

User resumed Forge, then specifically reported low-quality meshes, pop-in/out,
and requested greater render distance where the computer supports it. Continued
in the clean `D:/Code/FableForge` checkout on `feat/editor-ui-shell`, base `da9782c`.
These changes are local/uncommitted; the existing release/package is unchanged.
No game-install writes, visible launches or publishing in this continuation.

**Models:** Assets > Models now opens a lazy searchable render-mesh index, not the
import form. Reuses forgecore meshpreview/makeMesh and extracts the existing
thumbnail draw path into a shared preview renderer. One GPU mesh and one preview
image are retained; camera changes do not upload the mesh again. Drag/scroll orbit,
wireframe, reset, material links, geometry/primitive/bone data, helper points and
object/creature/building definition references are available. Context reload and
Refresh invalidate the model and references. Import model exposes the existing
workflow; the Objects-tab import shortcut opens it directly. The user guide has
an inspected screenshot. Animation/collision/helper-axis previews and export/place/
replace actions remain separate.

**World quality/range:** overview terrain targets 80 long-side quads instead of 40
(cache key versioned). Detail residency no longer changes with orbit focus distance.
Manual distance is adjustable 100..1000 m and map limit 1..32. Automatic detail starts
at six maps, grows toward a default 24-map ceiling only under sustained spare frame
time, and scales the base radius up to 2x. DXGI memory headroom and conservative
observed per-map costs constrain that ceiling. Missing telemetry falls back to at
most six; pressure clamps immediately, followed by normal resource fade/retirement.
See WORLD_PERFORMANCE.md for the exact policy and its heuristic limitations.

Validation completed:
- Release GUI, core tests and geometry-test targets built; all four CTest suites pass.
- Hidden `tests/ui/model_browser.txt`: 3,294 render meshes in this fixture; barrel
  96 vertices / 188 triangles / two referencing definitions. Name/id filtering,
  wireframe, alternate orbit, texture navigation and refresh pass. Screenshots seen.
- `FABLEFORGE_AUTOMATION_HIDDEN=1 python tools/test_meshimport.py`: PASS against
  copied scratch banks, including the revised import-card entry point.
- Hidden `tests/ui/world_detail_range.txt`: 18 detailed maps at 500 m; same eye with
  orbit distance 200 -> 8000 keeps all 18 with no new loads. Synthetic ample memory
  permits ceiling 24; overbudget telemetry clamps to one; unavailable telemetry
  caps expansion at six. Screenshot inspected at Guild (130 m eye height).
- Existing world_detail_cache, world_memory_pressure and world_water scripts PASS
  at 33 ms pacing. Cache invalidation, revisit and water toggles remain exercised.
- `python tools/test_world_flight.py --output build/world-flight-range-20260929
  --cache build/world-range-cache --route world --continuous --focus-distance 5
  --assert-focus-coverage`: PASS, 494 captured frames, 3,700 watchdog polls with no
  visible/foreground window, zero sampled coarse/detail culling mismatch. Largest
  transient missing-pixel candidate five; sampled live/reference candidate seven;
  focus comparison at most 48 pixels (under its existing 64-pixel threshold).
  Contact sheet inspected. Largest stationary colour-change candidate 980 pixels
  near Greatwood/Guild during detail arrival. This is not a no-pop-in claim or an
  FPS comparison; the route also crosses authored empty world space.
- docs command check and git diff --check pass.

Next visual work: overview/detail normal and lighting continuity, per-object LOD
transitions/shared mesh resources, and controlled performance captures on weaker
hardware. Existing object previews already use LOD 0; this pass improves terrain
sampling and coverage, not authored low-poly object topology. Larger manual ranges
were exercised locally, not certified against every hardware budget. Resume from
this entry rather than repeating the completed release preparation below.

## Release preparation: 0.18.0-rc.1

The user explicitly requested GitHub publication, a version/release/docs update,
then a visible launch of the packaged build. This supersedes the earlier no-push
working-session constraint. Publishing as a prerelease preserves the outstanding
fresh-game, human visual and separate-machine gates in `docs/RELEASE.md`; 0.17.1
remains the latest final release. Notes: `docs/releases/0.18.0-rc.1.md`.

The full release rebuild exposed `1L << 40` in the distant-texture DXT1 encoder.
On Windows, long is 32-bit; a four-shade block decoded as 255/255/255/255. Replacing
the invalid sentinel with `numeric_limits<long>::max()` restores 0/85/170/255.
Only palette-index selection changes; endpoints, block framing and container
layout remain unchanged. An exact round-trip regression covers the four shades.

Packaging now derives the version/suffix from CMake, validates its staging path,
and includes release notes/README screenshots. CI runs all four CTest suites.
`FABLEFORGE_AUTOMATION_HIDDEN=1` hides scripted children throughout scratch suites,
without affecting an ordinary interactive launch. The CLI reference was regenerated;
the engine-rules document now reflects supported extracted installs.

Release checks uncovered test-harness maintenance: ui_smoke tried to unlink a
subdirectory; newlevel's off-grid subcase looked for a Level-tab button on the
Objects tab and ignored its failed assertion; overworld assumed stock's 399 maps
instead of independently counting the fixture WLD; mods used a brittle 110..112
upper bound on changed records (this customized base needs 113). Fixes preserve
the 95-new-record/minimum-edit expectation and the existing content, layout,
refusal, deployment and rollback checks. Changed checks must pass on rerun.

Validation completed: all 33 broad checks passed across the initial run and
targeted reruns (`build/release-checks.log`, `build/release-rerun-*.log`); no corpus
skips reported. The strengthened newlevel check also passed. README screenshots
were regenerated and inspected; temporary ready-toasts are cleared for publication.
The 18.0 MB ZIP was extracted to a separate scratch folder and used as the working
directory: CLI info, GLB export with foliage/objects, defs listing, and hidden world
water/detail-toggle GUI checks all passed. `build/release-package-smoke.json` records
the tested ZIP SHA-256 and extracted path. The normal binary has profiling disabled
and only Windows/UCRT/D3D imports. Package and source version are 0.18.0-rc.1.

## Previous: continuous overview-to-detail material transition

The next marathon pass traced a remaining terrain snap: height morphing was
already continuous, but publication immediately switched to the detailed albedo.
World ground now samples the existing overview SRV during its transition and
blends toward detail using the same fraction as height. The shared SRV is retained
with AddRef/released on tag removal and layer clearing, and connected in either
upload order. It allocates no duplicate texture and adds no draw pass. Resource
accounting continues to count owned allocations rather than shared references.
Full-detail shading skips the additional sample. Object/plant dithering remains.

Automation adds `world_transition_hold <fraction|auto>` and an A/B material switch.
`tools/test_world_material_transition.py` holds Guild/Oakvale poses, disables
animated water, and captures fractions 0..1 in eleven steps. Hidden launches use
a fixed outer size through `test_world_streaming.py --size`. Initial unrestricted
window-size comparisons were rejected because their viewport dimensions differed;
use only `build/world-material-fixed-{before,after}` for A/B results.

Both controlled runs pass. Cropped viewport publication (coarse -> held fraction 0):
- Guild: pixels changing by >40 in any channel 8,709 -> 8; mean max-channel delta
  7.9170 -> 1.2872.
- Oakvale: 12,695 -> 1,550; mean max-channel delta 12.1337 -> 4.6388.
- Full-detail endpoints are pixel-identical at both poses. Intermediate steps still
  include moving terrain silhouettes and object dithering, not just texture changes.
The Guild endpoints were visually inspected. These diagnostics show reduced
publication discontinuity, not elimination of every geometry/lighting seam.

Normal and clang profile builds pass. `build/profiles/world-material-blend` retains
the same 24 prepared maps / 280.6 MiB geometry across four regions. Editor update
p99/max 3.246/4.335 ms; GPU viewport p99/max 0.614/0.620 ms; no profiler diagnostics.
A 33.407 ms Present wait remains unexplained. This is an overhead sanity capture,
not a controlled FPS improvement claim.

The broad zero-cache route `build/world-flight-material` passes: 473 continuous
frames, 2,594 watchdog polls, no visible window/focus capture, zero sampled culling
coverage differences, focus-distance regression passed. Largest transient missing
candidate is two pixels; no stationary colour-change candidates above the existing
threshold. Contact sheet inspected. Actual capture dimensions differ from the prior
turn's flight, so do not treat these candidate counts as a matched A/B speed or
quality score; use the controlled material test for that comparison. Authored void
and geometry/normal seams remain. Rebuilt CPU geometry tests pass.
Return-trip cache/invalidation, memory-pressure eviction/recovery, and water-toggle
UI regressions all pass at 33 ms pacing after the shared-SRV change. No game-install
writes, visible launches, commits or pushes. Next candidates remain shared mesh/
texture resources, object LODs, and matching overview/detail normals at publication.

## Previous: GPU memory telemetry and adaptive inactive cache

The next marathon pass added `Renderer::queryVideoMemory()` using
`IDXGIAdapter3::QueryVideoMemoryInfo` (local segment, node 0). World detail samples
once per second. The inactive cache shrinks under pressure, retaining up to
128 MiB / one sixteenth of the reported budget / estimated room after headroom,
whichever is smallest. Recovery grows at most 16 MiB per five healthy samples;
failed queries retain the previous bounded allowance. Only inactive maps are
evicted. Active maps, overview terrain and 178 overview water batches are outside
this policy. This does not bound the entire working set or implement object LODs.

`src/detailcache.hpp` holds the tested pure policy and ordered trim operation.
Automation's `world_memory_sample` injects budget/usage, unavailable data, or
restores real queries with `auto`. It never allocates VRAM or changes OS budgets.
The existing return-trip cache test now injects ample room for hardware-independent
expectations. `test_world_flight.py --memory-pressure` extends visual comparisons
to an exhausted synthetic allowance. Tracy records query/trim zones and process
budget/usage/cache allowance plots; state dumps expose validity and injection.

Validation: normal MinGW and clang profile builds pass; all four CTest
suites pass. `world_memory_pressure.txt` passes in both builds, evicting one cached
map, retaining active terrain/water, reloading on revisit, and recovering gradually.
The existing return-trip/invalidation cache regression also passes at 33 ms pacing.
Actual DXGI reports valid data on this machine (budget 16,198,258,688 bytes at
capture time; dynamic, not a hardware-capacity claim). The single-map profile
`build/profiles/world-memory-pressure` reports a 0.00657 ms real query and maximum
0.00528 ms inactive trim, with no GPU profiler diagnostics.

The normal full-world flight `build/world-flight-memory` passed: 473 continuous
frames, 3,026 window polls with no visible/foreground window, sampled culling
coverage differences zero, focus-distance regression passed. Largest transient
candidate was nine pixels; one 316-pixel colour transition near Hook Coast remains.
The contact sheet was inspected. Several poses face authored empty world space;
this coverage test is not proof that every rendering transition is smooth.

The zero-cache flight `build/world-flight-memory-pressure` also passed: 473 frames,
2,887 hidden/focus polls, zero sampled culling differences and at most nine transient
missing pixels. It additionally exposed 1,067/1,035-pixel stationary colour changes
near GuildExterior during detail arrival, plus 317 near Hook Coast. An affected
Guild frame was inspected: detail transitions remain visible; do not describe this
as all pop-in fixed. The harness's new report flag was added while this run was
already executing, so the generated script/state logs are the injection evidence.

`build/profiles/world-memory-stream-overbudget` follows the existing four-region
24-map route, then climbs and trims four inactive maps (122,743,308 tracked bytes)
to zero. Eight real DXGI queries: median 0.00598 ms, max 0.00791 ms. Maximum trim:
0.05691 ms. No profiler diagnostics. Editor update p99 3.249 ms / max 5.327 ms;
whole-frame maximum includes a 9.8 s automation profiler-connect wait, not an
interactive editor stall. These runs are not FPS benchmarks. The first scratch
variant incorrectly expected zero retention at usage exactly equal to budget:
reclaiming cache left enough room for one map after headroom. Its assertion failed
correctly; the final forced-overbudget variant uses 2 GiB usage against 1 GiB budget.

Next renderer work: shared geometry/texture resources and intermediate detail LODs,
measured independently of this inactive-cache policy. UI design and other backlog
constraints below remain in effect. No game-install writes, commits or pushes.

## Previous: dispatch latency, upload budget and additional research

Continued the user's marathon request, then researched more GitHub/primary-source
options while the final hidden route ran. The user subsequently closed their game;
visible launches were not needed. Findings and ranked implementation candidates:
[WORLD_RENDERING_RESEARCH.md](WORLD_RENDERING_RESEARCH.md). No new third-party
dependency was installed. Meshoptimizer/shared geometry/DXGI memory budgets are
promising; Intel MaskedOcclusionCulling was verified archived September 7, 2026.

Implemented two measured streaming improvements in `updateWorldDetail`:
- A ready CPU future bypasses the 400 ms demand timer. Current eye/generation/
  wanted checks still run before accepting it; ordinary idle demand stays throttled.
- GPU detail uploads retain the 2 ms soft budget but raise the batch-count guard
  from 4 to 32. Profiling showed 246/338 upload frames stopped below budget at four;
  the new capture used 1..16 batches. Individual allocations can exceed the budget.

`tests/ui/world_stream_profile.txt` is a screenshot-free four-region/altitude route.
`profile_world.py --frame-ms 33` now supports reduced-contention captures and
reports serial prepare-to-first-upload latency (unmatched/discarded work separate).
`test_world_streaming.py` accepts script lists and frame pacing, and uses below-
normal child priority. Normal and clang profile builds are current.

Three comparable captures, each with 24 prepared maps and identical geometry totals,
had no profiler diagnostics. Median readiness latency: 393.75 -> 48.17 ms after
dispatch; 49.48 ms with time-budgeted uploads. Four load phases total: 24.65 ->
17.22 -> 14.20 seconds. Detail upload frames: 337 -> 338 -> 238. This is a 42.4%
loading reduction at fixed 33 ms pacing, not an FPS claim. Upload p99/max rose
2.92/3.64 -> 3.51/4.63 ms; the work is less spread out. Complete evidence and caveats
are in WORLD_PERFORMANCE.md and `build/profiles/world-stream-{before,after,budget}`.
The combined comparison JSON is `build/profiles/world-stream-dispatch.json`.

Validation: cache reuse/invalidation script PASS at 33 ms pacing. Full-world
continuous routes passed after dispatch alone and after both changes. Final
`build/world-flight-budget-final`: 473 frames, 3164 visibility watchdog polls,
zero visible/foreground samples, zero sampled culling coverage differences,
focus-coverage assertion PASS, only a five-pixel transient candidate. Largest
stationary colour change was 338 pixels at the known Hook Coast transition;
contact sheet inspected. No large transient empty patch reproduced in that route.

After the game closed, `world-stream-game-closed` captured the same route at 4 ms
pacing: CPU editor-update median/p99/max 0.732/3.313/6.171 ms; GPU viewport
0.121/0.818/0.935 ms. One Present call took 30.836 ms; cause is unproven. Frame
duration includes deliberate sleep and hidden-window presentation, so do not infer
FPS from it or directly compare it with the earlier competing-game capture.
No new rendering test processes remain. No commit, push or install writes.

## Earlier: full-world flight and reproduced far clipping

User clarified that diagnosis must cover the large 3D world at varying heights.
The harness now defaults to `--route world`: 89 poses across Albion, absolute eye
heights 50..7000, climbs/descents/turns. `--continuous` captures every live frame
and world state; `capture_begin/end` preserve explicit screenshots too. A rolling
three-frame analyzer ranks temporary missing coverage and large changes at fixed
poses. `world_pose` moves without forcing demand or changing orbit focus distance.

First full route saved 473 consecutive frames (`build/world-flight-global-continuous`).
Its reference phase timed out: old `wait_world_detail` demanded at least one map
even at heights where zero maps is correct. This was an automation bug, not a
60-second UI freeze. It now refreshes demand once and accepts a settled empty set.
Partial-run sequence analysis found only 1..3-pixel transient holes and a largest
stationary colour change of 345 pixels near Hook Coast; this is not proof of the
user's original flash. Its window watchdog saw no visible/foreground child windows.

A separate identical-pose test DID reproduce missing world geometry. With orbit
distance 20, the far plane was only 1600 despite flying far above the world. One
sample lost 127,024 covered pixels compared with orbit distance 8000. Turning
frustum culling off did not help. `Renderer::render` now derives world far clipping
from loaded overview/detail batch bounds instead of orbit distance; map views are
unchanged. CPU tests verify enclosure of every world-box corner from high/outside
eyes. GUI build and focused core CTest passed.

Paired before/after captures: `build/world-far-before` / `build/world-far-after`.
Maximum focus-dependent missing coverage fell from 127,024 to 16 isolated pixels;
positions reviewed, no connected patch remains in that comparison. Minor camera
matrix/silhouette differences mean exact RGB equality is not this test's oracle.
`--assert-focus-coverage` provides a 64-pixel tolerance regression gate; the saved
baseline fails and fixed captures pass. Both hidden runs exited 0/PASS, with zero
visible/foreground samples (739 and 766 watchdog polls respectively). A final
continuous full-detail route is recorded separately under `world-flight-global-fixed`.
This fixes a proven disappearance mechanism, not every visual/streaming issue.

The full-detail follow-up completed exit 0/PASS in about 157 seconds: 473 consecutive
live frames, nine same-pose reference samples, zero culling coverage differences,
one nine-pixel transient candidate and no >128-pixel stationary colour changes at
the 80/255 threshold. Its 3072 watchdog polls saw no visible/foreground windows.
Reviewing the images exposed a second orbit-distance dependency: haze darkened
the restored world after zooming close. World fog now uses enclosing world span
instead. That final shader-constant change is checked separately by the paired
`build/world-far-fog-final` run; the 473-frame run predates the haze adjustment.
Final haze/clip run passed its focus-coverage assertion and native automation;
815 watchdog polls saw zero visible/foreground child windows. At sample 84 the
>40/255 colour-change count fell from 48,399 before the haze adjustment to 111
(small raster/silhouette differences and animated water remain). Final before/
after image: `build/world-far-fog-final/before-after.png`, visually inspected.
No GUI test process remains. Normal build is current; profiling build has not
been rebuilt for these last projection/automation changes. No commit or push.

## Earlier: autonomous hidden flight diagnostics

The user still sees patches flash/empty inside the viewport and asked for testing
while gaming. Do not launch visible windows or steal focus. Added
`tools/test_world_flight.py`: deterministic Start/adult Oakvale routes, low flight,
optional one-map budget/altitude stress, same-pose coarse/settled references and
culling-disabled comparisons. Captures include state logs, PNGs, contact sheets,
GIFs and JSON. Missing-coverage checks have synthetic positive/negative controls;
they are diagnostic, not a complete visual-quality oracle. Automation adds
`world_eye_ground x y clearance [yaw pitch]` without forcing demand updates and
camera telemetry in `dump_state`. `--auto-frame-ms` defaults to 4, harness uses 33.

Five hidden runs completed (normal Start/adult, adult stress, two Start stress
routes), 69 sampled live poses plus references. All native exits were zero and
automation logged PASS. Across 4,892 watchdog polls, zero child windows were
visible or foreground. No test process remains. Normal GUI build and Python
syntax/coverage controls passed. No renderer fix was made in this latest step.

Artifacts: `build/world-flight-start`, `world-flight-oakvale`,
`world-flight-oakvale-stress`, `world-flight-start-stress`, and final
`world-flight-start-overhead`. Normal runs used four phases; the last two also
compared settled detail with culling disabled. No sampled transient missing
coverage or culling coverage differences were found. The first altitude stress
view looked past the terrain at some poses; final stress pitch 1.5 keeps the
ground in view. Contact sheets inspected. This does NOT reproduce or resolve the
user's reported flash: captures are sampled, not every rendered frame, adaptive
quality is disabled, and only clear-background holes are detected. Visible coarse
terrain/detail arrival and disconnected world edges still merit investigation.
Overview-relative clearance also cannot guarantee clearance from detailed terrain
or buildings. Next diagnosis should capture consecutive render frames and widen
routes/settings, rather than asserting this issue fixed from sparse clean frames.

## Design contract

The user's direction in ROADMAP_1.0.md: **non-intimidating, intuitive, beautiful,
modern, functional, powerful. Port vanilla behaviour, never its UI.**
Use visual browsing, contextual actions and compact inspectors. Put technical
details and uncommon controls behind tooltips or an intentional advanced view.
Selection must not open a large popup or interrupt camera movement.

## This continuation

- Finished the pending pinned panel/sub-tab headers, fitted labels and hover
  details, compact hints, Speed value placement, labelled new-level coordinates,
  View > Text size, bounded/scrolled Help and tool windows. Activity follows
  available height until explicitly toggled; starts folded on short windows.
  Fractal preview now leaves space for Apply and Defaults at 1366x768 / 150%.
- Fixed the world-picking bottleneck: each ray sample called `worldPlacement`
  for every box, repeatedly scanning/lowercasing the entire world list. Use the
  already-known box and pending moves instead; preserve smallest-overlap rules.
  The transcript's roughly 280-second detail tour now completes in roughly
  6 seconds on this machine with warm tiles. Instrumented uploads were only
  33–54 ms/map: renderer expansion was not the dominant cause of that delay.
- Water now lives in the persistent overview at WLD origins, using the existing
  ripple/shore fade/ice shader, alpha blending and depth testing after opaque
  geometry. It stays visible while detail loads, unloads or is disabled. The Water
  toggle is independent of detail; leaving 3D releases the overview GPU buffers.
- Frustum culling of world terrain, detail and water batches. Oakvale comparison:
  653 submitted batches without culling, 249 with culling (404 skipped), with
  pixel-identical static screenshots. This is not occlusion culling.
- Automatic detail adjusts the map count up to the user's limit (default six):
  reduce after two seconds below 45 fps, restore after five seconds above 55 fps.
  Ignore loading, focus loss and isolated >250 ms stalls. Manual mode fixes the
  map count. This is the existing coarse-tile/full-detail transition, not new
  per-object mesh LODs or dynamic resolution. Choices currently last the session.
- Completed loads are checked against current camera demand and a generation
  counter before upload, so obsolete work after moves/options changes is dropped.
- Scale-sweep success now requires a clean process exit as well as RESULT PASS.
- Moved detailed geometry expansion/bounds to workers, paced GPU uploads at
  four batches / soft 2 ms per frame, and publish a map only after its
  batches are ready. Coarse/detail terrain transitions use a complementary
  250 ms opaque terrain height morph and separate object/plant dither fade;
  persistent water is unaffected. Residency hysteresis reduces churn.
- Paced overview texture and geometry uploads too. Removed another quadratic
  map lookup in every-frame overview maintenance. In a matching settled Oakvale
  phase, Tracy measured median maintenance falling from 6.584 ms to 0.165 ms.
  See [PROFILING.md](PROFILING.md) for comparison limits and captures.
- Added optional Tracy CPU/worker/D3D11 GPU zones, counters, a read-only route,
  phase summaries and isolated cold-cache capture. Normal builds compile these
  markers away. GCC profiling exposed a separate thread-local destructor crash
  under cold loading; profiling now requires the clang-cl/MSVC ABI build helper.
- Fixed black terrain on partially baked sea/filler maps: uncovered background
  pixels now use the existing LEV theme sampler. Covered STB colours are retained.
  Revised the tile cache key so old black tiles are rebuilt. Actual-data check
  `tools/test_world_background.py` matched 192,512 uncovered pixels to the LEV
  fallback; StartOakValeWest's fully covered output stayed pixel-identical.
  Dark space outside actual geometry is still the viewport backdrop.
- Continued with CPU-only terrain scheduling while the user is gaming. Replaced
  fresh per-bake thread groups with one bounded persistent `RowExecutor`, shared
  by foreground and LEV fallback paths. At most eight row workers (fewer on
  smaller CPUs), a queue capped at twice that count, inline nesting to prevent
  deadlocks, and exception draining before caller-owned image memory can die.
  Tiny bakes remain inline. Outer overview/decode workers are unchanged.
  Added queue/join profiler zones and a standalone concurrent scheduler test.
  Before/after CLI outputs for OakVale_Sea_02 and StartOakValeWest were byte-
  identical across all three diagnostic PNGs per map (`build/row-pool-compare`).
  No flight speedup is claimed until the deferred graphics measurements run.
  Normal and clang-cl profiling builds passed; the focused row-pool tests passed
  in both. Normal CTest is now 3/3 passing (including the new scheduler suite).
- The next cold trace found main-thread destruction of uploaded CPU data taking
  up to 19.281 ms. `DeferredRelease` moves it to one worker; streaming waits for
  that slot without accumulating a backlog. A follow-up cold capture measured
  main-thread handoff at at most 0.0174 ms, with the actual freeing on the worker.
  Tests cover retained ownership while busy, worker-thread destruction and drain
  on shutdown. GPU resources still belong to the render thread.
- Streamed layers and water now use indexed geometry instead of repeating a full
  vertex for every triangle. Each source vertex is transformed once per
  instance/material; triangle order, materials, UVs, shore alpha and ice remain.
  This is not GPU instancing: copies still have separate transformed vertices.
  CPU geometry tests passed. The full world viewport matched the pre-indexing
  image pixel-for-pixel (only an Activity elapsed-time digit differed outside it),
  and full-window culling on/off images matched. Hidden water/editor routes passed.
- Profile report parsing now handles commas in Tracy messages and keeps internal
  diagnostics out of route phases. Plot CSVs expose geometry bytes versus the
  old expanded equivalent. A pinned D3D11 overlay retries delayed timestamps;
  actual query errors remain visible. See PROFILING.md and vendor/VENDORED.md.
- Indexed cold capture passed: 50.39% less prepared geometry (204.7 versus 412.6
  MiB across 15 maps; not peak residency), five delayed GPU reads successfully
  retried, no diagnostics, matching viewport/UI CPU and GPU submission counts.
- Applied the format-I/O skill to the read-only STB optimization. The World tab
  reread the whole common header per map; a new bulk API reads it once, retaining
  the single-record slice/validation contract. Map placement and region ownership
  now use first-match indexes. The scripted opening step fell from 229.754 ms to
  8.193 ms; 401 map-list and 341 region-list lines match the saved CLI byte-for-byte.
  Unit coverage includes unknown-byte preservation and bad record end pointers.
  No writer or game-install file was changed.
- Tile cache v3 uses raw Windows file metadata, avoiding compiler-specific
  file-clock epochs. This lets MinGW Release and clang-cl profiling share tiles.
  Normal/profile builds passed; the latest core suite passed 4/4, including
  scheduler/retirement and CPU indexed-geometry tests. Last traces and comparisons
  live under `build/profiles` and `build/world-layout-compare`.
  Cross-toolchain cache validation also passed: the profile build reused all
  400 Release-generated tiles without changing their bytes or modification times
  (`build/cache-abi-ia3_ss00`). All test launches in this pass were hidden.
- Persistent overview water uses conservative flat-quad merging: only equal
  height, fade and ice values merge, within one liquid/ice stream and at most
  32 cells per side. Shores, holes, varying heights and ice boundaries retain
  their triangles. Unsupported topology is returned unchanged. This changes
  derived preview geometry only, not game files or the engine-derived water bake.
  Synthetic coverage/interpolation checks and cache round-trip/truncation tests
  pass. Actual 400-map capture contains water in 178 maps: CPU payload fell from
  26.65 to 9.81 MiB (63.2%); compact GPU buffers total about 14.70 MiB. These byte
  counts exclude container/allocator overhead, textures and the rest of the world.
  Hidden overview/detail/off screenshots confirm the coast remains visible.
  `world-overview-water.tracy` completed without diagnostics; its water GPU median
  was 0.010 ms, but concurrent gaming means this is not a controlled FPS claim.
- Cache format FWT2 / key v4 adds water and includes game.bin, names.bin,
  textures.big, STB and gain alongside the LEV/WAD source revision. Old tiles
  rebuild once. Raw Win32 timestamps keep keys portable between our compilers.
  Keys are captured when overview loading starts; live external edits still need
  a reload. Prepared full-detail geometry does not yet have a persistent cache.
- The water capture exposed repeated all-name searches in the 2D canvas too.
  Known boxes now supply their positions directly, with pending moves applied;
  draw ordering, hover selection and drag semantics are unchanged. Added a
  `World 2D canvas` profiler zone for attribution. Follow-up cold capture
  `world-water-canvas.tracy` reduced the overview phase's median editor update
  from 14.651 to 0.942 ms; the canvas itself measured 0.245 ms. The existing
  pending-move world route passed, along with the updated water route (178
  resident water maps independent of detail, and checkbox clicks with detail off).
  The complete four-test suite passed after these changes. Water screenshots
  were inspected; launches stayed hidden and below-normal priority.
- Terrain baking now reads/decompresses each map's STB frames once for both
  foreground layers and background colour. The standalone diagnostics retain
  their independent reader APIs; no cross-map cache, owning TLS, format parser
  changes or writer changes were introduced. Frames are released before texture
  row work. Six before/after maps (OakVale_Sea_02, StartOakValeWest, HookCoast,
  Greatwood_1, PrisonIsland, HeroGuildComplexInside) produced byte-identical
  engine/background, baked and LEV-fallback PNGs (18 files total), recorded in
  `build/stb-read-share-compare/comparison.json`. Both builds and the affected
  core test passed. Final cold capture `world-stb-shared.tracy` passed without
  diagnostics; all 400 resulting tile-cache files are byte-identical to the
  preceding capture (`tile-comparison.json`). Median terrain-build time across
  415 bakes fell from 20.690 to 15.098 ms. Foreground and background STB zones
  previously totaled 8.041 seconds of worker time; the combined read/decode
  totaled 4.879 seconds. Those are summed, overlapping worker durations, not
  elapsed load time, and concurrent gaming limits controlled timing claims.

## Verification and limits

Latest user follow-up: whole patches briefly flash/disappear **while still in
the viewport**, not just distant buildings arriving. The subsequent changes are:

- Detail residency follows the eye's horizontal position instead of the centre
  ray's ground intersection, which could jump across the world when looking near
  the horizon. The 250-unit radius extends up to 500 for a long focus distance;
  existing map-count limits, height gates and retention hysteresis remain.
- Recently inactive, fully uploaded detail maps stay hidden in a 128 MiB / six-map
  LRU. Returning restores GPU resources without decoding or uploading again.
  Accounting includes vertex/index buffers and each owned RGBA mip-0 texture;
  it excludes driver overhead and does not cap active maps or total app memory.
  Wanted cached maps are rescued before outgoing maps can evict them. Oversized
  maps are dropped without flushing useful smaller residents. Disable detail,
  leave World, reload layout/install, or change placement/detail options to clear
  the cache and invalidate in-flight workers. This is not a disk detail cache.
- Ground no longer uses complementary screen-space cutouts against a differently
  shaped coarse mesh. Full ground remains opaque and moves from sampled overview
  triangle heights to its detailed heights. Its otherwise unused `walk` vertex
  attribute holds the coarse height; vertex stride/memory is unchanged. Bounds
  include both endpoints for culling. Coarse ground hides only after a complete
  ground-bearing map uploads; it returns when fade-out reaches the coarse shape.
  Water remains independent; object/plant fades still use dither. This does not
  add intermediate mesh resolutions, object LODs or GPU instancing, nor eliminate
  every possible seam between differently authored neighbouring maps.
- World near clipping uses clearance over terrain (0.1 to 20 units), not the old
  orbit focus distance retained during free flight. Descending from a distant
  overview can therefore no longer retain a focus-derived near plane tens of
  metres ahead. Other views retain their existing clipping policy.

Tests cover LRU byte/count limits, oversized rejection, replacement, reuse and
clear; triangle-vs-bilinear overview heights; and near-plane bounds. The hidden
`world_detail_cache.txt` route checks horizon turns without new builds, a return
trip without another build, and invalidation on detail disable / leaving World.
`world_transition.txt` records start/middle/end ground transitions and a descent
that deliberately keeps an 8,000-unit focus distance. These are targeted fixes
for verified mechanisms; the user's exact flight has not been reproduced.
The normal build and focused core/geometry tests pass. Five hidden routes passed
with clean native exits: world_view, world_water, world_detail_cache,
world_transition and world_culling. Culling on/off images remain pixel-identical.
Mid-transition and low-flight screenshots were inspected: no map-sized empty
patch, but a pixel comparison still found 33 newly clear pixels within shared
start/end coverage (thin seam/silhouette differences). Do not claim all cracks
or all pop-in are eliminated. The clang-cl profile build also passes.
`world-detail-cache.tracy` passed with no diagnostics: exactly two map builds
(initial and after explicit invalidation), with none on the cached return.
A separate six-map Oakvale return check retained five maps (47,344,936 bytes),
reused all five, and rebuilt only the oversized sixth: loads 6 -> 7 rather than
6 -> 12. The cache stayed below its 134,217,728-byte allowance. Evidence:
`build/world-cache-six-result.json`. This checks real textured/foliage maps as
well as the one-map deterministic cache test.

Core tests include six-plane culling, clipping boundaries, a containing map,
and adaptive-budget pressure/recovery/focus/stall cases. UI scripts:
`world_view.txt` (including pending-move picking), `world_water.txt` (on/off,
unload/reload), `world_culling.txt` (paired screenshots), `editor.txt`,
`owner_daynight.txt`, `height_pens.txt`. Scale coverage includes 1280x720 / 100%,
1366x768 / 150%, 1920x1080 / 100%, 2560x1440 / 100%. True 4K remains untested.
The 132-item UI review is a historical issue list, not 132 independently closed
defects. No game-install changes or in-game validation belong to this pass.

World streaming exposed intermittent Windows heap corruption (0xC0000374) in
MinGW thread-exit TLS destruction (`build/world-crash-gdb.log`, `run_dtor_list`).
`walkFramedBlocks` had a thread-local owning scratch buffer. It now owns the
same uninitialized/grow-only buffer within a scan, eliminating TLS destruction.
Ten consecutive release world/water runs passed after this change; the saved
pre-change binary reproduced the crash again in the comparison. This isolates
the lifetime change; it does not prove a specific compiler/runtime defect.
Clang AddressSanitizer world and water tours passed even before that change,
so an ASan pass alone was insufficient here. Reproduce with
`python tools/test_world_streaming.py` (or `--exe` for a saved baseline).
ASan and temporary guarded-allocation builds are local diagnostics, not shipping
targets. The normal build's temporary CMAKE_PROJECT_INCLUDE was cleared.

The user is gaming and now permits occasional hidden launches that do not take focus.
`tools/profile_world.py` and `tools/test_world_streaming.py` now default to hidden
windows (`--show` opts in); hidden rendering still consumes GPU resources.
Full cold routes passed under clang-cl with the row pool and background payload
retirement. The latter run sampled the window 172 times: never visible or
foreground. CPU-only TLS stress is separate from graphics validation.
The clang-cl 22.1.8 profile build/helper and CPU-only stress passed: 4,096 workers,
4,096 recorded zones, clean test/recorder process exits. Evidence is
`build/profiles/clang-thread-lifetime.tracy`; no graphics window was launched for
that check.
The final ordinary Release build (`FableForge`, `forge`, both test binaries) and
core CTest passed 2/2 after the terrain fallback changes. Python helper syntax
checks and `git diff --check` passed. Earlier UI/scale coverage is listed
separately from the new hidden profiling runs. No commit or push was made.

## What remained from Claude, and the user's follow-up requests

1. **World performance:** per-object mesh LODs, GPU instancing/shared meshes,
   mip/compressed-texture residency, bounded scheduling
   beyond terrain rows, prefetch/cache eviction by memory budget and weak-GPU profiling
   remain. Uploads now have soft slices, but individual driver allocations and
   GPU resource destruction can still hitch. CPU payload disposal is now on a
   worker with bounded backpressure. See [research and GitHub
   candidates](WORLD_PERFORMANCE.md). No guarantee of hitch-free flight or
   cross-hardware fps.
2. **Asset browser first:** Assets > Models currently opens the import card.
   Add search by mesh name/id, orbitable textured/wireframe preview, material
   texture links, counts, helper points and used-by references. Follow with
   collision views, skeleton/skin preservation, animations and attachments.
   See PARITY_BACKLOG.md, “Mesh browser with 3D preview”, and animation sections.
3. **Particle/effect editing and creation:** start with lossless parsing/writing
   and clones/templates, then colour, texture, size, emission, life and lights
   with a preview. Preserve unexposed fields. Current static proxies are not an
   engine-faithful simulation. Reuse/cross-check EgoCore ParticleParser/Compiler
   and recovered runtime behaviour. Deep bank editing can remain in EgoCore.
   BLOCKED_LANES.md's corrections matter: its particle JSON route was export-only;
   a working two-way interchange must be implemented and verified, not assumed.
4. **Contextual viewport UI:** useful right-click actions (Focus, Duplicate,
   Delete, Drop to ground, Properties, suitable links), a restrained selection
   toolbar, and an on-demand inspector window that follows selection. Reuse
   existing editor operations and undo rather than parallel action logic.
5. **Broader parity:** multiple editable maps/regions, animation/bone/event tools,
   water generators, navigation cost/layer gaps, non-mesh things and asset
   dependencies remain in PARITY_BACKLOG.md / BLOCKED_LANES.md / HANDOFF_NAV.md.
   The separate water-generation branch still needs its own validation. Visible
   water in this view does not implement those generators.

Debug-editor evidence should define algorithms, defaults, ownership/selection
rules and serialization; EgoCore supplies format implementations and cross-checks.
Neither is a UI template. These are next-work plans, not implemented claims or
authorization to message Aeon/Discord. Re-read current code before tackling each
historical backlog item, because some entries predate later fixes.

## 2026-09-30: mod asset health in the dry-run report

`forge-tools assets missing-mesh <game-root> <schema.json> [--graphics path]
--json` audits direct `game.bin` Graphic model IDs against MBANK_ALLMESHES. The
Mods tab's Check conflicts run now compares the composed result against the
base install and displays newly introduced missing references before deploy.
Its JSON `asset_health` includes introduced rows, total/base counts, decode
count and unavailable status. Existing base defects do not become mod warnings.
The check cannot establish map scenery or scripted visibility correctness.

`tools/test_mod_asset_health.py` clones a retail barrel definition to a missing
mesh ID in a scratch mod. Check conflicts reports exactly that one new defect;
the hidden GUI run shows its name/ID and `mods_missing_models=1`. The screenshot
`build/ui/mod_asset_health.png` was inspected. The complete Aeon 5.03 archive's
graphics bank gives zero missing direct model references when composed with
Controller Support. All these checks are read-only or use scratch copies; no
game install was changed.

## 2026-09-30: Aeon root levels in FinalAlbion.wad

The Aeon 5.03 tree includes `Data/Levels/creature_hub.lev` and `.tng` outside
`FinalAlbion/`. Retail's WAD has entries at those same root paths. Forge's mod
composer previously repacked only `FinalAlbion/*.lev|*.tng`, leaving the two
WAD entries at the input bytes even when Aeon's loose files were copied to the
build. The repack loop now includes root `Levels/*.lev|*.tng` and appends any
new root levels at their actual archive paths.

`python tools/test_aeon_controller.py` passed with the complete local Aeon
archive and Controller Support package. It extracted the rebuilt scratch WAD
and compared all 843 Aeon level payloads byte-for-byte with the effective
build (594 loose overrides plus unchanged WAD entries), including both
`creature_hub` files. The baseline-aware composer reported 547 existing
entries replaced and 47 appended. The local input WAD has four extra scratch
entries, so the test derives its expected total entry count from the input WAD.
These are archive/content checks; in-game behavior on the author's Freeroam
route remains a separate gate. No game install was changed.

The same baseline change uncovered a stock-WAD TNG merge gap: `modsMerge` used
to look only for loose baseline TNG files. A stock install has those files in
`FinalAlbion.wad`, so two large packs could silently reduce to a whole-file
load-order copy and report zero thing conflicts. The composer now materializes
only touched WAD entries for its three-way TNG merge and collapses consecutive
byte-identical source TNGs. Project Seasons alone and AlbionSecrets Modpack
alone pass under UFP with 423 and 433 rebuilt WAD payloads checked against
loose output. Together they report 85 changed TNGs, three thing merges and 16
thing conflicts; all 433 rebuilt WAD payloads match the loose output. The
older zero-conflict combined result was invalid because it lacked the WAD
baseline. Forge does not require FreeRoam.exe for this WAD route; the separate
extracted-install layout remains supported by the editor. This only addresses
level storage. FreeRoam.exe's patched extraction sets every LEV cell's
walkability and camera passability to 1; the separate Freeroam.fmp supplies
76 patched LEVs. Forge now imports that FMP WAD bank, and
`python tools/test_freeroam.py` verifies all 76 survive a scratch WAD build.
Aeon's matching 76 LEVs are already fully walkable, so overlaying Freeroam.fmp
after Aeon risks replacing Aeon's terrain edits.

Dragon Cliff Restored V2 is another root-level WAD case. Its ZIP has eight
`Data/Levels/DragonCliff*.lev` maps and matching TNGs, plus Hook Coast edits,
world files and whole asset banks. `tools/test_dragoncliff.py` uses the full
extracted ZIP over a scratch retail root. It passes: 16 root entries appended,
two Hook Coast entries replaced, all extracted WAD payloads equal built loose
files, and no direct missing model references against the pack's graphics bank.
No game install was changed; in-game quest/visibility behavior is untested.

The full Lost Content 0.7.5 RAR was extracted into the ignored corpus for a
current single-pack check. `tools/test_lost_content.py` builds it on scratch
retail, extracts the output WAD, and compares all 1,002 source LEV/TNG paths
against the effective build. It passes: 498 WAD entries replaced, 206 new
entries appended, no direct missing model references against its graphics
bank. The parked `_FinalAlbion.wad` and its `userst.ini` do not become output
layers. The current `--with-aeon` audit passed with Aeon first and Lost
Content second: 3,085 definition changes (2,503 new), 36 field conflicts,
seven whole-record conflicts, 79 thing-merged levels and 1,812 thing
conflicts. Its output WAD replaces 568 entries and appends 247; all 815
built loose level payloads match WAD extraction. The final bank has zero
missing direct Graphic mesh IDs, but ID presence cannot establish that
Aeon's models/textures survived Lost Content's whole-bank winner. This pair
needs deliberate conflict review and in-game checks. No game install was
changed.

Expanded Chapters v1 was extracted from its separate content and graphics
RARs into the ignored corpus. `tools/test_expanded_chapters.py` passes over
scratch retail: 798 source LEV/TNG payloads resolve to the expected WAD,
all 583 emitted loose payloads match extraction, 579 entries are replaced
and four appended (two new maps). Its own graphics bank resolves every
direct Graphic model ID in the composed definitions. This is file-level
evidence; no game install was changed and in-game behavior is untested.
