#!/usr/bin/env python3
"""Check Environment's strict cell pen, full assignment, clearing, sampling and undo."""
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
    work = Path(tempfile.mkdtemp(prefix='environment-cursor-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select StartOakValeWest', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'tour -1', 'terrain_set_all 20', 'wait_foliage',
                 'set preview_things 0', 'set preview_foliage 0', 'terrain_mode 10',
                 'set environment_index 0', 'brush 16 4', 'terrain_stroke 64 110 1', 'frames 3',
                 'add_environment ENVIRONMENT_HAUNTED', 'assert_state_min environment_index 1',
                 'snapshot_environments', 'snapshot_themes', 'snapshot_heights', 'snapshot_sounds', 'snapshot_document']
        for x, y, radius, count in [(64.75, 110.75, .25, 1), (64.75, 110.75, 1, 1),
                                    (64.75, 110.75, 1.5, 2), (64.75, 108.75, 1.5, 4)]:
            lines += ['add_environment ENVIRONMENT_HAUNTED', f'brush {radius} 4',
                      f'camera {x} {y} 20 0 1.5 30', 'clear_toasts',
                      'reveal slider_radius', 'frames 3', 'reveal slider_radius', 'frames 3',
                      'mouse_move viewport', 'frames 3',
                      f'assert_state brush_cursor_environment_cells {count}',
                      'assert_environment_changes 0',
                      f'screenshot {dest.as_posix()}/environment-{count}-cells-radius{radius}.png',
                      'mouse_down Left', 'frames 3', 'mouse_up Left', 'wait_foliage',
                      f'assert_environment_changes {count}',
                      'set environment_index 0', 'key_down Ctrl', 'mouse_down Left', 'frames 2',
                      'mouse_up Left', 'key_up Ctrl', 'frames 3', 'assert_state_min environment_index 1',
                      f'assert_environment_changes {count}',
                      # No environment clears the same cells; each stroke is separately undoable.
                      'set environment_index 0', 'mouse_down Left', 'frames 3', 'mouse_up Left', 'wait_foliage',
                      'assert_environment_changes 0', 'undo', 'wait_foliage',
                      f'assert_environment_changes {count}', 'undo', 'wait_foliage',
                      'assert_environment_changes 0', 'assert_theme_changes 0',
                      'assert_heights_changed 0', 'assert_sound_changes 0', 'assert_document_same']
        lines += ['terrain_mode 11', 'frames 3', 'assert_state brush_cursor_environment_cells 0', 'quit']
        script = dest / 'environment.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]
        print(size, 'strict 1/2/4 environment cells, sample, clear and exact undo: PASS', flush=True)


if __name__ == '__main__':
    main()
