"""Compare object arrival at held elapsed times, with identical terrain/endpoints."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    poses = {'guild': '3398.75 3612.5 95 0.46365 0.35', 'oakvale': '3550 825 70 0.8 0.4'}
    report, captures = {}, {}
    for smooth in [0, 1]:
        label = 'smooth' if smooth else 'legacy'
        duration = .6 if smooth else .25
        lines = ['wait_maps', 'wait_ready', 'set uiscale 1', 'set world_auto_detail 0',
                 'set preview_water 0', 'set world_detail 0', f'set world_smooth_objects {smooth}',
                 'world_tab 1', 'wait_world_tiles', 'set world_3d 1', 'wait_world_tiles',
                 'mouse_move 20 20', 'clear_toasts']
        for name, pose in poses.items():
            lines += ['set world_detail 0', f'world_pose {pose}', 'world_transition_hold 0',
                      'set world_detail 1', 'wait_world_detail']
            for step in range(13):
                lines += [f'world_transition_hold {min(1, step * .05 / duration):.8f}', 'frames 2',
                          f'screenshot {(out / f"{label}-{name}-{step:02}.png").as_posix()}']
        lines += ['world_transition_hold auto', 'quit']
        script = out / f'{label}.txt'
        script.write_text('\n'.join(lines) + '\n')
        subprocess.run([sys.executable, str(ROOT / 'tools/test_world_streaming.py'), '--runs', '1',
                        '--script', str(script), '--frame-ms', '16', '--size', '1440x900'],
                       cwd=ROOT, check=True)
        report[label], captures[label] = {}, {}
        for name in poses:
            images = []
            for step in range(13):
                im = Image.open(out / f'{label}-{name}-{step:02}.png').convert('RGB')
                images.append(np.asarray(im.crop((285, 65, im.width - 356, im.height - 4))).astype(np.int16))
            captures[label][name] = images
            deltas = [np.abs(a-b).max(axis=2) for a, b in zip(images, images[1:])]
            report[label][name] = {'peak_mean_delta': max(float(d.mean()) for d in deltas),
                                   'peak_changed_over_80': max(int((d > 80).sum()) for d in deltas)}
    for name in poses:
        for step in [0, 12]:
            if not np.array_equal(captures['legacy'][name][step], captures['smooth'][name][step]):
                raise RuntimeError(f'{name}: transition endpoint changed')
        if report['smooth'][name]['peak_changed_over_80'] >= report['legacy'][name]['peak_changed_over_80']:
            raise RuntimeError(f'{name}: peak object appearance discontinuity did not improve')
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
