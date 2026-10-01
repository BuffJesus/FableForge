#!/usr/bin/env python3
"""Choice edits must read current disk choices and refuse unreadable input."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    if os.name != 'nt':
        raise SystemExit('this sharing-lock regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--gui', type=Path, default=repo / 'build/FableForge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='mod-pick-refresh-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    defs = root / 'data/CompiledDefs'
    defs.mkdir(parents=True)
    for name in ('game.bin', 'names.bin'):
        shutil.copyfile(args.root / 'data/CompiledDefs' / name, defs / name)
    tool = args.gui.resolve().with_name('forge-tools.exe')
    for tag in ('A', 'B'):
        pack = work / ('Pack' + tag)
        (pack / 'data/Misc').mkdir(parents=True)
        (pack / 'assets').mkdir()
        (pack / 'forge_pack.json').write_text(json.dumps({'version': 1, 'name': 'Pack' + tag,
            'models': [], 'groundThemes': []}), encoding='utf-8')
        (pack / 'data/Misc/shared.txt').write_text('from pack ' + tag, encoding='utf-8')
        result = subprocess.run([str(tool), 'mods', 'add', str(root), str(pack), '--name', 'Pack' + tag],
            cwd=repo, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stdout + result.stderr
    picks = root / 'forge_mods_picks.txt'
    picks.write_bytes(b'# previous empty choices\n')
    sentinel = root / 'forge_mods_picks.txt.tmp'
    sentinel.write_bytes(b'unrelated pending choices')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    initial = b'file:data/misc/other.txt\tPackA\n'
    external = initial + b'file:data/misc/external.txt =  PackB \r\n'
    def run(label, locked=False):
        picks.write_bytes(initial)
        marker = work / (label + '_ready.png')
        script = work / (label + '.txt')
        commands = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
            'mods_tab 1', 'frames 2', 'mods_conflicts', 'wait_mods', 'frames 2',
            'assert_state mods_conflicts 1', 'assert_state mods_picks 1',
            f'screenshot {marker.as_posix()}', 'frames 600']
        if locked:
            commands += ['mod_pick_refused * PackA', 'assert_state mods_picks 1',
                         'assert_log cannot read conflict choices']
        else:
            commands += ['mod_pick * PackA', 'assert_state mods_picks 3']
        commands += [f'screenshot {(work / (label + ".png")).as_posix()}', 'quit', '']
        script.write_text('\n'.join(commands), encoding='utf-8')
        handle = None
        with (work / (label + '_output.log')).open('w', encoding='utf-8') as output:
            process = subprocess.Popen([str(args.gui.resolve()), '--auto', str(script), '--install', str(args.root)],
                cwd=repo, stdout=output, stderr=subprocess.STDOUT,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
            try:
                deadline = time.monotonic() + 90
                while not marker.exists():
                    assert process.poll() is None, 'GUI exited before marker'
                    assert time.monotonic() < deadline, 'GUI marker timeout'
                    time.sleep(0.01)
                if locked:
                    # Deny new reads but allow replacement: save alone cannot detect stale input.
                    handle = kernel.CreateFileW(str(picks), 0x80000000, 6, None, 3, 0x80, None)
                    assert handle != wintypes.HANDLE(-1).value
                else:
                    picks.write_bytes(external)
                result = process.wait(timeout=120)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                if handle is not None:
                    kernel.CloseHandle(handle)
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result == 0 and 'RESULT PASS' in log, log[-2400:]
        if locked:
            assert picks.read_bytes() == initial
        else:
            saved = picks.read_bytes()
            assert b'file:data/misc/external.txt\tPackB' in saved
            assert b'file:data/misc/other.txt\tPackA' in saved
            assert b'file:data/misc/shared.txt\tPackA' in saved
    run('external_edit')
    run('unreadable', True)
    assert sentinel.read_bytes() == b'unrelated pending choices'
    assert not list(root.glob('.forge-mod-picks-*'))
    print('External choices preserved, unreadable choices refused, cached display preserved: PASS')


if __name__ == '__main__':
    main()
