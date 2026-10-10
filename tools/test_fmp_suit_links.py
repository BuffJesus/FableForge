#!/usr/bin/env python3
"""Verify original named and anonymous suit maps without changing clothing-part keys."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zipfile
import zlib

from test_fmp_links import package_records, read_bank
from test_fmp_weapon_links import run, field_start


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def snapshot(root):
    return {str(p.relative_to(root)): digest(p) for p in root.rglob('*') if p.is_file()}


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    inputs = [args.archive, args.tool] + [args.install/p for p in
        ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
         'data/lang/English/text.big', 'data/graphics/graphics.big', 'data/graphics/pc/textures.big')]
    before = {str(p): digest(p) for p in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-suit-links-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    names = ('Bright&Dark&Neutral Will Robes with Good&Bad&Neutral Wizard Hats',
             'Daedric Cuirass beta 1', 'Dark&Bright Platemail Suits with Evil&Good Demonic Helms',
             'Thunder Suit')
    checks = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for index, name in enumerate(names):
            member = f'mods/{name}.fmp'; payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            package = work/f'{index}.fmp'; package.write_bytes(payload)
            normalized = payload
            if catalogue[member]['format'] == 'wrapped-zlib':
                assert payload[:4] == b'90\0\0'
                decoder = zlib.decompressobj()
                normalized = decoder.decompress(payload[4:], 256*1024*1024+1)
                assert len(normalized) <= 256*1024*1024 and decoder.eof
                assert not decoder.unused_data and not decoder.unconsumed_tail
            parsed_package = work/f'{index}-parsed.fmp'; parsed_package.write_bytes(normalized)
            records = package_records(parsed_package, by_id=True)['GameBINEntries']
            metadata = package_records(parsed_package, by_id=True)['GameBINLinkMetaData']
            listing = json.loads(run(args.tool, 'fmp', 'list', package, '--json').stdout)
            game_rows = next(b['entries'] for b in listing['banks'] if b['bank'] == 'GameBINEntries')
            meta_rows = next(b['entries'] for b in listing['banks'] if b['bank'] == 'GameBINLinkMetaData')
            source_ids = {r['name']: r['id'] for r in game_rows if r['name']}
            report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
            direct, merged = work/f'direct-{index}', work/f'merged-{index}'
            run(args.tool, 'fmp', 'apply', args.install, package, direct)
            run(args.tool, 'mods', 'merge', args.install, merged, '--with', package)
            for bank in ('game.bin', 'names.bin'):
                assert digest(direct/'data/CompiledDefs'/bank) == digest(merged/'data/CompiledDefs'/bank)
            built = read_bank(direct)
            ids = {e['name']: i for i, e in enumerate(built) if e['name']}
            maps = []; refs = 0
            for row, meta in zip(report['records'], meta_rows, strict=True):
                assert row['record'] == meta['name']
                links = [g for g in row['groups'] if g['path'].startswith('1104892239.')]
                if not links:
                    continue
                # Anonymous metadata IDs are one-based positions in this
                # package's GameBINEntries table, not baseline record indices.
                owner_id = source_ids[row['record']] if row['record'] else game_rows[meta['id']-1]['id']
                source, source_at = records[owner_id]
                at = field_start(source, '41db514f')
                count = struct.unpack_from('<I', source, at)[0]
                assert len(links) == count and len(row['groups']) == count
                expected = bytearray(source)
                for link in links:
                    parts = link['path'].split('.')
                    assert parts[1] == 'unknown' and parts[3] == 'Suit ID' and len(link['targets']) == 1
                    slot = int(parts[2]); assert slot < count
                    struct.pack_into('<I', expected, at+8+slot*8, ids[link['targets'][0]])
                final_id = ids[row['record']] if row['record'] else owner_id
                assert built[final_id]['definition'] == 'CHeroSuitDef'
                assert built[final_id]['data'] == expected
                maps.append({'record': row['record'], 'source_id': owner_id, 'rows': count})
                refs += count
                if index == 1:
                    old_metadata, metadata_at = metadata[meta['id']]
                    path = b'1104892239.unknown.0.Suit ID'
                    target = links[0]['targets'][0].encode('ascii')
                    assert old_metadata.count(path) == old_metadata.count(target) == 1
                    mutations = []
                    bad = bytearray(normalized); struct.pack_into('<I', bad, source_at+at, count+1)
                    mutations.append(('count', bad))
                    bad = bytearray(normalized); offset = metadata_at+old_metadata.index(path)
                    bad[offset:offset+len(path)] = b'1104892239.unknown.9.Suit ID'
                    mutations.append(('index', bad))
                    bad = bytearray(normalized); offset = metadata_at+old_metadata.index(target)
                    bad[offset:offset+len(target)] = b'Z'*len(target)
                    mutations.append(('missing', bad))
                    for label, data in mutations:
                        mutant = work/f'bad-{label}.fmp'; mutant.write_bytes(data)
                        for command, output in [('apply', direct), ('merge', merged)]:
                            prior = snapshot(output)
                            if command == 'apply':
                                run(args.tool, 'fmp', 'apply', args.install, mutant, output, good=False)
                            else:
                                run(args.tool, 'mods', 'merge', args.install, output, '--with', mutant, good=False)
                            assert snapshot(output) == prior
            assert maps
            checks.append({'file': member, 'maps': maps, 'references': refs})
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'safe_refusals': 6,
        'inputs_unchanged': True, 'gameplay_tested': False}, indent=2))
    print('PASS: 4 original suit mods; exact map bytes, direct/composed parity and 6 safe refusals')


if __name__ == '__main__':
    main()
