#!/usr/bin/env python3
"""PNG export failures preserve prior output and report failure; retry succeeds."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib


def main():
    if os.name != 'nt':
        raise SystemExit('this sharing-lock regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='image-exports-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    for name in ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/Levels/FinalAlbion.wad', 'data/graphics/pc/textures.big'):
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
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]

    def run(label, route, name, target, success):
        result = subprocess.run([str(args.exe.resolve()), route, name, str(target), '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=180)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        (work / (label + '.json')).write_text(json.dumps({'returncode': result.returncode,
            'target': str(target), 'expected_success': success}, indent=2), encoding='utf-8')
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        if not success:
            assert 'wrote ' not in result.stdout

    for route, name in [('minimap-bake', 'TeleporterGreatwood'), ('texture-export', 'BARREL_BRACED_1_24')]:
        output = exports / (route + '.png')
        output.write_bytes(b'previous exported image')
        sentinel = Path(str(output) + '.tmp')
        sentinel.write_bytes(b'unrelated pending export')
        handle = kernel.CreateFileW(str(output), 0x80000000, 1, None, 3, 0x80, None)
        assert handle != wintypes.HANDLE(-1).value
        try:
            run(route + '_locked', route, name, output, False)
        finally:
            kernel.CloseHandle(handle)
        assert output.read_bytes() == b'previous exported image'
        directory = exports / (route + '_directory.png')
        directory.mkdir()
        (directory / 'keep.txt').write_bytes(b'keep directory occupant')
        run(route + '_directory', route, name, directory, False)
        assert (directory / 'keep.txt').read_bytes() == b'keep directory occupant'
        parent = exports / (route + '_parent')
        parent.write_bytes(b'keep parent occupant')
        run(route + '_parent', route, name, parent / 'image.png', False)
        assert parent.read_bytes() == b'keep parent occupant'
        run(route + '_retry', route, name, output, True)
        png = output.read_bytes()
        assert png[:8] == b'\x89PNG\r\n\x1a\n'
        width, height = struct.unpack_from('>II', png, 16)
        assert width > 0 and height > 0
        assert png[24] == 8 and png[25] in (2, 6)
        channels = 3 if png[25] == 2 else 4
        offset, compressed, ended = 8, bytearray(), False
        while offset < len(png):
            length = struct.unpack_from('>I', png, offset)[0]
            tag = png[offset + 4:offset + 8]
            payload = png[offset + 8:offset + 8 + length]
            assert len(payload) == length
            crc = struct.unpack_from('>I', png, offset + 8 + length)[0]
            assert zlib.crc32(tag + payload) == crc
            if tag == b'IDAT':
                compressed.extend(payload)
            offset += length + 12
            if tag == b'IEND':
                assert length == 0 and offset == len(png)
                ended = True
                break
        assert ended and len(zlib.decompress(compressed)) == height * (1 + width * channels)
        if route == 'minimap-bake':
            assert (width, height) == (256, 256)
        run(route + '_overwrite', route, name, output, True)
        assert output.read_bytes() == png and sentinel.read_bytes() == b'unrelated pending export'
        assert not list(exports.rglob('.forge-image-export-*'))
    assert snapshot() == original
    print('PNG export locked/invalid paths, retry, deterministic overwrite and source preservation: PASS')


if __name__ == '__main__':
    main()
