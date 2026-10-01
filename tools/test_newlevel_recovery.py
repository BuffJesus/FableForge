#!/usr/bin/env python3
"""A late loose-level creation failure must retain enough metadata for Restore."""
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
        raise SystemExit('this file-sharing regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    parser.add_argument('--marker-failure', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='newlevel-recovery-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
        'FinalAlbion.wad', 'FinalAlbion_RT.stb', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']
    for name in files:
        destination = root / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, destination)
    wad = root / 'data/Levels/FinalAlbion.wad'
    subprocess.run([str(repo / 'build/forge-tools.exe'), 'wad', 'extract', str(wad), str(root)],
        check=True, capture_output=True, timeout=180)
    renamed = wad.with_name('_FinalAlbion.wad')
    assert wad.resolve().is_relative_to(work) and renamed.resolve().is_relative_to(work)
    wad.rename(renamed)
    def snapshot():
        result = {}
        for p in root.rglob('*'):
            if p.is_file():
                with p.open('rb') as stream:
                    result[p.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    if args.marker_failure:
        sentinel = root / 'data/Levels/FinalAlbion/RecoveryProbe.tng.forge-created/preserve.txt'
        sentinel.parent.mkdir(parents=True)
        sentinel.write_bytes(b'unrelated marker-path occupant')
    original = snapshot()
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    bank = root / 'data/Levels/FinalAlbion_RT.stb'
    handle = None
    if not args.marker_failure:
        handle = kernel.CreateFileW(str(bank), 0x80000000, 1, None, 3, 0x80, None)
        assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
    try:
        result = subprocess.run([str(args.exe.resolve()), 'blank-level', 'RecoveryProbe',
            '--template', 'TeleporterGreatwood', '--at', '6400,6400', '--height', '12', '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=240)
    finally:
        if handle is not None:
            kernel.CloseHandle(handle)
    (work / 'create.log').write_text(result.stdout + result.stderr)
    expected = 'creation marker is not a file' if args.marker_failure else 'commit failed'
    assert result.returncode != 0 and expected in result.stdout + result.stderr
    created = [root / ('data/Levels/FinalAlbion/RecoveryProbe.' + ext) for ext in ('lev', 'tng')]
    if args.marker_failure:
        assert not any(p.exists() for p in created), 'marker failure committed new loose files'
        after_failure = snapshot()
        assert all(after_failure[name] == value for name, value in original.items()), 'marker failure changed an original'
    else:
        assert all(p.exists() for p in created), 'did not reach late failure after loose-file commit'
    markers = [Path(str(p) + '.forge-created').is_file() for p in created]
    result = subprocess.run([str(args.exe.resolve()), 'restore', '--install', str(root), '--forget'],
        cwd=repo, capture_output=True, text=True, timeout=120)
    (work / 'restore.log').write_text(result.stdout + result.stderr)
    restored = snapshot()
    (work / 'report.json').write_text(json.dumps({'markers': markers, 'restored_exactly': restored == original,
        'extra_files': sorted(set(restored) - set(original))}, indent=2))
    assert result.returncode == 0, result.stdout + result.stderr
    assert (args.marker_failure or all(markers)) and restored == original, 'creation failure left untracked files after Restore'
    print(('Marker preparation' if args.marker_failure else 'Late loose-level creation') + ' failure and exact Restore: PASS')


if __name__ == '__main__':
    main()
