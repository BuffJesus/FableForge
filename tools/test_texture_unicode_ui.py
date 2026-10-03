#!/usr/bin/env python3
"""Unicode image inputs round-trip through CLI add and GUI texture replacement."""
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
from test_install_switch_ui import make_install


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def snapshot(root):
    return {p.relative_to(root).as_posix(): digest(p) for p in root.rglob('*') if p.is_file()}


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='texture-unicode-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    gui = args.gui.resolve()
    cwd = work/'unrelated working directory'
    cwd.mkdir()
    marker = '\u00e9 \u03a9 \u6f22\u5b57 %FORGE_IMAGE_TOKEN% & files'
    root = work/('install '+marker)
    make_install(root, 'UnicodeTexture', 4)
    sources = [args.install/name for name in ['data/CompiledDefs/game.bin',
        'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']]
    source_hashes = {p: digest(p) for p in sources}
    for source in sources:
        target = root/source.relative_to(args.install)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    baseline = snapshot(root)
    image_folder = work/('images '+marker)
    image_folder.mkdir()
    control = work/'plain.png'
    red, blue = image_folder/'red \u6f22\u5b57.png', image_folder/'blue \u03a9.png'
    broken = image_folder/'broken \u00e9.png'
    Image.new('RGB', (64, 64), (0, 255, 0)).save(control)
    Image.new('RGB', (64, 64), (255, 0, 0)).save(red)
    Image.new('RGB', (64, 64), (0, 0, 255)).save(blue)
    broken.write_bytes(b'not an image')
    images_before = snapshot(image_folder)
    def cli(label, *command):
        result = subprocess.run([str(gui.with_name('forge.exe')), *map(str, command),
            '--install', str(root)], cwd=cwd, capture_output=True, timeout=120)
        output = (result.stdout+result.stderr).decode('utf-8', errors='replace')
        (work/(label+'.log')).write_text(output, encoding='utf-8')
        return result, output
    def pixels(path, colour):
        with Image.open(path) as image:
            assert image.size == (64, 64)
            assert image.convert('RGB').getextrema() == tuple((c, c) for c in colour)
    control_result, output = cli('ascii-add', 'texture-add', 'PNG_UNICODE_PROBE', control)
    assert control_result.returncode == 0, output
    before_bad = snapshot(root)
    result, output = cli('broken-add', 'texture-add', 'PNG_UNICODE_BROKEN', broken)
    assert result.returncode != 0 and snapshot(root) == before_bad, output
    if os.name == 'nt':
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
            ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        locked = kernel.CreateFileW(str(red), 0x80000000, 0, None, 3, 0x80, None)
        assert locked != ctypes.c_void_p(-1).value, ctypes.get_last_error()
        try:
            result, output = cli('locked-add', 'texture-add', 'PNG_UNICODE_RED', red)
            assert result.returncode != 0 and snapshot(root) == before_bad, output
        finally:
            kernel.CloseHandle(locked)
    result, output = cli('unicode-add', 'texture-add', 'PNG_UNICODE_RED', red)
    assert result.returncode == 0, output
    exported = image_folder/'export red \u6f22\u5b57.png'
    result, output = cli('unicode-export', 'texture-export', 'PNG_UNICODE_RED', exported)
    assert result.returncode == 0, output
    pixels(exported, (255, 0, 0))
    result, output = cli('restore-adds', 'restore', '--forget')
    assert result.returncode == 0 and snapshot(root) == baseline, output
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        result, output = cli(size+'-prepare', 'texture-add', 'PNG_UNICODE_PROBE', control)
        assert result.returncode == 0, output
        exported = image_folder/('export blue '+size+' \u03a9.png')
        script = work/(size+'.txt')
        script.write_text('\n'.join(['wait_maps', 'wait_ready', f'set uiscale {scale}',
            'textures_tab 1', 'frames 3', 'texture_select PNG_UNICODE_PROBE',
            f'texture_replace {blue.as_posix()}', 'frames 3',
            'assert_state texture_selected PNG_UNICODE_PROBE',
            f'texture_export {exported.as_posix()}', 'quit', '']), encoding='utf-8')
        result = subprocess.run([str(gui), '--install', str(root), '--size', size,
            '--auto', str(script)], cwd=cwd, capture_output=True, timeout=120,
            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', FORGE_IMAGE_TOKEN='expanded',
                APPDATA=str(work/(size+'-appdata')), LOCALAPPDATA=str(work/(size+'-localappdata'))))
        (work/(size+'.log')).write_text((result.stdout+result.stderr).decode('utf-8', errors='replace'), encoding='utf-8')
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-2400:]
        pixels(exported, (0, 0, 255))
        result, output = cli(size+'-restore', 'restore', '--forget')
        assert result.returncode == 0 and snapshot(root) == baseline, output
    assert all(digest(image_folder/name) == value for name, value in images_before.items())
    assert {p: digest(p) for p in sources} == source_hashes
    (work/'result.json').write_text(json.dumps({'passed': True, 'source_unchanged': True,
        'restored_exactly': True, 'sizes': ['1440x900', '800x600']}, indent=2), encoding='utf-8')
    print('Unicode PNG CLI add/export, invalid/locked input preservation, GUI replace/export and exact Restore: PASS')


if __name__ == '__main__':
    main()
