"""Check that the Bandit's eye patch covers its attached blue eyeball."""
from pathlib import Path

import numpy as np
from PIL import Image

path = Path(__file__).resolve().parents[1] / "build/ui/head_eyes_bandit.png"
pixels = np.asarray(Image.open(path).convert("RGB"))
height, width = pixels.shape[:2]
assert width >= 900 and height >= 700, "Unexpected Dialogue capture size"

def blue_eye(x0, x1):
    eye = pixels[int(height * 0.23):int(height * 0.43),
                 int(width * x0):int(width * x1)].astype(float)
    return int(((eye[:, :, 2] > eye[:, :, 0] * 1.25) &
                (eye[:, :, 2] > eye[:, :, 1] * 1.1) &
                (eye[:, :, 2] > 50)).sum())

covered = blue_eye(0.42, 0.47)
uncovered = blue_eye(0.49, 0.53)
assert covered < 10, f"Eye pokes through the Bandit's patch: {covered} blue pixels"
assert uncovered > 20, f"Uncovered eyeball is missing: {uncovered} blue pixels"
print(f"PASS: Bandit covered eye {covered} blue pixels; visible eye {uncovered}")
