#!/usr/bin/env python3
"""Texture import failures preserve the bank and unowned files; successful writes Restore."""
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
from test_meshimport import write_png


def main():
    if os.name != 'nt':
        raise SystemExit('the file-sharing regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='texture-recovery-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    for name in ('data/CompiledDefs/game.bin', 'data/graphics/pc/textures.big'):
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    bank = root / 'data/graphics/pc/textures.big'
    unowned = Path(str(bank) + '.atlas-tmp')
    unowned.write_bytes(b'unrelated texture output')
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    def snapshot():
        return {p.relative_to(root).as_posix(): digest(p) for p in root.rglob('*') if p.is_file()}
    original = snapshot()
    def run(label, *command, success=True):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=90)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        assert 'terminate called' not in result.stdout + result.stderr, 'uncaught import exception'
        return result.stdout
    bad = work / 'invalid.png'
    bad.write_bytes(b'invalid image')
    run('invalid_add', 'texture-add', 'FORGE_RECOVERY_TEXTURE', str(bad), success=False)
    unchanged = snapshot() == original
    (work / 'invalid_report.json').write_text(json.dumps({'unchanged': unchanged,
        'unowned_preserved': unowned.is_file(), 'backup_created': Path(str(bank) + '.forge-orig').exists()}, indent=2))
    assert unchanged, 'invalid image changed the bank, recovery metadata or unowned files'
    image = work / 'brown.png'
    write_png(str(image))
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    for label, share in (('locked_read', 0), ('locked_replace', 1)):
        handle = kernel.CreateFileW(str(bank), 0x80000000, share, None, 3, 0x80, None)
        assert handle != wintypes.HANDLE(-1).value
        try:
            run(label, 'texture-add', 'FORGE_RECOVERY_TEXTURE', str(image), success=False)
        finally:
            kernel.CloseHandle(handle)
        assert digest(bank) == original['data/graphics/pc/textures.big'] and unowned.read_bytes() == b'unrelated texture output'
        run(label + '_restore', 'restore', '--forget')
        assert snapshot() == original
    exported = work / 'barrel_before.png'
    run('unrelated_before', 'texture-export', 'BARREL_BRACED_1_24', str(exported))
    run('add', 'texture-add', 'FORGE_RECOVERY_TEXTURE', str(image), '--format', 'dxt3')
    assert Path(str(bank) + '.forge-orig').is_file()
    assert 'FORGE_RECOVERY_TEXTURE' in run('list', 'textures', 'FORGE_RECOVERY_TEXTURE')
    run('replace', 'texture-replace', 'FORGE_RECOVERY_TEXTURE', str(image))
    after = work / 'barrel_after.png'
    run('unrelated_after', 'texture-export', 'BARREL_BRACED_1_24', str(after))
    assert exported.read_bytes() == after.read_bytes(), 'unrelated texture changed'
    assert unowned.read_bytes() == b'unrelated texture output'
    run('restore', 'restore', '--forget')
    assert snapshot() == original and not list(root.rglob('.forge-texture-import-*'))
    print('Texture invalid/locked refusal, add/replace, unrelated texture and exact Restore: PASS')


if __name__ == '__main__':
    main()
