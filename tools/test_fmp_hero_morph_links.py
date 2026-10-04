#!/usr/bin/env python3
"""Check an archived hero morph FMP through direct apply and mod composition."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from test_fmp_links import read_bank


MEMBER = 'mods/Clean Hero.fmp'


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
    with tempfile.TemporaryDirectory(prefix='fmp-hero-morph-', dir=repo/'build') as temp:
        work = Path(temp).resolve()
        assert work.is_relative_to((repo/'build').resolve())
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
            payload = archive.read(MEMBER)
            assert hashlib.sha256(payload).hexdigest() == catalogue[MEMBER]['sha256']
        package = work/'Clean Hero.fmp'
        package.write_bytes(payload)
        direct, merged = work/'direct', work/'merged'
        output = run(args.tool, 'fmp', 'apply', args.install, package, direct)
        assert 'resolved 144 named definition link(s)' in output
        report = json.loads(run(args.tool, 'mods', 'merge', args.install, merged,
                                '--with', package, '--json'))
        assert report['defs']['summary']['applied'] == 1
        for name in ('game.bin', 'names.bin'):
            assert digest(direct/'data/CompiledDefs'/name) == digest(
                merged/'data/CompiledDefs'/name)
        hero = read_bank(merged)[10535]
        assert hero['definition'] == 'CHeroMorphDef'
        # First texture morph: independently observed retail texture IDs.
        data = hero['data']
        at = data.find(struct.pack('<II', 1250, 1025))
        assert at >= 0
    assert hashes == {str(path): digest(path) for path in sources}
    print('CHeroMorphDef: archived package direct/merge bytes and texture IDs agree')


if __name__ == '__main__':
    main()
