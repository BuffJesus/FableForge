#!/usr/bin/env python3
"""Saving a live loose TNG must honor the same game guard as deploying it."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    parser.add_argument('--gui', type=Path, default=repo / 'build/FableForge.exe')
    parser.add_argument('--stock', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='loose-save-guard-', dir=repo / 'build')).resolve()
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
    if not args.stock:
        subprocess.run([str(repo / 'build/forge-tools.exe'), 'wad', 'extract', str(wad), str(root)],
            check=True, capture_output=True, timeout=180)
        renamed = wad.with_name('_FinalAlbion.wad')
        assert wad.resolve().is_relative_to(work) and renamed.resolve().is_relative_to(work)
        wad.rename(renamed)
    source = work / 'hold.cpp'
    source.write_text('#include <windows.h>\n#include <cstdio>\nint main() { puts("ready"); fflush(stdout); Sleep(180000); }\n')
    compiler = re.search(r'^CMAKE_CXX_COMPILER:FILEPATH=(.+)$', (repo / 'build/CMakeCache.txt').read_text(), re.MULTILINE).group(1)
    helper = root / 'Fable.exe'
    subprocess.run([compiler, str(source), '-o', str(helper)], check=True, capture_output=True, timeout=60)
    def snapshot():
        result = {}
        for p in root.rglob('*'):
            if p.is_file():
                with p.open('rb') as stream:
                    result[p.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1')
    def gui(label, save, dirty):
        script = work / (label + '.txt')
        script.write_text('wait_maps\nwait_ready\nselect TeleporterGreatwood\nwait_loaded\nedit 1\n'
            'place OBJECT_BARREL_BREAKABLE GuardProbe_' + label + '\n' + save + '\nassert_state doc_dirty ' + str(dirty) + '\ndump_log\nquit\n')
        result = subprocess.run([str(args.gui.resolve()), '--install', str(root), '--auto', str(script)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=120)
        log = Path(str(script) + '.log').read_text()
        assert result.returncode == 0 and 'RESULT PASS' in log, log + result.stderr
        return log
    process = subprocess.Popen([str(helper)], cwd=root, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        assert process.stdout.readline().strip() == 'ready'
        log = gui('running', ('save_level' if args.stock else 'save_level_refused') + '\ndeploy_level_refused', 0 if args.stock else 1)
        assert 'Fable.exe is running from this install' in log
        after = snapshot()
        if args.stock:
            assert all(after[name] == value for name, value in original.items()), 'stock draft changed an existing bank'
            assert set(after) - set(original) == {'data/Levels/FinalAlbion/TeleporterGreatwood.tng', 'data/Levels/FinalAlbion/TeleporterGreatwood.tng.forge-created'}
        else:
            assert after == original, 'refused save changed install files'
    finally:
        process.terminate()
        process.communicate(timeout=10)
    gui('after_exit', 'save_level', 0)
    target = root / 'data/Levels/FinalAlbion/TeleporterGreatwood.tng'
    assert 'GuardProbe' in target.read_text()
    result = subprocess.run([str(args.exe.resolve()), 'restore', '--install', str(root), '--forget'],
        cwd=repo, capture_output=True, text=True, timeout=120)
    (work / 'restore.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0 and snapshot() == original
    assert wad.exists() == args.stock, 'save changed the active layout'
    print(('Stock draft allowed' if args.stock else 'Loose save refused') + ', deploy guard, exit retry and exact Restore: PASS')


if __name__ == '__main__':
    main()
