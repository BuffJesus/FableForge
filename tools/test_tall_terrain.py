#!/usr/bin/env python3
"""Tall terrain edits bake: a hill sculpted where the donor's fixed patch slots
cannot hold it (GuildExterior; failed with "vertices do not fit the donor CRange
span" before 0.17.2) is written with the grown patches re-laid, and every
background patch -- including the simplified distant-view LOD ones -- carries
the new heights (`forge-tools stb patch-heights`). Scratch copy only; needs the
Fable install (skips cleanly without one).

  python tools/test_tall_terrain.py [--root <fable-root>] [--keep]
"""
import argparse, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_INSTALL = r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters"
CONTAINERS = ["FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion.wad", "FinalAlbion_RT.stb", "FinalAlbion.gtg"]
MAP, WX, WY = "GuildExterior", 3360, 3456


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.environ.get("FABLE_ROOT", DEFAULT_INSTALL))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    levels = os.path.join(a.root, "data", "Levels")
    if not all(os.path.exists(os.path.join(levels, c)) for c in CONTAINERS):
        print("tall-terrain test skipped (no install)")
        return 0
    gui = os.path.join(ROOT, "build", "FableForge.exe")
    tools = os.path.join(ROOT, "build", "forge-tools.exe")
    cli = os.path.join(ROOT, "build", "forge.exe")
    scratch = os.path.join(ROOT, "build", "ui_tall_terrain_install")
    shutil.rmtree(scratch, ignore_errors=True)
    slevels = os.path.join(scratch, "data", "Levels")
    os.makedirs(slevels)
    os.makedirs(os.path.join(scratch, "data", "CompiledDefs"))
    for c in CONTAINERS:
        shutil.copyfile(os.path.join(levels, c), os.path.join(slevels, c))
    for f in ("game.bin", "names.bin"):
        shutil.copyfile(os.path.join(a.root, "data", "CompiledDefs", f), os.path.join(scratch, "data", "CompiledDefs", f))
    ok = True

    script = os.path.join(ROOT, "build", "ui_tall_terrain.txt")
    with open(script, "w") as f:
        f.write("\n".join([
            "wait_maps", "wait_ready", f"select {MAP}", "wait_loaded", "edit 1", "frames 2",
            "terrain_mode 0", "brush 6 4", "terrain_stroke 80 112 1", "frames 2",
            "deploy_terrain", "wait_terrain", "frames 1",
            "assert_state terrain_dirty 0",
            "assert_log re-baked terrain chunk",
            "assert_log outgrew their slot",
            "assert_log distant-view LOD patch",
            "dump_log", "quit",
        ]) + "\n")
    r = subprocess.run([gui, "--auto", script, "--install", scratch], capture_output=True, text=True)
    log = open(script + ".log", encoding="utf-8", errors="replace").read() if os.path.exists(script + ".log") else ""
    if r.returncode != 0 or "RESULT PASS" not in log:
        print("GUI tall terrain deploy:"); print(log[-1500:]); ok = False

    out = os.path.join(scratch, "extract")
    subprocess.run([tools, "stb", "extract", os.path.join(slevels, "FinalAlbion_RT.stb"), out, MAP], capture_output=True, text=True)
    chunk = os.path.join(out, "Data", "Levels", "FinalAlbion", MAP + ".lev")
    lev = os.path.join(slevels, "FinalAlbion", MAP + ".lev")
    if not (os.path.exists(chunk) and os.path.exists(lev)):
        print("no chunk / loose lev after the deploy"); ok = False
    else:
        r = subprocess.run([tools, "stb", "patch-heights", chunk, lev, str(WX), str(WY)], capture_output=True, text=True)
        print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip())
        if r.returncode != 0 or " 0 off the LEV" not in r.stdout:
            print("patches do not carry the edited heights:", r.stdout[-800:]); ok = False
    r = subprocess.run([cli, "chunk-audit", MAP, "--install", scratch], capture_output=True, text=True)
    if "0 with findings" not in r.stdout:
        print("chunk audit:", r.stdout[-800:], r.stderr[-400:]); ok = False

    if not a.keep:
        shutil.rmtree(scratch, ignore_errors=True)
    print("tall terrain:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
