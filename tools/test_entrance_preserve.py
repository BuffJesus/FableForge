#!/usr/bin/env python3
"""Move an entrance in copied retail GTG without losing custom fields, then Restore."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
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
    work = Path(tempfile.mkdtemp(prefix='entrance-preserve-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin']
    for name in files:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    original = {name: digest(root / name) for name in files}
    def run(label, *command):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=60)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout
    run('create', 'entrance', 'TeleporterGreatwood', '1', '2', '3')
    gtg = root / 'data/Levels/FinalAlbion.gtg'
    authored = gtg.read_bytes()
    blocks = list(re.finditer(rb'NewThing [^\r\n]*;\r?\n.*?EndThing;', authored, re.DOTALL))
    index = next(i for i, block in enumerate(blocks) if b'ScriptName TeleporterGreatwoodHSP;' in block[0])
    assert index > 0 and b'DefinitionType "REGION_ENTRANCE_POINT";' in blocks[index - 1][0]
    customized, expected = authored, authored
    for ordinal in (index, index - 1):
        block = blocks[ordinal]
        custom = block[0].replace(b'EndThing;', b'CustomPreserve 123;\r\nEndThing;')
        moved = custom
        for axis, before, after in (('X', '1.0', '4.0'), ('Y', '2.0', '5.0'), ('Z', '3.0', '6.0')):
            old = f'Position{axis} {before};'.encode()
            assert old in moved
            moved = moved.replace(old, f'Position{axis} {after};'.encode())
        customized = customized[:block.start()] + custom + customized[block.end():]
        expected = expected[:block.start()] + moved + expected[block.end():]
    gtg.write_bytes(customized)
    run('move', 'entrance', 'TeleporterGreatwood', '4', '5', '6')
    assert gtg.read_bytes() == expected, 'entrance move changed unrelated GTG bytes'
    run('idempotent', 'entrance', 'TeleporterGreatwood', '4', '5', '6')
    assert gtg.read_bytes() == expected
    run('restore', 'restore', '--forget')
    assert original == {name: digest(root / name) for name in files}
    assert {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()} == set(files)
    (work / 'report.json').write_text(json.dumps({'custom_fields_preserved': True,
        'only_requested_positions_changed': True, 'restored_exactly': True}, indent=2))
    print('Retail GTG entrance move, custom fields and exact Restore: PASS')


if __name__ == '__main__':
    main()
