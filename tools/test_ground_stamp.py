#!/usr/bin/env python3
"""Check Ground's full stamp toggle, strict vertex cursor, real painting and undo."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix='ground-stamp-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select BanditCampResidential', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'tour -1', 'terrain_set_all 20', 'wait_foliage',
                 'set preview_things 0', 'set preview_foliage 0', 'terrain_mode 6',
                 'assert_state paint_stamp 0', 'paint_theme 2', 'brush 16 20', 'terrain_stroke 64 64 1',
                 'frames 4', 'snapshot_themes', 'snapshot_heights', 'snapshot_sounds',
                 'snapshot_environments', 'snapshot_document', 'paint_theme 3',
                 'reveal check_paint_stamp', 'frames 3', 'reveal check_paint_stamp', 'frames 3',
                 'click check_paint_stamp', 'frames 3', 'assert_state paint_stamp 1']
        for radius, count in [(.25, 1), (1, 1), (1.5, 9), (2, 9)]:
            lines += [f'brush {radius} 0', 'camera 64.75 64.75 20 0 1.5 15',
                      'clear_toasts', 'reveal slider_radius', 'frames 3',
                      'reveal slider_radius', 'frames 3', 'mouse_move viewport', 'frames 3',
                      f'assert_state brush_cursor_vertices {count}', 'assert_theme_changes 0',
                      f'screenshot {dest.as_posix()}/stamp-radius{radius}.png',
                      'mouse_down Left', 'frames 3', 'assert_theme_stamp 64 64 3',
                      'mouse_up Left', 'wait_foliage', f'assert_theme_changes {count}',
                      'assert_theme_stamp 64 64 3', 'assert_heights_changed 0',
                      'assert_sound_changes 0', 'assert_environment_changes 0', 'assert_document_same',
                      'undo', 'wait_foliage', 'assert_theme_changes 0',
                      'assert_heights_changed 0', 'assert_sound_changes 0', 'assert_document_same']
        lines += ['reveal check_paint_stamp', 'frames 3', 'click check_paint_stamp', 'frames 3',
                  'assert_state paint_stamp 0', 'reveal slider_strength', 'frames 3', 'assert_widget slider_strength',
                  # The original soft brush with zero strength leaves the blends alone.
                  'terrain_stroke 64.75 64.75 1', 'frames 3', 'assert_theme_changes 0', 'quit']
        script = dest / 'stamp.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]
        print(size, 'full assignments, strict 1/9-vertex cursor, zero strength and exact undo: PASS', flush=True)


if __name__ == '__main__':
    main()
