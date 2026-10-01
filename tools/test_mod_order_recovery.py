#!/usr/bin/env python3
"""Load-order edits preserve prior metadata on blocked publication, then retry."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    if os.name != 'nt':
        raise SystemExit('this sharing-lock regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo / 'build/forge-tools.exe')
    parser.add_argument('--gui', type=Path)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='mod-order-recovery-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    root.mkdir()
    packs = {}
    for tag in ('A', 'B', 'C'):
        pack = work / ('Pack' + tag)
        (pack / 'data/Misc').mkdir(parents=True)
        (pack / 'forge_pack.json').write_text(json.dumps({'version': 1, 'name': 'Pack' + tag,
            'models': [], 'groundThemes': []}), encoding='utf-8')
        (pack / 'data/Misc/shared.txt').write_text('from pack ' + tag, encoding='utf-8')
        packs[tag] = pack
    def cli(label, verb, options, success=True):
        result = subprocess.run([str(args.tool.resolve()), 'mods', verb, str(root), *map(str, options)],
            cwd=repo, capture_output=True, text=True, timeout=60)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == success, result.stdout + result.stderr
    for tag in ('A', 'B'):
        cli('add_' + tag, 'add', [packs[tag], '--name', 'Pack' + tag])
    order = root / 'forge_mods.json'
    sentinel = root / 'forge_mods.json.tmp'
    sentinel.write_bytes(b'unrelated order output')
    original = order.read_bytes()
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    def gui(label, failed):
        script = work / (label + '.txt')
        commands = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
            'mods_tab 1', 'frames 3', 'assert_state mods_count 2',
            'reveal mod_enabled_0', 'frames 3', 'click mod_enabled_0', 'frames 3',
            'reveal mod_up_1', 'frames 3', 'click mod_up_1', 'frames 3']
        if failed:
            commands += ['assert_log cannot save mod order']
        commands += [f'screenshot {(work / (label + ".png")).as_posix()}', 'quit', '']
        script.write_text('\n'.join(commands), encoding='utf-8')
        result = subprocess.run([str(args.gui.resolve()), '--auto', str(script), '--install', str(args.root)],
            cwd=repo, capture_output=True, text=True, timeout=180,
            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (work / (label + '_output.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-2200:]
    handle = kernel.CreateFileW(str(order), 0x80000000, 3, None, 3, 0x80, None)
    assert handle != wintypes.HANDLE(-1).value
    try:
        for label, verb, options in [('disable', 'disable', ['PackA']), ('move', 'move', ['PackB', '0']),
                                     ('remove', 'remove', ['PackA']), ('add', 'add', [packs['C'], '--name', 'PackC'])]:
            cli('locked_' + label, verb, options, False)
            assert order.read_bytes() == original
        if args.gui:
            gui('gui_locked', True)
            assert order.read_bytes() == original
    finally:
        kernel.CloseHandle(handle)
    if args.gui:
        gui('gui_retry', False)
    else:
        cli('retry_disable', 'disable', ['PackA'])
        cli('retry_move', 'move', ['PackB', '0'])
    saved = json.loads(order.read_text(encoding='utf-8'))['mods']
    assert [m['name'] for m in saved] == ['PackB', 'PackA'] and not saved[1]['enabled']
    cli('retry_add', 'add', [packs['C'], '--name', 'PackC'])
    cli('retry_remove', 'remove', ['PackC'])
    assert json.loads(order.read_text(encoding='utf-8'))['mods'] == saved
    assert sentinel.read_bytes() == b'unrelated order output'
    assert sorted(p.name for p in root.iterdir()) == ['forge_mods.json', 'forge_mods.json.tmp']
    (work / 'report.json').write_text(json.dumps({'locked_edits': 4, 'gui_exercised': bool(args.gui),
        'retry_saved': True, 'unrelated_file_preserved': True}, indent=2), encoding='utf-8')
    print('Load-order blocked edits, GUI state, retries and exact metadata preservation: PASS')


if __name__ == '__main__':
    main()
