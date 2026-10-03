#!/usr/bin/env python3
"""Setup keeps a locked target unchanged, explains failure and retries in one GUI."""
import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time
from test_setup_restore_ui import level
from test_install_switch_ui import make_install


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--partial', action='store_true',
        help='also restore a second selected map while the first target stays locked')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='setup-restore-failure-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        root = case/'install'
        defs = root/'data/CompiledDefs'
        defs.mkdir(parents=True)
        (defs/'game.bin').write_bytes(b'x')
        levels = root/'data/Levels/FinalAlbion'
        levels.mkdir(parents=True)
        target = levels/'RestoreFailureWithLongReadableName.lev'
        backup = Path(str(target)+'.forge-orig')
        current, original = level(9), level(4)
        target.write_bytes(current); backup.write_bytes(original)
        (levels/'RestoreFailureWithLongReadableName.tng').write_bytes(b'Version 2;\r\n')
        selected = 'RestoreFailureWithLongReadableName'
        partial_target = None
        if args.partial:
            selected = 'PartialRestoreVisible'
            partial_target = levels/(selected+'.lev')
            partial_target.write_bytes(current)
            Path(str(partial_target)+'.forge-orig').write_bytes(original)
            (levels/(selected+'.tng')).write_bytes(b'Version 2;\r\n')
        other = case/'other-install'
        make_install(other, 'OtherRestoreFolder', 4)
        other_bytes = {p: p.read_bytes() for p in other.rglob('*') if p.is_file()}
        script = case/'failure.txt'
        def reveal(widget): return [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3']
        def restore_buttons():
            return reveal('btn_restore_all')+['click btn_restore_all', 'frames 3']+\
                reveal('btn_restore_confirm')+['click btn_restore_confirm', 'frames 3']
        failed = case/'failed.png'
        lines = ['wait_maps', 'wait_ready', f'select {selected}',
            'wait_loaded', f'set uiscale {scale}', 'setup 1', 'frames 3', 'clear_toasts']
        lines += restore_buttons()
        if args.partial:
            lines += ['wait_maps', 'wait_ready', 'wait_loaded', 'assert_log 1 file(s) returned']
        lines += ['assert_state backups_differ 1',
            f'assert_state terrain_center_height {"4.000" if args.partial else "9.000"}',
            'assert_log cannot replace', 'assert_state setup_restore_failed 1']
        lines += reveal('setup_restore_error')+['assert_widget setup_restore_error',
            'dump_widget setup_restore_error', 'clear_toasts', f'screenshot {failed.as_posix()}',
            'frames 300']
        if scale > 1:
            lines += [f'scan_install {other.as_posix()}', 'wait_maps', 'wait_ready',
                'assert_state setup_restore_failed 0', f'scan_install {root.as_posix()}',
                'wait_maps', 'wait_ready', f'select {selected}',
                'wait_loaded', 'frames 3']
        lines += restore_buttons()+['wait_maps', 'wait_ready', 'wait_loaded', 'frames 3',
            'assert_state setup_restore_failed 0', 'assert_state backups_differ 0',
            'assert_state terrain_center_height 4.000', f'screenshot {(case/"retried.png").as_posix()}', 'quit']
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        handle = kernel.CreateFileW(str(target), 0x80000000, 1, None, 3, 0x80, None)
        assert handle != wintypes.HANDLE(-1).value
        unlocked = False
        with (case/'output.log').open('w', encoding='utf-8') as output:
            process = subprocess.Popen([str(args.gui.resolve()), '--install', str(root), '--size', size,
                '--auto', str(script)], cwd=repo, stdout=output, stderr=subprocess.STDOUT,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                         APPDATA=str(work/'appdata'), LOCALAPPDATA=str(work/'localappdata')))
            try:
                deadline = time.monotonic()+60
                while not failed.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.01)
                assert failed.exists(), 'GUI did not reach the locked-failure checkpoint'
                assert target.read_bytes() == current and backup.read_bytes() == original
                if partial_target:
                    assert partial_target.read_bytes() == original
                    assert Path(str(partial_target)+'.forge-orig').read_bytes() == original
                assert not Path(str(target)+'.forge-restore').exists()
                assert kernel.CloseHandle(handle)
                unlocked = True
                rc = process.wait(timeout=60)
            finally:
                if not unlocked: kernel.CloseHandle(handle)
                if process.poll() is None:
                    process.terminate(); process.wait(timeout=10)
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc == 0 and 'RESULT PASS' in log, log[-5000:]
        bounds = re.search(r'widget_rect setup_restore_error ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
        assert bounds
        left, top, right, bottom = map(float, bounds.groups())
        width, height = struct.unpack_from('>II', failed.read_bytes(), 16)
        assert 0 <= left < right <= width and 0 <= top < bottom <= height, bounds.groups()
        assert bottom-top >= 20*scale, 'long restore error did not wrap'
        assert target.read_bytes() == backup.read_bytes() == original
        if partial_target:
            assert partial_target.read_bytes() == original
            assert Path(str(partial_target)+'.forge-orig').read_bytes() == original
        assert not Path(str(target)+'.forge-restore').exists()
        assert all(p.read_bytes() == value for p, value in other_bytes.items())
        print(size, 'partial restore and refreshed selected map;' if args.partial else '',
            'visible locked-write failure, preserved bytes and same-process retry: PASS', flush=True)


if __name__ == '__main__':
    main()
