# Releasing FableForge

The steps for a tagged build, in order. Everything before the tag is checked by a script;
the two in-game items are the human part.

1. Game closed (`Fable.exe` not running from the install: every writer and the suite's
   scratch tests refuse or flake while it is).
2. `python tools/check_all.py` -- ALL PASS (16 checks; the synthetic-click suites
   `ui paths` / `ui foliage` occasionally miss a click under the full run: rerun that one
   alone before calling it a failure).
3. In-game probes on the release build, from a **fresh** New Game where the item needs one
   (`docs/ENGINE_RULES.md`): a new level reached through its region entrance from the map
   screen; a retextured barrel; a placed preset; a placed particle emitter; a placed
   fishing spot; one session on a compacted bank (`forge compact-stb`). Then
   `forge restore` and `forge backups` -> 0 differ.
4. Version: `CMakeLists.txt` `project(... VERSION x.y.z)` and `README.md`; the zip name and
   the GUI's title come from it.
5. `python tools/package.py` (runs the suite again unless `--no-check`) ->
   `dist/FableForge-<version>-win64.zip`: `FableForge.exe`, `forge.exe`, `forge-tools.exe`
   (all `-static`; imports are Windows system DLLs, the UCRT and `D3DCOMPILER_47`, so
   Windows 10/11 needs nothing installed), README, LICENSE, THIRD_PARTY, the user docs,
   the walkthrough, presets, `docs/re_reference` (forge-tools reads `def_schema.json`),
   `docs/modding`.
6. Unzip on a machine/VM without the repo, point it at a retail Steam install, run
   `docs/FIRST_LEVEL.md` end to end (the stranger's test, 1.0-rc #5). Every workaround is a
   bug to fix before tagging.
   *(Local dry run 2026-09-19: the 0.16.0 zip unpacked to a scratch folder found the Steam
   install by itself; `forge list/info/export --foliage --things`, `forge-tools defs list` and the
   GUI through `wait_foliage` all worked from there. The other-machine run is still owed.)*
7. `git tag v<version>` -> push with tags -> GitHub Actions green (the workflow builds with
   the same MinGW/CMake profile and runs the offline suite) -> attach the zip to the release.
8. Post: the Discord thread with the zip link, the walkthrough's first screenshot and the
   engine rules paragraph from `README.md`; note `forge restore` as the way back.
