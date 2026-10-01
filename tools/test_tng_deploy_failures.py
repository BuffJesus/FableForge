#!/usr/bin/env python3
"""Locked WAD/loose TNG targets must retain the draft and roll back object deploy."""
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
    work = Path(tempfile.mkdtemp(prefix='tng-deploy-failures-', dir=repo / 'build')).resolve()
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
    wad = root / 'data/Levels/FinalAlbion.wad'
    tools = str(repo / 'build/forge-tools.exe')
    subprocess.run([tools, 'wad', 'extract', str(wad), str(root), 'TeleporterGreatwood.tng'],
        check=True, capture_output=True, timeout=60)
    loose = root / 'data/Levels/FinalAlbion/TeleporterGreatwood.tng'
    assert loose.is_file()
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
        action = 'deploy_level_refused' if refused else 'deploy_level'
        script.write_text('wait_maps\nwait_ready\nselect TeleporterGreatwood\nwait_loaded\nedit 1\n'
            'place OBJECT_BARREL_BREAKABLE DeployProbe\n' + action + '\nassert_state doc_dirty ' + ('1' if refused else '0') + '\ndump_log\nquit\n')
        result = subprocess.run([str(args.gui.resolve()), '--install', str(root), '--auto', str(script)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=180)
        log = Path(str(script) + '.log').read_text()
        assert result.returncode == 0 and 'RESULT PASS' in log, log + result.stderr
    def restore(label):
        result = subprocess.run([str(args.exe.resolve()), 'restore', '--install', str(root), '--forget'],
            cwd=repo, capture_output=True, text=True, timeout=120)
        (work / (label + '_restore.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stdout + result.stderr
        assert snapshot() == original, 'Restore changed original files or retained artifacts'
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    for label, target in (('loose_locked', loose), ('wad_locked', wad)):
        handle = kernel.CreateFileW(str(target), 0x80000000, 1, None, 3, 0x80, None)
        assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
        try:
            gui(label, True)
        finally:
            kernel.CloseHandle(handle)
        after = snapshot()
        assert all(after[name] == value for name, value in original.items()), 'failed deployment changed a target'
        assert not list(root.glob('.forge-tng-deploy-*')), 'failed deployment leaked its workspace'
        restore(label)
    gui('retry', False)
    assert 'DeployProbe' in loose.read_text()
    subprocess.run([tools, 'wad', 'extract', str(wad), str(work / 'deployed'), 'TeleporterGreatwood.tng'],
        check=True, capture_output=True, timeout=60)
    assert (work / 'deployed/data/Levels/FinalAlbion/TeleporterGreatwood.tng').read_bytes() == loose.read_bytes()
    restore('retry')
    print('Locked TNG/WAD deployment, dirty draft, rollback, retry and exact Restore: PASS')


if __name__ == '__main__':
    main()
