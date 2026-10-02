#!/usr/bin/env python3
"""Exercise Ctrl-drag height picking in every sculpt tool without saving."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, help='Optional owned build to validate while another gate runs.')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    gui = (args.gui or repo / 'build/FableForge.exe').resolve()
    work = Path(tempfile.mkdtemp(prefix='height-pick-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select StartOakValeWest', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'tour -1', 'terrain_set_all 20', 'wait_foliage',
                 'terrain_mode 2', 'set pen_target 30', 'set pen_speed 1', 'brush 8 4',
                 'terrain_stroke 64 110 1', 'wait_foliage', 'assert_height 64 110 30',
                 'assert_height 90 110 20', 'snapshot_document', 'snapshot_heights']
        for mode in [0, 1, 2, 3, 9, 16]:
            lines += [f'terrain_mode {mode}', 'set pen_target 0', 'set pen_target_from_stroke 1',
                      'camera 64 110 30 0 1.5 40', 'frames 4', 'mouse_move viewport',
                      'key_down Ctrl', 'frames 2', 'mouse_down left', 'frames 3',
                      'assert_state_min pen_target 29.999', 'assert_state_max pen_target 30.001',
                      'assert_state pen_target_from_stroke 0',
                      # Keep LMB held while the ray moves to a different height. This
                      # distinguishes continuous sampling from a press-only picker.
                      'camera 90 110 20 0 1.5 40', 'frames 4', 'mouse_delta 5 0', 'frames 3',
                      'assert_state_min pen_target 19.999', 'assert_state_max pen_target 20.001',
                      'mouse_up left', 'key_up Ctrl', 'wait_foliage',
                      'assert_heights_changed 0', 'assert_document_same', 'assert_widget text_paint_height']
        # Ctrl+Shift also samples rather than applying the inverted Raise tool.
        lines += ['terrain_mode 0', 'camera 64 110 30 0 1.5 40', 'frames 4', 'mouse_move viewport',
                  'key_down Ctrl', 'key_down Shift', 'mouse_down left', 'frames 3',
                  'assert_state_min pen_target 29.999', 'assert_state_max pen_target 30.001',
                  'mouse_up left', 'key_up Shift', 'key_up Ctrl', 'wait_foliage',
                  'assert_document_same', 'assert_heights_changed 0',
                  'clear_toasts', 'reveal text_paint_height', 'frames 4',
                  'reveal text_paint_height', 'frames 4', 'mouse_move viewport',
                  f'screenshot {dest.as_posix()}/picked-height.png']
        # Flatten consumes the sampled target without another manual height edit.
        lines += ['terrain_mode 2', 'camera 90 110 20 0 1.5 40', 'frames 4',
                  'mouse_move viewport', 'mouse_down left', 'frames 3', 'mouse_up left',
                  'wait_foliage', 'assert_height 90 110 30', 'undo', 'wait_foliage',
                  'assert_heights_changed 0', 'assert_document_same', 'terrain_mode 0']
        # Pressing Ctrl during an existing stroke must retain that edit and its undo.
        lines += ['set pen_exact 1', 'set pen_step 2', 'camera 64 110 30 0 1.5 40', 'frames 4',
                  'mouse_move viewport', 'mouse_down left', 'frames 3', 'key_down Ctrl',
                  'mouse_delta 5 0', 'frames 3', 'mouse_up left', 'key_up Ctrl', 'wait_foliage',
                  'assert_heights_changed 1', 'undo', 'wait_foliage',
                  'assert_heights_changed 0', 'assert_document_same', 'quit']
        script = dest / 'pick.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(gui), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-4000:]
        print(size, 'six tools, continuous Ctrl/Shift sampling, visible height and active-stroke undo: PASS', flush=True)


if __name__ == '__main__':
    main()
