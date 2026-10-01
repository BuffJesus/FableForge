"""Hidden constrained-memory scenery admission and stationary stability check.

Synthetic memory telemetry selects a 256 MiB scenery tier without allocating
VRAM. A modest camera move changes priority while retaining overlapping demand.
The settled view must remain unchanged during another 240 stationary frames.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
POSE = 'world_pose 3398.75 3612.5 350 0.46365 0.5'
MOVED_POSE = 'world_pose 3548.75 3612.5 350 0.96365 0.5'
KEYS = (
    'world_scenery_maps', 'world_scenery_pending', 'world_scenery_bytes',
    'world_scenery_failures', 'world_scenery_blocked', 'world_scenery_memory_limit',
    'world_scenery_drawn_parts', 'world_scenery_priority_evictions',
    'world_detail_maps', 'world_detail_failures', 'texture_pool_gpu_bytes',
    'world_eye_x', 'world_eye_y', 'world_eye_height',
    'world_camera_yaw', 'world_camera_pitch',
    'viewport_x', 'viewport_y', 'viewport_width', 'viewport_height',
)
NAME_KEYS = ('world_scenery_names', 'world_detail_names')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/FableForge.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=300,
                        help='Native process deadline in seconds')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    labels = []
    lines = ['wait_maps', 'wait_ready', 'set world_detail 0',
             'set world_auto_detail 0', 'set world_detail_limit 1',
             'set world_detail_radius 1000', 'set world_aa 1',
             'set preview_water 0', 'set world_scenery 1', 'set world_culling 1',
             'set world_objects 1', 'set world_plants 1', 'set world_creatures 0',
             'set world_object_lods 1', 'set world_terrain_lods 1',
             'world_memory_sample 1073741824 0', 'world_tab 1', 'wait_world_tiles',
             'world_camera 3398.75 3612.5 0 0.46365 0.5 200', POSE,
             'set world_3d 1', 'wait_world_tiles', 'set world_detail 1']

    def capture(label):
        lines.extend(['wait_world_detail', 'wait_world_scenery',
                      'mouse_move 20 20', 'clear_toasts', 'frames 30',
                      'wait_world_detail', 'wait_world_scenery', 'dump_state',
                      f'screenshot {(out / (label + ".png")).as_posix()}'])
        labels.append(label)

    capture('before')
    lines.extend([MOVED_POSE, 'frames 3'])
    capture('moved')
    lines.append('frames 240')
    capture('stationary')
    lines.append('quit')
    script = out / 'priority.txt'
    script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    startup = None
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    run = subprocess.run([str(args.exe.resolve()), '--auto', str(script),
                          '--auto-frame-ms', '33', '--size', '1280x800'],
                         cwd=ROOT, startupinfo=startup, timeout=args.timeout,
                         creationflags=getattr(subprocess, 'BELOW_NORMAL_PRIORITY_CLASS', 0))
    log_path = Path(str(script) + '.log')
    log = log_path.read_text(encoding='utf-8', errors='replace') if log_path.exists() else ''
    if run.returncode or 'RESULT PASS' not in log:
        raise RuntimeError(f'Native priority check failed (exit {run.returncode}):\n{log[-3000:]}')
    series = {key: [float(v) for v in re.findall(r'\b' + re.escape(key) + r'=([-+\d.eE]+)', log)]
              for key in KEYS}
    for key in NAME_KEYS:
        series[key] = [v.strip() for v in re.findall(r'\b' + re.escape(key) + r'=([^\r\n]*)', log)]
    if any(len(values) != len(labels) for values in series.values()):
        raise RuntimeError('Missing or ambiguous priority state captures')
    states = {label: {key: values[i] for key, values in series.items()}
              for i, label in enumerate(labels)}
    report = {'states': states, 'initial_camera': POSE, 'moved_camera': MOVED_POSE,
              'stationary_frames': 240, 'memory_override': {'budget': 1073741824, 'usage': 0}}
    # Preserve diagnostic state before gates, including a fixture that does not
    # provoke admission or an implementation that never settles correctly.
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    before, moved, stationary = [states[label] for label in labels]
    for label, state in states.items():
        if state['world_scenery_memory_limit'] != 256 * 1024 * 1024:
            raise RuntimeError(f'{label}: synthetic telemetry did not select the 256 MiB scenery tier')
        if state['world_scenery_bytes'] > state['world_scenery_memory_limit']:
            raise RuntimeError(f'{label}: scenery exceeded its memory budget')
        if state['world_scenery_pending'] > state['world_scenery_blocked']:
            raise RuntimeError(f'{label}: unconstrained scenery demand did not settle')
        if state['world_scenery_failures'] or state['world_detail_failures']:
            raise RuntimeError(f'{label}: preparation or upload failure')
        if state['world_scenery_maps'] <= 0 or state['world_scenery_drawn_parts'] <= 0:
            raise RuntimeError(f'{label}: fixture has no rendered distant scenery')
    if moved['world_scenery_priority_evictions'] <= before['world_scenery_priority_evictions']:
        raise RuntimeError('Camera move did not exercise a priority admission eviction; inspect fixture')
    before_names = set(before['world_scenery_names'].split(','))
    moved_names = set(moved['world_scenery_names'].split(','))
    if not (before_names & moved_names) or not (moved_names - before_names):
        raise RuntimeError('Move did not retain overlapping residency and admit new scenery')
    stable_keys = ('world_scenery_names', 'world_scenery_bytes', 'world_scenery_drawn_parts',
                   'world_scenery_priority_evictions', 'world_scenery_maps',
                   'world_detail_names', 'world_eye_x', 'world_eye_y', 'world_eye_height',
                   'world_camera_yaw', 'world_camera_pitch')
    for key in stable_keys:
        if moved[key] != stationary[key]:
            raise RuntimeError(f'Stationary scenery changed {key}; possible repeated admission/eviction')

    def pixels(label):
        state = states[label]
        x, y, w, h = (state[key] for key in KEYS[-4:])
        box = (int(x) + 2, int(y) + 35, int(x + w) - 2, int(y + h) - 2)
        with Image.open(out / (label + '.png')) as image:
            return box, np.asarray(image.convert('RGB').crop(box))

    box, image = pixels('moved')
    later_box, later_image = pixels('stationary')
    if box != later_box or not np.array_equal(image, later_image):
        raise RuntimeError('Settled stationary viewport changed during the 240-frame stability check')
    report['stationary_pixels_identical'] = True
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
