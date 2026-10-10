#!/usr/bin/env python3
"""Read-only mesh identity audit for the named historic FMP archive."""
import argparse
import collections
import hashlib
import json
import subprocess
import struct
import tempfile
import zipfile
import zlib
from pathlib import Path

from test_fmp_texture_layers import entries


GRAPHICS = Path('data/graphics/graphics.big')
TEXTURES = Path('data/graphics/pc/textures.big')


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
    build = (repo/'build').resolve()
    archive_hash, graphics_hash = digest(args.archive), digest(args.install/GRAPHICS)
    retail = entries(args.install/GRAPHICS, 'MBANK_ALLMESHES', include_payload=False)
    retail_ids = {e['id']: name for name, e in retail.items()}
    retail_textures = entries(args.install/TEXTURES, 'GBANK_MAIN_PC', include_payload=False)
    retail_texture_ids = {e['id']: name for name, e in retail_textures.items()}
    rows = []
    link_paths = collections.Counter()
    with tempfile.TemporaryDirectory(prefix='named-fmp-graphics-', dir=build) as temporary:
        work = Path(temporary).resolve()
        assert work != build and work.is_relative_to(build)
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = json.loads(archive.read('catalogue.json'))
            for record in catalogue:
                if 'graphics' not in record['banks']:
                    continue
                payload = archive.read(record['file'])
                assert hashlib.sha256(payload).hexdigest() == record['sha256']
                if record['format'] == 'wrapped-zlib':
                    assert payload[:4] == b'90\0\0'
                    decoder = zlib.decompressobj()
                    payload = decoder.decompress(payload[4:], 256 * 1024 * 1024 + 1)
                    assert len(payload) <= 256 * 1024 * 1024 and decoder.eof
                    assert not decoder.unused_data and not decoder.unconsumed_tail
                package = work/'current.fmp'
                package.write_bytes(payload)
                problems = []
                try:
                    changed = entries(package, 'graphics', include_payload=False)
                    links = entries(package, 'graphicsLinkMetaData', include_payload=False)
                except (ValueError, OSError, IndexError) as error:
                    changed, links = {}, {}
                    problems.append(str(error))
                source_ids = {}
                replaced = added = 0
                for name, entry in changed.items():
                    if not name or entry['length'] == 0:
                        problems.append(name + ': missing name or payload')
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
                        if entry['id'] in retail_ids:
                            problems.append(name + ': new name takes retail id of ' + retail_ids[entry['id']])
                if set(links) - set(changed):
                    problems.append('graphics link metadata without matching graphics entry')
                texture_identity_problems = []
                try:
                    local_textures = entries(package, 'maintextures', include_payload=False)
                except ValueError as error:
                    local_textures = {}
                    texture_identity_problems.append(str(error))
                for name, texture in local_textures.items():
                    original = retail_textures.get(name)
                    if original and (texture['id'], texture['type']) != (original['id'], original['type']):
                        texture_identity_problems.append(name + ': retail texture id/type mismatch')
                    elif not original and texture['id'] in retail_texture_ids:
                        texture_identity_problems.append(name + ': new texture id belongs to ' +
                                                         retail_texture_ids[texture['id']])
                decoded_links = 0
                link_error = ''
                missing_targets = set()
                unmatched_target_ids = set()
                if links:
                    result = subprocess.run([str(args.tool), 'fmp', 'links', str(package),
                                             '--graphics', '--json'], capture_output=True, text=True,
                                            timeout=30, cwd=repo)
                    if result.returncode:
                        link_error = result.stderr.strip()[-400:]
                    else:
                        for mesh in json.loads(result.stdout)['records']:
                            for link in mesh['links']:
                                link_paths[link['path'].split('.')[-1]] += 1
                                decoded_links += 1
                                target = local_textures.get(link['target'], retail_textures.get(link['target']))
                                if target is None:
                                    missing_targets.add(link['target'])
                                elif mesh['record'] in changed:
                                    header = changed[mesh['record']]['header']
                                    needle = struct.pack('<I', target['id'])
                                    if all(header[at:at+4] != needle for at in range(0, len(header)-3, 4)):
                                        unmatched_target_ids.add(mesh['record'] + ':' + link['target'])
                rows.append({'file': record['file'], 'sha256': record['sha256'],
                             'banks': record['banks'], 'entries': len(changed),
                             'links': len(links), 'replaced': replaced, 'added': added,
                             'decoded_links': decoded_links, 'link_error': link_error,
                             'missing_texture_targets': sorted(missing_targets),
                             'unmatched_texture_ids': sorted(unmatched_target_ids),
                             'problems': problems, 'texture_identity_problems': texture_identity_problems,
                             'metadata_compatible': not problems,
                             'self_contained': not problems and not texture_identity_problems and
                                               not missing_targets and not unmatched_target_ids and not link_error})
                package.unlink()
    assert digest(args.archive) == archive_hash and digest(args.install/GRAPHICS) == graphics_hash
    result = {'archive_sha256': archive_hash, 'retail_graphics_sha256': graphics_hash,
              'packages': len(rows), 'entry_totals': {
                  'replaced': sum(r['replaced'] for r in rows),
                  'added': sum(r['added'] for r in rows)},
              'status_counts': dict(collections.Counter('compatible' if r['metadata_compatible']
                   else 'problem' for r in rows)), 'rows': rows,
              'self_contained_packages': sum(r['self_contained'] for r in rows),
              'link_paths': dict(link_paths),
              'full_big_writes_tested': False, 'gameplay_tested': False}
    output = build/'named-fmp-graphics-audit.json'
    output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(result['packages'], 'graphics packages:', result['status_counts'],
          'self-contained:', result['self_contained_packages'],
          result['entry_totals'], 'report:', output)


if __name__ == '__main__':
    main()
