# GUI definition clone continuation, 2026-10-10

Owned development checkout:
`C:/Users/Cornelio/AppData/Local/FableForgeDevelopment/definition-clone-gui-20261010`,
branch `feat/definition-clone-gui-20261010`, starting commit `c3f87a7`.
Its 971 tracked baseline files were compared byte-for-byte with the clean release
checkout: zero differences. The slow partial checkout from D was stopped by
terminating only its owned process tree; tracked files and the index were copied
from the clean C release checkout. Git subsequently confirmed a clean baseline.
The separate dirty D lane and its dialogue-audio test remain untouched.

## Implemented

Assets > Definitions now offers Clone selected definition, a new-name draft and
Create confirmation. It uses the existing exact donor API and paired publisher;
all prior records and pool offsets remain intact. References retain their original
targets. Unknown/opaque payloads can be cloned without inspector decoding.
Game/frontend/script banks, direct writes, save folders and valid packs are supported.
Whole source bank/name bytes are captured and checked for stale drafts. Changed
destinations and incompatible existing outputs refuse. Existing Close/Install
protection uses the shared definition draft state. New records are selected by
explicit index after saving; game-bank contexts are refreshed.

## Verification completed before the development-version rebuild

- First isolated build of GUI, both CLIs and clone/writer tests passed.
- Core clone and BIN writer CTests pass (two targets).
- New GUI cases: 38 at desktop/compact sizes, across 19 modes. Initial save/Restore
  evidence: `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261010/definition-clone-ui-ndvtht_0`
  (four cases). Remaining modes: `definition-clone-ui-3cazxxz9` (34 cases).
- Existing scalar draft suite: 34 cases pass; evidence `definition-edit-ui-z8kpwk0s`.
- Actual retail read-only inputs with new GUI controls: all 14,764 records and raw
  identity rows preserved, exact donor/name and ordinal appended to owned output,
  created bank/name files removed by explicit-root CLI Restore, native compiled-bank
  hashes unchanged. Evidence: `retail-definition-clone-k8nu1mql`. Its created-record
  screenshot was inspected. The output has no texture bank; context warnings after
  the successful copy are expected and do not claim a full asset export.
- `definition_clone_ui` is registered in source and extracted-package gates.
  `test_definition_clone_retail.py --gui` makes the retail UI proof repeatable.

The first feature binaries inherited rc.2's version suffix. The source now uses
`-dev` to distinguish this unpublished continuation. Its isolated rebuild completed;
a subsequent Ninja check reports no work. CLI documentation was regenerated.
The development archive is `dist/development/20261010-def-clone/FableForge-0.18.1-dev-win64.zip`,
SHA256 `73fb4dae506f4a387bc26bcf64e464a1276c29253e77f0aae6ad6cb92c5a9349`.
Its packaging metadata correctly records baseline `c3f87a7` with tracked changes;
it is a development checkpoint, not a clean-commit release. Extracted acceptance
passes under `package-smoke-9hds0ft1`: 112 file identities, 132 local documentation
links and seven selected workflows (retail, GUI smoke, CLI clone, GUI clone,
existing-name picker, scalar drafts and text close prompt); 120 not run, zero skips.
The packaged GUI also passes the independent 14,764-record retail preservation
and Restore check at `retail-definition-clone-ulonnzlv`, with native hashes unchanged.
The first invocation used the extraction parent rather than its versioned folder
and failed before launching an executable; the corrected explicit path passed.
Both core targets pass again after the version rebuild; Python compilation and
`git diff --check` pass. No full suite or live-game test was repeated.
No GUI-clone GitHub publication has occurred yet.

## RC2 release work in parallel

PR 1 merged at `f96692dbb6d69b1f3daef6014a092bbcd72a190a` after final-head CI
38059467407 passed. Remote tag `v0.18.1-rc.2` points to that merge. Tag CI
38060259144 and main CI 38060238810 both passed, including tagged packaging.
The unchanged RC2 archive remains in the separate release checkout, SHA256
`d488737601d00b054bd27d5d0052d1c9994ba248a6136a35e3d5c69afd0c5e91`.
It passed 111 file identities, 131 links and 57 selected extracted workflows;
66 not run, two optional corpus skips. It is now published as a
[GitHub prerelease](https://github.com/BuffJesus/FableForge/releases/tag/v0.18.1-rc.2).
GitHub's application asset digest matches the validated hash exactly; its size is
36,896,482 bytes. The illustrated guide is also attached, SHA256
`bcdbdb7b33ae616c765bc00437cb3a42d9fe2311e2b28e45f9547c996f211721`.
Stable 0.18.0 remains latest; rc.1 is retained. The release checkout stays clean
at `c3f87a7`. The new GUI-clone development build is not part of that release.

## Next

Commit the scoped GUI continuation, then choose the next roadmap task. The
development rebuild, compact confirmation inspection and exact extracted archive
acceptance are complete. RC2 publication is completed; keep its validated archive
unchanged and separate from the new development checkpoint.
Continue roadmap work until the user asks to stop. Live gameplay and a clean-machine
walkthrough remain separate; no native install write or checkpoint deletion is needed.
