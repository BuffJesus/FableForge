#!/usr/bin/env python3
"""Loose-level install check: the modders' layout (levels extracted to
data/Levels/FinalAlbion, FinalAlbion.wad renamed to _FinalAlbion.wad so the game
reads the loose files) must be accepted, listed, edited and extended WITHOUT a
FinalAlbion.wad ever being (re)created. Runs on a scratch copy (never the real
install); needs the Fable install (skips cleanly without one).

  python tools/test_loose_install.py [--root <fable-root>] [--keep]
"""
import argparse, hashlib, os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_INSTALL = r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters"
CONTAINERS = ["FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion_RT.stb", "FinalAlbion.gtg"]


def digest(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.environ.get("FABLE_ROOT", DEFAULT_INSTALL))
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--donor", default="TeleporterGreatwood")
    a = ap.parse_args()
    levels = os.path.join(a.root, "data", "Levels")
    if not all(os.path.exists(os.path.join(levels, c)) for c in CONTAINERS + ["FinalAlbion.wad"]):
        print("loose-install test skipped (no install)")
        return 0
    cli = os.path.join(ROOT, "build", "forge.exe")
    tools = os.path.join(ROOT, "build", "forge-tools.exe")
    gui = os.path.join(ROOT, "build", "FableForge.exe")

    scratch = os.path.join(ROOT, "build", "ui_loose_install")
    shutil.rmtree(scratch, ignore_errors=True)
    slevels = os.path.join(scratch, "data", "Levels")
    os.makedirs(slevels)
    os.makedirs(os.path.join(scratch, "data", "CompiledDefs"))
    t0 = time.time()
    for c in CONTAINERS:
        shutil.copyfile(os.path.join(levels, c), os.path.join(slevels, c))
    for f in ("game.bin", "names.bin"):
        shutil.copyfile(os.path.join(a.root, "data", "CompiledDefs", f), os.path.join(scratch, "data", "CompiledDefs", f))
    # the modders' layout: every level extracted loose, the WAD renamed out of the way
    r = subprocess.run([tools, "wad", "extract", os.path.join(levels, "FinalAlbion.wad"), scratch], capture_output=True, text=True)
    if r.returncode != 0:
        print("wad extract failed:", r.stdout[-400:], r.stderr[-400:]); return 1
    shutil.copyfile(os.path.join(levels, "FinalAlbion.wad"), os.path.join(slevels, "_FinalAlbion.wad"))
    renamed_before = digest(os.path.join(slevels, "_FinalAlbion.wad"))
    loose_dir = os.path.join(slevels, "FinalAlbion")
    n_lev = sum(1 for f in os.listdir(loose_dir) if f.lower().endswith(".lev"))
    print(f"scratch loose install: {n_lev} loose levels in {time.time() - t0:.1f}s")
    wad = os.path.join(slevels, "FinalAlbion.wad")
    ok = n_lev > 0

    # CLI: list comes from the loose folder, export resolves a loose map
    r = subprocess.run([cli, "list", "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0 or f"{n_lev} maps in" not in r.stdout or "FinalAlbion.wad" in r.stdout.splitlines()[0]:
        print("CLI list on a loose install:", r.stdout[:300], r.stderr); ok = False
    r = subprocess.run([cli, "export", a.donor, "--install", scratch, "--out", os.path.join(scratch, "donor.glb"), "--things", "--quiet"], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(os.path.join(scratch, "donor.glb")):
        print("CLI export on a loose install:", r.stderr); ok = False

    # CLI: a new level lands as loose .lev/.tng; no WAD appears
    r = subprocess.run([cli, "new-level", a.donor, "LooseCliCopy", "--install", scratch], capture_output=True, text=True)
    print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip())
    if r.returncode != 0 or "installed: map slot" not in r.stdout:
        print("CLI new-level on a loose install failed:", r.stdout[-600:], r.stderr); ok = False
    for ext in (".lev", ".tng"):
        p = os.path.join(loose_dir, "LooseCliCopy" + ext)
        if not os.path.exists(p):
            print("new level: missing loose", p); ok = False
        elif open(p, "rb").read() != open(os.path.join(loose_dir, a.donor + ext), "rb").read():
            print("new level: loose", ext, "is not the donor's bytes"); ok = False
    r = subprocess.run([cli, "list", "--install", scratch], capture_output=True, text=True)
    if "LooseCliCopy" not in r.stdout:
        print("new level missing from `list`"); ok = False
    r = subprocess.run([cli, "new-level", a.donor, "LooseCliCopy", "--install", scratch], capture_output=True, text=True)
    if r.returncode == 0 or "already" not in (r.stderr + r.stdout):
        print("duplicate name was not refused on a loose install"); ok = False
    leftovers = [f for d in (slevels, loose_dir) for f in os.listdir(d) if "forge-tmp" in f]
    if leftovers:
        print("temp files left behind:", leftovers); ok = False

    # GUI: accepted, lists the loose maps, deploy writes the loose .tng (with a backup)
    tng = os.path.join(loose_dir, a.donor + ".tng")
    script = os.path.join(ROOT, "build", "ui_loose_install.txt")
    with open(script, "w") as f:
        f.write("\n".join([
            "wait_maps",
            "assert_state install_valid 1",
            "assert_state levels_loose 1",
            f"assert_state maps {n_lev + 1}",
            f"select {a.donor}", "wait_loaded",
            "edit 1", "frames 2",
            "deploy_level",
            "dump_log", "quit",
        ]) + "\n")
    r = subprocess.run([gui, "--auto", script, "--install", scratch], capture_output=True, text=True)
    log = open(script + ".log", encoding="utf-8", errors="replace").read() if os.path.exists(script + ".log") else ""
    if r.returncode != 0 or "RESULT PASS" not in log or "loose-level install" not in log:
        print("GUI on a loose install:"); print(log[-1500:]); ok = False
    if not any(os.path.exists(tng + s) for s in (".forge-orig", ".atlas-orig")):
        print("GUI deploy: no one-time backup of the loose .tng"); ok = False

    # GUI: picking the Data folder steps up to the install
    with open(script, "w") as f:
        f.write("\n".join(["wait_maps", "assert_state install_valid 1", "dump_log", "quit"]) + "\n")
    r = subprocess.run([gui, "--auto", script, "--install", os.path.join(scratch, "data")], capture_output=True, text=True)
    log = open(script + ".log", encoding="utf-8", errors="replace").read() if os.path.exists(script + ".log") else ""
    if r.returncode != 0 or "RESULT PASS" not in log or "Data folder" not in log:
        print("GUI with the Data folder picked:"); print(log[-1000:]); ok = False

    # the invariant: nothing recreated FinalAlbion.wad, the renamed one is untouched
    if os.path.exists(wad):
        print("FinalAlbion.wad was created on a loose install"); ok = False
    if digest(os.path.join(slevels, "_FinalAlbion.wad")) != renamed_before:
        print("_FinalAlbion.wad was modified"); ok = False

    if not a.keep:
        shutil.rmtree(scratch, ignore_errors=True)
    print("loose install:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
