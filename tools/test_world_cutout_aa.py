"""Compare cutout edge AA with identical MSAA targets, poses and texture payloads."""
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
    poses = {'guild': (3398.75, 3612.5, 130, .46365, .5),
             'oakvale': (3550, 825, 90, .8, .4)}
    lines = ['wait_maps', 'wait_ready', 'set uiscale 1', 'set preview_water 0',
             'world_memory_sample 8589934592 2147483648', 'set world_auto_detail 0',
             'set world_detail_limit 6', 'set world_detail 0', 'world_tab 1',
             'wait_world_tiles', 'set world_3d 1', 'wait_world_tiles',
             'mouse_move 20 20', 'clear_toasts']
    labels = []
    for name, pose in poses.items():
        lines += ['set world_detail 0', 'world_pose ' + ' '.join(map(str, pose)),
                  'set world_detail 1', 'wait_world_detail']
        for samples in (1, 2, 4):
            for enabled in (0, 1):
                label = f'{name}-{samples}x-{enabled}'
                labels.append(label)
                lines += [f'set world_aa {samples}', f'set world_cutout_aa {enabled}',
                          'frames 3', f'assert_state world_aa_samples {samples}', 'dump_state']
                for step in range(12):
                    lines += [f'world_look {pose[3] + step * .0002:.6f} {pose[4]}', 'frames 2',
                              f'screenshot {(out / f"{label}-{step:02}.png").as_posix()}']
                lines += [f'world_look {pose[3]} {pose[4]}', 'set world_cutout_mask 1', 'frames 2',
                          f'screenshot {(out / f"{label}-mask.png").as_posix()}']
                if enabled and samples > 1:
                    for progress in (0, .25, .5, .75, 1):
                        lines += [f'world_transition_hold {progress}', 'frames 2',
                                  f'screenshot {(out / f"{label}-fade-{progress}.png").as_posix()}']
                    lines += ['world_transition_hold auto']
                lines += ['set world_cutout_mask 0']
    lines += ['quit']
    script = out / 'capture.txt'
    script.write_text('\n'.join(lines) + '\n')
    subprocess.run([sys.executable, str(ROOT / 'tools/test_world_streaming.py'), '--runs', '1',
                    '--script', str(script), '--frame-ms', '16', '--size', '1440x900'],
                   cwd=ROOT, check=True)
    log = Path(str(script) + '.log').read_text()
    keys = ('viewport_x', 'viewport_y', 'viewport_width', 'viewport_height', 'texture_pool_gpu_bytes')
    states = {key: [float(v) for v in re.findall(r'\b' + key + r'=([\d.]+)', log)] for key in keys}
    if any(len(values) != len(labels) for values in states.values()):
        raise RuntimeError('Missing capture state')
    report, captures = {}, {}
    for index, label in enumerate(labels):
        x, y, w, h = [states[key][index] for key in keys[:4]]
        box = (math.ceil(x)+2, math.ceil(y)+35, math.floor(x+w)-2, math.floor(y+h)-2)
        def read(suffix):
            with Image.open(out / f'{label}-{suffix}.png') as im:
                return np.asarray(im.convert('RGB').crop(box)).astype(np.int16)
        frames = [read(f'{step:02}') for step in range(12)]
        mask = read('mask')
        # White diagnostic cutouts resolve against (19,18,26). Fractional sample
        # coverage counts proportionately instead of discarding smoothed edges.
        coverage = np.clip((mask[:,:,0] - 19) / 236., 0, 1)
        captures[label] = frames
        deltas = np.stack([np.abs(a-b).max(axis=2) for a,b in zip(frames, frames[1:])])
        report[label] = dict(box=box, coverage=float(coverage.sum()),
                             partial_pixels=int(((coverage > .01) & (coverage < .99)).sum()),
                             texture_bytes=int(states['texture_pool_gpu_bytes'][index]),
                             motion=float(deltas.mean()),
                             squared_motion=float(np.square(deltas.astype(float)).mean()),
                             large_changes=float((deltas > 80).sum(axis=(1,2)).mean()))
        if label.endswith(('2x-1', '4x-1')):
            fades = [read(f'fade-{progress}') for progress in (0, .25, .5, .75, 1)]
            if not np.array_equal(fades[-1], mask):
                raise RuntimeError(f'{label}: fade changed the settled cutout image')
            if not np.all(fades[0] == (19,18,26)):
                raise RuntimeError(f'{label}: cutouts remain at zero coverage')
            if any(np.any(a > b) for a,b in zip(fades, fades[1:])):
                raise RuntimeError(f'{label}: cutout coverage flickered during held fade')
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    for name in poses:
        for samples in (1, 2, 4):
            before, after = [report[f'{name}-{samples}x-{v}'] for v in (0, 1)]
            if before['box'] != after['box'] or before['texture_bytes'] != after['texture_bytes']:
                raise RuntimeError('Comparison changed viewport or texture payload')
            if before['coverage'] < 100 or not .85 <= after['coverage']/before['coverage'] <= 1.15:
                raise RuntimeError(f'{name}/{samples}: foliage coverage changed excessively')
            if samples == 1:
                if any(not np.array_equal(a,b) for a,b in zip(captures[f'{name}-1x-0'], captures[f'{name}-1x-1'])):
                    raise RuntimeError(f'{name}: 1x fallback pixels changed')
            elif after['partial_pixels'] <= before['partial_pixels']:
                raise RuntimeError(f'{name}/{samples}: cutout smoothing not exercised')
            # AA spreads edge changes across pixels, so mean absolute change need
            # not decrease. Check the energy and count of abrupt changes instead;
            # keep the mean in the report as a separate, unhidden diagnostic.
            if samples > 1 and (after['squared_motion'] >= before['squared_motion'] or
                                after['large_changes'] >= before['large_changes']):
                raise RuntimeError(f'{name}/{samples}: abrupt cutout motion did not improve')


if __name__ == '__main__':
    main()
