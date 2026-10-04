#!/usr/bin/env python3
"""Check archived BRAIN action-name links and bounded list refusal."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from test_fmp_links import crc0, package_records, read_bank


MEMBERS = ('mods/Brain Earth Troll New.fmp', 'mods/Necropolis Drunken Louts.fmp')
TAG = struct.pack('<I', crc0('Behaviours'))


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(tool, *argv, good=True):
    result = subprocess.run([str(tool), *map(str, argv)], capture_output=True,
                            text=True, timeout=180)
    assert (result.returncode == 0) == good, (argv, result.stderr[-1800:])
    return result


def name_offsets(path):
    raw = path.read_bytes()
    count = struct.unpack_from('<I', raw, 8)[0]
    result = {}
    cursor = 20
    for _ in range(count):
        end = raw.index(0, cursor + 4)
        name = raw[cursor + 4:end].decode('ascii')
        result.setdefault(name, cursor + 4 - 20)
        cursor = end + 1
    return result


def behaviour_value(data):
    assert data.count(TAG) == 1
    start = data.index(TAG) + 4
    count = struct.unpack_from('<I', data, start)[0]
    assert 0 < count < 100 and start + 4 + count * 12 <= len(data)
    return data[start:start + 4 + count * 12]


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
    work = Path(tempfile.mkdtemp(prefix='fmp-brain-links-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    checks = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        for index, member in enumerate(MEMBERS):
            payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            package = work/f'brain-{index}.fmp'; package.write_bytes(payload)
            links = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
            direct, merged = work/f'direct-{index}', work/f'merged-{index}'
            run(args.tool, 'fmp', 'apply', args.install, package, direct)
            run(args.tool, 'mods', 'merge', args.install, merged, '--with', package)
            for name in ('game.bin', 'names.bin'):
                assert digest(direct/'data/CompiledDefs'/name) == digest(
                    merged/'data/CompiledDefs'/name)
            source = package_records(package)['GameBINEntries']
            built = {entry['name']: entry['data'] for entry in read_bank(direct) if entry['name']}
            offsets = name_offsets(direct/'data/CompiledDefs/names.bin')
            checked = 0
            for record in links['records']:
                groups = [g for g in record['groups'] if
                          g['path'].startswith('3579991822.CreatureActionList.')]
                if not groups:
                    continue
                owner = record['record']
                assert owner in source and owner in built
                original = behaviour_value(source[owner][0])
                actual = behaviour_value(built[owner])
                expected = bytearray(original)
                for group in groups:
                    assert len(group['targets']) == 1
                    slot = int(group['path'].split('.')[2])
                    target = group['targets'][0]
                    assert target.startswith('SG_') and target in offsets
                    assert 4 + slot * 12 + 4 <= len(expected)
                    struct.pack_into('<I', expected, 4 + slot * 12, offsets[target])
                    checked += 1
                assert actual == expected, owner
            assert checked > 0
            checks.append({'member': member, 'action_links': checked, 'direct_merge_equal': True})

        # Corrupt only the count in one original FMP. The source action rows
        # remain unchanged and the importer must refuse before touching output.
        package = work/'brain-0.fmp'
        record, offset = package_records(package)['GameBINEntries']['BRAIN_EARTH_TROLL_NEW']
        assert record.count(TAG) == 1
        count_at = offset + record.index(TAG) + 4
        bad = bytearray(package.read_bytes())
        struct.pack_into('<I', bad, count_at, 8)
        malformed = work/'malformed.fmp'; malformed.write_bytes(bad)
        prior = work/'prior'; prior.mkdir(); (prior/'sentinel').write_bytes(b'kept')
        failed = run(args.tool, 'fmp', 'apply', args.install, malformed, prior, good=False)
        assert 'brain behaviour layout' in failed.stderr
        assert sorted(p.name for p in prior.iterdir()) == ['sentinel']
        assert (prior/'sentinel').read_bytes() == b'kept'
        checks.append({'check': 'malformed-count-refusal', 'passed': True})
    assert before == {str(path): digest(path) for path in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS:', sum(c.get('action_links', 0) for c in checks),
          'brain action links, direct/merge parity, malformed refusal')


if __name__ == '__main__':
    main()
