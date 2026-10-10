#!/usr/bin/env python3
"""A locked PNG export preserves old output, explains failure and retries in one GUI."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
from PIL import Image
from test_install_switch_ui import make_install


def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    args = parser.parse_args()
    gui, cwd = args.gui.resolve(), args.cwd.resolve()
    work = Path(tempfile.mkdtemp(prefix='texture-export-failure-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'
    make_install(root, 'TextureExport', 4)
    sources = [args.install/name for name in ['data/CompiledDefs/game.bin',
        'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']]
    source_hashes = {p: digest(p) for p in sources}
    for source in sources:
        target = root/source.relative_to(args.install)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    image = work/'green.png'
    Image.new('RGB', (64, 64), (0, 255, 0)).save(image)
    added = subprocess.run([str(gui.with_name('forge.exe')), 'texture-add', 'PNG_EXPORT_PROBE',
        str(image), '--install', str(root)], cwd=cwd, capture_output=True, text=True, timeout=90)
    assert added.returncode == 0, added.stdout+added.stderr
    other = subprocess.run([str(gui.with_name('forge.exe')), 'texture-add', 'PNG_EXPORT_OTHER',
        str(image), '--install', str(root)], cwd=cwd, capture_output=True, text=True, timeout=90)
    assert other.returncode == 0, other.stdout+other.stderr
    banks = {p: digest(p) for p in root.rglob('*') if p.is_file()}
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        output = case/'export folder with a long readable name'
        output.mkdir(parents=True)
        target = output/'PNG_EXPORT_PROBE.png'
        target.write_bytes(b'previous PNG output must remain unchanged while locked')
        (output/'keep.txt').write_bytes(b'unrelated output')
        (output/'.forge-image-export-sentinel').mkdir()
        def snapshot():
            return {p.relative_to(output).as_posix(): digest(p) if p.is_file() else None for p in output.rglob('*')}
        before = snapshot()
        def click():
            return ['reveal btn_tex_export', 'frames 3', 'reveal btn_tex_export', 'frames 3',
                    'click btn_tex_export', 'frames 3']
        failed = case/'failed.png'
        script = case/'failure.txt'
        lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}', 'assets_tab 0', 'frames 4',
            'texture_select PNG_EXPORT_PROBE', 'frames 3', f'set outdir {output.as_posix()}']
        lines += click()+['dump_log', 'assert_log cannot rename', 'assert_state texture_export_failed 1',
            'assert_state texture_selected PNG_EXPORT_PROBE', 'reveal texture_export_error', 'frames 3',
            'reveal texture_export_error', 'frames 3', 'assert_widget texture_export_error',
            'dump_widget texture_export_error', 'mouse_move viewport', 'clear_toasts', f'screenshot {failed.as_posix()}', 'frames 300']
        if scale > 1:
            lines += ['texture_select PNG_EXPORT_OTHER', 'frames 3', 'assert_state texture_export_failed 0',
                'texture_select PNG_EXPORT_PROBE', 'frames 3', 'assert_state texture_export_failed 0']
        lines += click()+['assert_state texture_export_failed 0',
            'assert_state texture_selected PNG_EXPORT_PROBE', 'quit']
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        handle = kernel.CreateFileW(str(target), 0x80000000, 1, None, 3, 0x80, None)
        assert handle != wintypes.HANDLE(-1).value
        unlocked = False
        with (case/'output.log').open('w', encoding='utf-8') as log_output:
            process = subprocess.Popen([str(gui), '--install', str(root), '--size', size,
                '--auto', str(script)], cwd=cwd, stdout=log_output, stderr=subprocess.STDOUT,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                    APPDATA=str(case/'appdata'), LOCALAPPDATA=str(case/'localappdata')))
            try:
                deadline = time.monotonic()+60
                while not failed.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.01)
                assert failed.exists(), 'locked export checkpoint missing'
                assert snapshot() == before
                assert kernel.CloseHandle(handle)
                unlocked = True
                rc = process.wait(timeout=60)
            finally:
                if not unlocked: kernel.CloseHandle(handle)
                if process.poll() is None:
                    process.terminate(); process.wait(timeout=10)
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc == 0 and 'RESULT PASS' in log, '\n'.join(log.splitlines()[-20:])
        bounds = re.search(r'widget_rect texture_export_error ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
        assert bounds
        left, top, right, bottom = map(float, bounds.groups())
        with Image.open(failed) as screenshot:
            width, height = screenshot.size
            assert 0 <= left < right <= width and 0 <= top < bottom <= height
            assert bottom-top > 20*scale
        with Image.open(target) as png:
            assert png.size == (64, 64)
            assert all(g > 240 and r < 10 and b < 10 for r, g, b in set(png.convert('RGB').getdata()))
        expected = dict(before); expected[target.name] = digest(target)
        assert snapshot() == expected
        assert all(digest(p) == value for p, value in banks.items())
        print(size, 'visible locked export failure, preserved output and same-process retry: PASS', flush=True)
    assert all(digest(p) == value for p, value in source_hashes.items())
    print('Source banks and GUI install unchanged: PASS', flush=True)


if __name__ == '__main__':
    main()
