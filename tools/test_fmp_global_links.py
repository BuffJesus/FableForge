#!/usr/bin/env python3
"""Verify original GLOBAL model, HUD texture and teleport effect references."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zipfile

from test_fmp_links import package_records, read_bank
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run, field_start


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    mesh_file = args.install/'data/graphics/graphics.big'
    texture_file = args.install/'data/graphics/pc/textures.big'
    effect_file = args.install/'data/Misc/pc/effects.big'
    inputs = [args.archive, mesh_file, texture_file, effect_file,
              args.install/'data/CompiledDefs/game.bin', args.install/'data/CompiledDefs/names.bin']
    before = {str(path): digest(path) for path in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-global-links-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    members = ('mods/Aging Slowdown.fmp', 'mods/Fire Teleport.fmp', 'mods/Lightning Teleport.fmp')
    checks = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        for index, member in enumerate(members):
            payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            package = work/f'global-{index}.fmp'; package.write_bytes(payload)
            source, source_offset = package_records(package)['GameBINEntries']['DEFAULT_GLOBAL_DEFS']
            report = json.loads(run(args.tool, 'fmp', 'links', package, '--groups', '--json').stdout)
            groups = next(r['groups'] for r in report['records'] if r['record'] == 'DEFAULT_GLOBAL_DEFS')
            paths = {g['path']: g['targets'][0] for g in groups if len(g['targets']) == 1}
            model_paths = ('1020994168.unknown', '1267143265.unknown')
            texture_paths = ('2746605571.HUD Items', '3332655214.HUD Items',
                             '2943773648.HUD Items', '2118074272.HUD Items',
                             '1721504732.HUD Items', '3439341061.HUD Items')
            texture_paths += tuple(p for p in ('27438103.Texture', '252835944.Texture',
                '11796568.Texture', '3411202723.Texture', '74968592.Texture', '200353817.Texture') if p in paths)
            effect_paths = ('3316004677.Teleport Out', '1775054305.Teleport In')
            models = entries(mesh_file, 'MBANK_ALLMESHES', {paths[p] for p in model_paths}, include_payload=False)
            textures = entries(texture_file, 'GBANK_MAIN_PC', {paths[p] for p in texture_paths}, include_payload=False)
            effects = entries(effect_file, 'PARTICLE_MAIN_PC', {paths[p] for p in effect_paths}, include_payload=False)
            direct, merged = work/f'direct-{index}', work/f'merged-{index}'
            run(args.tool, 'fmp', 'apply', args.install, package, direct)
            run(args.tool, 'mods', 'merge', args.install, merged, '--with', package)
            for bank in ('game.bin', 'names.bin'):
                assert digest(direct/'data/CompiledDefs'/bank) == digest(merged/'data/CompiledDefs'/bank)
            built = next(e['data'] for e in read_bank(direct) if e['name'] == 'DEFAULT_GLOBAL_DEFS')
            expected = bytearray(source)
            for family, identities, member_offset in ((model_paths, models, 4),
                                                       (texture_paths, textures, 0),
                                                       (effect_paths, effects, 0)):
                for path in family:
                    tag = int(path.split('.')[0]).to_bytes(4, 'big').hex()
                    at = field_start(source, tag) + member_offset
                    value = identities[paths[path]]['id']
                    assert struct.unpack_from('<I', built, at)[0] == value
                    struct.pack_into('<I', expected, at, value)
            # Other symbolic game/name links may be relocated too. Exclude their
            # addressed words using the independently retained source markers.
            for path, target in paths.items():
                if path in model_paths + texture_paths + effect_paths or path.startswith('.'):
                    continue
                at = field_start(source, int(path.split('.')[0]).to_bytes(4, 'big').hex())
                expected[at:at+4] = built[at:at+4]
            # Component references can move as names.bin is reserialized.
            prefix = field_start(source, (4248625128).to_bytes(4, 'big').hex()) - 4
            expected[:prefix] = built[:prefix]
            assert bytes(expected) == built, 'GLOBAL bytes outside linked words changed'
            checks.append({'member': member, 'asset_links': len(model_paths + texture_paths + effect_paths),
                           'direct_merge_equal': True})
            if index == 0:
                metadata, metadata_offset = package_records(package)['GameBINLinkMetaData']['DEFAULT_GLOBAL_DEFS']
                needle = b'MESH_OBJECT_POINTER'; assert metadata.count(needle) == 1
                mutant = bytearray(payload)
                mutant[metadata_offset + metadata.index(needle)] = ord('Z')
                struct.pack_into('<I', mutant, source_offset + field_start(source, '4b871261') + 4, 0xffffffff)
                missing = work/'missing.fmp'; missing.write_bytes(mutant)
                prior = work/'prior'; prior.mkdir(); (prior/'sentinel').write_bytes(b'kept')
                for command in (('fmp', 'apply', args.install, missing, prior),
                                ('mods', 'merge', args.install, prior, '--with', missing)):
                    failed = run(args.tool, *command, good=False)
                    assert 'missing target ZESH_OBJECT_POINTER' in failed.stderr
                    assert sorted(p.name for p in prior.iterdir()) == ['sentinel']
                    assert (prior/'sentinel').read_bytes() == b'kept'
    assert before == {str(path): digest(path) for path in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'missing_refused': True,
        'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS:', sum(c['asset_links'] for c in checks),
          'GLOBAL asset links, preserved bytes, direct/composed parity and missing-target refusal')


if __name__ == '__main__':
    main()
