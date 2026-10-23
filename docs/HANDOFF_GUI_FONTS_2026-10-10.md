# UI glyph fallback continuation, 2026-10-10

Owned development checkout/branch are unchanged from
[Unicode handoff](HANDOFF_GUI_UNICODE_2026-10-10.md), starting HEAD `2d8abff`.
That Unicode commit is pushed in draft PR 2 and its Windows CI run 38062819381
passes. The preceding clone/context head also passed Windows CI. RC2 remains
published unchanged; stable 0.18.0 remains latest. All prior archives are retained.

## Implemented

The compact Unicode screenshot had replacement glyphs for CJK and emoji despite
correct input/storage. `buildFonts` now locates Windows Fonts through the native
Windows-directory API rather than assuming C:/Windows. Segoe UI remains primary.
The first readable available CJK face among Microsoft YaHei, MS Gothic, Yu Gothic
and Malgun Gothic is merged; Segoe UI Emoji (or Symbol if absent) adds missing
glyphs. This applies to body/bold/title/small roles. Primary glyphs keep priority.

Font bytes are cached once per file in App-owned immutable buffers, shared among
all roles and repeated scale rebuilds. ImGui does not own these bytes; main.cpp
destroys the atlas before App, and rebuild clears the atlas while buffers persist.
Sources over 64 MiB, missing/unreadable files and unavailable Windows paths refuse
loading and retain existing default-font behavior. No font is downloaded, installed,
modified or bundled. The installed system fonts remain external dependencies.

Automation glyph assertions inspect source availability, not the replacement glyph.
A font-directory override is available only in active automation so missing-font
paths can be tested without touching Windows Fonts or normal user settings.

## Evidence

- GUI build passes. Initial full-font role/three-scale cases pass at both sizes:
  `font-fallback-ui-k9vti0e7`. Initial retail Unicode GUI checks with actual glyph
  assertions pass both sizes: `retail-definition-wide-ui-ueq2zd8h`. Native compiled
  hashes, all 14,764 identities/unrelated records, exact names and Restore pass.
- Final source checks include six system/no-font/primary-only cases at both sizes,
  all four roles and three scale passes per case: `font-fallback-ui-bgvjh5ba`.
  Required CJK and emoji fonts were present; zero skipped glyph groups. Default
  and primary-only fixtures explicitly assert missing CJK/emoji while retaining ASCII.
- Inspected compact native and synthetic drafts: 漢 and monochrome smiling/rocket
  glyphs are now visible. The previous replacement-glyph finding is historical.
- Python compilation and diff whitespace checks pass. The font workflow is
  registered in source and extracted-package gates. Core serializer/ABI behavior
  is unchanged from the validated Unicode checkpoint; no new full CTest run.

The extracted archive is retained at
`dist/development/20261010-ui-fonts/FableForge-0.18.1-dev-win64.zip`, SHA256
`581cc3201f1382c21bf4c0895b09ae0c8df16d6c4e2217f04d2bbc77517bfe3c`.
Packaging metadata records `2d8abff` with tracked changes. Exact evidence
`package-smoke-5jlsn3sm` passes 114 file identities, 134 local links and eight
selected workflows (retail, GUI smoke, all six font environments, text content,
all 42 wide cases, both retail wide cases, text close prompt and Unicode paths),
121 not run, zero skipped workflows. The font result separately records zero
skipped glyph groups on this machine, with YaHei and Segoe UI Emoji present.
Final notes are updated after packaging; the archive remains unchanged.

Coverage depends on available font files; fallback is not universal Unicode coverage,
complex-script shaping or colour emoji support. Missing optional glyph groups are
reported in the font test's result JSON. Real clean-machine walkthrough and gameplay
remain separate, unclaimed. Next: commit/push the scoped font continuation, then
the documented definition-reference authoring gap. Start with confirmed GroupDef
indices used by existing browser/group tests; inspect target-family constraints,
preserve unresolved originals on no-op, and refuse invalid changed targets rather
than treating every integer as a definition reference. No new wire-format inference.
