#!/usr/bin/env python3
"""Check that a named FMP's object graphics resolve to its composed meshes."""
import argparse
import hashlib
import json
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path

from test_fmp_texture_layers import entries


GRAPHICS = Path('data/graphics/graphics.big')
GAME = Path('data/CompiledDefs/game.bin')
PACKAGE = 'mods/Huge Mushroom.fmp'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(tool, *args):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True,
                            text=True, timeout=240)
    assert result.returncode == 0, (args, result.stderr[-2000:])
    return json.loads(result.stdout) if '--json' in args else result


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    source_hashes = {str(p): digest(p) for p in
                     (args.archive, args.install/GRAPHICS, args.install/GAME)}
    build = (repo/'build').resolve()
    with tempfile.TemporaryDirectory(prefix='fmp-named-mesh-', dir=build) as temp:
        work = Path(temp).resolve()
        assert work != build and work.is_relative_to(build)
        package = work/'Huge Mushroom.fmp'
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
            payload = archive.read(PACKAGE)
            assert hashlib.sha256(payload).hexdigest() == catalogue[PACKAGE]['sha256']
            package.write_bytes(payload)
        original_meshes = entries(package, 'graphics')
        links = run(args.tool, 'fmp', 'links', package, '--groups', '--json')
        objects = {}
        for record in links['records']:
            model = [group for group in record['groups']
                     if group['path'].endswith('.Model ID')]
            if model:
                assert len(model) == 1 and len(model[0]['targets']) == 1
                objects[record['record']] = model[0]['targets'][0]
        assert len(objects) == len(original_meshes) == 7
        assert set(objects.values()) == set(original_meshes)

        output = work/'merged'
        report = run(args.tool, 'mods', 'merge', args.install, output,
                     '--with', package, '--json')
        assert report['defs']['summary']['added'] == 7
        assert len(report['graphics']['entries']) == 7
        assert not report['graphics']['conflicts']
        assert report['asset_health']['introduced'] == []
        built_meshes = entries(output/GRAPHICS, 'MBANK_ALLMESHES', set(original_meshes))
        for name, original in original_meshes.items():
            built = built_meshes[name]
            assert (built['id'], built['type'], built['header'], built['payload']) == (
                original['id'], original['type'], original['header'], original['payload']), name
        for object_name, mesh_name in objects.items():
            definition = run(args.tool, 'defs', 'decode', output,
                             repo/'docs/re_reference/def_schema.json', object_name, '--json')
            graphic = [field for field in definition['fields'] if field['name'] == 'Graphic']
            assert len(graphic) == 1 and graphic[0]['type'] == 'CEngineGraphic'
            value = bytes.fromhex(graphic[0]['value'])
            assert len(value) == 17
            assert struct.unpack_from('<I', value, 4)[0] == built_meshes[mesh_name]['id'], (
                object_name, mesh_name)
        assert source_hashes == {str(p): digest(p) for p in
                                 (args.archive, args.install/GRAPHICS, args.install/GAME)}
    print('Huge Mushroom: seven objects point to seven exact output meshes; '
          'source files unchanged')


if __name__ == '__main__':
    main()
