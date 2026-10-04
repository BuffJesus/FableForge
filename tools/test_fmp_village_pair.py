#!/usr/bin/env python3
"""Check ordered duplicate village links in a named package and guarded refusal."""
import argparse
import hashlib
import json
import subprocess
import tempfile
import zipfile
from pathlib import Path


PACKAGE = 'mods/Knothole Glade Children.fmp'
RECORD = 'VILLAGE_KNOTHOLE_GLADE - KH_CHILDREN - CVillagePeopleDef'
PATH = '4272390883.VillagePeopleID'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    source_hashes = {str(p): digest(p) for p in
                     (args.archive, args.install/'data/CompiledDefs/game.bin')}
    with tempfile.TemporaryDirectory(prefix='fmp-village-pair-', dir=repo/'build') as temp:
        work = Path(temp).resolve()
        assert work != (repo/'build').resolve() and work.is_relative_to((repo/'build').resolve())
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
            payload = archive.read(PACKAGE)
            assert hashlib.sha256(payload).hexdigest() == catalogue[PACKAGE]['sha256']
            package = work/'knothole_children.fmp'
            package.write_bytes(payload)
        groups = subprocess.run([str(args.tool), 'fmp', 'links', str(package), '--groups', '--json'],
                                capture_output=True, text=True, timeout=30)
        assert groups.returncode == 0, groups.stderr
        grouped = next(row for row in json.loads(groups.stdout)['records'] if row['record'] == RECORD)
        pair = [group for group in grouped['groups'] if group['path'] == PATH]
        assert len(pair) == 1 and pair[0]['targets'] == ['NULLDEF_CHeroMorphDef'] * 2
        links = subprocess.run([str(args.tool), 'fmp', 'links', str(package), '--json'],
                               capture_output=True, text=True, timeout=30)
        assert links.returncode == 0, links.stderr
        record = next(row for row in json.loads(links.stdout)['records'] if row['record'] == RECORD)
        expanded = [link for link in record['links'] if link['path'].startswith(PATH)]
        assert expanded == [{'path': PATH+'.0', 'target': 'NULLDEF_CHeroMorphDef'},
                            {'path': PATH+'.1', 'target': 'NULLDEF_CHeroMorphDef'}]
        output = work/'should-not-exist'
        merged = subprocess.run([str(args.tool), 'mods', 'merge', str(args.install), str(output),
                                 '--with', str(package), '--json'], capture_output=True,
                                text=True, timeout=180)
        assert merged.returncode != 0 and 'TorsoMainTextureID' in merged.stderr, merged.stderr
        assert not output.exists(), 'remaining unsupported creature links must leave output untouched'
    assert source_hashes == {str(p): digest(p) for p in
                             (args.archive, args.install/'data/CompiledDefs/game.bin')}
    print('Knothole village pair preserves both targets; creature weapon/brain links '
          'pass and remaining texture link refuses safely')


if __name__ == '__main__':
    main()
