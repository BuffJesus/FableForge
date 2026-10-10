#!/usr/bin/env python3
"""Check archived reward object links, relocation, and malformed-list refusal."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from test_fmp_links import package_records, read_bank


MEMBERS = ('mods/1Mil Guard Drop.fmp', "mods/Archon's Battle Armor Guard Drop.fmp")


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(tool, *argv, good=True):
    result = subprocess.run([str(tool), *map(str, argv)], capture_output=True,
                            text=True, timeout=180)
    assert (result.returncode == 0) == good, (argv, result.stderr[-1800:])
    return result


def reward_payload(root):
    records = read_bank(root)
    by_name = {entry['name']: i for i, entry in enumerate(records) if entry['name']}
    data = records[by_name['REWARD_GUARD_01']]['data']
    assert data[5:9] == bytes.fromhex('112acbe0')  # Objects field tag
    count = struct.unpack_from('<I', data, 9)[0]
    assert 0 < count < 100 and len(data) == 13 + count * 8
    return records, by_name, data


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    inputs = [args.archive, args.install/'data/CompiledDefs/game.bin',
              args.install/'data/CompiledDefs/names.bin']
    before = {str(path): digest(path) for path in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-reward-links-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    checks = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        for index, member in enumerate(MEMBERS):
            raw = archive.read(member)
            assert hashlib.sha256(raw).hexdigest() == catalogue[member]['sha256']
            source = work/f'reward-{index}.fmp'
            source.write_bytes(raw)
            direct, merged = work/f'direct-{index}', work/f'merged-{index}'
            run(args.tool, 'fmp', 'apply', args.install, source, direct)
            run(args.tool, 'mods', 'merge', args.install, merged, '--with', source)
            for bank in ('game.bin', 'names.bin'):
                assert digest(direct/'data/CompiledDefs'/bank) == digest(
                    merged/'data/CompiledDefs'/bank)
            records, by_name, data = reward_payload(direct)
            names = (['OBJECT_GOLD_1000000'] if index == 0 else
                     ['OBJECT_HERO_TROUSERS_PLATINUM', 'OBJECT_HERO_SHIRT_PLATINUM',
                      'OBJECT_HERO_GLOVES_PLATINUM', 'OBJECT_HERO_HELMET_PLATINUM',
                      'OBJECT_HERO_BOOTS_PLATINUM'])
            assert struct.unpack_from('<I', data, 9)[0] == len(names)
            for slot, name in enumerate(names):
                assert struct.unpack_from('<I', data, 13 + slot * 8)[0] == by_name[name]
            checks.append({'member': member, 'links': len(names), 'direct_merge_equal': True})

        # Both archived mods replace the same REWARD_GUARD_01 list. The last
        # package in each order must supply that complete field and its links.
        first, second = work/'reward-0.fmp', work/'reward-1.fmp'
        for label, ordered, expected in (
            ('gold-then-armour', (first, second), work/'direct-1'),
            ('armour-then-gold', (second, first), work/'direct-0'),
        ):
            output = work/label
            run(args.tool, 'mods', 'merge', args.install, output, '--with', *ordered)
            assert reward_payload(output)[2] == reward_payload(expected)[2]
            checks.append({'check': label, 'last_source_wins': True})

        # Exercise a symbolic relocation even when the archive's numeric ID
        # happens to match this installed baseline.
        source = work/'reward-0.fmp'
        raw = bytearray(source.read_bytes())
        record, offset = package_records(source)['GameBINEntries']['REWARD_GUARD_01']
        assert record[5:9] == bytes.fromhex('112acbe0')
        struct.pack_into('<I', raw, offset + 13, 0)
        shifted = work/'shifted.fmp'; shifted.write_bytes(raw)
        relocated = work/'relocated'
        run(args.tool, 'fmp', 'apply', args.install, shifted, relocated)
        _, by_name, data = reward_payload(relocated)
        assert struct.unpack_from('<I', data, 13)[0] == by_name['OBJECT_GOLD_1000000']
        assert data[:13] == record[:13] and data[17:] == record[17:]
        checks.append({'check': 'symbolic-relocation-only-id-word', 'passed': True})

        malformed = bytearray(source.read_bytes())
        struct.pack_into('<I', malformed, offset + 9, 2)
        broken = work/'broken.fmp'; broken.write_bytes(malformed)
        prior = work/'prior'; prior.mkdir(); (prior/'sentinel').write_bytes(b'kept')
        result = run(args.tool, 'fmp', 'apply', args.install, broken, prior, good=False)
        assert 'reward object list layout' in result.stderr and sorted(p.name for p in prior.iterdir()) == ['sentinel']
        assert (prior/'sentinel').read_bytes() == b'kept'
        checks.append({'check': 'malformed-count-refuses-before-output', 'passed': True})
    assert before == {str(path): digest(path) for path in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS:', len(checks), 'reward link checks; input hashes unchanged')


if __name__ == '__main__':
    main()
