#!/usr/bin/env python3
"""GUI smoke test: drive FableForge with an automation script, then check
the script log (RESULT PASS), the exported GLB (structural validation shared
with retail_smoke.py) and the screenshots (pixel assertions: the viewport
actually shows terrain, view modes differ, the accent colour is present, the
filter narrows the list).

  python tools/ui_smoke.py [--exe build/FableForge.exe] [--script tests/ui/smoke.txt]
"""
import argparse, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(__file__))
from retail_smoke import parse_glb, validate  # noqa: E402

try:
    from PIL import Image
except ImportError:
    Image = None

# the inner part of the 3D view as fractions of the window: the panels are fixed-width, so a
# window the desktop clamped (a fullscreen game at 1024x768 shrinks it) still lands inside
VIEWPORT_FRAC = (0.25, 0.16, 0.70, 0.85)
def viewport(img):
    w, h = img.size
    return (int(w * VIEWPORT_FRAC[0]), int(h * VIEWPORT_FRAC[1]), int(w * VIEWPORT_FRAC[2]), int(h * VIEWPORT_FRAC[3]))
ACCENT = (0x8B, 0x5C, 0xF6)

def stats(img, box):
    """Return (distinct colour count, mean RGB, fraction of near-background pixels) inside box."""
    crop = img.crop(box).convert("RGB")
    small = crop.resize((max(1, crop.width // 4), max(1, crop.height // 4)))
    px = list(small.getdata())
    distinct = len(set(px))
    mean = tuple(sum(c[i] for c in px) / len(px) for i in range(3))
    bg = sum(1 for c in px if abs(c[0] - 19) < 8 and abs(c[1] - 18) < 8 and abs(c[2] - 26) < 8) / len(px)
    return distinct, mean, bg

def has_accent(img, tol=28):
    small = img.convert("RGB").resize((img.width // 4, img.height // 4))
    return any(all(abs(c[i] - ACCENT[i]) < tol for i in range(3)) for c in small.getdata())

def diff_fraction(a, b, box):
    ca, cb = a.crop(box).convert("RGB"), b.crop(box).convert("RGB")
    ca = ca.resize((ca.width // 4, ca.height // 4)); cb = cb.resize(ca.size)
    pa, pb = list(ca.getdata()), list(cb.getdata())
    return sum(1 for x, y in zip(pa, pb) if sum(abs(x[i] - y[i]) for i in range(3)) > 30) / len(pa)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join("build", "FableForge.exe"))
    ap.add_argument("--script", default=os.path.join("tests", "ui", "smoke.txt"))
    a = ap.parse_args()
    os.makedirs(os.path.join("build", "ui"), exist_ok=True)
    for f in os.listdir(os.path.join("build", "ui")):
        os.remove(os.path.join("build", "ui", f))

    t0 = time.time()
    r = subprocess.run([a.exe, "--auto", a.script], timeout=600)
    dt = time.time() - t0
    log = open(a.script + ".log", encoding="utf-8", errors="replace").read()
    fails = []
    if r.returncode != 0 or "RESULT PASS" not in log:
        fails.append(f"script exit {r.returncode}; log tail:\n" + "\n".join(log.splitlines()[-12:]))
    print(f"script ran in {dt:.1f}s, exit {r.returncode}")

    glb = os.path.join("build", "ui", "Greatwood_1.glb")
    try:
        doc, bin_ = parse_glb(glb)
        n, tris, tex = validate(doc, bin_, True)
        print(f"glb ok: {n} verts {tris} tris albedo {tex}")
    except Exception as e:  # noqa: BLE001
        fails.append(f"glb invalid: {e}")

    if Image is None:
        print("PIL not installed; skipping pixel assertions")
    else:
        shots = {k: os.path.join("build", "ui", k) for k in
                 ["01_empty.png", "02_textured.png", "03_wireframe.png", "04_walkable.png",
                  "05_height.png", "06_orbited.png", "07_filtered.png", "08_exported.png"]}
        imgs = {}
        for k, p in shots.items():
            if not os.path.exists(p): fails.append(f"missing screenshot {k}"); continue
            imgs[k] = Image.open(p)
        if len(imgs) == len(shots):
            # empty state: viewport is mostly background
            d, mean, bg = stats(imgs["01_empty.png"], viewport(imgs["01_empty.png"]))
            if bg < 0.85: fails.append(f"01 empty viewport should be mostly background (bg={bg:.2f})")
            # textured: lots of colour, little background
            d, mean, bg = stats(imgs["02_textured.png"], viewport(imgs["02_textured.png"]))
            if d < 400: fails.append(f"02 textured viewport has too few colours ({d})")
            if bg > 0.7: fails.append(f"02 textured viewport mostly background (bg={bg:.2f})")
            if not (mean[0] > mean[2]): fails.append(f"02 textured mean colour not earthy: {mean}")
            # modes differ from textured
            for k in ["03_wireframe.png", "04_walkable.png", "05_height.png"]:
                f = diff_fraction(imgs["02_textured.png"], imgs[k], viewport(imgs[k]))
                if f < 0.25: fails.append(f"{k} barely differs from textured ({f:.2f})")
            # height ramp is violet-ish (blue > green on average) where the ground is: the lower-right of
            # the view, clear of the tree canopy that a small (clamped) window pushes into the middle
            w, h = imgs["05_height.png"].size
            ground = (int(w * 0.45), int(h * 0.55), int(w * 0.72), int(h * 0.86))
            d, mean, bg = stats(imgs["05_height.png"], ground)
            if not (mean[2] > mean[1]): fails.append(f"05 height ramp not violet: {mean}")
            # orbit changed the view
            f = diff_fraction(imgs["02_textured.png"], imgs["06_orbited.png"], viewport(imgs["06_orbited.png"]))
            if f < 0.2: fails.append(f"06 orbit/zoom did not change the view ({f:.2f})")
            # filter narrows the explorer (fewer distinct rows -> more background in list area)
            list_box = (0, 140, 300, 860)
            f = diff_fraction(imgs["02_textured.png"], imgs["07_filtered.png"], list_box)
            if f < 0.05: fails.append(f"07 filter did not change the list ({f:.2f})")
            # theme: accent colour visible
            if not has_accent(imgs["08_exported.png"]): fails.append("08 accent colour not found")
            print("pixel assertions evaluated")

    if fails:
        print("UI SMOKE FAIL"); [print("  -", f) for f in fails]; return 1
    print("UI SMOKE PASS"); return 0

if __name__ == "__main__":
    sys.exit(main())
