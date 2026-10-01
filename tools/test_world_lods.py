"""Read-only native world LOD, cross-map culling, and recovery checks.

Requires the installed retail fixtures. Upload comparisons report frame counts;
they do not assert a hardware-independent speedup or measure rendering FPS.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
POSE = 'world_pose 3398.75 3612.5 130 0.46365 0.5'
KEYS = (
    'world_drawn_object_parts', 'world_lower_lod_parts',
    'world_object_draw_calls', 'world_object_distance',
    'world_terrain_triangles', 'world_terrain_full_triangles',
    'world_terrain_coarse_patches',
    'world_detail_upload_frames', 'world_detail_maps',
    'world_detail_failures', 'texture_pool_gpu_bytes',
    'viewport_x', 'viewport_y', 'viewport_width', 'viewport_height',
)


def capture(lines, out, label):
    lines += ['wait_world_scenery', 'mouse_move 20 20', 'clear_toasts', 'frames 30', 'dump_state',
              f'screenshot {(out / (label + ".png")).as_posix()}']


def run(args, out, upload_ms):
    out.mkdir(parents=True, exist_ok=True)
    lines = ['wait_maps', 'wait_ready', 'set world_detail 0', 'set world_scenery 0',
             'set world_auto_detail 0', 'set world_detail_limit 6',
             'set world_detail_radius 500', f'set world_detail_upload_ms {upload_ms}',
             'set world_aa 1', 'set preview_water 0', 'set world_culling 1',
             'set world_object_lods 1', 'set world_terrain_lods 1',
             'world_tab 1', 'wait_world_tiles',
             'world_camera 3398.75 3612.5 0 0.46365 0.5 200', POSE,
             'set world_3d 1', 'wait_world_tiles', 'set world_detail 1',
             'wait_world_detail', 'assert_state world_detail_maps 6',
             'assert_state world_detail_failures 0']
    labels = ['near', 'unculled', 'lod0', 'far', 'terrain0', 'close', 'restored']
    capture(lines, out, 'near')
    lines += ['set world_culling 0']
    capture(lines, out, 'unculled')
    lines += ['set world_culling 1', 'set world_object_lods 0']
    capture(lines, out, 'lod0')
    lines += ['set world_object_lods 1',
              'world_pose 3398.75 3612.5 350 0.46365 0.5', 'wait_world_detail']
    capture(lines, out, 'far')
    lines += ['set world_terrain_lods 0']
    capture(lines, out, 'terrain0')
    lines += ['set world_terrain_lods 1',
              'world_pose 3398.75 3612.5 70 0.46365 0.5', 'wait_world_detail']
    capture(lines, out, 'close')
    lines += [POSE, 'wait_world_detail', 'assert_state world_detail_maps 6',
              'assert_state world_detail_failures 0']
    capture(lines, out, 'restored')
    lines += ['quit']
    script = out / 'lods.txt'
    script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    subprocess.run([sys.executable, str(ROOT / 'tools/test_world_streaming.py'),
                    '--exe', str(args.exe.resolve()), '--runs', '1', '--script',
                    str(script), '--frame-ms', '33', '--size', '1280x800'],
                   cwd=ROOT, check=True)
    log = Path(str(script) + '.log').read_text(encoding='utf-8', errors='replace')
    values = {key: [float(v) for v in re.findall(
        r'\b' + re.escape(key) + r'=([-+\d.eE]+)', log)] for key in KEYS}
    if any(len(v) != len(labels) for v in values.values()):
        raise RuntimeError('Missing or ambiguous native state captures')
    states = {label: {key: values[key][i] for key in KEYS}
              for i, label in enumerate(labels)}
    (out / 'report.json').write_text(json.dumps(states, indent=2) + '\n')
    if states['near']['world_lower_lod_parts'] <= 0:
        raise RuntimeError('Retail six-map scene did not draw an authored lower LOD')
    if states['lod0']['world_lower_lod_parts'] != 0:
        raise RuntimeError('Disabling object LODs still selected lower geometry')
    far = states['far']
    if (far['world_terrain_coarse_patches'] <= 0 or
            far['world_terrain_triangles'] >= far['world_terrain_full_triangles']):
        raise RuntimeError('Far camera did not select reduced terrain geometry')
    terrain0 = states['terrain0']
    if (terrain0['world_terrain_coarse_patches'] != 0 or
            terrain0['world_terrain_triangles'] != terrain0['world_terrain_full_triangles']):
        raise RuntimeError('Disabling terrain LODs did not restore full geometry')
    for label in labels:
        if states[label]['world_detail_failures'] != 0:
            raise RuntimeError(f'{label}: detail preparation failed')
    compare_pixels(out, 'near', states['near'], out, 'unculled', states['unculled'])
    compare_pixels(out, 'near', states['near'], out, 'restored', states['restored'])
    if states['near']['texture_pool_gpu_bytes'] != states['restored']['texture_pool_gpu_bytes']:
        raise RuntimeError('Camera recovery changed settled GPU texture payload')
    print(json.dumps(states, indent=2))
    return states


def viewport(out, label, state):
    x, y, w, h = [state[key] for key in KEYS[-4:]]
    # Exclude the viewport's UI overlay and border, as other world pixel tests do.
    box = (int(x) + 2, int(y) + 35, int(x + w) - 2, int(y + h) - 2)
    with Image.open(out / (label + '.png')) as im:
        return box, np.asarray(im.convert('RGB').crop(box))


def compare_pixels(out_a, label_a, state_a, out_b, label_b, state_b):
    box_a, pixels_a = viewport(out_a, label_a, state_a)
    box_b, pixels_b = viewport(out_b, label_b, state_b)
    if box_a != box_b or not np.array_equal(pixels_a, pixels_b):
        raise RuntimeError(f'Settled viewport differs: {out_a.name}/{label_a} vs '
                           f'{out_b.name}/{label_b}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/FableForge.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--upload-compare', action='store_true',
                        help='Run fixed 2 ms and 4 ms upload slices in separate processes')
    args = parser.parse_args()
    out = args.output.resolve()
    if not args.upload_compare:
        run(args, out, 0)
        return
    reports = {str(ms): run(args, out / str(ms), ms) for ms in (2, 4)}
    for label in reports['2']:
        compare_pixels(out / '2', label, reports['2'][label],
                       out / '4', label, reports['4'][label])
        if reports['2'][label]['texture_pool_gpu_bytes'] != reports['4'][label]['texture_pool_gpu_bytes']:
            raise RuntimeError('Upload slice changed GPU texture payload')
    (out / 'report.json').write_text(json.dumps(reports, indent=2) + '\n')


if __name__ == '__main__':
    main()
