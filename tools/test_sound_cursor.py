#!/usr/bin/env python3
"""Check Sound's rounded vertex pen, cell cursor, sampling and undo at both sizes."""
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
    work = Path(tempfile.mkdtemp(prefix='sound-cursor-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select StartOakValeWest', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'tour -1', 'terrain_set_all 20', 'wait_foliage',
                 'set preview_things 0', 'set preview_foliage 0', 'terrain_mode 11',
                 'set sound_index 0', 'reveal btn_fill_sound', 'frames 3',
                 'reveal btn_fill_sound', 'frames 3', 'click btn_fill_sound', 'frames 3',
                 'snapshot_sounds', 'snapshot_document', 'snapshot_heights', 'set sound_index 1',
                 'assert_state sound_index 1']
        for x, y, radius, count in [(64, 110, .25, 1), (64, 110, 1, 2), (64, 108, 1, 4)]:
            lines += [f'brush {radius} 4', f'camera {x} {y} 20 0 1.5 30',
                      'clear_toasts', 'reveal slider_radius', 'frames 3',
                      'reveal slider_radius', 'frames 3', 'mouse_move viewport', 'frames 3',
                      f'assert_state brush_cursor_sound_cells {count}',
                      'assert_sound_changes 0', 'assert_document_same',
                      f'screenshot {dest.as_posix()}/sound-{count}-cells.png',
                      'mouse_down Left', 'frames 3', 'mouse_up Left', 'wait_foliage',
                      f'assert_sound_changes {count}', 'assert_heights_changed 0', 'assert_document_same',
                      # Ctrl+click samples instead of repainting; the same grid value is picked.
                      'set sound_index 0', 'key_down Ctrl', 'mouse_down Left', 'frames 2',
                      'mouse_up Left', 'key_up Ctrl', 'frames 3', 'assert_state sound_index 1',
                      f'assert_sound_changes {count}', 'undo', 'wait_foliage',
                      'assert_sound_changes 0', 'assert_heights_changed 0', 'assert_document_same']
        lines += ['terrain_mode 10', 'frames 3', 'assert_state brush_cursor_sound_cells 0', 'quit']
        script = dest / 'sound.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=150,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]
        print(size, '1/2/4 sound cells, Ctrl sampling and exact undo: PASS', flush=True)


if __name__ == '__main__':
    main()
