#!/usr/bin/env python3
"""Exercise precise terrain sizes through the actual slider and keyboard."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix='terrain-brush-controls-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'select Greatwood_1', 'wait_loaded', 'wait_foliage',
                 f'set uiscale {scale}', 'edit 1', 'terrain_mode 0',
                 'set pen_exact 1', 'set pen_step .5', 'frames 3',
                 'reveal slider_radius', 'frames 3', 'reveal slider_radius', 'frames 3', 'mouse_move slider_radius',
                 'mouse_down left', 'mouse_delta -10000 0', 'mouse_up left', 'frames 3',
                 'assert_state brush_radius 0.250000', 'snapshot_document', 'snapshot_heights',
                 'terrain_stroke 32 32 1', 'assert_heights_changed 1', 'undo',
                 'assert_heights_changed 0', 'assert_document_same', 'mouse_move viewport']
        for _ in range(2):
            lines += ['key_down RBracket', 'frames 2', 'key_up RBracket', 'frames 2']
        lines += ['assert_state brush_radius 0.500000', 'terrain_stroke 32 32 1',
                  'assert_heights_changed 1', 'undo', 'assert_document_same']
        for _ in range(4):
            lines += ['key_down LBracket', 'frames 2', 'key_up LBracket', 'frames 2']
        lines += ['assert_state brush_radius 0.250000', 'clear_toasts',
                  'reveal slider_radius', 'frames 3', f'screenshot {dest.as_posix()}/precise.png',
                  'mouse_move slider_radius', 'mouse_down left', 'mouse_delta 10000 0',
                  'mouse_up left', 'frames 3', 'dump_state', 'quit']
        script = dest / 'controls.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]
        counts = re.findall(r'ok\s+assert_heights_changed 1 \((\d+) vertices differ\)', log)
        assert counts == ['1', '1'], counts
        radius = float(re.findall(r'brush_radius=([\d.]+)', log)[-1])
        assert abs(radius - 60) < .001, radius
        print(size, 'slider endpoints, bracket sizes, single-vertex strokes and undo: PASS', flush=True)


if __name__ == '__main__':
    main()
