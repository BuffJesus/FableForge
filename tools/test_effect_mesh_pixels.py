"""Check mesh-preview screenshots produced by tests/ui/effect_meshes.txt."""
from pathlib import Path
import argparse

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--rotation", action="store_true", help="Check the DAZED01STAR rotation fixture instead")
args = parser.parse_args()
prefix = "effect_rotation" if args.rotation else "effect_mesh"
start, advanced = ("30", "45") if args.rotation else ("15", "30")
images = {
    name: np.array(Image.open(ROOT / f"build/ui/{prefix}_{name}.png").convert("RGB"))
    for name in (start, "paused", advanced, "reset")
}
background = np.array([6, 9, 13], dtype=np.uint8)
y, x = np.where(np.all(images[start] == background, axis=2))
assert len(x), "Mesh preview background not found"
x0, y0, x1, y1 = int(x.min()), int(y.min()), int(x.max() + 1), int(y.max() + 1)
crops = {name: pixels[y0:y1, x0:x1] for name, pixels in images.items()}
visible = np.any(crops[start] != background, axis=2).sum()
assert visible > 50, f"Preview contains too little visible geometry: {visible} pixels"
assert np.array_equal(crops[start], crops["paused"]), "Paused mesh preview changed"
assert np.array_equal(crops[start], crops["reset"]), "Mesh restart is not deterministic"
changed = np.any(crops[start] != crops[advanced], axis=2).sum()
assert changed > 50, f"Mesh animation does not visibly progress: {changed} pixels"
print(f"PASS: {visible} visible pixels; pause/restart identical; animation changes {changed} pixels; viewport {(x0,y0,x1,y1)}")
