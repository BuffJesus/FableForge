#!/usr/bin/env python3
"""Run every check: build, unit tests, retail CLI smoke, GUI suites.

  python tools/check_all.py [--no-build] [--count 8]
"""
import argparse, json, os, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def retail_dialogue_big():
    root = os.environ.get("FABLE_TLC_ROOT", "")
    if not root:
        settings = os.path.join(os.environ.get("APPDATA", ""), "FableForge", "settings.json")
        try:
            with open(settings, encoding="utf-8") as f:
                root = json.load(f).get("install", "")
        except (OSError, ValueError, AttributeError):
            pass
    path = os.path.join(root, "data", "lang", "English", "dialogue.big") if root else ""
    return path if path and os.path.isfile(path) else ""

def run(name, cmd, **kw):
    t0 = time.time()
    r = subprocess.run(cmd, **kw)
    for output in (r.stdout, r.stderr):
        if output:
            text = output.decode(errors="replace") if isinstance(output, bytes) else output
            if r.returncode:
                print(text, end="" if text.endswith("\n") else "\n")
            else:
                for line in text.splitlines():
                    if "skip" in line.lower(): print(line)
    print(f"[{'PASS' if r.returncode == 0 else 'FAIL'}] {name} ({time.time() - t0:.1f}s)")
    return r.returncode == 0

def main():
    os.chdir(ROOT)  # the script's owning worktree, even when launched from another checkout
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--count", type=int, default=8)
    a = ap.parse_args()
    ok = True
    if not a.no_build:
        ok &= run("build", ["cmake", "--build", "build"], capture_output=True)
    ok &= run("CTest (core, rows, geometry, LZO)", ["ctest", "--test-dir", "build", "--output-on-failure"], capture_output=True)
    dialogue = retail_dialogue_big()
    if dialogue:
        install_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(dialogue))))
        ok &= run("retail lipsync byte roundtrip", [os.path.join("build", "fableforge_lipsync_tests.exe"), dialogue])
        ok &= run("retail dialogue LUT/lipsync join", [os.path.join("build", "fableforge_lut_tests.exe"), os.path.dirname(dialogue)])
        ok &= run("retail dialogue subtitle join", [os.path.join("build", "fableforge_dialoguetext_tests.exe"), install_root])
        graphics = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(dialogue))), "graphics", "graphics.big")
        if os.path.isfile(graphics):
            ok &= run("retail lip sync head presets", [os.path.join("build", "fableforge_lipsync_preset_tests.exe"), graphics])
            ok &= run("retail head skeletons", [os.path.join("build", "fableforge_meshskeleton_tests.exe"), graphics])
            ok &= run("retail animation bank and phoneme poses", [os.path.join("build", "fableforge_animation_tests.exe"), graphics, "--all"])
            ok &= run("retail head phoneme skin poses", [os.path.join("build", "fableforge_headpose_tests.exe"), graphics])
        ok &= run("retail dialogue frame edits, scratch export and pack recipe",
                  [sys.executable, "tools/test_dialogue_edit.py", "--install", install_root], capture_output=True)
    else:
        print("[SKIP] retail lipsync byte roundtrip (English dialogue.big unavailable)")
    ok &= run("lzo1x vs the engine's asm decoder", [sys.executable, "tools/verify_engine_lzo.py"], capture_output=True)
    ok &= run(f"retail smoke ({a.count} maps)", [sys.executable, "tools/retail_smoke.py", "--count", str(a.count)], capture_output=True)
    ok &= run("ui smoke", [sys.executable, "tools/ui_smoke.py"], capture_output=True)
    gui = os.path.join("build", "FableForge.exe")
    if dialogue:
        ok &= run("ui dialogue lip sync browser", [gui, "--auto", "tests/ui/dialogue_browser.txt"])
        ok &= run("ui dialogue head eyes", [gui, "--auto", "tests/ui/dialogue_head_eyes.txt"])
        ok &= run("ui dialogue head compact layouts", [sys.executable, "tools/test_dialogue_layout.py"], capture_output=True)
        ok &= run("Dialogue head pixels", [sys.executable, "tools/test_dialogue_head_pixels.py"])
        ok &= run("Dialogue posed head pixels", [sys.executable, "tools/test_dialogue_head_pose_pixels.py"])
        ok &= run("Dialogue retail eye defs", [sys.executable, "tools/test_dialogue_eye_defs.py",
                                                "--install", install_root], capture_output=True)
        effects = os.path.join(install_root, "data", "Misc", "pc", "effects.big")
        if os.path.isfile(effects):
            ok &= run("ui FX transport", [gui, "--auto", "tests/ui/effect_transport.txt"])
            ok &= run("ui FX grid", [gui, "--auto", "tests/ui/effect_grid.txt"])
            ok &= run("FX grid pixels", [sys.executable, "tools/test_effect_grid_pixels.py"])
            ok &= run("ui FX current framing", [gui, "--auto", "tests/ui/effect_frame_current.txt"])
            ok &= run("FX current framing pixels", [sys.executable, "tools/test_effect_frame_current_pixels.py"])
    ok &= run("ui paths", [gui, "--auto", "tests/ui/paths.txt"])
    ok &= run("ui controls", [gui, "--auto", "tests/ui/controls.txt"])
    ok &= run("ui wheel", [gui, "--auto", "tests/ui/wheel.txt"])
    ok &= run("ui foliage", [gui, "--auto", "tests/ui/foliage.txt"])
    ok &= run("ui region export", [gui, "--auto", "tests/ui/region.txt"])
    ok &= run("ui editor", [gui, "--auto", "tests/ui/editor.txt"])
    ok &= run("ui compact map and viewport layout", [gui, "--size", "800x600", "--auto", "tests/ui/compact_layout.txt"])
    ok &= run("ui thing keyboard transforms", [gui, "--auto", "tests/ui/thing_keyboard_transforms.txt"])
    ok &= run("ui selected radius rings", [gui, "--auto", "tests/ui/radius_rings.txt"])
    ok &= run("ui attachment picker", [gui, "--auto", "tests/ui/attach_picker.txt"])
    ok &= run("ui region entrance attachment", [gui, "--auto", "tests/ui/attach_entrance.txt"])
    ok &= run("ui creature sex definitions", [gui, "--auto", "tests/ui/creature_sex.txt"])
    ok &= run("ui placement", [gui, "--auto", "tests/ui/placement.txt"])
    ok &= run("ui budget", [gui, "--auto", "tests/ui/budget.txt"])
    ok &= run("ui brushes", [gui, "--auto", "tests/ui/brushes.txt"])
    ok &= run("ui area object delete", [gui, "--auto", "tests/ui/clip_delete.txt"])
    ok &= run("ui owner + day/night", [gui, "--auto", "tests/ui/owner_daynight.txt"])
    ok &= run("ui height pens (vanilla Height Toolbox)", [gui, "--auto", "tests/ui/height_pens.txt"])
    ok &= run("ui terrain follows placed things and foliage", [gui, "--auto", "tests/ui/terrain_follows_objects.txt"])
    ok &= run("ui floating tool section context", [gui, "--auto", "tests/ui/tool_window_context.txt"])
    ok &= run("ui scene browser switches (script-named only, nearest first)", [gui, "--auto", "tests/ui/scene_browser.txt"])
    ok &= run("ui track preview (vanilla Play Track)", [gui, "--auto", "tests/ui/track_preview.txt"])
    ok &= run("ui world view (2D ground tiles + 3D fly-over of every map)", [gui, "--auto", "tests/ui/world_view.txt"])
    ok &= run("ui no-install", [gui, "--auto", "tests/ui/noinstall.txt", "--install", "D:/definitely/not/fable"])
    ok &= run("new level from donor (scratch install)", [sys.executable, "tools/test_newlevel.py"], capture_output=True)
    ok &= run("overworld moves (scratch install + World tab)", [sys.executable, "tools/test_overworld.py"], capture_output=True)
    ok &= run("textures tab (scratch textures.big)", [sys.executable, "tools/test_textures.py"], capture_output=True)
    ok &= run("mod load order (scratch root; skips without the corpus)", [sys.executable, "tools/test_mods.py"], capture_output=True)
    ok &= run("Aeon Edition + Controller Support (scratch retail WAD; skips without the corpus)",
              [sys.executable, "tools/test_aeon_controller.py"], capture_output=True)
    ok &= run("Freeroam FMP walkability (76 LEVs through scratch WAD; skips without corpus)",
              [sys.executable, "tools/test_freeroam.py"], capture_output=True)
    ok &= run("mod asset health (new missing mesh versus retail, Mods report)",
              [sys.executable, "tools/test_mod_asset_health.py"], capture_output=True)
    ok &= run("backup manager (every suffix, stage revert + rebase)", [sys.executable, "tools/test_backups.py"], capture_output=True)
    ok &= run("Setup restore (scratch loose level, GUI reload)", [sys.executable, "tools/test_setup_restore_ui.py"], capture_output=True)
    ok &= run("Install switch (scratch roots, unsaved edit and invalid path)",
              [sys.executable, "tools/test_install_switch_ui.py"], capture_output=True)
    ok &= run("GB pack under the UFP (Project Seasons; skips without the pack)", [sys.executable, "tools/test_gbpack.py"], capture_output=True)
    ok &= run("overlapping GB packs under the UFP (Seasons + AlbionSecrets; skips without both)",
              [sys.executable, "tools/test_gbpack.py", "--pack", "both"], capture_output=True)
    ok &= run("Dragon Cliff Restored (root WAD levels and asset references; skips without the archive)",
              [sys.executable, "tools/test_dragoncliff.py"], capture_output=True)
    ok &= run("Lost Content (full WAD payloads and asset references; skips without the archive)",
              [sys.executable, "tools/test_lost_content.py"], capture_output=True)
    ok &= run("Expanded Chapters (content + graphics archives; skips without both)",
              [sys.executable, "tools/test_expanded_chapters.py"], capture_output=True)
    ok &= run("custom mesh import (.obj + .glb cube into scratch banks)", [sys.executable, "tools/test_meshimport.py"], capture_output=True)
    ok &= run("custom theme import (base/cliff textures, failure rollback)", [sys.executable, "tools/test_custom_theme.py"], capture_output=True)
    ok &= run("custom theme full-palette refusal (synthetic GUI fixture)", [sys.executable, "tools/test_custom_theme_ui.py"], capture_output=True)
    ok &= run("recipe packs (forge_pack.json models + themes composed at build, order swap, deploy/undeploy)", [sys.executable, "tools/test_recipe_packs.py"], capture_output=True)
    ok &= run("loose-level install (extracted levels, no FinalAlbion.wad)", [sys.executable, "tools/test_loose_install.py"], capture_output=True)
    ok &= run("tall terrain edit (grown patches re-laid, LOD patches re-sampled)", [sys.executable, "tools/test_tall_terrain.py"], capture_output=True)
    ok &= run("level edits into a mod pack (.lev/.tng + static-map chunk; mods build lays them in; install untouched)", [sys.executable, "tools/test_pack_levels.py"], capture_output=True)
    ok &= run("world edits in mod packs (captured shadows; per-record BWD/WLD merge equals the sequential edit; new level STB/WAD)", [sys.executable, "tools/test_pack_world.py"], capture_output=True)
    ok &= run("mods tab as a plugin list (masters set in the GUI and flagged, wins/loses badges, build reports masters)", [sys.executable, "tools/test_mods_masters.py"], capture_output=True)
    ok &= run("docs name real commands", [sys.executable, "tools/check_docs_commands.py"], capture_output=True)
    print("ALL PASS" if ok else "SOME CHECKS FAILED")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
