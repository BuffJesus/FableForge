#!/usr/bin/env python3
"""Malformed export options preserve prior outputs; valid numbers produce a valid GLB."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from retail_smoke import parse_glb, validate


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='export-inputs-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    for name in ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/Levels/FinalAlbion.wad'):
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    output = work / 'export.glb'
    output.write_bytes(b'prior exported model')
    invalid = []
    for option in ('--texels', '--max-texture'):
        invalid += [[option, value] for value in ('oops', '4tail', '1.5', '', '+-1', '2147483648')]
    for option in ('--tile', '--gain'):
        invalid += [[option, value] for value in ('oops', '4tail', '', '+-1', 'nan', 'inf', '-inf', '1e50')]
    invalid += [['--origin', value] for value in ('1,2tail', '1,2,3', '1', ',2', '1,', 'nan,2', '1,inf', '')]
    invalid += [['--up', value] for value in ('x', '', 'yz')]
    invalid += [[option] for option in ('--tile', '--gain', '--texels', '--max-texture', '--out', '--install', '--origin', '--up')]
    invalid += [['--tile', '--quiet'], ['--install', '--quiet']]
    def run(label, options):
        result = subprocess.run([str(args.exe.resolve()), 'export', 'TeleporterGreatwood',
            '--no-textures', '--out', str(output), '--install', str(root), *options],
            cwd=work, capture_output=True, text=True, timeout=120)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result
    for index, options in enumerate(invalid):
        result = run('invalid_' + str(index), options)
        (work / f'invalid_{index}.json').write_text(json.dumps({'options': options,
            'returncode': result.returncode}, indent=2), encoding='utf-8')
        assert result.returncode == 2, (options, result.stdout, result.stderr)
        assert output.read_bytes() == b'prior exported model', options
    result = run('valid', ['--texels', '+8', '--max-texture', '0256', '--tile', '+8e0',
                           '--gain', '+1.25', '--origin', '+16,-32', '--up', 'Z'])
    assert result.returncode == 0, result.stdout + result.stderr
    doc, binary = parse_glb(output)
    vertices, triangles, _ = validate(doc, binary, False)
    position = doc['accessors'][doc['meshes'][0]['primitives'][0]['attributes']['POSITION']]
    assert position['min'][0] == 16 and position['min'][1] == -32, position
    assert snapshot() == original
    (work / 'report.json').write_text(json.dumps({'invalid_cases': len(invalid),
        'vertices': vertices, 'triangles': triangles, 'source_unchanged': True}, indent=2), encoding='utf-8')
    print('Export numeric/option refusals, valid GLB and source preservation: PASS')


if __name__ == '__main__':
    main()
