#!/usr/bin/env python3
"""Verify a real archived CREATURE ranged-weapon FMP across direct and merged paths."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from test_fmp_links import read_bank


MEMBER = 'mods/No body Rots.fmp'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(tool, *args):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True,
                            text=True, timeout=180)
    assert result.returncode == 0, result.stderr[-2000:]
    return result.stdout


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    sources = (args.archive, args.install/'data/CompiledDefs/game.bin',
               args.install/'data/CompiledDefs/names.bin')
    hashes = {str(path): digest(path) for path in sources}
    with tempfile.TemporaryDirectory(prefix='fmp-creature-weapon-', dir=repo/'build') as temp:
        work = Path(temp).resolve()
        assert work.is_relative_to((repo/'build').resolve())
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
            payload = archive.read(MEMBER)
            assert hashlib.sha256(payload).hexdigest() == catalogue[MEMBER]['sha256']
        package = work/'No body Rots.fmp'
        package.write_bytes(payload)
        direct, merged = work/'direct', work/'merged'
        output = run(args.tool, 'fmp', 'apply', args.install, package, direct)
        assert 'resolved 47328 named definition link(s)' in output
        report = json.loads(run(args.tool, 'mods', 'merge', args.install, merged,
                                '--with', package, '--json'))
        assert report['defs']['summary']['applied'] == 505
        for name in ('game.bin', 'names.bin'):
            assert digest(direct/'data/CompiledDefs'/name) == digest(
                merged/'data/CompiledDefs'/name)
        records = read_bank(merged)
        target_id = next(i for i, entry in enumerate(records)
                         if entry['name'] == 'OBJECT_BANDIT_CROSSBOW')
        assert target_id == 5595
        archer = next(entry for entry in records if entry['name'] == 'CREATURE_BANDIT_ARCHER')
        marker = archer['data'].find(bytes.fromhex('2a2015c5'))
        assert marker >= 0
        assert struct.unpack_from('<I', archer['data'], marker + 4)[0] == target_id
    assert hashes == {str(path): digest(path) for path in sources}
    print('CREATURE Weapon3ID: archived package direct/merge bytes and target ID agree')


if __name__ == '__main__':
    main()
