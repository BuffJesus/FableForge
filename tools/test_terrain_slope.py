#!/usr/bin/env python3
"""Check prop slope following, saved orientation and exact undo in the editor."""
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
    work = Path(tempfile.mkdtemp(prefix='terrain-slope-', dir=repo / 'build')).resolve()
    output = work / 'saved'
    print('Evidence retained at', work, flush=True)
    lines = ['wait_maps', 'wait_ready', f'set saveroot {output.as_posix()}',
             'select Greatwood_1', 'wait_loaded', 'wait_foliage', 'edit 1',
             'camera 32 32 20 0 .65 22', 'place OBJECT_BARREL_BREAKABLE ForgeSlopeProbe',
             'frames 4', 'frame_selected', 'zoom -6', 'frames 3', 'snapshot_frame',
             'snapshot_heights', 'clear_toasts', f'screenshot {work.as_posix()}/before.png',
             'terrain_mode 0', 'brush 5 4', 'terrain_stroke 32 34 1', 'frames 5',
             'assert_heights_changed 1', 'assert_selected_facing_changed', 'wait_foliage',
             'clear_toasts', f'screenshot {work.as_posix()}/sloped.png', 'save_level',
             'undo', 'frames 5', 'assert_heights_changed 0', 'assert_selected_frame_same',
             'wait_foliage', f'screenshot {work.as_posix()}/undone.png', 'quit']
    script = work / 'slope.txt'
    script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                             '--size', '1440x900', '--auto', str(script)], cwd=repo,
                            capture_output=True, text=True, timeout=180,
                            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
    (work / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    log = Path(str(script) + '.log').read_text(encoding='utf-8')
    assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]
    saved = (output / 'data/Levels/FinalAlbion/Greatwood_1.tng').read_text()
    blocks = re.findall(r'NewThing\s+Object;.*?EndThing;', saved, re.S)
    probe = next(block for block in blocks if 'ForgeSlopeProbe' in block)
    up = [float(re.search(rf'RHSetUp{axis}\s+([^;]+);', probe)[1]) for axis in 'XYZ']
    assert abs(sum(v*v for v in up) - 1) < .001, up
    assert abs(up[0]) + abs(up[1]) > .05, up
    print('PASS: terrain stroke rotates prop; saved basis is tilted and normalized; undo restores frame')


if __name__ == '__main__':
    main()
