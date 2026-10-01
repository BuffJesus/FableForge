"""Check that the Bandit's eye patch covers its attached blue eyeball."""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--capture-dir", type=Path, required=True)
path = parser.parse_args().capture_dir / "head_eyes_bandit.preview.png"
pixels = np.asarray(Image.open(path).convert("RGB"))
height, width = pixels.shape[:2]
assert width == height and width >= 300, "Unexpected Dialogue preview size"

def blue_eye(x0, x1):
    eye = pixels[int(height * 0.46):int(height * 0.57),
                 int(width * x0):int(width * x1)].astype(float)
    return int(((eye[:, :, 2] > eye[:, :, 0] * 1.25) &
                (eye[:, :, 2] > eye[:, :, 1] * 1.1) &
                (eye[:, :, 2] > 50)).sum())

covered = blue_eye(0.34, 0.45)
uncovered = blue_eye(0.53, 0.63)
assert covered < 10, f"Eye pokes through the Bandit's patch: {covered} blue pixels"
assert uncovered > 20, f"Uncovered eyeball is missing: {uncovered} blue pixels"
print(f"PASS: Bandit covered eye {covered} blue pixels; visible eye {uncovered}")
