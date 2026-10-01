#!/usr/bin/env python3
"""Texture commands must refuse unknown, inapplicable and incomplete options."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_meshimport import write_png


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='texture-cli-inputs-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    for name in ('data/CompiledDefs/game.bin', 'data/graphics/pc/textures.big'):
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    image = work / 'input.png'
    write_png(str(image))
    output = work / 'output.png'
    output.write_bytes(b'previous export')

    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result

    original = snapshot()
    def run(label, command):
        result = subprocess.run([str(args.exe.resolve()), command[0], '--install', str(root), *command[1:]],
            cwd=repo, capture_output=True, text=True, timeout=120)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result

    add = ['texture-add', 'FORGE_INPUT_PROBE', str(image)]
    replace = ['texture-replace', 'BARREL_BRACED_1_24', str(image)]
    export = ['texture-export', 'BARREL_BRACED_1_24', str(output)]
    cases = []
    for base in (add, replace, export, ['textures']):
        cases += [base + ['--typo'], base + ['--bank'], base + ['--format'], base + ['--install'],
                  base + ['--install', ''], base + ['--bank', ''], base + ['--format', '']]
    for base in (add, replace, export):
        cases.append(base + ['unexpected'])
        cases.append([base[0], '', base[2]])
        cases.append([base[0], base[1], ''])
    cases += [['textures', 'one', 'two'], ['textures', '--format', 'dxt1'],
              replace + ['--bank', 'GBANK_MAIN_PC'], replace + ['--format', 'dxt3'],
              export + ['--bank', 'GBANK_MAIN_PC'], export + ['--format', 'dxt3']]
    for index, command in enumerate(cases):
        result = run(str(index), command)
        after = snapshot()
        report = {'args': command, 'returncode': result.returncode,
                  'unchanged': after == original, 'export_preserved': output.read_bytes() == b'previous export'}
        (work / (str(index) + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
        assert result.returncode != 0 and report['unchanged'] and report['export_preserved'], report
    for label, command in [('add', add + ['--bank', 'GBANK_MAIN_PC', '--format', 'dxt3']),
                           ('replace', ['texture-replace', 'FORGE_INPUT_PROBE', str(image)]),
                           ('export', ['texture-export', 'FORGE_INPUT_PROBE', str(output)])]:
        result = run(label, command)
        assert result.returncode == 0, result.stdout + result.stderr
    assert output.read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
    listed = run('list', ['textures', 'FORGE_INPUT_PROBE', '--bank', 'GBANK_MAIN_PC'])
    assert listed.returncode == 0 and '1 of ' in listed.stdout
    restored = run('restore', ['restore', '--forget'])
    assert restored.returncode == 0 and snapshot() == original
    print('Texture option refusals, valid add/replace/export/list and exact Restore: PASS')


if __name__ == '__main__':
    main()
