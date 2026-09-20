#!/usr/bin/env python3
"""The backup manager over every convention an install can carry, on a scratch root:
a `.forge-orig` original (an editor write), a legacy `.atlas-orig`, a `.forge-created` marker,
a staged deploy (`forge-tools stage` -> `.forgebak` + manifest) and an original that was taken
ON TOP of that stage (the trap from the real install: `forge restore` must revert the stage and
rebase that original, not copy the staged content back). Nothing touches the install.

  python tools/test_backups.py [--keep]
"""
import argparse, os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    os.chdir(ROOT)
    forge = os.path.join(ROOT, "build", "forge.exe")
    tools = os.path.join(ROOT, "build", "forge-tools.exe")
    scratch = os.path.join(ROOT, "build", "ui_backups_root")
    shutil.rmtree(scratch, ignore_errors=True)
    levels = os.path.join(scratch, "data", "Levels")
    os.makedirs(os.path.join(levels, "FinalAlbion")); os.makedirs(os.path.join(scratch, "data", "CompiledDefs"))
    open(os.path.join(scratch, "data", "CompiledDefs", "game.bin"), "wb").write(b"x")   # what makes a folder an install to the CLI
    ok = True

    def w(path, text):
        with open(path, "w", newline="\n") as f: f.write(text)
    def r(path):
        return open(path).read() if os.path.exists(path) else None
    def run(exe, *args, expect=0):
        p = subprocess.run([exe, *args], capture_output=True, text=True)
        if (p.returncode == 0) != (expect == 0):
            nonlocal ok; ok = False
            print("unexpected rc", p.returncode, "for", " ".join(args)); print(p.stdout[-600:], p.stderr[-400:])
        return p

    # 1. an editor write with the new suffix, a legacy one, a created file
    w(os.path.join(levels, "A.wld"), "retail A"); w(os.path.join(levels, "A.wld.forge-orig"), "retail A"); w(os.path.join(levels, "A.wld"), "edited A")
    w(os.path.join(levels, "B.bwd"), "retail B"); w(os.path.join(levels, "B.bwd.atlas-orig"), "retail B"); w(os.path.join(levels, "B.bwd"), "edited B")
    w(os.path.join(levels, "FinalAlbion", "New.tng"), "created"); w(os.path.join(levels, "FinalAlbion", "New.tng.forge-created"), "created by FableForge\n")
    # 2. a staged deploy over C (the .forgebak is retail C), then an original of C taken on top of the stage
    w(os.path.join(levels, "C.qst"), "retail C")
    overlay = os.path.join(ROOT, "build", "ui_backups_overlay"); shutil.rmtree(overlay, ignore_errors=True)
    os.makedirs(os.path.join(overlay, "data", "Levels")); w(os.path.join(overlay, "data", "Levels", "C.qst"), "staged C")
    run(tools, "stage", scratch, overlay)
    if r(os.path.join(levels, "C.qst")) != "staged C" or r(os.path.join(levels, "C.qst.forgebak")) != "retail C": print("stage did not land"); ok = False
    time.sleep(1.1)   # the original must be younger than the .forgebak
    w(os.path.join(levels, "C.qst.forge-orig"), "staged C"); w(os.path.join(levels, "C.qst"), "edited C")

    p = run(forge, "backups", "--install", scratch)
    for tag, leaf in (("EDIT", "A.wld"), ("EDIT", "B.bwd"), ("NEW ", "New.tng"), ("MODS", "C.qst"), ("EDIT", "C.qst")):
        if not any(tag in l and leaf in l for l in p.stdout.splitlines()): print("backups list lacks", tag, leaf); print(p.stdout); ok = False

    p = run(forge, "restore", "--install", scratch)
    if r(os.path.join(levels, "A.wld")) != "retail A": print("A not restored"); ok = False
    if r(os.path.join(levels, "B.bwd")) != "retail B": print("legacy B not restored"); ok = False
    if os.path.exists(os.path.join(levels, "FinalAlbion", "New.tng")) or os.path.exists(os.path.join(levels, "FinalAlbion", "New.tng.forge-created")): print("created file not removed"); ok = False
    if r(os.path.join(levels, "C.qst")) != "retail C": print("C should be the stage's retail bytes, got", r(os.path.join(levels, "C.qst"))); ok = False
    if r(os.path.join(levels, "C.qst.forge-orig")) != "retail C": print("C's original should be rebased onto retail, got", r(os.path.join(levels, "C.qst.forge-orig"))); ok = False
    if os.path.exists(os.path.join(levels, "C.qst.forgebak")) or os.path.exists(os.path.join(scratch, "forge_stage_manifest.json")): print("stage not reverted"); ok = False
    if "rebased" not in p.stdout: print("restore did not report the rebase:", p.stdout); ok = False
    if not os.path.exists(os.path.join(levels, "A.wld.forge-orig")) or not os.path.exists(os.path.join(levels, "B.bwd.atlas-orig")): print("originals must be kept"); ok = False

    if not a.keep:
        shutil.rmtree(scratch, ignore_errors=True); shutil.rmtree(overlay, ignore_errors=True)
    print("backups test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
