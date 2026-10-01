#!/usr/bin/env python3
"""Conflicting creation/original records must stop Restore before any writes."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    exe = parser.parse_args().exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='restore-conflicts-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    results = []
    for original in ('.forge-orig', '.atlas-orig', '.ovrbak'):
        for marker in ('.forge-created', '.atlas-created'):
            for missing in (False, True):
                root = work / f'{original[1:]}-{marker[1:]}-{missing}'
                bank = root / 'data/CompiledDefs/game.bin'
                bank.parent.mkdir(parents=True)
                bank.write_bytes(b'edited staged bank')
                Path(str(bank) + '.forgebak').write_bytes(b'retail bank')
                manifest = root / 'forge_stage_manifest.json'
                manifest.write_text(json.dumps({'files': [{'path': 'data/CompiledDefs/game.bin', 'had_original': True}]}))
                added = root / 'data/Levels/FinalAlbion/Added.lev'
                added.parent.mkdir(parents=True)
                if not missing:
                    added.write_bytes(b'latest edit')
                Path(str(added) + original).write_bytes(b'earlier edit')
                Path(str(added) + marker).write_bytes(b'created')
                def snapshot():
                    return {p.relative_to(root).as_posix(): p.read_bytes().hex()
                            for p in root.rglob('*') if p.is_file()}
                before = snapshot()
                result = subprocess.run([str(exe), 'restore', '--install', str(root), '--forget'],
                    cwd=repo, capture_output=True, text=True, timeout=30)
                log = result.stdout + result.stderr
                (work / (root.name + '.log')).write_text(log)
                after = snapshot()
                results.append({'case': root.name, 'returncode': result.returncode,
                                'preserved': before == after})
                assert result.returncode != 0 and 'conflicting' in log.lower(), log
                assert before == after, 'conflict changed targets or consumed recovery data'
                # An explicit resolution (remove the creation marker) permits recovery.
                Path(str(added) + marker).unlink()
                retry = subprocess.run([str(exe), 'restore', '--install', str(root), '--forget'],
                    cwd=repo, capture_output=True, text=True, timeout=30)
                assert retry.returncode == 0, retry.stdout + retry.stderr
                assert bank.read_bytes() == b'retail bank' and added.read_bytes() == b'earlier edit'
                assert set(snapshot()) == {'data/CompiledDefs/game.bin', 'data/Levels/FinalAlbion/Added.lev'}
    (work / 'report.json').write_text(json.dumps(results, indent=2))
    print('Restore conflict preservation and resolved retries: PASS')


if __name__ == '__main__':
    main()
