#!/usr/bin/env python3
"""Minimap framing and options must be valid before replacing exported PNGs."""
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
    work = Path(tempfile.mkdtemp(prefix='minimap-inputs-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    files = ['data/Levels/FinalAlbion.wld', 'data/Levels/FinalAlbion.wad',
             'data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']
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
    output = work / 'minimap.png'
    output.write_bytes(b'previous minimap image')
    base = ['minimap-bake', 'TeleporterGreatwood', str(output)]
    cases = [base + ['--framing', value] for value in
             ('1,0,0tail', '1,0,0,0', '1,0', '1,,0', '1,0,', '', ' 1,0,0', '+-1,0,0',
              'nan,0,0', 'inf,0,0', '1,nan,0', '1,0,inf', '1e100,0,0', '0,0,0', '-1,0,0')]
    cases += [base + ['--typo'], base + ['extra'], base + ['--framing'], base + ['--region'],
              base + ['--install'], base + ['--region', ''], base + ['--install', ''],
              base + ['--region', 'Greatwood', '--framing', '1,0,0']]
    def run(label, command):
        result = subprocess.run([str(args.exe.resolve()), command[0], '--install', str(root), *command[1:]],
            cwd=repo, capture_output=True, text=True, timeout=180)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result
    for index, command in enumerate(cases):
        result = run(str(index), command)
        preserved = output.read_bytes() == b'previous minimap image'
        report = {'args': command, 'returncode': result.returncode, 'export_preserved': preserved}
        (work / (str(index) + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
        assert result.returncode != 0 and preserved, report
    result = run('default', base)
    assert result.returncode == 0, result.stdout + result.stderr
    default = output.read_bytes()
    assert default.startswith(b'\x89PNG\r\n\x1a\n')
    result = run('framed', base + ['--framing', '+1e0,+0,-0'])
    assert result.returncode == 0 and output.read_bytes() == default, result.stdout + result.stderr
    result = run('region', base + ['--region', 'Greatwood'])
    assert result.returncode == 0 and output.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'), result.stdout + result.stderr
    assert snapshot() == original
    print('Minimap framing/option refusals, signed framing, region lookup and source preservation: PASS')




if __name__ == '__main__':
    main()
