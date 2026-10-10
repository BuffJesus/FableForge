#!/usr/bin/env python3
"""Check rounded Walkable/Blocked/Camera pens, cursor vertices and exact undo."""
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
    work = Path(tempfile.mkdtemp(prefix='passability-cursor-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select StartOakValeWest', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'tour -1', 'terrain_set_all 20', 'wait_foliage',
                 'set preview_things 0', 'set preview_foliage 0', 'mode walkable',
                 'snapshot_heights', 'snapshot_themes', 'snapshot_environments',
                 'snapshot_sounds', 'snapshot_shore', 'snapshot_document']
        for mode in [4, 5, 12, 13]:
            # Seed opposite flags in a larger region, then test exact changes from that baseline.
            lines += [f'terrain_mode {4 if mode == 5 else 5}', 'brush 16 0',
                      'terrain_stroke 64 108 0', f'terrain_mode {12 if mode == 13 else 13}',
                      'terrain_stroke 64 108 0', 'snapshot_passability', f'terrain_mode {mode}']
            for radius, count in [(.25, 1), (1, 9), (2, 21)]:
                walk, camera = (count, 0) if mode in [4, 5] else (0, count)
                lines += [f'brush {radius} 0', 'camera 64.75 108.75 20 0 1.5 15',
                          'clear_toasts', 'reveal slider_radius', 'frames 3',
                          'reveal slider_radius', 'frames 3', 'mouse_move viewport', 'frames 3',
                          f'assert_state brush_cursor_vertices {count}',
                          f'screenshot {dest.as_posix()}/mode{mode}-radius{radius}.png',
                          'mouse_down Left', 'frames 3', 'mouse_up Left', 'wait_foliage',
                          f'assert_passability_changes {walk} {camera}', 'assert_heights_changed 0',
                          'assert_theme_changes 0', 'assert_environment_changes 0',
                          'assert_sound_changes 0', 'assert_shore_changes 0', 'assert_document_same']
                if mode in [4, 5] and radius == .25:
                    lines += ['key_down Shift', 'mouse_down Left', 'frames 3', 'mouse_up Left',
                              'key_up Shift', 'wait_foliage', 'assert_passability_changes 0 0',
                              'undo', 'wait_foliage', f'assert_passability_changes {walk} {camera}']
                lines += ['undo', 'wait_foliage', 'assert_passability_changes 0 0']
            lines += ['brush .25 0', 'terrain_stroke 65 109 0', 'frames 3',
                      f'assert_passability_changes {1 if mode in [4, 5] else 0} {1 if mode in [12, 13] else 0}',
                      'undo', 'wait_foliage', 'assert_passability_changes 0 0']
        lines += ['terrain_mode 0', 'frames 3', 'assert_state brush_cursor_vertices 0', 'quit']
        script = dest / 'passability.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=240,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-4000:]
        print(size, 'four rounded pens, 1/9/21 vertices, Shift, channel preservation and undo: PASS', flush=True)


if __name__ == '__main__':
    main()
