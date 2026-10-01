#!/usr/bin/env python3
"""A late level-install failure must roll back all world files and preserve unowned staging paths."""
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
    parser.add_argument('--loose', action='store_true')
    parser.add_argument('--core-exe', type=Path, default=repo / 'build/fableforge_worldinstall_tests.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='worldinstall-recovery-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
        'FinalAlbion.wad', 'FinalAlbion_RT.stb', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin']
    for name in files:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    mirrors = ['FinalAlbion.bwd', 'data/Levels/FinalAlbion/FinalAlbion.bwd']
    for name in mirrors:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / 'data/Levels/FinalAlbion.bwd', target)
    wad = root / 'data/Levels/FinalAlbion.wad'
    if args.loose:
        subprocess.run([str(repo / 'build/forge-tools.exe'), 'wad', 'extract', str(wad), str(root)],
            check=True, capture_output=True, timeout=180)
        renamed = wad.with_name('_FinalAlbion.wad')
        assert wad.resolve().is_relative_to(root.resolve()) and renamed.resolve().is_relative_to(root.resolve())
        wad.rename(renamed)
    unowned_paths = ['data/Levels/' + name + '.forge-tmp' for name in
        ('FinalAlbion.bwd', 'FinalAlbion.wld', 'FinalAlbion.wad', 'FinalAlbion_RT.stb')]
    unowned_paths += ['data/Levels/FinalAlbion.wad.forge-tmp2']
    unowned_paths += ['data/Levels/FinalAlbion/WorldInstallProbe.' + ext + '.forge-tmp' for ext in ('lev', 'tng')]
    for name in unowned_paths:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(b'unrelated level-install output')
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
    handle = kernel.CreateFileW(str(root / 'data/Levels/FinalAlbion_RT.stb'), 0x80000000, 1, None, 3, 0x80, None)
    assert handle != wintypes.HANDLE(-1).value
    command = ['new-level', 'TeleporterGreatwood', 'WorldInstallProbe', '--at', '6400,6400', '--region', 'Greatwood']
    try:
        run('locked', command, success=False)
    finally:
        kernel.CloseHandle(handle)
    after = snapshot()
    changed = [name for name, value in original.items() if after.get(name) != value]
    (work / 'failure_report.json').write_text(json.dumps({'changed_originals': changed}, indent=2))
    assert not changed, 'failed level installation changed original banks or unowned files'
    run('restore_failure', ['restore', '--forget'])
    assert snapshot() == original
    output = run('retry', command)
    assert 'installed: map slot' in output
    assert all((root / name).read_bytes() == b'unrelated level-install output' for name in unowned_paths)
    assert all((root / name).read_bytes() == (root / 'data/Levels/FinalAlbion.bwd').read_bytes() for name in mirrors)
    assert not list(root.rglob('.forge-world-install-*'))
    if args.loose:
        assert not wad.exists() and renamed.exists()
        assert all((root / ('data/Levels/FinalAlbion/WorldInstallProbe.' + ext)).is_file() for ext in ('lev', 'tng'))
    run('restore', ['restore', '--forget'])
    assert snapshot() == original
    if not args.loose:
        blocked = root / 'data/Levels/FinalAlbion.bwd.bak'
        blocked.mkdir()
        def core(label):
            result = subprocess.run([str(args.core_exe.resolve()), str(root)],
                cwd=repo, capture_output=True, text=True, timeout=240)
            (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
            return result
        refused = core('invalid_backup')
        assert refused.returncode != 0 and 'backup is not a file' in refused.stderr, refused.stdout + refused.stderr
        assert snapshot() == original and blocked.is_dir()
        blocked.rmdir()
        completed = core('core_retry')
        assert completed.returncode == 0, completed.stdout + completed.stderr
        backups = list(root.rglob('*.bak'))
        assert len(backups) == 6
        for backup in backups:
            target = Path(str(backup)[:-4])
            assert target.resolve().is_relative_to(root.resolve()) and backup.resolve().is_relative_to(root.resolve())
            with backup.open('rb') as stream:
                assert hashlib.file_digest(stream, 'sha256').hexdigest() == original[target.relative_to(root).as_posix()]
            shutil.copyfile(backup, target)
            backup.unlink()
        assert snapshot() == original
    print('Level-install rollback, staging ownership, retry and exact Restore: PASS')



if __name__ == '__main__':
    main()
