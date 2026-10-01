#!/usr/bin/env python3
"""A late terrain-pack failure must preserve all previous pack files and the source install."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    if os.name != 'nt':
        raise SystemExit('this file-sharing regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    parser.add_argument('--gui', type=Path, default=repo / 'build/FableForge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='terrain-pack-recovery-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
        'FinalAlbion.wad', 'FinalAlbion_RT.stb', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']
    for name in files:
        destination = root / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, destination)
    pack = work / 'pack'
    pack_files = ['data/Levels/FinalAlbion/TeleporterGreatwood.lev', 'stb/TeleporterGreatwood.chunk', 'stb/TeleporterGreatwood.record']
    for name in pack_files:
        target = pack / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(('previous pack file ' + name).encode())
    pack_before = {name: (pack / name).read_bytes() for name in pack_files}
    def snapshot():
        result = {}
        for p in root.rglob('*'):
            if p.is_file():
                with p.open('rb') as stream:
                    result[p.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1')
    def gui(label, refused):
        script = work / (label + '.txt')
        script.write_text('wait_maps\nwait_ready\nselect TeleporterGreatwood\nwait_loaded\nedit 1\n'
            'pack_dest ' + pack.as_posix() + '\nterrain_mode 0\nbrush 3 2\nterrain_stroke 30 30 1\nassert_state terrain_dirty 1\n'
            'deploy_terrain\nwait_file_job\nwait_terrain\nassert_state terrain_dirty ' + ('1' if refused else '0') + '\ndump_log\nquit\n')
        result = subprocess.run([str(args.gui.resolve()), '--install', str(root), '--auto', str(script)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=180)
        log = Path(str(script) + '.log').read_text()
        assert result.returncode == 0 and 'RESULT PASS' in log, log + result.stderr
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateFileW(str(pack / pack_files[-1]), 0x80000000, 1, None, 3, 0x80, None)
    assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
    try:
        gui('record_locked', True)
    finally:
        kernel.CloseHandle(handle)
    assert snapshot() == original, 'failed pack write changed source install'
    assert {p.relative_to(pack).as_posix(): p.read_bytes() for p in pack.rglob('*') if p.is_file()} == pack_before
    assert not list(pack.glob('.forge-terrain-deploy-*'))
    gui('retry', False)
    assert snapshot() == original, 'successful pack write changed source install'
    assert {p.relative_to(pack).as_posix() for p in pack.rglob('*') if p.is_file()} == set(pack_files)
    assert all((pack / name).read_bytes() != pack_before[name] for name in pack_files)
    result = subprocess.run([str(args.exe.resolve()), 'info', str(pack / pack_files[0])],
        cwd=repo, capture_output=True, text=True, timeout=60)
    (work / 'lev_info.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    print('Terrain pack late-file rollback, dirty draft, retry and unchanged source install: PASS')



if __name__ == '__main__':
    main()
