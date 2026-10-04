#!/usr/bin/env python3
"""Verify archived appearance mesh links and bounded list refusal."""
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


MEMBERS = ('mods/Berserk Horns.fmp', 'mods/Will Baldness.fmp')
GRAPHICS = Path('data/graphics/graphics.big')
TAG = bytes.fromhex('67019d5c')  # CAppearanceModifierDef.Graphics


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(tool, *argv, good=True):
    result = subprocess.run([str(tool), *map(str, argv)], capture_output=True,
                            text=True, timeout=180)
    assert (result.returncode == 0) == good, (argv, result.stderr[-1800:])
    return result


def graphics_value(record):
    assert record.count(TAG) == 1
    start = record.index(TAG) + 4
    count = struct.unpack_from('<I', record, start)[0]
    assert 0 < count <= 16 and start + 4 + count * 24 <= len(record)
    return record[start:start + 4 + count * 24]


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    inputs = [args.archive, args.install/GRAPHICS,
              args.install/'data/CompiledDefs/game.bin',
              args.install/'data/CompiledDefs/names.bin']
    before = {str(path): digest(path) for path in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-appearance-links-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    cases = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        packages = []
        for i, member in enumerate(MEMBERS):
            payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            package = work/f'appearance-{i}.fmp'; package.write_bytes(payload)
            packages.append(package)

        linked = []
        for package in packages:
            report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
            owners = {}
            for record in report['records']:
                model_links = [g for g in record['groups'] if
                               g['path'].startswith('1728159068.Appearance Clothing Model List.')]
                if model_links:
                    assert record['record'] and all(len(g['targets']) == 1 for g in model_links)
                    owners[record['record']] = model_links
                    linked.extend(g['targets'][0] for g in model_links)
            assert owners
            output = work/(package.stem + '-direct')
            merged = work/(package.stem + '-merged')
            run(args.tool, 'fmp', 'apply', args.install, package, output)
            run(args.tool, 'mods', 'merge', args.install, merged, '--with', package)
            for bank in ('game.bin', 'names.bin'):
                assert digest(output/'data/CompiledDefs'/bank) == digest(merged/'data/CompiledDefs'/bank)
            cases.append((package, output, owners))

        meshes = entries(args.install/GRAPHICS, 'MBANK_ALLMESHES', set(linked),
                         include_payload=False)
        assert set(linked) <= set(meshes)
        checked = 0
        for package, output, owners in cases:
            source = package_records(package)['GameBINEntries']
            built = {entry['name']: entry['data'] for entry in read_bank(output) if entry['name']}
            for owner, groups in owners.items():
                original = graphics_value(source[owner][0])
                result = graphics_value(built[owner])
                assert len(result) == len(original)
                expected = bytearray(original)
                for group in groups:
                    slot = int(group['path'].split('.')[2])
                    offset = 4 + slot * 24 + 8
                    assert offset + 4 <= len(expected)
                    struct.pack_into('<I', expected, offset, meshes[group['targets'][0]]['id'])
                    checked += 1
                assert result == expected, owner  # only the named mesh ID words may move

        # A one-entry archived package provides a compact malformed-list probe.
        package = packages[0]
        links = cases[0][2]
        owner = next(iter(links))
        raw, offset = package_records(package)['GameBINEntries'][owner]
        assert raw.count(TAG) == 1
        malformed = bytearray(package.read_bytes())
        struct.pack_into('<I', malformed, offset + raw.index(TAG) + 4, 2)
        broken = work/'broken.fmp'; broken.write_bytes(malformed)
        prior = work/'prior'; prior.mkdir(); (prior/'sentinel').write_bytes(b'kept')
        failed = run(args.tool, 'fmp', 'apply', args.install, broken, prior, good=False)
        assert 'appearance graphics layout' in failed.stderr
        assert sorted(p.name for p in prior.iterdir()) == ['sentinel']
        assert (prior/'sentinel').read_bytes() == b'kept'

        missing_member = 'mods/Assassin Mask Fix.fmp'
        missing_raw = archive.read(missing_member)
        assert hashlib.sha256(missing_raw).hexdigest() == catalogue[missing_member]['sha256']
        missing = work/'missing-mesh.fmp'; missing.write_bytes(missing_raw)
        for command in (('fmp', 'apply', args.install, missing, prior),
                        ('mods', 'merge', args.install, prior, '--with', missing)):
            failed = run(args.tool, *command, good=False)
            assert 'FMP links:' in failed.stderr
            if command[0] == 'fmp':
                assert 'missing target MESH_HERO_ASSASSIN_MASK_01' in failed.stderr
            assert sorted(p.name for p in prior.iterdir()) == ['sentinel']
            assert (prior/'sentinel').read_bytes() == b'kept'

    assert before == {str(path): digest(path) for path in inputs}
    (work/'result.json').write_text(json.dumps({'members': MEMBERS, 'links_checked': checked,
                                                 'malformed_refused': True,
                                                 'missing_mesh_refused': True,
                                                 'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS:', checked, 'appearance mesh links, direct/merge parity, malformed refusal')


if __name__ == '__main__':
    main()
