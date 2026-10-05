#!/usr/bin/env python3
"""Verify original region-mod description and breath-effect asset references."""
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
    effects = args.install/'data/Misc/pc/effects.big'
    inputs = [args.archive, text, effects, args.install/'data/CompiledDefs/game.bin',
              args.install/'data/CompiledDefs/names.bin']
    before = {str(p): digest(p) for p in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-region-links-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    names = ('Bowerstone South Pole', 'Bower Stone Weapon Laws', 'Bower Stone Weapon Magick laws',
             'Darkwood Winter', 'Fable PAIN', 'Frostbitten Albion', 'No Crimes', 'REGION Restore',
             'Regions Mod', 'Weapons & Spells in Bowerstone & Bordello', 'Wep Mag Ban Lift Gold Cap Incr')
    checks = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for index, name in enumerate(names):
            member = f'mods/{name}.fmp'; payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            package = work/f'{index}.fmp'; package.write_bytes(payload)
            source = package_records(package)['GameBINEntries']
            report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
            descriptions, breath = [], []
            for record in report['records']:
                for group in record['groups']:
                    if group['path'] == '801675342.Region Description':
                        assert len(group['targets']) == 1
                        descriptions.append((record['record'], group['targets'][0]))
                    if group['path'] == '435772141.Area Effects':
                        assert len(group['targets']) == 1
                        breath.append((record['record'], group['targets'][0]))
            assert descriptions or breath
            text_ids = entries(text, 'TEXT_ENGLISH_MAIN', {n for _, n in descriptions}, False)
            effect_ids = entries(effects, 'PARTICLE_MAIN_PC', {n for _, n in breath}, False)
            direct, merged = work/f'direct-{index}', work/f'merged-{index}'
            run(args.tool, 'fmp', 'apply', args.install, package, direct)
            run(args.tool, 'mods', 'merge', args.install, merged, '--with', package)
            for bank in ('game.bin', 'names.bin'):
                assert digest(direct/'data/CompiledDefs'/bank) == digest(merged/'data/CompiledDefs'/bank)
            built = {e['name']: e['data'] for e in read_bank(direct) if e['name']}
            for refs, ids, tag in ((descriptions, text_ids, '2fc8984e'), (breath, effect_ids, '19f95aed')):
                for record, target in refs:
                    at = field_start(source[record][0], tag)
                    assert struct.unpack_from('<I', built[record], at)[0] == ids[target]['id']
            checks.append({'file': member, 'description_refs': len(descriptions), 'breath_refs': len(breath)})
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'inputs_unchanged': True,
                                              'gameplay_tested': False}, indent=2))
    print('PASS: 11 original region mods; direct/composed parity and actual text/effect IDs')


if __name__ == '__main__':
    main()
