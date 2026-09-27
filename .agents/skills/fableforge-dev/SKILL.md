---
name: fableforge-dev
description: Build and validate FableForge C++20, forgecore, GUI and game-install changes using the active worktree and targeted scratch-install checks. Excludes native VC7.1 engine rebuilds and release publishing.
---

# Preflight

- Resolve `git rev-parse --show-toplevel`; set every command's working directory explicitly.
- Inspect branch, worktrees and scoped dirty paths before writing. Count large status results first.
- Read root/nested AGENTS instructions and the current handoff; do not borrow another lane's build.
- Consult [roadmap](../../../docs/ROADMAP_1.0.md) for support and known limitations before diagnosing paths.
- Check for an existing skill owner or uncommitted skill edits; amend once in this lane, not across siblings.
- The canonical core is `libs/forgecore`; reuse its abstractions before adding parallel readers/writers.

# Build and iterate

1. Search symbols/file names first, then read relevant ranges. Batch independent lookups.
2. Inspect `build/CMakeCache.txt`: source root, compiler and Ninja must belong to this checkout.
3. For a new build, use the [README build instructions](../../../README.md):

   ```powershell
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

4. For iteration, build the affected target, e.g. `cmake --build build --target forge fableforge_tests`.
5. Run the smallest check that could falsify the change; broaden when it passes.
6. Core checks: `ctest --test-dir build --output-on-failure`.
7. Read actual script arguments before invoking a check. Useful existing checks:
   - `python tools/test_overworld.py`: world moves, scratch containers and World tab.
   - `python tools/test_tall_terrain.py`: oversized terrain patches and distant LOD.
   - `python tools/test_loose_install.py`: extracted levels, renamed WAD.
   - `python tools/retail_smoke.py --count 8`: selected retail map export checks.
8. UI changes use the relevant `tests/ui/*.txt` through `build/FableForge.exe --auto`.
9. Broad gate, when warranted: `python tools/check_all.py --no-build` after building all targets.
   Inspect its current checks first; skip messages are not successful exercised coverage.
   Its captured failing output may require rerunning only the failing child command.

# Install boundaries

- Read [engine rules](../../../docs/ENGINE_RULES.md) and the [supported layouts](../../../README.md).
- Destructive checks use real copies under this checkout's `build`, never hardlinks to game files.
- Verify resolved scratch paths before scripts delete/recreate them; inspect script root resolution.
- Pass the install root explicitly where supported, or use the script's documented `FABLE_ROOT`.
- Keep live game checks separate from offline tests; record exactly what was exercised.
- Preserve the target-install running-game guard and backup behavior in `src/backups`.

# Project traps and stop condition

- Never hard-code the FableTLC checkout as the root of a FableForge script.
- PowerShell quoting and Git Bash path conversion differ; use native argument arrays where possible.
- Do not delete `index.lock` until its owner is known; do not reset another lane's changes.
- Respect `.gitattributes`; avoid incidental whole-file CRLF conversions and Windows `CON` filenames.
- VC7.1/MSYS compiler switches belong to the native lane's tooling docs, not this C++20 build.
- Change strategy after two materially identical failures; preserve the first useful diagnostic.
- Reuse unchanged findings and logs. Stop repeating gates once the acceptance bar is met.
- Record changed behavior, tests, skips and residual risks in the lane handoff.
