"""Check that retail phoneme poses visibly move each human preview mouth."""
from pathlib import Path

import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1] / "build/ui"
for name in ("bandit", "female", "male", "child"):
    neutral = np.asarray(Image.open(root / f"head_eyes_{name}.png").convert("RGB"))
    posed = np.asarray(Image.open(root / f"head_pose_{name}.png").convert("RGB"))
    assert neutral.shape == posed.shape, f"{name}: capture sizes differ"
    height, width = neutral.shape[:2]
    assert width >= 900 and height >= 700, f"{name}: unexpected capture size"
    x0, x1 = int(width * 0.42), int(width * 0.55)
    y0, y1 = int(height * 0.38), int(height * 0.52)
    changed = int(np.any(neutral[y0:y1, x0:x1] != posed[y0:y1, x0:x1], axis=2).sum())
    assert changed > 500, f"{name}: phoneme mouth barely changed ({changed} pixels)"
    print(f"{name}: {changed} changed mouth pixels")
print("PASS: all four retail human heads visibly follow the selected phoneme")
