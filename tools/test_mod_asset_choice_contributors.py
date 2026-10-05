#!/usr/bin/env python3
"""Check saved texture/mesh contributors through direct merge and registered build."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from test_fmp_texture_layers import entries
from test_fmp_graphics_texture_list import asset_only
from test_mod_field_details import write_bank


def snapshot(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def bank(path, name, records):
    """Independent compact fixture writer preserving selected entry bytes."""
    raw = bytearray(b'BIGB' + struct.pack('<3I', 0, 0, 0))
    offsets = {}
    for symbol, record in records.items():
        offsets[symbol] = len(raw)
        raw += record['payload']
    start = len(raw)
    raw += struct.pack('<I', 0)
    for symbol, record in records.items():
        text = symbol.encode('ascii')
        raw += struct.pack('<7I', 0, record['id'], record['type'], len(record['payload']),
                           offsets[symbol], 0, len(text)) + text
        raw += struct.pack('<3I', 0, 0, len(record['header'])) + record['header']
    size = len(raw) - start
    directory = len(raw)
    raw += struct.pack('<I', 1) + name.encode('ascii') + b'\0'
    raw += struct.pack('<5I', 0, len(records), start, size, 1)
    struct.pack_into('<I', raw, 8, directory)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-asset-choice-contributors-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)

    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool), *map(str, arguments)], capture_output=True,
                                text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stderr)
        return result

    checks = []
    specs = [('texture', 'maintextures', 'GBANK_MAIN_PC', Path('data/graphics/pc/textures.big'),
              'TEXTURE_HERO_APPRENTICE_SHIRT_NEUTRAL',
              ('Apprentice outfit recolour.fmp', 'Apprentice Retext.fmp', 'Young Hero Retexture.fmp')),
             ('mesh', 'graphics', 'MBANK_ALLMESHES', Path('data/graphics/graphics.big'),
              'MESH_HERO_JOB_MASK_01',
              ('Half Mask (Variant 1).fmp', 'Lava JOB Mask.fmp', 'Backflip.fmp'))]
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for domain, source_bank, target_bank, relative, target, names in specs:
            root = work/domain; write_bank(root, (1, 2, 3, 4))
            sources = []
            touched = set()
            for label, name in zip(('PackA', 'PackB', 'PackC'), names):
                payload = archive.read('mods/'+name)
                assert hashlib.sha256(payload).hexdigest() == catalogue['mods/'+name]['sha256']
                path = work/(domain+'-'+label+'.fmp')
                path.write_bytes(asset_only(payload)); sources.append(path)
                touched.update(entries(path, source_bank, include_payload=False))
                run('mods', 'add', root, path, '--name', path.stem)
            # Texture metadata supplies existing symbolic IDs for mesh validation.
            # Only selected assets have real payloads; other fixture entries are inert.
            texture_path = args.install/'data/graphics/pc/textures.big'
            texture_records = entries(texture_path, 'GBANK_MAIN_PC', include_payload=False)
            for record in texture_records.values(): record['payload'] = b'\0'
            if domain == 'texture':
                texture_records.update(entries(texture_path, 'GBANK_MAIN_PC', touched))
            bank(root/'data/graphics/pc/textures.big', 'GBANK_MAIN_PC', texture_records)
            if domain == 'mesh':
                bank(root/relative, target_bank, entries(args.install/relative, target_bank, touched))
            baseline = entries(root/relative, target_bank, {target})[target]
            versions = [entries(p, source_bank, {target}) for p in sources]
            assert target in versions[0] and target in versions[1] and target not in versions[2]
            inputs = {str(p): snapshot(p) for p in [root]}
            source_hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
            picks = work/(domain+'-picks.txt')
            for route in ('merge', 'build'):
                output = work/(domain+'-'+route)
                command = ['mods', route, root, output]
                if route == 'merge': command += ['--with', *sources]
                command += ['--picks', picks]
                picks.write_text('')
                run(*command)
                (output/'sentinel').write_bytes(b'prior output')
                for winner in ('MissingMod', sources[2].stem):
                    picks.write_text(domain+':'+target+'\t'+winner+'\n')
                    before = snapshot(output)
                    failed = run(*command, good=False)
                    assert 'mods '+domain+' choice:' in failed.stderr and 'review or clear' in failed.stderr
                    assert winner in failed.stderr and snapshot(output) == before
                    checks.append({'domain': domain, 'route': route, 'winner': winner, 'preserved': True})
                for winner, expected in ((sources[0].stem, versions[0][target]), ('vanilla', baseline)):
                    picks.write_text(domain+':'+target+'\t'+winner+'\n')
                    run(*command)
                    actual = entries(output/relative, target_bank, {target})[target]
                    assert all(actual[k] == expected[k] for k in ('id', 'type', 'header', 'payload'))
                    checks.append({'domain': domain, 'route': route, 'winner': winner, 'exact': True})
                picks.write_text(domain+':UNREPORTED_ENTRY\tMissingMod\n')
                run(*command)
                actual = entries(output/relative, target_bank, {target})[target]
                assert all(actual[k] == versions[1][target][k] for k in ('id', 'type', 'header', 'payload'))
                assert (output/'sentinel').read_bytes() == b'prior output'
                checks.append({'domain': domain, 'route': route, 'inactive': True})
            assert inputs == {str(p): snapshot(p) for p in [root]}
            assert source_hashes == {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'inputs_unchanged': True}, indent=2))
    print('PASS:', len(checks), 'asset contributor and exact-winner checks')


if __name__ == '__main__':
    main()
