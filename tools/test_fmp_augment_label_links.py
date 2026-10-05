#!/usr/bin/env python3
"""Original Augments text/icon scalar links preserve surrounding bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import shutil
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
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    inputs = [args.archive, args.tool] + [args.install/p for p in (
        'data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
        'data/lang/English/text.big', 'data/graphics/pc/textures.big',
        'data/graphics/graphics.big')]
    before = {str(p): digest(p) for p in inputs}
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fmp-augment-label-links-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    # A staged source has different current/original baselines between direct
    # apply and composition. Give both routes the same real copied file bytes.
    baseline = work/'baseline'
    for source in inputs[2:]:
        target = baseline/source.relative_to(args.install)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    with zipfile.ZipFile(args.archive) as archive:
        member = 'mods/Augments.fmp'
        raw = archive.read(member)
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        assert hashlib.sha256(raw).hexdigest() == catalogue[member]['sha256']
    package = work/'original.fmp'; package.write_bytes(raw)
    records = package_records(package)
    targets = {
        'DiamondAug-AugDef': ('TEXT_OBJECT_AUGMENTATION_DIAMOND_TITLE', 'HUD_ICON_CHUNK_CRYSTAL'),
        'KnowledgeAug-AugDef': ('TEXT_AUGMENTATION_KNOWLEDGE_TITLE', 'HUD_ORB_AUGMENTATION_ICE')}
    textures = entries(args.install/'data/graphics/pc/textures.big', 'GBANK_MAIN_PC',
                       {v[1] for v in targets.values()}, include_payload=False)
    direct, merged = work/'direct', work/'merged'
    run(args.tool, 'fmp', 'apply', baseline, package, direct)
    run(args.tool, 'mods', 'merge', baseline, merged, '--with', package)
    for relative in ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                     'data/lang/English/text.big'):
        assert digest(direct/relative) == digest(merged/relative)
    text = entries(direct/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN',
                   {v[0] for v in targets.values()}, include_payload=False)
    built = {r['name']: r for r in read_bank(direct) if r['name']}
    for name, (title, icon) in targets.items():
        source = records['GameBINEntries'][name][0]
        assert len(source) == 75 and source[3:7] == bytes.fromhex('240e55df')
        assert source[19:23] == bytes.fromhex('b6f76cb1')
        expected = bytearray(source)
        struct.pack_into('<I', expected, 7, text[title]['id'])
        struct.pack_into('<I', expected, 23, textures[icon]['id'])
        assert built[name]['definition'] == 'CAugmentationDef'
        assert built[name]['data'] == expected
    refusals = []
    for label, old, new in (
        ('unverified-path', b'GUIDescription1ID', b'GUIDescription9ID'),
        ('missing-text', b'TEXT_OBJECT_AUGMENTATION_DIAMOND_TITLE', b'X'*len(b'TEXT_OBJECT_AUGMENTATION_DIAMOND_TITLE')),
        ('missing-texture', b'HUD_ICON_CHUNK_CRYSTAL', b'X'*len(b'HUD_ICON_CHUNK_CRYSTAL'))):
        bad = bytearray(raw)
        metadata, offset = records['GameBINLinkMetaData']['DiamondAug-AugDef']
        assert metadata.count(old) == 1 and len(old) == len(new)
        bad[offset:offset+len(metadata)] = metadata.replace(old, new)
        fixture = work/(label+'.fmp'); fixture.write_bytes(bad)
        for route, output in (('apply', direct), ('merge', merged)):
            saved = snapshot(output)
            command = ('fmp', 'apply', baseline, fixture, output) if route == 'apply' else (
                'mods', 'merge', baseline, output, '--with', fixture)
            failure = run(args.tool, *command, good=False)
            assert 'FMP links:' in failure.stderr and snapshot(output) == saved
            refusals.append({'case': label, 'route': route, 'reason': failure.stderr.strip()})
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'file': member, 'scalar_links': 4,
        'refusals': refusals, 'inputs_unchanged': True, 'gameplay_tested': False}, indent=2))
    print('PASS: four original augmentation scalar links, exact surrounding bytes, parity and six safe refusals')


if __name__ == '__main__':
    main()
