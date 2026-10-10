#!/usr/bin/env python3
"""Capture actual Forge Browse selections for the three original mod sources."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from test_install_switch_ui import make_install


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--install', type=Path,
                        help='A validated disposable game copy for the picker screenshots')
    parser.add_argument('--aeon', type=Path, help='Short picker path to Aeon Edition')
    parser.add_argument('--fmp', type=Path, help='Short picker path to ControllerSupport.fmp')
    parser.add_argument('--runtime', type=Path, help='Short picker path to the DLL folder')
    args = parser.parse_args()
    # Keep a short Windows drive alias in the picker field when one is supplied.
    corpus = args.corpus.absolute()
    choices = [
        ('folder', args.aeon or corpus/'AeonEdition', '05-aeon-selected.png'),
        ('file', args.fmp or corpus/'ControllerSupportFMP/ControllerSupport.fmp', '06-fmp-selected.png'),
        ('folder', args.runtime or corpus/'FableControllerSupport/Mods/FableControllerSupport',
         '07-runtime-selected.png'),
    ]
    for mode, source, _ in choices:
        assert (source.is_dir() if mode == 'folder' else source.is_file()), source
    work = Path(tempfile.mkdtemp(prefix='perodis-source-steps-', dir=repo/'build')).resolve()
    root = args.install.resolve() if args.install else work/'game'
    if args.install:
        assert (root/'data/CompiledDefs/game.bin').is_file()
        original_order = (root/'forge_mods.json').read_bytes() if (root/'forge_mods.json').is_file() else None
    else:
        make_install(root, 'GuideExample', 4)
        original = {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
    script = work/'pickers.txt'
    lines = ['wait_maps', 'wait_ready', 'setup 0', 'set uiscale 1.5',
             'mods_tab 1', 'frames 5',
             f'assert_state mods_count {3 if args.install and original_order else 0}']
    for mode, _, name in choices:
        lines += ['reveal input_mod_path_browse', 'click input_mod_path_browse', 'frames 3',
                  f'click input_mod_path_{mode}', 'frames 4',
                  f'screenshot {(work/name).as_posix()}']
    lines += [f'assert_state mods_count {3 if args.install and original_order else 0}', 'quit']
    script.write_text('\n'.join(lines)+'\n', encoding='utf-8')

    user = c.WinDLL('user32', use_last_error=True)
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback, w.LPARAM]
    user.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetDlgCtrlID.argtypes = [w.HWND]
    user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.ShowWindow.argtypes = [w.HWND, c.c_int]
    user.IsWindow.argtypes = [w.HWND]
    user.IsWindow.restype = w.BOOL

    def windows(parent=None):
        result = []
        def add(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            cls = c.create_unicode_buffer(256)
            user.GetClassNameW(hwnd, cls, len(cls))
            result.append((hwnd, pid.value, cls.value, user.GetDlgCtrlID(hwnd)))
            return True
        cb = callback(add)
        if parent:
            user.EnumChildWindows(parent, cb, 0)
        else:
            user.EnumWindows(cb, 0)
        return result

    output = work/'output.log'
    with output.open('w', encoding='utf-8') as log:
        proc = subprocess.Popen([str(args.gui.resolve()), '--install', str(root),
            '--auto', str(script), '--size', '1440x1000'], cwd=repo,
            stdout=log, stderr=subprocess.STDOUT,
            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                     APPDATA=str(work/'appdata'), LOCALAPPDATA=str(work/'localappdata')))
        try:
            deadline = time.monotonic()+180
            handled = 0
            while proc.poll() is None and time.monotonic() < deadline:
                dialogs = [x for x in windows() if x[1] == proc.pid and x[2] == '#32770']
                if not dialogs:
                    time.sleep(.05)
                    continue
                assert handled < len(choices), 'unexpected extra picker dialog'
                hwnd = dialogs[0][0]
                mode, source, _ = choices[handled]
                time.sleep(.5)
                user.ShowWindow(hwnd, 0)  # only the owned automated dialog
                control = 1152 if mode == 'folder' else 1148
                edits = [x for x in windows(hwnd) if x[2] == 'Edit' and x[3] == control]
                assert edits, (mode, windows(hwnd))
                value = c.create_unicode_buffer(str(source))
                user.SendMessageW(edits[-1][0], 0x000C, 0, c.addressof(value))
                user.PostMessageW(hwnd, 0x0111, 1, 0)  # IDOK
                if mode == 'folder':
                    time.sleep(.6)
                    if user.IsWindow(hwnd): user.PostMessageW(hwnd, 0x0111, 1, 0)
                handled += 1
                while user.IsWindow(hwnd) and proc.poll() is None and time.monotonic() < deadline:
                    time.sleep(.05)
            assert handled == len(choices), f'only {handled} pickers handled'
            assert proc.wait(timeout=10) == 0, output.read_text(errors='replace')[-2000:]
        finally:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=10)
    trace = Path(str(script)+'.log').read_text(encoding='utf-8')
    assert 'RESULT PASS' in trace, trace[-3000:]
    if args.install:
        if original_order is None:
            assert not (root/'forge_mods.json').exists()
        else:
            assert (root/'forge_mods.json').read_bytes() == original_order
        assert not (root/'forge_stage_manifest.json').exists()
    else:
        assert {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()} == original
    shots = repo/'docs/walkthrough/aeon-controller'
    for _, _, name in choices:
        assert (work/name).is_file(), name
        shutil.copyfile(work/name, shots/name)
    print('Three real native Browse selections, screenshots and unchanged order: PASS')
    print('evidence:', work)


if __name__ == '__main__':
    main()
