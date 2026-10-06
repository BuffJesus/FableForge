#!/usr/bin/env python3
"""Audit original Script Balance and labelled feat derivatives against a prior tool."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile

from test_fmp_creature_optional_links import digest, metadata
from test_fmp_identical_records import read_rows, write_rows
from test_fmp_links import crc0, read_bank
from test_fmp_weapon_links import run
from test_mod_file_choice_contributors import snapshot

FEAT = '3147235600.unknown.'
MEMBERS = ('FeatName', 'Verb', 'TimeLimit', 'TargetNumber', 'GoldReward', 'XPReward',
           'ItemReward', 'NoBlocking', 'KN_AttackType', 'KN_Perfect', 'KN_CreatureType',
           'GF_FromRegion', 'GF_ToRegion', 'GF_NoTeleporting', 'CO_ItemName')


def rows(data):
    tag = struct.pack('<I', crc0('Feats'))
    assert data.count(tag) == 1
    count_at = data.index(tag)+4
    count = struct.unpack_from('<I', data, count_at)[0]
    assert count <= 4096
    at, result = count_at+4, []
    for _ in range(count):
        fields = {}
        for member in MEMBERS:
            assert struct.unpack_from('<I', data, at)[0] == crc0(member)
            at += 4
            size = 1 if member in ('NoBlocking', 'KN_Perfect', 'GF_NoTeleporting') else 4
            fields[member] = (int.from_bytes(data[at:at+size], 'little'), at)
            at += size
        result.append(fields)
    assert at == count_at+4+111*count
    return count_at, result


def names(root):
    raw = (root/'data/CompiledDefs/names.bin').read_bytes()
    at, result = 20, {}
    for _ in range(struct.unpack_from('<I', raw, 8)[0]):
        end = raw.index(0, at+4)
        result[raw[at+4:end].decode('ascii')] = at+4-20
        at = end+1
    return result


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--control-tool', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fmp-script-feats-', dir=args.work_root)).resolve()
    assert work.parent == args.work_root.resolve()
    print('Evidence retained at', work, flush=True)
    relatives = ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/lang/English/text.big', 'data/graphics/pc/textures.big',
                 'data/graphics/graphics.big', 'data/Misc/pc/effects.big')
    inputs = [args.archive, args.tool, args.control_tool, *(args.install/p for p in relatives)]
    before = {str(p): digest(p) for p in inputs}
    base = work/'baseline'
    for relative in relatives:
        target = base/relative; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.install/relative, target)
    with zipfile.ZipFile(args.archive) as archive:
        raw = archive.read('mods/Script Balance.fmp')
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        assert hashlib.sha256(raw).hexdigest() == catalogue['mods/Script Balance.fmp']['sha256']
    original = work/'original.fmp'; original.write_bytes(raw)
    groups = json.loads(run(args.tool, 'fmp', 'links', original, '--groups', '--json').stdout)['records']
    assert len(groups) == 1 and groups[0]['record'] == 'SCRIPT_DEF'
    groups = groups[0]['groups']
    banks = read_rows(raw)

    def source(bs):
        return next(r for b in bs if b['name'] == b'GameBINEntries' for r in b['rows'] if r['symbol'] == b'SCRIPT_DEF')

    def with_metadata(bs, gs):
        result = copy.deepcopy(bs)
        meta = next(r for b in result if b['name'] == b'GameBINLinkMetaData' for r in b['rows'] if r['symbol'] == b'SCRIPT_DEF')
        meta['data'] = metadata(gs)
        return result

    def command(route, package, output):
        return ('fmp', 'apply', base, package, output) if route == 'apply' else (
            'mods', 'merge', base, output, '--with', package)

    count_at, original_rows = rows(source(banks)['data'])
    checks, refusals = [], []
    for mode in ('original', 'stale-offsets', 'sparse-singleton'):
        bs, gs = copy.deepcopy(banks), copy.deepcopy(groups)
        data = bytearray(source(bs)['data'])
        if mode == 'stale-offsets':
            for row in original_rows:
                for member in ('FeatName', 'Verb', 'ItemReward'):
                    value, at = row[member]
                    if value != 0xffffffff: struct.pack_into('<I', data, at, 0x12345678)
        if mode == 'sparse-singleton':
            for member in ('FeatName', 'Verb'):
                struct.pack_into('<I', data, original_rows[0][member][1], 0xffffffff)
            group = next(g for g in gs if g['path'] == FEAT+'0.unknown')
            assert len(group['targets']) == 3
            group['targets'] = group['targets'][2:]
        source(bs)['data'] = bytes(data)
        package = original if mode == 'original' else work/(mode+'.fmp')
        if mode != 'original': write_rows(package, raw, with_metadata(bs, gs))
        control = work/(mode+'-control.fmp')
        write_rows(control, raw, with_metadata(bs, [g for g in gs if not g['path'].startswith(FEAT)]))
        for route in ('apply', 'merge'):
            output, prior = work/(mode+'-'+route), work/(mode+'-'+route+'-control')
            run(args.control_tool, *command(route, control, prior))
            run(args.tool, *command(route, package, output))
            expected = read_bank(prior); actual = read_bank(output)
            record = next(r for r in expected if r['name'] == 'SCRIPT_DEF')
            result = bytearray(record['data']); lookup = names(prior); linked = 0
            _, layout = rows(source(bs)['data'])
            for group in gs:
                if not group['path'].startswith(FEAT): continue
                parts = group['path'].split('.')
                assert len(parts) == 4 and parts[3] == 'unknown'
                row = layout[int(parts[2])]
                slots = [row[m][1] for m in ('FeatName', 'Verb', 'ItemReward') if row[m][0] != 0xffffffff]
                assert len(slots) == len(group['targets'])
                for at, target in zip(slots, group['targets']):
                    struct.pack_into('<I', result, at, lookup[target]); linked += 1
            record['data'] = bytes(result)
            assert actual == expected, (mode, route)
            for relative in relatives[1:]:
                a = output/relative if (output/relative).exists() else base/relative
                b = prior/relative if (prior/relative).exists() else base/relative
                assert digest(a) == digest(b)
            checks.append({'mode': mode, 'route': route, 'linked_words': linked,
                           'source_sha256': digest(package), 'prior_control_sha256': digest(control),
                           'only_named_feat_words_differ': True})
        assert (work/(mode+'-apply')/'data/CompiledDefs/game.bin').read_bytes() == (
            work/(mode+'-merge')/'data/CompiledDefs/game.bin').read_bytes()
        print(mode, 'both routes preserve every other byte: PASS', flush=True)
    for case in ('missing-name', 'omitted-populated-slot', 'extra-slot', 'row-out-of-range', 'bad-later-tag', 'bad-count'):
        bs, gs = copy.deepcopy(banks), copy.deepcopy(groups)
        group = next(g for g in gs if g['path'] == FEAT+'0.unknown')
        if case == 'missing-name': group['targets'][0] = 'FORGE_MISSING_FEAT_NAME'
        if case == 'omitted-populated-slot': group['targets'].pop(0)
        if case == 'extra-slot': group['targets'].append(group['targets'][0])
        if case == 'row-out-of-range': group['path'] = FEAT+str(len(original_rows))+'.unknown'
        data = bytearray(source(bs)['data'])
        if case == 'bad-later-tag': data[original_rows[-1]['CO_ItemName'][1]-4] ^= 1
        if case == 'bad-count': struct.pack_into('<I', data, count_at, len(original_rows)+1)
        source(bs)['data'] = bytes(data)
        package = work/(case+'.fmp'); write_rows(package, raw, with_metadata(bs, gs))
        for route in ('apply', 'merge'):
            output = work/('original-'+route); saved = snapshot(output)
            failure = run(args.tool, *command(route, package, output), good=False)
            assert 'FMP links:' in failure.stderr and snapshot(output) == saved
            refusals.append({'case': case, 'route': route, 'reason': failure.stderr.strip()})
    assert original.read_bytes() == raw and before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'refusals': refusals,
        'rows': len(original_rows), 'original_sha256': digest(original), 'inputs_sha256': before,
        'inputs_unchanged': True, 'gameplay_tested': False}, indent=2))
    print('PASS:', len(checks), 'original/derived imports,', len(refusals), 'safe negative routes', flush=True)


if __name__ == '__main__':
    main()
