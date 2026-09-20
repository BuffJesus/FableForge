#!/usr/bin/env python3
"""The GB-pack stress case of the mod composer (ROADMAP 0.20): Project Seasons (a whole-tree
pack: 794 loose levels, a new ProjectAutumn/ folder, whole STB / banks / game.bin / text.big,
the retail WAD parked as `_FinalAlbion.wad`, a userst.ini) under the Unofficial Fable Patch's
bsdiff, built onto a scratch root that carries the install's retail containers. Checks the
composer's GB-pack rules: the parked WAD and the settings file are not layers, the loose levels
are repacked into FinalAlbion.wad, the new level folder rides along, provenance covers every
changed level. Skips without the extracted pack (work/nexus_mods/_peek/ProjectSeasons) or the
install. ~30 s, ~2 GB of scratch under build/. Nothing touches the install.

  python tools/test_gbpack.py [--root <fable install>] [--keep]
"""
import argparse, json, os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEASONS = os.path.join(ROOT, "work", "nexus_mods", "_peek", "ProjectSeasons")
UFP = os.path.join(ROOT, "work", "nexus_mods", "_peek", "UnofficialFablePatch", "Patches", "game.bin.patch")


def find_root(explicit: str) -> str:
    if explicit:
        return explicit
    for c in [r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters",
              r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters"]:
        if os.path.exists(os.path.join(c, "data", "CompiledDefs", "game.bin")):
            return c
    return ""


def pristine(root, rel):
    """the install's file, preferring the retail bytes a deploy or an editor write put aside"""
    for sfx in (".retail-bak", ".forgebak", ".forge-orig", ".atlas-orig", ""):
        p = os.path.join(root, rel + sfx)
        if os.path.exists(p): return p
    return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    root = find_root(a.root)
    if not root or not os.path.isdir(os.path.join(SEASONS, "data")) or not os.path.exists(UFP):
        print("gb-pack test skipped (no install, or Project Seasons not extracted under work/nexus_mods/_peek)"); return 0
    os.chdir(ROOT)
    tool = os.path.join(ROOT, "build", "forge-tools.exe")
    scratch = os.path.join(ROOT, "build", "gb_root"); out = os.path.join(ROOT, "build", "gb_out")
    shutil.rmtree(scratch, ignore_errors=True); shutil.rmtree(out, ignore_errors=True)
    for d in ("data/Levels/FinalAlbion", "data/CompiledDefs", "data/graphics/pc", "data/lang/English"):
        os.makedirs(os.path.join(scratch, d))
    for rel in ("data/CompiledDefs/game.bin", "data/CompiledDefs/names.bin", "data/CompiledDefs/script.bin", "data/CompiledDefs/frontend.bin",
                "data/Levels/FinalAlbion.wad", "data/Levels/FinalAlbion.wld", "data/Levels/FinalAlbion.bwd", "data/Levels/FinalAlbion.gtg",
                "data/Levels/FinalAlbion.qst", "data/Levels/FinalAlbion_RT.stb", "data/lang/English/text.big"):
        src = pristine(root, rel)
        if not src: print("install lacks", rel); return 1
        shutil.copyfile(src, os.path.join(scratch, rel))
    ok = True

    def run(*args):
        r = subprocess.run([tool, *args], capture_output=True, text=True)
        if r.returncode != 0:
            nonlocal ok; ok = False
            print("rc", r.returncode, "for", " ".join(args)); print(r.stdout[-800:], r.stderr[-400:])
        return r

    run("mods", "add", scratch, UFP, "--name", "UFP")
    run("mods", "add", scratch, SEASONS, "--name", "Project Seasons")
    t0 = time.time()
    r = run("mods", "build", scratch, out)
    dt = time.time() - t0
    o = r.stdout
    if "parks the retail WAD as _FinalAlbion.wad: skipped" not in o: print("parked WAD not recognised"); ok = False
    if "ships userst.ini: not applied" not in o: print("userst.ini not skipped"); ok = False
    if "794 level file(s) repacked" not in o: print("loose levels not repacked:", [l for l in o.splitlines() if "wad" in l.lower()]); ok = False
    if "448 levels (0 thing-merged, 448 single-editor copies)" not in o: print("TNG copies unexpected:", [l for l in o.splitlines() if "TNG" in l]); ok = False
    if os.path.exists(os.path.join(out, "data", "Levels", "_FinalAlbion.wad")) or os.path.exists(os.path.join(out, "userst.ini")): print("parked WAD / settings copied into the build"); ok = False
    if len(os.listdir(os.path.join(out, "data", "Levels", "ProjectAutumn"))) != 101: print("ProjectAutumn folder not carried"); ok = False
    if not os.path.exists(os.path.join(out, "data", "Levels", "FinalAlbion_RT.stb")): print("STB layer missing"); ok = False
    prov = json.load(open(os.path.join(out, "forge_mods_provenance.json")))
    if len(prov["levels"]) != 448: print("provenance levels", len(prov["levels"])); ok = False
    # the retail WAD wins over loose files in-engine: every level Seasons ships is now inside the rebuilt WAD
    lst = run("wad", "list", os.path.join(out, "data", "Levels", "FinalAlbion.wad")).stdout
    if "arena.tng" not in lst.lower(): print("rebuilt WAD lacks the levels"); ok = False
    print(f"build took {dt:.1f}s")
    if not a.keep:
        shutil.rmtree(scratch, ignore_errors=True); shutil.rmtree(out, ignore_errors=True)
    print("gb-pack test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
