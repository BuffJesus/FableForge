#!/usr/bin/env python3
"""Check sculpt height keys latch one ground point and leave other tools alone."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    gui = (args.gui or repo / 'build/FableForge.exe').resolve()
    work = Path(tempfile.mkdtemp(prefix='height-keys-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select StartOakValeWest', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'tour -1', 'terrain_set_all 20',
                 'wait_foliage', 'set pen_speed 0.5', 'brush 2 4',
                 'snapshot_document', 'snapshot_heights']
        for mode, key in [(m, 'Equal') for m in [0, 1, 2, 3, 9, 16]] + [(0, 'Minus')]:
            lines += [f'terrain_mode {mode}', 'camera 64 110 20 0 1.5 40', 'frames 4',
                      'mouse_move viewport', 'frames 2', f'key_down {key}', 'frames 3',
                      'assert_state height_key_active 1',
                      'assert_state_min height_key_x 63', 'assert_state_max height_key_x 65',
                      'assert_state_min height_key_y 109', 'assert_state_max height_key_y 111',
                      'camera 90 110 20 0 1.5 40', 'frames 4', 'mouse_delta 5 0', 'frames 3',
                      'assert_height 90 110 20', 'assert_state height_key_active 1',
                      # Leaving the viewport must not drop the fixed point or undo partition.
                      'mouse_move 20 20', 'frames 4', 'assert_state height_key_active 1',
                      f'key_up {key}', 'wait_foliage', 'assert_state height_key_active 0',
                      'assert_heights_changed 1', 'undo', 'wait_foliage',
                      'assert_heights_changed 0', 'assert_document_same']
        for mode in [4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15]:
            for key in ['Equal', 'Minus']:
                lines += [f'terrain_mode {mode}', 'camera 64 110 20 0 1.5 40', 'frames 3',
                          'mouse_move viewport', 'frames 2', f'key_down {key}', 'frames 3',
                          'assert_state height_key_active 0', f'key_up {key}', 'wait_foliage',
                          'assert_heights_changed 0', 'assert_document_same']
        # Switching out of Sculpt commits the existing key edit and releases its anchor.
        lines += ['terrain_mode 0', 'camera 64 110 20 0 1.5 40', 'frames 3',
                  'mouse_move viewport', 'frames 2', 'key_down Equal', 'frames 3',
                  'terrain_mode 6', 'frames 3', 'assert_state height_key_active 0',
                  'key_up Equal', 'wait_foliage', 'assert_heights_changed 1',
                  'undo', 'wait_foliage', 'assert_heights_changed 0', 'assert_document_same',
                  'quit']
        script = dest / 'keys.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(gui), '--install', str(args.install), '--size', size,
                                 '--auto', str(script)], cwd=repo, capture_output=True,
                                text=True, timeout=240,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-4000:]
        print(size, 'fixed-point keys, sculpt-only gating, tool switch and single undo: PASS', flush=True)


if __name__ == '__main__':
    main()
