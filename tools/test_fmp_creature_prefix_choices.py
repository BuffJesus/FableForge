#!/usr/bin/env python3
"""Original anonymous creature prefix symbols follow selected source/baseline."""
import argparse
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
from test_mod_file_choice_contributors import snapshot


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
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fmp-creature-prefix-choices-', dir=args.work_root)).resolve()
    assert work.parent == args.work_root.resolve()
    print('Evidence retained at', work, flush=True)
    relatives = ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/graphics/pc/textures.big', 'data/graphics/graphics.big')
    inputs = [args.archive, args.tool, *(args.install/p for p in relatives)]
    before = {str(p): digest(p) for p in inputs}
    baseline = work/'baseline'
    for relative in relatives:
        target = baseline/relative; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.install/relative, target)
    sources = []; source_inventory = {}; expected = {}; linked = {}
    anchor = struct.pack('<I', crc0('InitialActions'))
    base_record = read_bank(baseline)[10532]
    assert base_record['definition'] == 'CCreatureDef'
    boundary = base_record['data'].index(anchor)
    expected['vanilla'] = base_record['data'][:boundary]
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for label, member in [('NoScars', 'mods/Bruised and Bloodied (No Scars).fmp'),
                              ('WithScars', 'mods/Bruised and Bloodied (With Scars).fmp')]:
            raw = archive.read(member); assert hashlib.sha256(raw).hexdigest() == catalogue[member]['sha256']
            package = work/(label+'.fmp'); package.write_bytes(raw); sources.append(package)
            source_inventory[str(package)] = hashlib.sha256(raw).hexdigest()
            source = package_records(package)['GameBINEntries'][''][0]
            assert source.index(anchor) == boundary
            groups = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)['records'][0]['groups']
            prefix_groups = [g for g in groups if g['path'].startswith(('4294151720.', '4042377857.'))]
            textures = entries(baseline/'data/graphics/pc/textures.big', 'GBANK_MAIN_PC',
                {g['targets'][0] for g in prefix_groups if g['path'].startswith('4294151720.')}, include_payload=False)
            animations = entries(baseline/'data/graphics/graphics.big', 'MBANK_ALLMESHES',
                {g['targets'][0] for g in prefix_groups if g['path'].startswith('4042377857.')}, include_payload=False)
            prefix = bytearray(source[:boundary])
            assert struct.unpack_from('<I', prefix, 7)[0] == 25
            expression_at = 11+25*44+4+64
            assert prefix[expression_at:expression_at+8] == bytes.fromhex('f0f1ca81')+struct.pack('<I', 4)
            for g in prefix_groups:
                parts = g['path'].split('.'); assert len(g['targets']) == 1
                if parts[0] == '4294151720':
                    slot = 0 if parts[3] == 'Link to ID' else int(parts[3][-1])
                    assert 0 <= slot < 6 and 0 <= int(parts[2]) < 25
                    at = 11+int(parts[2])*44+8+slot*4; value = textures[g['targets'][0]]['id']
                else:
                    assert 0 <= int(parts[2]) < 4
                    at = expression_at+8+int(parts[2])*12+8; value = animations[g['targets'][0]]['id']
                struct.pack_into('<I', prefix, at, value)
            expected[label] = prefix; linked[label] = len(prefix_groups)
            run(args.tool, 'mods', 'add', baseline, package, '--name', label)
    assert len({bytes(v) for v in expected.values()}) == 3
    picks = work/'choices.txt'; checks = []
    for route in ('merge', 'build'):
        output = work/route
        command = ['mods', route, baseline, output]
        if route == 'merge': command += ['--with', *sources]
        for choice in ('', 'NoScars', 'WithScars', 'vanilla'):
            picks.write_text('@unnamed:10532\t'+choice+'\n' if choice else '', encoding='utf-8')
            saved = picks.read_bytes()
            result = run(args.tool, *command, '--picks', picks, '--json')
            report = json.loads(result.stdout)
            conflict = next(c for c in report['defs']['conflicts'] if c['index'] == 10532)
            field = next(f for f in conflict['field_conflicts'] if f['part_index'] == 0)
            winner = choice or 'WithScars'
            assert field['winner'] == winner and field['choice_key'] == ''
            data = read_bank(output)[10532]['data']
            assert data[:boundary] == expected[winner]
            assert data[boundary:] == base_record['data'][boundary:]
            assert picks.read_bytes() == saved
            checks.append({'route': route, 'choice': choice or 'order', 'winner': winner})
    run(args.tool, 'mods', 'disable', baseline, 'NoScars')
    refusals = []
    for route in ('merge', 'build'):
        output = work/route
        command = ['mods', route, baseline, output]
        if route == 'merge': command += ['--with', sources[1]]
        picks.write_text('@unnamed:10532\tNoScars\n', encoding='utf-8')
        saved_picks = picks.read_bytes(); saved_output = snapshot(output)
        failure = run(args.tool, *command, '--picks', picks, '--json', good=False)
        assert 'unavailable contributor NoScars' in failure.stderr
        assert snapshot(output) == saved_output and picks.read_bytes() == saved_picks
        refusals.append({'route': route, 'reason': failure.stderr.strip()})
        picks.write_text('', encoding='utf-8')
        run(args.tool, *command, '--picks', picks, '--json')
        assert read_bank(output)[10532]['data'][:boundary] == expected['WithScars']
        checks.append({'route': route, 'choice': 'clear-unavailable', 'winner': 'WithScars'})
    assert source_inventory == {str(p): digest(p) for p in sources}
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'refusals': refusals, 'prefix_links': linked,
        'inputs_sha256': before | source_inventory, 'inputs_unchanged': True, 'other_fields_unchanged': True,
        'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS: ten original prefix selections, two safe unavailable refusals, exact symbols and unchanged inputs')


if __name__ == '__main__':
    main()
