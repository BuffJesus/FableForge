"""Hidden, deterministic cancellation checks; hold a worker after terrain preparation."""
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
    lines = ['wait_maps', 'wait_ready', 'set world_detail 0', 'set world_auto_detail 0',
             'set world_detail_limit 1', 'set world_aa 1', 'set preview_water 0',
             'world_tab 1', 'wait_world_tiles', 'set world_3d 1', 'wait_world_tiles',
             'world_pose 927 4220 150 0.8 0.35', 'mouse_move 20 20', 'clear_toasts',
             'set world_detail_hold_prepare HookCoast', 'set world_detail 1',
             'wait_world_detail_held', 'assert_state world_detail_maps 0',
             'assert_state world_cutout_cache_builds 0',
             # The held worker must observe changed demand without a release command.
             'world_pose 930 4220 150 0.8 0.35', 'wait_world_detail',
             'assert_state world_detail_cancelled 1', 'assert_state world_detail_loads 2',
             'assert_state world_detail_names Hookcoast_Filler_02',
             'set world_detail 0', 'world_pose 927 4220 150 0.8 0.35',
             'set world_detail 1', 'wait_world_detail_held',
             'set world_objects 0', 'set world_detail_hold_prepare -', 'wait_world_detail',
             'assert_state world_detail_cancelled 2', 'assert_state world_detail_last_objects 0',
             'assert_state world_detail_names HookCoast', 'set world_objects 1',
             'wait_world_detail', 'wait_world_scenery', 'frames 3', 'dump_state',
             f'screenshot {(out / "before.png").as_posix()}']
    for count, action in enumerate(('set world_detail 0', 'set world_3d 0', 'world_tab 0'), 3):
        lines += ['set world_detail 0', 'world_tab 1', 'set world_3d 1',
                  'set world_detail_hold_prepare HookCoast', 'set world_detail 1',
                  'wait_world_detail_held', action, 'wait_world_detail_idle',
                  f'assert_state world_detail_cancelled {count}',
                  'assert_state world_detail_maps 0', 'assert_state world_detail_failures 0']
    lines += ['set world_detail 0', 'set world_detail_hold_prepare -', 'world_tab 1',
              'set world_3d 1', 'wait_world_tiles', 'set world_detail 1', 'wait_world_detail',
              'assert_state world_detail_names HookCoast', 'assert_state world_detail_deferred 0',
              'assert_state world_detail_failures 0', 'wait_world_scenery', 'frames 3', 'dump_state',
              f'screenshot {(out / "after.png").as_posix()}',
              # Native process exit must join a held worker without a manual release.
              'set world_detail 0', 'set world_detail_hold_prepare HookCoast',
              'set world_detail 1', 'wait_world_detail_held', 'quit']
    script = out / 'cancel.txt'
    script.write_text('\n'.join(lines) + '\n')
    subprocess.run([sys.executable, str(ROOT / 'tools/test_world_streaming.py'), '--runs', '1',
                    '--script', str(script), '--frame-ms', '16', '--size', '1280x800'],
                   cwd=ROOT, check=True)
    log = Path(str(script) + '.log').read_text()
    keys = ['world_detail_cancelled', 'world_detail_failures', 'texture_pool_gpu_bytes',
            'viewport_x', 'viewport_y', 'viewport_width', 'viewport_height']
    values = {key: [float(v) for v in re.findall(r'\b' + key + r'=([\d.]+)', log)] for key in keys}
    if any(len(v) != 2 for v in values.values()):
        raise RuntimeError('Missing state captures')
    report = {label: {key: values[key][i] for key in keys} for i,label in enumerate(('before','after'))}
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    images, boxes = [], []
    for label, state in report.items():
        x,y,w,h = [state[key] for key in keys[-4:]]
        box = (int(x)+2,int(y)+35,int(x+w)-2,int(y+h)-2)
        boxes.append(box)
        with Image.open(out / f'{label}.png') as im:
            images.append(np.asarray(im.convert('RGB').crop(box)))
    if boxes[0] != boxes[1] or not np.array_equal(*images):
        raise RuntimeError('Cancellation/re-entry changed settled world pixels')
    if values['texture_pool_gpu_bytes'][0] != values['texture_pool_gpu_bytes'][1]:
        raise RuntimeError('Cancellation/re-entry changed GPU texture payload')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
