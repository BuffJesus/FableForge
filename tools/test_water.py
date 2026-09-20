#!/usr/bin/env python3
"""The water writer, offline: paint a lake theme on a dry retail map through the scripted GUI,
write the terrain into a scratch copy of the containers, then check the chunk with the CLI:
`forge water-audit` must find our CWaterPatchMesh frames and reproduce every record from the
LEV (our own formulas, so all of them), and the chunk must still parse (`forge chunk-audit`).

  python tools/test_water.py [--root <fable install>] [--keep]
"""
import argparse, os, re, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTAINERS = ["FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion.wad", "FinalAlbion_RT.stb", "FinalAlbion.gtg"]
MAP = "BanditCampPath_1"


def find_root(explicit: str) -> str:
    if explicit:
        return explicit
    for c in [r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters",
              r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters"]:
        if os.path.exists(os.path.join(c, "data", "Levels", "FinalAlbion.wad")):
            return c
    return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    root = find_root(a.root)
    if not root:
        print("water test skipped (no install)"); return 0
    os.chdir(ROOT)
    scratch = os.path.join(ROOT, "build", "ui_water_install")
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(os.path.join(scratch, "data", "Levels"))
    os.makedirs(os.path.join(scratch, "data", "CompiledDefs"))
    t0 = time.time()
    for c in CONTAINERS:
        shutil.copyfile(os.path.join(root, "data", "Levels", c), os.path.join(scratch, "data", "Levels", c))
    for f in ["game.bin", "names.bin"]:
        shutil.copyfile(os.path.join(root, "data", "CompiledDefs", f), os.path.join(scratch, "data", "CompiledDefs", f))
    print(f"scratch install copied in {time.time() - t0:.1f}s")
    cli = os.path.join(ROOT, "build", "forge.exe")
    gui = os.path.join(ROOT, "build", "FableForge.exe")
    ok = True
    # before: the map has no water at all
    r = subprocess.run([cli, "water-audit", MAP, "--install", scratch], capture_output=True, text=True)
    if "no water" not in r.stdout:
        print("expected a dry map before the paint:", r.stdout[-300:]); ok = False
    r = subprocess.run([gui, "--auto", "tests/ui/water.txt"], capture_output=True, text=True, cwd=ROOT)
    log = os.path.join(ROOT, "tests", "ui", "water.txt.log")
    text = open(log, encoding="utf-8", errors="ignore").read() if os.path.exists(log) else ""
    if r.returncode != 0 or "RESULT PASS" not in text:
        print("GUI water script failed:"); print(text[-2500:]); ok = False
    else:
        print("GUI water script PASS")
    # after: our frames are there, the records come back from our own formulas, the chunk parses
    r = subprocess.run([cli, "water-audit", MAP, "--install", scratch], capture_output=True, text=True)
    print(r.stdout.strip())
    m = re.search(r"(\d+) water patches", r.stdout)
    if not m or int(m.group(1)) == 0:
        print("no water patches after the deploy"); ok = False
    w = re.search(r"writer: (\d+)/(\d+) patches produced .* all (\d+) of (\d+)", r.stdout)
    if not w or w.group(1) != w.group(2) or w.group(3) != w.group(4):
        print("the writer does not reproduce its own patches"); ok = False
    c = re.search(r"native re-encode byte-exact (\d+)/(\d+)", r.stdout)
    if not c or c.group(1) != c.group(2):
        print("our blocks must re-encode byte-exact (they are ours)"); ok = False
    r = subprocess.run([cli, "chunk-audit", MAP, "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0:
        print("chunk-audit failed:", r.stdout[-600:], r.stderr[-300:]); ok = False
    else:
        print("chunk-audit OK:", r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "")
    r = subprocess.run([cli, "layers", MAP, "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0 or "patches" not in r.stdout:
        print("layers parse failed after the water deploy:", r.stdout[-300:]); ok = False
    if not a.keep:
        shutil.rmtree(scratch, ignore_errors=True)
    print("water test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
