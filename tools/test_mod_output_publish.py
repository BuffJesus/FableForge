#!/usr/bin/env python3
"""Preserve prior build files when the final publish encounters a locked target."""
import argparse
import contextlib
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_mod_field_details import write_bank, read_values


def snapshot(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


@contextlib.contextmanager
def locked(path):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x80, None)
    assert handle != wintypes.HANDLE(-1).value, ctypes.get_last_error()
    try:
        yield
    finally:
        kernel.CloseHandle(handle)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    assert os.name == 'nt', 'This check requires real Windows file-sharing locks'
    work = Path(tempfile.mkdtemp(prefix='mod-output-publish-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    base, source = work/'baseline', work/'source'
    write_bank(base, (1, 2, 3, 4))
    files = source/'data/Misc'; files.mkdir(parents=True)
    for name, value in (('a_existing.bin', b'new existing'),
                        ('m_added.bin', b'new addition'), ('z_locked.bin', b'new locked')):
        (files/name).write_bytes(value)
    (source/'forge_pack.json').write_text(json.dumps({'version': 1, 'name': 'Publish fixture',
        'models': [], 'groundThemes': []}), encoding='utf-8')
    tool = args.tool.resolve()
    added = subprocess.run([str(tool), 'mods', 'add', str(base), str(source)],
                           capture_output=True, timeout=60, cwd=repo)
    assert added.returncode == 0, added.stderr
    original_inputs = {str(root): snapshot(root) for root in (base, source)}
    checks = []
    for route in ('merge', 'build'):
        for failure in ('locked', 'directory'):
            output = work/(route + '-' + failure)
            write_bank(output, (99, 98, 97, 96))
            previous = output/'data/Misc'; previous.mkdir(parents=True)
            (previous/'a_existing.bin').write_bytes(b'prior existing')
            (output/'unrelated.txt').write_bytes(b'keep unrelated file')
            target = previous/'z_locked.bin'
            if failure == 'directory':
                target.mkdir(); (target/'keep.txt').write_bytes(b'keep conflicting directory')
            else:
                target.write_bytes(b'prior locked')
            before = snapshot(output)
            command = [str(tool), 'mods', route, str(base), str(output)]
            if route == 'merge': command += ['--with', str(source)]
            command += ['--json']
            with locked(target) if failure == 'locked' else contextlib.nullcontext():
                result = subprocess.run(command, capture_output=True, timeout=90, cwd=repo)
            label = route + '-' + failure
            (work/(label + '.log')).write_bytes(result.stdout + result.stderr)
            assert result.returncode != 0, label
            assert snapshot(output) == before, label + ': final publish changed previous files'
            assert not list(output.glob('.forge-mod-output-*')), label
            assert original_inputs == {str(root): snapshot(root) for root in (base, source)}
            checks.append(label)
            if failure == 'locked':
                success = subprocess.run(command, capture_output=True, timeout=90, cwd=repo)
                assert success.returncode == 0, success.stderr
                assert read_values(output) == (1, 2, 3, 4)
                for path in files.iterdir(): assert (previous/path.name).read_bytes() == path.read_bytes()
                assert (output/'unrelated.txt').read_bytes() == b'keep unrelated file'
                assert not list(output.glob('.forge-mod-output-*'))
                checks.append(route + '-retry')
    assert original_inputs == {str(root): snapshot(root) for root in (base, source)}
    (work/'result.json').write_text(json.dumps({'passed': checks, 'inputs_unchanged': True,
        'scope': 'non-staged direct merge and registered build output publication'}, indent=2), encoding='utf-8')
    print('PASS:', len(checks), 'publish lock/collision/refusal and retry cases')


if __name__ == '__main__':
    main()
