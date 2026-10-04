#!/usr/bin/env python3
"""Verify summon brain links to existing and newly appended names.bin entries."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from test_fmp_links import crc0, package_records, read_bank
from test_fmp_brain_behaviour_links import name_offsets


MEMBERS = ('mods/Ghost Sword - Summoner.fmp', 'mods/Summoner summon.fmp',
           'mods/Troll Summon.fmp')
TAG = struct.pack('<I', crc0('SummonedBrain'))


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(tool, *argv, good=True):
    result = subprocess.run([str(tool), *map(str, argv)], capture_output=True,
                            text=True, timeout=180)
    assert (result.returncode == 0) == good, (argv, result.stderr[-1800:])
    return result


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
    work = Path(tempfile.mkdtemp(prefix='fmp-summon-brain-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    checks = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        packages = []
        for index, member in enumerate(MEMBERS):
            payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            package = work/f'summon-{index}.fmp'; package.write_bytes(payload)
            packages.append(package)
            links = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
            direct, merged = work/f'direct-{index}', work/f'merged-{index}'
            run(args.tool, 'fmp', 'apply', args.install, package, direct)
            run(args.tool, 'mods', 'merge', args.install, merged, '--with', package)
            for name in ('game.bin', 'names.bin'):
                assert digest(direct/'data/CompiledDefs'/name) == digest(
                    merged/'data/CompiledDefs'/name)
            original = package_records(package)['GameBINEntries']
            result = {entry['name']: entry['data'] for entry in read_bank(direct) if entry['name']}
            offsets = name_offsets(direct/'data/CompiledDefs/names.bin')
            linked = 0
            for record in links['records']:
                groups = [g for g in record['groups'] if g['path'] == '3463808271.Brain To Use']
                if not groups:
                    continue
                owner = record['record']
                assert len(groups) == 1 and len(groups[0]['targets']) == 1
                target = groups[0]['targets'][0]
                assert target in offsets and owner in original and owner in result
                source = original[owner][0]
                built = result[owner]
                assert source.count(TAG) == built.count(TAG) == 1
                at = source.index(TAG) + 4
                assert len(source) == len(built) and built.index(TAG) == at - 4
                expected = bytearray(source)
                struct.pack_into('<I', expected, at, offsets[target])
                assert built == expected, owner
                linked += 1
            assert linked > 0
            checks.append({'member': member, 'links': linked, 'direct_merge_equal': True})

        # Change only a metadata target string, leaving the package's new brain
        # record intact. Neither build route should write into prior output.
        package = packages[1]
        owner = 'CSummonableCreatureDef_SUMMONER'
        payload, offset = package_records(package)['GameBINLinkMetaData'][owner]
        target = b'BRAIN_summoned_SUMMONER'
        assert payload.count(target) == 1
        mutant = bytearray(package.read_bytes())
        mutant[offset + payload.index(target)] = ord('Z')
        missing = work/'missing.fmp'; missing.write_bytes(mutant)
        prior = work/'prior'; prior.mkdir(); (prior/'sentinel').write_bytes(b'kept')
        for command in (('fmp', 'apply', args.install, missing, prior),
                        ('mods', 'merge', args.install, prior, '--with', missing)):
            failed = run(args.tool, *command, good=False)
            assert 'missing target ZRAIN_summoned_SUMMONER' in failed.stderr
            assert sorted(p.name for p in prior.iterdir()) == ['sentinel']
            assert (prior/'sentinel').read_bytes() == b'kept'
        checks.append({'check': 'missing-new-name-refusal', 'passed': True})
    assert before == {str(path): digest(path) for path in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS:', sum(c.get('links', 0) for c in checks),
          'summon brain links; existing/new names and refusal verified')


if __name__ == '__main__':
    main()
