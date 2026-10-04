#!/usr/bin/env python3
"""Verify sparse augmentation effect groups and named dummy-node references."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zipfile

from test_fmp_links import package_records, read_bank
from test_fmp_texture_layers import entries
from test_fmp_brain_behaviour_links import name_offsets
from test_fmp_weapon_links import run, field_start


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def particle_rows(raw):
    start = field_start(raw, '7bfbc2d2')
    count = struct.unpack_from('<I', raw, start)[0]
    assert 0 < count <= 32
    cursor = start + 4
    rows = []
    for _ in range(count):
        effects = [cursor + 8 + slot * 4 for slot in range(4)]
        cursor += 24
        lists = []
        for _ in range(2):
            size = struct.unpack_from('<I', raw, cursor)[0]
            assert size <= 16
            cursor += 4
            lists.append([cursor + item * 4 for item in range(size)])
            cursor += size * 4
        rows.append((effects, lists))
    assert raw[cursor:cursor+4] == bytes.fromhex('d16ac5f6')
    return rows


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    effect_file = args.install/'data/Misc/pc/effects.big'
    text_file = args.install/'data/lang/English/text.big'
    inputs = [args.archive, effect_file, text_file,
              args.install/'data/CompiledDefs/game.bin',
              args.install/'data/CompiledDefs/names.bin']
    before = {str(path): digest(path) for path in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-augmentation-links-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    member = 'mods/Obsidian Edge Augmentation Fix.fmp'
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        payload = archive.read(member)
        assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
    package = work/'augmentation.fmp'; package.write_bytes(payload)
    owner = 'OBSIDIAN_EDGE_OBJECT_AUG_DEF'
    source, payload_offset = package_records(package)['GameBINEntries'][owner]
    rows = particle_rows(source)
    report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
    assert len(report['records']) == 1 and report['records'][0]['record'] == owner
    groups = report['records'][0]['groups']
    effect_groups = [g for g in groups if g['path'].endswith('.unknown')]
    effect_names = {target for group in effect_groups for target in group['targets']}
    effects = entries(effect_file, 'PARTICLE_MAIN_PC', effect_names, include_payload=False)
    text = entries(text_file, 'TEXT_ENGLISH_MAIN', {'TEXT_GUI_EMPTY_AUGMENTATION_SLOT'},
                   include_payload=False)
    direct, merged = work/'direct', work/'merged'
    run(args.tool, 'fmp', 'apply', args.install, package, direct)
    run(args.tool, 'mods', 'merge', args.install, merged, '--with', package)
    for name in ('game.bin', 'names.bin'):
        assert digest(direct/'data/CompiledDefs'/name) == digest(merged/'data/CompiledDefs'/name)
    built = read_bank(direct)
    ids = {entry['name']: index for index, entry in enumerate(built) if entry['name']}
    names = name_offsets(direct/'data/CompiledDefs/names.bin')
    expected = bytearray(source)
    checked = 0
    sparse_first = sparse_last = full = False
    for group in groups:
        path, targets = group['path'], group['targets']
        parts = path.split('.')
        if parts[0] == '2080096978':
            effect_slots, lists = rows[int(parts[2])]
            if parts[3] == 'unknown':
                populated = [at for at in effect_slots if struct.unpack_from('<I', source, at)[0]]
                assert len(populated) == len(targets)
                sparse_first |= populated == effect_slots[:2]
                sparse_last |= populated == effect_slots[2:]
                full |= populated == effect_slots
                offsets = populated
                values = [effects[target]['id'] for target in targets]
            else:
                which = 0 if parts[3] == 'Model Dummy to Attach Effect 1' else 1
                offsets = [lists[which][int(parts[4])]]
                values = [names[targets[0]]]
        elif parts[0] == '3513435638':
            offsets = [field_start(source, 'd16ac5f6')]
            values = [text[targets[0]]['id']]
        elif parts[0] == '1057197361':
            offsets = [field_start(source, '3f038d31') + 4 + int(parts[2]) * 4]
            values = [ids[targets[0]]]
        else:
            offsets = [field_start(source, int(parts[0]).to_bytes(4, 'big').hex())]
            values = [effects[targets[0]]['id']]
        for at, value in zip(offsets, values):
            struct.pack_into('<I', expected, at, value); checked += 1
    assert sparse_first and sparse_last and full
    assert checked == 67 and built[ids[owner]]['data'] == expected
    # Removing a populated source effect must refuse instead of shifting the
    # two metadata targets into the remaining words.
    sparse = next(slots for slots, _ in rows if
                  sum(bool(struct.unpack_from('<I', source, at)[0]) for at in slots) == 2)
    at = next(at for at in sparse if struct.unpack_from('<I', source, at)[0])
    mutant = bytearray(package.read_bytes())
    struct.pack_into('<I', mutant, payload_offset + at, 0)
    broken = work/'ambiguous.fmp'; broken.write_bytes(mutant)
    prior = work/'prior'; prior.mkdir(); (prior/'sentinel').write_bytes(b'kept')
    oversized = bytearray(package.read_bytes())
    struct.pack_into('<I', oversized,
                     payload_offset + field_start(source, '7bfbc2d2') + 4 + 24, 4097)
    large_list = work/'oversized-list.fmp'; large_list.write_bytes(oversized)
    for invalid, message in ((broken, 'ambiguous augmentation effect slots'),
                             (large_list, 'augmentation particle name list')):
        for command in (('fmp', 'apply', args.install, invalid, prior),
                        ('mods', 'merge', args.install, prior, '--with', invalid)):
            failed = run(args.tool, *command, good=False)
            assert message in failed.stderr
            assert sorted(p.name for p in prior.iterdir()) == ['sentinel']
            assert (prior/'sentinel').read_bytes() == b'kept'
    assert before == {str(path): digest(path) for path in inputs}
    (work/'result.json').write_text(json.dumps({'member': member, 'links_checked': checked,
        'first_pair_last_pair_full_rows': True, 'direct_merge_equal': True,
        'ambiguous_refused': True, 'oversized_list_refused': True,
        'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS: 67 augmentation links; sparse/full rows, exact deltas, parity, refusal')


if __name__ == '__main__':
    main()
