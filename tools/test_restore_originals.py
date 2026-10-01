#!/usr/bin/env python3
"""Multiple original conventions must agree before Restore consumes any of them."""
import argparse
from itertools import combinations
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    exe = parser.parse_args().exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='restore-originals-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    def snapshot(root):
        return {p.relative_to(root).as_posix(): p.read_bytes().hex() for p in root.rglob('*') if p.is_file()}
    def run(root, label):
        result = subprocess.run([str(exe), 'restore', '--install', str(root), '--forget'],
            cwd=repo, capture_output=True, text=True, timeout=30)
        (work / (label + '.log')).write_text(result.stdout + result.stderr)
        return result
    for left, right in combinations(('.forge-orig', '.atlas-orig', '.ovrbak'), 2):
        for equal in (False, True):
            label = left[1:] + '-' + right[1:] + '-' + str(equal)
            root = work / label
            target = root / 'data/CompiledDefs/game.bin'
            target.parent.mkdir(parents=True)
            target.write_bytes(b'current edit')
            first = Path(str(target) + left)
            second = Path(str(target) + right)
            first.write_bytes(b'original one')
            second.write_bytes(b'original one' if equal else b'original two')
            staged = root / 'stage-added'
            staged.write_bytes(b'staged file')
            manifest = root / 'forge_stage_manifest.json'
            manifest.write_text(json.dumps({'files': [{'path': 'stage-added', 'had_original': False}]}))
            before = snapshot(root)
            result = run(root, label)
            if not equal:
                assert result.returncode != 0 and 'conflicting' in result.stdout + result.stderr, result.stdout + result.stderr
                assert before == snapshot(root), 'conflicting originals changed targets or consumed recovery data'
                second.unlink()  # explicit selection of the first baseline
                result = run(root, label + '-retry')
            assert result.returncode == 0, result.stdout + result.stderr
            assert target.read_bytes() == b'original one'
            assert set(snapshot(root)) == {'data/CompiledDefs/game.bin'}
    print('Conflicting originals preserved; agreeing originals and explicit retries restored: PASS')


if __name__ == '__main__':
    main()
