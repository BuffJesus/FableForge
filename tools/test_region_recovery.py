#!/usr/bin/env python3
"""Keep WLD and all BWD copies consistent when a region-property write fails."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    if os.name != 'nt':
        raise SystemExit('the bank-sharing regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='region-recovery-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    files = ['data/Levels/FinalAlbion.wld', 'data/Levels/FinalAlbion.bwd', 'data/CompiledDefs/game.bin']
    for name in files:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    mirrors = ['FinalAlbion.bwd', 'data/Levels/FinalAlbion/FinalAlbion.bwd']
    for name in mirrors:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / 'data/Levels/FinalAlbion.bwd', target)
    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    original_wld = (root / files[0]).read_bytes()
    block = next(m for m in re.finditer(rb'NewRegion [^\n]*\n.*?EndRegion;', original_wld, re.S)
                 if b'RegionName "Greatwood";' in m[0])
    eol = b'\r\n' if b'\r\n' in original_wld else b'\n'
    edited = re.sub(rb'NewDisplayName [^\n]*\n', lambda _: b'NewDisplayName "Recovery Region";' + eol, block[0])
    edited = re.sub(rb'AppearOnWorldMap;[^\n]*\n', lambda _: b'AppearOnWorldMap;' + eol, edited)
    expected_wld = original_wld[:block.start()] + edited + original_wld[block.end():]
    def run(label, command, success=True):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=90)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result.stdout
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateFileW(str(root / mirrors[-1]), 0x80000000, 1, None, 3, 0x80, None)
    assert handle != wintypes.HANDLE(-1).value
    command = ['region-props', 'Greatwood', '--display', 'Recovery Region', '--worldmap', '1']
    try:
        run('locked', command, success=False)
    finally:
        kernel.CloseHandle(handle)
    after = snapshot()
    changed = [name for name, value in original.items() if after.get(name) != value]
    (work / 'failure_report.json').write_text(json.dumps({'changed_originals': changed}, indent=2), encoding='utf-8')
    assert not changed, 'failed region-property write changed original banks'
    run('restore_failure', ['restore', '--forget'])
    assert snapshot() == original
    run('retry', command)
    after = snapshot()
    assert after[files[0]] != original[files[0]] and after[files[1]] != original[files[1]]
    assert all(after[name] == after[files[1]] for name in mirrors)
    assert (root / files[0]).read_bytes() == expected_wld
    assert not list(root.rglob('.forge-region-props-*'))
    run('restore', ['restore', '--forget'])
    assert snapshot() == original
    for name in mirrors:
        (root / name).unlink()
    without_mirrors = snapshot()
    run('no_mirrors', command)
    assert all(not (root / name).exists() for name in mirrors)
    assert (root / files[0]).read_bytes() == expected_wld
    run('restore_no_mirrors', ['restore', '--forget'])
    assert snapshot() == without_mirrors
    print('Region-property rollback, unrelated WLD bytes, optional mirrors and exact Restore: PASS')


if __name__ == '__main__':
    main()
