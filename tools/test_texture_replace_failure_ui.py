#!/usr/bin/env python3
"""Actual texture input failures preserve copied banks and recover without restarting."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import tempfile
import time
from PIL import Image
from test_install_switch_ui import make_install
from test_model_import_form import fill as fill_input


def fill(widget, value):
    # A second reveal settles the compact panel after the selected card grows.
    return ['reveal '+widget, 'frames 3']+fill_input(widget, value)


def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()


def snapshot(root):
    return {p.relative_to(root).as_posix(): digest(p) for p in root.rglob('*') if p.is_file()}


def small_texture_bank(source, target):
    """Copy one real texture record, independently rebuilding the BIG directory."""
    with source.open('rb') as stream:
        def word(): return struct.unpack('<I', stream.read(4))[0]
        header = bytearray(stream.read(16)); stream.seek(struct.unpack_from('<I', header, 8)[0])
        count = word()
        for _ in range(count):
            name = bytearray()
            while (byte := stream.read(1)) not in (b'\0', b'\xff'):
                assert byte; name += byte
            bank_id, entries, start, _, block_size = struct.unpack('<5I', stream.read(20))
            if name == b'GBANK_MAIN_PC': break
        else: raise AssertionError('retail main texture bank absent')
        assert entries
        stream.seek(start); types = word(); stream.seek(types*8, 1)
        record_start = stream.tell(); prefix = stream.read(28)
        _, _, kind, length, data_offset, _, name_length = struct.unpack('<7I', prefix)
        stream.seek(name_length+4, 1); sources = word()
        for _ in range(sources): size = word(); stream.seek(size, 1)
        size = word(); stream.seek(size, 1); record_end = stream.tell()
        stream.seek(record_start); record = bytearray(stream.read(record_end-record_start))
        struct.pack_into('<I', record, 16, 16)
        stream.seek(data_offset); payload = stream.read(length); assert len(payload) == length
    table = struct.pack('<3I', 1, kind, 1)+record
    table_offset = 16+len(payload); directory_offset = table_offset+len(table)
    struct.pack_into('<I', header, 8, directory_offset)
    target.write_bytes(header+payload+table+struct.pack('<I', 1)+b'GBANK_MAIN_PC\0'+
        struct.pack('<5I', bank_id, 1, table_offset, len(table), block_size))


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--invalid', action='store_true')
    parser.add_argument('--add', action='store_true', help='Exercise the Add a texture form instead of replacement')
    parser.add_argument('--small-bank', action='store_true', help='Use one real texture record plus native seed records')
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
    probe, other_name = 'PNG_REPLACE_PROBE', 'PNG_REPLACE_OTHER'
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        root = case/'install'
        make_install(root, 'TextureReplace', 4)
        for source in sources:
            target = root/source.relative_to(args.install)
            target.parent.mkdir(parents=True, exist_ok=True)
            if (args.add or args.small_bank) and source.suffix == '.big':
                # UI feedback needs a valid bank, not thousands of retail slots.
                small_texture_bank(source, target)
            else: shutil.copyfile(source, target)
        baseline = snapshot(root)
        green, blue = case/'green.png', case/'replacement.png'
        Image.new('RGB', (64,64), (0,255,0)).save(green)
        Image.new('RGB', (64,64), (0,0,255)).save(blue)
        blue_hash, green_hash = digest(blue), digest(green)
        for name in (probe, other_name):
            result = subprocess.run([str(gui.with_name('forge.exe')), 'texture-add', name,
                str(green), '--install', str(root)], cwd=cwd, capture_output=True, timeout=90)
            assert result.returncode == 0, (result.stdout+result.stderr).decode('utf-8',errors='replace')
        before = snapshot(root)
        other = case/'other-install'
        if args.add:
            make_install(other, 'TextureAddOther', 4)
            other_before = snapshot(other)
        bad = case/'invalid.png'
        bad.write_bytes(b'not an image')
        failed = case/'failed.png'
        exported = case/'replaced.png'
        script = case/'actions.txt'
        def click(widget):
            return [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3', f'click {widget}', 'frames 3']
        lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}', 'assets_tab 0', 'frames 3',
            f'texture_select {probe}', 'frames 3']
        action = 'btn_tex_add' if args.add else 'btn_tex_replace'
        field = 'input_tex_add_image' if args.add else 'input_tex_image'
        state = 'texture_add_failed' if args.add else 'texture_replace_failed'
        widget = 'texture_add_error' if args.add else 'texture_replace_error'
        if args.add:
            lines += fill('input_tex_add_name', 'PNG_ADDED_PROBE')+['assert_state texture_add_name PNG_ADDED_PROBE']
        else:
            lines += click('btn_tex_replace_toggle')
        lines += fill(field, bad if args.invalid else blue)
        lines += click(action)+['dump_log', 'assert_log cannot read image', 'dump_state', 'clear_toasts',
            f'screenshot {failed.as_posix()}', 'frames 300', f'assert_state {state} 1']
        if args.add:
            lines += ['assert_state texture_add_name PNG_ADDED_PROBE', 'assets_tab 1', 'frames 3',
                      'assets_tab 0', 'frames 3']
        else:
            lines += click('btn_tex_replace_toggle')+click('btn_tex_replace_toggle')
        lines += [f'assert_state {state} 1', f'reveal {widget}', 'frames 3',
            f'reveal {widget}', 'frames 3', f'assert_widget {widget}',
            f'dump_widget {widget}', 'mouse_move 400 200', 'frames 3',
            f'screenshot {(case/"error.png").as_posix()}']
        if scale > 1:
            retained = 1 if args.add else 0
            lines += [f'texture_select {other_name}', f'assert_state {state} {retained}',
                      f'texture_select {probe}', f'assert_state {state} {retained}']
            if args.add:
                lines += fill('input_tex_add_name', 'PNG_ADDED_RENAMED')+[f'assert_state {state} 0']
                lines += fill('input_tex_add_name', 'PNG_ADDED_PROBE')
                if args.invalid:
                    lines += click(action)+[f'assert_state {state} 1']
                lines += [f'set saveroot {other.as_posix()}', f'assert_state {state} 0',
                          f'set saveroot {root.as_posix()}', f'assert_state {state} 0']
        if args.invalid:
            lines += fill(field, blue)
            if args.add: lines += [f'assert_state {state} 0']
        selected = 'PNG_ADDED_PROBE' if args.add else probe
        lines += click(action)+['wait_ready', f'assert_state {state} 0',
            f'assert_state texture_selected {selected}']
        if args.add: lines += ['assert_state texture_add_name ']
        lines += [f'texture_export {exported.as_posix()}', 'quit']
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
        bounds = re.search(r'widget_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
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
        if args.add: assert snapshot(other) == other_before
        assert digest(blue) == blue_hash and digest(green) == green_hash and bad.read_bytes() == b'not an image'
        print(size, 'add' if args.add else 'replace', 'invalid' if args.invalid else 'locked', 'visible failure, exact preservation, retry/pixels/Restore: PASS', flush=True)
    assert {p: digest(p) for p in sources} == source_hashes


if __name__ == '__main__':
    main()
