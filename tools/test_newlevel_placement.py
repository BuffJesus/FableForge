#!/usr/bin/env python3
"""Invalid new-level placement must fail before minimap or world-bank changes."""
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
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='newlevel-placement-', dir=repo / 'build')).resolve()
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
    unowned = root / 'data/graphics/pc/textures.big.atlas-tmp'
    unowned.write_bytes(b'unrelated minimap output')
    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    def run(label, command, success=True):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=240)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        if not success:
            assert 'terrain grid' in result.stderr or 'world grid' in result.stderr, result.stdout + result.stderr
        return result.stdout
    for route in ('new-level', 'blank-level'):
        for index, placement in enumerate(('6401,6400', '8192,6400', '-32,6400', '2147483616,6400')):
            command = ([route, 'TeleporterGreatwood', 'PlacementProbe'] if route == 'new-level' else
                       [route, 'PlacementProbe', '--template', 'TeleporterGreatwood', '--height', '12'])
            command += ['--at', placement, '--own-region', 'new']
            run(route + '_' + str(index), command, success=False)
            after = snapshot()
            report = {'route': route, 'placement': placement, 'unchanged': after == original}
            (work / (route + '_' + str(index) + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
            assert after == original, report
    output = run('valid_edge', ['blank-level', 'EdgePlacementProbe', '--template', 'TeleporterGreatwood',
        '--height', '12', '--at', '8128,8128', '--own-region', 'new'])
    assert 'box (8128,8128)-(8192,8192)' in output
    run('restore_edge', ['restore', '--forget'])
    assert snapshot() == original
    print('Invalid placements remain unchanged; valid world-edge creation and exact Restore: PASS')



if __name__ == '__main__':
    main()
