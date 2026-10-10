#!/usr/bin/env python3
"""Read-only identity audit of texture-capable named historic FMPs."""
import argparse
import collections
import hashlib
import json
import tempfile
import zipfile
import zlib
from pathlib import Path

from test_fmp_texture_layers import TEXTURES, entries


OTHER_SUPPORTED = {'GameBINEntries', 'GameBINLinkMetaData', 'Objects',
                   'Settings', 'ObjectLinks', 'Text', 'FinalAlbionWAD'}


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    build = (repo/'build').resolve()
    archive_hash, textures_hash = digest(args.archive), digest(args.install/TEXTURES)
    retail = entries(args.install/TEXTURES, 'GBANK_MAIN_PC', include_payload=False)
    ids = {e['id']: name for name, e in retail.items()}
    with tempfile.TemporaryDirectory(prefix='named-fmp-texture-', dir=build) as temporary:
        work = Path(temporary).resolve()
        assert work != build and work.is_relative_to(build)
        rows = []
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = json.loads(archive.read('catalogue.json'))
            for record in catalogue:
                banks = set(record['banks'])
                if 'maintextures' not in banks or not banks <= OTHER_SUPPORTED | {'maintextures'}:
                    continue
                payload = archive.read(record['file'])
                assert hashlib.sha256(payload).hexdigest() == record['sha256']
                if record['format'] == 'wrapped-zlib':
                    assert payload[:4] == b'90\0\0'
                    decoder = zlib.decompressobj()
                    payload = decoder.decompress(payload[4:], 256 * 1024 * 1024 + 1)
                    assert len(payload) <= 256 * 1024 * 1024 and decoder.eof
                    assert not decoder.unused_data and not decoder.unconsumed_tail
                package = work/'current.fmp'; package.write_bytes(payload)
                try:
                    changed = entries(package, 'maintextures', include_payload=False)
                    problems = []
                except (ValueError, OSError, IndexError) as error:
                    changed = {}
                    problems = [str(error)]
                source_ids = {}
                replaced = added = 0
                for name, entry in changed.items():
                    if not name or entry['length'] == 0 or len(entry['header']) != 34:
                        problems.append(name + ': missing name/payload or unexpected header length')
                    if entry['id'] in source_ids and source_ids[entry['id']] != name:
                        problems.append(name + ': package id also used by ' + source_ids[entry['id']])
                    source_ids[entry['id']] = name
                    base = retail.get(name)
                    if base:
                        replaced += 1
                        if (entry['id'], entry['type']) != (base['id'], base['type']):
                            problems.append(name + ': retail id/type mismatch')
                    else:
                        added += 1
                        if entry['id'] in ids:
                            problems.append(name + ': new name takes retail id of ' + ids[entry['id']])
                rows.append({'file': record['file'], 'sha256': record['sha256'],
                             'banks': record['banks'], 'entries': len(changed),
                             'replaced': replaced, 'added': added, 'problems': problems,
                             'metadata_compatible': not problems})
                package.unlink()
        assert digest(args.archive) == archive_hash and digest(args.install/TEXTURES) == textures_hash
        result = {'archive_sha256': archive_hash, 'retail_textures_sha256': textures_hash,
                  'packages': len(rows), 'entry_totals': {
                      'replaced': sum(r['replaced'] for r in rows),
                      'added': sum(r['added'] for r in rows)},
                  'status_counts': dict(collections.Counter('compatible' if r['metadata_compatible']
                       else 'problem' for r in rows)), 'rows': rows,
                  'full_big_writes_tested': False, 'gameplay_tested': False}
        output = build/'named-fmp-texture-audit.json'
        output.write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(result['packages'], 'texture-capable packages:', result['status_counts'],
              result['entry_totals'], 'report:', output)


if __name__ == '__main__':
    main()
