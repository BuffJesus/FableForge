"""Hidden world recovery checks with preparation and partial GPU-upload failures."""
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
    parser.add_argument('--cutouts', action='store_true', help='Keep foliage enabled to exercise shared derived-mip lifetimes during failure/recovery')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    lines = ['wait_maps', 'wait_ready', 'set world_detail 0', 'set world_auto_detail 0',
             'set world_detail_limit 2', 'set world_objects 0', 'set world_creatures 0',
             f'set world_plants {int(args.cutouts)}', 'set world_aa 1', 'set preview_water 0',
             'world_tab 1', 'wait_world_tiles', 'set world_3d 1', 'wait_world_tiles',
             'world_pose 927 4220 150 0.8 0.35', 'mouse_move 20 20', 'clear_toasts']
    cases = []
    for stage in ['prepare', 'upload']:
        lines += ['set world_detail 0', f'set world_detail_fail_{stage} HookCoast',
                  'set world_detail 1', 'wait_world_detail_settled',
                  'assert_state world_detail_deferred 1', 'assert_state world_detail_maps 1',
                  'assert_state world_detail_names Hookcoast_Filler_02', 'wait_texture_cleanup',
                  'clear_toasts', 'frames 2', 'dump_state', f'screenshot {(out/f"{stage}-failed.png").as_posix()}',
                  'frames 10', 'dump_state', f'set world_detail_fail_{stage} -']
        cases += [f'{stage}-failed', f'{stage}-held']
        if stage == 'upload':
            lines += ['click btn_world_retry_detail']
        # Preparation recovers automatically after backoff; upload uses the UI retry.
        lines += ['wait_world_detail', 'assert_state world_detail_deferred 0',
                  'assert_state world_detail_maps 2', 'dump_state']
        cases += [f'{stage}-recovered']
    lines += ['quit']
    script = out/'recovery.txt'
    script.write_text('\n'.join(lines)+'\n')
    subprocess.run([sys.executable, str(ROOT/'tools/test_world_streaming.py'), '--runs', '1',
                    '--script', str(script), '--frame-ms', '16'], cwd=ROOT, check=True)
    log = Path(str(script)+'.log').read_text()
    keys = ['world_detail_failures', 'world_detail_loads', 'texture_pool_gpu_bytes',
            'texture_pool_identity_bytes', 'world_cutout_cache_bytes', 'viewport_x', 'viewport_y', 'viewport_width', 'viewport_height']
    values = {key: [float(v) for v in re.findall(key+r'=([\d.]+)', log)] for key in keys}
    if any(len(v) != len(cases) for v in values.values()):
        raise RuntimeError('Missing state capture')
    report = {case: {key: values[key][i] for key in keys} for i, case in enumerate(cases)}
    (out/'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    for stage in ['prepare', 'upload']:
        failed, held, recovered = [report[f'{stage}-{phase}'] for phase in ['failed', 'held', 'recovered']]
        if held['world_detail_failures']-failed['world_detail_failures'] > 1:
            raise RuntimeError('Failed map is retrying continuously')
        if recovered['world_detail_failures'] != held['world_detail_failures']:
            raise RuntimeError('Recovery introduced another failure')
    first, second = report['prepare-failed'], report['upload-failed']
    for key in ['texture_pool_gpu_bytes', 'texture_pool_identity_bytes']:
        if first[key] != second[key]:
            raise RuntimeError('Partial upload retained extra texture resources')
    if any(state['world_cutout_cache_bytes']>32*1024*1024 for state in report.values()):
        raise RuntimeError('Recovery exceeded the derived texture cache budget')
    if args.cutouts and not first['world_cutout_cache_bytes']:
        raise RuntimeError('Fixture did not populate the derived texture cache')
    images = []
    boxes = []
    for stage, state in [('prepare', first), ('upload', second)]:
        x,y,w,h = [state[key] for key in keys[-4:]]
        box = (int(x)+2, int(y)+35, int(x+w)-2, int(y+h)-2)
        boxes.append(box)
        images.append(np.array(Image.open(out/f'{stage}-failed.png').convert('RGB').crop(box)))
    if boxes[0] != boxes[1] or not np.array_equal(*images):
        raise RuntimeError('Preparation and upload failure changed overview coverage')


if __name__ == '__main__':
    main()
