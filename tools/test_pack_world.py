#!/usr/bin/env python3
"""World edits travel in mod packs: two packs captured from shadow installs --
A adds a level (`forge-tools world install-level`: a map, a dedicated region, the
WAD entries and an appended static map), B edits a region (display name + a sees
reference) -- are built together by `mods build`, and the merged FinalAlbion.bwd
is byte-identical to the one both edits give when applied one after the other to
one install. Each pack alone rebuilds its own shadow's BWD exactly; the new level's
chunk lands in the built STB and its .lev / .tng in the built WAD. Scratch copies
only; needs the Fable install (skips cleanly without one).

  python tools/test_pack_world.py [--root <fable-root>] [--keep]
"""
import argparse, json, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_INSTALL = r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters"
CONTAINERS = ["FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion.wad", "FinalAlbion_RT.stb", "FinalAlbion.gtg"]
NEW, DONOR, NX, NY = "PackWorldTest", "StartOakValeWest", 6400, 6400
REGION, DISPLAY, SEES_OFF = "PicnicArea", "Pack World Picnic", "Fisherman"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.environ.get("FABLE_ROOT", DEFAULT_INSTALL))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    levels = os.path.join(a.root, "data", "Levels")
    if not all(os.path.exists(os.path.join(levels, c)) for c in CONTAINERS):
        print("pack world test skipped (no install)")
        return 0
    tools = os.path.join(ROOT, "build", "forge-tools.exe")
    forge = os.path.join(ROOT, "build", "forge.exe")
    work = os.path.join(ROOT, "build", "pack_world")
    shutil.rmtree(work, ignore_errors=True)
    ok = True

    def install(name):
        r = os.path.join(work, name)
        os.makedirs(os.path.join(r, "data", "Levels"))
        os.makedirs(os.path.join(r, "data", "CompiledDefs"))
        for c in CONTAINERS:
            shutil.copyfile(os.path.join(levels, c), os.path.join(r, "data", "Levels", c))
        for f in ("game.bin", "names.bin"):
            shutil.copyfile(os.path.join(a.root, "data", "CompiledDefs", f), os.path.join(r, "data", "CompiledDefs", f))
        return r

    def run(exe, *args):
        nonlocal ok
        r = subprocess.run([exe, *args], capture_output=True, text=True)
        if r.returncode != 0:
            ok = False
            print("rc", r.returncode, "for", os.path.basename(exe), " ".join(args)); print(r.stdout[-1200:], r.stderr[-1200:])
        return r

    def add_level(root):
        run(tools, "world", "install-level", root, NEW, str(NX), str(NY), "--from-donor", DONOR, "--no-backup")

    def edit_region(root):
        run(forge, "region-props", REGION, "--display", DISPLAY, "--install", root)
        run(forge, "world-sees", REGION, SEES_OFF, "0", "--install", root)

    base = install("base")
    sa, sb, sab = install("shadowA"), install("shadowB"), install("shadowAB")
    add_level(sa); edit_region(sb); add_level(sab); edit_region(sab)
    if not ok: print("pack world test FAILED (setup)"); return 1

    packs = {}
    for tag, shadow in (("A", sa), ("B", sb)):
        p = os.path.join(work, "Pack" + tag); os.makedirs(os.path.join(p, "assets"))
        json.dump({"version": 1, "name": "Pack " + tag, "models": [], "groundThemes": []}, open(os.path.join(p, "forge_pack.json"), "w"))
        r = run(tools, "mods", "capture", shadow, base, p)
        print("captured", tag + ":", " | ".join(l for l in r.stdout.splitlines()))
        packs[tag] = p
    if not os.path.exists(os.path.join(packs["A"], "stb", NEW + ".chunk")): print("pack A lacks the new static map"); ok = False
    if os.path.isdir(os.path.join(packs["B"], "stb")): print("pack B captured static maps it never changed:", os.listdir(os.path.join(packs["B"], "stb"))); ok = False

    def bwd(root):
        return open(os.path.join(root, "data", "Levels", "FinalAlbion.bwd"), "rb").read()

    def build(tag, names):
        # a fresh order per build: base keeps no forge_mods.json between builds
        om = os.path.join(base, "forge_mods.json")
        if os.path.exists(om): os.remove(om)
        for n in names: run(tools, "mods", "add", base, packs[n])
        out = os.path.join(work, "out_" + tag)
        r = run(tools, "mods", "build", base, out, "--json")
        try: rep = json.loads(r.stdout[r.stdout.index("{"):])
        except Exception: print("no JSON report:", r.stdout[-600:]); rep = {}
        return out, rep

    for tag, names, want in (("A", ["A"], sa), ("B", ["B"], sb), ("AB", ["A", "B"], sab), ("BA", ["B", "A"], None)):
        out, rep = build(tag, names)
        w = rep.get("world", {})
        if w.get("conflicts"): print(tag, "world conflicts:", w["conflicts"]); ok = False
        got = bwd(out) if os.path.exists(os.path.join(out, "data", "Levels", "FinalAlbion.bwd")) else b""
        if want is not None and got != bwd(want):
            print(tag, "merged FinalAlbion.bwd differs from the shadow's (", len(got), "vs", len(bwd(want)), ")", "notes:", w.get("notes")); ok = False
        if tag == "BA" and got != bwd(sab):
            # B's region edit and A's append commute: the other order gives the same world
            print("BA merged FinalAlbion.bwd differs from AB's"); ok = False
        if "A" in names:
            ext = os.path.join(out, "_stb")
            run(tools, "stb", "extract", os.path.join(out, "data", "Levels", "FinalAlbion_RT.stb"), ext, NEW)
            chunk = open(os.path.join(packs["A"], "stb", NEW + ".chunk"), "rb").read()
            if not any(open(os.path.join(dp, f), "rb").read() == chunk for dp, _, fs_ in os.walk(ext) for f in fs_):
                print(tag, "the built STB lacks", NEW + "'s chunk"); ok = False
            wx = os.path.join(out, "_wad")
            run(tools, "wad", "extract", os.path.join(out, "data", "Levels", "FinalAlbion.wad"), wx, NEW)
            leaves = {f.lower() for _, _, fs_ in os.walk(wx) for f in fs_}
            if not {NEW.lower() + ".lev", NEW.lower() + ".tng"} <= leaves: print(tag, "the built WAD lacks", NEW, "entries:", leaves); ok = False
            wld = open(os.path.join(out, "data", "Levels", "FinalAlbion.wld"), encoding="latin-1").read()
            if NEW not in wld: print(tag, "the built FinalAlbion.wld does not name", NEW); ok = False
        if "B" in names:
            wld = open(os.path.join(out, "data", "Levels", "FinalAlbion.wld"), encoding="latin-1").read()
            if DISPLAY not in wld: print(tag, "the built FinalAlbion.wld lacks the display name edit"); ok = False

    if not a.keep: shutil.rmtree(work, ignore_errors=True)
    print("pack world test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
