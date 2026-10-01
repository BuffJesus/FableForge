"""Verify that increasing the real draw-distance slider populates other levels.

The camera stays 350 world units high near Guild. Full-detail capacity stays at
one map, so additional scenery must come from the outer streaming service.
The maximum is exercised with a GUI mouse drag; baseline/recovery use the
automation setting because numeric text entry is not currently supported.
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
POSE = 'world_pose 3398.75 3612.5 350 0.46365 0.5'
NUMERIC_KEYS = (
    'world_scenery_maps', 'world_scenery_pending', 'world_scenery_bytes',
    'world_scenery_blocked', 'world_scenery_memory_limit',
    'world_scenery_failures', 'world_detail_maps', 'world_detail_failures',
    'world_scenery_drawn_parts', 'world_eye_x', 'world_eye_y', 'world_eye_height',
    'world_camera_yaw', 'world_camera_pitch',
    'world_object_distance', 'world_detail_radius', 'world_lower_lod_parts',
    'world_drawn_object_parts', 'viewport_x', 'viewport_y',
    'viewport_width', 'viewport_height',
)
NAME_KEYS = ('world_detail_names', 'world_scenery_names')


def capture(lines, output, name):
    lines.extend(['mouse_move 20 20', 'clear_toasts', 'frames 30', 'dump_state',
                  f'screenshot {(output / (name + ".png")).as_posix()}'])


def pixels(output, label, state):
    x, y, width, height = [state[key] for key in
                          ('viewport_x', 'viewport_y', 'viewport_width', 'viewport_height')]
    # Exclude viewport labels and its border, retaining only rendered scenery.
    box = (int(x) + 2, int(y) + 35, int(x + width) - 2, int(y + height) - 2)
    with Image.open(output / (label + '.png')) as image:
        return box, np.asarray(image.convert('RGB').crop(box))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/FableForge.exe')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    lines = ['wait_maps', 'wait_ready', 'set world_detail 0',
             'set world_auto_detail 0', 'set world_detail_limit 1',
             'set world_detail_radius 250', 'set world_aa 1',
             'set preview_water 0', 'set world_culling 1',
             'set world_object_lods 1', 'set world_terrain_lods 1',
             'world_memory_sample 8589934592 2147483648',
             'world_tab 1', 'wait_world_tiles',
             'world_camera 3398.75 3612.5 0 0.46365 0.5 200', POSE,
             'set world_3d 1', 'wait_world_tiles', 'set world_detail 1',
             'wait_world_detail', 'wait_world_scenery',
             'assert_state world_detail_maps 1']
    capture(lines, output, 'low')
    lines.extend(['reveal slider_world_detail_radius', 'frames 3',
                  'mouse_move slider_world_detail_radius', 'mouse_down left',
                  'mouse_delta 1000 0', 'mouse_up left', 'frames 3',
                  'wait_world_detail', 'wait_world_scenery',
                  'assert_state world_detail_maps 1'])
    capture(lines, output, 'high')
    lines.extend(['set world_detail_radius 250', 'frames 3',
                  'wait_world_detail', 'wait_world_scenery',
                  'assert_state world_detail_maps 1'])
    capture(lines, output, 'restored')
    lines.append('quit')
    script = output / 'distance.txt'
    script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    subprocess.run([sys.executable, str(ROOT / 'tools/test_world_streaming.py'),
                    '--exe', str(args.exe.resolve()), '--runs', '1',
                    '--script', str(script), '--frame-ms', '33',
                    '--size', '1280x800'], cwd=ROOT, check=True)
    log = Path(str(script) + '.log').read_text(encoding='utf-8', errors='replace')
    labels = ('low', 'high', 'restored')
    series = {}
    for key in NUMERIC_KEYS:
        series[key] = [float(value) for value in re.findall(
            r'\b' + re.escape(key) + r'=([-+\d.eE]+)', log)]
    for key in NAME_KEYS:
        series[key] = [value.strip() for value in re.findall(
            r'\b' + re.escape(key) + r'=([^\r\n]*)', log)]
    if any(len(values) != len(labels) for values in series.values()):
        raise RuntimeError('Missing or ambiguous distance-test state captures')
    states = {label: {key: values[i] for key, values in series.items()}
              for i, label in enumerate(labels)}
    report = {'states': states,
              'distance_input': {'low': 'automation setting: 250',
                                 'high': 'real registered slider: mouse drag to maximum',
                                 'restored': 'automation setting: 250'},
              'camera': POSE,
              'memory_override': {'budget': 8589934592, 'usage': 2147483648}}
    # Preserve evidence before assertions, including on a visually unchanged run.
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    low, high, restored = [states[label] for label in labels]
    for label, state in states.items():
        expected_camera = {'world_eye_x': 3398.75, 'world_eye_y': 3612.5,
                           'world_eye_height': 350, 'world_camera_yaw': 0.46365,
                           'world_camera_pitch': 0.5}
        if any(abs(state[key] - expected) > 0.00001 for key, expected in expected_camera.items()):
            raise RuntimeError(f'{label}: camera moved during the distance-only comparison')
        if state['world_detail_maps'] != 1 or state['world_detail_names'] != low['world_detail_names']:
            raise RuntimeError(f'{label}: full-detail map set changed; comparison is not isolated')
        if (state['world_detail_failures'] or state['world_scenery_failures'] or
                state['world_scenery_pending'] > state['world_scenery_blocked']):
            raise RuntimeError(f'{label}: streaming did not settle successfully')
        if state['world_scenery_bytes'] > state['world_scenery_memory_limit']:
            raise RuntimeError(f'{label}: scenery exceeded its explicit memory budget')
    if high['world_object_distance'] != 1000 or high['world_detail_radius'] != 1000:
        raise RuntimeError('Maximum GUI slider did not set the actual draw distance to 1000')
    if any(state['world_object_distance'] != 250 for state in (low, restored)):
        raise RuntimeError('Low-distance setting did not match the actual draw distance')
    if high['world_scenery_maps'] <= low['world_scenery_maps']:
        raise RuntimeError('Increasing distance did not populate additional scenery levels')
    if (high['world_scenery_bytes'] <= 0 or high['world_lower_lod_parts'] <= 0 or
            high['world_scenery_drawn_parts'] <= 0):
        raise RuntimeError('Distant scenery did not upload and render lower LOD geometry')
    if not (set(high['world_scenery_names'].split(',')) - set(low['world_scenery_names'].split(','))):
        raise RuntimeError('No additional named levels became resident at maximum distance')
    boxes_and_pixels = {label: pixels(output, label, state) for label, state in states.items()}
    if any(box != boxes_and_pixels['low'][0] for box, _ in boxes_and_pixels.values()):
        raise RuntimeError('Viewport geometry changed during the GUI slider test')
    before, after, recovered = [boxes_and_pixels[label][1] for label in labels]
    if np.array_equal(before, after):
        raise RuntimeError('Additional scenery produced no visible change from the fixed camera')
    # Retention hysteresis may keep extra offscreen residents after returning.
    # Residency names remain in the report; visible parts and pixels must recover.
    if (restored['world_scenery_drawn_parts'] != low['world_scenery_drawn_parts'] or
            not np.array_equal(before, recovered)):
        raise RuntimeError('Returning to the original distance did not restore the settled view')
    report['changed_pixels'] = int(np.count_nonzero(np.any(before != after, axis=2)))
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
