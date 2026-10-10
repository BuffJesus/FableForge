#!/usr/bin/env python3
"""Check held brush pinning, moved rays, Shift inversion, release and undo."""
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
    work = Path(tempfile.mkdtemp(prefix='brush-pin-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines=['wait_maps','wait_ready','select TeleporterGreatwood','wait_loaded','wait_foliage',
            f'set uiscale {scale}','edit 1','tour -1','set preview_things 0','set preview_foliage 0',
            'terrain_set_all 20','terrain_mode 0','set pen_exact 1','set pen_step 2','brush 0.25 4',
            'mouse_move 20 20','key_down Space','camera 10 10 20 0 1.5 30',
            'mouse_move viewport','frames 3','assert_state brush_pinned 0','key_up Space','frames 2',
            'snapshot_heights','snapshot_document','key_down Space','frames 3',
            'assert_state brush_pinned 1','assert_state_min brush_pin_x 9.9','assert_state_max brush_pin_x 10.1',
            'assert_state_min brush_pin_y 9.9','assert_state_max brush_pin_y 10.1',
            f'screenshot {dest.as_posix()}/pinned.png',
            'camera 30 30 20 0 1.5 30','frames 3','mouse_delta 5 0','frames 3',
            'assert_state brush_pinned 1','mouse_down Left','frames 4','mouse_up Left','frames 3',
            'assert_height 10 10 22','assert_height 30 30 20','assert_document_same',
            'undo','frames 3','assert_heights_changed 0','key_up Space','frames 3',
            'assert_state brush_pinned 0','mouse_move viewport','frames 3',
            'mouse_down Left','frames 3','mouse_up Left','frames 3',
            'assert_height 10 10 20','assert_height 30 30 22','undo','frames 3','assert_heights_changed 0',
            'camera 10 10 20 0 1.5 30','frames 3','mouse_move viewport','frames 3',
            'key_down Space','key_down Shift','frames 3','mouse_down Left','frames 3',
            'mouse_up Left','key_up Shift','frames 3','assert_height 10 10 18',
            'undo','frames 3','assert_heights_changed 0',
            'terrain_mode 17','frames 3','assert_state brush_pinned 0','key_up Space','frames 3',
            'set shore_set 0','terrain_stroke 10 10 1','terrain_stroke 30 30 1',
            'set shore_set 1','snapshot_shore','camera 10 10 20 0 1.5 30','frames 3',
            'mouse_move viewport','frames 3','key_down Space','frames 3','assert_state brush_pinned 1',
            'camera 30 30 20 0 1.5 30','frames 3','mouse_down Left','frames 3','mouse_up Left','frames 3',
            'assert_shore 10 10 1','assert_shore 30 30 0','assert_shore_changes 1','undo','frames 3',
            'assert_shore_changes 0','key_down Escape','key_up Escape','frames 3',
            'assert_state brush_pinned 0','key_up Space','frames 2','key_down Space','frames 3',
            'assert_state brush_pinned 1','key_down Ctrl','frames 3','assert_state brush_pinned 0',
            'key_up Ctrl','key_up Space','frames 3','terrain_mode 14','frames 3',
            'key_down Space','frames 3','assert_state brush_pinned 0','terrain_mode 15',
            'frames 3','assert_state brush_pinned 0','key_up Space','terrain_mode 0','frames 3',
            'mouse_move viewport','frames 3','key_down Space','frames 3','assert_state brush_pinned 1','select LookoutPoint','wait_loaded',
            'assert_state brush_pinned 0','key_up Space','frames 3','quit']
        script = dest / 'pin.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]
        print(size, 'held brush pin, release, Shift, survey and undo: PASS', flush=True)


if __name__ == '__main__':
    main()
