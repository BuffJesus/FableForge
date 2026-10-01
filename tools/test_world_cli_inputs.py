#!/usr/bin/env python3
"""Reject malformed and overflowing world coordinates before writing placement files."""
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
    work = Path(tempfile.mkdtemp(prefix='world-cli-inputs-', dir=repo / 'build')).resolve()
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
    invalid = [['2176tail', '8064'], ['2176', '8064tail'], ['oops', '8064'], ['', '8064'],
        [' 2176', '8064'], ['2176 ', '8064'], ['1.5', '8064'], ['nan', '8064'],
        ['2147483648', '8064'], ['-2147483649', '8064'], ['999999999999999999999', '8064'],
        ['+','8064'], ['+-2176','8064'], ['0x880','8064'], ['2176','8064','--unknown'],
        ['2176','8064','--install']]
    for index, values in enumerate(invalid):
        result = run('invalid_' + str(index), ['world-move', 'OrchardFarm', *values])
        after = snapshot()
        report = {'arguments': values, 'exit': result.returncode, 'unchanged': after == original}
        (work / ('invalid_' + str(index) + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
        assert result.returncode == 2 and after == original, report
    for index, coordinates in enumerate([('2147483616', '0'), ('0', '2147483616'), ('-2147483648', '0')]):
        result = run('outside_' + str(index), ['world-move', 'OrchardFarm', *coordinates])
        assert result.returncode == 1 and snapshot() == original, result.stdout + result.stderr
    result = run('valid', ['world-move', 'OrchardFarm', '+2176', '08064'])
    assert result.returncode == 0 and 'moved 1 map(s)' in result.stdout, result.stdout + result.stderr
    from test_overworld import bwd_box, wld_pos
    assert wld_pos(root / 'data/Levels/FinalAlbion.wld', 'OrchardFarm') == (2176, 8064)
    assert bwd_box(root / 'data/Levels/FinalAlbion.bwd', 'OrchardFarm') == (2176, 8064, 2272, 8192)
    restored = run('restore', ['restore', '--forget'])
    assert restored.returncode == 0 and snapshot() == original, restored.stdout + restored.stderr
    print('Strict world coordinates, extreme bounds, valid move and exact Restore: PASS')



if __name__ == '__main__':
    main()
