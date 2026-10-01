# World view and UI continuation - 2026-09-29

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
