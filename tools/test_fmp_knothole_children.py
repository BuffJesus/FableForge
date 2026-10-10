#!/usr/bin/env python3
"""Check a named village/creature FMP against its merged game and asset banks."""
import argparse
import hashlib
import json
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path

from test_fmp_texture_layers import entries


PACKAGE = 'mods/Knothole Glade Children.fmp'
RECORD = 'VILLAGE_KNOTHOLE_GLADE - KH_CHILDREN - CVillagePeopleDef'
PATH = '4272390883.VillagePeopleID'
GAME = Path('data/CompiledDefs/game.bin')
GRAPHICS = Path('data/graphics/graphics.big')
TEXTURES = Path('data/graphics/pc/textures.big')

CREATURE_LINKS = {
    'Weapon1ID': ('InitialWeaponDef', 8, 'game'),
    'Weapon2ID': ('InitialWeaponDef', 16, 'game'),
    'BrainID': ('RangedWeaponDef', 16, 'game'),
    'TorsoMainTextureID': ('Damage', 16, 'texture'),
    'Model ID': ('Graphic', 4, 'mesh'),
    'Model Eye ID': ('Graphic', 25, 'mesh'),
    'AttackStyleID': ('Graphic', 58, 'game'),
    'CombatType1ID': ('ResetCombatTypeOnUnsheatheDefaultWeapons', 5, 'game'),
    'CombatType2ID': ('ResetCombatTypeOnUnsheatheDefaultWeapons', 13, 'game'),
}


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def command(tool, *args, json_output=False):
    argv = [str(tool), *map(str, args)]
    if json_output: argv.append('--json')
    result = subprocess.run(argv, capture_output=True, text=True, timeout=180)
    assert result.returncode == 0, (argv, result.stderr[-2000:])
    return json.loads(result.stdout) if json_output else result.stdout


def game_index(tool, output, name):
    lines = command(tool, 'defs', 'list', output, 'game.bin', name).splitlines()
    matches = [line for line in lines if line.rstrip().endswith('  '+name)]
    assert len(matches) == 1, (name, lines)
    return int(matches[0].split()[0])


def fields(tool, schema, output, name):
    decoded = command(tool, 'defs', 'decode', output, schema, name, json_output=True)
    assert decoded['all_tags_ok'] and decoded['leftover'] == 0, name
    return decoded['fields']


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    source_hashes = {str(p): digest(p) for p in
                     (args.archive, args.install/GAME, args.install/GRAPHICS,
                      args.install/TEXTURES)}
    with tempfile.TemporaryDirectory(prefix='fmp-knothole-children-', dir=repo/'build') as temp:
        work = Path(temp).resolve()
        assert work != (repo/'build').resolve() and work.is_relative_to((repo/'build').resolve())
        with zipfile.ZipFile(args.archive) as archive:
            catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
            payload = archive.read(PACKAGE)
            assert hashlib.sha256(payload).hexdigest() == catalogue[PACKAGE]['sha256']
            package = work/'knothole_children.fmp'
            package.write_bytes(payload)
        groups = subprocess.run([str(args.tool), 'fmp', 'links', str(package), '--groups', '--json'],
                                capture_output=True, text=True, timeout=30)
        assert groups.returncode == 0, groups.stderr
        grouped = next(row for row in json.loads(groups.stdout)['records'] if row['record'] == RECORD)
        pair = [group for group in grouped['groups'] if group['path'] == PATH]
        assert len(pair) == 1 and pair[0]['targets'] == ['NULLDEF_CHeroMorphDef'] * 2
        links = subprocess.run([str(args.tool), 'fmp', 'links', str(package), '--json'],
                               capture_output=True, text=True, timeout=30)
        assert links.returncode == 0, links.stderr
        record = next(row for row in json.loads(links.stdout)['records'] if row['record'] == RECORD)
        expanded = [link for link in record['links'] if link['path'].startswith(PATH)]
        assert expanded == [{'path': PATH+'.0', 'target': 'NULLDEF_CHeroMorphDef'},
                            {'path': PATH+'.1', 'target': 'NULLDEF_CHeroMorphDef'}]
        output = work/'merged'
        report = command(args.tool, 'mods', 'merge', args.install, output,
                         '--with', package, json_output=True)
        assert report['defs']['summary']['added'] == 3
        assert report['defs']['summary']['applied'] == 4
        assert not report['defs']['conflicts']
        assert report['asset_health']['introduced'] == []
        graphic_bank = output/GRAPHICS if (output/GRAPHICS).exists() else args.install/GRAPHICS
        texture_bank = output/TEXTURES if (output/TEXTURES).exists() else args.install/TEXTURES
        schema = repo/'docs/re_reference/def_schema.json'
        village = fields(args.tool, schema, output, RECORD)
        teachers = [field for field in village if field['name'] == 'MaleTeacher']
        assert len(teachers) == 2
        null_index = game_index(args.tool, output, 'NULLDEF_CHeroMorphDef')
        assert all(int(field['value']) == null_index for field in teachers)
        for gender, name in (('MaleChild', 'CREATURE_KH_VILLAGER_BOY'),
                             ('FemaleChild', 'CREATURE_KH_VILLAGER_GIRL')):
            child = [field for field in village if field['name'] == gender]
            assert len(child) == 1 and int(child[0]['value']) == game_index(args.tool, output, name)
        group_rows = json.loads(groups.stdout)['records']
        creature_rows = [row for row in group_rows if row['record'] in
                         ('CREATURE_KH_VILLAGER_BOY', 'CREATURE_KH_VILLAGER_GIRL')]
        assert len(creature_rows) == 2
        mesh_targets = {group['targets'][0] for row in creature_rows for group in row['groups']
                        if group['path'].endswith(('.Model ID', '.Model Eye ID'))}
        texture_targets = {group['targets'][0] for row in creature_rows for group in row['groups']
                           if group['path'].endswith('.TorsoMainTextureID')}
        meshes = entries(graphic_bank, 'MBANK_ALLMESHES', mesh_targets, False)
        textures = entries(texture_bank, 'GBANK_MAIN_PC', texture_targets, False)
        assert set(meshes) == mesh_targets and set(textures) == texture_targets
        checked = 0
        for row in creature_rows:
            decoded = {field['name']: bytes.fromhex(field['value'])
                       for field in fields(args.tool, schema, output, row['record'])}
            for group in row['groups']:
                suffix = group['path'].split('.', 1)[-1]
                if suffix not in CREATURE_LINKS: continue
                assert len(group['targets']) == 1
                field, offset, kind = CREATURE_LINKS[suffix]
                actual = struct.unpack_from('<I', decoded[field], offset)[0]
                target = group['targets'][0]
                expected = (game_index(args.tool, output, target) if kind == 'game' else
                            meshes[target]['id'] if kind == 'mesh' else textures[target]['id'])
                assert actual == expected, (row['record'], suffix, actual, expected)
                checked += 1
        assert checked == 2 * len(CREATURE_LINKS), checked
        village_graphic = next(field for field in fields(args.tool, schema, output,
                              'VILLAGE_KNOTHOLE_GLADE') if field['name'] == 'Graphic')
        marker = entries(graphic_bank, 'MBANK_ALLMESHES', {'MESH_MARKER_VILLAGE'}, False)
        assert struct.unpack_from('<I', bytes.fromhex(village_graphic['value']), 4)[0] == (
            marker['MESH_MARKER_VILLAGE']['id'])
    assert source_hashes == {str(p): digest(p) for p in
                             (args.archive, args.install/GAME, args.install/GRAPHICS,
                              args.install/TEXTURES)}
    print('Knothole Glade Children: four definitions merged; ordered village links, '
          '18 creature references and village marker match output identities')


if __name__ == '__main__':
    main()
