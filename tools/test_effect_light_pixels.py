"""Check native volume screenshots from tests/ui/effect_lights.txt."""
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
images = {
    name: np.array(Image.open(ROOT / f"build/ui/effect_light_{name}.png").convert("RGB"))
    for name in ("15", "paused", "hidden", "30", "reset")
}
background = np.array([6, 9, 13], dtype=np.uint8)
y, x = np.where(np.all(images["15"] == background, axis=2))
assert len(x), "Effect viewport not found"
x0, y0, x1, y1 = int(x.min()), int(y.min()), int(x.max()+1), int(y.max()+1)
crops = {name: pixels[y0:y1, x0:x1] for name, pixels in images.items()}
assert np.array_equal(crops["15"], crops["paused"]), "Paused light preview changed"
assert np.array_equal(crops["15"], crops["reset"]), "Light restart is not deterministic"
volume_pixels = np.any(crops["15"] != crops["hidden"], axis=2).sum()
assert volume_pixels > 100, f"Light toggle changes too few pixels: {volume_pixels}"
changed = np.any(crops["15"] != crops["30"], axis=2).sum()
assert changed > 100, f"Light effect does not visibly advance: {changed}"
print(f"PASS: {volume_pixels} light-volume pixels; {changed} animated pixels; pause/restart identical")
