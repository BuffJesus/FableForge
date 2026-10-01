#!/usr/bin/env python3
"""FableForge recipe packs (forge_pack.json) through the mod composer, offline, on a scratch
root that carries the install's defs and banks: two packs -- A adds a model (CUBE_A, with a
texture) and a ground theme (GROUND_RECIPE_A), B adds a model (CUBE_B) -- are added to the load
order and built. Checks: every recipe applied; OBJECT_<NAME>.Graphic.modelId = the id its
MESH_<NAME> got in the BUILT graphics.big; swapping the order swaps which cube gets the lower id
(ids come from the build, not from the pack); the theme is an ENGINE_THEME in the built game.bin;
deploy stages the banks and undeploy puts every file back byte-identical. Skips without the
install. Copies ~800 MB of banks under build/. Nothing touches the install.

  python tools/test_recipe_packs.py [--root <fable install>] [--keep]
"""
import argparse, hashlib, json, os, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from test_meshimport import find_root, pristine, write_obj, write_png   # the shared cube + texture


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""): h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    root = find_root(a.root)
    if not root:
        print("recipe pack test skipped (no install)"); return 0
    os.chdir(ROOT)
    tools = os.path.join(ROOT, "build", "forge-tools.exe")
    scratch = os.path.join(ROOT, "build", "recipe_root"); shutil.rmtree(scratch, ignore_errors=True)
    for d in ("data/CompiledDefs", "data/graphics/pc", "data/Levels"):
        os.makedirs(os.path.join(scratch, d))
    banks = ["data/CompiledDefs/game.bin", "data/CompiledDefs/names.bin", "data/graphics/pc/textures.big"]
    for rel in banks:
        src = pristine(root, rel)
        if not src: print("install lacks", rel); return 1
        shutil.copyfile(src, os.path.join(scratch, rel))
    gfx = pristine(root, "data/graphics/graphics.big") or pristine(root, "data/graphics/pc/graphics.big")
    shutil.copyfile(gfx, os.path.join(scratch, "data", "graphics", "graphics.big"))
    banks.append("data/graphics/graphics.big")
    before = {rel: sha(os.path.join(scratch, rel)) for rel in banks}

    # A retains legacy flat asset paths; B uses the editor's per-recipe layout.
    packs = os.path.join(ROOT, "build", "recipe_packs"); shutil.rmtree(packs, ignore_errors=True)
    pa, pb = os.path.join(packs, "PackA"), os.path.join(packs, "PackB")
    for p in (pa, pb): os.makedirs(os.path.join(p, "assets"))
    write_obj(os.path.join(pa, "assets", "cube.obj")); write_png(os.path.join(pa, "assets", "wood.png"))
    model_b = "assets/models/CUBE_B/model/cube.obj"
    os.makedirs(os.path.dirname(os.path.join(pb, model_b)))
    write_obj(os.path.join(pb, model_b))
    json.dump({"version": 1, "name": "Pack A",
               "models": [{"name": "CUBE_A", "model": "assets/cube.obj", "texture": "assets/wood.png", "donor": "OBJECT_BARREL_UNBREAKABLE", "collision": True}],
               "groundThemes": [{"name": "GROUND_RECIPE_A", "png": "assets/wood.png", "cliffPng": "", "donor": "GROUND_GRASS"}]},
              open(os.path.join(pa, "forge_pack.json"), "w"), indent=2)
    json.dump({"version": 1, "name": "Pack B", "models": [{"name": "CUBE_B", "model": model_b, "texture": "", "donor": "OBJECT_BARREL_UNBREAKABLE", "collision": True}]},
              open(os.path.join(pb, "forge_pack.json"), "w"), indent=2)
    ok = True

    def run(*args, expect=0):
        r = subprocess.run([tools, *args], capture_output=True, text=True)
        if (r.returncode == 0) != (expect == 0):
            nonlocal ok; ok = False
            print("unexpected rc", r.returncode, "for", " ".join(args)); print(r.stdout[-800:], r.stderr[-800:])
        return r

    run("mods", "add", scratch, pa)
    run("mods", "add", scratch, pb)
    order = json.loads(run("mods", "list", scratch, "--json").stdout)["mods"]
    if [m["kind"] for m in order] != ["forge", "forge"]: print("kinds:", [m["kind"] for m in order]); ok = False

    def build_ids(tag):
        out = os.path.join(ROOT, "build", "recipe_out_" + tag); shutil.rmtree(out, ignore_errors=True)
        r = run("mods", "build", scratch, out, "--json")
        try: rep = json.loads(r.stdout[r.stdout.index("{"):])
        except Exception: print("no JSON report:", r.stdout[-400:]); return None
        for f in rep.get("forge", []):
            if f.get("errors"): print("recipe errors in", f["mod"], f["errors"]); nonlocal_fail()
        ids = {}
        for name in ("CUBE_A", "CUBE_B"):
            info = json.loads(run("mesh-info", os.path.join(out, "data", "graphics", "graphics.big"), "MESH_" + name, "--json").stdout)
            mid = info.get("id", 0)
            dec = run("defs", "decode", out, "docs/re_reference/def_schema.json", "OBJECT_" + name).stdout
            if ("05000000" + struct.pack("<I", mid).hex()) not in dec.replace(" ", ""):
                print(tag, name, "Graphic.modelId does not name the built mesh id", mid); nonlocal_fail()
            ids[name] = mid
        dec = run("defs", "decode", out, "docs/re_reference/def_schema.json", "GROUND_RECIPE_A").stdout
        if "BaseTexture" not in dec: print(tag, "GROUND_RECIPE_A not in the built game.bin"); nonlocal_fail()
        return ids

    fails = []
    def nonlocal_fail(): fails.append(1)

    ab = build_ids("ab")
    run("mods", "move", scratch, "PackB", "0")
    ba = build_ids("ba")
    if not ab or not ba or not (ab["CUBE_A"] < ab["CUBE_B"] and ba["CUBE_B"] < ba["CUBE_A"]):
        print("ids did not follow the load order:", ab, ba); ok = False
    # deploy stages the built banks; undeploy puts every file back byte-identical
    run("mods", "deploy", scratch)
    staged = {rel: sha(os.path.join(scratch, rel)) for rel in banks}
    if staged == before: print("deploy staged nothing"); ok = False
    run("mods", "undeploy", scratch)
    after = {rel: sha(os.path.join(scratch, rel)) for rel in banks}
    if after != before: print("undeploy left", [r for r in banks if after[r] != before[r]]); ok = False
    if fails: ok = False
    if not a.keep:
        for d in (scratch, packs, os.path.join(ROOT, "build", "recipe_out_ab"), os.path.join(ROOT, "build", "recipe_out_ba")):
            shutil.rmtree(d, ignore_errors=True)
    print("recipe pack test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
