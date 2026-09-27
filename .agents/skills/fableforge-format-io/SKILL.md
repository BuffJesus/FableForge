---
name: fableforge-format-io
description: Investigate or change FableForge BIG, BIN, STB, WAD, LEV, TNG, WLD, BWD or GTG I/O with format evidence, byte-preservation contracts and stock or loose-level validation. Excludes unrelated UI styling and speculative format rewrites.
---

# Preflight and evidence routing

- Resolve the active worktree and read its AGENTS instructions and scoped dirty state.
- Start with [engine rules](../../../docs/ENGINE_RULES.md) and [support status](../../../docs/ROADMAP_1.0.md).
- Search `libs/forgecore/include/forge` and `libs/forgecore/src` for the current contract.
- Follow callers in `src`, `src/cli` and `tools/forge-cli`; GTG also lives in `src/gtg`.
- Locate the separate FableTLC evidence checkout explicitly; do not assume the process cwd is it.
- Its `docs/formats/LEVEL_CONTAINER_INDEX.md` routes level containers; `LEV_WRITER.md`
  covers LEV, `DEFS.md` covers definitions, and `MESH.md` covers graphics/physics payloads.
- Search that format directory by extension/name for BIG, BIN, STB, WAD, TNG, WLD and BWD.
- Consult `docs/re_reference/def_schema.json` locally for compiled-definition field structure.
- Follow existing EgoCore references in the implementation and `vendor/VENDORED.md`;
  locate the referenced source before relying on it. Do not assume all upstream copies agree.
- For consequential unknowns, inspect native/debug evidence using the editor RE procedure.

# Workflow

1. State the intended changed fields and the untouched-byte contract before changing a writer.
2. Identify units, offsets, alignment, counts, compression framing and cross-container references.
3. Read one representative fixture and a relevant boundary case; avoid whole-bank dumps.
4. Use existing `forge-tools` diagnostics; inspect help/source for the command actually available.
5. Reuse `levelstore::detect` / `requireFile` and existing write routing for level access.
6. Preserve unknown fields, trailing data, ordering and compression metadata where the contract requires it.
7. Distinguish semantic equivalence from byte-identical no-op serialization in assertions and reports.
8. Reject malformed/truncated/overflowing input before writes; keep backups and game-running guards.
9. For a writer, parse the result independently where possible and verify the intended field delta.
10. Validate related references: a world move can touch WLD, BWD copies, STB origins and neighbour bakes.

# Stock and loose levels

- Stock levels are served from `data/Levels/FinalAlbion.wad`.
- Supported extracted installs use `data/Levels/FinalAlbion/*.lev` / `*.tng` with the WAD renamed away.
- Do not recreate `FinalAlbion.wad` in a loose install: it would override the edited loose files.
- Source LEV heights and baked STB render meshes are different payloads; updating one is not proof of both.
- LEV walkability bytes alone do not establish runtime navigation; inspect the serialized nav tree.
- Validate edits on scratch copies in the owning checkout, including renamed-WAD preservation.

# Validation and stop condition

- Run a small round-trip/delta or malformed-input test appropriate to the changed contract first.
- Reuse fixtures and reports already available when their input hashes and implementation are unchanged.
- Build/test the affected C++ target, then the corresponding scratch-install script.
- Use `tools/test_loose_install.py` for level routing; broaden to retail smoke only when warranted.
- A script reporting a missing-corpus skip leaves that case unverified.
- Keep addresses, observations and uncertainty in canonical format docs or the lane findings report.
- Do not duplicate format manuals or create one skill per extension.
- Stop when the field change, preservation contract and relevant layouts are validated;
  explicitly retain unresolved format questions rather than guessing a writer layout.
