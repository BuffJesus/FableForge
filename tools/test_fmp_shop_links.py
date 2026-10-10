#!/usr/bin/env python3
"""Verify original Hero Titles and scripted shop records, including stock guards."""
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
    scripted_checks = []
    with zipfile.ZipFile(args.archive) as archive:
        for variant in (1, 2):
            scripted_member = f'mods/Scripted Shopkeeper (Variant {variant}).fmp'
            scripted_payload = archive.read(scripted_member)
            assert hashlib.sha256(scripted_payload).hexdigest() == catalogue[scripted_member]['sha256']
            scripted_package = work/f'scripted-{variant}.fmp'; scripted_package.write_bytes(scripted_payload)
            scripted_source, source_at = package_records(scripted_package)['GameBINEntries']['SCRIPTED_SHOP']
            scripted_report = json.loads(run(args.tool, 'fmp', 'links', scripted_package, '--groups', '--json').stdout)
            scripted_groups = next(r['groups'] for r in scripted_report['records'] if r['record'] == 'SCRIPTED_SHOP')
            direct, merged = work/f'scripted-direct-{variant}', work/f'scripted-merged-{variant}'
            run(args.tool, 'fmp', 'apply', args.install, scripted_package, direct)
            run(args.tool, 'mods', 'merge', args.install, merged, '--with', scripted_package)
            for bank in ('game.bin', 'names.bin'):
                assert digest(direct/'data/CompiledDefs'/bank) == digest(merged/'data/CompiledDefs'/bank)
            built = read_bank(direct)
            ids = {e['name']: i for i, e in enumerate(built) if e['name']}
            expected = bytearray(scripted_source)
            category_count = 0
            for group in scripted_groups:
                assert len(group['targets']) == 1
                parts = group['path'].split('.'); target = group['targets'][0]
                at = field_start(scripted_source, int(parts[0]).to_bytes(4, 'big').hex())
                if parts[1] in ('HAWKING', 'REPLYTOREQUEST', 'FOLLOWME', 'THANKING'):
                    value = entries(text, 'TEXT_ENGLISH_MAIN', {target}, False)[target]['id']
                elif parts[0] == '2023417370':
                    assert parts[1] == 'unknown' and parts[3] == 'Unknown'
                    at += 8 + int(parts[2])*64; value = ids[target]; category_count += 1
                else:
                    assert group['path'] == '425186066.ShopKeeperCreatureID'
                    value = ids[target]
                struct.pack_into('<I', expected, at, value)
            assert category_count == 13
            assert built[ids['SCRIPTED_SHOP']]['data'] == expected
            category_at = field_start(scripted_source, '789ae61a')
            for label, relative, value in [('count', category_at, 14), ('tag', category_at+12, 0)]:
                malformed = bytearray(scripted_payload)
                struct.pack_into('<I', malformed, source_at + relative, value)
                bad = work/f'scripted-{variant}-bad-{label}.fmp'; bad.write_bytes(malformed)
                for command, output in [('apply', direct), ('merge', merged)]:
                    prior = {str(p.relative_to(output)): digest(p) for p in output.rglob('*') if p.is_file()}
                    if command == 'apply':
                        run(args.tool, 'fmp', 'apply', args.install, bad, output, good=False)
                    else:
                        run(args.tool, 'mods', 'merge', args.install, output, '--with', bad, good=False)
                    assert prior == {str(p.relative_to(output)): digest(p) for p in output.rglob('*') if p.is_file()}
                    refused += 1
            scripted_checks.append({'file': scripted_member, 'references': len(scripted_groups),
                                    'category_rows': category_count, 'record_byte_exact': True})
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'file': member, 'references': len(groups),
        'stock_rows': stock_links, 'record_byte_exact': True, 'safe_refusals': refused, 'inputs_unchanged': True,
        'scripted_checks': scripted_checks, 'gameplay_tested': False}, indent=2))
    print('PASS: 3 original shop mods; exact shop payloads and 12 safe stock refusals')


if __name__ == '__main__':
    main()
