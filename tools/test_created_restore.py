#!/usr/bin/env python3
"""Repeated terrain/TNG edits must retain created-file semantics through Restore."""
import argparse
import hashlib
import json
import os
from pathlib import Path
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
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='created-restore-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
        'FinalAlbion.wad', 'FinalAlbion_RT.stb', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']
    for name in files:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    original = {name: digest(root / name) for name in files}
    script = work / 'repeat.txt'
    script.write_text('''wait_maps
wait_ready
select TeleporterGreatwood
wait_loaded
edit 1
terrain_mode 0
brush 3 2
terrain_stroke 30 30 1
deploy_terrain
wait_file_job
wait_terrain
assert_state terrain_dirty 0
terrain_stroke 30 30 1
deploy_terrain
wait_file_job
wait_terrain
assert_state terrain_dirty 0
place OBJECT_BARREL_BREAKABLE CreatedFirst
save_level
assert_state doc_dirty 0
place OBJECT_BARREL_BREAKABLE CreatedSecond
deploy_level
assert_state doc_dirty 0
dump_log
quit
''')
    env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1')
    result = subprocess.run([str(args.gui.resolve()), '--install', str(root), '--auto', str(script)],
        cwd=repo, env=env, capture_output=True, text=True, timeout=240)
    log = Path(str(script) + '.log').read_text()
    assert result.returncode == 0 and 'RESULT PASS' in log, log + result.stderr
    created = [root / ('data/Levels/FinalAlbion/TeleporterGreatwood.' + extension) for extension in ('lev', 'tng')]
    assert all(p.is_file() and Path(str(p) + '.forge-created').is_file() for p in created)
    assert 'CreatedFirst' in created[1].read_text() and 'CreatedSecond' in created[1].read_text()
    unexpected_backups = [p.relative_to(root).as_posix() for p in root.rglob('*.forge-orig') if p.with_suffix('').suffix in ('.lev', '.tng')]
    leftovers = []
    for attempt in range(2):
        result = subprocess.run([str(args.exe.resolve()), 'restore', '--install', str(root), '--forget'],
            cwd=repo, env=env, capture_output=True, text=True, timeout=60)
        (work / f'restore_{attempt}.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stdout + result.stderr
        assert original == {name: digest(root / name) for name in files}, 'Restore changed source-bank bytes'
        leftovers.append(sorted(p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and p.relative_to(root).as_posix() not in original))
    (work / 'report.json').write_text(json.dumps({'unexpected_backups': unexpected_backups, 'leftovers': leftovers,
        'original_bank_hashes_restored': True}, indent=2))
    assert not unexpected_backups and not any(leftovers), 'created files acquired original backups or survived Restore'
    print('Repeated LEV/TNG edits and two exact Restores: PASS')


if __name__ == '__main__':
    main()
