#!/usr/bin/env python3
"""New-level numeric options must be complete, finite and refused before writes."""
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
    work = Path(tempfile.mkdtemp(prefix='newlevel-inputs-', dir=repo / 'build')).resolve()
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
        return result.stdout
    cases = []
    for route in ('new-level', 'blank-level'):
        base = ([route, 'TeleporterGreatwood', 'NumericProbe'] if route == 'new-level' else
                [route, 'NumericProbe', '--template', 'TeleporterGreatwood'])
        for placement in ('6400,6400tail', '6400junk,6400', '6400,6400,0', '2147483648,6400',
                          '6400,2147483648', '6400', ',6400', '6400,', ' 6400,6400', '+-6400,6400', ''):
            cases.append((base + ['--at', placement], '--at'))
    blank = ['blank-level', 'NumericProbe', '--template', 'TeleporterGreatwood', '--at', '6400,6400']
    for size in ('64x64tail', '64x64x64', '2147483648x64', '64', '64x', ' 64x64', ''):
        cases.append((blank + ['--size', size], '--size'))
    for height in ('12tail', 'oops', 'nan', 'inf', '-inf', '1e100', ' 12', '+-12', ''):
        cases.append((blank + ['--height', height], '--height'))
    for theme in ('2tail', '2147483648', '-1', '256', ''):
        cases.append((blank + ['--theme', theme], '--theme'))
    for index, (command, option) in enumerate(cases):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=240)
        (work / (str(index) + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        after = snapshot()
        report = {'args': command, 'returncode': result.returncode, 'unchanged': after == original}
        (work / (str(index) + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
        assert result.returncode != 0 and after == original, report
        assert option in result.stderr, result.stdout + result.stderr
    output = run('valid', ['blank-level', 'NumericProbe', '--template', 'TeleporterGreatwood',
        '--height', '+1.25e1', '--size', '+64x064', '--at', '+6400,06400'])
    assert 'box (6400,6400)-(6464,6464)' in output and 'height 12.5' in output
    run('restore', ['restore', '--forget'])
    assert snapshot() == original
    print('Complete numeric inputs, finite height, valid signed forms and exact Restore: PASS')




if __name__ == '__main__':
    main()
