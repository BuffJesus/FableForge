#!/usr/bin/env python3
"""New-level-from-donor check on a scratch copy of the install's four world
containers (never the real install): the CLI clones a small retail map, the
list/export paths see it, and the GUI's card installs a second copy and opens
it. Needs the Fable install (skips cleanly without one).

  python tools/test_newlevel.py [--root <fable-root>] [--keep]
"""
import argparse, os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_INSTALL = r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters"
CONTAINERS = ["FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion.wad", "FinalAlbion_RT.stb"]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.environ.get("FABLE_ROOT", DEFAULT_INSTALL))
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--donor", default="TeleporterGreatwood")
    a = ap.parse_args()
    levels = os.path.join(a.root, "data", "Levels")
    if not all(os.path.exists(os.path.join(levels, c)) for c in CONTAINERS):
        print("newlevel test skipped (no install)")
        return 0
    scratch = os.path.join(ROOT, "build", "ui_newlevel_install")
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(os.path.join(scratch, "data", "Levels"))
    os.makedirs(os.path.join(scratch, "data", "CompiledDefs"))
    t0 = time.time()
    for c in CONTAINERS:
        shutil.copyfile(os.path.join(levels, c), os.path.join(scratch, "data", "Levels", c))
    for f in ("game.bin", "names.bin"):
        src = os.path.join(a.root, "data", "CompiledDefs", f)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(scratch, "data", "CompiledDefs", f))
    # textures.big is only needed for the textured preview; the GUI copes without it
    print(f"scratch install copied in {time.time() - t0:.1f}s")

    cli = os.path.join(ROOT, "build", "AlbionAtlas.exe")
    gui = os.path.join(ROOT, "build", "AlbionAtlasGUI.exe")
    ok = True

    r = subprocess.run([cli, "new-level", a.donor, "AtlasCliCopy", "--install", scratch], capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode != 0 or "installed: map slot" not in r.stdout:
        print("CLI new-level failed:", r.stderr); ok = False
    if "chunk re-baked" not in r.stdout:
        print("expected a re-baked chunk"); ok = False
    r = subprocess.run([cli, "list", "--install", scratch], capture_output=True, text=True)
    if "AtlasCliCopy" not in r.stdout:
        print("new level missing from `list`"); ok = False
    r = subprocess.run([cli, "export", "AtlasCliCopy", "--install", scratch, "--out", os.path.join(scratch, "copy.glb"), "--things", "--quiet"], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(os.path.join(scratch, "copy.glb")):
        print("export of the new level failed:", r.stderr); ok = False
    # a second install with the same name must be refused and leave no temp files
    r = subprocess.run([cli, "new-level", a.donor, "AtlasCliCopy", "--install", scratch], capture_output=True, text=True)
    if r.returncode == 0 or "already" not in (r.stderr + r.stdout):
        print("duplicate name was not refused"); ok = False
    leftovers = [f for f in os.listdir(os.path.join(scratch, "data", "Levels")) if "forge-tmp" in f]
    if leftovers:
        print("temp files left behind:", leftovers); ok = False
    for c in CONTAINERS:
        if not os.path.exists(os.path.join(scratch, "data", "Levels", c + ".atlas-orig")):
            print("missing backup for", c); ok = False

    # blank level from scratch (no donor geometry): CLI, by theme name
    r = subprocess.run([cli, "blank-level", "AtlasCliBlank", "--install", scratch, "--theme", "GROUND_FOREST_LEAVES", "--height", "12"], capture_output=True, text=True)
    print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr)
    if r.returncode != 0 or "authored from scratch" not in r.stdout or "navigation:" not in r.stdout:
        print("CLI blank-level failed:", r.stderr); ok = False
    r = subprocess.run([cli, "layers", "AtlasCliBlank", "--install", scratch], capture_output=True, text=True)
    if "16 patches" not in r.stdout or "0 differ" not in r.stdout:
        print("blank level layers do not match its LEV:", r.stdout[:400]); ok = False
    # a retail-sized one (128x224, the template is picked automatically) and a non-power-of-two one
    for name, size, patches in (("AtlasCliBig", "128x224", "112 patches"), ("AtlasCliOdd", "96x96", "36 patches")):
        r = subprocess.run([cli, "blank-level", name, "--install", scratch, "--size", size, "--height", "20"], capture_output=True, text=True)
        if r.returncode != 0 or "authored from scratch" not in r.stdout:
            print(f"CLI blank-level {size} failed:", r.stderr, r.stdout[-300:]); ok = False; continue
        r = subprocess.run([cli, "layers", name, "--install", scratch], capture_output=True, text=True)
        if patches not in r.stdout or "0 differ" not in r.stdout:
            print(f"{size} blank level layers do not match its LEV:", r.stdout[:400]); ok = False

    # GUI: the card installs a second copy (suggested origin) and opens it
    script = os.path.join(ROOT, "build", "ui_newlevel.txt")
    with open(script, "w") as f:
        f.write("\n".join([
            "wait_maps", "wait_ready", f"select {a.donor}", "wait_loaded",
            "edit 1", "frames 2", "assert_widget btn_new_level",
            "screenshot build/ui/n1_new_level_card.png",
            "new_level AtlasGuiCopy 16 0 Greatwood",   # off the 32-unit grid -> refused, logged
            "wait_new_level", "frames 2",
            "dump_log",
            "quit",
        ]) + "\n")
    r = subprocess.run([gui, "--auto", script, "--install", scratch], capture_output=True, text=True)
    log = open(script + ".log", encoding="utf-8", errors="replace").read() if os.path.exists(script + ".log") else ""
    if "new level failed" not in log or "grid" not in log:
        print("GUI: the off-grid origin should have been refused with a log line"); print(log[-1500:]); ok = False
    # now a valid one: let the card's suggestion pick the origin (name/x/y from the CLI's donor info)
    r2 = subprocess.run([cli, "new-level", a.donor, "AtlasProbe", "--install", scratch, "--no-rebake"], capture_output=True, text=True)
    sug = None
    for line in r2.stdout.splitlines():
        if line.startswith("new level AtlasProbe at ("):
            sug = line.split("(")[1].split(")")[0]
    if not sug:
        print("could not read the suggested origin"); ok = False
    else:
        # AtlasProbe now occupies that spot; the GUI copy goes one slot further right
        x, y = (int(v) for v in sug.split(","))
        with open(script, "w") as f:
            f.write("\n".join([
                "wait_maps", "wait_ready", f"select {a.donor}", "wait_loaded",
                "edit 1", "frames 2",
                f"new_level AtlasGuiCopy {x + 128} {y} Greatwood",
                "wait_new_level", "frames 3", "wait_loaded",
                "assert_state selected AtlasGuiCopy",
                "screenshot build/ui/n2_new_level_opened.png",
                "dump_log", "quit",
            ]) + "\n")
        r = subprocess.run([gui, "--auto", script, "--install", scratch], capture_output=True, text=True)
        log = open(script + ".log", encoding="utf-8", errors="replace").read() if os.path.exists(script + ".log") else ""
        if r.returncode != 0 or "RESULT PASS" not in log:
            print("GUI new-level run failed"); print(log[-2500:]); ok = False
        if "installed (map slot" not in log:
            print("GUI: no install confirmation in the log"); ok = False
        # GUI blank level (theme slot 3 = GROUND_FOREST_LEAVES in TeleporterGreatwood's palette), two slots further right
        with open(script, "w") as f:
            f.write("\n".join([
                "wait_maps", "wait_ready", f"select {a.donor}", "wait_loaded",
                "edit 1", "frames 2",
                "new_level_blank 3 15",
                f"new_level AtlasGuiBlank {x + 256} {y} Greatwood",
                "wait_new_level", "frames 3", "wait_loaded",
                "assert_state selected AtlasGuiBlank",
                "screenshot build/ui/n3_blank_level_opened.png",
                "dump_log", "quit",
            ]) + "\n")
        r = subprocess.run([gui, "--auto", script, "--install", scratch], capture_output=True, text=True)
        log = open(script + ".log", encoding="utf-8", errors="replace").read() if os.path.exists(script + ".log") else ""
        if r.returncode != 0 or "RESULT PASS" not in log or "authored from scratch" not in log:
            print("GUI blank-level run failed"); print(log[-2500:]); ok = False
    if not a.keep:
        shutil.rmtree(scratch, ignore_errors=True)
    print("newlevel test", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
