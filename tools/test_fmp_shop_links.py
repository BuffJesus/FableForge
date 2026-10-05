#!/usr/bin/env python3
"""Independently verify every relocated word in original Hero Titles shop data."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zipfile

from test_fmp_links import package_records, read_bank
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run, field_start


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    text = args.install/'data/lang/English/text.big'
    inputs = [args.archive, text, args.install/'data/CompiledDefs/game.bin',
              args.install/'data/CompiledDefs/names.bin']
    before = {str(p): digest(p) for p in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-shop-links-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    with zipfile.ZipFile(args.archive) as archive:
        member = 'mods/Hero Titles.fmp'
        payload = archive.read(member)
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
    package = work/'Hero Titles.fmp'; package.write_bytes(payload)
    originals = package_records(package, by_id=True)['GameBINEntries']
    assert list(originals) == [9446]
    source = originals[9446][0]
    report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
    groups = report['records'][0]['groups']
    text_groups = [g for g in groups if g['path'].split('.')[1] in
                   ('HAWKING', 'REPLYTOREQUEST', 'FOLLOWME', 'THANKING')]
    texts = entries(text, 'TEXT_ENGLISH_MAIN', {g['targets'][0] for g in text_groups}, False)
    direct, merged = work/'direct', work/'merged'
    run(args.tool, 'fmp', 'apply', args.install, package, direct)
    run(args.tool, 'mods', 'merge', args.install, merged, '--with', package)
    for bank in ('game.bin', 'names.bin'):
        assert digest(direct/'data/CompiledDefs'/bank) == digest(merged/'data/CompiledDefs'/bank)
    built = read_bank(direct)
    game_ids = {e['name']: i for i, e in enumerate(built) if e['name']}
    expected = bytearray(source)
    stock_count = struct.unpack_from('<I', source, field_start(source, 'ee2fc7f1'))[0]
    stock_links = 0
    for group in groups:
        assert len(group['targets']) == 1
        path, target = group['path'], group['targets'][0]
        parts = path.split('.')
        at = field_start(source, int(parts[0]).to_bytes(4, 'big').hex())
        if group in text_groups:
            value = texts[target]['id']
        elif parts[1] == 'CShopDefArrayList1':
            assert parts[3] == 'ItemForSell'
            at += 8 + int(parts[2])*64
            value = game_ids[target]; stock_links += 1
        else:
            assert path == '425186066.ShopKeeperCreatureID'
            value = game_ids[target]
        struct.pack_into('<I', expected, at, value)
    assert stock_links == stock_count == 30
    assert built[9446]['data'] == expected, 'preserve prices, quantities, restock settings and all opaque bytes'
    stock_at = field_start(source, 'ee2fc7f1')
    refused = 0
    for label, at, value in [('bad-count', stock_at, stock_count + 1),
                             ('bad-row-tag', stock_at + 12, 0)]:
        malformed = bytearray(payload)
        struct.pack_into('<I', malformed, originals[9446][1] + at, value)
        bad = work/f'{label}.fmp'; bad.write_bytes(malformed)
        for command, output in [('apply', direct), ('merge', merged)]:
            prior = {str(p.relative_to(output)): digest(p) for p in output.rglob('*') if p.is_file()}
            if command == 'apply':
                result = run(args.tool, 'fmp', 'apply', args.install, bad, output, good=False)
            else:
                result = run(args.tool, 'mods', 'merge', args.install, output, '--with', bad, good=False)
            assert 'shop stock' in result.stderr or 'unclean' in result.stderr or 'no schema' in result.stderr
            assert prior == {str(p.relative_to(output)): digest(p) for p in output.rglob('*') if p.is_file()}
            refused += 1
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'file': member, 'references': len(groups),
        'stock_rows': stock_links, 'record_byte_exact': True, 'safe_refusals': refused, 'inputs_unchanged': True,
        'gameplay_tested': False}, indent=2))
    print('PASS: original Hero Titles; 35 reference words and complete shop payload')


if __name__ == '__main__':
    main()
