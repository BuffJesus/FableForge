"""Check that retail phoneme poses visibly move each human preview mouth."""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--capture-dir", type=Path, required=True)
root = parser.parse_args().capture_dir
for name in ("bandit", "female", "male", "child"):
    neutral = np.asarray(Image.open(root / f"head_eyes_{name}.preview.png").convert("RGB"))
    posed = np.asarray(Image.open(root / f"head_pose_{name}.preview.png").convert("RGB"))
    assert neutral.shape == posed.shape, f"{name}: capture sizes differ"
    height, width = neutral.shape[:2]
    assert width == height and width >= 300, f"{name}: unexpected preview size"
    x0, x1 = int(width * 0.35), int(width * 0.65)
    y0, y1 = int(height * 0.67), int(height * 0.86)
    changed = int(np.any(neutral[y0:y1, x0:x1] != posed[y0:y1, x0:x1], axis=2).sum())
    assert changed > 500, f"{name}: phoneme mouth barely changed ({changed} pixels)"
    print(f"{name}: {changed} changed mouth pixels")
print("PASS: all four retail human heads visibly follow the selected phoneme")
