"""Exercise independent world Objects/Creatures controls and in-flight changes."""
import argparse
import json
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
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    lines = ['wait_maps', 'wait_ready', 'set world_detail 0', 'set world_auto_detail 0',
             'set world_detail_limit 1', 'set world_aa 1', 'set preview_water 0',
             'set world_plants 0', 'world_tab 1', 'wait_world_tiles', 'set world_3d 1',
             'wait_world_tiles', 'world_pose 3550 825 90 0.8 0.4', 'set world_detail 1', 'clear_toasts']
    cases = ['both', 'creatures', 'none', 'objects', 'both-restored', 'rapid-creatures', 'unchanged']
    for case in cases:
        if case in ['creatures', 'objects']:
            lines += ['click check_world_objects']
        elif case in ['none', 'both-restored']:
            lines += ['click check_world_creatures']
        elif case == 'rapid-creatures':
            lines += ['set world_creatures 0', 'frames 3', 'set world_objects 0',
                      'set world_creatures 1', 'frames 3', 'set world_objects 1',
                      'frames 3', 'set world_objects 0']
        elif case == 'unchanged':
            lines += ['set world_objects 0', 'set world_creatures 1']
        lines += ['wait_world_detail', 'frames 3',
                  'assert_state world_detail_names StartOakValeWest', 'dump_state',
                  f'screenshot {(out/f"{case}.png").as_posix()}']
    lines += ['quit']
    script = out/'filters.txt'
    script.write_text('\n'.join(lines)+'\n')
    subprocess.run([sys.executable, str(ROOT/'tools/test_world_streaming.py'), '--runs', '1',
                    '--script', str(script), '--frame-ms', '16'], cwd=ROOT, check=True)
    log = Path(str(script)+'.log').read_text()
    keys = ['world_detail_last_objects', 'world_detail_last_creatures', 'world_detail_loads']
    values = {key: [int(v) for v in re.findall(key+r'=(\d+)', log)] for key in keys}
    if any(len(v) != len(cases) for v in values.values()):
        raise RuntimeError('Incomplete state captures')
    report = {case: {key: values[key][i] for key in keys} for i, case in enumerate(cases)}
    (out/'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    objects = values[keys[0]]
    creatures = values[keys[1]]
    if objects[0] <= 0 or creatures[0] <= 0:
        raise RuntimeError('Fixture must contain both objects and creatures')
    if objects != [objects[0], 0, 0, objects[0], objects[0], 0, 0]:
        raise RuntimeError('Objects visibility or restored content changed incorrectly')
    if creatures != [creatures[0], creatures[0], 0, 0, creatures[0], creatures[0], creatures[0]]:
        raise RuntimeError('Creatures visibility or restored content changed incorrectly')
    if values[keys[2]][-1] != values[keys[2]][-2]:
        raise RuntimeError('Setting an unchanged filter reloaded detail')
    bounds = {key: [float(v) for v in re.findall(key+r'=([\d.]+)', log)] for key in
              ['viewport_x', 'viewport_y', 'viewport_width', 'viewport_height']}
    boxes = []
    images = {}
    for i, case in enumerate(cases):
        x, y, w, h = [bounds[key][i] for key in bounds]
        box = (int(x)+2, int(y)+35, int(x+w)-2, int(y+h)-2)
        boxes.append(box)
        images[case] = np.array(Image.open(out/f'{case}.png').convert('RGB').crop(box))
    if len(set(boxes)) != 1:
        raise RuntimeError('Viewport changed dimensions')
    for original, restored in [('both', 'both-restored'), ('creatures', 'rapid-creatures'), ('creatures', 'unchanged')]:
        if not np.array_equal(images[original], images[restored]):
            raise RuntimeError(f'{restored}: restored viewport differs from {original}')
    for visible in ['objects', 'creatures']:
        if np.array_equal(images[visible], images['none']):
            raise RuntimeError(f'{visible}: enabled group has no visible geometry in fixture')


if __name__ == '__main__':
    main()
