#!/usr/bin/env python3
"""Check original potion/shot effect identities and larger FMP composition parity."""
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
    effect_file = args.install/'data/Misc/pc/effects.big'
    inputs = [args.archive, effect_file, args.install/'data/CompiledDefs/game.bin',
              args.install/'data/CompiledDefs/names.bin']
    before = {str(path): digest(path) for path in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-particle-links-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    members = ('mods/No Need for XP.fmp', 'mods/Black Magic.fmp', 'mods/Fable Reloaded.fmp')
    checks = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        for index, member in enumerate(members):
            payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            package = work/f'particle-{index}.fmp'; package.write_bytes(payload)
            source = package_records(package, by_id=True)['GameBINEntries']
            metadata = package_records(package, by_id=True)['GameBINLinkMetaData']
            direct, merged = work/f'direct-{index}', work/f'merged-{index}'
            run(args.tool, 'fmp', 'apply', args.install, package, direct)
            run(args.tool, 'mods', 'merge', args.install, merged, '--with', package)
            for bank in ('game.bin', 'names.bin'):
                assert digest(direct/'data/CompiledDefs'/bank) == digest(merged/'data/CompiledDefs'/bank)
            built = read_bank(direct)
            report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
            names = {entry['name']: idx for idx, entry in enumerate(built) if entry['name']}
            targets = {target for record in report['records'] for group in record['groups']
                       if group['path'].startswith(('4071417874.', '3914204730.', '1451703396.'))
                       for target in group['targets']}
            effects = entries(effect_file, 'PARTICLE_MAIN_PC', targets, include_payload=False)
            count = 0
            assert len(report['records']) == len(metadata)
            source_ids = list(source)
            for record, metadata_id in zip(report['records'], metadata):
                for group in record['groups']:
                    path = group['path']
                    control = path.split('.')[0]
                    if control not in ('4071417874', '3914204730', '1451703396'):
                        continue
                    target, = group['targets']
                    # Legacy anonymous metadata uses a one-based position in
                    # the ordered GameBINEntries bank, not a game.bin ID.
                    owner = names[record['record']] if record['record'] else source_ids[metadata_id - 1]
                    original = source[owner][0]
                    result = built[owner]['data']
                    at = field_start(original, int(control).to_bytes(4, 'big').hex())
                    assert field_start(result, int(control).to_bytes(4, 'big').hex()) == at
                    assert struct.unpack_from('<I', result, at)[0] == effects[target]['id']
                    count += 1
            assert count > 0
            checks.append({'member': member, 'effect_links': count,
                           'imported_records': len(source), 'direct_merge_equal': True})
            if index == 0:
                needle = b'POTION_HEALTH_01'
                metadata_id, (raw, offset) = next((key, value) for key, value in metadata.items()
                                                 if needle in value[0])
                assert raw.count(needle) == 1
                mutant = bytearray(payload); mutant[offset + raw.index(needle)] = ord('Z')
                # Make the linked component a selected change. The composer
                # intentionally ignores metadata on unchanged source records.
                original, original_offset = source[source_ids[metadata_id - 1]]
                struct.pack_into('<I', mutant,
                                 original_offset + field_start(original, '56873c64'), 0xffffffff)
                missing = work/'missing.fmp'; missing.write_bytes(mutant)
                prior = work/'prior'; prior.mkdir(); (prior/'sentinel').write_bytes(b'kept')
                for command in (('fmp', 'apply', args.install, missing, prior),
                                ('mods', 'merge', args.install, prior, '--with', missing)):
                    failed = run(args.tool, *command, good=False)
                    assert 'missing target ZOTION_HEALTH_01' in failed.stderr
                    assert sorted(p.name for p in prior.iterdir()) == ['sentinel']
                    assert (prior/'sentinel').read_bytes() == b'kept'
    assert before == {str(path): digest(path) for path in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'missing_refused': True,
        'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS:', sum(c['effect_links'] for c in checks),
          'potion/shot effect links; original package parity and missing-target refusal')


if __name__ == '__main__':
    main()
