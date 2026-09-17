#!/usr/bin/env python3
"""Overworld (map relocation) check on a scratch copy of the install's world
containers (never the real install): `world` lists a consistent layout, a bad
move is refused, a good move rewrites WLD + BWD (all copies) + the STB info
block and chunk, touching neighbours are re-baked, and moving back restores
the WLD/BWD byte-for-byte. Needs the Fable install (skips cleanly without one).

  python tools/test_overworld.py [--root <fable-root>] [--keep]
"""
import argparse, os, re, shutil, struct, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_INSTALL = r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters"
CONTAINERS = ["FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion.wad", "FinalAlbion_RT.stb"]


def wld_pos(path, stem):
    t = open(path, encoding="latin1").read()
    m = re.search(r"MapX\s+(-?\d+);\s*MapY\s+(-?\d+);\s*LevelName\s+\"FinalAlbion\\" + re.escape(stem) + r"\.lev\"", t)
    return (int(m.group(1)), int(m.group(2))) if m else None


def bwd_box(path, stem):
    d = open(path, "rb").read(); o = 0
    def s():
        nonlocal o
        n = struct.unpack_from("<I", d, o)[0]; o += 4; v = d[o:o + n].decode("latin1"); o += n; return v
    cnt = struct.unpack_from("<I", d, o)[0]; o += 4
    for _ in range(cnt - 1):
        ln = s(); sn = s(); o += 3
        l, r, t, b = struct.unpack_from("<iiii", d, o); o += 16; o += 1 + 8
        if sn.lower() == stem.lower():
            return (l, t, r, b)
    return None


def stb_origin(cli, scratch, stem):
    r = subprocess.run([cli, "world", "--install", scratch], capture_output=True, text=True)
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) > 4 and parts[1].lower() == stem.lower():
            return line
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.environ.get("FABLE_ROOT", DEFAULT_INSTALL))
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--map", default="TeleporterGreatwood")   # 64x64, owned by Greatwood, has neighbours
    a = ap.parse_args()
    levels = os.path.join(a.root, "data", "Levels")
    if not all(os.path.exists(os.path.join(levels, c)) for c in CONTAINERS):
        print("overworld test skipped (no install)")
        return 0
    scratch = os.path.join(ROOT, "build", "ui_overworld_install")
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(os.path.join(scratch, "data", "Levels", "FinalAlbion"))
    os.makedirs(os.path.join(scratch, "data", "CompiledDefs"))
    t0 = time.time()
    for c in CONTAINERS:
        shutil.copyfile(os.path.join(levels, c), os.path.join(scratch, "data", "Levels", c))
    # the engine reads the BWD from three places; Atlas keeps them in step
    shutil.copyfile(os.path.join(levels, "FinalAlbion.bwd"), os.path.join(scratch, "FinalAlbion.bwd"))
    shutil.copyfile(os.path.join(levels, "FinalAlbion.bwd"), os.path.join(scratch, "data", "Levels", "FinalAlbion", "FinalAlbion.bwd"))
    for f in ("game.bin", "names.bin"):
        src = os.path.join(a.root, "data", "CompiledDefs", f)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(scratch, "data", "CompiledDefs", f))
    print(f"scratch install copied in {time.time() - t0:.1f}s")
    cli = os.path.join(ROOT, "build", "AlbionAtlas.exe")
    sl = os.path.join(scratch, "data", "Levels")
    wld, bwd = os.path.join(sl, "FinalAlbion.wld"), os.path.join(sl, "FinalAlbion.bwd")
    orig = {c: open(os.path.join(sl, c), "rb").read() for c in ("FinalAlbion.wld", "FinalAlbion.bwd")}
    ok = True

    r = subprocess.run([cli, "world", "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0 or "399 maps" not in r.stdout:
        print("world listing failed:", r.stderr, r.stdout[:200]); ok = False
    if "baked at" in r.stdout:
        print("retail layout reports baked origins that disagree with the placement"); ok = False
    before = wld_pos(wld, a.map); box = bwd_box(bwd, a.map)
    print(f"{a.map}: wld {before}, bwd box {box}")
    if before is None or box is None or (box[0], box[1]) != before:
        print("WLD/BWD disagree before the move"); ok = False

    # refused: overlap with a neighbour, misaligned origin
    r = subprocess.run([cli, "world-move", a.map, str(before[0] + 32), str(before[1]), "--install", scratch], capture_output=True, text=True)
    if r.returncode == 0 or "overlaps" not in r.stderr:
        print("overlapping move was not refused:", r.stdout, r.stderr); ok = False
    r = subprocess.run([cli, "world-move", a.map, "1000", "1000", "--install", scratch], capture_output=True, text=True)
    if r.returncode == 0 or "32-aligned" not in r.stderr:
        print("misaligned move was not refused:", r.stdout, r.stderr); ok = False
    r = subprocess.run([cli, "world-move", a.map, "1024", "9024", "--install", scratch], capture_output=True, text=True)
    if r.returncode == 0 or "world grid" not in r.stderr:
        print("out-of-grid move was not refused:", r.stdout, r.stderr); ok = False
    if open(wld, "rb").read() != orig["FinalAlbion.wld"]:
        print("a refused move touched the WLD"); ok = False

    # a real move to a free spot (far below the retail world)
    nx, ny = 2048, 8064
    t0 = time.time()
    r = subprocess.run([cli, "world-move", a.map, str(nx), str(ny), "--install", scratch], capture_output=True, text=True)
    print(r.stdout.strip()); print(f"move took {time.time() - t0:.1f}s")
    if r.returncode != 0:
        print("move failed:", r.stderr); ok = False
    if "neighbour " not in r.stdout:
        print("expected touching neighbours to be re-baked"); ok = False
    if wld_pos(wld, a.map) != (nx, ny):
        print("WLD placement not updated:", wld_pos(wld, a.map)); ok = False
    nb = bwd_box(bwd, a.map)
    if nb != (nx, ny, nx + box[2] - box[0], ny + box[3] - box[1]):
        print("BWD box not updated:", nb); ok = False
    for mirror in (os.path.join(scratch, "FinalAlbion.bwd"), os.path.join(sl, "FinalAlbion", "FinalAlbion.bwd")):
        if open(mirror, "rb").read() != open(bwd, "rb").read():
            print("BWD mirror not in step:", mirror); ok = False
    line = stb_origin(cli, scratch, a.map)
    print(line)
    if line is None or "baked at" in line or f" {nx} " not in line:
        print("STB info block origin not updated"); ok = False
    for c in CONTAINERS:
        if not os.path.exists(os.path.join(sl, c + ".atlas-orig")):
            print("missing backup", c); ok = False
    # the export path still reads the moved map at its new place
    r = subprocess.run([cli, "export", a.map, "--install", scratch, "--out", os.path.join(scratch, "moved.glb"), "--no-textures", "--world", "--quiet"], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(os.path.join(scratch, "moved.glb")):
        print("export of the moved map failed:", r.stderr); ok = False

    # move back: WLD + BWD byte-identical to retail
    r = subprocess.run([cli, "world-move", a.map, str(before[0]), str(before[1]), "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0:
        print("move back failed:", r.stderr); ok = False
    if open(wld, "rb").read() != orig["FinalAlbion.wld"]:
        print("WLD not byte-identical after moving back"); ok = False
    if open(bwd, "rb").read() != orig["FinalAlbion.bwd"]:
        print("BWD not byte-identical after moving back"); ok = False
    line = stb_origin(cli, scratch, a.map)
    if line is None or "baked at" in line:
        print("STB origin not restored:", line); ok = False

    # two maps in one go, one of them placed where the other used to be
    other = "TeleporterGreatwood" if a.map != "TeleporterGreatwood" else "OrchardFarm"
    op = wld_pos(wld, other)
    r = subprocess.run([cli, "world-move", a.map, "2048", "8064", other, "2176", "8064", "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0 or wld_pos(wld, other) != (2176, 8064):
        print("batch move failed:", r.stderr, r.stdout); ok = False
    r = subprocess.run([cli, "world-move", a.map, str(before[0]), str(before[1]), other, str(op[0]), str(op[1]), "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0 or open(wld, "rb").read() != orig["FinalAlbion.wld"] or open(bwd, "rb").read() != orig["FinalAlbion.bwd"]:
        print("batch move back did not restore the WLD/BWD"); ok = False

    # the GUI's World tab on the same scratch tree (queued moves, refusal, apply, undo)
    gui = os.path.join(ROOT, "build", "AlbionAtlasGUI.exe")
    if os.path.exists(gui):
        r = subprocess.run([gui, "--auto", "tests/ui/world.txt"], capture_output=True, text=True, cwd=ROOT)
        log = os.path.join(ROOT, "tests", "ui", "world.txt.log")
        if r.returncode != 0:
            print("GUI world script failed:")
            if os.path.exists(log):
                print(chr(10).join(open(log, encoding="utf-8", errors="replace").read().splitlines()[-15:]))
            ok = False
        else:
            print("GUI world script PASS")
        if open(wld, "rb").read() != orig["FinalAlbion.wld"] or open(bwd, "rb").read() != orig["FinalAlbion.bwd"]:
            print("GUI moves did not restore the WLD/BWD"); ok = False

    if not a.keep:
        shutil.rmtree(scratch, ignore_errors=True)
    print("overworld test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
