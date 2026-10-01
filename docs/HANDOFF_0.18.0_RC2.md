# 0.18.0-rc.2 handoff — 2026-09-30

Resume in `D:\Code\FableForge`. The release candidate is `v0.18.0-rc.2` and
the preview download is at
<https://github.com/BuffJesus/FableForge/releases/tag/v0.18.0-rc.2>.
It was published as a GitHub prerelease with the Windows zip and SHA-256 file.
Both Windows CI runs for the tagged commit `d0651f9` passed. The branch moves
back to the `-dev` version suffix after the tag; build the tag for release work.
Read [release notes](releases/0.18.0-rc.2.md),
[feature depth audit](FEATURE_DEPTH_AUDIT.md), and
[Aeon's feedback check](AEON_EDITOR_FEEDBACK.md) before adding editor features.

## Release evidence

- `cmake --build build -j 2` completed after the compact-layout and viewport
  control changes.
- `python tools/check_all.py --no-build` completed `ALL PASS`: 70 checks, including
  retail dialogue/head and effects, 8-map smoke, GUI workflows, scratch writes,
  Aeon/Controller/Freeroam paths, and available large content packs. Full local
  log: `build/release_rc2_check_all.log`.
- `tests/ui/readme_shots.txt`, `model_browser.txt` and
  `effect_background.txt` passed from this build. Screenshots were inspected;
  the README now leads with 3D World flight and links to the feature gallery.
- `python tools/package.py --no-check` wrote the 28.4 MB zip. The extracted
  package passed `forge list`, `forge info Greatwood_1`, `forge-tools wad list`,
  and a GUI script that selected and loaded Greatwood_1. README/gallery local
  links resolve inside the zip.
- The final-release fresh-game probes and separate-machine walkthrough remain
  open. These are stated in the prerelease notes, not counted as passed.

## First depth work tomorrow

1. Continue the [feature depth audit](FEATURE_DEPTH_AUDIT.md) with a complete
   user workflow per area: visible state, save result, undo/restore and game
   behavior. Prioritize theme paint/local detail, CTC components, Actors,
   navigation and object scale sensitivity from Aeon's feedback.
2. Visit a raised-ground map in game and compare planted things, baked foliage
   and terrain after both terrain and object writes. Restore the scratch install.
3. Check 3D World flyover at more level transitions and a true 4K display.
   Compare retail lighting, missing geometry and texture detail.
4. Review all popups and first-run failures at narrow sizes and high UI scale.
   Verify the gallery/screenshots whenever visible behavior changes.

Credit: [AeoN's EgoCore repository](https://github.com/eeeeeAeoN/EgoCore)
informed asset research and selected particle decoding/preview behavior; see
the [vendor scope](../vendor/VENDORED.md). Fable: Aeon Edition is Alexander
The Alright's [separate mod](https://www.nexusmods.com/fablethelostchapters/mods/454).
