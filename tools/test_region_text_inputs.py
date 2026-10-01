#!/usr/bin/env python3
"""Region text must obey WLD quoting and token rules before any bank is changed."""
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
    work = Path(tempfile.mkdtemp(prefix='region-text-inputs-', dir=repo / 'build')).resolve()
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
    for option in ('--display', '--def'):
        for value in ('Bad"Name', 'Bad\nName', 'Bad\rName'):
            invalid.append(['region-props', 'Greatwood', option, value])
    for value in ('MINI MAP', 'MINI;MAP', 'MINI"MAP', 'MINI\nMAP', 'MINI\rMAP', 'MINI\tMAP'):
        invalid.append(['region-props', 'Greatwood', '--minimap', value])
    for index, command in enumerate(invalid):
        result = run('invalid_' + str(index), command)
        after = snapshot()
        report = {'arguments': command, 'exit': result.returncode, 'unchanged': after == original}
        (work / ('invalid_' + str(index) + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
        assert result.returncode != 0 and after == original, report
    display = "Tester's Cove; North"
    result = run('valid', ['region-props', 'Greatwood', '--display', display,
        '--def', 'REGION_GREATWOOD', '--minimap', 'MINIMAP_GREATWOOD', '--worldmap', '1'])
    assert result.returncode == 0, result.stdout + result.stderr
    wld = (root / 'data/Levels/FinalAlbion.wld').read_text(encoding='latin-1')
    assert 'NewDisplayName "' + display + '";' in wld
    assert 'RegionDef "REGION_GREATWOOD";' in wld and 'MiniMapGraphic MINIMAP_GREATWOOD;' in wld
    restored = run('restore', ['restore', '--forget'])
    assert restored.returncode == 0 and snapshot() == original, restored.stdout + restored.stderr
    print('Region quoted-text/token refusals, valid punctuation and exact Restore: PASS')




if __name__ == '__main__':
    main()
