# Releasing FableForge

The steps for a tagged build, in order. Everything before the tag is checked by a script;
the two in-game items are the human part.

1. Game closed (`Fable.exe` not running from the install: every writer and the suite's
   scratch tests refuse or flake while it is).
2. `python tools/check_all.py` -- ALL PASS (17 checks; the synthetic-click suites
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
5. Version: `CMakeLists.txt` `project(... VERSION x.y.z)` and `README.md`; the zip name and
   the GUI's title come from it.
6. `python tools/package.py` (runs the suite again unless `--no-check`) ->
   `dist/FableForge-<version>-win64.zip`: `FableForge.exe`, `forge.exe`, `forge-tools.exe`
   (all `-static`; imports are Windows system DLLs, the UCRT and `D3DCOMPILER_47`, so
   Windows 10/11 needs nothing installed), README, LICENSE, THIRD_PARTY, the user docs,
   the walkthrough, presets, `docs/re_reference` (forge-tools reads `def_schema.json`),
   `docs/modding`.
7. Unzip on a machine/VM without the repo, point it at a retail Steam install, run
   `docs/FIRST_LEVEL.md` end to end (the stranger's test, 1.0-rc #5). Every workaround is a
   bug to fix before tagging.
   *(Local dry run 2026-09-19: the 0.16.0 zip unpacked to a scratch folder found the Steam
   install by itself; `forge list/info/export --foliage --things`, `forge-tools defs list` and the
   GUI through `wait_foliage` all worked from there. The other-machine run is still owed.)*
8. `git tag v<version>` -> push with tags -> GitHub Actions green (the workflow builds with
   the same MinGW/CMake profile and runs the offline suite) -> attach the zip to the release.
9. Post: the Discord thread with the zip link, the walkthrough's first screenshot and the
   engine rules paragraph from `README.md`; note `forge restore` as the way back.
