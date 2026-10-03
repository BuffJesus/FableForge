#!/usr/bin/env python3
"""Actual replacement failures preserve copied banks and recover without restarting."""
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
from test_model_import_form import fill


def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()


def snapshot(root):
    return {p.relative_to(root).as_posix(): digest(p) for p in root.rglob('*') if p.is_file()}


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--invalid', action='store_true')
    args = parser.parse_args()
    gui = args.gui.resolve()
    work = Path(tempfile.mkdtemp(prefix='texture-replace-failure-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    cwd = work/'unrelated working directory'
    cwd.mkdir()
    sources = [args.install/name for name in ['data/CompiledDefs/game.bin',
        'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']]
    source_hashes = {p: digest(p) for p in sources}
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        root = case/'install'
        make_install(root, 'TextureReplace', 4)
        for source in sources:
            target = root/source.relative_to(args.install)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        baseline = snapshot(root)
        green, blue = case/'green.png', case/'replacement.png'
        Image.new('RGB', (64,64), (0,255,0)).save(green)
        Image.new('RGB', (64,64), (0,0,255)).save(blue)
        blue_hash, green_hash = digest(blue), digest(green)
        for name in ('PNG_REPLACE_PROBE', 'PNG_REPLACE_OTHER'):
            result = subprocess.run([str(gui.with_name('forge.exe')), 'texture-add', name,
                str(green), '--install', str(root)], cwd=cwd, capture_output=True, timeout=90)
            assert result.returncode == 0, (result.stdout+result.stderr).decode('utf-8',errors='replace')
        before = snapshot(root)
        bad = case/'invalid.png'
        bad.write_bytes(b'not an image')
        failed = case/'failed.png'
        exported = case/'replaced.png'
        script = case/'actions.txt'
        def click(widget):
            return [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3', f'click {widget}', 'frames 3']
        lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}', 'assets_tab 0', 'frames 3',
            'texture_select PNG_REPLACE_PROBE', 'frames 3']
        lines += click('btn_tex_replace_toggle')+fill('input_tex_image', bad if args.invalid else blue)
        lines += click('btn_tex_replace')+['dump_log', 'assert_log cannot read image', 'dump_state', 'clear_toasts',
            f'screenshot {failed.as_posix()}', 'frames 300',
            'assert_state texture_replace_failed 1']
        lines += click('btn_tex_replace_toggle')+click('btn_tex_replace_toggle')+[
            'assert_state texture_replace_failed 1', 'reveal texture_replace_error', 'frames 3',
            'reveal texture_replace_error', 'frames 3', 'assert_widget texture_replace_error',
            'dump_widget texture_replace_error', f'screenshot {(case/"error.png").as_posix()}']
        if scale > 1:
            lines += ['texture_select PNG_REPLACE_OTHER', 'assert_state texture_replace_failed 0',
                'texture_select PNG_REPLACE_PROBE', 'assert_state texture_replace_failed 0']
        if args.invalid: lines += fill('input_tex_image', blue)
        lines += click('btn_tex_replace')+['wait_ready', 'assert_state texture_replace_failed 0',
            'assert_state texture_selected PNG_REPLACE_PROBE',
            f'texture_export {exported.as_posix()}', 'quit']
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        handle = None
        if not args.invalid:
            handle = kernel.CreateFileW(str(blue), 0x80000000, 0, None, 3, 0x80, None)
            assert handle != wintypes.HANDLE(-1).value
        with (case/'output.log').open('w', encoding='utf-8') as output:
            process = subprocess.Popen([str(gui), '--install', str(root), '--size', size,
                '--auto', str(script)], cwd=cwd, stdout=output, stderr=subprocess.STDOUT,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                    APPDATA=str(case/'appdata'), LOCALAPPDATA=str(case/'localappdata')))
            try:
                deadline = time.monotonic()+60
                while not failed.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.01)
                assert failed.exists(), 'replacement refusal checkpoint missing'
                assert snapshot(root) == before, 'failed replacement changed the copied install'
                if handle is not None:
                    assert kernel.CloseHandle(handle)
                    handle = None
                rc = process.wait(timeout=90)
            finally:
                if handle is not None: kernel.CloseHandle(handle)
                if process.poll() is None:
                    process.terminate(); process.wait(timeout=10)
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc == 0 and 'RESULT PASS' in log, log[-4000:]
        bounds = re.search(r'widget_rect texture_replace_error ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
        assert bounds
        left,top,right,bottom = map(float,bounds.groups())
        with Image.open(case/'error.png') as picture:
            assert 0 <= left < right <= picture.width and 0 <= top < bottom <= picture.height
        with Image.open(exported) as picture:
            assert picture.size == (64,64)
            assert picture.convert('RGB').getextrema() == ((0,0),(0,0),(255,255))
        result = subprocess.run([str(gui.with_name('forge.exe')), 'restore', '--forget',
            '--install', str(root)], cwd=cwd, capture_output=True, timeout=120)
        assert result.returncode == 0 and snapshot(root) == baseline
        assert digest(blue) == blue_hash and digest(green) == green_hash and bad.read_bytes() == b'not an image'
        print(size, 'invalid' if args.invalid else 'locked', 'visible failure, exact preservation, retry/pixels/Restore: PASS', flush=True)
    assert {p: digest(p) for p in sources} == source_hashes


if __name__ == '__main__':
    main()
