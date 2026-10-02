#!/usr/bin/env python3
"""Drive the actual Windows mod file/folder pickers, including cancel, on a scratch root."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size', default='1280x900')
    parser.add_argument('--scale', default='1')
    args=parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix='path-browse-', dir=repo / 'build')).resolve()
    print('evidence retained at', work, flush=True)
    retail = Path(r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')
    user = c.WinDLL('user32', use_last_error=True)
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback, w.LPARAM]
    user.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetDlgCtrlID.argtypes = [w.HWND]
    user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.SendMessageW.restype = w.LPARAM
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.ShowWindow.argtypes = [w.HWND, c.c_int]
    user.IsWindow.argtypes = [w.HWND]
    user.IsWindow.restype = w.BOOL

    def windows(parent=None):
        result = []
        def add(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            cls, title = c.create_unicode_buffer(256), c.create_unicode_buffer(1024)
            user.GetClassNameW(hwnd, cls, len(cls))
            user.GetWindowTextW(hwnd, title, len(title))
            result.append((hwnd, pid.value, cls.value, title.value, user.GetDlgCtrlID(hwnd)))
            return True
        cb = callback(add)
        if parent: user.EnumChildWindows(parent, cb, 0)
        else: user.EnumWindows(cb, 0)
        return result

    file = work / 'Controller % literal & spaces.fmp'
    # Adding to the list classifies/hashes a source; this fixture is never deployed.
    file.write_bytes(b'path picker fixture, never applied')
    folder = work / 'Mod folder & spaces'
    (folder / 'Data/Misc').mkdir(parents=True)
    (folder / 'Data/Misc/example.txt').write_text('folder picker fixture')
    for mode, source in [('file', file), ('folder', folder), ('save', work / 'chosen dialogue.big')]:
        root = work / mode / 'game'
        (root / 'data/CompiledDefs').mkdir(parents=True)
        for name in ('game.bin', 'names.bin'):
            shutil.copyfile(retail / 'data/CompiledDefs' / name, root / 'data/CompiledDefs' / name)
        script = work / (mode + '.txt')
        commands=['wait_maps', 'wait_ready', f'set saveroot {root}',
            'mods_tab 1', 'frames 3', 'reveal input_mod_path_browse',
            'click input_mod_path_browse', 'frames 2',
            f'screenshot {(work / (mode + "-menu.png")).as_posix()}',
            'click input_mod_path_' + mode, 'frames 3',
            'click input_mod_path_browse', 'frames 2', 'click input_mod_path_' + mode,
            'frames 3', 'reveal btn_mod_add', 'click btn_mod_add', 'frames 3',
            'assert_state mods_count 1', f'screenshot {(work / (mode + ".png")).as_posix()}',
            'quit', '']
        if mode == 'save':
            commands=['wait_maps','wait_ready','assets_tab 4','frames 3',
                'dialogue_search beefy','frames 2','reveal dialogue_search_result_0','frames 2','click dialogue_search_result_0',
                'frames 3','click button_dialogue_tools','frames 3','reveal slider_dialogue_key_0','click slider_dialogue_key_0',
                'frames 2','assert_state dialogue_staged 1',
                f'dialogue_export_path {work / "before-browse.big"}',
                'reveal input_dialogue_scratch_path','frames 3',
                'reveal input_dialogue_scratch_path_browse','click input_dialogue_scratch_path_browse',
                'frames 3','click input_dialogue_scratch_path_browse','frames 3',
                'reveal button_dialogue_export','click button_dialogue_export','frames 3',
                'assert_state dialogue_exported 1','quit','']
        commands[2:2]=[f'set uiscale {args.scale}', 'frames 4']
        script.write_text('\n'.join(commands), encoding='utf-8')
        with (work / (mode + '-output.log')).open('w', encoding='utf-8') as log:
            proc = subprocess.Popen([str(repo / 'build/FableForge.exe'), '--install', str(retail),
                '--auto', str(script), '--size', args.size], cwd=repo,
                stdout=log, stderr=subprocess.STDOUT,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
            deadline = time.monotonic() + 90
            handled = 0
            events = []
            while proc.poll() is None and time.monotonic() < deadline:
                dialogs = [x for x in windows() if x[1] == proc.pid and x[2] == '#32770']
                if not dialogs:
                    time.sleep(.05)
                    continue
                hwnd = dialogs[0][0]
                time.sleep(.5)  # let the common item dialog finish creating its controls
                user.ShowWindow(hwnd, 0)  # interact with only this test process's hidden dialog
                children = windows(hwnd)
                events.append([(x[2], x[3], x[4]) for x in children])
                if handled == 0:
                    edits = [x for x in children if x[2] == 'Edit' and x[4] == (1152 if mode == 'folder' else 1001 if mode == 'save' else 1148)]
                    if not edits:
                        user.PostMessageW(hwnd, 0x0111, 2, 0)
                        proc.terminate()
                        proc.wait(timeout=10)
                        raise AssertionError(events)
                    value = c.create_unicode_buffer(str(source))
                    user.SendMessageW(edits[-1][0], 0x000C, 0, c.addressof(value))  # WM_SETTEXT
                    user.PostMessageW(hwnd, 0x0111, 1, 0)  # IDOK
                    if mode == 'folder':
                        # Entering an absolute folder navigates into it first.
                        time.sleep(.6)
                        if user.IsWindow(hwnd): user.PostMessageW(hwnd, 0x0111, 1, 0)
                elif handled == 1:
                    if mode == 'save': assert not source.exists(), 'picker must not write the export itself'
                    user.PostMessageW(hwnd, 0x0111, 2, 0)  # IDCANCEL: retain selected path
                else:
                    user.PostMessageW(hwnd, 0x0111, 2, 0)
                    proc.terminate()
                    proc.wait(timeout=10)
                    raise AssertionError('unexpected extra picker dialog')
                handled += 1
                time.sleep(.3)
            (work / (mode + '-dialogs.json')).write_text(json.dumps(events, indent=2), encoding='utf-8')
            if proc.poll() is None:
                proc.terminate()  # only the process started above
                proc.wait(timeout=10)
                raise AssertionError('native dialog timed out')
        print('dialog run',mode,'exit',proc.returncode,'handled',handled,flush=True)
        trace = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert handled == 2 and proc.returncode == 0 and 'RESULT PASS' in trace, trace[-2000:]
        if mode == 'save':
            assert source.read_bytes().startswith(b'BIGB')
            print('real native save selection + cancel + explicit archive export: PASS', flush=True)
            continue
        order = json.loads((root / 'forge_mods.json').read_text(encoding='utf-8'))
        assert len(order['mods']) == 1, order
        assert Path(order['mods'][0]['source']).resolve() == source, order
        print(mode, 'real native selection + cancel + exact source added: PASS', flush=True)


if __name__ == '__main__':
    main()
