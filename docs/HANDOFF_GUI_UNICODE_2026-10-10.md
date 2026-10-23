# GUI Unicode definition authoring, 2026-10-10

Owned checkout: `C:/Users/Cornelio/AppData/Local/FableForgeDevelopment/definition-clone-gui-20261010`,
branch `feat/definition-clone-gui-20261010`, baseline `a041e20`. The previous GUI
clone and partial-output texture-context work is committed/pushed in draft PR 2.
The release and separately owned dirty D lanes remain unchanged.

## Implementation

`CWideString` GUI drafts use the existing strict UTF-8/native NUL-terminated
UTF-16LE converter. Complete valid original strings fitting 127 UTF-8 bytes can
be edited; ASCII and Unicode exact no-ops preserve all bytes and create no backup.
Malformed surrogates, odd lengths, missing/embedded terminators, trailers and
overlong originals remain read-only. C0/C1 controls are refused. Clone names and
narrow strings keep their existing ASCII contract. Only the selected field changes;
all other payloads, identities and the name pool remain exact. Existing transactional
publication, stale-source/destination, running-game and Restore guards are reused.

ImGui and every linked consumer now share `IMGUI_USE_WCHAR32` so supplementary
characters survive input. GUI renderer tests were rebuilt for that ABI. Draft
initialization now explicitly refuses unsupported values instead of ignoring a
failed setter and retaining an earlier buffer. No core wire-format change.

The native layout is unchanged: FableWin TransferBinaryOut `01895C50` calls
WriteNullTerminatedWString `030830D0`; `(GetLength+1)*2` bytes, without a count.
Canonical native/retail evidence is recorded in PARITY_BACKLOG/ROADMAP's
2026-10-09 wide-string correction. UI field 3 of entry 5983 is TextValue in retail.

## Evidence so far

- Before implementation, the new Unicode raw-byte expectation fails against the
  previous GUI: `definition-wide-ui-qddetteh`.
- Rebuilt GUI, ImGui and both renderer tests. Built the new definition-draft unit
  target and existing Unicode/definition-decode targets.
- Five targeted CTests pass: definition drafts, Unicode, definition decode,
  geometry and picking. Independent expected bytes and malformed/control originals
  are covered. The complete 55-target inventory was not run locally.
- Eight focused GUI cases pass at both sizes, including Unicode, supplementary,
  unchanged Unicode originals and the 127-byte UTF-8 boundary:
  `definition-wide-ui-o6m1ouyb`.
- All 42 expanded wide GUI cases pass: `definition-wide-ui-cu0x_s23`.
- All 24 narrow ASCII GUI cases pass: `definition-string-ui-uvuwofa5`.
- The new repeatable `test_definition_wide_retail.py` passes both sizes at
  `retail-definition-wide-ui-qk3gnb0x`. It reads the native install, writes only a
  new output, independently compares all 14,764 identities and every unrelated
  payload, exact table/name bytes and UTF-16 field delta, then explicit-output
  Restore removes the created pair/markers. Native compiled-bank hashes stay exact.
- The compact draft screenshot was inspected. Accented Latin/Greek render;
  CJK/emoji show replacement glyphs. Their input and saved bytes are correct.
  Wider font coverage remains a separate presentation limitation, not a passed claim.

The final C1-control tightening was rebuilt; the five targeted CTests pass again
and both invalid GUI cases pass at `definition-wide-ui-iuv25izf`. Python compilation
and diff whitespace checks pass. The new retail workflow is registered in source
and package gates. The archive is retained at
`dist/development/20261010-def-gui-unicode/FableForge-0.18.1-dev-win64.zip`, SHA256
`ed71aa9bf5ee94ad2d155b24de6fdc71c7af4e457531a4950ec92c53294cd74a`.
Its metadata records baseline `a041e20` with tracked changes at packaging time.
Extracted acceptance passes at `package-smoke-h67gpth1`: 113 file identities,
132 local links and nine selected workflows (retail, GUI smoke, text content,
all 44 clone cases, all 34 scalar cases, all 42 wide cases, both retail wide cases,
all 24 narrow cases and text close prompt), 119 not run, zero skips.
The packaged no-install check separately passes with exit code zero and RESULT PASS
using private profiles; its screenshot is `build/ui/n1_noinstall.png`. The first
PowerShell launch returned before its GUI log existed; a hidden synchronous launch
verified process completion. Calling gh in that same temporary APPDATA could not
find its normal credentials; only that process environment changed, no login/settings.
The updated editor help is in the ZIP; final acceptance/remaining-scope notes are
recorded after packaging without altering the tested bytes.

The previous head `a041e20` passes Windows CI run 38061993142; the Unicode source
still needs its own pushed-head CI. Next: scoped commit/push, then broader font fallback.
Keep prior archives intact; do not alter published RC2. Broader font fallback
is the next observed presentation issue. Continue Forge work until asked to stop.
