#!/usr/bin/env python3
"""Reject malformed minimap IDs and options before touching definition banks."""
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
    work = Path(tempfile.mkdtemp(prefix='minimap-cli-inputs-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    for name in ('game.bin', 'names.bin'):
        target = root / 'data/CompiledDefs' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / 'data/CompiledDefs' / name, target)
    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    def run(label, command):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=90)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result
    invalid = [['oops'], ['12tail'], ['-1'], ['4294967296'], ['999999999999999999999999'],
        [''], [' 1'], ['1 '], ['+'], ['+-1'], ['0x'], ['0x100000000'], ['08'], ['1.5'],
        ['1', '--unknown'], ['1', 'extra'], ['1', '--install']]
    for index, values in enumerate(invalid):
        result = run('invalid_' + str(index), ['minimap-register', 'MINIMAP_INPUT_PROBE', *values])
        after = snapshot()
        report = {'arguments': values, 'exit': result.returncode, 'unchanged': after == original}
        (work / ('invalid_' + str(index) + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
        assert result.returncode == 2 and after == original, report
    for index, (value, expected) in enumerate([('16', 16), ('0x10', 16), ('020', 16),
                                              ('+16', 16), ('0', 0), ('4294967295', 4294967295)]):
        result = run('valid_' + str(index), ['minimap-register', 'MINIMAP_INPUT_PROBE', value])
        assert result.returncode == 0 and (' -> ' + str(expected) + ' (') in result.stdout, result.stdout + result.stderr
        restored = run('restore_' + str(index), ['restore', '--forget'])
        assert restored.returncode == 0 and snapshot() == original, restored.stdout + restored.stderr
    print('Strict minimap IDs/options, supported integer forms and exact Restore: PASS')


if __name__ == '__main__':
    main()
