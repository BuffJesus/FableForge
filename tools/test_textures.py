#!/usr/bin/env python3
"""Textures tab against a scratch tree: copies textures.big + CompiledDefs (and the WAD
for the map preview) into build/ui_textures_install, runs tests/ui/textures.txt, then
checks the written archive with the CLI (entry validated, backup present).

  python tools/test_textures.py [--root <fable-root>] [--keep]
"""
import argparse, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=r"C:\programs\steam\steamapps\common\Fable The Lost Chapters")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    big = os.path.join(a.root, "data", "graphics", "pc", "textures.big")
    if not os.path.exists(big):
        print("textures test skipped (no install)"); return 0
    scratch = os.path.join(ROOT, "build", "ui_textures_install")
    shutil.rmtree(scratch, ignore_errors=True)
    for d in ("data/graphics/pc", "data/CompiledDefs", "data/Levels"):
        os.makedirs(os.path.join(scratch, d))
    shutil.copyfile(big, os.path.join(scratch, "data", "graphics", "pc", "textures.big"))
    for f in ("game.bin", "names.bin"):
        shutil.copyfile(os.path.join(a.root, "data", "CompiledDefs", f), os.path.join(scratch, "data", "CompiledDefs", f))
    cli = os.path.join(ROOT, "build", "AlbionAtlas.exe")
    gui = os.path.join(ROOT, "build", "AlbionAtlasGUI.exe")
    ok = True
    r = subprocess.run([gui, "--auto", "tests/ui/textures.txt"], capture_output=True, text=True, cwd=ROOT)
    log = os.path.join(ROOT, "tests", "ui", "textures.txt.log")
    if r.returncode != 0:
        print("GUI textures script failed:")
        if os.path.exists(log):
            print(chr(10).join(open(log, encoding="utf-8", errors="replace").read().splitlines()[-15:]))
        ok = False
    else:
        print("GUI textures script PASS")
    if not os.path.exists(os.path.join(scratch, "data", "graphics", "pc", "textures.big.atlas-orig")):
        print("no textures.big.atlas-orig backup"); ok = False
    r = subprocess.run([cli, "textures", "ATLAS_UI_TEX", "--install", scratch], capture_output=True, text=True)
    if "ATLAS_UI_TEX" not in r.stdout or "DXT3" not in r.stdout:
        print("the added texture is not listed:", r.stdout[-300:]); ok = False
    # the replaced slot decodes back to (nearly) the same pixels
    r = subprocess.run([cli, "texture-export", r"[\DEV\BBBPROJECTS\FABLE1_5RELEASE\FABLE\RESOURCES\ART3\GAMEPLAY\TEXTURE\BARREL_BRACED_1_24.TGA]",
                        os.path.join(ROOT, "build", "ui", "barrel_braced_2.png"), "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0:
        print("export after replace failed:", r.stderr); ok = False
    else:
        from PIL import Image
        import numpy as np
        p1 = np.asarray(Image.open(os.path.join(ROOT, "build", "ui", "barrel_braced.png")).convert("RGB"), int)
        p2 = np.asarray(Image.open(os.path.join(ROOT, "build", "ui", "barrel_braced_2.png")).convert("RGB"), int)
        diff = np.abs(p1 - p2).mean()
        if p1.shape != p2.shape or diff > 2.0:
            print(f"replaced texture drifted: shape {p1.shape} vs {p2.shape}, mean diff {diff:.2f}"); ok = False
        else:
            print(f"replace round trip: mean abs diff {diff:.2f}")
    if not a.keep:
        shutil.rmtree(scratch, ignore_errors=True)
    print("textures test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
