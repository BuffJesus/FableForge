#!/usr/bin/env python3
"""Labelled environment Time derivatives retain symbols under source choices."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile

from test_fmp_environment_links import digest, time_rows
from test_fmp_identical_records import read_rows, write_rows
from test_fmp_links import read_bank
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run
from test_mod_file_choice_contributors import snapshot


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fmp-environment-choices-', dir=args.work_root)).resolve()
    assert work.parent == args.work_root.resolve()
    print('Evidence retained at', work, flush=True)
    relatives = ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/graphics/pc/textures.big', 'data/graphics/graphics.big')
    inputs = [args.archive, args.tool, *(args.install/p for p in relatives)]
    before = {str(p): digest(p) for p in inputs}
    base = work/'baseline'
    for relative in relatives:
        target = base/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.install/relative, target)
    owner = 'ENVIRONMENT_THEME1'
    baseline_index, baseline = next((index, r) for index, r in enumerate(read_bank(base)) if r['name'] == owner)
    fields = json.loads((repo/'docs/re_reference/def_schema.json').read_text())['CEnvironmentThemeDef']['fields']
    base_end = 13 + 269 * len(time_rows(baseline['data'], fields))
    textures = entries(base/'data/graphics/pc/textures.big', 'GBANK_MAIN_PC', include_payload=False)
    with zipfile.ZipFile(args.archive) as archive:
        raw = archive.read('mods/Enviroment Snowing.fmp')
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        assert hashlib.sha256(raw).hexdigest() == catalogue['mods/Enviroment Snowing.fmp']['sha256']
    original = work/'original.fmp'
    original.write_bytes(raw)
    metadata = json.loads(run(args.tool, 'fmp', 'links', original, '--groups', '--json').stdout)
    groups = {r['record']: r['groups'] for r in metadata['records']}
    original_banks = read_rows(raw)
    expected = {'vanilla': baseline['data']}
    sources, derivatives = [], []
    for label, source_owner in (('Snowing', owner), ('Battle', 'ENVIRONMENT_THEME_BATTLE')):
        banks = [copy.deepcopy(b) for b in original_banks
                 if b['name'] in (b'GameBINEntries', b'GameBINLinkMetaData')]
        assert len(banks) == 2
        for bank in banks:
            bank['rows'] = [r for r in bank['rows'] if r['symbol'] == source_owner.encode()]
            assert len(bank['rows']) == 1
            bank['rows'][0]['symbol'] = owner.encode()
        entry = next(b['rows'][0] for b in banks if b['name'] == b'GameBINEntries')
        source = entry['data']
        rows = time_rows(source, fields)
        source_end = 13 + len(rows)*269
        # Explicitly derive two contributors to the same existing record:
        # retain original Time/metadata, use baseline prefix and later fields.
        entry['data'] = baseline['data'][:9] + source[9:source_end] + baseline['data'][base_end:]
        package = work/(label+'.fmp')
        write_rows(package, raw, banks)
        result = bytearray(entry['data'])
        links = 0
        for group in groups[source_owner]:
            parts = group['path'].split('.')
            assert parts[:2] == ['1743315694', 'unknown'] and len(parts) == 4
            names = ['SkyTexture0', 'SkyTexture1'] if parts[3] == 'Sky' else [
                'CloudLowerLayerTexture0', 'CloudLowerLayerTexture1',
                'CloudUpperLayerTexture0', 'CloudUpperLayerTexture1']
            assert parts[3] in ('Sky', 'Clouds')
            values, offsets = rows[int(parts[2])]
            populated = [name for name in names if values[name] != 0]
            assert len(populated) == len(group['targets'])
            for name, target in zip(populated, group['targets']):
                struct.pack_into('<I', result, offsets[name], textures[target]['id'])
                links += 1
        expected[label] = bytes(result)
        sources.append(package)
        derivatives.append({'label': label, 'source_record': source_owner, 'rows': len(rows),
                            'links': links, 'path': str(package), 'sha256': digest(package)})
        run(args.tool, 'mods', 'add', base, package, '--name', label)
    assert len({value for value in expected.values()}) == 3
    assert len({row['rows'] for row in derivatives}) == 2
    picks = work/'choices.txt'
    checks, refusals = [], []
    for route in ('merge', 'build'):
        output = work/route
        command = ['mods', route, base, output]
        if route == 'merge': command += ['--with', *sources]
        for choice in ('', 'Snowing', 'Battle', 'vanilla'):
            picks.write_text(owner+'\t'+choice+'\n' if choice else '', encoding='utf-8')
            saved = picks.read_bytes()
            report = json.loads(run(args.tool, *command, '--picks', picks, '--json').stdout)
            conflict = next(c for c in report['defs']['conflicts'] if c['index'] == baseline_index)
            field = next(f for f in conflict['field_conflicts'] if f['part_index'] == 1)
            winner = choice or 'Battle'
            assert field['winner'] == winner and field['choice_key'] == ''
            actual = next(r for r in read_bank(output) if r['name'] == owner)
            assert actual['data'] == expected[winner], (route, choice)
            assert picks.read_bytes() == saved
            checks.append({'route': route, 'choice': choice or 'order', 'winner': winner})
    run(args.tool, 'mods', 'disable', base, 'Snowing')
    for route in ('merge', 'build'):
        output = work/route
        command = ['mods', route, base, output]
        if route == 'merge': command += ['--with', sources[1]]
        picks.write_text(owner+'\tSnowing\n', encoding='utf-8')
        saved, prior = picks.read_bytes(), snapshot(output)
        failure = run(args.tool, *command, '--picks', picks, '--json', good=False)
        assert 'unavailable contributor Snowing' in failure.stderr
        assert snapshot(output) == prior and picks.read_bytes() == saved
        refusals.append({'route': route, 'reason': failure.stderr.strip()})
        picks.write_text('', encoding='utf-8')
        run(args.tool, *command, '--picks', picks, '--json')
        assert next(r for r in read_bank(output) if r['name'] == owner)['data'] == expected['Battle']
        checks.append({'route': route, 'choice': 'clear-unavailable', 'winner': 'Battle'})
    for row in derivatives:
        assert digest(Path(row['path'])) == row['sha256']
    assert original.read_bytes() == raw
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'refusals': refusals,
        'derivatives': derivatives, 'original_sha256': digest(original),
        'inputs_sha256': before, 'inputs_unchanged': True, 'all_other_fields_unchanged': True,
        'gameplay_tested': False}, indent=2))
    print('PASS: ten Time selections, two safe unavailable refusals, exact texture symbols', flush=True)


if __name__ == '__main__':
    main()
