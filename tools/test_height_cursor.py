#!/usr/bin/env python3
"""Check sculpt and fractional height-key cursors at normal and compact sizes."""
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
    work = Path(tempfile.mkdtemp(prefix='height-cursor-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select StartOakValeWest', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'tour -1', 'terrain_set_all 20', 'wait_foliage',
                 'set preview_things 0', 'set preview_foliage 0', 'camera 64 110 20 0 1.5 12',
                 'set pen_exact 1', 'set pen_step 1', 'snapshot_document', 'snapshot_heights']
        for mode in [0, 1, 2, 3]:
            cases = [(0.25, 1), (1, 1), (2, 9)] if mode == 3 else [(0.25, 1), (0.5, 1), (1, 9)]
            for radius, count in cases:
                lines += [f'terrain_mode {mode}', f'brush {radius} 4', 'frames 3',
                          'mouse_move viewport', 'frames 3',
                          f'assert_state brush_cursor_vertices {count}',
                          'assert_heights_changed 0', 'assert_document_same']
                if (radius in [0.25, 1] and mode in [0, 2]) or mode == 3:
                    lines += ['clear_toasts', 'reveal slider_radius', 'frames 3',
                              'reveal slider_radius', 'frames 3', 'mouse_move viewport', 'frames 3',
                              f'assert_state brush_cursor_vertices {count}',
                              f'screenshot {dest.as_posix()}/mode{mode}-radius{radius}.png']
        # Smooth-falloff Raise and the other sculpt pens retain their own cursor.
        lines += ['terrain_mode 0', 'set pen_exact 0', 'frames 3',
                  'assert_state brush_cursor_vertices 0']
        for mode in [9, 16]:
            lines += [f'terrain_mode {mode}', 'frames 3', 'assert_state brush_cursor_vertices 0']
        for radius in [0.25, 2]:
            lines += [f'brush {radius} 4', 'reveal slider_radius', 'frames 3',
                      'reveal slider_radius', 'frames 3', 'mouse_move viewport', 'frames 3',
                      'assert_state brush_cursor_vertices 0', 'assert_heights_changed 0',
                      'assert_document_same', f'screenshot {dest.as_posix()}/noise-radius{radius}.png']
        # Height-key geometry is fractional/truncated, unlike the mouse disc.
        lines += ['terrain_mode 0', 'set pen_speed 0.5']
        for radius, count in [(0.25, 0), (0.5, 0), (1, 5), (1.5, 4), (2, 13)]:
            lines += [f'brush {radius} 4', 'camera 64 110 20 0 1.5 12',
                      'frames 3', 'mouse_move viewport', 'frames 3', 'key_down Equal', 'frames 3',
                      'assert_state height_key_active 1', f'assert_state brush_cursor_vertices {count}']
            if radius in [0.25, 1.5]:
                lines += [f'screenshot {dest.as_posix()}/height-key-radius{radius}.png']
            lines += ['key_up Equal', 'wait_foliage', 'assert_state height_key_active 0',
                      f'assert_heights_changed {int(count > 0)}']
            if count:
                lines += ['undo', 'wait_foliage']
            lines += ['assert_heights_changed 0', 'assert_document_same']
        lines += ['assert_heights_changed 0', 'assert_document_same', 'quit']
        script = dest / 'cursor.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]
        print(size, 'sculpt discs, Noise square, fractional height keys and exact snapshots/undo: PASS', flush=True)


if __name__ == '__main__':
    main()
