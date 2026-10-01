"""Hidden retail checks for distant scenery memory, filters, and generation changes."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
KEYS = (
    'world_scenery_maps', 'world_scenery_pending', 'world_scenery_bytes',
    'world_scenery_failures', 'world_scenery_blocked', 'world_scenery_memory_limit',
    'world_scenery_drawn_parts', 'texture_pool_gpu_bytes', 'world_detail_maps',
    'world_detail_failures', 'viewport_x', 'viewport_y', 'viewport_width', 'viewport_height',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/FableForge.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=300,
                        help='Native process deadline; the lifecycle intentionally rebuilds scenery several times')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    labels = []
    lines = ['wait_maps', 'wait_ready', 'set world_detail 0', 'set world_auto_detail 0',
             'set world_detail_limit 1', 'set world_detail_radius 1000',
             'set world_aa 1', 'set preview_water 0', 'set world_scenery 1',
             'set world_objects 1', 'set world_plants 1', 'set world_creatures 0',
             'set world_object_lods 1', 'set world_terrain_lods 1',
             'world_memory_sample 8589934592 2147483648', 'world_tab 1', 'wait_world_tiles',
             'world_camera 3398.75 3612.5 0 0.46365 0.5 200',
             'world_pose 3398.75 3612.5 350 0.46365 0.5',
             'set world_3d 1', 'wait_world_tiles', 'set world_detail 1']

    def capture(label, wait_detail=True):
        if wait_detail:
            lines.append('wait_world_detail')
        lines.extend(['wait_world_scenery', 'mouse_move 20 20', 'clear_toasts',
                      'frames 30', 'dump_state',
                      f'screenshot {(out / (label + ".png")).as_posix()}'])
        labels.append(label)

    capture('before')
    # Synthetic telemetry exercises admission/eviction without allocating VRAM.
    lines += ['world_memory_sample 1073741824 4294967296', 'frames 3']
    capture('pressure')
    lines += ['world_memory_sample 8589934592 2147483648', 'frames 3']
    capture('memory_restored')
    lines += ['set world_scenery 0', 'frames 3']
    capture('scenery_disabled')
    # Start fresh demand, then invalidate it before waiting for completion.
    # This checks drainage, without claiming a deterministic worker checkpoint.
    lines += ['set world_scenery 1', 'frames 1', 'set world_detail 0',
              'frames 3', 'wait_world_detail_idle']
    capture('detail_disabled', wait_detail=False)
    lines += ['set world_detail 1', 'frames 3']
    capture('detail_restored')
    lines += ['set world_objects 0', 'set world_plants 0', 'frames 3']
    capture('filtered_empty')
    lines += ['set world_objects 1', 'set world_plants 1', 'frames 3']
    capture('filters_restored')
    lines += ['quit']
    script = out / 'lifecycle.txt'
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
        raise RuntimeError(f'Native lifecycle failed (exit {run.returncode}):\n{log[-3000:]}')
    values = {key: [float(v) for v in re.findall(r'\b' + key + r'=([-+\d.eE]+)', log)]
              for key in KEYS}
    names = re.findall(r'\bworld_scenery_names=([^\r\n]*)', log)
    detail_names = re.findall(r'\bworld_detail_names=([^\r\n]*)', log)
    if (len(names) != len(labels) or len(detail_names) != len(labels) or
            any(len(v) != len(labels) for v in values.values())):
        raise RuntimeError('Missing or ambiguous state captures')
    states = {label: {**{key: values[key][i] for key in KEYS},
                      'world_scenery_names': names[i].strip(),
                      'world_detail_names': detail_names[i].strip()}
              for i, label in enumerate(labels)}
    (out / 'report.json').write_text(json.dumps(states, indent=2) + '\n')
    before = states['before']
    if before['world_scenery_maps'] <= 0 or before['world_scenery_drawn_parts'] <= 0:
        raise RuntimeError('Baseline contains no visible outer scenery')
    for label, state in states.items():
        if state['world_scenery_bytes'] > state['world_scenery_memory_limit']:
            raise RuntimeError(f'{label}: outer scenery exceeds its memory budget')
        if state['world_scenery_pending'] > state['world_scenery_blocked']:
            raise RuntimeError(f'{label}: unconstrained scenery demand did not settle')
        if state['world_scenery_failures'] or state['world_detail_failures']:
            raise RuntimeError(f'{label}: preparation or upload failure')
    for label in ('pressure', 'scenery_disabled', 'detail_disabled'):
        state = states[label]
        if any(state[key] != 0 for key in ('world_scenery_maps', 'world_scenery_bytes', 'world_scenery_drawn_parts')):
            raise RuntimeError(f'{label}: outer geometry survived disable/pressure')
        if state['world_scenery_names']:
            raise RuntimeError(f'{label}: stale resident names')
    if states['pressure']['world_scenery_memory_limit'] != 0 or states['pressure']['world_scenery_blocked'] <= 0:
        raise RuntimeError('Pressure did not report memory-blocked demand')
    for label in ('scenery_disabled', 'detail_disabled'):
        if states[label]['world_scenery_pending'] != 0:
            raise RuntimeError(f'{label}: demand did not drain')
    filtered = states['filtered_empty']
    if filtered['world_scenery_drawn_parts'] != 0 or filtered['world_scenery_bytes'] != 0:
        raise RuntimeError('Disabling every object class left outer object resources')

    def pixels(label):
        state = states[label]
        x, y, w, h = (state[key] for key in KEYS[-4:])
        box = (int(x) + 2, int(y) + 35, int(x + w) - 2, int(y + h) - 2)
        with Image.open(out / (label + '.png')) as im:
            return box, np.asarray(im.convert('RGB').crop(box))

    baseline_box, baseline_pixels = pixels('before')
    def name_set(value):
        return {name.strip() for name in value.split(',') if name.strip()}

    for label in ('memory_restored', 'detail_restored', 'filters_restored'):
        state = states[label]
        if state['world_detail_names'] != before['world_detail_names']:
            raise RuntimeError(f'{label}: restored detailed-map demand differs')
        changed = name_set(state['world_scenery_names']) ^ name_set(before['world_scenery_names'])
        # A coarse fallback may be retained from initial loading beneath a fully
        # covered near map; subsequent loading can skip that invisible copy.
        # Its optional geometry/textures remain diagnostic, not an equality gate.
        if not changed <= name_set(state['world_detail_names']):
            raise RuntimeError(f'{label}: restored visible scenery residency differs')
        if state['world_scenery_drawn_parts'] != before['world_scenery_drawn_parts']:
            raise RuntimeError(f'{label}: restored distant draw count differs')
        if not changed:
            for key in ('world_scenery_bytes', 'texture_pool_gpu_bytes'):
                if state[key] != before[key]:
                    raise RuntimeError(f'{label}: unchanged residency has different {key}')
        box, image = pixels(label)
        if box != baseline_box or not np.array_equal(image, baseline_pixels):
            raise RuntimeError(f'{label}: restored viewport pixels differ')
    print(json.dumps(states, indent=2))


if __name__ == '__main__':
    main()
