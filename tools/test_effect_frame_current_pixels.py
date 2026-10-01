"""Check the selected-tick camera shows more of ACTIVATE_SKILL_01."""
import argparse
from pathlib import Path
import numpy as np
from test_effect_grid_pixels import captured_previews


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-dir', type=Path, default=repo/'build/ui')
    parser.add_argument('--log', type=Path, default=repo/'tests/ui/effect_frame_current.txt.log')
    args = parser.parse_args()
    crops = [crop.astype(np.int16) for crop in captured_previews(args.capture_dir, args.log,
        ['effect_frame_all.png', 'effect_frame_current.png'])]
    background = np.array([6, 9, 13], dtype=np.int16)
    assert np.all(crops[0][0, 0] == background), 'FX preview background changed'
    visible = [int((np.max(np.abs(pixels - background), axis=2) > 3).sum()) for pixels in crops]
    changed = int(np.any(crops[0] != crops[1], axis=2).sum())
    assert visible[0] > 100, f'Full-path frame has too little geometry: {visible[0]} pixels'
    # Compare the complete preview, including pixels previously excluded by the
    # old hard-coded crop. Require a clear gain, not an exact raster-area ratio.
    assert visible[1] > visible[0] * 1.15, f'Current frame did not improve visibility: {visible}'
    assert changed > 500, f'Camera framing barely changed: {changed} pixels'
    print(f'PASS: visible FX pixels {visible[0]} -> {visible[1]}; changed {changed}')


if __name__ == '__main__':
    main()
