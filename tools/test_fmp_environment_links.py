#!/usr/bin/env python3
"""Verify original environment texture groups, relocated IDs and safe refusals."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile

from test_fmp_links import package_records, read_bank, crc0
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run
from test_fmp_identical_records import read_rows, write_rows
from test_mod_file_choice_contributors import snapshot


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def time_rows(data, fields):
    # Original package has the ordinary five-byte definition prefix.
    assert data[5:9] == struct.pack('<I', crc0('Time'))
    count = struct.unpack_from('<I', data, 9)[0]
    assert 0 < count <= 4096
    cursor = 13
    rows = []
    for _ in range(count):
        values, offsets = {}, {}
        for field in fields:
            name, kind = field['name'], field['type']
            assert kind in ('float', 'int32', 'bool')
            assert data[cursor:cursor+4] == struct.pack('<I', crc0(name))
            cursor += 4
            width = 1 if kind == 'bool' else 4
            assert cursor + width <= len(data)
            values[name] = int.from_bytes(data[cursor:cursor+width], 'little')
            offsets[name] = cursor
            cursor += width
        rows.append((values, offsets))
    assert data[cursor:cursor+4] == struct.pack('<I', crc0('SunTilt'))
    return rows


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fmp-environment-links-', dir=args.work_root)).resolve()
    assert work.parent == args.work_root.resolve()
    print('Evidence retained at', work, flush=True)
    relatives = ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/lang/English/text.big', 'data/graphics/pc/textures.big',
                 'data/graphics/graphics.big', 'data/Misc/pc/effects.big')
    inputs = [args.archive, args.tool, *(args.install/p for p in relatives)]
    before = {str(p): digest(p) for p in inputs}
    base = work/'baseline'
    for relative in relatives:
        target = base/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.install/relative, target)
    member = 'mods/Enviroment Snowing.fmp'
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        raw = archive.read(member)
        assert hashlib.sha256(raw).hexdigest() == catalogue[member]['sha256']
    package = work/'original.fmp'
    package.write_bytes(raw)
    sources = package_records(package)['GameBINEntries']
    metadata = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
    groups = {r['record']: r['groups'] for r in metadata['records']}
    assert len(sources) == len(groups) == 9
    fields = json.loads((repo/'docs/re_reference/def_schema.json').read_text())['CEnvironmentThemeDef']['fields']
    bank_path = base/'data/graphics/pc/textures.big'
    textures = entries(bank_path, 'GBANK_MAIN_PC', include_payload=False)
    slots = []
    for owner, (data, _) in sources.items():
        rows = time_rows(data, fields)
        for group in groups[owner]:
            parts = group['path'].split('.')
            assert len(parts) == 4 and parts[:2] == ['1743315694', 'unknown']
            assert parts[3] in ('Sky', 'Clouds')
            names = ['SkyTexture0', 'SkyTexture1'] if parts[3] == 'Sky' else [
                'CloudLowerLayerTexture0', 'CloudLowerLayerTexture1',
                'CloudUpperLayerTexture0', 'CloudUpperLayerTexture1']
            values, offsets = rows[int(parts[2])]
            populated = [name for name in names if values[name] != 0]
            assert len(populated) == len(group['targets']), (owner, group, populated)
            for name, target in zip(populated, group['targets']):
                assert values[name] == textures[target]['id'], (owner, name, target)
                slots.append((owner, offsets[name], target))
    reports = []
    for mode in ('original-baseline', 'relocated-texture-ids'):
        if mode == 'relocated-texture-ids':
            # Explicitly derived scratch baseline; original package stays intact.
            used = {r['id'] for r in textures.values()}
            with bank_path.open('r+b') as stream:
                for target in sorted({target for _, _, target in slots}):
                    row = textures[target]
                    new_id = row['id'] + 500000
                    assert new_id not in used
                    used.add(new_id)
                    stream.seek(row['record_offset'] + 4)
                    stream.write(struct.pack('<I', new_id))
            textures = entries(bank_path, 'GBANK_MAIN_PC', include_payload=False)
        expected = {owner: bytearray(data) for owner, (data, _) in sources.items()}
        for owner, at, target in slots:
            struct.pack_into('<I', expected[owner], at, textures[target]['id'])
        if mode == 'relocated-texture-ids':
            assert any(expected[owner] != data for owner, (data, _) in sources.items())
        outputs = []
        for route in ('apply', 'merge'):
            output = work/(mode+'-'+route)
            command = ('fmp', 'apply', base, package, output) if route == 'apply' else (
                'mods', 'merge', base, output, '--with', package)
            run(args.tool, *command)
            actual = {r['name']: r['data'] for r in read_bank(output)}
            for owner, wanted in expected.items():
                assert actual[owner] == wanted, (mode, route, owner)
            outputs.append(output)
        for relative in relatives:
            effective = [output/relative if (output/relative).is_file() else base/relative for output in outputs]
            assert digest(effective[0]) == digest(effective[1]), (mode, relative)
        reports.append({'mode': mode, 'records': len(sources), 'texture_words': len(slots),
                        'exact_other_bytes': True, 'effective_bank_parity': True,
                        'texture_bank_sha256': digest(bank_path)})
        print(mode, len(slots), 'texture words /', len(sources), 'exact records: PASS', flush=True)
    banks = read_rows(raw)
    mutations = []
    for label in ('oversized-count', 'short-count', 'later-row-tag', 'zero-linked-slot', 'nonzero-unlinked-slot'):
        changed = copy.deepcopy(banks)
        row = next(r for b in changed if b['name'] == b'GameBINEntries' for r in b['rows']
                   if r['symbol'] == b'ENVIRONMENT_THEME1')
        data = bytearray(row['data'])
        decoded = time_rows(data, fields)
        if label == 'oversized-count': struct.pack_into('<I', data, 9, 0xffffffff)
        if label == 'short-count': struct.pack_into('<I', data, 9, 7)
        if label == 'later-row-tag': data[13+7*269] ^= 1
        if label == 'zero-linked-slot': struct.pack_into('<I', data, decoded[0][1]['SkyTexture0'], 0)
        if label == 'nonzero-unlinked-slot':
            assert decoded[0][0]['SkyTexture1'] == 0
            struct.pack_into('<I', data, decoded[0][1]['SkyTexture1'], 0xfffffffe)
        row['data'] = bytes(data)
        fixture = work/(label+'.fmp')
        write_rows(fixture, raw, changed)
        mutations.append((label, fixture))
    for label, old, new in (
        ('wrong-row', b'1743315694.unknown.0.Sky', b'1743315694.unknown.9.Sky'),
        ('wrong-member', b'1743315694.unknown.0.Sky', b'1743315694.unknown.0.sky'),
        ('missing-texture', b'GRAPHIC_ATMOSPHERIC_SKY_MIDNIGHT_SNOWSPIRE', b'ABSENT_ENVIRONMENT_TEXTURE')):
        changed = copy.deepcopy(banks)
        meta = next(r for b in changed if b['name'] == b'GameBINLinkMetaData' for r in b['rows']
                    if r['symbol'] == b'ENVIRONMENT_THEME1')
        # Keep NRBF string lengths intact for a semantic dependency/path refusal.
        if label == 'missing-texture': new = b'X'*len(old)
        assert len(old) == len(new) and old in meta['data']
        meta['data'] = meta['data'].replace(old, new)
        fixture = work/(label+'.fmp')
        write_rows(fixture, raw, changed)
        mutations.append((label, fixture))
    prior = [snapshot(output) for output in outputs]
    refusals = []
    for label, fixture in mutations:
        for index, route in enumerate(('apply', 'merge')):
            output = outputs[index]
            command = ('fmp', 'apply', base, fixture, output) if route == 'apply' else (
                'mods', 'merge', base, output, '--with', fixture)
            failure = run(args.tool, *command, good=False)
            assert 'FMP links:' in failure.stderr and snapshot(output) == prior[index]
            refusals.append({'case': label, 'route': route, 'reason': failure.stderr.strip()})
        print(label, 'both routes preserve prior output: PASS', flush=True)
    assert digest(package) == catalogue[member]['sha256']
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'file': member, 'original_sha256': digest(package),
        'runs': reports, 'refusals': refusals, 'inputs_sha256': before,
        'inputs_unchanged': True, 'gameplay_tested': False}, indent=2))
    print('PASS:', len(slots), 'environment links;', len(refusals), 'safe negative routes', flush=True)


if __name__ == '__main__':
    main()
