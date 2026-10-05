#!/usr/bin/env python3
"""Reject named mesh textures found only outside the Info texture-ID list."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zipfile

from test_fmp_texture_layers import entries
from test_fmp_links import package_records
from test_fmp_weapon_links import run


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def asset_only(data):
    """Retain exact asset banks; omit unrelated, unresolved game-data layers.

    The originals need missing custom text names. This fixture verifies their
    mesh descriptors without claiming that the complete mods are installable.
    """
    directory = struct.unpack_from('<I', data, 8)[0]
    count = struct.unpack_from('<I', data, directory)[0]
    cursor = directory + 4; rows = []
    for _ in range(count):
        start = cursor; end = data.index(0, cursor)
        name = data[cursor:end].decode('ascii'); cursor = end + 21
        if name in ('graphics', 'graphicsLinkMetaData', 'maintextures'):
            rows.append(data[start:cursor])
    result = bytearray(data)
    struct.pack_into('<I', result, 8, len(result))
    result += struct.pack('<I', len(rows)) + b''.join(rows)
    return bytes(result)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    inputs = [args.archive, args.install/'data/graphics/graphics.big',
              args.install/'data/graphics/pc/textures.big']
    before = {str(path): digest(path) for path in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-graphics-texture-list-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    member = 'mods/Hollow Wip.fmp'
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        payload = archive.read(member)
        assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
        multiple_lods = {name: archive.read('mods/' + name)
                         for name in ('Archon Blade.fmp', 'Behemoth polearm.fmp')}
        for name, data in multiple_lods.items():
            assert hashlib.sha256(data).hexdigest() == catalogue['mods/' + name]['sha256']
    original = work/'original.fmp'; original.write_bytes(payload)
    mesh = entries(original, 'graphics', {'MESH_HERO_HOLLOW_MASK'}, include_payload=False)['MESH_HERO_HOLLOW_MASK']
    header = mesh['header']; assert len(header) == 64
    lod_count = struct.unpack_from('<I', header, 44)[0]; assert lod_count == 1
    texture_count_at = 52 + lod_count * 4
    assert struct.unpack_from('<I', header, texture_count_at)[0] == 1
    texture_id = struct.unpack_from('<I', header, texture_count_at + 4)[0]
    textures = entries(original, 'maintextures', {'HOLLOW_TEX1'}, include_payload=False)
    assert texture_id == textures['HOLLOW_TEX1']['id']
    assert payload.count(header) == 1
    header_at = payload.index(header)
    good = work/'valid'
    run(args.tool, 'fmp', 'apply', args.install, original, good)
    result = entries(good/'data/graphics/graphics.big', 'MBANK_ALLMESHES',
                     {'MESH_HERO_HOLLOW_MASK'}, include_payload=False)
    assert result['MESH_HERO_HOLLOW_MASK']['header'] == header
    cases = []
    outside = bytearray(header)
    # The old scan found this physics word and accepted the absent texture.
    struct.pack_into('<I', outside, 0, texture_id)
    struct.pack_into('<I', outside, texture_count_at + 4, 0)
    assert texture_id not in struct.unpack_from('<I', outside, texture_count_at + 4)
    cases.append(('outside-list', outside, 'its id is absent'))
    oversized = bytearray(header); struct.pack_into('<I', oversized, texture_count_at, 0xffffffff)
    cases.append(('oversized-texture-count', oversized, 'truncated mesh Info texture list'))
    lods = bytearray(header); struct.pack_into('<I', lods, 44, 0xffffffff)
    cases.append(('oversized-lod-count', lods, 'invalid mesh Info LOD count'))
    for name, changed, reason in cases:
        mutant = bytearray(payload); mutant[header_at:header_at + len(header)] = changed
        package = work/(name + '.fmp'); package.write_bytes(mutant)
        prior = work/(name + '-output'); prior.mkdir(); (prior/'sentinel').write_bytes(b'kept')
        for command in (('fmp', 'apply', args.install, package, prior),
                        ('mods', 'merge', args.install, prior, '--with', package)):
            failed = run(args.tool, *command, good=False)
            assert reason in failed.stderr, failed.stderr
            assert sorted(p.name for p in prior.iterdir()) == ['sentinel']
            assert (prior/'sentinel').read_bytes() == b'kept'
    original_mesh, mesh_at = package_records(original)['graphics']['MESH_HERO_HOLLOW_MASK']
    texture_word = struct.pack('<I', texture_id)
    assert original_mesh.count(texture_word) == 1
    wrong_slot = bytearray(payload)
    struct.pack_into('<I', wrong_slot, mesh_at + original_mesh.index(texture_word), 0)
    metadata, metadata_at = package_records(original)['graphicsLinkMetaData']['MESH_HERO_HOLLOW_MASK']
    path = b'LOD0.MAT0.BaseTexture'; assert metadata.count(path) == 1
    slot_cases = [('wrong-material-texture', wrong_slot, 'does not reference named texture')]
    for label, replacement in [('lod-out-of-range', b'LOD9.MAT0.BaseTexture'),
                               ('material-out-of-range', b'LOD0.MAT9.BaseTexture')]:
        changed = bytearray(payload)
        at = metadata_at + metadata.index(path)
        changed[at:at+len(path)] = replacement
        slot_cases.append((label, changed, 'material link index out of range'))
    for label, changed, reason in slot_cases:
        package = work/f'{label}.fmp'; package.write_bytes(changed)
        prior = work/f'{label}-output'; prior.mkdir(); (prior/'sentinel').write_bytes(b'kept')
        for command in (('fmp', 'apply', args.install, package, prior),
                        ('mods', 'merge', args.install, prior, '--with', package)):
            failed = run(args.tool, *command, good=False)
            assert reason in failed.stderr, failed.stderr
            assert sorted(p.name for p in prior.iterdir()) == ['sentinel']
            assert (prior/'sentinel').read_bytes() == b'kept'
    for index, (name, data) in enumerate(multiple_lods.items()):
        package = work/('asset-only-' + name); package.write_bytes(asset_only(data))
        original_meshes = entries(package, 'graphics', include_payload=False)
        output = work/f'multiple-lods-{index}'
        run(args.tool, 'fmp', 'apply', args.install, package, output)
        built = entries(output/'data/graphics/graphics.big', 'MBANK_ALLMESHES',
                        set(original_meshes), include_payload=False)
        for symbol, original_mesh in original_meshes.items():
            info = original_mesh['header']
            count = struct.unpack_from('<I', info, 44)[0]; assert count == 3
            # Sizes[count], safe radius, switch distances[count-1], texture count.
            texture_count_at = 48 + 4 * count + 4 + 4 * (count - 1)
            texture_count = struct.unpack_from('<I', info, texture_count_at)[0]
            assert texture_count > 0 and len(info) == texture_count_at + 4 + 4 * texture_count
            assert built[symbol]['header'] == info
    assert before == {str(path): digest(path) for path in inputs}
    (work/'result.json').write_text(json.dumps({'member': member, 'valid_header_preserved': True,
        'refused': [name for name, _, _ in cases + slot_cases], 'multiple_lods': list(multiple_lods),
        'input_hashes_unchanged': True,
        'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS: original one/three-LOD meshes and twelve descriptor/material refusal checks')


if __name__ == '__main__':
    main()
