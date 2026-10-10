#!/usr/bin/env python3
"""Check original wound/expression prefix links and preserved creature bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile
from test_fmp_links import package_records, read_bank
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run
from test_mod_file_choice_contributors import snapshot


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--members', nargs='+', default=[
        'mods/No Scar Fade.fmp', 'mods/Bruised and Bloodied (No Scars).fmp'])
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fmp-creature-prefix-links-', dir=args.work_root)).resolve()
    assert work.parent == args.work_root.resolve()
    print('Evidence retained at', work, flush=True)
    relatives = ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/lang/English/text.big', 'data/graphics/pc/textures.big',
                 'data/graphics/graphics.big', 'data/Misc/pc/effects.big')
    inputs = [args.archive, args.tool, *(args.install/p for p in relatives)]
    before = {str(p): digest(p) for p in inputs}
    baseline = work/'baseline'
    for relative in relatives:
        target = baseline/relative; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.install/relative, target)
    checked = []
    refusals = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for index, member in enumerate(args.members):
            raw = archive.read(member)
            assert hashlib.sha256(raw).hexdigest() == catalogue[member]['sha256']
            case = work/str(index); case.mkdir()
            package = case/'original.fmp'; package.write_bytes(raw)
            records = package_records(package)
            report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
            row = next(r for r in report['records'] if any(
                g['path'].startswith(('4294151720.', '4042377857.')) for g in r['groups']))
            owner = row['record']; groups = row['groups']
            source, offset = records['GameBINEntries'][owner]
            assert source[3:7] == bytes.fromhex('fff38e28')
            wounds = struct.unpack_from('<I', source, 7)[0]
            assert wounds in (0, 25)
            random_at = 11+wounds*44+4
            assert source[random_at-4:random_at] == bytes.fromhex('ee6cab01')
            assert source[random_at:random_at+64] == bytes.fromhex('237dcbe3')+struct.pack('<I', 3)+bytes(56)
            expression_at = random_at+64
            assert source[expression_at:expression_at+4] == bytes.fromhex('f0f1ca81')
            expressions = struct.unpack_from('<I', source, expression_at+4)[0]
            assert expressions in (1, 4)
            expression_end = expression_at+8+expressions*12
            assert source[expression_end:expression_end+8] == bytes.fromhex('326dab01b3bfa450')
            textures = entries(baseline/'data/graphics/pc/textures.big', 'GBANK_MAIN_PC',
                {g['targets'][0] for g in groups if g['path'].startswith('4294151720.')}, include_payload=False)
            animations = entries(baseline/'data/graphics/graphics.big', 'MBANK_ALLMESHES',
                {g['targets'][0] for g in groups if g['path'].startswith('4042377857.')}, include_payload=False)
            direct, merged = case/'direct', case/'merged'
            run(args.tool, 'fmp', 'apply', baseline, package, direct)
            run(args.tool, 'mods', 'merge', baseline, merged, '--with', package)
            for relative in relatives[:2]:
                assert digest(direct/relative) == digest(merged/relative)
            built = read_bank(direct)
            game_ids = {r['name']: i for i, r in enumerate(built) if r['name']}
            output = next(r for r in built if r['name'] == owner) if owner else built[10532]
            assert output['definition'] == 'CCreatureDef'
            expected = bytearray(source); prefix_links = 0
            for g in groups:
                parts = g['path'].split('.'); assert len(g['targets']) == 1
                target = g['targets'][0]
                if parts[0] == '4294151720':
                    slot = 0 if parts[3] == 'Link to ID' else int(parts[3][-1])
                    assert 0 <= slot < 6 and 0 <= int(parts[2]) < wounds
                    at = 11+int(parts[2])*44+8+slot*4; value = textures[target]['id']; prefix_links += 1
                elif parts[0] == '4042377857':
                    assert parts[1] == 'unknown' and parts[3] == 'Animations'
                    assert 0 <= int(parts[2]) < expressions
                    at = expression_at+8+int(parts[2])*12+8; value = animations[target]['id']; prefix_links += 1
                else:
                    tag = struct.pack('>I', int(parts[0])); assert source.count(tag) == 1
                    at = source.index(tag)+4; value = game_ids[target]
                    if len(parts) == 4:
                        assert parts[0] == '521674895'
                        assert int(parts[2]) < struct.unpack_from('<I', source, at)[0]
                        at += 4+int(parts[2])*4
                    else: assert len(parts) == 2
                struct.pack_into('<I', expected, at, value)
            assert output['data'] == expected
            checked.append({'file': member, 'prefix_links': prefix_links, 'all_creature_links': len(groups)})
            print(member, prefix_links, 'prefix links, all creature bytes and bank parity: PASS', flush=True)
            mutations = []
            for label, at, value in [('wound-count', 7, 0xffffffff),
                    ('body-count', random_at+4, 0xffffffff), ('model-count', random_at+8, 0xffffffff),
                    ('expression-count', expression_at+4, 0xffffffff),
                    ('late-expression-marker', expression_end, 0)]:
                bad = bytearray(raw); struct.pack_into('<I', bad, offset+at, value)
                mutations.append((label, bad))
            metadata, metadata_offset = records['GameBINLinkMetaData'][owner]
            missing_skin = next((g['targets'][0] for g in groups if g['path'].startswith('4294151720.')), None)
            missing_animation = next(g['targets'][0] for g in groups if g['path'].startswith('4042377857.'))
            metadata_mutations = [
                ('animation-index', b'4042377857.unknown.0.', b'4042377857.unknown.9.'),
                ('missing-animation', missing_animation.encode(), b'X'*len(missing_animation))]
            if missing_skin:
                metadata_mutations += [('skin-index', b'Skin Effects Array.24.', b'Skin Effects Array.99.'),
                    ('skin-member', b'Link to ID5', b'Link to ID6'),
                    ('missing-skin', missing_skin.encode(), b'X'*len(missing_skin))]
            for label, old, new in metadata_mutations:
                assert old in metadata and len(old) == len(new)
                bad = bytearray(raw); bad[metadata_offset:metadata_offset+len(metadata)] = metadata.replace(old, new)
                mutations.append((label, bad))
            saved = {route: snapshot(output) for route, output in [('apply', direct), ('merge', merged)]}
            for label, bad in mutations:
                fixture = case/(label+'.fmp'); fixture.write_bytes(bad)
                for route, output in [('apply', direct), ('merge', merged)]:
                    command = ('fmp', 'apply', baseline, fixture, output) if route == 'apply' else (
                        'mods', 'merge', baseline, output, '--with', fixture)
                    failure = run(args.tool, *command, good=False)
                    assert 'FMP links:' in failure.stderr and snapshot(output) == saved[route]
                    refusals.append({'file': member, 'case': label, 'route': route,
                                     'reason': failure.stderr.strip()})
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'originals': checked, 'refusals': refusals,
        'inputs_sha256': before, 'inputs_unchanged': True, 'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS:', len(checked), 'originals, exact creature bytes, bank parity and',
          len(refusals), 'safe refusals')


if __name__ == '__main__':
    main()
