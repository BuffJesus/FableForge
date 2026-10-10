#!/usr/bin/env python3
"""Exercise sound/environment overlays, controls, painting and undo without install writes."""
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
    work = Path(tempfile.mkdtemp(prefix='survey-overlay-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select StartOakValeWest', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'tour -1', 'terrain_set_all 20', 'wait_foliage',
                 'set preview_things 0', 'set preview_foliage 0', 'terrain_mode 11',
                 'set sound_index 1', 'reveal btn_fill_sound', 'frames 3', 'reveal btn_fill_sound',
                 'frames 3', 'click btn_fill_sound', 'frames 3', 'snapshot_sounds',
                 'set sound_index 0', 'reveal combo_sound', 'frames 3', 'click combo_sound', 'frames 2',
                 'click colour_sound_1', 'frames 3', 'assert_state sound_index 1',
                 'camera 64 108 20 0 1.5 30', 'brush 6 4', 'clear_toasts']

        def capture(name):
            lines.extend(['clear_toasts', 'mouse_move viewport', 'frames 3',
                          f'screenshot {dest.as_posix()}/{name}.png'])

        lines += ['set sound_overlay 0', 'frames 3', 'assert_state survey_overlay_kind 0']
        capture('sound-off')
        lines += ['set sound_overlay 1', 'frames 3', 'assert_state survey_overlay_kind 2']
        capture('sound-all')
        lines += ['set sound_overlay 2', 'set sound_index 0', 'frames 3', 'assert_state survey_overlay_filter 0']
        capture('sound-none-selected')
        lines += ['set sound_index 1', 'frames 3', 'assert_state survey_overlay_filter 1']
        capture('sound-one-selected')
        lines += ['set sound_index 2', 'frames 3', 'assert_state survey_overlay_filter 2']
        capture('sound-two-empty')
        lines += ['mouse_down Left', 'frames 3', 'mouse_up Left', 'wait_foliage']
        capture('sound-two-painted')
        for view in ['height', 'walkable', 'wireframe']:
            lines += [f'mode {view}', 'set sound_overlay 0']
            capture(view + '-off')
            lines += ['set sound_overlay 2']
            capture(view + '-on')
        lines += ['mode textured']
        lines += ['undo', 'wait_foliage', 'assert_sound_changes 0']
        capture('sound-two-undone')
        lines += ['reveal combo_sound_overlay', 'frames 3', 'click combo_sound_overlay', 'frames 2']
        capture('sound-control')
        lines += ['key_down Escape', 'frames 2', 'key_up Escape', 'terrain_mode 10', 'set environment_index 0',
                  'terrain_stroke 64 108 1', 'frames 3', 'snapshot_environments',
                  'add_environment ENVIRONMENT_HAUNTED', 'set environment_index 0',
                  'reveal combo_env_slot', 'frames 3', 'click combo_env_slot', 'frames 2',
                  'click colour_environment_1', 'frames 3', 'assert_state environment_index 1',
                  'set environment_overlay 0', 'frames 3',
                  'assert_state survey_overlay_kind 0']
        capture('environment-off')
        lines += ['reveal check_environment_overlay', 'frames 3', 'click check_environment_overlay',
                  'frames 3', 'assert_state survey_overlay_kind 1']
        capture('environment-none')
        lines += ['mouse_down Left', 'frames 3', 'mouse_up Left', 'wait_foliage']
        capture('environment-painted')
        lines += ['undo', 'wait_foliage', 'assert_environment_changes 0']
        capture('environment-undone')
        lines += ['terrain_mode 0', 'frames 3', 'assert_state survey_overlay_kind 0']
        capture('tool-exit')
        lines += ['dump_state', 'quit']
        script = dest / 'overlay.txt'
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
        crop = tuple(round(v) for v in (x + w*.3, y + h*.3, x + w*.7, y + h*.7))

        def difference(a, b):
            pixels = ImageChops.difference(Image.open(dest / (a+'.png')).convert('RGB').crop(crop),
                                          Image.open(dest / (b+'.png')).convert('RGB').crop(crop))
            return sum(max(pixel) > 3 for pixel in pixels.getdata())

        for a, b in [('sound-off', 'sound-all'), ('sound-two-empty', 'sound-two-painted'),
                     ('environment-off', 'environment-none'), ('environment-none', 'environment-painted')]:
            assert difference(a, b) > 500, (size, a, b, 'overlay produced no visible colour change')
        for view in ['height', 'walkable', 'wireframe']:
            assert difference(view+'-off', view+'-on') > 100, (size, view, 'overlay missing in view mode')
        for a, b in [('sound-off', 'sound-none-selected'), ('sound-off', 'sound-two-empty'),
                     ('sound-all', 'sound-one-selected'), ('sound-two-empty', 'sound-two-undone'),
                     ('environment-none', 'environment-undone')]:
            assert difference(a, b) == 0, (size, a, b, 'unexpected viewport difference')
        print(size, 'overlay controls, visible colours, selected filtering and exact undo: PASS', flush=True)


if __name__ == '__main__':
    main()
