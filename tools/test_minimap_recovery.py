#!/usr/bin/env python3
"""A failed minimap registry write must roll back its texture and definition banks."""
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
        raise SystemExit('the bank-sharing regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='minimap-recovery-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
        'FinalAlbion.wad', 'FinalAlbion_RT.stb', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']
    for name in files:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    unowned = root / 'data/graphics/pc/textures.big.atlas-tmp'
    unowned.write_bytes(b'unrelated minimap output')
    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    def run(label, command, success=True):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=240)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result.stdout
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateFileW(str(root / 'data/CompiledDefs/game.bin'), 0x80000000, 1, None, 3, 0x80, None)
    assert handle != wintypes.HANDLE(-1).value
    command = ['blank-level', 'MinimapRecoveryProbe', '--template', 'TeleporterGreatwood',
        '--at', '6400,6400', '--height', '12', '--own-region', 'new']
    try:
        run('locked', command, success=False)
    finally:
        kernel.CloseHandle(handle)
    after = snapshot()
    changed = [name for name, value in original.items() if after.get(name) != value]
    (work / 'failure_report.json').write_text(json.dumps({'changed_originals': changed}, indent=2))
    assert not changed, 'failed minimap registration changed original banks or unowned files'
    run('restore_failure', ['restore', '--forget'])
    assert snapshot() == original
    output = run('retry', command)
    assert 'MINIMAP_MINIMAPRECOVERYPROBE' in output and 'installed: map slot' in output
    assert unowned.read_bytes() == b'unrelated minimap output'
    assert not list(root.rglob('.forge-minimap-*'))
    run('restore', ['restore', '--forget'])
    assert snapshot() == original
    handle = kernel.CreateFileW(str(root / 'data/CompiledDefs/game.bin'), 0x80000000, 1, None, 3, 0x80, None)
    assert handle != wintypes.HANDLE(-1).value
    register = ['minimap-register', 'MINIMAP_RECOVERY_STANDALONE', '1']
    try:
        run('register_locked', register, success=False)
    finally:
        kernel.CloseHandle(handle)
    after = snapshot()
    assert all(after.get(name) == value for name, value in original.items())
    run('restore_register_failure', ['restore', '--forget'])
    assert snapshot() == original
    run('register_retry', register)
    assert snapshot()['data/CompiledDefs/game.bin'] != original['data/CompiledDefs/game.bin']
    run('restore_register', ['restore', '--forget'])
    assert snapshot() == original
    assert not list(root.rglob('.forge-minimap-*'))
    print('Minimap registry rollback, standalone registration, new-level retry and exact Restore: PASS')


if __name__ == '__main__':
    main()
