#!/usr/bin/env python3
"""Original opinion reaction vectors retain attitudes while relinking names/text."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile
from test_fmp_links import package_records, read_bank, crc0
from test_fmp_brain_behaviour_links import name_offsets
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run
from test_mod_file_choice_contributors import snapshot


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def slots(raw):
    tag = bytes.fromhex('c8ba8338')
    assert raw.count(tag) == 1
    start = raw.index(tag) + 4
    count = struct.unpack_from('<I', raw, start)[0]
    assert 0 <= count <= 128
    cursor = start + 4; rows = []
    for _ in range(count):
        assert struct.unpack_from('<I', raw, cursor)[0] == crc0('Attitudes')
        attitudes = struct.unpack_from('<I', raw, cursor+4)[0]
        assert attitudes <= 32
        attitude_count = cursor + 4
        cursor += 8 + attitudes*4
        assert struct.unpack_from('<I', raw, cursor)[0] == crc0('Animation')
        assert struct.unpack_from('<I', raw, cursor+8)[0] == crc0('DialogueTag')
        rows.append({'Animation': cursor+4, 'Text': cursor+12, 'attitude_count': attitude_count})
        cursor += 16
    return start, rows


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    inputs = [args.archive, args.tool] + [args.install/p for p in (
        'data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/lang/English/text.big')]
    before = {str(p): digest(p) for p in inputs}
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fmp-opinion-reaction-links-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    baseline = work/'baseline'
    for source in inputs[2:]:
        target = baseline/source.relative_to(args.install)
        target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, target)
    with zipfile.ZipFile(args.archive) as archive:
        member = 'mods/Getting Away With Murder Mod.fmp'; raw = archive.read(member)
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        assert hashlib.sha256(raw).hexdigest() == catalogue[member]['sha256']
    package = work/'original.fmp'; package.write_bytes(raw)
    records = package_records(package)
    metadata = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
    assert len(metadata['records']) == 13
    groups = {r['record']: r['groups'] for r in metadata['records']}
    direct, merged = work/'direct', work/'merged'
    run(args.tool, 'fmp', 'apply', baseline, package, direct)
    run(args.tool, 'mods', 'merge', baseline, merged, '--with', package)
    for relative in ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin'):
        assert digest(direct/relative) == digest(merged/relative)
    names = name_offsets(direct/'data/CompiledDefs/names.bin')
    wanted_text = {g['targets'][0] for gs in groups.values() for g in gs if g['path'].endswith('.Text')}
    text = entries(baseline/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN', wanted_text, include_payload=False)
    built = {r['name']: r for r in read_bank(direct) if r['name']}
    checked = 0
    for owner, gs in groups.items():
        source = records['GameBINEntries'][owner][0]; _, rows = slots(source)
        expected = bytearray(source)
        for g in gs:
            parts = g['path'].split('.')
            assert len(parts) == 4 and parts[:2] == ['3367666488', 'unknown'] and len(g['targets']) == 1
            index = int(parts[2]); domain = parts[3]; target = g['targets'][0]
            value = names[target] if domain == 'Animation' else text[target]['id']
            struct.pack_into('<I', expected, rows[index][domain], value); checked += 1
        assert built[owner]['definition'] == 'OPINION_DEED_EFFECTS' and built[owner]['data'] == expected
    assert checked == 306
    owner = 'OPINION_DEED_EFFECTS_CRIME_ASSAULT'
    source, offset = records['GameBINEntries'][owner]; start, rows = slots(source)
    mutations = [('reaction-count', start, 0xffffffff),
                 ('attitude-count', rows[-1]['attitude_count'], 0xffffffff),
                 ('attitude-tag', rows[-1]['attitude_count']-4, 0),
                 ('animation-tag', rows[-1]['Animation']-4, 0),
                 ('dialogue-tag', rows[-1]['Text']-4, 0)]
    refusals = []
    for label, at, value in mutations:
        bad = bytearray(raw); struct.pack_into('<I', bad, offset+at, value)
        fixture = work/(label+'.fmp'); fixture.write_bytes(bad)
        for route, output in (('apply', direct), ('merge', merged)):
            saved = snapshot(output)
            command = ('fmp', 'apply', baseline, fixture, output) if route == 'apply' else (
                'mods', 'merge', baseline, output, '--with', fixture)
            failure = run(args.tool, *command, good=False)
            assert 'FMP links:' in failure.stderr and snapshot(output) == saved
            refusals.append({'case': label, 'route': route, 'reason': failure.stderr.strip()})
    owner_metadata, metadata_offset = records['GameBINLinkMetaData'][owner]
    for label, domain in (('missing-animation', 'Animation'), ('missing-dialogue', 'Text')):
        group = next(g for g in groups[owner] if g['path'].endswith('.'+domain))
        target = group['targets'][0].encode('ascii')
        assert target in owner_metadata
        bad = bytearray(raw)
        bad[metadata_offset:metadata_offset+len(owner_metadata)] = owner_metadata.replace(target, b'X'*len(target))
        fixture = work/(label+'.fmp'); fixture.write_bytes(bad)
        for route, output in (('apply', direct), ('merge', merged)):
            saved = snapshot(output)
            command = ('fmp', 'apply', baseline, fixture, output) if route == 'apply' else (
                'mods', 'merge', baseline, output, '--with', fixture)
            failure = run(args.tool, *command, good=False)
            assert 'FMP links:' in failure.stderr and 'missing target' in failure.stderr and snapshot(output) == saved
            refusals.append({'case': label, 'route': route, 'reason': failure.stderr.strip()})
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'file': member, 'links_checked': checked,
        'records_checked': 13, 'refusals': refusals, 'inputs_sha256': before,
        'inputs_unchanged': True, 'gameplay_tested': False}, indent=2))
    print('PASS: 306 original opinion links, exact attitudes/other bytes, parity and fourteen safe refusals')


if __name__ == '__main__':
    main()
