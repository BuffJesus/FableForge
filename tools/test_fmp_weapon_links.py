#!/usr/bin/env python3
"""Check original weapon FMP references, composition, and bounded refusal."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from test_fmp_links import package_records, read_bank
from test_fmp_texture_layers import entries
from test_fmp_brain_behaviour_links import name_offsets


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(tool, *argv, good=True):
    result = subprocess.run([str(tool), *map(str, argv)], capture_output=True,
                            text=True, timeout=240)
    assert (result.returncode == 0) == good, (argv, result.stderr[-2000:])
    return result


def field_start(raw, tag):
    needle = bytes.fromhex(tag)
    assert raw.count(needle) == 1
    return raw.index(needle) + 4


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    effect_file = args.install/'data/Misc/pc/effects.big'
    texture_file = args.install/'data/graphics/pc/textures.big'
    inputs = [args.archive, effect_file, texture_file,
              args.install/'data/CompiledDefs/game.bin',
              args.install/'data/CompiledDefs/names.bin']
    before = {str(path): digest(path) for path in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-weapon-links-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    members = ('mods/Avenger Flourish.fmp', 'mods/Dripping Blood.fmp')
    packages = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        for index, member in enumerate(members):
            payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            package = work/f'weapon-{index}.fmp'; package.write_bytes(payload)
            packages.append(package)
    effects = entries(effect_file, 'PARTICLE_MAIN_PC',
                      {'FLAMING_SWORD_LEVEL_2', 'BLOOD_DRIPPING'}, include_payload=False)
    report = json.loads(run(args.tool, 'fmp', 'links', packages[0], '--groups', '--json').stdout)
    assert len(report['records']) == 1
    groups = report['records'][0]['groups']
    texture_names = {target for group in groups for target in group['targets']
                     if group['path'].startswith(('473863218.', '1732080037.'))}
    textures = entries(texture_file, 'GBANK_MAIN_PC', texture_names, include_payload=False)
    source, payload_offset = package_records(packages[0])['GameBINEntries']['']
    flourish = field_start(source, 'b297657e')
    first_count = struct.unpack_from('<I', source, flourish + 20)[0]
    second_at = flourish + 24 + first_count * 4
    expected = bytearray(source)
    direct = work/'avenger-direct'; merged = work/'avenger-merged'
    run(args.tool, 'fmp', 'apply', args.install, packages[0], direct)
    run(args.tool, 'mods', 'merge', args.install, merged, '--with', packages[0])
    for bank in ('game.bin', 'names.bin'):
        assert digest(direct/'data/CompiledDefs'/bank) == digest(merged/'data/CompiledDefs'/bank)
    built = read_bank(direct)
    ids = {entry['name']: index for index, entry in enumerate(built) if entry['name']}
    names = name_offsets(direct/'data/CompiledDefs/names.bin')
    checked = 0
    scalar_tags = {'301428501': '11f76f15', '2583195946': '99f8712a',
                   '135199086': '080ef96e', '1353002850': '50a52f62'}
    for group in groups:
        path, targets = group['path'], group['targets']
        parts = path.split('.')
        if path == '2996266366.Flourish Effect':
            offsets = [flourish + 4, flourish + 8]
            values = [effects[target]['id'] for target in targets]
        elif parts[0] == '2996266366':
            first = parts[1] == 'Weapon Augmentation Slots List 1'
            offsets = [(flourish + 24 if first else second_at + 4) + int(parts[2]) * 4]
            values = [names[targets[0]]]
        elif path == '473863218.Flourish Sprite':
            start = field_start(source, '1c3e9432')
            offsets = [start, start + 4]
            values = [textures[target]['id'] for target in targets]
        elif parts[0] == '1732080037':
            start = field_start(source, '673d71a5') + 4 + int(parts[2]) * 12
            offsets = [start + 4, start + 8]
            values = [textures[target]['id'] for target in targets]
        else:
            assert parts[0] in scalar_tags and len(targets) == 1, path
            offsets = [field_start(source, scalar_tags[parts[0]])]
            values = [ids[targets[0]]]
        assert len(offsets) == len(values)
        for at, value in zip(offsets, values):
            struct.pack_into('<I', expected, at, value); checked += 1
    assert checked == 20 and built[14282]['data'] == expected

    # Both original packages edit this weapon's particle field. Each load order
    # must retain the winner's named effects after final identity resolution.
    for index, order in enumerate((packages, list(reversed(packages)))):
        output = work/f'order-{index}'
        result = run(args.tool, 'mods', 'merge', args.install, output,
                     '--with', *order, '--json')
        assert 'FlourishParticles' in result.stdout
        raw = read_bank(output)[14282]['data']
        at = field_start(raw, 'b297657e')
        target = 'BLOOD_DRIPPING' if index == 0 else 'FLAMING_SWORD_LEVEL_2'
        assert struct.unpack_from('<2I', raw, at + 4) == (effects[target]['id'],) * 2

    # Corrupt a list count in the original package, then require both import
    # routes to fail before replacing any pre-existing output.
    mutant = bytearray(packages[0].read_bytes())
    trail = field_start(source, '673d71a5')
    struct.pack_into('<I', mutant, payload_offset + trail, 4097)
    broken = work/'broken.fmp'; broken.write_bytes(mutant)
    prior = work/'prior'; prior.mkdir(); (prior/'sentinel').write_bytes(b'kept')
    for command in (('fmp', 'apply', args.install, broken, prior),
                    ('mods', 'merge', args.install, prior, '--with', broken)):
        failed = run(args.tool, *command, good=False)
        assert 'weapon trail list layout' in failed.stderr
        assert sorted(p.name for p in prior.iterdir()) == ['sentinel']
        assert (prior/'sentinel').read_bytes() == b'kept'
    assert before == {str(path): digest(path) for path in inputs}
    (work/'result.json').write_text(json.dumps({'members': members, 'links_checked': checked,
        'direct_merge_equal': True, 'both_load_orders': True, 'malformed_refused': True,
        'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS: 20 weapon links; direct/merge parity, both load orders, bounded refusal')


if __name__ == '__main__':
    main()
