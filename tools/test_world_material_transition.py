"""Fixed-pose world terrain transition diagnostic; hidden, read-only, no FPS claims."""
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
    parser.add_argument('--baseline', type=Path, help='Compare publication changes and unchanged full-detail endpoints')
    parser.add_argument('--legacy-material', action='store_true', help='Use the legacy height-only transition (colour and normal blending disabled)')
    parser.add_argument("--legacy-normals", action="store_true", help="Disable normal blending for a matched lighting comparison")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    lines = ['wait_maps', 'wait_ready', 'set uiscale 1', 'set world_auto_detail 0',
             'set preview_water 0', 'set world_detail 0', 'world_tab 1',
             'wait_world_tiles', 'set world_3d 1', 'wait_world_tiles',
             'mouse_move 20 20', 'clear_toasts']
    poses = {'guild': '3398.75 3612.5 95 0.46365 0.35',
             'oakvale': '3550 825 70 0.8 0.4'}
    lines += [f'set world_material_blend {int(not args.legacy_material)}']
    lines += [f'set world_normal_blend {int(not args.legacy_normals)}']
    for name, pose in poses.items():
        lines += ['set world_detail 0', f'world_pose {pose}', 'frames 3',
                  f'screenshot {(out / (name + "-coarse.png")).as_posix()}',
                  'world_transition_hold 0', 'set world_detail 1', 'wait_world_detail']
        for step in range(11):
            lines += [f'world_transition_hold {step / 10}', 'frames 2',
                      f'screenshot {(out / f"{name}-{step:02}.png").as_posix()}']
    lines += ['world_transition_hold auto', 'quit']
    script = out / 'transition.txt'
    script.write_text('\n'.join(lines) + '\n')
    subprocess.run([sys.executable, str(ROOT / 'tools/test_world_streaming.py'),
                    '--script', str(script), '--runs', '1', '--frame-ms', '33', '--size', '1280x800'],
                   cwd=ROOT, check=True)
    report = {}
    for name in poses:
        images = []
        for suffix in ['coarse'] + [f'{i:02}' for i in range(11)]:
            im = Image.open(out / f'{name}-{suffix}.png').convert('RGB')
            images.append(np.asarray(im.crop((285, 65, im.width-356, im.height-4))).astype(np.int16))
        steps = []
        for a, b in zip(images, images[1:]):
            delta = np.abs(a-b).max(axis=2)
            steps.append({'changed_over_40': int((delta > 40).sum()),
                          'mean_max_channel_delta': float(delta.mean())})
        report[name] = steps
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    if args.baseline:
        baseline = json.loads((args.baseline / 'report.json').read_text())
        for name in poses:
            old = baseline[name][0]['mean_max_channel_delta']
            new = report[name][0]['mean_max_channel_delta']
            print(f'{name}: publication mean delta {old:.4f} -> {new:.4f}')
            if new >= old:
                raise RuntimeError(f'{name}: publication discontinuity did not improve')
            before = Image.open(args.baseline / f'{name}-10.png').convert('RGB')
            after = Image.open(out / f'{name}-10.png').convert('RGB')
            if before.size != after.size:
                raise RuntimeError('Viewport dimensions differ; comparison is invalid')
            box = (285, 65, after.width-356, after.height-4)
            if not np.array_equal(np.asarray(before.crop(box)), np.asarray(after.crop(box))):
                raise RuntimeError(f'{name}: full-detail endpoint changed')


if __name__ == '__main__':
    main()
