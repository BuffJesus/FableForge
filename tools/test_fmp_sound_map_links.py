#!/usr/bin/env python3
"""Check a real archived hero sound-map FMP through direct apply and composition."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from test_fmp_links import read_bank


MEMBER = 'mods/Full Hero JOB Playable Set (Variant 4).fmp'


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
    with tempfile.TemporaryDirectory(prefix='fmp-sound-map-', dir=repo/'build') as temp:
        work = Path(temp).resolve()
        assert work.is_relative_to((repo/'build').resolve())
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
            payload = archive.read(MEMBER)
            assert hashlib.sha256(payload).hexdigest() == catalogue[MEMBER]['sha256']
        package = work/'Full Hero JOB Playable Set (Variant 4).fmp'
        package.write_bytes(payload)
        direct, merged = work/'direct', work/'merged'
        output = run(args.tool, 'fmp', 'apply', args.install, package, direct)
        assert 'resolved 164 named definition link(s)' in output
        report = json.loads(run(args.tool, 'mods', 'merge', args.install, merged,
                                '--with', package, '--json'))
        assert report['defs']['summary']['applied'] == 4
        for name in ('game.bin', 'names.bin'):
            assert digest(direct/'data/CompiledDefs'/name) == digest(
                merged/'data/CompiledDefs'/name)
        sound = read_bank(merged)[10534]
        assert sound['definition'] == 'CEntitySoundDef'
        data = sound['data']
        at = data.find(bytes.fromhex('6ac85f3b'))
        assert at >= 0 and struct.unpack_from('<II', data, at + 4) == (0, 1)
        names = (merged/'data/CompiledDefs/names.bin').read_bytes()
        name_offset = struct.unpack_from('<I', data, at + 16)[0]
        assert names[name_offset + 20:name_offset + 28] == b'ON_FIRE\0'
    assert hashes == {str(path): digest(path) for path in sources}
    print('CEntitySoundDef: archived package direct/merge bytes and ON_FIRE offset agree')


if __name__ == '__main__':
    main()
