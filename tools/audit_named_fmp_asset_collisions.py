#!/usr/bin/env python3
"""Independently verify reported asset-ID collisions; never allocate or install IDs."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import tempfile
import zipfile
import zlib
from test_fmp_texture_layers import entries


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--coverage', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--work-root', required=True, type=Path)
    args = parser.parse_args()
    coverage = json.loads(args.coverage.read_text())
    assert coverage['complete']
    assets = {'maintextures': ('data/graphics/pc/textures.big', 'GBANK_MAIN_PC'),
              'graphics': ('data/graphics/graphics.big', 'MBANK_ALLMESHES')}
    inputs = [args.archive, args.coverage] + [args.install/p for p, _ in assets.values()]
    before = {str(p): digest(p) for p in inputs}
    assert before[str(args.archive)] == coverage['archive_sha256']
    baseline = {}
    for source_bank, (relative, bank) in assets.items():
        assert before[str(args.install/relative)] == coverage['baseline_asset_sha256'][relative]
        baseline[source_bank] = entries(args.install/relative, bank, include_payload=False)
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='named-fmp-asset-collisions-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    pattern = re.compile(r'^error: FMP (maintextures|graphics): id (\d+) already belongs to (.+); cannot add (.+)$')
    checked = []; other = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for row in coverage['rows']:
            if row['category'] != 'other FMP':
                continue
            match = pattern.fullmatch(row['reason'])
            if not match:
                other.append({'file': row['file'], 'reason': row['reason']})
                continue
            bank, identity, vanilla_name, mod_name = match.groups(); identity = int(identity)
            assert row['sha256'] == catalogue[row['file']]['sha256']
            payload = archive.read(row['file'])
            assert hashlib.sha256(payload).hexdigest() == row['sha256']
            if payload[:4] == b'90\0\0':
                decoder = zlib.decompressobj()
                payload = decoder.decompress(payload[4:], 256*1024*1024+1)
                assert len(payload) <= 256*1024*1024 and decoder.eof
                assert not decoder.unused_data and not decoder.unconsumed_tail
            package = work/'normalized.fmp'; package.write_bytes(payload)
            source_entries = entries(package, bank, include_payload=False)
            source = source_entries[mod_name]
            original = baseline[bank].get(vanilla_name)
            scope = 'baseline'
            if original is None:
                original = source_entries[vanilla_name]
                order = list(source_entries)
                assert order.index(vanilla_name) < order.index(mod_name)
                scope = 'earlier package entry'
            assert source['id'] == original['id'] == identity
            assert mod_name not in baseline[bank], 'replacement is not a new-symbol collision'
            checked.append({'file': row['file'], 'sha256': row['sha256'], 'bank': bank,
                'id': identity, 'source_name': mod_name, 'baseline_name': vanilla_name,
                'collision_scope': scope,
                'source_type': source['type'], 'baseline_type': original['type'],
                'game_link_metadata_present': 'GameBINLinkMetaData' in row['banks'],
                'graphics_link_metadata_present': 'graphicsLinkMetaData' in row['banks']})
            package.unlink()
    assert before == {str(p): digest(p) for p in inputs}
    result = {'coverage_sha256': before[str(args.coverage)], 'archive_sha256': before[str(args.archive)],
              'verified_reported_collisions': len(checked),
              'by_bank': dict(collections.Counter(r['bank'] for r in checked)),
              'by_scope': dict(collections.Counter(r['collision_scope'] for r in checked)),
              'rows': checked, 'other_asset_refusals': other, 'inputs_unchanged': True,
              'ids_allocated': False, 'compatibility_verified': False,
              'scope': 'First reported collision per package; subsequent failures are unexamined.'}
    (work/'result.json').write_text(json.dumps(result, indent=2))
    print('Verified:', len(checked), 'reported collisions;', result['by_bank'])


if __name__ == '__main__':
    main()
