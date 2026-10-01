#!/usr/bin/env python3
"""Reject malformed world visibility flags and incomplete region options before writes."""
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
    work = Path(tempfile.mkdtemp(prefix='world-flag-inputs-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    for name in ('data/CompiledDefs/game.bin', 'data/Levels/FinalAlbion.wld', 'data/Levels/FinalAlbion.bwd'):
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
    def run(label, command):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=90)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result
    invalid = []
    for value in ('oops', '2', '-1', '', ' 1', '1 ', '1tail', '0.0', 'true', '4294967296'):
        invalid.append(['region-props', 'Greatwood', '--worldmap', value])
        invalid.append(['world-sees', 'Greatwood', 'OrchardFarm', value])
    invalid += [['region-props', 'Greatwood', '--display', 'MustNotWrite', '--install', str(root), '--worldmap'],
                ['region-props', 'Greatwood', '--display', 'MustNotWrite', '--install', str(root), '--unknown'],
                ['region-props', 'Greatwood', '--worldmap', '1', '--install', str(root), '--install']]
    for index, command in enumerate(invalid):
        if '--install' in command:
            result = subprocess.run([str(args.exe.resolve()), *command], cwd=repo, capture_output=True, text=True, timeout=90)
            (work / ('invalid_' + str(index) + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        else:
            result = run('invalid_' + str(index), command)
        after = snapshot()
        report = {'arguments': command, 'exit': result.returncode, 'unchanged': after == original}
        (work / ('invalid_' + str(index) + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
        assert result.returncode == 2 and after == original, report
    for index, command in enumerate([
            ['region-props', 'Greatwood', '--worldmap', '0'],
            ['region-props', 'Greatwood', '--worldmap', '1'],
            ['world-sees', 'Greatwood', 'OrchardFarm', '1'],
            ['world-sees', 'Greatwood', 'OrchardFarm', '0']]):
        result = run('valid_' + str(index), command)
        assert result.returncode == 0, result.stdout + result.stderr
    restored = run('restore', ['restore', '--forget'])
    assert restored.returncode == 0 and snapshot() == original, restored.stdout + restored.stderr
    print('Strict world flags, incomplete option refusal, valid edits and exact Restore: PASS')



if __name__ == '__main__':
    main()
