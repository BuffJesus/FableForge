#!/usr/bin/env python3
"""Verify archived special-effect links through direct apply and final composition."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from test_fmp_links import read_bank


MEMBER = 'mods/Fireberserk.fmp'


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
    with tempfile.TemporaryDirectory(prefix='fmp-special-effects-', dir=repo/'build') as temp:
        work = Path(temp).resolve()
        assert work.is_relative_to((repo/'build').resolve())
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
            payload = archive.read(MEMBER)
            assert hashlib.sha256(payload).hexdigest() == catalogue[MEMBER]['sha256']
        package = work/'Fireberserk.fmp'
        package.write_bytes(payload)
        direct, merged = work/'direct', work/'merged'
        output = run(args.tool, 'fmp', 'apply', args.install, package, direct)
        assert 'resolved ' in output and 'named definition link(s)' in output
        report = json.loads(run(args.tool, 'mods', 'merge', args.install, merged,
                                '--with', package, '--json'))
        assert report['defs']['summary']['applied'] > 0
        for name in ('game.bin', 'names.bin'):
            assert digest(direct/'data/CompiledDefs'/name) == digest(
                merged/'data/CompiledDefs'/name)
        effects = read_bank(merged)[14670]
        assert effects['definition'] == 'CSpecialEffectsDef'
        data = effects['data']
        at = data.find(bytes.fromhex('02ac2d63'))
        assert at >= 0 and struct.unpack_from('<I', data, at + 4)[0] >= 2
        assert struct.unpack_from('<I', data, at + 12)[0] == 149  # LARGEFIRE
        assert struct.unpack_from('<I', data, at + 20)[0] == 217  # EARTH_TROLL_SPECIAL_THUMP_GROUND_01
    assert hashes == {str(path): digest(path) for path in sources}
    print('CSpecialEffectsDef: archived package direct/merge bytes and effect IDs agree')


if __name__ == '__main__':
    main()
