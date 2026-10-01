#!/usr/bin/env python3
"""Check the rendered heading compass through pitch, travel, zoom and full turns."""
import argparse
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile

import numpy as np
from PIL import Image


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='compass-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    headings = [('north', 0, (0, -1)), ('west', math.pi/2, (1, 0)),
                ('south', math.pi, (0, 1)), ('east', -math.pi/2, (-1, 0)),
                ('northwest', math.pi/4, (2**-.5, -2**-.5))]
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work/size
        dest.mkdir()
        lines = ['wait_maps', 'select Greatwood_1', 'wait_ready', 'wait_loaded',
                 f'set uiscale {scale}', 'frames 3', 'mouse_move input_filter', 'clear_toasts']
        shots = []

        def capture(name, heading, group=None):
            path = dest/(name+'.png')
            lines.extend(['frames 3', 'assert_widget viewport_compass',
                          'dump_widget viewport', 'dump_widget viewport_compass',
                          f'screenshot {path.as_posix()}'])
            shots.append((path, heading, group or name.split('_')[0]))

        for name, yaw, direction in headings:
            for pose, pitch in [('down', .6), ('level', 0), ('up', -.6),
                                ('vertical_down', 1.55), ('vertical_up', -1.55)]:
                lines.append(f'camera 30 30 0 {yaw} {pitch} 100')
                capture(name+'_'+pose, direction)
            lines.append(f'camera 100000000 100000000 1000 {yaw} .6 20000')
            capture(name+'_distant', direction)
            lines.extend(['zoom 100', 'fly 1 1 1 10'])
            capture(name+'_travel_zoom', direction)
            for _ in range(20):
                lines.append(f'orbit {2*math.pi} 0')
            capture(name+'_turns', direction)
        lines.extend(['set world_auto_detail 0', 'world_tab 1', 'set world_3d 1',
                      'wait_world_tiles', 'clear_toasts'])
        for name, yaw, direction in headings:
            for pose, pitch in [('down', .6), ('level', 0), ('up', -.6)]:
                lines.append(f'world_camera 3088 2896 0 {yaw} {pitch} 100')
                capture(name+'_world_'+pose, direction, 'world_'+name)
        # TeleporterGreatwood contains this world focus. Opening it converts eye
        # coordinates to map-local space while preserving orientation.
        lines.extend([f'world_camera 3088 2896 0 {math.pi/4} .6 100',
                      'world_open_3d TeleporterGreatwood', 'wait_loaded', 'clear_toasts'])
        capture('northwest_from_world', headings[-1][2])
        lines.extend(['quit', ''])
        script = dest/'tour.txt'
        script.write_text('\n'.join(lines), encoding='utf-8')
        result = subprocess.run([str(repo/'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-2000:]

        def rectangles(widget):
            return [tuple(map(float, match)) for match in re.findall(
                rf'widget_rect {widget} ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+)', log)]

        viewports, compasses = rectangles('viewport'), rectangles('viewport_compass')
        assert len(viewports) == len(compasses) == len(shots)
        masks = {}
        for (path, expected, heading), viewport, rect in zip(shots, viewports, compasses):
            assert viewport[0] <= rect[0] < rect[2] <= viewport[2], (path, viewport, rect)
            assert viewport[1] <= rect[1] < rect[3] <= viewport[3], (path, viewport, rect)
            box = tuple(round(value) for value in rect)
            with Image.open(path) as image:
                crop = image.crop(box).convert('RGB')
                crop.save(path.with_suffix('.compass.png'))
                rgb = np.asarray(crop).astype(float)
            h, w = rgb.shape[:2]
            yy, xx = np.mgrid[:h, :w]
            centre = ((rect[0]+rect[2])/2-box[0], (rect[1]+rect[3])/2-box[1])
            inside = (xx-centre[0])**2+(yy-centre[1])**2 < (w*.29)**2
            accent = inside & (rgb[:, :, 0] > 80) & (rgb[:, :, 2] > 180) & \
                (rgb[:, :, 2] > rgb[:, :, 0]*1.3) & (rgb[:, :, 1] < 160)
            assert accent.sum() > 8, f'{path.name}: compass needle missing'
            y, x = np.nonzero(accent)
            dx, dy = x.mean()+.5-centre[0], y.mean()+.5-centre[1]
            length = math.hypot(dx, dy)
            assert length > 1 and (dx*expected[0]+dy*expected[1])/length > .96, \
                f'{path.name}: wrong needle direction {(dx, dy)}'
            if heading in masks:
                reference = masks[heading]
                overlap = (reference & accent).sum() / (reference | accent).sum()
                assert overlap > .9, f'{path.name}: pitch/travel/zoom changed the needle ({overlap:.3f})'
            else:
                masks[heading] = accent
        print(size, len(shots), 'compass poses: stable direction, bounds and world-to-map handoff PASS', flush=True)


if __name__ == '__main__':
    main()
