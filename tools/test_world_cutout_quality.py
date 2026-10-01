"""Hidden cutout minification comparison, including rendered silhouette coverage."""
import argparse
import json
import math
import re
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
    poses = {'guild': (3398.75, 3612.5, 130, .46365, .5), 'oakvale': (3550, 825, 90, .8, .4)}
    report = {}
    for enabled in [0, 1]:
        label = 'filtered' if enabled else 'legacy'
        lines = ['wait_maps', 'wait_ready', 'set uiscale 1', 'set preview_water 0', 'set world_aa 1',
                 f'set world_cutout_mips {enabled}', 'set world_detail 0',
                 'set world_auto_detail 0', 'set world_detail_limit 18', 'set world_detail_radius 500',
                 'world_tab 1', 'wait_world_tiles', 'set world_3d 1', 'wait_world_tiles',
                 'mouse_move 20 20', 'clear_toasts']
        for name, pose in poses.items():
            lines += ['set world_detail 0', 'world_pose ' + ' '.join(map(str, pose)),
                      'set world_detail 1', 'wait_world_detail', 'dump_state']
            for step in range(12):
                lines += [f'world_look {pose[3] + step*.0002:.6f} {pose[4]}', 'frames 2',
                          f'screenshot {(out/f"{label}-{name}-{step:02}.png").as_posix()}']
            lines += [f'world_look {pose[3]} {pose[4]}', 'set world_cutout_mask 1', 'frames 2',
                      f'screenshot {(out/f"{label}-{name}-mask.png").as_posix()}', 'set world_cutout_mask 0']
        lines += ['set world_detail 0', 'wait_texture_cleanup', 'assert_state texture_retired_cpu_bytes 0', 'quit']
        script = out/f'{label}.txt'
        script.write_text('\n'.join(lines)+'\n')
        subprocess.run([sys.executable, str(ROOT/'tools/test_world_streaming.py'), '--runs', '1',
                        '--script', str(script), '--frame-ms', '16', '--size', '1440x900'], cwd=ROOT, check=True)
        log = Path(str(script)+'.log').read_text()
        state = {key: [float(v) for v in re.findall(key+r'=([\d.]+)', log)] for key in
                 ['viewport_x', 'viewport_y', 'viewport_width', 'viewport_height', 'texture_pool_cutout', 'texture_pool_gpu_bytes']}
        if enabled and not all(state['texture_pool_cutout']):
            raise RuntimeError('Fixture did not exercise filtered cutouts')
        report[label] = {}
        for index, name in enumerate(poses):
            x,y,w,h = [state[key][index] for key in ['viewport_x', 'viewport_y', 'viewport_width', 'viewport_height']]
            box = (math.ceil(x)+2, math.ceil(y)+35, math.floor(x+w)-2, math.floor(y+h)-2)
            images = [np.asarray(Image.open(out/f'{label}-{name}-{i:02}.png').convert('RGB').crop(box)).astype(np.int16) for i in range(12)]
            mask = np.asarray(Image.open(out/f'{label}-{name}-mask.png').convert('RGB').crop(box))
            coverage = int(np.all(mask == 255, axis=2).sum())
            if coverage < 100:
                raise RuntimeError('Cutout coverage diagnostic is empty')
            report[label][name] = {'box': box, 'coverage_pixels': coverage,
                                  'gpu_texture_bytes': int(state['texture_pool_gpu_bytes'][index]),
                                  'cutout_textures': int(state['texture_pool_cutout'][index]),
                                  'mean_motion_delta': float(np.mean([np.abs(a-b).max(axis=2).mean() for a,b in zip(images, images[1:])]))}
    # Always preserve evidence even if a candidate fails the acceptance thresholds.
    (out/'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    for name in poses:
        before, after = report['legacy'][name], report['filtered'][name]
        if before['box'] != after['box']:
            raise RuntimeError('Viewport dimensions changed')
        ratio = after['coverage_pixels']/before['coverage_pixels']
        if not .85 <= ratio <= 1.15:
            raise RuntimeError(f'{name}: visible cutout coverage changed too much ({ratio:.3f})')
        if after['mean_motion_delta'] >= before['mean_motion_delta']:
            raise RuntimeError(f'{name}: minification motion did not improve')


if __name__ == '__main__':
    main()
