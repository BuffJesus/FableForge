#!/usr/bin/env python3
"""Concurrent definitions roundtrips must preserve unrelated scratch files."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
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
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge-tools.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='defs-workspace-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root, temp = work / 'install', work / 'temp'
    defs = root / 'data/CompiledDefs'
    defs.mkdir(parents=True)
    for name in ('game.bin', 'names.bin'):
        shutil.copyfile(args.root / 'data/CompiledDefs' / name, defs / name)
    def digest(p):
        with p.open('rb') as f:
            return hashlib.file_digest(f, 'sha256').hexdigest()
    originals = {p: digest(p) for p in defs.iterdir()}
    marker = temp / 'forge_defs_roundtrip/unrelated.txt'
    marker.parent.mkdir(parents=True)
    marker.write_bytes(b'unrelated defs workspace')
    env = dict(os.environ, TEMP=str(temp), TMP=str(temp))
    def run(index):
        result = subprocess.run([str(args.exe.resolve()), 'defs', 'roundtrip', str(root)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=60)
        (work / f'roundtrip_{index}.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0 and 'entries semantically identical' in result.stdout, result.stdout + result.stderr
        return result.stdout
    with ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(run, range(2)))
    assert results[0] == results[1]
    assert marker.is_file() and marker.read_bytes() == b'unrelated defs workspace', 'roundtrip deleted an unowned workspace'
    assert not list((temp / 'FableForge').glob('defs-roundtrip-*')), 'roundtrip leaked an owned workspace'
    assert all(digest(p) == h for p, h in originals.items())
    print('Concurrent semantic roundtrips, source hashes and workspace ownership: PASS')


if __name__ == '__main__':
    main()
