#!/usr/bin/env python3
"""Original village/component aliases match equivalent metadata in a prior tool."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile
import zlib

from test_fmp_creature_optional_links import digest, metadata
from test_fmp_identical_records import read_rows, write_rows
from test_fmp_links import crc0, read_bank
from test_fmp_weapon_links import run
from test_mod_file_choice_contributors import snapshot

TEACHERS = '4272390883._'
OWNER = 'VILLAGE_KNOTHOLE_GLADE - KH_CHILDREN - CVillagePeopleDef'
ALIASES = dict(zip(('3898150842', '2486342274', '1867951032', '587433138', '368349979', '600034842'),
                   ('Weapon1ID', 'Weapon2ID', 'BrainID', 'AttackStyleID', 'CombatType1ID', 'CombatType2ID')))


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--control-tool', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fmp-component-aliases-', dir=args.work_root)).resolve()
    assert work.parent == args.work_root.resolve()
    print('Evidence retained at', work, flush=True)
    relatives = ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/graphics/graphics.big', 'data/graphics/pc/textures.big',
                 'data/lang/English/text.big', 'data/Misc/pc/effects.big')
    inputs = [args.archive, args.tool, args.control_tool, *(args.install/p for p in relatives)]
    before = {str(p): digest(p) for p in inputs}
    base = work/'baseline'
    for relative in relatives:
        target = base/relative; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.install/relative, target)
    with zipfile.ZipFile(args.archive) as archive:
        member = 'mods/Knothole Glade Children (Anniversary).fmp'
        wrapped = archive.read(member)
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        assert hashlib.sha256(wrapped).hexdigest() == catalogue[member]['sha256']
    raw = zlib.decompress(wrapped[4:])
    assert struct.unpack_from('<I', wrapped)[0] == 12345 and raw[:4] == b'B\0\0\0'
    original = work/'original.fmp'; original.write_bytes(wrapped)
    groups = json.loads(run(args.tool, 'fmp', 'links', original, '--groups', '--json').stdout)['records']
    original_groups = {r['record']: r['groups'] for r in groups}
    original_banks = read_rows(raw)

    def write(package, groups, normalize=False):
        banks = copy.deepcopy(original_banks)
        for bank in banks:
            if bank['name'] != b'GameBINLinkMetaData': continue
            for row in bank['rows']:
                gs = copy.deepcopy(groups[row['symbol'].decode()])
                if normalize:
                    for g in gs:
                        p = g['path'].split('.')
                        if g['path'] == TEACHERS: g['path'] = '4272390883.VillagePeopleID'
                        elif len(p) == 5 and p[:3] == ['1537122114', 'Components', 'Add'] and p[4] == 'Component':
                            g['path'] = '1537122114.CTC List.'+p[3]+'.CTC Name'
                        elif len(p) == 2 and p[0] in ALIASES and p[1] == '_':
                            g['path'] = p[0]+'.'+ALIASES[p[0]]
                row['data'] = metadata(gs)
        write_rows(package, raw, banks)

    def command(route, package, output):
        return ('fmp', 'apply', base, package, output) if route == 'apply' else (
            'mods', 'merge', base, output, '--with', package)

    checks, refusals = [], []
    for mode in ('original', 'ordered-teachers'):
        gs = copy.deepcopy(original_groups)
        if mode == 'ordered-teachers':
            next(g for g in gs[OWNER] if g['path'] == TEACHERS)['targets'] = [
                'CREATURE_KH_VILLAGER_GIRL', 'CREATURE_KH_VILLAGER_BOY']
        package = original if mode == 'original' else work/(mode+'.fmp')
        if mode != 'original': write(package, gs)
        control = work/(mode+'-control.fmp'); write(control, gs, normalize=True)
        for route in ('apply', 'merge'):
            output, prior = work/(mode+'-'+route), work/(mode+'-'+route+'-control')
            run(args.control_tool, *command(route, control, prior))
            run(args.tool, *command(route, package, output))
            for relative in relatives:
                a = output/relative if (output/relative).exists() else base/relative
                b = prior/relative if (prior/relative).exists() else base/relative
                assert digest(a) == digest(b), (mode, route, relative)
            records = read_bank(output); ids = {r['name']: i for i, r in enumerate(records) if r['name']}
            data = records[ids[OWNER]]['data']; tag = struct.pack('<I', crc0('MaleTeacher'))
            positions = [i+4 for i in range(len(data)-3) if data[i:i+4] == tag]
            expected = next(g for g in gs[OWNER] if g['path'] == TEACHERS)['targets']
            assert len(positions) == 2
            assert [struct.unpack_from('<I', data, at)[0] for at in positions] == [ids[t] for t in expected]
            checks.append({'mode': mode, 'route': route, 'source_sha256': digest(package),
                           'control_sha256': digest(control), 'six_bank_parity': True})
        print(mode, 'both routes match old-spelling control: PASS', flush=True)
    for case in ('one-teacher', 'three-teachers', 'old-one-teacher', 'component-out-of-range', 'component-wrong-member', 'missing-component'):
        gs = copy.deepcopy(original_groups)
        teacher = next(g for g in gs[OWNER] if g['path'] == TEACHERS)
        component = next(g for g in gs['CREATURE_KH_VILLAGER_BOY'] if g['path'].startswith('1537122114.'))
        if case in ('one-teacher', 'old-one-teacher'): teacher['targets'] = teacher['targets'][:1]
        if case == 'old-one-teacher': teacher['path'] = '4272390883.VillagePeopleID'
        if case == 'three-teachers': teacher['targets'].append(teacher['targets'][0])
        if case == 'component-out-of-range': component['path'] = '1537122114.Components.Add.4096.Component'
        if case == 'component-wrong-member': component['path'] = '1537122114.Components.Add.0.Parameter'
        if case == 'missing-component': component['targets'] = ['CTC_FORGE_MISSING_COMPONENT']
        package = work/(case+'.fmp'); write(package, gs)
        for route in ('apply', 'merge'):
            output = work/('original-'+route); saved = snapshot(output)
            failure = run(args.tool, *command(route, package, output), good=False)
            assert 'FMP links:' in failure.stderr and snapshot(output) == saved
            refusals.append({'case': case, 'route': route, 'reason': failure.stderr.strip()})
    assert original.read_bytes() == wrapped and before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'refusals': refusals,
        'inputs_sha256': before, 'inputs_unchanged': True, 'gameplay_tested': False}, indent=2))
    print('PASS:', len(checks), 'original/derived imports,', len(refusals), 'safe negative routes', flush=True)


if __name__ == '__main__':
    main()
