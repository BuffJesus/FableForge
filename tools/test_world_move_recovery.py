#!/usr/bin/env python3
"""A late world-move failure must roll back placement, creature positions and terrain."""
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
from test_overworld import bwd_box, wld_pos


def main():
    if os.name != 'nt':
        raise SystemExit('the bank-sharing regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    parser.add_argument('--loose', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='world-move-recovery-', dir=repo / 'build')).resolve()
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
    extract = [str(repo / 'build/forge-tools.exe'), 'wad', 'extract', str(wad), str(root)]
    if not args.loose:
        extract.append('OrchardFarm.tng')
    subprocess.run(extract, check=True, capture_output=True, timeout=90)
    if args.loose:
        renamed = wad.with_name('_FinalAlbion.wad')
        assert wad.resolve().is_relative_to(root.resolve()) and renamed.resolve().is_relative_to(root.resolve())
        wad.rename(renamed)
    loose = root / 'data/Levels/FinalAlbion/OrchardFarm.tng'
    assert b'InitialPosX' in loose.read_bytes()
    sentinels = [root / ('data/Levels/' + name + '.atlas-tmp') for name in ('FinalAlbion.wad', 'FinalAlbion_RT.stb')]
    for path in sentinels:
        path.write_bytes(b'unrelated world-move output')
    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    old_pos = wld_pos(root / 'data/Levels/FinalAlbion.wld', 'OrchardFarm')
    original_tng = loose.read_bytes()
    initial = lambda data, axis: [float(value) for value in re.findall(rb'InitialPos' + axis + rb' ([^;]+);', data)]
    def run(label, command, success=True):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=360)
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
    command = ['world-move', 'OrchardFarm', '2176', '8064']
    try:
        run('locked', command, success=False)
    finally:
        kernel.CloseHandle(handle)
    after = snapshot()
    changed = [name for name, value in original.items() if after.get(name) != value]
    (work / 'failure_report.json').write_text(json.dumps({'changed_originals': changed}, indent=2))
    assert not changed, 'failed world move changed original banks or unowned files'
    run('restore_failure', ['restore', '--forget'])
    assert snapshot() == original
    run('invalid_owner', ['world-owner', 'OrchardFarm', 'NoSuchRecoveryRegion'], success=False)
    assert snapshot() == original
    run('invalid_visibility', ['world-sees', 'NoSuchRecoveryRegion', 'OrchardFarm', '1'], success=False)
    assert snapshot() == original
    output = run('retry', command)
    assert 'OrchardFarm.tng:' in output and 'creature InitialPos shifted' in output
    assert all(path.read_bytes() == b'unrelated world-move output' for path in sentinels)
    assert wld_pos(root / 'data/Levels/FinalAlbion.wld', 'OrchardFarm') == (2176, 8064)
    assert bwd_box(root / 'data/Levels/FinalAlbion.bwd', 'OrchardFarm') == (2176, 8064, 2272, 8192)
    assert all((root / name).read_bytes() == (root / 'data/Levels/FinalAlbion.bwd').read_bytes() for name in mirrors)
    if args.loose:
        assert not wad.exists() and renamed.exists()
    else:
        subprocess.run([str(repo / 'build/forge-tools.exe'), 'wad', 'extract', str(wad), str(work / 'deployed'), 'OrchardFarm.tng'],
            check=True, capture_output=True, timeout=90)
        assert loose.read_bytes() == (work / 'deployed/data/Levels/FinalAlbion/OrchardFarm.tng').read_bytes()
    for axis, delta in ((b'X', 2176 - old_pos[0]), (b'Y', 8064 - old_pos[1])):
        before_values, after_values = initial(original_tng, axis), initial(loose.read_bytes(), axis)
        assert before_values and len(before_values) == len(after_values)
        assert all(abs(after - before - delta) < 0.01 for before, after in zip(before_values, after_values))
    world = run('world', ['world'])
    moved = next(line for line in world.splitlines() if 'OrchardFarm ' in line)
    assert 'baked at' not in moved and '2176' in moved and '8064' in moved
    assert not list(root.rglob('.forge-world-edit-*'))
    run('restore', ['restore', '--forget'])
    assert snapshot() == original
    print('World-move rollback, placement/TNG/STB consistency, retry and exact Restore: PASS')



if __name__ == '__main__':
    main()
