"""Check the FX grid using the captured preview bounds, not fixed screen coordinates."""
import argparse
from pathlib import Path
import re
import numpy as np
from PIL import Image


def captured_previews(root, log, names):
    rects = re.findall(r'widget_rect effect_preview_image ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log.read_text())
    assert len(rects) == len(names), (log, len(rects), len(names))
    crops = []
    for name, values in zip(names, rects):
        rect = tuple(round(float(value)) for value in values)
        image = Image.open(root / name).convert('RGB')
        assert 0 <= rect[0] < rect[2] <= image.width and 0 <= rect[1] < rect[3] <= image.height, rect
        crops.append(np.asarray(image.crop(rect)))
    assert all(crop.shape == crops[0].shape for crop in crops)
    return crops


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-dir', type=Path, default=repo/'build/ui')
    parser.add_argument('--log', type=Path, default=repo/'tests/ui/effect_grid.txt.log')
    args = parser.parse_args()
    off, on, reset = captured_previews(args.capture_dir, args.log,
        [f'effect_grid_{name}.png' for name in ('off', 'on', 'reset')])
    changed = int(np.any(off != on, axis=2).sum())
    reset_changed = int(np.any(off != reset, axis=2).sum())
    assert changed > 1000, f'Grid is barely visible: {changed} changed pixels'
    assert reset_changed == 0, f'Grid remains visible after disabling: {reset_changed} pixels'
    print(f'PASS: grid changes {changed} preview pixels; off/reset match exactly')


if __name__ == '__main__':
    main()
