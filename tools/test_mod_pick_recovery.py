#!/usr/bin/env python3
"""Failed mod conflict choice writes preserve the file and the displayed choice."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    if os.name != 'nt':
        raise SystemExit('this sharing-lock regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--gui', type=Path, default=repo / 'build/FableForge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='mod-pick-recovery-', dir=repo / 'build')).resolve()
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
    def run(label, commands, locked=False):
        script = work / (label + '.txt')
        script.write_text('\n'.join(['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
            'mods_tab 1', 'frames 2', 'mods_conflicts', 'wait_mods', 'frames 2',
            'assert_state mods_conflicts 1', *commands,
            f'screenshot {(work / (label + ".png")).as_posix()}', 'quit', '']), encoding='utf-8')
        handle = None
        if locked:
            handle = kernel.CreateFileW(str(picks), 0x80000000, 1, None, 3, 0x80, None)
            assert handle != wintypes.HANDLE(-1).value
        try:
            result = subprocess.run([str(args.gui.resolve()), '--auto', str(script), '--install', str(args.root)],
                cwd=repo, capture_output=True, text=True, timeout=180,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        finally:
            if handle is not None:
                kernel.CloseHandle(handle)
        (work / (label + '_output.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-2500:]
    run('locked_add', ['assert_state mods_picks 0', 'mod_pick_refused * PackA', 'assert_state mods_picks 0', 'assert_log cannot save conflict choices'], True)
    assert picks.read_bytes() == b'# previous empty choices\n'
    run('retry', ['mod_pick * PackA', 'assert_state mods_picks 1'])
    saved = picks.read_bytes()
    assert b'PackA' in saved
    run('locked_remove', ['assert_state mods_picks 1', 'mod_pick_refused * -', 'assert_state mods_picks 1'], True)
    assert picks.read_bytes() == saved
    run('remove', ['mod_pick * -', 'assert_state mods_picks 0'])
    assert not picks.exists()
    assert sentinel.read_bytes() == b'unrelated pending choices'
    assert not list(root.glob('.forge-mod-picks-*'))
    print('Mod choice locked add/remove, unchanged display, retry and exact file preservation: PASS')


if __name__ == '__main__':
    main()
