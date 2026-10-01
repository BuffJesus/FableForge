#!/usr/bin/env python3
"""Compare background terrain writes against a single-stroke reference.

All outputs use a unique scratch install below build; the source is read only.
Later strokes and document switches must not change the worker's LEV/chunk.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', default=os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters'))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    source = Path(args.root)
    files = ['data/Levels/' + name for name in
             ('FinalAlbion.wld', 'FinalAlbion.bwd', 'FinalAlbion.wad', 'FinalAlbion_RT.stb')]
    files += ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin']
    if not all((source / name).is_file() for name in files):
        raise SystemExit('terrain async test requires stock world banks and definitions')
    work = Path(tempfile.mkdtemp(prefix='terrain-async-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    root = work / 'install'
    for name in files:
        dest = root / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, dest)
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    original = {name: digest(root / name) for name in files}
    env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1')
    def run(name, during, after):
        pack = work / name
        script = work / (name + '.txt')
        script.write_text(f'''wait_maps
wait_ready
select Greatwood_1
wait_loaded
edit 1
pack_dest {pack}
terrain_mode 0
brush 10 6
terrain_stroke 48 96 1
deploy_terrain
assert_state terrain_deploy_busy 1
{during}
wait_terrain
frames 3
assert_log re-baked terrain chunk
{after}
quit
''')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(root),
                                 '--auto', str(script)], cwd=repo, env=env,
                                capture_output=True, text=True, timeout=180)
        log = Path(str(script) + '.log').read_text()
        assert result.returncode == 0 and 'RESULT PASS' in log, log + result.stderr
        print(name + ': PASS', flush=True)
        return {p.relative_to(pack).as_posix(): digest(p) for p in pack.rglob('*') if p.is_file()}
    reference = run('reference', '', 'assert_state terrain_dirty 0')
    assert len(reference) == 3, reference
    edited = run('later_edit', '''terrain_stroke 48 96 1
assert_state terrain_deploy_busy 1''', '''assert_state terrain_dirty 1
undo
assert_state terrain_dirty 0
redo
assert_state terrain_dirty 1''')
    assert edited == reference, 'later edit changed the deployed terrain snapshot'
    switched = run('switch', '''select TeleporterGreatwood
assert_state terrain_deploy_busy 1
select Greatwood_1
assert_state terrain_deploy_busy 1''', '''assert_state selected Greatwood_1
assert_state terrain_dirty 0''')
    assert switched == reference, 'map switch changed the deployed terrain snapshot'
    assert original == {name: digest(root / name) for name in files}, 'source banks changed'
    print('terrain snapshot outputs and scratch banks: PASS; evidence retained at', work)


if __name__ == '__main__':
    main()
