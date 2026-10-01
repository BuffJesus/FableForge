"""Hidden, read-only texture sharing/lifetime and minification comparisons."""
import argparse
import json
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
    report = {}
    captures = {}
    for label, sharing, mips in [('unshared', 0, 0), ('shared', 1, 0), ('filtered', 1, 1)]:
        lines = ['wait_maps', 'wait_ready', 'set uiscale 1', 'set preview_water 0', 'set world_cutout_mips 0',
                 f'set world_texture_sharing {sharing}', f'set world_texture_mips {mips}',
                 'set world_auto_detail 0', 'set world_detail_limit 18', 'set world_detail_radius 500',
                 'set world_detail 0', 'world_tab 1', 'wait_world_tiles', 'set world_3d 1',
                 'world_pose 3398.75 3612.5 130 0.46365 0.5', 'wait_world_tiles',
                 'frames 3', 'dump_state', 'set world_detail 1', 'wait_world_detail',
                 'assert_state world_detail_maps 18', 'mouse_move 20 20', 'clear_toasts']
        for step in range(12):
            lines += [f'world_look {0.46365 + step * .0002:.6f} 0.5', 'frames 2',
                      f'screenshot {(out / f"{label}-{step:02}.png").as_posix()}']
        lines += ['dump_state', 'set world_detail 0', 'wait_texture_cleanup', 'frames 3', 'dump_state', 'quit']
        script = out / f'{label}.txt'
        script.write_text('\n'.join(lines) + '\n')
        subprocess.run([sys.executable, str(ROOT / 'tools/test_world_streaming.py'),
                        '--runs', '1', '--script', str(script), '--frame-ms', '16',
                        '--size', '1440x900'], cwd=ROOT, check=True)
        log = Path(str(script) + '.log').read_text()
        stats = {key: [int(v) for v in re.findall(key + r'=(\d+)', log)]
                 for key in ['texture_pool_allocations', 'texture_pool_gpu_bytes',
                             'texture_pool_identity_bytes', 'texture_pool_hits', 'texture_pool_mipmapped', 'texture_retired_cpu_bytes']}
        for key in ['texture_pool_allocations', 'texture_pool_gpu_bytes', 'texture_pool_identity_bytes']:
            if len(stats[key]) != 3 or stats[key][0] != stats[key][2]:
                raise RuntimeError(f'{label}: resource lifetime mismatch: {key} {stats[key]}')
        if stats["texture_retired_cpu_bytes"][-1] != 0:
            raise RuntimeError(f"{label}: retired CPU pixels remain")
        images = []
        for step in range(12):
            im = Image.open(out / f'{label}-{step:02}.png').convert('RGB')
            images.append(np.asarray(im.crop((285, 65, im.width - 356, im.height - 4))).astype(np.int16))
        captures[label] = images
        report[label] = {'resources': stats, 'mean_motion_delta': float(np.mean([
            np.abs(a - b).max(axis=2).mean() for a, b in zip(images, images[1:])]))}
    if any(a.shape != b.shape or not np.array_equal(a, b)
           for a, b in zip(captures['unshared'], captures['shared'])):
        raise RuntimeError('Sharing changed rendered pixels')
    if report['shared']['resources']['texture_pool_gpu_bytes'][1] >= report['unshared']['resources']['texture_pool_gpu_bytes'][1]:
        raise RuntimeError('Fixture did not exercise GPU texture sharing')
    if not report['filtered']['resources']['texture_pool_mipmapped'][1]:
        raise RuntimeError('Fixture did not exercise mipmapped textures')
    # Motion delta is a diagnostic, not a general image-quality or FPS score.
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
