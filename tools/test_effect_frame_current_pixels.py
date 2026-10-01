"""Check the selected-tick camera shows more of ACTIVATE_SKILL_01."""
from pathlib import Path

import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1]
images = [
    np.asarray(Image.open(root / f"build/ui/effect_frame_{name}.png").convert("RGB"))
    for name in ("all", "current")
]
background = np.array([6, 9, 13], dtype=np.int16)
assert images[0].shape == images[1].shape == (861, 1424, 3), "Unexpected UI capture size"
x0, y0, x1, y1 = 293, 267, 1053, 496
crops = [pixels[y0:y1, x0:x1].astype(np.int16) for pixels in images]
assert np.all(crops[0][0, 0] == background), "FX viewport background changed"
visible = [int((np.max(np.abs(pixels - background), axis=2) > 3).sum()) for pixels in crops]
changed = int(np.any(crops[0] != crops[1], axis=2).sum())
assert visible[0] > 100, f"Full-path frame has too little geometry: {visible[0]} pixels"
assert visible[1] > visible[0] * 1.3, f"Current frame did not improve visibility: {visible}"
assert changed > 500, f"Camera framing barely changed: {changed} pixels"
print(f"PASS: visible FX pixels {visible[0]} -> {visible[1]}; changed {changed}; viewport {(x0,y0,x1,y1)}")
