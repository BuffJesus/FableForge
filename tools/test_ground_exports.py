#!/usr/bin/env python3
"""Ground diagnostic PNGs must report blocked outputs and preserve prior files."""
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
from PIL import Image


def main():
    if os.name != 'nt':
        raise SystemExit('this sharing-lock regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='ground-exports-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    for name in ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/Levels/FinalAlbion.wad', 'data/Levels/FinalAlbion_RT.stb',
                 'data/graphics/pc/textures.big'):
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    exports = work / 'exports'
    exports.mkdir()
    name = 'OakVale_Sea_02'
    outputs = [exports / (name + suffix + '.png') for suffix in ('_engine_bg', '_our_bake', '_lev_bake')]
    for path in outputs:
        path.write_bytes(b'previous exported image')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    def run(label, success):
        result = subprocess.run([str(args.exe.resolve()), 'ground', name, '--install', str(root)],
            cwd=exports, capture_output=True, text=True, timeout=180)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        (work / (label + '.json')).write_text(json.dumps({'returncode': result.returncode,
            'expected_success': success}, indent=2), encoding='utf-8')
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result.stdout
    sentinel = exports / (name + '_our_bake.png.tmp')
    sentinel.write_bytes(b'unrelated pending output')
    for index in (0, 2):
        handle = kernel.CreateFileW(str(outputs[index]), 0x80000000, 1, None, 3, 0x80, None)
        assert handle != wintypes.HANDLE(-1).value
        try:
            assert 'wrote ' not in run('locked_' + str(index), False)
        finally:
            kernel.CloseHandle(handle)
        assert all(p.read_bytes() == b'previous exported image' for p in outputs)
        assert not list(exports.glob('.forge-ground-export-*'))
    # A directory at the last destination must refuse the whole group.
    assert outputs[2].resolve().parent == exports.resolve()
    outputs[2].unlink()
    outputs[2].mkdir()
    marker = outputs[2] / 'keep.txt'
    marker.write_bytes(b'keep directory occupant')
    assert 'wrote ' not in run('directory', False)
    assert marker.read_bytes() == b'keep directory occupant'
    assert all(p.read_bytes() == b'previous exported image' for p in outputs[:2])
    marker.unlink()
    outputs[2].rmdir()
    run('retry', True)
    pngs = [p.read_bytes() for p in outputs]
    for path in outputs:
        with Image.open(path) as image:
            image.verify()
    run('overwrite', True)
    assert [p.read_bytes() for p in outputs] == pngs
    assert not list(exports.glob('.forge-ground-export-*'))
    assert sentinel.read_bytes() == b'unrelated pending output'
    assert snapshot() == original
    print('Ground PNG refusal, retry, deterministic overwrite and source preservation: PASS')


if __name__ == '__main__':
    main()
