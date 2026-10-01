#!/usr/bin/env python3
"""Legacy install-level must share core rollback and packed/loose routing."""
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
        raise SystemExit('this sharing-lock regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge-tools.exe')
    parser.add_argument('--loose', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='legacy-worldinstall-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    banks = ['data/Levels/' + name for name in
        ('FinalAlbion.wld', 'FinalAlbion.bwd', 'FinalAlbion.wad', 'FinalAlbion_RT.stb')]
    for name in banks:
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
        subprocess.run([str(args.exe.resolve()), 'wad', 'extract', str(wad), str(root)],
            check=True, capture_output=True, timeout=180)
        renamed = wad.with_name('_FinalAlbion.wad')
        assert wad.resolve().is_relative_to(root) and renamed.resolve().is_relative_to(root)
        wad.rename(renamed)
    for name in [name + '.tmp' for name in banks] + ['data/Levels/FinalAlbion.wad.tmp2']:
        (root / name).write_bytes(b'unrelated staging occupant')

    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result

    original = snapshot()
    command = ['world', 'install-level', str(root), 'LegacyInstallProbe', '6400', '6400',
        '--from-donor', 'TeleporterGreatwood']

    def run(label, command, success):
        result = subprocess.run([str(args.exe.resolve()), *command], cwd=repo,
            capture_output=True, text=True, timeout=240)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(root / 'data/Levels/FinalAlbion_RT.stb'), 0x80000000, 1, None, 3, 0x80, None)
    assert handle != wintypes.HANDLE(-1).value
    try:
        run('locked', command + ['--no-backup'], False)
    finally:
        kernel.CloseHandle(handle)
    after = snapshot()
    changed = [name for name, value in original.items() if after.get(name) != value]
    (work / 'failure_report.json').write_text(json.dumps({'changed_originals': changed}, indent=2))
    assert after == original, 'failed legacy install changed original or unowned files'
    for index, value in enumerate(['6400tail', 'oops', '2147483648', '+-6400', '-32', '8192', '6401']):
        invalid = command.copy()
        invalid[4] = value
        run('invalid_' + str(index), invalid, False)
        assert snapshot() == original
    run('missing_input', command + ['--lev', str(work / 'missing.lev')], False)
    assert snapshot() == original
    empty = work / 'empty.lev'
    empty.write_bytes(b'')
    run('empty_input', command + ['--lev', str(empty)], False)
    assert snapshot() == original
    donor_files = work / 'donor'
    subprocess.run([str(args.exe.resolve()), 'wad', 'extract',
        str(args.root / 'data/Levels/FinalAlbion.wad'), str(donor_files), 'TeleporterGreatwood.tng'],
        check=True, capture_output=True, timeout=180)
    donor_tng = next(path for path in donor_files.rglob('*') if path.suffix.lower() == '.tng')
    custom = work / 'custom.tng'
    custom.write_bytes(donor_tng.read_bytes() + b'\r\n// Legacy installer custom payload\r\n')
    result = run('retry', command + ['--tng', str(custom), '--region', 'LegacyProbeRegion',
        '--display', 'Legacy Probe Region', '--proximity'], True)
    assert 'new game or a save made after installation' in result.stdout
    assert '141-region cap' not in result.stdout
    assert 'Legacy Probe Region' in (root / 'data/Levels/FinalAlbion.wld').read_text(encoding='latin-1')
    if args.loose:
        saved_tng = root / 'data/Levels/FinalAlbion/LegacyInstallProbe.tng'
    else:
        extracted = work / 'new_level'
        subprocess.run([str(args.exe.resolve()), 'wad', 'extract', str(wad), str(extracted), 'LegacyInstallProbe.tng'],
            check=True, capture_output=True, timeout=180)
        saved_tng = next(path for path in extracted.rglob('*') if path.suffix.lower() == '.tng')
    assert saved_tng.read_bytes() == custom.read_bytes()
    assert not list(root.rglob('.forge-world-install-*'))
    assert all((root / name).read_bytes() == (root / 'data/Levels/FinalAlbion.bwd').read_bytes() for name in mirrors)
    backups = list(root.rglob('*.bak'))
    assert len(backups) == (5 if args.loose else 6)
    for backup in backups:
        target = Path(str(backup)[:-4])
        assert target.resolve().is_relative_to(root) and backup.resolve().is_relative_to(root)
        with backup.open('rb') as stream:
            assert hashlib.file_digest(stream, 'sha256').hexdigest() == original[target.relative_to(root).as_posix()]
        shutil.copyfile(backup, target)
        backup.unlink()
    if args.loose:
        assert not wad.exists() and renamed.exists()
        for ext in ('lev', 'tng'):
            created = root / ('data/Levels/FinalAlbion/LegacyInstallProbe.' + ext)
            assert created.is_file() and created.resolve().is_relative_to(root)
            created.unlink()
    assert snapshot() == original
    print('Legacy installer rollback, inputs, routing and backup recovery: PASS')


if __name__ == '__main__':
    main()
