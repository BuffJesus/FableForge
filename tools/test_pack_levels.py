#!/usr/bin/env python3
"""Level edits into a mod pack, end to end: the editor (Writes go into = a pack)
sculpts Greatwood_1 and places an object; the pack gets the .lev, the .tng and the
re-baked static-map chunk + record (stb/), the install stays untouched; `mods build`
then lays the chunk into the built FinalAlbion_RT.stb, repacks the .lev into the
built FinalAlbion.wad and carries the placed thing in the merged .tng. Scratch copy
only; needs the Fable install (skips cleanly without one).

  python tools/test_pack_levels.py [--root <fable-root>] [--keep]
"""
import argparse, hashlib, json, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_INSTALL = r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters"
CONTAINERS = ["FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion.wad", "FinalAlbion_RT.stb", "FinalAlbion.gtg"]
MAP = "Greatwood_1"
PLACED = "PackLevelsBarrel"


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.environ.get("FABLE_ROOT", DEFAULT_INSTALL))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    levels = os.path.join(a.root, "data", "Levels")
    if not all(os.path.exists(os.path.join(levels, c)) for c in CONTAINERS):
        print("pack levels test skipped (no install)")
        return 0
    gui = os.path.join(ROOT, "build", "FableForge.exe")
    tools = os.path.join(ROOT, "build", "forge-tools.exe")
    scratch = os.path.join(ROOT, "build", "ui_pack_levels_install")
    pack = os.path.join(ROOT, "build", "ui_pack_levels_pack")
    out = os.path.join(ROOT, "build", "ui_pack_levels_out")
    for d in (scratch, pack, out): shutil.rmtree(d, ignore_errors=True)
    slevels = os.path.join(scratch, "data", "Levels")
    os.makedirs(slevels)
    os.makedirs(os.path.join(scratch, "data", "CompiledDefs"))
    for c in CONTAINERS:
        shutil.copyfile(os.path.join(levels, c), os.path.join(slevels, c))
    for f in ("game.bin", "names.bin"):
        shutil.copyfile(os.path.join(a.root, "data", "CompiledDefs", f), os.path.join(scratch, "data", "CompiledDefs", f))
    os.makedirs(os.path.join(pack, "assets"))
    json.dump({"version": 1, "name": "Pack Levels", "models": [], "groundThemes": []},
              open(os.path.join(pack, "forge_pack.json"), "w"), indent=2)
    tracked = [os.path.join(slevels, c) for c in CONTAINERS] + \
              [os.path.join(scratch, "data", "CompiledDefs", f) for f in ("game.bin", "names.bin")]
    before = {p: sha(p) for p in tracked}
    ok = True

    os.makedirs(os.path.join(ROOT, "build", "ui"), exist_ok=True)
    script = os.path.join(ROOT, "build", "ui_pack_levels.txt")
    with open(script, "w") as f:
        f.write("\n".join([
            "wait_maps", "wait_ready", f"set saveroot {scratch}", f"select {MAP}", "wait_loaded", "edit 1", "frames 2",
            f"pack_dest {pack}", "frames 3",
            "terrain_mode 0", "brush 10 6", "terrain_stroke 48 96 1", "frames 3",
            "screenshot build/ui/pack_footer.png",
            "deploy_terrain", "wait_terrain", "frames 1",
            "assert_state terrain_dirty 0",
            "assert_log into the pack",
            f"place OBJECT_BARREL_BREAKABLE {PLACED}", "frames 2",
            "deploy_level", "frames 1",
            "assert_log into pack",
            "dump_log", "quit",
        ]) + "\n")
    r = subprocess.run([gui, "--auto", script, "--install", a.root], capture_output=True, text=True)
    log = open(script + ".log", encoding="utf-8", errors="replace").read() if os.path.exists(script + ".log") else ""
    if r.returncode != 0 or "RESULT PASS" not in log:
        print("GUI pack writes:"); print(log[-1500:]); ok = False

    pl = os.path.join(pack, "data", "Levels", "FinalAlbion")
    want = [os.path.join(pl, MAP + ".lev"), os.path.join(pl, MAP + ".tng"),
            os.path.join(pack, "stb", MAP + ".chunk"), os.path.join(pack, "stb", MAP + ".record")]
    missing = [p for p in want if not os.path.exists(p)]
    if missing: print("pack lacks", missing); ok = False
    changed = [os.path.basename(p) for p in tracked if sha(p) != before[p]]
    if changed: print("the install changed:", changed); ok = False
    loose = os.path.join(slevels, "FinalAlbion")
    if os.path.isdir(loose) and os.listdir(loose): print("loose files written into the install:", os.listdir(loose)); ok = False
    if missing:
        print("pack levels test FAILED"); return 1

    def run(*args):
        rr = subprocess.run([tools, *args], capture_output=True, text=True)
        if rr.returncode != 0:
            nonlocal ok; ok = False
            print("rc", rr.returncode, "for", " ".join(args)); print(rr.stdout[-800:], rr.stderr[-800:])
        return rr

    run("mods", "add", scratch, pack)
    rr = run("mods", "build", scratch, out, "--json")
    try:
        rep = json.loads(rr.stdout[rr.stdout.index("{"):])
    except Exception:
        print("no JSON report:", rr.stdout[-600:], rr.stderr[-600:]); rep = {}
    stb_rows = rep.get("forge_stb", [])
    if not any(MAP in row.get("maps", []) for row in stb_rows): print("forge_stb report:", stb_rows); ok = False

    # the built STB's chunk for the map is the pack's
    ext = os.path.join(out, "_stb_extract")
    run("stb", "extract", os.path.join(out, "data", "Levels", "FinalAlbion_RT.stb"), ext, MAP)
    chunk = open(os.path.join(pack, "stb", MAP + ".chunk"), "rb").read()
    found = False
    for dp, _, fs_ in os.walk(ext):
        for fn in fs_:
            if open(os.path.join(dp, fn), "rb").read() == chunk: found = True
    if not found: print("the built STB does not carry the pack's", MAP, "chunk (extracted:", os.listdir(ext) if os.path.isdir(ext) else "-", ")"); ok = False

    # the built WAD carries the pack's .lev; the merged .tng the placed thing
    wx = os.path.join(out, "_wad_extract")
    run("wad", "extract", os.path.join(out, "data", "Levels", "FinalAlbion.wad"), wx)
    lev = open(os.path.join(pl, MAP + ".lev"), "rb").read()
    wlev = [os.path.join(dp, fn) for dp, _, fs_ in os.walk(wx) for fn in fs_ if fn.lower() == MAP.lower() + ".lev"]
    if not wlev or open(wlev[0], "rb").read() != lev: print("the built WAD's", MAP + ".lev is not the pack's", wlev); ok = False
    wtng = [os.path.join(dp, fn) for dp, _, fs_ in os.walk(wx) for fn in fs_ if fn.lower() == MAP.lower() + ".tng"]
    otng = os.path.join(out, "data", "Levels", "FinalAlbion", MAP + ".tng")
    texts = [open(p, encoding="latin-1").read() for p in wtng + ([otng] if os.path.exists(otng) else [])]
    if not texts or not all(PLACED in t for t in texts): print("the merged", MAP + ".tng lacks", PLACED, "(", len(texts), "copies )"); ok = False

    if not a.keep:
        for d in (scratch, pack, out): shutil.rmtree(d, ignore_errors=True)
    print("pack levels test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
