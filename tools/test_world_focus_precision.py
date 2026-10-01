"""Hidden matched-pose check: orbit focus must not change rendered world pixels."""
import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/FableForge.exe')
    parser.add_argument('--baseline', action='store_true', help='Report pre-fix differences without requiring equality')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    poses = [(3398.75, 3612.5, 130, .46365, .5), (3550, 825, 90, .8, .4),
             (850, 4220, 120, .8, .35), (2000, 3440, 700, .97481, .95055)]
    distances = [.5, 5, 200, 8000]
    lines = ['wait_maps', 'wait_ready', 'set uiscale 1', 'set preview_water 0',
             'set world_aa 1', 'set world_auto_detail 0', 'set world_detail_limit 6',
             'set world_detail 0', 'world_tab 1', 'wait_world_tiles', 'set world_3d 1',
             'wait_world_tiles', 'mouse_move 20 20', 'clear_toasts']
    cases = [(detail, index) for detail in [0, 1] for index in range(4 if detail == 0 else 2)]
    for detail, index in cases:
        for sample, distance in enumerate(distances):
            lines += [f'world_camera 0 0 0 0 0 {distance}',
                      'world_pose ' + ' '.join(map(str, poses[index])),
                      f'set world_detail {detail}']
            if detail:
                lines += ['wait_world_detail']
            lines += ['frames 3', 'dump_state',
                      f'screenshot {(out/f"case-{detail}-{index}-{sample}.png").as_posix()}']
    lines += ['quit']
    script = out/'focus.txt'
    script.write_text('\n'.join(lines)+'\n')
    subprocess.run([sys.executable, str(ROOT/'tools/test_world_streaming.py'), '--runs', '1',
                    '--exe', str(args.exe.resolve()), '--script', str(script), '--frame-ms', '16',
                    '--size', '1280x800'], cwd=ROOT, check=True)
    log = Path(str(script)+'.log').read_text()
    state = {key: [float(v) for v in re.findall(key+r'=([\d.]+)', log)]
             for key in ['viewport_x', 'viewport_y', 'viewport_width', 'viewport_height']}
    report = []
    for case, (detail, index) in enumerate(cases):
        images = []
        boxes = []
        for sample in range(len(distances)):
            x, y, w, h = [state[key][case*len(distances)+sample] for key in state]
            box = (math.ceil(x)+2, math.ceil(y)+35, math.floor(x+w)-2, math.floor(y+h)-2)
            boxes.append(box)
            images.append(np.array(Image.open(out/f'case-{detail}-{index}-{sample}.png').convert('RGB').crop(box)))
        if len(set(boxes)) != 1:
            raise RuntimeError('Viewport dimensions changed')
        if np.count_nonzero(np.any(images[0] != [19, 18, 26], axis=2)) < 1000:
            raise RuntimeError('World fixture is empty')
        changed = [int(np.any(im != images[0], axis=2).sum()) for im in images[1:]]
        report.append(dict(detail=detail, pose=index, changed_pixels=changed, box=boxes[0]))
    (out/'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    if not args.baseline and any(any(row['changed_pixels']) for row in report):
        raise RuntimeError('Orbit focus changed the rendered world at a fixed pose')


if __name__ == '__main__':
    main()
