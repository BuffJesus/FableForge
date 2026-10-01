#!/usr/bin/env python3
"""Diagnostic STB writes need recoverable backups and an owned temporary output."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    if os.name != 'nt':
        raise SystemExit('the locked-output check requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='chunk-write-recovery-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    for name in ('data/CompiledDefs/game.bin', 'data/Levels/FinalAlbion_RT.stb'):
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    bank = root / 'data/Levels/FinalAlbion_RT.stb'
    unowned = Path(str(bank) + '.atlas-tmp')
    unowned.write_bytes(b'unrelated temporary output')
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    def snapshot():
        return {p.relative_to(root).as_posix(): digest(p) for p in root.rglob('*') if p.is_file()}
    original = snapshot()
    def run(label, command, expected=0):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=180)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == (expected == 0), result.stdout + result.stderr
        return result.stdout
    command = ['chunk-zcheck', 'Greatwood_1', '1', '--write']
    output = run('write', command)
    backup = Path(str(bank) + '.forge-orig')
    report = {'bank_changed': digest(bank) != original['data/Levels/FinalAlbion_RT.stb'],
        'backup_present': backup.is_file(),
        'unowned_preserved': unowned.is_file() and unowned.read_bytes() == b'unrelated temporary output'}
    (work / 'write_report.json').write_text(json.dumps(report, indent=2))
    assert all(report.values()), report
    assert 'record equal 1, chunk equal 1, audit ok (0 issues)' in output, output
    run('restore', ['restore', '--forget'])
    assert snapshot() == original
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateFileW(str(bank), 0x80000000, 1, None, 3, 0x80, None)
    assert handle != wintypes.HANDLE(-1).value
    try:
        run('locked', command, expected=1)
        assert digest(bank) == original['data/Levels/FinalAlbion_RT.stb']
        assert unowned.read_bytes() == b'unrelated temporary output'
    finally:
        kernel.CloseHandle(handle)
    run('retry', command)
    run('restore_retry', ['restore', '--forget'])
    assert snapshot() == original
    assert not list(root.rglob('.forge-chunk-write-*'))
    print('Diagnostic STB write, locked refusal/retry and exact Restore: PASS')


if __name__ == '__main__':
    main()
