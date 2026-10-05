#!/usr/bin/env python3
"""Verify original texture replacement pairs and bounded safe refusal."""
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
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fmp-texture-replacement-links-', dir=args.work_root)).resolve()
    assert work.parent == args.work_root.resolve()
    print('Evidence retained at', work, flush=True)
    relatives = ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/lang/English/text.big', 'data/graphics/pc/textures.big',
                 'data/graphics/graphics.big')
    inputs = [args.archive, args.tool, *(args.install/p for p in relatives)]
    before = {str(p): digest(p) for p in inputs}
    baseline = work/'baseline'
    for relative in relatives:
        target = baseline/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.install/relative, target)
    checked = []
    refusals = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for index, member in enumerate(('mods/Plate Helmet.fmp', 'mods/Prophet 01 Upper Robe.fmp')):
            raw = archive.read(member)
            assert hashlib.sha256(raw).hexdigest() == catalogue[member]['sha256']
            case = work/str(index); case.mkdir()
            package = case/'original.fmp'; package.write_bytes(raw)
            records = package_records(package)
            report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
            replacement = [r for r in report['records'] if any(
                g['path'].startswith('659892607.') for g in r['groups'])]
            assert len(replacement) == 1
            owner = replacement[0]['record']
            links = [g for g in replacement[0]['groups'] if g['path'].startswith('659892607.')]
            assert len(links) == 2 and all(len(g['targets']) == 1 for g in links)
            texture_names = {g['targets'][0] for g in links}
            source, source_offset = records['GameBINEntries'][owner]
            assert len(source) == 19 and source[3:7] == bytes.fromhex('2755297f')
            assert struct.unpack_from('<I', source, 7)[0] == 1
            direct, merged = case/'direct', case/'merged'
            run(args.tool, 'fmp', 'apply', baseline, package, direct)
            run(args.tool, 'mods', 'merge', baseline, merged, '--with', package)
            for relative in relatives:
                if (direct/relative).exists() or (merged/relative).exists():
                    assert digest(direct/relative) == digest(merged/relative), relative
            bank_path = direct/'data/graphics/pc/textures.big'
            if not bank_path.exists(): bank_path = baseline/'data/graphics/pc/textures.big'
            textures = entries(bank_path, 'GBANK_MAIN_PC', texture_names, include_payload=False)
            built = {r['name']: r for r in read_bank(direct) if r['name']}
            expected = bytearray(source)
            for link in links:
                suffix = link['path'].split('.')[-1]
                assert suffix in ('Texture To Replace', 'Replacement Texture')
                at = 11 if suffix == 'Texture To Replace' else 15
                struct.pack_into('<I', expected, at, textures[link['targets'][0]]['id'])
            assert built[owner]['definition'] == 'CTextureReplacementDef'
            assert built[owner]['data'] == expected
            checked.append({'file': member, 'owner': owner, 'links': 2})
            print(member, 'exact replacement words and direct/composed parity: PASS', flush=True)
            saved_outputs = {route: snapshot(output) for route, output in [('apply', direct), ('merge', merged)]}
            mutations = []
            for label, position, value in [('count-too-large', 7, 0xffffffff), ('count-mismatch', 7, 2)]:
                bad = bytearray(raw); struct.pack_into('<I', bad, source_offset+position, value)
                mutations.append((label, bad))
            metadata, metadata_offset = records['GameBINLinkMetaData'][owner]
            for label, old, new in [('unverified-path', b'Texture To Replace', b'Texture To ReplacX'),
                ('index-out-of-range', b'CTextureReplacementDef_List.0.', b'CTextureReplacementDef_List.9.'),
                ('missing-texture', links[0]['targets'][0].encode('ascii'),
                    b'X'*len(links[0]['targets'][0]))]:
                assert len(old) == len(new) and old in metadata
                bad = bytearray(raw)
                bad[metadata_offset:metadata_offset+len(metadata)] = metadata.replace(old, new)
                mutations.append((label, bad))
            for label, bad in mutations:
                fixture = case/(label+'.fmp'); fixture.write_bytes(bad)
                for route, output in [('apply', direct), ('merge', merged)]:
                    command = ('fmp', 'apply', baseline, fixture, output) if route == 'apply' else (
                        'mods', 'merge', baseline, output, '--with', fixture)
                    failure = run(args.tool, *command, good=False)
                    assert 'FMP links:' in failure.stderr and snapshot(output) == saved_outputs[route]
                    refusals.append({'file': member, 'case': label, 'route': route,
                                     'reason': failure.stderr.strip()})
                print(member, label, 'both routes preserve prior output: PASS', flush=True)
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'links_checked': 4, 'originals': checked,
        'refusals': refusals, 'inputs_sha256': before, 'inputs_unchanged': True,
        'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS: four original replacement links, exact other bytes, bank parity and twenty safe refusals')


if __name__ == '__main__':
    main()
