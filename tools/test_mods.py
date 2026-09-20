#!/usr/bin/env python3
"""The mod load order, offline, on a scratch root: forge-tools mods add / list / move /
disable / build over three corpus packs of three shapes (a bsdiff .patch that needs the pristine
game.bin, a Fable Explorer v459 .fmp, a ChocolateBox v510 .fmp + its loose-TNG tree). Skips when
the corpus (work/nexus_mods/_peek) or the install is missing. Nothing touches the install.

  python tools/test_mods.py [--root <fable install>] [--keep]
"""
import argparse, json, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PEEK = os.path.join(ROOT, "work", "nexus_mods", "_peek")
UFP = os.path.join(PEEK, "UnofficialFablePatch", "Patches", "game.bin.patch")
SPECIAL = os.path.join(PEEK, "SpecialMelee", "SpecialMeleeAbilities.fmp")
F2 = os.path.join(PEEK, "F2MeleeWeapons")
F2FMP = os.path.join(F2, "F2MeleeWeaponPack.fmp")
CONTROLLER = os.path.join(PEEK, "FableControllerSupport", "Mods", "FableControllerSupport")
WADER = os.path.join(PEEK, "WaterWader", "Mods", "WaterWader")
DEFC = r"C:\Users\Cornelio\Documents\EgoCoreInspect\fable-defs\target\release\defc.exe"
DEFS_TEXT = r"D:\Documents\FableTLC\unified_build\UnifiedFable\Data\Defs"
TNGS = ["ArenaHallOfHeroes", "BanditCampPathEntrance", "BarrowFields", "BowerstoneSlumsWarehouses", "GibbetWoods", "GrannysHouse", "HookCoast"]


def find_root(explicit: str) -> str:
    if explicit:
        return explicit
    for c in [r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters",
              r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters"]:
        if os.path.exists(os.path.join(c, "data", "CompiledDefs", "game.bin")):
            return c
    return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    root = find_root(a.root)
    if not root or not all(os.path.exists(p) for p in (UFP, SPECIAL, F2FMP)):
        print("mods test skipped (no install or no corpus under work/nexus_mods/_peek)"); return 0
    os.chdir(ROOT)
    tool = os.path.join(ROOT, "build", "forge-tools.exe")
    scratch = os.path.join(ROOT, "build", "ui_mods_root")
    shutil.rmtree(scratch, ignore_errors=True)
    defs = os.path.join(scratch, "data", "CompiledDefs"); levels = os.path.join(scratch, "data", "Levels", "FinalAlbion")
    os.makedirs(defs); os.makedirs(levels)
    for f in ["game.bin", "names.bin", "script.bin", "frontend.bin", "game.bin.retail-bak", "names.bin.retail-bak"]:
        src = os.path.join(root, "data", "CompiledDefs", f)
        if os.path.exists(src): shutil.copyfile(src, os.path.join(defs, f))
    for t in TNGS:
        src = os.path.join(root, "data", "Levels", "FinalAlbion", t + ".tng")
        if os.path.exists(src): shutil.copyfile(src, os.path.join(levels, t + ".tng"))
    wad = os.path.join(root, "data", "Levels", "FinalAlbion.wad")
    if os.path.exists(wad): shutil.copyfile(wad, os.path.join(scratch, "data", "Levels", "FinalAlbion.wad"))
    lang = os.path.join(root, "data", "lang", "English")
    text_src = os.path.join(lang, "text.big.retail-bak") if os.path.exists(os.path.join(lang, "text.big.retail-bak")) else os.path.join(lang, "text.big")
    if os.path.exists(text_src):
        os.makedirs(os.path.join(scratch, "data", "lang", "English"))
        shutil.copyfile(text_src, os.path.join(scratch, "data", "lang", "English", "text.big"))
    ok = True

    def run(*args, expect=0):
        r = subprocess.run([tool, *args], capture_output=True, text=True)
        if (r.returncode == 0) != (expect == 0):
            nonlocal ok; ok = False
            print("unexpected rc", r.returncode, "for", " ".join(args)); print(r.stdout[-600:], r.stderr[-600:])
        return r

    run("mods", "add", scratch, UFP, "--name", "Unofficial Fable Patch", "--note", "2.0")
    run("mods", "add", scratch, SPECIAL)
    run("mods", "add", scratch, F2FMP, "--name", "F2 Melee (defs)")
    run("mods", "add", scratch, F2, "--name", "F2 Melee (levels)")
    r = run("mods", "add", scratch, SPECIAL, expect=1)   # same contents twice
    if "already in the order" not in r.stderr: print("duplicate not refused:", r.stderr); ok = False
    r = run("mods", "list", scratch, "--json")
    order = json.loads(r.stdout)["mods"]
    if [m["kind"] for m in order] != ["patch", "fmp", "fmp", "tree"]:
        print("kinds:", [m["kind"] for m in order]); ok = False
    if any(len(m["sha256"]) != 64 for m in order): print("hashes missing"); ok = False
    run("mods", "move", scratch, "F2 Melee (levels)", "0")
    run("mods", "disable", scratch, "1")
    order = json.loads(run("mods", "list", scratch, "--json").stdout)["mods"]
    if order[0]["name"] != "F2 Melee (levels)" or order[1]["enabled"]:
        print("move/disable wrong:", [(m["name"], m["enabled"]) for m in order]); ok = False
    run("mods", "enable", scratch, "1")
    out = os.path.join(ROOT, "build", "ui_mods_out")
    shutil.rmtree(out, ignore_errors=True)
    r = run("mods", "build", scratch, out)
    if "112 changes applied (95 new records)" not in r.stdout:
        print("build output unexpected:", r.stdout[-800:]); ok = False
    if "applied to game.bin.retail-bak instead" not in r.stdout and os.path.exists(os.path.join(defs, "game.bin.retail-bak")):
        # the install's game.bin is a re-save; the patch must have gone to the pristine copy
        print("expected the pristine fallback for the bsdiff patch:", r.stdout[-400:]); ok = False
    if not os.path.exists(os.path.join(out, "data", "CompiledDefs", "game.bin")):
        print("no merged game.bin"); ok = False
    tngs = [f for f in os.listdir(os.path.join(out, "data", "Levels", "FinalAlbion")) if f.endswith(".tng")] if os.path.isdir(os.path.join(out, "data", "Levels", "FinalAlbion")) else []
    if len(tngs) < 7: print("merged TNGs:", tngs); ok = False
    if os.path.exists(wad):
        if "FinalAlbion.wad rebuilt: 8 level file(s) repacked, 0 new" not in r.stdout: print("WAD repack missing:", [l for l in r.stdout.splitlines() if "wad" in l.lower()]); ok = False
        r2 = run("wad", "list", os.path.join(out, "data", "Levels", "FinalAlbion.wad"))
        if "BarrowFields.tng" not in r2.stdout: print("repacked WAD unreadable"); ok = False
    r = run("defs", "list", out, "game.bin", "F2_RUSTY")
    if "CInventoryItemDef_F2_RUSTY_LONGSWORD" not in r.stdout: print("F2 record missing from the merged defs"); ok = False
    # the Fable Explorer package's Text bank reaches text.big (Special Melee shortens this tooltip)
    merged_text = os.path.join(out, "data", "lang", "English", "text.big")
    if os.path.exists(os.path.join(scratch, "data", "lang", "English", "text.big")):
        r = run("text", "show", merged_text, "TEXT_GUI_EXPSPEND_PHYSICAL_STRENGTH_LEVEL3")
        if "heavy weapons" in r.stdout or "Physique dictates" not in r.stdout:
            print("Special Melee's text.big change did not land:", r.stdout[:300]); ok = False
    # EgoCore packs: a DLL-only mod and a DLL + text-def mod (the def part needs defc + the text tree)
    if os.path.isdir(CONTROLLER) and os.path.isdir(WADER):
        env = dict(os.environ)
        defc = env.get("FORGE_DEFC", DEFC); text = env.get("FORGE_DEFS_TEXT", DEFS_TEXT)
        have_defc = os.path.exists(defc) and os.path.isdir(text)
        if have_defc:
            env["FORGE_DEFC"] = defc; env["FORGE_DEFS_TEXT"] = text
        if os.path.exists(os.path.join(root, "Mods.ini")):
            shutil.copyfile(os.path.join(root, "Mods.ini"), os.path.join(scratch, "Mods.ini"))
        run("mods", "add", scratch, CONTROLLER, "--name", "Controller Support")
        run("mods", "add", scratch, WADER)
        shutil.rmtree(out, ignore_errors=True)
        r = subprocess.run([tool, "mods", "build", scratch, out], capture_output=True, text=True, env=env)
        if r.returncode != 0: print("egocore build failed:", r.stdout[-800:], r.stderr[-400:]); ok = False
        ini_path = os.path.join(out, "Mods.ini")
        ini = open(ini_path, encoding="utf-8").read() if os.path.exists(ini_path) else ""
        for line in ["[Mods]", "FableScriptExtender.dll=1", "FableControllerSupport\\FableControllerSupport.dll=1", "WaterWader\\WaterWader.dll=1"]:
            if line not in ini: print("Mods.ini misses", line); print(ini); ok = False
        for dll in ["FableControllerSupport/FableControllerSupport.dll", "WaterWader/WaterWader.dll"]:
            if not os.path.exists(os.path.join(out, "Mods", dll)): print("DLL not installed:", dll); ok = False
        if have_defc:
            if "egocore FableControllerSupport: 3 .def file(s), 4 block(s) replaced, 1 added -> 14 record(s) changed (43 field(s)), 1 new, 0 skipped" not in r.stdout:
                print("egocore def layer unexpected:", [l for l in r.stdout.splitlines() if "egocore" in l]); ok = False
            r2 = subprocess.run([tool, "defs", "decode", out, "docs/re_reference/def_schema.json", "FABLE_XBOX_CONTROL_SCHEME_BASE"], capture_output=True, text=True)
            if "count=71" not in r2.stdout: print("the controller mod's extra binding is missing from the merged scheme"); ok = False
        else:
            print("(defc / text defs not found: the EgoCore def layer part is skipped)")
    else:
        print("(EgoCore corpus not extracted: skipped)")
    # deploy = revert + rebuild + stage; undeploy = revert: the root comes back byte-identical
    def digest(p):
        import hashlib
        return hashlib.sha256(open(p, "rb").read()).hexdigest() if os.path.exists(p) else None
    before = digest(os.path.join(defs, "game.bin"))
    r = subprocess.run([tool, "mods", "deploy", scratch], capture_output=True, text=True, env=env if os.path.isdir(CONTROLLER) else None)
    if r.returncode != 0 or "staged" not in r.stdout: print("deploy failed:", r.stdout[-600:], r.stderr[-400:]); ok = False
    if digest(os.path.join(defs, "game.bin")) == before: print("deploy did not change game.bin"); ok = False
    r = subprocess.run([tool, "mods", "deploy", scratch], capture_output=True, text=True, env=env if os.path.isdir(CONTROLLER) else None)
    if "reverted the previous stage" not in r.stdout: print("second deploy did not revert first:", r.stdout[-400:]); ok = False
    r = run("mods", "undeploy", scratch)
    if digest(os.path.join(defs, "game.bin")) != before: print("undeploy did not restore game.bin"); ok = False
    if os.path.isdir(os.path.join(scratch, "Mods")) and any(os.scandir(os.path.join(scratch, "Mods"))): print("undeploy left Mods/ content"); ok = False
    run("mods", "remove", scratch, "F2 Melee (defs)")
    if "F2 Melee (defs)" in [m["name"] for m in json.loads(run("mods", "list", scratch, "--json").stdout)["mods"]]: print("remove failed"); ok = False
    # the Mods tab over the same scratch root: add / reorder / enable, deploy + undeploy through forge-tools.exe
    gui = os.path.join(ROOT, "build", "FableForge.exe")
    if os.path.exists(gui):
        if os.path.exists(os.path.join(scratch, 'forge_mods.json')): os.remove(os.path.join(scratch, 'forge_mods.json'))   # the tab starts from an empty order
        r = subprocess.run([gui, "--auto", "tests/ui/mods.txt"], capture_output=True, text=True)
        log = os.path.join(ROOT, "tests", "ui", "mods.txt.log")
        tail = open(log, encoding="utf-8", errors="replace").read().strip().splitlines() if os.path.exists(log) else []
        if r.returncode != 0 or not tail or "RESULT PASS" not in tail[-1]:
            print("ui mods failed:", " | ".join(tail[-8:])); ok = False
    if not a.keep:
        shutil.rmtree(scratch, ignore_errors=True); shutil.rmtree(out, ignore_errors=True)
    print("mods test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
