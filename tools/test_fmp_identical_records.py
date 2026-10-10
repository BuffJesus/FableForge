#!/usr/bin/env python3
"""Original exact FMP repeats agree with an independent deduplicated control."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile
from test_fmp_weapon_links import run
from test_mod_file_choice_contributors import snapshot


def u32(raw, at):
    return struct.unpack_from('<I', raw, at)[0]


def read_rows(raw):
    cursor = u32(raw, 8); count = u32(raw, cursor); cursor += 4; banks = []
    for _ in range(count):
        end = next(n for n in range(cursor, len(raw)) if raw[n] in (0, 255))
        name = raw[cursor:end]; cursor = end+1
        bank_id, entries, start, _, block = struct.unpack_from('<5I', raw, cursor); cursor += 20
        at = start+4+u32(raw, start)*8; rows = []
        for _ in range(entries):
            header = list(struct.unpack_from('<7I', raw, at)); at += 28
            symbol = raw[at:at+header[6]]; at += header[6]
            crc, sources = struct.unpack_from('<II', raw, at); at += 8; paths = []
            for _ in range(sources):
                size = u32(raw, at); at += 4; paths.append(raw[at:at+size]); at += size
            size = u32(raw, at); at += 4; sub = raw[at:at+size]; at += size
            rows.append({'header': header, 'symbol': symbol, 'crc': crc, 'paths': paths,
                         'sub': sub, 'data': raw[header[4]:header[4]+header[3]]})
        banks.append({'name': name, 'id': bank_id, 'block': block, 'rows': rows})
    return banks


def write_rows(path, original, banks):
    raw = bytearray(original[:16]); directory = []
    for bank in banks:
        locations = []
        for row in bank['rows']:
            locations.append(len(raw)); raw += row['data']
        start = len(raw); raw += struct.pack('<I', 0)
        for row, location in zip(bank['rows'], locations):
            header = row['header'].copy(); header[3] = len(row['data']); header[4] = location
            header[6] = len(row['symbol'])
            raw += struct.pack('<7I', *header)+row['symbol']+struct.pack('<II', row['crc'], len(row['paths']))
            for source in row['paths']: raw += struct.pack('<I', len(source))+source
            raw += struct.pack('<I', len(row['sub']))+row['sub']
        directory.append((bank, start, len(raw)-start))
    directory_at = len(raw); raw += struct.pack('<I', len(directory))
    for bank, start, length in directory:
        raw += bank['name']+b'\0'+struct.pack('<5I', bank['id'], len(bank['rows']), start, length, bank['block'])
    struct.pack_into('<I', raw, 8, directory_at); path.write_bytes(raw)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args(); args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fmp-identical-records-', dir=args.work_root)).resolve()
    assert work.parent == args.work_root.resolve()
    print('Evidence retained at', work, flush=True)
    relatives = ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
        'data/lang/English/text.big', 'data/graphics/pc/textures.big',
        'data/graphics/graphics.big', 'data/Misc/pc/effects.big')
    inputs = [args.archive, args.tool, *(args.install/p for p in relatives)]
    before = {str(p): digest(p) for p in inputs}
    baseline = work/'baseline'
    for relative in relatives:
        target = baseline/relative; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.install/relative, target)
    member = 'mods/Assasain retexture set and flame flourish for soa.fmp'
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        raw = archive.read(member); assert hashlib.sha256(raw).hexdigest() == catalogue[member]['sha256']
        other = {label: archive.read(name) for label, name in [
            ('conflicting-named-identities', 'mods/Augmentations - Regeneration and Damage Tweaks.fmp'),
            ('existing-texture-collision', 'mods/Augmentation Test.fmp')]}
        for label, name in [('conflicting-named-identities', 'mods/Augmentations - Regeneration and Damage Tweaks.fmp'),
                            ('existing-texture-collision', 'mods/Augmentation Test.fmp')]:
            assert hashlib.sha256(other[label]).hexdigest() == catalogue[name]['sha256']
    original = work/'original.fmp'; original.write_bytes(raw)
    banks = read_rows(raw); by_name = {b['name']: b for b in banks}
    game = by_name[b'GameBINEntries']['rows']; metadata = by_name[b'GameBINLinkMetaData']['rows']
    assert len(game) == len(metadata) == 2
    assert all(not r['symbol'] and r['header'][1] == 14336 and r['sub'] == b'CWeaponDef\0' for r in game)
    assert game[0]['data'] == game[1]['data'] and game[0]['header'][2] == game[1]['header'][2]
    assert [r['header'][1] for r in metadata] == [1, 2] and metadata[0]['data'] == metadata[1]['data']
    control_banks = copy.deepcopy(banks)
    for bank in control_banks:
        if bank['name'] in (b'GameBINEntries', b'GameBINLinkMetaData'): bank['rows'] = bank['rows'][:1]
    control = work/'independent-dedup-control.fmp'; write_rows(control, raw, control_banks)
    one_meta_banks = copy.deepcopy(banks)
    next(b for b in one_meta_banks if b['name'] == b'GameBINLinkMetaData')['rows'] = copy.deepcopy(metadata[:1])
    one_meta = work/'repeated-record-one-metadata.fmp'; write_rows(one_meta, raw, one_meta_banks)
    checks = []; outputs = {}
    for route in ('apply', 'merge'):
        def command(package, output):
            return ('fmp', 'apply', baseline, package, output) if route == 'apply' else (
                'mods', 'merge', baseline, output, '--with', package)
        control_output = work/('control-'+route); run(args.tool, *command(control, control_output))
        expected = snapshot(control_output)
        for label, package in [('original', original), ('one-metadata', one_meta)]:
            output = work/(label+'-'+route); run(args.tool, *command(package, output))
            assert snapshot(output) == expected
            checks.append({'route': route, 'fixture': label, 'identical_to_control': True})
            if label == 'original': outputs[route] = output
    parsed = json.loads(run(args.tool, 'fmp', 'links', control, '--groups', '--json').stdout)
    assert len(parsed['records']) == 1
    pointers = sum(len(g['targets']) for g in parsed['records'][0]['groups'])
    print('Original and one-metadata variant match independent control through both routes:', pointers, 'links', flush=True)
    mutations = []
    changed = copy.deepcopy(one_meta_banks)
    next(b for b in changed if b['name'] == b'GameBINEntries')['rows'][1]['symbol'] = b'@unnamed:14336'
    fixture = work/'reserved-anonymous-name.fmp'; write_rows(fixture, raw, changed)
    mutations.append(('reserved-anonymous-name', fixture))
    for label, change in [('source-payload', 'payload'), ('source-type', 'type'),
                          ('source-definition', 'definition'), ('wrong-anonymous-id', 'id'),
                          ('payload-one-metadata', 'payload-single'), ('type-one-metadata', 'type-single')]:
        changed = copy.deepcopy(banks); lookup = {b['name']: b for b in changed}
        row = lookup[b'GameBINEntries']['rows'][1]
        if change.startswith('payload'): row['data'] = bytes([row['data'][0]^1])+row['data'][1:]
        if change.startswith('type'): row['header'][2] += 1
        if change == 'definition': row['sub'] = b'X'+row['sub'][1:]
        if change == 'id': row['header'][1] = 0
        if change.endswith('-single'): lookup[b'GameBINLinkMetaData']['rows'] = lookup[b'GameBINLinkMetaData']['rows'][:1]
        fixture = work/(label+'.fmp'); write_rows(fixture, raw, changed); mutations.append((label, fixture))
    target = parsed['records'][0]['groups'][0]['targets'][0].encode('ascii')
    for label, rows in [('different-metadata-target', (1,)), ('missing-target-both-metadata', (0, 1))]:
        changed = copy.deepcopy(banks); meta = next(b for b in changed if b['name'] == b'GameBINLinkMetaData')['rows']
        for index in rows:
            assert target in meta[index]['data']
            meta[index]['data'] = meta[index]['data'].replace(target, b'X'*len(target))
        fixture = work/(label+'.fmp'); write_rows(fixture, raw, changed); mutations.append((label, fixture))
    changed = copy.deepcopy(banks)
    next(b for b in changed if b['name'] == b'GameBINLinkMetaData')['rows'][1]['header'][1] = 0
    fixture = work/'invalid-metadata-order.fmp'; write_rows(fixture, raw, changed); mutations.append(('invalid-metadata-order', fixture))
    for label, payload in other.items():
        fixture = work/(label+'.fmp'); fixture.write_bytes(payload); mutations.append((label, fixture))
    saved = {route: snapshot(output) for route, output in outputs.items()}; refusals = []
    for label, fixture in mutations:
        for route, output in outputs.items():
            command = ('fmp', 'apply', baseline, fixture, output) if route == 'apply' else (
                'mods', 'merge', baseline, output, '--with', fixture)
            failure = run(args.tool, *command, good=False)
            assert 'FMP' in failure.stderr and snapshot(output) == saved[route]
            refusals.append({'case': label, 'route': route, 'reason': failure.stderr.strip()})
        print(label, 'both routes refuse and preserve prior output: PASS', flush=True)
    assert digest(original) == hashlib.sha256(raw).hexdigest()
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'file': member, 'checks': checks, 'links': pointers,
        'refusals': refusals, 'inputs_sha256': before, 'inputs_unchanged': True,
        'independent_control': str(control), 'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS: original exact repeats, independent bank parity and', len(refusals), 'safe refusals')


if __name__ == '__main__':
    main()
