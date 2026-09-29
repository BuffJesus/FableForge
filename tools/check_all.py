#!/usr/bin/env python3
"""Run every check: build, unit tests, retail CLI smoke, GUI suites.

  python tools/check_all.py [--no-build] [--count 8]
"""
import argparse, os, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

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
    ok &= run("lzo1x vs the engine's asm decoder", [sys.executable, "tools/verify_engine_lzo.py"], capture_output=True)
    ok &= run(f"retail smoke ({a.count} maps)", [sys.executable, "tools/retail_smoke.py", "--count", str(a.count)], capture_output=True)
    ok &= run("ui smoke", [sys.executable, "tools/ui_smoke.py"], capture_output=True)
    gui = os.path.join("build", "FableForge.exe")
    ok &= run("ui paths", [gui, "--auto", "tests/ui/paths.txt"])
    ok &= run("ui controls", [gui, "--auto", "tests/ui/controls.txt"])
    ok &= run("ui wheel", [gui, "--auto", "tests/ui/wheel.txt"])
    ok &= run("ui foliage", [gui, "--auto", "tests/ui/foliage.txt"])
    ok &= run("ui region export", [gui, "--auto", "tests/ui/region.txt"])
    ok &= run("ui editor", [gui, "--auto", "tests/ui/editor.txt"])
    ok &= run("ui placement", [gui, "--auto", "tests/ui/placement.txt"])
    ok &= run("ui budget", [gui, "--auto", "tests/ui/budget.txt"])
    ok &= run("ui brushes", [gui, "--auto", "tests/ui/brushes.txt"])
    ok &= run("ui owner + day/night", [gui, "--auto", "tests/ui/owner_daynight.txt"])
    ok &= run("ui height pens (vanilla Height Toolbox)", [gui, "--auto", "tests/ui/height_pens.txt"])
    ok &= run("ui scene browser switches (script-named only, nearest first)", [gui, "--auto", "tests/ui/scene_browser.txt"])
    ok &= run("ui track preview (vanilla Play Track)", [gui, "--auto", "tests/ui/track_preview.txt"])
    ok &= run("ui world view (2D ground tiles + 3D fly-over of every map)", [gui, "--auto", "tests/ui/world_view.txt"])
    ok &= run("ui no-install", [gui, "--auto", "tests/ui/noinstall.txt", "--install", "D:/definitely/not/fable"])
    ok &= run("new level from donor (scratch install)", [sys.executable, "tools/test_newlevel.py"], capture_output=True)
    ok &= run("overworld moves (scratch install + World tab)", [sys.executable, "tools/test_overworld.py"], capture_output=True)
    ok &= run("textures tab (scratch textures.big)", [sys.executable, "tools/test_textures.py"], capture_output=True)
    ok &= run("mod load order (scratch root; skips without the corpus)", [sys.executable, "tools/test_mods.py"], capture_output=True)
    ok &= run("backup manager (every suffix, stage revert + rebase)", [sys.executable, "tools/test_backups.py"], capture_output=True)
    ok &= run("GB pack under the UFP (Project Seasons; skips without the pack)", [sys.executable, "tools/test_gbpack.py"], capture_output=True)
    ok &= run("custom mesh import (.obj + .glb cube into scratch banks)", [sys.executable, "tools/test_meshimport.py"], capture_output=True)
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
