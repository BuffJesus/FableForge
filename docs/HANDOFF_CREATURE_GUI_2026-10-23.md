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
