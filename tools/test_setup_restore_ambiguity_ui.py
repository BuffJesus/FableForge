#!/usr/bin/env python3
"""Setup refuses ambiguous originals/creation records before any file changes."""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time
from test_setup_restore_ui import level


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='setup-restore-ambiguity-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    for kind in ['originals', 'created']:
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            case = work/kind/size
            root = case/'install'
            defs = root/'data/CompiledDefs'
            defs.mkdir(parents=True)
            (defs/'game.bin').write_bytes(b'x')
            levels = root/'data/Levels/FinalAlbion'
            levels.mkdir(parents=True)
            target = levels/'AmbiguousRestore.lev'
            target.write_bytes(level(9))
            original = Path(str(target)+'.forge-orig')
            original.write_bytes(level(4))
            conflict = Path(str(target)+('.atlas-orig' if kind == 'originals' else '.forge-created'))
            conflict.write_bytes(level(5) if kind == 'originals' else b'created by FableForge\n')
            (levels/'AmbiguousRestore.tng').write_bytes(b'Version 2;\r\n')
            independent = levels/'IndependentRestore.lev'
            independent.write_bytes(level(7))
            Path(str(independent)+'.forge-orig').write_bytes(level(3))
            (levels/'IndependentRestore.tng').write_bytes(b'Version 2;\r\n')
            before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            def reveal(widget):
                return [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3']
            def restore():
                return reveal('btn_restore_all')+['click btn_restore_all', 'frames 3']+\
                    reveal('btn_restore_confirm')+['click btn_restore_confirm', 'frames 3']
            failed = case/'failed.png'
            script = case/'restore.txt'
            lines = ['wait_maps', 'wait_ready', 'select AmbiguousRestore', 'wait_loaded',
                f'set uiscale {scale}', 'setup 1', 'frames 3']
            lines += restore()+['assert_state setup_restore_failed 1',
                'assert_state terrain_center_height 9.000', 'assert_log conflicting restore']
            lines += reveal('setup_restore_error')+['assert_widget setup_restore_error',
                'dump_widget setup_restore_error', 'clear_toasts', f'screenshot {failed.as_posix()}', 'frames 300']
            lines += restore()+['wait_maps', 'wait_ready', 'wait_loaded',
                'assert_state setup_restore_failed 0', 'assert_state backups_differ 0',
                'assert_state terrain_center_height 4.000', 'quit']
            script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
            preserved = case/conflict.name
            with (case/'output.log').open('w', encoding='utf-8') as output:
                process = subprocess.Popen([str(args.gui.resolve()), '--install', str(root),
                    '--size', size, '--auto', str(script)], cwd=repo, stdout=output,
                    stderr=subprocess.STDOUT, env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                        APPDATA=str(case/'appdata'), LOCALAPPDATA=str(case/'localappdata')))
                try:
                    deadline = time.monotonic()+60
                    while not failed.exists() and process.poll() is None and time.monotonic() < deadline:
                        time.sleep(0.01)
                    assert failed.exists(), 'did not reach ambiguity refusal'
                    assert {p: p.read_bytes() for p in root.rglob('*') if p.is_file()} == before
                    # Explicit fixture resolution: preserve the conflicting record outside the install.
                    conflict.rename(preserved)
                    rc = process.wait(timeout=60)
                finally:
                    if process.poll() is None:
                        process.terminate(); process.wait(timeout=10)
            log = Path(str(script)+'.log').read_text(encoding='utf-8')
            assert rc == 0 and 'RESULT PASS' in log, log[-5000:]
            bounds = re.search(r'widget_rect setup_restore_error ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
            assert bounds
            left, top, right, bottom = map(float, bounds.groups())
            width, height = struct.unpack_from('>II', failed.read_bytes(), 16)
            assert 0 <= left < right <= width and 0 <= top < bottom <= height
            assert target.read_bytes() == original.read_bytes() == level(4)
            assert independent.read_bytes() == level(3)
            assert preserved.read_bytes() == before[conflict]
            expected = dict(before)
            del expected[conflict]
            expected[target] = level(4); expected[independent] = level(3)
            assert {p: p.read_bytes() for p in root.rglob('*') if p.is_file()} == expected
            print(kind, size, 'visible refusal, all bytes preserved, explicit resolution and retry: PASS', flush=True)


if __name__ == '__main__':
    main()
