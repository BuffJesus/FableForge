#!/usr/bin/env python3
"""World registration refusals must precede new-level minimap changes."""
import argparse
import hashlib
import json
import os
import re
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
    work = Path(tempfile.mkdtemp(prefix='newlevel-preflight-', dir=repo / 'build')).resolve()
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
    wad = root / 'data/Levels/FinalAlbion.wad'
    if args.loose:
        subprocess.run([str(args.exe.resolve().with_name('forge-tools.exe')), 'wad', 'extract', str(wad), str(root)],
            check=True, capture_output=True, timeout=180)
        renamed = wad.with_name('_FinalAlbion.wad')
        assert wad.resolve().is_relative_to(root.resolve()) and renamed.resolve().is_relative_to(root.resolve())
        wad.rename(renamed)
        collision = root / 'data/Levels/FinalAlbion/CollisionProbe.tng'
        collision.write_bytes(b'unrelated loose level')
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
        return result.stdout
    text = (root / 'data/Levels/FinalAlbion.wld').read_text(encoding='latin-1')
    donor = re.search(r'MapX ([-0-9]+);\s*MapY ([-0-9]+);\s*LevelName "FinalAlbion\\TeleporterGreatwood.lev";', text)
    assert donor
    occupied = donor.group(1) + ',' + donor.group(2)
    cases = [('TeleporterGreatwood', '6400,6400', ['--own-region', 'new']),
             ('Bad.Name', '6400,6400', ['--own-region', 'new']),
             ('OverlapProbe', occupied, ['--own-region', 'new']),
             ('RegionProbe', '6400,6400', ['--own-region', 'MissingRegion', '--merge-into', 'Greatwood'])]
    if args.loose:
        cases.append(('CollisionProbe', '6400,6400', ['--own-region', 'new']))
    for route in ('new-level', 'blank-level'):
        for index, (name, placement, options) in enumerate(cases):
            command = ([route, 'TeleporterGreatwood', name] if route == 'new-level' else
                       [route, name, '--template', 'TeleporterGreatwood', '--height', '12'])
            command += ['--at', placement, *options]
            result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
                cwd=repo, capture_output=True, text=True, timeout=240)
            label = route + '_' + str(index)
            (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
            after = snapshot()
            report = {'args': command, 'returncode': result.returncode,
                      'changed_originals': [name for name, value in original.items() if after.get(name) != value],
                      'extra_files': sorted(set(after) - set(original))}
            (work / (label + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
            assert result.returncode != 0 and after == original, report
    output = run('valid', ['blank-level', 'PreflightProbe', '--template', 'TeleporterGreatwood',
        '--height', '12', '--at', '6400,6400', '--own-region', 'new'])
    assert 'installed: map slot' in output and 'minimap:' in output
    if args.loose:
        assert not wad.exists() and renamed.exists()
    run('restore', ['restore', '--forget'])
    assert snapshot() == original
    print('World registration preflight, minimap preservation, valid creation and exact Restore: PASS')




if __name__ == '__main__':
    main()
