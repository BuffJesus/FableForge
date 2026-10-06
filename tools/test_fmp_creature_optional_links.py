#!/usr/bin/env python3
"""Original creature links versus prior-tool controls, including moved identities."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile

from test_fmp_links import read_bank, rewrite_bank
from test_fmp_identical_records import read_rows, write_rows
from test_fmp_weapon_links import run
from test_mod_file_choice_contributors import snapshot

SHEATH = '2674723971.Weapon4ID'
LEADER = '2089043171.GameBINEntryID'
NULL = 'NULLDEF_CHeroMorphDef'
SWORD = 'OBJECT_SUMMONER_SWORD_01'
LIEUTENANT = 'CREATURE_HOBBE_LIEUTENANT'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def metadata(groups):
    """Independent inert NRBF NameValueCollection writer for labelled controls."""
    word = lambda value: struct.pack('<I', value & 0xffffffff)
    def text(value):
        data = value.encode('utf-8'); n = len(data); prefix = bytearray()
        while n >= 128:
            prefix.append((n & 127) | 128); n >>= 7
        prefix.append(n)
        return prefix + data
    ref = lambda number: b'\x09' + word(number)
    string = lambda number, value: b'\x06' + word(number) + text(value)
    count = len(groups)
    raw = bytearray(b'\0'+word(1)+word(-1)+word(1)+word(0))
    raw += b'\x04'+word(1)+text('System.Collections.Specialized.NameValueCollection')
    raw += word(3)+text('Count')+text('Keys')+text('Values')+bytes([0, 6, 5, 8])
    raw += word(count)+ref(2)+ref(3)
    raw += b'\x11'+word(2)+word(count)
    for index, group in enumerate(groups): raw += string(4+2*count+index, group['path'])
    raw += b'\x10'+word(3)+word(count)
    for index in range(count): raw += ref(4+index)
    next_string = 4+3*count
    for index, group in enumerate(groups):
        values = group['targets']
        raw += b'\x04'+word(4+index)+text('System.Collections.ArrayList')+word(3)
        raw += text('_items')+text('_size')+text('_version')+bytes([5, 0, 0, 8, 8])
        raw += ref(4+count+index)+word(len(values))+word(0)
        raw += b'\x10'+word(4+count+index)+word(len(values))
        for value in values:
            raw += string(next_string, value); next_string += 1
    return bytes(raw+b'\x0b')


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
    work = Path(tempfile.mkdtemp(prefix='fmp-creature-optional-', dir=args.work_root)).resolve()
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
    original_records = read_bank(base)
    baseline_ids = {r['name']: index for index, r in enumerate(original_records) if r['name']}
    assert baseline_ids[NULL] == 0
    reports, originals = [], []

    def groups_for(package):
        report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
        return {r['record']: r['groups'] for r in report['records']}

    def remove_new_links(raw, groups):
        banks = read_rows(raw)
        for bank in banks:
            if bank['name'] != b'GameBINLinkMetaData': continue
            for row in bank['rows']:
                row['data'] = metadata([g for g in groups[row['symbol'].decode()]
                                        if g['path'] not in (SHEATH, LEADER)])
        return banks

    def command(route, baseline, package, output):
        return ('fmp', 'apply', baseline, package, output) if route == 'apply' else (
            'mods', 'merge', baseline, output, '--with', package)

    def compare_banks(left, right, baseline):
        for relative in relatives:
            a = left/relative if (left/relative).is_file() else baseline/relative
            b = right/relative if (right/relative).is_file() else baseline/relative
            assert digest(a) == digest(b), relative

    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for index, name in enumerate(('Raise Dead New', 'Spearanimsbeta3', 'Thunder Transform Mod', 'Winter Sky')):
            member = 'mods/'+name+'.fmp'; raw = archive.read(member)
            assert hashlib.sha256(raw).hexdigest() == catalogue[member]['sha256']
            case = work/str(index); case.mkdir()
            original = case/'original.fmp'; original.write_bytes(raw)
            groups = groups_for(original)
            selected = {owner: [g for g in gs if g['path'] in (SHEATH, LEADER)] for owner, gs in groups.items()}
            selected = {owner: gs for owner, gs in selected.items() if gs}
            assert sum(len(gs) for gs in selected.values()) == 2
            assert all(g['targets'] == [NULL] for gs in selected.values() for g in gs)
            control = case/'two-metadata-groups-omitted.fmp'
            write_rows(control, raw, remove_new_links(raw, groups))
            expected_groups = {owner: [g for g in gs if g['path'] not in (SHEATH, LEADER)] for owner, gs in groups.items()}
            assert groups_for(control) == expected_groups
            for route in ('apply', 'merge'):
                output, control_output = case/route, case/(route+'-prior-control')
                run(args.control_tool, *command(route, base, control, control_output))
                run(args.tool, *command(route, base, original, output))
                compare_banks(output, control_output, base)
                records = {r['name']: r for r in read_bank(output) if r['name']}
                for owner in selected:
                    data = records[owner]['data']
                    for tag in (bytes.fromhex('9f6d0c83'), bytes.fromhex('7c8444e3')):
                        assert data.count(tag) == 1 and struct.unpack_from('<I', data, data.index(tag)+4)[0] == 0
            compare_banks(case/'apply', case/'merge', base)
            reports.append({'file': member, 'original_sha256': digest(original),
                            'control_sha256': digest(control), 'new_links': 2,
                            'prior_tool_control_parity': True, 'direct_composed_parity': True})
            originals.append((original, raw, groups))
            print(name, 'both routes match prior-tool control: PASS', flush=True)

    # Derived baseline moves the null label and both positive target identities.
    # Reorder matching payloads and physical identity table rows together.
    shifted = work/'shifted-baseline'
    reordered = copy.deepcopy(original_records)
    swaps = [(0, 1), (baseline_ids[SWORD], baseline_ids[SWORD]+1),
             (baseline_ids[LIEUTENANT], baseline_ids[LIEUTENANT]+1)]
    assert len({n for pair in swaps for n in pair}) == 6
    for a, b in swaps: reordered[a], reordered[b] = reordered[b], reordered[a]
    rewrite_bank(base, shifted, reordered)
    game_path = shifted/'data/CompiledDefs/game.bin'; data = bytearray(game_path.read_bytes())
    for a, b in swaps:
        first, second = bytes(data[13+a*12:25+a*12]), bytes(data[13+b*12:25+b*12])
        data[13+a*12:25+a*12], data[13+b*12:25+b*12] = second, first
    game_path.write_bytes(data)
    for relative in relatives[2:]:
        target = shifted/relative; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(base/relative, target)
    shifted_records = read_bank(shifted)
    assert shifted_records == reordered
    shifted_ids = {r['name']: index for index, r in enumerate(shifted_records) if r['name']}
    assert shifted_ids[NULL] == 1
    original, raw, groups = originals[0]
    owner = 'GHOSTLY_SWORD'
    banks = read_rows(raw)
    selected_banks = [copy.deepcopy(b) for b in banks if b['name'] in (b'GameBINEntries', b'GameBINLinkMetaData')]
    for bank in selected_banks:
        bank['rows'] = [r for r in bank['rows'] if r['symbol'] == owner.encode()]
        assert len(bank['rows']) == 1
    source = next(b['rows'][0] for b in selected_banks if b['name'] == b'GameBINEntries')
    sheath_at = source['data'].index(bytes.fromhex('9f6d0c83'))+4
    leader_at = source['data'].index(bytes.fromhex('7c8444e3'))+4
    shifted_checks, choice_sources, choice_expected = [], [], {}
    for mode in ('null', 'positive'):
        fixture_banks = copy.deepcopy(selected_banks)
        row = next(b['rows'][0] for b in fixture_banks if b['name'] == b'GameBINEntries')
        meta = next(b['rows'][0] for b in fixture_banks if b['name'] == b'GameBINLinkMetaData')
        fixture_groups = copy.deepcopy(groups[owner])
        if mode == 'positive':
            data = bytearray(row['data'])
            struct.pack_into('<I', data, sheath_at, baseline_ids[SWORD])
            struct.pack_into('<I', data, leader_at, baseline_ids[LIEUTENANT])
            row['data'] = bytes(data)
            for group in fixture_groups:
                if group['path'] == SHEATH: group['targets'] = [SWORD]
                if group['path'] == LEADER: group['targets'] = [LIEUTENANT]
        meta['data'] = metadata(fixture_groups)
        fixture = work/('shifted-'+mode+'.fmp'); write_rows(fixture, raw, fixture_banks)
        choice_sources.append(fixture)
        control_banks = copy.deepcopy(fixture_banks)
        next(b['rows'][0] for b in control_banks if b['name'] == b'GameBINLinkMetaData')['data'] = metadata(
            [g for g in fixture_groups if g['path'] not in (SHEATH, LEADER)])
        control = work/('shifted-'+mode+'-control.fmp'); write_rows(control, raw, control_banks)
        for route in ('apply', 'merge'):
            output, prior_output = work/(mode+'-'+route), work/(mode+'-'+route+'-control')
            run(args.control_tool, *command(route, shifted, control, prior_output))
            run(args.tool, *command(route, shifted, fixture, output))
            expected = read_bank(prior_output); actual = read_bank(output)
            wanted = next(r for r in expected if r['name'] == owner)
            data = bytearray(wanted['data'])
            struct.pack_into('<I', data, sheath_at, 0 if mode == 'null' else shifted_ids[SWORD])
            struct.pack_into('<I', data, leader_at, shifted_ids[NULL if mode == 'null' else LIEUTENANT])
            wanted['data'] = bytes(data)
            assert actual == expected, (mode, route)
            choice_expected['shifted-'+mode] = wanted['data']
            shifted_checks.append({'mode': mode, 'route': route, 'fixture_sha256': digest(fixture),
                                   'only_two_reference_words_differ_from_prior_control': True})
        print('Reordered baseline', mode, 'both routes: PASS', flush=True)

    # Selected contradictory null, wrong object kind and broken local layout.
    refusals = []
    for label in ('null-named-object', 'positive-wrong-type', 'positive-missing-object', 'bad-group-tag'):
        bad_banks = copy.deepcopy(selected_banks)
        row = next(b['rows'][0] for b in bad_banks if b['name'] == b'GameBINEntries')
        meta = next(b['rows'][0] for b in bad_banks if b['name'] == b'GameBINLinkMetaData')
        gs = copy.deepcopy(groups[owner]); data = bytearray(row['data'])
        for group in gs:
            if group['path'] != SHEATH: continue
            group['targets'] = [SWORD if label == 'null-named-object' else
                                LIEUTENANT if label == 'positive-wrong-type' else
                                'MISSING_SHEATHED_OBJECT' if label == 'positive-missing-object' else NULL]
        if label.startswith('positive'): struct.pack_into('<I', data, sheath_at, baseline_ids[SWORD])
        if label == 'bad-group-tag': data[sheath_at+4] ^= 1  # PBrain tag.
        row['data'] = bytes(data); meta['data'] = metadata(gs)
        fixture = work/(label+'.fmp'); write_rows(fixture, raw, bad_banks)
        for route in ('apply', 'merge'):
            output = work/('positive-'+route); before_output = snapshot(output)
            failed = run(args.tool, *command(route, shifted, fixture, output), good=False)
            assert 'FMP links:' in failed.stderr and snapshot(output) == before_output
            refusals.append({'case': label, 'route': route, 'reason': failed.stderr.strip()})
    # Competing sources must retain the metadata belonging to the chosen bytes.
    # First use an absent record, then a labelled existing-record baseline whose
    # two words differ from both sources. All other bytes stay identical.
    choice_folder = work/'choice-sources'; choice_folder.mkdir()
    choice_owner = 'FORGE_OPTIONAL_LINK_CHOICE_FIXTURE'
    assert choice_owner not in shifted_ids
    renamed_sources = []
    for package in choice_sources:
        package_raw = package.read_bytes(); banks = read_rows(package_raw)
        for bank in banks:
            for row in bank['rows']:
                assert row['symbol'] == owner.encode()
                row['symbol'] = choice_owner.encode()
        renamed = choice_folder/package.name
        write_rows(renamed, package_raw, banks); renamed_sources.append(renamed)
    owner, choice_sources = choice_owner, renamed_sources
    choices, choice_refusals = [], []
    picks = work/'choices.txt'
    source_hashes = {str(p): digest(p) for p in choice_sources}
    for baseline_kind in ('absent', 'existing'):
        choice_base = shifted
        if baseline_kind == 'existing':
            choice_base = work/'choice-existing-baseline'
            seeded = copy.deepcopy(shifted_records)
            data = bytearray(choice_expected['shifted-null'])
            other_object = next(i for i, r in enumerate(seeded)
                                if i > 0 and r['definition'] == 'OBJECT' and r['name'] != SWORD)
            other_creature = next(i for i, r in enumerate(seeded)
                                  if r['definition'] == 'CREATURE' and r['name'] != LIEUTENANT)
            struct.pack_into('<I', data, sheath_at, other_object)
            struct.pack_into('<I', data, leader_at, other_creature)
            choice_expected['vanilla'] = bytes(data)
            seeded.append({'name': owner, 'definition': 'CREATURE', 'data': bytes(data)})
            rewrite_bank(shifted, choice_base, seeded)
            assert read_bank(choice_base) == seeded
            for relative in relatives[2:]:
                target = choice_base/relative; target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(shifted/relative, target)
        for package in choice_sources:
            run(args.tool, 'mods', 'add', choice_base, package, '--name', package.stem)
        for route in ('merge', 'build'):
            output = work/('choice-'+baseline_kind+'-'+route)
            command = ['mods', route, choice_base, output]
            if route == 'merge': command += ['--with', *choice_sources]
            for choice in ('', 'shifted-null', 'shifted-positive', *(['vanilla'] if baseline_kind == 'existing' else [])):
                picks.write_text(owner+'\t'+choice+'\n' if choice else '', encoding='utf-8')
                saved = picks.read_bytes()
                report = json.loads(run(args.tool, *command, '--picks', picks, '--json').stdout)
                winner = choice or 'shifted-positive'
                actual = read_bank(output)
                result = next(r for r in actual if r['name'] == owner)
                assert result['data'] == choice_expected[winner], (baseline_kind, route, choice)
                assert [r for r in actual if r['name'] != owner] == shifted_records
                conflict = next(c for c in report['defs']['conflicts'] if c['record'] == owner)
                assert conflict['field_merged'] == (baseline_kind == 'existing')
                if baseline_kind == 'absent': assert conflict['winner'] == winner
                else:
                    assert len(conflict['field_conflicts']) == 2
                    assert all(f['winner'] == winner and f['choice_key'] == ''
                               for f in conflict['field_conflicts'])
                assert picks.read_bytes() == saved
                choices.append({'baseline': baseline_kind, 'route': route,
                                'choice': choice or 'order', 'winner': winner})
        run(args.tool, 'mods', 'disable', choice_base, 'shifted-null')
        for route in ('merge', 'build'):
            output = work/('choice-'+baseline_kind+'-'+route)
            command = ['mods', route, choice_base, output]
            if route == 'merge': command += ['--with', choice_sources[1]]
            picks.write_text(owner+'\tshifted-null\n', encoding='utf-8')
            saved, prior = picks.read_bytes(), snapshot(output)
            failure = run(args.tool, *command, '--picks', picks, '--json', good=False)
            assert 'unavailable contributor shifted-null' in failure.stderr
            assert snapshot(output) == prior and picks.read_bytes() == saved
            choice_refusals.append({'baseline': baseline_kind, 'route': route,
                                    'reason': failure.stderr.strip()})
            picks.write_text('', encoding='utf-8')
            run(args.tool, *command, '--picks', picks, '--json')
            assert next(r['data'] for r in read_bank(output) if r['name'] == owner) == choice_expected['shifted-positive']
            choices.append({'baseline': baseline_kind, 'route': route,
                            'choice': 'clear-unavailable', 'winner': 'shifted-positive'})
        print('Source choices', baseline_kind, 'both routes: PASS', flush=True)
    assert source_hashes == {str(p): digest(p) for p in choice_sources}
    for path, raw, _ in originals: assert path.read_bytes() == raw
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'originals': reports, 'shifted_checks': shifted_checks,
        'refusals': refusals, 'choices': choices, 'choice_refusals': choice_refusals,
        'choice_sources_sha256': source_hashes, 'derived_baseline_swaps': swaps, 'inputs_sha256': before,
        'inputs_unchanged': True, 'gameplay_tested': False}, indent=2))
    print('PASS: four originals, four reordered-bank cases, eight safe negative routes,',
          len(choices), 'source choices,', len(choice_refusals), 'safe unavailable refusals', flush=True)


if __name__ == '__main__':
    main()
