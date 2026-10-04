#!/usr/bin/env python3
"""A locked same-size source must never be treated as unchanged."""
import argparse
import ctypes
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
from test_mod_field_details import write_bank


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='unreadable-file-compare-', dir=repo/'build'))
    base, source, out = (work/name for name in ('base', 'source', 'output'))
    write_bank(base, (1, 2, 3, 4))
    for root in (base, source):
        (root/'data/Misc').mkdir(parents=True)
    (base/'data/Misc/shared.asset').write_bytes(b'baseline')
    changed = source/'data/Misc/shared.asset'
    changed.write_bytes(b'newbytes')
    out.mkdir()
    prior = out/'prior.txt'
    prior.write_bytes(b'keep me')
    source_hash, prior_hash = digest(changed), digest(prior)
    command = [str(args.tool.resolve()), 'mods', 'merge', str(base), str(out),
               '--with', str(source), '--json']

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                   ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32,
                                   ctypes.c_void_p]
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.CreateFileW(str(changed), 0x80000000, 0, None, 3, 0, None)
    assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
    try:
        locked = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
    finally:
        kernel.CloseHandle(handle)
    (work/'locked.log').write_bytes(locked.stdout + locked.stderr)
    assert locked.returncode != 0, locked.stdout.decode(errors='replace')
    assert b'cannot read' in locked.stderr, locked.stderr
    assert digest(changed) == source_hash and digest(prior) == prior_hash
    assert sorted(p.relative_to(out).as_posix() for p in out.rglob('*')) == ['prior.txt']

    unlocked = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
    (work/'unlocked.log').write_bytes(unlocked.stdout + unlocked.stderr)
    assert unlocked.returncode == 0, unlocked.stderr
    assert isinstance(json.loads(unlocked.stdout), dict)
    assert (out/'data/Misc/shared.asset').read_bytes() == b'newbytes'
    assert digest(changed) == source_hash
    print('locked same-size file refused; prior output preserved; unlocked retry applied:', work)


if __name__ == '__main__':
    main()
