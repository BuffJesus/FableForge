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
from test_fmp_weapon_links import run


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
    assert before == {str(path): digest(path) for path in inputs}
    (work/'result.json').write_text(json.dumps({'member': member, 'valid_header_preserved': True,
        'refused': [name for name, _, _ in cases], 'input_hashes_unchanged': True,
        'gameplay_tested': False}, indent=2), encoding='utf-8')
    print('PASS: real mesh texture list and six bounded/safe refusal checks')


if __name__ == '__main__':
    main()
