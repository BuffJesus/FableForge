#!/usr/bin/env python3
"""Check shore flag brushes, red overlays, Shift inversion and undo at both sizes."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
from PIL import Image, ImageChops


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix='shore-points-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select StartOakValeWest', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'tour -1', 'terrain_set_all 20', 'wait_foliage',
                 'set preview_things 0', 'set preview_foliage 0', 'terrain_mode 17',
                 'set shore_set 0', 'brush 16 0', 'terrain_stroke 64 108 0', 'frames 3',
                 'snapshot_shore', 'snapshot_environments', 'snapshot_heights',
                 'snapshot_themes', 'snapshot_sounds', 'snapshot_document',
                 'reveal chip_shore_on', 'frames 3', 'reveal chip_shore_on', 'frames 3',
                 'click chip_shore_on', 'frames 3', 'assert_state shore_set 1',
                 'assert_state survey_overlay_kind 3',
                 f'screenshot {dest.as_posix()}/shore-controls.png']
        if scale > 1:
            lines += ['terrain_mode 12', 'reveal chip_tool_shore', 'frames 3',
                      'reveal chip_tool_shore', 'frames 3', 'click chip_tool_shore',
                      'frames 3', 'assert_state survey_overlay_kind 3',
                      f'screenshot {dest.as_posix()}/shore-tool-control.png']
        for radius, count in [(.25, 1), (1, 9), (2, 21)]:
            lines += [f'brush {radius} 0', 'camera 64.75 108.75 20 0 1.5 15',
                      'clear_toasts', 'reveal slider_radius', 'frames 3',
                      'reveal slider_radius', 'frames 3', 'mouse_move viewport', 'frames 3',
                      f'assert_state brush_cursor_vertices {count}',
                      f'screenshot {dest.as_posix()}/shore-cursor-{radius}.png',
                      'mouse_down Left', 'frames 3', 'mouse_up Left', 'wait_foliage',
                      f'assert_shore_changes {count}', 'assert_heights_changed 0',
                      'assert_theme_changes 0', 'assert_environment_changes 0',
                      'assert_sound_changes 0', 'assert_document_same']
            if radius == 2:
                for view in ['textured', 'height', 'walkable', 'wireframe']:
                    lines += [f'mode {view}', 'set shore_overlay 0', 'clear_toasts', 'frames 3',
                              f'screenshot {dest.as_posix()}/{view}-off.png',
                              'set shore_overlay 1', 'frames 3',
                              f'screenshot {dest.as_posix()}/{view}-on.png']
                lines += ['mode textured']
            lines += ['undo', 'wait_foliage', 'assert_shore_changes 0']
        lines += ['brush .25 0', 'terrain_stroke 65 109 0', 'frames 3', 'assert_shore_changes 1',
                  'reveal chip_shore_off', 'frames 3', 'click chip_shore_off', 'frames 3',
                  'assert_state shore_set 0', 'mouse_move viewport',
                  'mouse_down Left', 'frames 3', 'mouse_up Left', 'wait_foliage',
                  'assert_shore_changes 0', 'undo', 'wait_foliage', 'assert_shore_changes 1',
                  'reveal chip_shore_on', 'frames 3', 'click chip_shore_on', 'frames 3',
                  'mouse_move viewport', 'key_down Shift', 'mouse_down Left', 'frames 3',
                  'mouse_up Left', 'key_up Shift', 'wait_foliage', 'assert_shore_changes 0',
                  'undo', 'wait_foliage', 'assert_shore_changes 1',
                  'undo', 'wait_foliage', 'assert_shore_changes 0',
                  'reveal check_shore_overlay', 'frames 3', 'click check_shore_overlay', 'frames 3',
                  'assert_state shore_overlay 0', 'assert_state survey_overlay_kind 0',
                  'terrain_mode 10', 'frames 3', 'assert_state survey_overlay_kind 1',
                  'terrain_mode 0', 'frames 3', 'assert_state survey_overlay_kind 0', 'dump_state', 'quit']
        script = dest / 'shore.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-4000:]
        values = dict(re.findall(r'\b(viewport_\w+)=([\d.]+)', log))
        x, y, w, h = (float(values['viewport_' + key]) for key in ['x', 'y', 'width', 'height'])
        crop = tuple(round(v) for v in (x+w*.35, y+h*.35, x+w*.65, y+h*.65))
        for view in ['textured', 'height', 'walkable', 'wireframe']:
            off = Image.open(dest / f'{view}-off.png').convert('RGB').crop(crop)
            on = Image.open(dest / f'{view}-on.png').convert('RGB').crop(crop)
            assert ImageChops.difference(off, on).getbbox(), f'{size}: missing {view} shore overlay'
        print(size, '1/9/21 cells, On/Off, Shift, four visible overlays and exact undo: PASS', flush=True)


if __name__ == '__main__':
    main()
