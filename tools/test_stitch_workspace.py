#!/usr/bin/env python3
"""Verify stitch extraction ownership and seam/restore behavior on an owned install."""
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
    parser.add_argument('--loose', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='stitch-workspace-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root, temp = work / 'install', work / 'temp'
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
        'FinalAlbion.wad', 'FinalAlbion_RT.stb', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin']
    for name in files:
        destination = root / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, destination)
    if args.loose:
        wad = root / 'data/Levels/FinalAlbion.wad'
        subprocess.run([str(args.exe.resolve().with_name('forge-tools.exe')), 'wad', 'extract', str(wad), str(root)],
                       check=True, capture_output=True, timeout=180)
        renamed = wad.with_name('_FinalAlbion.wad')
        assert wad.resolve().is_relative_to(work) and renamed.resolve().is_relative_to(work)
        wad.rename(renamed)
    def hashes():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = hashes()
    shared = temp / 'FableForge/stitch'
    shared.mkdir(parents=True)
    maps = ('TeleporterGreatwood', 'OrchardFarm')
    for name in maps:
        (shared / (name + '.lev')).write_bytes(b'unrelated stitch marker')
    env = dict(os.environ, TEMP=str(temp), TMP=str(temp))
    def run(name, *command):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=180)
        (work / (name + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout
    def workspace_intact():
        assert all((shared / (name + '.lev')).read_bytes() == b'unrelated stitch marker' for name in maps), 'stitch overwrote an unowned extraction'
        assert not list((temp / 'FableForge').glob('stitch-lev-*')), 'stitch leaked an owned extraction'
    run('move', 'world-move', maps[0], '2048', '8064', maps[1], '2112', '8064')
    moved = hashes()
    invalid = [['--feather', value] for value in
               ('oops', '4tail', '1.5', '', '+-1', '2147483648', '-2147483649')]
    invalid += [['--feather'], ['--typo'], ['--install'], ['--install', '']]
    for index, options in enumerate(invalid):
        result = subprocess.run([str(args.exe.resolve()), 'world-stitch', *maps,
            '--install', str(root), *options, '--dry-run'], cwd=repo, env=env,
            capture_output=True, text=True, timeout=60)
        (work / f'invalid_{index}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        assert result.returncode == 2, (options, result.returncode, result.stdout, result.stderr)
        assert hashes() == moved, options
    run('dry_run', 'world-stitch', *maps, '--dry-run')
    for value in ('auto', '-1', '-2147483648', '+4', '0', '2147483647'):
        run('valid_' + value, 'world-stitch', *maps, '--feather', value, '--dry-run')
        assert hashes() == moved, value
    workspace_intact()
    assert '1 stitched' in run('stitch', 'world-stitch', *maps)
    workspace_intact()
    heights = []
    for name, x in zip(maps, (64, 0)):
        result = subprocess.run([str(args.exe.resolve()), 'heights',
            str(root / 'data/Levels/FinalAlbion' / (name + '.lev')),
            *[f'{x},{y}' for y in (0, 7, 20, 33, 47, 64)]],
            cwd=repo, env=env, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        heights.append([float(line.split()[1]) for line in result.stdout.splitlines() if len(line.split()) == 2])
    assert len(heights[0]) == 6 and heights[0] == heights[1], heights
    assert 'already tight' in run('tight', 'world-stitch', *maps, '--dry-run')
    run('restore', 'restore', '--forget')
    workspace_intact()
    assert hashes() == original, 'restore changed original files or left new files'
    (work / 'report.json').write_text(json.dumps({'shared_heights': heights[0],
        'restored_exactly': True, 'markers_preserved': True, 'loose': args.loose}, indent=2))
    print('Stitch workspace, seam heights and exact restore: PASS')


if __name__ == '__main__':
    main()
