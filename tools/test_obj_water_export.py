#!/usr/bin/env python3
"""Retail OBJ water keeps terrain material and later foliage indices correct."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image
import trimesh


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='obj-water-export-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    output = work / 'water_foliage.obj'
    result = subprocess.run([str(args.exe.resolve()), 'export', 'TeleporterGreatwood',
        '--out', str(output), '--foliage', '--things', '--max-texture', '64', '--install', str(args.root)],
        cwd=work, capture_output=True, text=True, timeout=240)
    (work / 'export.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    positions = uvs = normals = water_positions = appended_faces = 0
    group = ''
    for line in output.read_text(encoding='utf-8').splitlines():
        if line.startswith('o '):
            group = line[2:]
        elif line.startswith('v '):
            positions += 1
            water_positions += group == 'Water'
        elif line.startswith('vt '):
            uvs += 1
        elif line.startswith('vn '):
            normals += 1
        elif line.startswith('f '):
            for token in line.split()[1:]:
                indices = [int(value) for value in token.split('/')]
                assert 0 < indices[0] <= positions
                if len(indices) == 3:
                    assert 0 < indices[1] <= uvs and 0 < indices[2] <= normals
                    if group in ('water_foliage_Foliage', 'water_foliage_Things'):
                        assert indices[0] - indices[1] == water_positions and indices[1] == indices[2], token
            appended_faces += group in ('water_foliage_Foliage', 'water_foliage_Things')
    assert water_positions > 0 and appended_faces > 0
    material = ''
    terrain_textured = False
    for line in output.with_suffix('.mtl').read_text(encoding='utf-8').splitlines():
        if line.startswith('newmtl '):
            material = line[7:]
        elif line.startswith('map_Kd '):
            name = line[7:]
            if name == 'water_foliage_albedo.png':
                assert material == 'terrain', material
                terrain_textured = True
            with Image.open(work / name) as image:
                image.verify()
    assert terrain_textured
    scene = trimesh.load(output, force='scene')
    assert len(scene.geometry) > 1
    (work / 'report.json').write_text(json.dumps({'positions': positions, 'uvs': uvs,
        'normals': normals, 'water_positions': water_positions, 'appended_faces': appended_faces,
        'terrain_textured': terrain_textured, 'loaded_geometries': len(scene.geometry)}, indent=2), encoding='utf-8')
    print('Retail OBJ water/material/layer indices and independent loading: PASS')


if __name__ == '__main__':
    main()
