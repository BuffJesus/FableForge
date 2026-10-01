"""Check that the FX ground grid changes only the toggled viewport."""
from pathlib import Path

import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1] / "build/ui"
off, on, reset = (
    np.asarray(Image.open(root / f"effect_grid_{name}.png").convert("RGB"))
    for name in ("off", "on", "reset")
)
assert off.shape == on.shape == reset.shape == (861, 1424, 3), "Unexpected UI capture size"
viewport = np.s_[267:496, 293:1053]
changed = int(np.any(off[viewport] != on[viewport], axis=2).sum())
reset_changed = int(np.any(off[viewport] != reset[viewport], axis=2).sum())
assert changed > 1000, f"Grid is barely visible: {changed} changed pixels"
assert reset_changed == 0, f"Grid remains visible after disabling: {reset_changed} pixels"
print(f"PASS: grid changes {changed} viewport pixels; off/reset match exactly")
