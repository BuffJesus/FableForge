#!/usr/bin/env python3
"""Textures tab against a scratch tree: copies textures.big + CompiledDefs (the WAD
for the map preview is read from the source install), adapts tests/ui/textures.txt
in an owned workspace, then checks the archive, PNG round trip and exact Restore.

  python tools/test_textures.py [--root <fable-root>] [--keep]
"""
import argparse, hashlib, json, os, re, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=r"C:\programs\steam\steamapps\common\Fable The Lost Chapters")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    big = os.path.join(a.root, "data", "graphics", "pc", "textures.big")
    if not os.path.exists(big):
        print("textures test skipped (no install)"); return 0
    work = Path(tempfile.mkdtemp(prefix="textures-ui-", dir=Path(ROOT) / "build")).resolve()
    assert work.parent == (Path(ROOT) / "build").resolve()
    print("evidence retained at", work, flush=True)
    scratch = str(work / "install")
    images = work / "ui"
    images.mkdir()
    for d in ("data/graphics/pc", "data/CompiledDefs", "data/Levels"):
        os.makedirs(os.path.join(scratch, d))
    shutil.copyfile(big, os.path.join(scratch, "data", "graphics", "pc", "textures.big"))
    for f in ("game.bin", "names.bin"):
        shutil.copyfile(os.path.join(a.root, "data", "CompiledDefs", f), os.path.join(scratch, "data", "CompiledDefs", f))
    def snapshot():
        result = {}
        for path in Path(scratch).rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(scratch).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    cli = os.path.join(ROOT, "build", "forge.exe")
    gui = os.path.join(ROOT, "build", "FableForge.exe")
    ok = True
    pixel_difference = None
    # the id an append gets: one past the scratch bank's highest (6294 on retail, more after mesh imports)
    listed = subprocess.run([cli, "textures", "--bank", "GBANK_MAIN_PC", "--install", scratch], capture_output=True, text=True, timeout=90)
    assert listed.returncode == 0, listed.stdout + listed.stderr
    ids = [int(m.group(1)) for m in re.finditer(r"^\s*(\d+)\s+[A-Z0-9_]", listed.stdout, re.M)]
    next_id = max(ids) + 1
    script = work / "textures.txt"
    script.write_text((Path(ROOT) / "tests/ui/textures.txt").read_text(encoding='utf-8')
        .replace('build/ui_textures_install', Path(scratch).as_posix())
        .replace('build/ui/', images.as_posix() + '/'), encoding='utf-8')
    r = subprocess.run([gui, "--auto", str(script), "--install", a.root], capture_output=True, text=True, cwd=ROOT, timeout=240)
    (work / 'gui_output.log').write_text(r.stdout + r.stderr, encoding='utf-8')
    log = str(script) + ".log"
    if r.returncode != 0 or not Path(log).is_file() or 'RESULT PASS' not in Path(log).read_text(encoding='utf-8', errors='replace'):
        print("GUI textures script failed:")
        if os.path.exists(log):
            print(chr(10).join(open(log, encoding="utf-8", errors="replace").read().splitlines()[-15:]))
        ok = False
    else:
        print("GUI textures script PASS")
    if not any(os.path.exists(os.path.join(scratch, "data", "graphics", "pc", "textures.big" + sfx)) for sfx in (".forge-orig", ".atlas-orig")):
        print("no textures.big.forge-orig backup"); ok = False
    r = subprocess.run([cli, "textures", "ATLAS_UI_TEX", "--install", scratch], capture_output=True, text=True)
    if "ATLAS_UI_TEX" not in r.stdout or "DXT3" not in r.stdout:
        print("the added texture is not listed:", r.stdout[-300:]); ok = False
    elif not re.search(rf"^\s*{next_id}\s+ATLAS_UI_TEX", r.stdout, re.M):
        print(f"the added texture did not get id {next_id}:", r.stdout[-300:]); ok = False
    # the replaced slot decodes back to (nearly) the same pixels
    # by the name the list prints (the retail symbol is a [\DEV\...\NAME.TGA] path); the id must work too
    r = subprocess.run([cli, "texture-export", "BARREL_BRACED_1_24",
                        str(images / "barrel_braced_2.png"), "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0:
        print("export after replace failed:", r.stderr); ok = False
    r = subprocess.run([cli, "texture-export", "786", str(images / "barrel_braced_3.png"), "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0:
        print("export by id failed:", r.stderr); ok = False
    name_export = images / "barrel_braced_2.png"
    id_export = images / "barrel_braced_3.png"
    same_export = name_export.is_file() and id_export.is_file() and name_export.read_bytes() == id_export.read_bytes()
    if not same_export:
        print("export by texture name and id differ"); ok = False
    # the CLI replace by listed name (the GUI path above replaced by row); a missing name must say why
    r = subprocess.run([cli, "texture-replace", "BARREL_BRACED_1_24", str(images / "barrel_braced.png"), "--install", scratch], capture_output=True, text=True)
    if r.returncode != 0 or "replaced BARREL_BRACED_1_24" not in r.stdout:
        print("CLI replace by listed name failed:", r.stderr, r.stdout[-300:]); ok = False
    r = subprocess.run([cli, "texture-replace", "NO_SUCH_TEXTURE_XYZ", str(images / "barrel_braced.png"), "--install", scratch], capture_output=True, text=True)
    if r.returncode == 0 or "no texture named" not in r.stderr + r.stdout:
        print("replace of a missing name must fail with a reason:", r.stderr, r.stdout[-200:]); ok = False
    else:
        from PIL import Image
        import numpy as np
        p1 = np.asarray(Image.open(str(images / "barrel_braced.png")).convert("RGB"), int)
        p2 = np.asarray(Image.open(str(images / "barrel_braced_2.png")).convert("RGB"), int)
        diff = float('inf') if p1.shape != p2.shape else float(np.abs(p1 - p2).mean())
        pixel_difference = diff
        if p1.shape != p2.shape or diff > 2.0:
            print(f"replaced texture drifted: shape {p1.shape} vs {p2.shape}, mean diff {diff:.2f}"); ok = False
        else:
            print(f"replace round trip: mean abs diff {diff:.2f}")
    r = subprocess.run([cli, "restore", "--forget", "--install", scratch], capture_output=True, text=True, timeout=90)
    (work / 'restore.log').write_text(r.stdout + r.stderr, encoding='utf-8')
    restored = r.returncode == 0 and snapshot() == original
    if not restored:
        print("texture workflow did not Restore exactly:", r.stdout, r.stderr); ok = False
    (work / 'report.json').write_text(json.dumps({'passed': ok, 'restored_exactly': restored, 'new_id': next_id,
        'name_id_exports_equal': same_export, 'pixel_mean_absolute_difference': pixel_difference}, indent=2), encoding='utf-8')
    if not a.keep and ok:
        assert work.parent == (Path(ROOT) / "build").resolve()
        shutil.rmtree(work)
    print("textures test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
