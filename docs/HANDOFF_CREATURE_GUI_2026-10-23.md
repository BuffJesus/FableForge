# Creature custom-import GUI, 2026-10-23

User resumed FableForge until asked to stop. Active checkout remains
`C:/Users/Cornelio/AppData/Local/FableForgeDevelopment/definition-clone-gui-20261010`,
branch `feat/definition-clone-gui-20261010`, starting HEAD `6fc51e7`.
The [preceding handoff](HANDOFF_CREATURE_CLONE_2026-10-10.md) records the four
preserved source files and existing native/CLI acceptance. The separate Git object
directory documented there is still required; missing HEAD without those environment
variables does not establish repository corruption. An initial redundant fetch was
interrupted; no branch reset or object-store configuration change was made.

## Current implementation

Private Creature drafts enumerate confirmed body/alternative/model IDs during
clone preflight and offer optional custom skinned glTF import. Inputs include model
and image browsing, explicit body alternative, full MESH_ name, existing texture ID
or a new named image, texture format, and stored/compressed mesh selection. Review
prepares the existing mesh plan asynchronously; Create uses the reviewed snapshot
and the existing coordinated publisher. Input/destination changes invalidate review.
No-mesh private cloning retains the existing publisher.

The captured glTF main file and external buffers retain canonical paths and hashes
of consumed bytes. Creation rechecks these, source banks, destination files/root,
the pack manifest and optional texture proof. Draft controls and cancellation are
disabled during review/publication; automation cancellation refuses while busy,
and close-prompt Discard waits for the import worker. Completion reloads the new
definition, model source, texture view and recovery inventory.

Intended delta: append three private definitions and one mesh, retarget only the
selected body-model word and verified private references, optionally append one
texture. Prior definition payloads, identities, names and asset entries must survive.
Other body alternatives, gameplay components and animations stay shared.
Runtime compatibility and assembled-creature presentation remain unverified.

## Source verification

- Owned GUI/forge/skinned-input build passes. Updated skin-input regression checks
  main/sidecar consumed-byte hashes and sidecar mutation. Skinned-input, static
  mesh-input and skinned-composition CTests pass after rebuilding affected targets.
- Public CLI regression passes all five skeleton fixtures: 25 fresh and five
  in-place publications/Restore plus 25 prewrite refusals, independent graph/mesh/
  pixel checks. Evidence: `creature-cli-9mqaudpa` under the retained 20261010 C root.
- Consolidated source GUI gate passes 40 real-control cases: stored/compressed,
  all three image formats, existing/no texture, pack output, close/review,
  cancel/absent-slot refusal, changed model/sidecar/image/bank/name/graphics/
  texture-bank/manifest refusal, and destination/input review invalidation.
  Exact unrelated records, identity rows, prior name bytes and asset payloads
  survive; Restore removes tracked outputs. Evidence: `creature-import-ui-ibb9tiok`.
  Four additional same-bytes model-directory and destination-root junction
  changes refuse at both sizes: `creature-import-ui-atqqqt8g`.
  Byte-identical runtime-copy probes pass four image/pack cases:
  `creature-import-ui-0mof94lx`. Existing clone save/cancel/pack/close neighbors
  pass eight cases: `creature-clone-ui-1crcpc8_`.
- Inspected compact 800x600 review screenshot: body import controls scroll and the
  Create/Cancel confirmation is visible. No game window or game install was changed.

Initial UI setup lacked a level fixture, so wait_maps correctly timed out. The
test now creates an owned tiny install with an unused LEV marker. The first mesh
name lacked the established MESH_ prefix; the UI now explains the naming rule.
An early destination test tried to click confirmation after it had correctly been
invalidated; corrected test checks direct-save refusal and no output creation.
An early texture assertion expected a bank copy without an image append; the
publisher correctly retains the source texture bank, so the assertion now checks
that only image imports create a texture output. No production guard was weakened.

## Immutable extracted checkpoint

`dist/development/20261023-creature-import-gui/FableForge-0.18.1-dev-win64.zip`
SHA256 `ea4390e9c89cab0956ec0e6e7e8fd8caa0e9b4acf18dd97a28e4cb0071cff469`.
Packaged at starting HEAD `6fc51e7` with scoped source changes dirty after owned
GUI/forge/forge-tools builds; final evidence/docs updates follow packaging without
replacing it. Archive checks pass 118 identities and 142 local documentation links.

The final `creature_import_ui` workflow passes all 44 cases against byte-identical
copies of the extracted executable/DLLs. The tiny synthetic schema is supplied
through an unrelated cwd; packaged retail schemas remain exact. New workflow is
registered in source/package gates. Supply `FABLEFORGE_CREATURE_FIXTURES` with the
passed retained donor packet; absent fixtures report SKIP, never exercised coverage.
Final receipt: `package-smoke-7x1gpz8v/result.json`; inner evidence:
`creature-import-ui-h3xpli7z`. The stripped GUI SHA256 is
`1dad94e0a3d9e9e411837f33f4f4a854cc2f97447b4c2ed723833e07d4835986`.

Same-archive neighbors pass retail eight-map smoke, GUI smoke, all scalar draft
cases and all 36 existing Creature clone GUI cases. Receipts:
`package-smoke-x9qirc34/result.json` and `package-smoke-ps17m3zt/progress.json`.
The initial selector mistakenly included nonexistent `definition_drafts`; both
real selected workflows passed before the final selection assertion. Corrected
custom-import invocation passes, including the four additional junction cases.
No failed test was hidden or guard weakened. Combined receipt:
`build/creature-gui-checkpoint-20261023.json` records five distinct passed workflows,
143 not run and two unrelated missing controller/Aeon and F2 corpus skips.
No full-suite, clean-machine, native filesystem custom-import or gameplay claim.

Implementation committed/pushed as `687fa30`; draft PR 2 has been rewritten around
the resulting authoring features and exact coverage. Windows CI run 39953743747
was still running at the follow-up check. No merge or release publication.

## Follow-up: pack model-list cache

A new `pack-preview` case primes Models with the donor before importing into a
pack, then selects the new mesh. It fails against `687fa30` because the cached
model directory still contains the donor only: `creature-import-ui-pyeea_ti`.
Pack completion now calls the existing `refreshModels` path when it does not
reload the global context. Folder output retains its context reload. Six affected
pack-preview/pack/folder cases pass at both sizes: `creature-import-ui-2_1bu4d8`.
The appended mesh is independently selected with three vertices and one triangle;
prior definition/asset preservation and Restore still pass. Owned GUI rebuild
passes. This correction is newer than the immutable checkpoint above; that ZIP
retains the known cached-list behavior and has not been silently replaced.

Next: check pack output with a separate save folder and imported-texture model
preview; the current pack fixture uses one folder for both. Preview texture
context and whole-creature assembly remain separate from filesystem acceptance.
Runtime/placement remain open; do not repeat the unchanged full native graph gate
or write full native banks merely to test the UI.

## F2Native coordination and offline preview preparation

Cache fix committed/pushed as `1da1ab3`; draft PR 2 records the regression and
newer-than-checkpoint limitation. Windows CI run 39954426240 now completed with
success, independently read back at 06:52 Edmonton. The original implementation
run was 39953743747; its outcome was not inferred from the successor. This CI
does not cover the newer uncommitted texture-context candidate.

F2Native thread `01a168a5-fbee-72f0-87fe-39450caddb64` requested a bounded hidden
capture window. Responded with `codex queue` using message
`01a168c3-eece-78f0-870f-b55c6088a0b1`, identifying the existing shared
`D:/Documents/FableTLC/.agents/runtime-lease.json` and its exclusive-create/token
helper in the texture checkout's `tools/ingame/runtime_lease.py`. It was absent at
the readback, and no owned Forge GUI/test process was active. An unrelated native
particle test was left alone. No games were stopped.

The peer then explicitly required no GPU/game tests until a queued release.
Acknowledged through queue message `01a168c5-7ab3-7550-86ee-c98d5055c42c`.
**This supersedes the earlier timed window. Do not infer release from the clock,
90-second estimate, 420-second watchdog, process disappearance or absent lease.**
Wait for the explicit message, then recheck the shared lease and active games.
Current local receipt: `build/runtime-coordination-20261023.json`. Overall Forge
development remains active; source/docs work continues offline.

Uncommitted preview work; GPU execution still deferred:

- Pack fixtures now use a separate save folder and require it to remain absent.
  `pack-image-preview` primes Models, imports an image into the pack, selects the
  appended mesh, and captures its viewport at both sizes.
- Renderer exposes a read-only count of model parts holding actual texture SRVs;
  `model_textured_parts` reports it to automation. The new case requires one
  textured part; untextured pack preview requires zero. This distinguishes
  drawable geometry from successfully bound diffuse imagery.
- Static tracing shows model preview uses `ctx_` for diffuse decoding while pack
  completion deliberately retains the install context. New pack texture ID 8
  cannot resolve in the tiny source bank containing only ID 7. This motivates
  the regression, but it is not a runtime failure claim yet. Keep global world
  context independent of undeployed pack contents when choosing the fix.

CPU-only diagnostic GUI build passes at below-normal priority/j2. Frozen before
the texture-context candidate at
`build/frozen/pack-preview-diagnostics-20261023/FableForge.exe`, SHA256
`40163fe36224f3f72f0ada0488135de95200b2bc9d63d538c568d1bbce2668cb`.
Its `provenance.json` pins the executable and exact diagnostic source files
beyond starting `1da1ab3`. It includes the SRV count but predates the subsequent
`asset_context_root` state key; account for this when interpreting negative-control
logs. No baseline GPU run has occurred during the hold.

Read-only Python audit of retained native-donor output independently confirms
mesh 4522 references texture 8; source bank has only 7 while pack bank has 7 and 8.
All three input hashes remain unchanged. Receipt:
`build/pack-preview-texture-source-audit-20261023.json`. No executable/GPU launch.

Candidate source now prepares a separate Context after successful pack publication
inside the existing import worker, choosing the appended bank or its exact source
bank when no image was added. Completion retains that context only for the resolved
physical graphics bank; Models uses it for diffuse lookup. The world `ctx_` stays
unchanged. Normal context reload clears the model-only context. A preview-load
failure remains separate from already-successful publication and blocks that
pack's texture preview with its own diagnostic. `asset_context_root` additionally
lets positive fixtures assert the original world root survives pack creation.
Candidate compiled successfully below-normal/j2 with output in
`build/pack-model-texture-context-build-20261023.log`; no runtime acceptance yet.
Frozen candidate:
`build/frozen/pack-preview-candidate-20261023/FableForge.exe`, SHA256
`3ac5c5f9cbcc4a4b06b472eea1638c48f17181e721264c8a7f854d5a25782d49`.
Its provenance records exact source hashes. Comparison receipt:
`build/pack-preview-frozen-comparison-20261023.json`. The negative-control driver
is a scratch copy of the GUI gate omitting only the newer world-context state key
unavailable in the frozen diagnostic; it retains the renderer texture assertion
and all source/output checks. The production positive gate keeps both assertions.
Both Python drivers compile; whitespace check passes.

After explicit release, recheck shared lease and game processes. Use the frozen
diagnostic GUI for the pack-image negative control, then the candidate GUI for
pack-preview/pack-image-preview and existing pack neighbors. Inspect the actual
viewport, require the SRV count and unchanged world context, and retain exact
delta/Restore/source preservation. Do not weaken the new SRV assertion or
silently switch the global world context to the undeployed pack.

Further offline driver preparation adds `pack-existing-preview`: a pack using
source texture ID 7 must bind one texture without creating a new textures.big.
The image-preview case also refreshes Models and reselects the imported mesh,
then switches back to the original install and requires the new pack mesh to
disappear from the model list while the original remains drawable. The world
context root must stay at the original install throughout. Driver receipts now
pin the driver's startup SHA256 and refuse if it changes during execution;
supplied donor packets must have a passed result receipt. These additions are
syntax/whitespace checked only; runtime acceptance remains pending. The earlier
frozen negative-control driver remains intentionally unchanged.

## Source acceptance after explicit F2Native release

The peer explicitly released its hold. Immediate process/lease checks found no
Fable/xenia/V83 process and an absent lease. A bounded supervisor acquired the
existing RuntimeLease protocol, and each hidden GUI child repeated lease-token,
live-owner and Fable/xenia/V83 process checks before launch. No user-owned process
was closed. Both binaries were the pinned frozen snapshots above.

Canonical successful window:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/pv-x1mr2v9q/result.json`.
The baseline creates/selects the imported mesh successfully and fails only
`model_textured_parts=0 != 1`; its four created outputs were removed with explicit
Restore. Inner receipt: `creature-import-ui-fi5ozchh`.
Candidate passes ten focused cases (`creature-import-ui-c1k034hm`) and the full
50-case gate (`creature-import-ui-bdonedle`). The focused cases are a subset,
not 60 distinct cases. Pack/source texture selection, actual SRV binding,
refresh, source switching and unchanged world context all pass at both sizes.
Exact definition/pool/identity/asset preservation and Restore remain intact;
native donor fixture inventory stays unchanged. Inspected compact 800x600
pack-image model viewport: the triangle is drawn with the red/green image's
filtered colour, and the appended model is selected.

All owned GUIs closed, and the supervisor released the lease with
`pv-x1mr2v9q/released-runtime-lease.json`. Immediate process readback was empty;
lease absence was verified. Peer availability queued as
`01a168de-3d41-7591-90f2-0b03237ec025`. New GUI windows must claim the lease again;
do not treat the peer's earlier release as permanent exclusive ownership.

Two earlier runner attempts are retained. A deeply nested build scratch root
refused recovery-marker preparation before publication; a shorter C evidence
root succeeds without any production guard changes. That failed attempt is not
the texture regression. A second attempt reached the intended texture failure,
but the runner accidentally included a traceback's quoted `FAIL ` literal in
its failure filter; fixed to match timestamped automation failure lines only.
Its created outputs were independently removed with Restore. Successful canonical
negative control now requires every automation failure to be the missing SRV.
Logs: `build/pack-preview-window-svdvrnhu` and
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/pv-5p1xve_x`.

Source texture-context correction is accepted. Next: commit the scoped change,
prepare a distinct immutable development checkpoint, then exercise its extracted
GUI under a new shared-lease window. The earlier ZIP remains untouched and still
has its documented cache/texture-preview limitations. No gameplay or assembled
creature claim follows from these per-mesh fixtures.


## Texture-context checkpoint prepared

Source correction committed and pushed as `65dba50d555f5d0204866a4ee80a526236642720`.
Windows CI run 39959326852 is in progress; draft PR #2 records the source gate.
New immutable package: `dist/development/20261023-pack-model-textures/FableForge-0.18.1-dev-win64.zip`,
SHA256 `2e5addece0de168c4f4a7dec4c079e1115548b1bae59d6adcf4d577d36e31413`.
It was prepared from the clean source commit with `package.py --no-check`;
this is packaging, not an extracted-runtime acceptance claim.
The old custom-import ZIP remains unchanged.

A fresh extracted-window preflight found unrelated native particle tests,
then definition-catalog tests. No lease was claimed or GUI launched, and those
processes were left alone. Peer notified through queue message
`01a168e4-5e19-7752-a3c3-549001b6c296`. The prepared bounded runner
`build/run_pack_package_window_20261023.py` will claim the shared lease only
when the immediate process guard is clear. Its scoped sitecustomize guard is
inherited by child Python drivers and verifies token/live owner/processes before
every FableForge launch. Planned extracted workflows: creature_import_ui, gui,
and retail. No unchanged core gate is repeated.


Offline extraction acceptance passes for the new ZIP: 118 file identities,
142 local documentation links, and the eight-map retail export smoke.
Receipt: `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/package-smoke-9mmhqrzx/result.json`.
This invocation ran one workflow, left 147 not run, and explicitly skipped two
unrelated missing-corpus workflows. The remaining GUI window will select only
`gui` and `creature_import_ui`; retail will not be repeated.


## Extracted texture-preview checkpoint acceptance

The new archive passes GUI smoke and all 50 custom-import cases at both sizes,
including actual texture SRVs, Refresh/source switching, unchanged global world
context, physical-alias refusals, exact preservation and Restore. Receipt:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/pk-1q8hw2fe/package-smoke-8r8legjw/result.json`;
inner custom gate `creature-import-ui-4pcwvqs3/result.json`. Extracted GUI SHA256
`697ad0d49496249d186d7c8ffe766f7a51764c11bb9bfeed73e32aa80b922ed0`.
Combined with offline retail: three distinct workflows passed, 145 not run,
two unrelated missing-corpus skips. Combined receipt:
`build/pack-texture-checkpoint-20261023.json`.

All owned GUIs closed; shared lease released and absence read back. Release
receipt `pk-1q8hw2fe/released-runtime-lease.json`; peer availability queued as
`01a168e9-6932-70e1-a36b-a986378c6a54`. An earlier attempt claimed/released
without launching because Python could not resolve PowerShell's codex wrapper;
the runner now uses the explicit codex.cmd path. No peer process was closed.

Screenshot inspection found a separate presentation issue: the pack's diffuse
texture visibly renders, but Materials still queries the base Textures list and
reports texture ID 8 missing. This does not invalidate the actual SRV gate, but
is a known UI limitation of this immutable archive. Next source correction should
show material identity from the same context used by the model preview and avoid
navigating into an unrelated base texture with a coincident ID. Keep this ZIP
unchanged; no whole-creature assembly, placement or gameplay claim.


## Materials identity follow-up prepared offline

The selected pack model now retains the exact preview texture context and uses
its textureInfo metadata for Materials. Pack/source-snapshot entries show their
verified name and dimensions read-only, avoiding navigation to an unrelated base
texture with the same numeric ID. Normal install material links keep existing
behavior. Selection and context reload clear the retained selection snapshot.
Focused driver assertions require the rendered metadata widget for image/existing
pack textures, retain it after Refresh, and require it absent after switching to
the original donor. GUI rebuild passes; source snapshot is pinned under
`build/frozen/model-material-source-20261023`, SHA256
`b340f5111a124839461ecb8aaf9ea139257a1df6f3a952e7b00ebb9ed1e545c8`.
This correction is not runtime accepted yet. F2Native owns opening-vulkan1 lease;
no GUI is launched while it exists. Peer readiness queued as
`01a168ec-585d-7173-9376-27b1895a339f`.


Materials follow-up diagnostics: the immutable checkpoint fails only the new
metadata-widget assertion after successful actual texture binding; four outputs
Restored. Negative receipt `mi-p_e_epfh/creature-import-ui-ubovo1ds` under the
20261023 C evidence root. Initial candidate shows correct `Diffuse 8: GUI_TEXTURE
(4 x 4)` in the screenshot, but a source-switch assertion mistakenly used the
cumulative widget registry. Explicit clear fixes that assertion. Next compact
attempts reveal a clipped Materials header: its children are not submitted by
ImGui while offscreen, so revealing a child cannot scroll its parent into view.
The final driver now reveals the registered Materials header first, then the
metadata row, clears the registry, and requires the row on the current screen.
The source-switch absence assertion also clears the registry. No metadata
assertion or production guard was removed. All failed created outputs explicitly
Restored; no owned GUI remains between windows. Retained attempts:
`mi-p_e_epfh`, `mi-tgljwu1q`, `mi-7tg_rdw9`.

Header-instrumented GUI rebuild passes. Frozen snapshot:
`build/frozen/model-material-source-header-20261023/FableForge.exe`, SHA256
`334ba3b7343e174f336e7ae872134d7d3e70fd5084afd2d1a45ac0434a39fed8`.
A new claim correctly refused an existing peer lease before launching any GUI;
waiting for a fresh clear window. Windows CI run 39959326852 for 65dba50 completed
success; this does not cover the newer local Materials correction.


## Materials identity source acceptance

Final focused candidate gate passes all ten cases (five modes at both sizes):
pack image/existing/no-texture preview plus folder image/save neighbors. Receipt:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/mi-pvwas_r7/creature-import-ui-5b27g5sg/result.json`.
Candidate executable/source hashes match the pinned header-instrumented snapshot.
Initial metadata, parent-first reveal after Refresh, current-screen metadata and
source-switch absence pass. Texture SRVs, original world context, exact
preservation and Restore still pass. Inspected full-size screenshot shows
`Diffuse 8: GUI_TEXTURE (4 x 4)` with the textured triangle. Compact initial
screenshot does not show the below-fold Materials panel; the refreshed compact
metadata is verified by the current-screen registry assertion after parent reveal.

All owned GUIs closed, lease released at 13:26:29 UTC, peer availability queued as
`01a168f1-0bc6-7733-8e94-f458a8c4e405`; receipt
`mi-pvwas_r7/released-runtime-lease.json`. Peer acquired a new token at 13:26:51;
a later blanket lease-absence assertion correctly failed. Verified different
owner/token and released own receipt, leaving peer lease untouched.
This is a newer source-only correction; retained pack-texture ZIP still has its
documented Materials limitation. No new package is required for this small
presentation follow-up. Next work is scratch placement/whole-creature presentation
planning using existing editor paths; gameplay and assembled-creature behavior
remain unverified. Do not repeat accepted full50/core/native gates without a
relevant change.


## Native window offered; offline placement investigation

Peer requested a short Native window after our extracted-package release. Offer
queued as `01a168f3-19a6-70f2-877d-b31ec0c5f828`: no owned Forge frontend remains,
no Forge GPU work pending for this offered window, continue shared-lease/immediate
process guards. No peer lease/process reclaimed or closed. Current Materials
source CI run 39961421194 is in progress (730ea70); source gate remains accepted.

Offline routing review found a concrete next candidate: placeDefinitionAt and
ownerFor use a CREATURE_ name prefix, while private Creature cloning explicitly
accepts generic ASCII names (the accepted GUI fixtures create GUI_ROOT). Context
already retains the compiled record's definition type and enumerates by that
identity. Before changing routing, prepare a scratch placement regression for a
non-prefixed CREATURE plus a misleading prefix on another type; verify TNG thing
class, owner and InitialPos/undo independently. This is source inspection only;
no placement/runtime acceptance claim and no additional GUI launch while peer
uses the offered window.


Native requested the next gap before any further Forge comparison for one D3D
real opening (~90 seconds; 420-second cold watchdog). Acknowledged by queue
`01a168f5-e210-7af3-99e5-6573601a0d07`: no Forge GUI/GPU launch until peer's
explicit release/notification, even if the shared lease temporarily disappears.
Current peer gpu-vulkan-off1 lease was observed and left untouched. This explicit
hold supersedes the earlier general available-window messages. Offline work may
continue; no owned Forge GUI was running at acknowledgment.


## Placement class routing prepared offline

New regression `tools/test_definition_placement_types_ui.py` writes only tiny
owned synthetic definitions, one texture and a valid loose LEV/TNG. It covers a
non-prefixed CREATURE (`GUI_ROOT`), misleading CREATURE_ object name, unprefixed
building, ordinary names, Auto and explicit owner, placement undo/redo, and the
real Actors Place button. Independent TNG checks require AICreature/Object/
Building classes, owner values and creature world-space InitialPos; Restore must
recover exact original inventory. `--prepare-only` launches no executable and
marks its receipt prepared_only=true, passed=false. Current prepared fixture:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/placement-types-_c_a4xmm`.
Six identity rows independently parse; this is preparation, not GUI acceptance.

Proposed source change exposes the immutable compiled class through the existing
Context and uses it in placement, Auto ownership and the Actors Place button.
The UI hint no longer implies that CREATURE_ is mandatory. It reuses existing
Document serializers and changes no binary/TNG writer. Below-normal -j2 GUI build
is in progress; no test frontend launched. Runtime acceptance must first compare
old pinned Materials GUI against the candidate after explicit Native release.
Do not claim whole-creature rendering/gameplay: this fixture deliberately uses
editor markers, and Document's separate VILLAGER name heuristic remains outside
this class-routing correction. Live-link spawning also retains its old prefix
check and has not been exercised.

Placement class-routing GUI build passed (including the final hint change).
Frozen candidate `build\frozen\definition-placement-types-20261023\FableForge.exe`, SHA256 `7d9915a9938a4e3a57c7635c8181c5542e45314c9ca4caab46eb2470611a9384`.
Provenance pins the three C++ sources and new driver. Prepared fixtures also
independently verify complete ordered schema tag anchors and owner tags at both
sizes. No GUI/runtime acceptance yet: Native explicit-release hold is active,
and the coordination receipt now clears the stale prior-release flag. Materials
source CI run 39961421194 completed success; placement candidate is newer.


Own queue was inspected read-only (`queue_1.sqlite`, mode=ro, this thread only).
Incoming `01a168f7-35cd-74b3-89ae-bd29b5ded43b` is a NEW explicit Native release:
D3D real opening passed 60.3 seconds/nine dialogue tags; frontend closed and lease
released; no need to hold indefinitely for the older request. Acknowledged by
`01a168ff-8574-71d3-80d6-6d0c7ac13314`. Coordination receipt clears the explicit
hold, but still requires a fresh exclusive claim and immediate process guards.

Prepared placement-window supervisor refused an existing peer gpu-vulkan-off4
lease before any GUI launch. Owner PID15076 is verified live as python.exe with
process start1792762928.3455365. Lease/token left untouched. Supervisor and pinned
baseline/candidate remain ready; wait for this actual process/window rather than
repeating build/tests or treating the prior release as exclusive ownership.


## Definition-class placement source acceptance

Baseline independently saves GUI_ROOT as Object/owner4, CREATURE_DECOY as
AICreature/owner0, and PLAIN_HOUSE as Object; Actors Place ignores GUI_ROOT.
Receipt `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/pt-evcljbni/baseline-independent-tng.json`;
baseline original TNG explicitly Restored. Candidate passes all eight placements
at both sizes (16 total), including Auto owners2/3, explicit owner1, ordinary names,
real Actors button, undo/redo counts and independent TNG class/InitialPos checks.
Canonical candidate receipt:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/pt-qkwy_fgp/placement-types-0dsrocc6/result.json`.
GUI hash matches frozen 7d9915a9938a4e3a57c7635c8181c5542e45314c9ca4caab46eb2470611a9384.
Driver pins startup GUI/driver hashes and refuses mutation during execution.

An initial candidate passed its complete full-size GUI/TNG gate but the oracle
wrongly expected Restore to remove an existing-file first-original backup. Current
CLI defaults to keepBackup; reviewed backups::restore confirms this contract.
Original files were all exact and only PlacementProbe.tng.forge-orig was added.
Final gate strictly requires original inventory plus exactly that byte-identical
first-original backup, and passes at both sizes. No recovery implementation or
production guard changed. Initial receipt `pt-evcljbni/placement-types-_ga9vvaa`.
All owned GUIs closed; released lease receipt `pt-qkwy_fgp/released-runtime-lease.json`,
peer availability queued `01a16901-caad-7e33-babc-5a23f50ec008`. No immediate further
GUI launch planned while source review/docs proceed.

Regression is registered as definition_placement_types in source and package
gates. No unchanged broad/core/import/native gate repeated. This corrects class
routing for custom-named definitions; marker fixtures do not prove custom body
assembly or live gameplay. Existing VILLAGER-name heuristic and live-link prefix
check remain separate, unexercised follow-ups.


## Combined checkpoint preparation

Source correction committed/pushed as 91d284e; CI run39964147797 is in progress.
Next distinct checkpoint destination is dist/development/20261023-creature-placement
(previous immutable archives stay untouched). Owned GUI/forge/forge-tools are
being rebuilt at below-normal priority/-j2. Placement package workflow now passes
an unrelated cwd and the archive's schema explicitly, avoiding an implicit repo
working-directory dependency. Source driver defaults are unchanged. Extracted
runtime acceptance for this combined checkpoint is still pending.


Combined checkpoint prepared from clean77eddec after GUI/forge/forge-tools build:
`dist/development/20261023-creature-placement/FableForge-0.18.1-dev-win64.zip`,
SHA256 `c49bf3555d9f423bcf21714110e61ae3d889b6da3d5696ca333ed546068735e8`.
Offline extraction gate passes118 identities,142 documentation links and the
retail eight-map smoke. Receipt
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/package-smoke-o1x_sutd/result.json`.
One workflow run,148 not run, two unrelated missing-corpus skips. New archive is
immutable; prior checkpoints remain unchanged. Extracted GUI/placement/import
workflows remain pending: immediate guard found an unrelated native definition
block-cache test, no lease claimed or GUI launched. Readiness queued as
`01a16907-ef2f-7f31-8013-866ebf1671b5`. Ready supervisor:
`build/run_creature_placement_package_window_20261023.py`.


## Combined immutable checkpoint acceptance

The new creature-placement archive passes all four selected extracted workflows:
retail eight-map smoke, GUI smoke,50 custom-import cases and16 marker placements
at both sizes. Pack diffuse SRVs/metadata, Refresh/source switching, stale-source
refusals, exact preservation/Restore and shipped-schema/unrelated-cwd placement
all pass.118 file identities and142 local documentation links pass.145 workflows
were not run; two unrelated missing controller/Aeon and F2 corpora remain explicit
skips. No full-suite, clean-machine, whole-creature geometry or gameplay claim.

Offline receipt: `package-smoke-o1x_sutd/result.json` under the 20261023 C evidence
root. GUI receipt: `cp-iynr178j/package-smoke-wai5ad7z/result.json`; inner import
`creature-import-ui-tdbm9g1m/result.json` and placement
`placement-types-b0seid45/result.json`. Both pin the same stripped GUI SHA256
`13e2f3949b331837e23d72c93e8b3b92c82b7f08793d5df065142f869454b542`.
Combined receipt: `build/creature-placement-checkpoint-20261023.json`.
Archive SHA256 remains
`c49bf3555d9f423bcf21714110e61ae3d889b6da3d5696ca333ed546068735e8`,
packaging checkout clean77eddec912a631be491f8c14b0f46ec94e06058d. Prior two
checkpoint ZIP hashes independently rechecked unchanged. No archive overwritten.

Inspected full-size pack model screenshot: textured triangle with Materials
`Diffuse 8: GUI_TEXTURE (4 x 4)`. Compact placement screenshot shows the enabled
`Place GUI_ROOT` button and the saved marker fixture; stacked fixture warnings
obscure much of the tiny viewport, so this is not a geometry presentation claim.
All owned GUIs closed; lease released with
`cp-iynr178j/released-runtime-lease.json`. Native availability queued as
`01a1690b-4ee9-72b0-9273-b90dbae6d55f`. Later peer claims remain independent and
must not be removed. No immediate further GUI launch planned.

Windows CI 39964147797 for 91d284e remains in progress, verified at the Windows
Build step (Unit tests/WARP/package still pending). Do not infer success from
local package acceptance or earlier green Materials CI. Next: finish draft-PR
checkpoint documentation, then investigate the remaining creature presentation
and placement-profile gaps with existing native evidence before new algorithms.


## Next profile correction recovered

Placement CI 39964147797 completed success. Read-only native investigation now
recovers a concrete VillageMember profile rule from runtime Components rather
than definition spelling; [profile evidence and next steps](HANDOFF_CREATURE_PROFILES_2026-10-23.md).
All 517 retail CREATURE records decode; 344 list CTCVillageMember, 173 do not.
The name heuristic differs on 215 definitions. 32 sampled retail placements match
runtime components exactly. Eight targeted debug function byte ranges match the
original PE; direct GetByteSize disassembly confirms nine-byte records and native
TransferBinaryIn verifies0x01ab6cce. No production writer changed, no GPU launched,
no native install bytes changed. The current immutable checkpoint remains exact.


Runtime village-membership source correction is now accepted; see
[profile handoff](HANDOFF_CREATURE_PROFILES_2026-10-23.md). Two affected unit
executables,517 native profile comparisons,16 placements/eight malformed-profile
refusals and the additional unknown-block preservation/undo assertion pass.
The immutable checkpoint is not replaced; it still uses the old membership
name heuristic. Latest own runtime lease released with pu-2qfi4vr0 receipt and
Native availability queued 01a16931-32c3-7081-9df8-5c8563fb6cfa.


A newer distinct runtime-profile checkpoint now passes its four extracted gates
(retail/GUI, 16 profile placements, eight malformed-profile refusals), 119 identities
and 145 docs links. See [profile acceptance](HANDOFF_CREATURE_PROFILES_2026-10-23.md).
This supersedes the previous source-only membership limitation; older archives
remain unchanged. No unrelated 50-case import rerun or live-game claim.
