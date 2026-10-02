# Releasing FableForge

The steps for a tagged build, in order. Everything before the tag is checked by a script;
the two in-game items are the human part.

## Release candidates

A candidate may be published as a GitHub **prerelease** after building, running the
available offline/retail scratch checks, inspecting screenshots and smoke-testing
the extracted package. Use an explicit suffix such as `-rc.1` in
`FORGE_VERSION_SUFFIX`; packaging reads both version and suffix from CMake.
Record failed/skipped checks and deferred in-game/other-machine validation in the
candidate's notes. Do not mark it latest/final or claim the deferred checks passed.
The final release still requires the checklist below. GitHub publication does not
authorize posting to Discord; do that only when separately requested.

For unattended local checks, set `FABLEFORGE_AUTOMATION_HIDDEN=1` in the test
process environment. Scripted editor children remain hidden; ordinary launches
are unaffected. Builds should use a bounded job count, e.g. `cmake --build build -j 2`.

Keep local development packages separate from published assets with
`python tools/package.py --no-check --output-dir dist/development` after the
relevant checks pass. Both the application and guide ZIPs use that destination;
`--guide-only` accepts it too. The default remains `dist` for release tooling.

## Final release checklist

1. Game closed (`Fable.exe` not running from the install: every writer and the suite's
   scratch tests refuse or flake while it is).
2. `python tools/check_all.py` -- ALL PASS (CTest plus CLI, GUI and scratch-install checks; the synthetic-click suites
   `ui paths` / `ui foliage` occasionally miss a click under the full run: rerun that one
   alone before calling it a failure).
3. In-game probes on the release build, from a **fresh** New Game where the item needs one
   (`docs/ENGINE_RULES.md`): a new level reached through its region entrance from the map
   screen; a retextured barrel; a placed preset; a placed particle emitter; a placed
   fishing spot; one session on a compacted bank (`forge compact-stb`). Then
   `forge restore` and `forge backups` -> 0 differ.
   `python tools/ingame/release_probes.py` does all of it unattended (stages `things`,
   `compact`, `region`, `restore`; `--dry-run` prints the commands): it deploys with the
   built exes, runs the retail game on a fresh profile through the harness, and ends with the
   restore + 0-differ check. Two items stay human: the barrel's tint and the preset are judged
   from `build/ingame/release/things/04_after_probe.png`, and the map-screen click into the
   own-region level (the driver proves the region load through ForgeFSE's retail transition).
4. README screenshots: `build\FableForge.exe --auto tests\ui\readme_shots.txt --size 1600x900`
   rewrites the three `docs/screenshot_*.png` from the current build (nothing is written to
   the install; the save root is `build/readme_install`). Look at them.
5. Version: `CMakeLists.txt` `project(... VERSION x.y.z)` and `README.md`; the zip name and Clear `FORGE_VERSION_SUFFIX` ("-dev") in CMakeLists.txt for the release build, and set it back after tagging.
   the GUI's title come from it.
6. `python tools/package.py` (runs the suite again unless `--no-check`) ->
   `dist/FableForge-<version>-win64.zip`: `FableForge.exe`, `forge.exe`, `forge-tools.exe`
   (all `-static`; imports are Windows system DLLs, the UCRT and `D3DCOMPILER_47`, so
   Windows 10/11 needs nothing installed), README, LICENSE, THIRD_PARTY, the user docs,
   the walkthrough, presets, `docs/re_reference` (forge-tools reads `def_schema.json`),
   `docs/modding`.
7. Run `python tools/test_package.py dist/FableForge-<version>-win64.zip --root <retail-install>`
   for the local extracted-package check. It validates linked documentation, retail
   exports, GUI pixels and mod commands from an unrelated working directory, retaining
   evidence under `build/package-smoke-*`. Then unzip on a machine/VM without the repo, point it at a retail Steam install, run
   `docs/FIRST_LEVEL.md` end to end (the stranger's test, 1.0-rc #5). Every workaround is a
   bug to fix before tagging.
   *(Local dry run 2026-09-19: the 0.16.0 zip unpacked to a scratch folder found the Steam
   install by itself; `forge list/info/export --foliage --things`, `forge-tools defs list` and the
   GUI through `wait_foliage` all worked from there. The other-machine run is still owed.)*
8. `git tag v<version>` -> push with tags -> GitHub Actions green (the workflow builds with
   the same MinGW/CMake profile and runs the offline suite) -> attach the zip to the release.
9. Post: the Discord thread with the zip link, the walkthrough's first screenshot and the
   engine rules paragraph from `README.md`; note `forge restore` as the way back.
