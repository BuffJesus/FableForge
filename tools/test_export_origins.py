#!/usr/bin/env python3
"""Explicit export origins move terrain, foliage and placed objects by the same delta."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from retail_smoke import parse_glb


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='export-origins-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    def run(label, axis, options):
        output = work / (label + '.glb')
        result = subprocess.run([str(args.exe.resolve()), 'export', 'OrchardFarm',
            '--out', str(output), '--no-textures', '--foliage', '--things', '--up', axis,
            '--install', str(args.root), *options], cwd=work, capture_output=True, text=True, timeout=180)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert result.returncode == 0, result.stdout + result.stderr
        return parse_glb(output)
    def positions(doc, binary):
        accessor = doc['accessors'][doc['meshes'][0]['primitives'][0]['attributes']['POSITION']]
        view = doc['bufferViews'][accessor['bufferView']]
        return struct.unpack_from('<' + str(accessor['count'] * 3) + 'f', binary,
                                  view['byteOffset'] + accessor.get('byteOffset', 0))
    counts = {}
    for axis, delta in [('y', (16, 0, 32)), ('z', (16, -32, 0))]:
        base, base_bytes = run(axis + '_base', axis, [])
        shifted, shifted_bytes = run(axis + '_shifted', axis, ['--origin', '16,-32'])
        before, after = positions(base, base_bytes), positions(shifted, shifted_bytes)
        assert len(before) == len(after)
        assert all(abs(b - a - delta[i % 3]) < .001 for i, (a, b) in enumerate(zip(before, after)))
        for root_name in ('Foliage', 'Things'):
            roots = [n for n in base['nodes'] if n.get('name') == root_name]
            assert len(roots) == 1 and roots[0]['children'], root_name
            shifted_root = next(n for n in shifted['nodes'] if n.get('name') == root_name)
            assert len(shifted_root['children']) == len(roots[0]['children'])
            for old_index, new_index in zip(roots[0]['children'], shifted_root['children']):
                a, b = base['nodes'][old_index], shifted['nodes'][new_index]
                assert a.get('rotation') == b.get('rotation') and a.get('scale') == b.get('scale')
                assert all(abs(y - x - d) < .001 for x, y, d in zip(a['translation'], b['translation'], delta)), (axis, root_name, a['translation'], b['translation'])
            counts[axis + '_' + root_name] = len(roots[0]['children'])
        override, override_bytes = run(axis + '_world_override', axis, ['--world', '--origin', '16,-32'])
        assert override == shifted and override_bytes == shifted_bytes
    (work / 'report.json').write_text(json.dumps(counts, indent=2), encoding='utf-8')
    print('Y/Z explicit origins and world override agree for terrain, foliage and things: PASS')


if __name__ == '__main__':
    main()
