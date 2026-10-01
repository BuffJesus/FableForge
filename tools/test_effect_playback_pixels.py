"""Verify native playback screenshots from tests/ui/effect_playback.txt."""
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
images = {name: np.array(Image.open(ROOT / f"build/ui/effect_playback_{name}.png").convert("RGB"))
          for name in ("30", "paused", "60", "reset")}
# The independent opaque viewport has this clear colour. Its clear edges yield
# the whole image rectangle without including text, clocks or other editor UI.
y, x = np.where(np.all(images["30"] == [6, 9, 13], axis=2))
assert len(x), "Preview background not found"
x0, y0, x1, y1 = int(x.min()), int(y.min()), int(x.max()+1), int(y.max()+1)
crops = {name: image[y0:y1, x0:x1] for name, image in images.items()}
assert np.array_equal(crops["30"], crops["paused"]), "Paused preview changed"
assert np.array_equal(crops["30"], crops["reset"]), "Reset replay is not deterministic"
changed = np.any(crops["30"] != crops["60"], axis=2).sum()
assert changed > 100, f"Animation is not visibly progressing: {changed} pixels"
print(f"PASS: pause/reset identical; 30-to-60 ticks changes {changed} pixels; viewport {(x0,y0,x1,y1)}")
