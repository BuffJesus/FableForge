#!/usr/bin/env python3
"""Restore must validate stage ownership before changing editor baselines."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    exe = parser.parse_args().exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='restore-stage-plan-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    def put(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path
    def snapshot(root):
        return {p.relative_to(root).as_posix(): p.read_bytes().hex() for p in root.rglob('*') if p.is_file()}
    def run(root, label):
        result = subprocess.run([str(exe), 'restore', '--install', str(root), '--forget'],
            cwd=repo, capture_output=True, text=True, timeout=30)
        (work / (label + '.log')).write_text(result.stdout + result.stderr)
        return result
    def fixture(name, entries):
        root = work / name
        bank = put(root / 'data/CompiledDefs/game.bin', b'edited staged bank')
        original = put(Path(str(bank) + '.forge-orig'), b'staged bank')
        staged = put(Path(str(bank) + '.forgebak'), b'retail bank')
        os.utime(staged, (1700000000, 1700000000))
        manifest = root / 'forge_stage_manifest.json'
        manifest.write_text(json.dumps({'files': entries}))
        return root, bank, original, staged, manifest
    bank_entry = {'path': 'data/CompiledDefs/game.bin', 'had_original': True}
    root, bank, original, staged, manifest = fixture('missing', [bank_entry, {'path': 'missing', 'had_original': True}])
    before = snapshot(root)
    result = run(root, 'missing')
    assert result.returncode != 0 and 'missing original' in result.stdout + result.stderr
    assert before == snapshot(root), 'invalid stage changed an editor baseline before validation'
    put(root / 'missing.forgebak', b'missing original')
    assert run(root, 'missing_retry').returncode == 0
    assert bank.read_bytes() == b'retail bank'
    assert (root / 'missing').read_bytes() == b'missing original'
    assert not original.exists() and not staged.exists() and not manifest.exists()

    root, bank, original, staged, manifest = fixture('unowned', [{'path': 'mod-added', 'had_original': False}])
    put(root / 'mod-added', b'mod file')
    result = run(root, 'unowned')
    assert result.returncode == 0, result.stdout + result.stderr
    assert bank.read_bytes() == b'staged bank', 'unowned staged backup replaced the editor original'
    assert staged.read_bytes() == b'retail bank' and not original.exists()
    assert not (root / 'mod-added').exists() and not manifest.exists()

    for suffix in ('.forge-orig', '.atlas-orig', '.ovrbak'):
        root, bank, original, staged, manifest = fixture('created-' + suffix[1:], [bank_entry, {'path': 'data/Levels/FinalAlbion/New.tng', 'had_original': False}])
        added = put(root / 'data/Levels/FinalAlbion/New.tng', b'edited mod file')
        backup = put(Path(str(added) + suffix), b'mod file')
        before = snapshot(root)
        result = run(root, 'created-' + suffix[1:])
        assert result.returncode != 0 and 'conflicting' in result.stdout + result.stderr
        assert before == snapshot(root), 'stage-created conflict consumed recovery evidence'
        backup.unlink()  # explicit resolution: the stage creation record is authoritative
        assert run(root, 'created_retry-' + suffix[1:]).returncode == 0
        assert bank.read_bytes() == b'retail bank' and not added.exists()
        assert set(snapshot(root)) == {'data/CompiledDefs/game.bin'}
    print('Stage preflight, ownership and creation conflicts: PASS')


if __name__ == '__main__':
    main()
