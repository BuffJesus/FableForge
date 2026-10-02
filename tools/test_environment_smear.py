#!/usr/bin/env python3
"""Check Environment Smear controls, sparse cursor, real strokes and exact undo."""
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
    work = Path(tempfile.mkdtemp(prefix='environment-smear-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select StartOakValeWest', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'tour -1', 'terrain_set_all 20', 'wait_foliage',
                 'set preview_things 0', 'set preview_foliage 0', 'terrain_mode 10',
                 'assert_state environment_smear 0', 'set environment_index 0',
                 'brush 16 4', 'terrain_stroke 64 108 1',
                 'add_environment ENVIRONMENT_HAUNTED', 'brush 1 4', 'terrain_stroke 64 108 1',
                 'frames 3', 'snapshot_environments', 'snapshot_heights', 'snapshot_themes',
                 'snapshot_sounds', 'snapshot_document',
                 'reveal chip_environment_smear', 'frames 3', 'reveal chip_environment_smear',
                 'frames 3', 'click chip_environment_smear', 'frames 3',
                 'assert_state environment_smear 1', 'assert_state survey_overlay_kind 1']
        for x, y, radius, count in [(65, 109, 1, 0), (65, 109, 1.25, 1), (64, 108, 1, 1)]:
            lines += [f'brush {radius} 0', f'camera {x} {y} 20 0 1.5 30', 'clear_toasts',
                      'reveal slider_radius', 'frames 3', 'reveal slider_radius', 'frames 3',
                      'mouse_move viewport', 'frames 3',
                      f'assert_state brush_cursor_environment_cells {count}',
                      f'screenshot {dest.as_posix()}/smear-radius{radius}-at{x}-{y}.png',
                      'mouse_down Left', 'frames 3', 'mouse_up Left', 'wait_foliage',
                      f'assert_environment_changes {count}', 'assert_heights_changed 0',
                      'assert_theme_changes 0', 'assert_sound_changes 0', 'assert_document_same']
            if count:
                lines += [f'screenshot {dest.as_posix()}/smear-painted-radius{radius}-at{x}-{y}.png',
                          'undo', 'wait_foliage', 'assert_environment_changes 0']
        lines += ['set environment_index 0', 'key_down Ctrl', 'mouse_down Left', 'frames 2',
                  'mouse_up Left', 'key_up Ctrl', 'frames 3', 'assert_state_min environment_index 1',
                  'assert_environment_changes 0',
                  # Automation shares the pointer brush dispatch; zero elapsed time still smears.
                  'terrain_stroke 64 108 0', 'frames 3', 'assert_environment_changes 1',
                  'undo', 'wait_foliage', 'assert_environment_changes 0',
                  'reveal chip_environment_paint', 'frames 3', 'click chip_environment_paint',
                  'frames 3', 'assert_state environment_smear 0',
                  'reveal combo_env_slot', 'frames 3', 'assert_widget combo_env_slot', 'quit']
        script = dest / 'smear.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]
        print(size, 'sparse cursor, Paint/Smear controls, sampling, channel preservation and undo: PASS', flush=True)


if __name__ == '__main__':
    main()
