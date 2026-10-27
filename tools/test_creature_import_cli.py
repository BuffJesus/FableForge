#!/usr/bin/env python3
"""Public CLI acceptance using retained tiny native-donor fixtures, never game writes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from test_fmp_links import package_records, read_bank


def inventory(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--forge', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--oracle-root', type=Path, required=True)
    args = parser.parse_args()
    forge = args.forge.resolve(strict=True)
    fixtures = args.fixtures.resolve(strict=True)
    assert json.loads((fixtures/'result.json').read_text())['passed']
    sys.path.insert(0, str(args.oracle_root.resolve()/'tools/blender_addon/io_scene_fable'))
    import mesh_rw
    fc = mesh_rw.fc
    before = inventory(fixtures)
    checks = []
    refusals = []
    # New outputs are tiny banks only; retain logs/manifest, Restore removes data.
    work = Path(tempfile.mkdtemp(prefix='creature-cli-', dir=fixtures.parent))
    print('Evidence retained at', work, flush=True)

    def run(command, label):
        result = subprocess.run([str(forge), *map(str, command)], cwd=work,
                                capture_output=True, text=True, timeout=120)
        (work/(label+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        return result

    for donor in (4521, 5121, 5150, 3985, 5110):
        folder = fixtures/str(donor)
        source = folder/'source-install'
        originals = read_bank(source)
        assert len(originals) == 6
        old_gfx = package_records(source/'data/graphics/graphics.big', by_id=True)
        old_tex = package_records(source/'data/graphics/pc/textures.big', by_id=True)
        source_buf, source_entries = fc.open_graphics_big(str(source/'data/graphics/graphics.big'))
        source_entry = source_entries[0]
        raw = source_buf[source_entry['offset']:source_entry['offset']+source_entry['size']]
        skeleton = mesh_rw.clone_skeleton(raw, 5)
        for mode in ('none', 'existing', 'dxt1', 'dxt3', 'argb8888', 'inplace'):
            label = f'{donor}-{mode}'
            output = work/(label+' output % &')
            if mode == 'inplace':
                shutil.copytree(source, output)
                baseline = inventory(output)
            name = 'CLI_'+str(donor)+'_'+mode.upper()
            command = ['creature-import', folder/'source.gltf', name, '--donor-index', '1',
                       '--body', '0', '--alternative', '0', '--schema', folder/'schema.json',
                       '--install', output if mode == 'inplace' else source, '--out', output, '--json']
            texture_id = 0
            if mode == 'existing':
                command += ['--texture-id', '7']
                texture_id = 7
            elif mode not in ('none', 'existing'):
                command += ['--texture', folder/'diffuse.png', '--texture-format',
                            'argb8888' if mode == 'inplace' else mode]
                texture_id = 8
            if mode in ('none', 'dxt3'):
                command += ['--stored']
            result = run(command, label)
            assert result.returncode == 0, result.stdout+result.stderr
            report = json.loads(result.stdout)
            for key, value in {'root_index': 6, 'body_index': 7, 'appearance_index': 8,
                               'donor_model_id': donor, 'mesh_id': donor+1, 'texture_id': texture_id,
                               'other_body_slots_shared': True, 'animations_shared': True,
                               'runtime_compatibility_verified': False}.items():
                assert report[key] == value, (label, key, report)
            actual = read_bank(output)
            assert len(actual) == 9 and actual[:6] == originals
            expected_root = bytearray(originals[1]['data'])
            for offset, value in ((9,7), (13,6), (21,8), (25,6), (53,6)):
                struct.pack_into('<I', expected_root, offset, value)
            expected_body = bytearray(originals[3]['data'])
            struct.pack_into('<I', expected_body, 27, donor+1)
            struct.pack_into('<I', expected_body, len(expected_body)-12, 6)
            expected_appearance = bytearray(originals[4]['data'])
            struct.pack_into('<I', expected_appearance, len(expected_appearance)-12, 6)
            for index, definition, new_name, payload in (
                    (6, 'CREATURE', 'CREATURE_'+name, expected_root),
                    (7, 'CCreatureDef', 'FORGE_'+name+'_BODY', expected_body),
                    (8, 'CAppearanceDef', 'FORGE_'+name+'_APPEARANCE', expected_appearance)):
                assert actual[index] == {'definition': definition, 'name': new_name, 'data': payload}
            old_identity = (source/'data/CompiledDefs/game.bin').read_bytes()
            new_identity = (output/'data/CompiledDefs/game.bin').read_bytes()
            assert new_identity[13:13+6*12] == old_identity[13:13+6*12]
            old_names = (source/'data/CompiledDefs/names.bin').read_bytes()
            new_names = (output/'data/CompiledDefs/names.bin').read_bytes()
            assert new_names[:8] == old_names[:8] and new_names[16:len(old_names)] == old_names[16:]
            gfx_path = output/'data/graphics/graphics.big'
            published = package_records(gfx_path, by_id=True)
            assert set(published) == set(old_gfx)
            assert set(published['MBANK_ALLMESHES']) == {donor, donor+1}
            assert published['MBANK_ALLMESHES'][donor][0] == old_gfx['MBANK_ALLMESHES'][donor][0]
            buf, entries = fc.open_graphics_big(str(gfx_path))
            entry = next(e for e in entries if e['id'] == donor+1)
            payload = buf[entry['offset']:entry['offset']+entry['size']]
            lod_size = struct.unpack_from('<I', entry['info'], 48)[0]
            current = mesh_rw.clone_skeleton(payload, 5)
            ghost = mesh_rw.clone_skeleton(payload[lod_size:], 5)
            for key in ('indices_raw','names_raw','bones_raw','keyframes_raw','ibms_raw'):
                assert current[key] == ghost[key] == skeleton[key]
            _, prims, _ = fc._parse_mesh_primitives(payload, 5)
            vertices, faces, _ = fc._prim_geometry(prims[0], 5)
            assert len(prims) == 1 and faces == [(0,1,2)]
            assert all(abs(a-b)<1e-6 for v,w in zip(vertices, [(0,0,0),(1,0,0),(0,1,0)]) for a,b in zip(v,w))
            skin = fc._prim_skin(prims[0], skeleton['count'])
            assert dict(skin[1]) == {2:1.0} and dict(skin[2]) == {1:1.0}
            assert all(abs(dict(skin[0])[i]-weight)<=1/255 for i,weight in enumerate((.5,.3,.2)))
            texture_path = output/'data/graphics/pc/textures.big'
            if texture_id == 8:
                published_tex = package_records(texture_path, by_id=True)
                assert published_tex['GBANK_MAIN_PC'][7][0] == old_tex['GBANK_MAIN_PC'][7][0]
                assert set(published_tex['GBANK_MAIN_PC']) == {7,8}
            else:
                assert not texture_path.exists()
            texture_bank = fc.TextureBank([str(texture_path if texture_id == 8 else source/'data/graphics/pc/textures.big')])
            if texture_id == 8:
                expected_fmt = {'dxt1':0x1f, 'dxt3':0x20, 'argb8888':0x01, 'inplace':0x01}[mode]
                for tex_id in (7,8):
                    tex_buf, _, tex_entry, _ = texture_bank._by_id[tex_id]
                    desc = fc.parse_texture.parse_info(tex_entry['info'])
                    if tex_id == 8:
                        assert desc['fmt'] == expected_fmt
                    assert desc['alloc_w'] == desc['alloc_h'] == 4
                    pay = tex_buf[tex_entry['offset']:tex_entry['offset']+tex_entry['size']]
                    raw_size = fc._base_mip_raw_len(desc['fmt'],4,4)
                    if desc['first_block_field']:
                        pixels_raw, cursor = fc._lzo_fable(pay,0,raw_size)
                        assert cursor == desc['first_block_field']
                    else:
                        pixels_raw = pay[:raw_size]
                    assert len(pixels_raw) == raw_size
                    pixels = fc.rgba_to_blender(fc.parse_texture.decode_mip(desc['fmt'],pixels_raw,4,4))
                    row = [0,0,1,1]*4 if tex_id == 7 else [1,0,0,1]*2+[0,1,0,1]*2
                    assert all(abs(value-row[i%16])<.01 for i,value in enumerate(pixels))
            mesh_model = fc.build_model(buf, entry, texture_bank, decode_textures=False)
            # The descriptor lists referenced nonzero texture IDs, not all
            # geometry materials. An untextured material has no descriptor row.
            assert [m.base_tex_id for m in mesh_model.materials] == ([texture_id] if texture_id else [])
            restore = run(['restore', '--install', output], label+'-restore')
            assert restore.returncode == 0, restore.stdout+restore.stderr
            if mode == 'inplace':
                # Restore keeps one-time originals; authored files must be exact.
                assert all(inventory(output)[path] == value for path,value in baseline.items())
            else:
                assert not any(p.is_file() for p in output.rglob('*'))
            checks.append(label)
        for flag, value, diagnostic in (
                ('--body','99','absent body part or alternative'),
                ('--alternative','99','absent body part or alternative'),
                ('--donor-index','2','CREATURE'),
                ('--texture-id','99','texture'),
                ('model',str(folder/'mismatched.gltf'),'donor skin mapping')):
            label = f'{donor}-refuse-{flag.removeprefix("--")}'
            output = work/(label+' output')
            command = ['creature-import', folder/'source.gltf', 'REFUSAL', '--donor-index','1',
                       '--body','0','--alternative','0','--schema',folder/'schema.json',
                       '--install',source,'--out',output,'--json']
            if flag == 'model':
                command[1] = value
            elif flag == '--texture-id':
                command += [flag,value]
            else:
                command[command.index(flag)+1] = value
            result = run(command,label)
            assert result.returncode != 0 and diagnostic.lower() in (result.stdout+result.stderr).lower(), result.stderr
            assert not output.exists(), 'refusal created output tree'
            refusals.append(label)
        print(donor, 'six CLI import/Restore variants: PASS', flush=True)
    assert inventory(fixtures) == before, 'retained source packet changed'
    (work/'result.json').write_text(json.dumps({'passed': True, 'checks': checks, 'refusals': refusals,
        'source_packet': str(fixtures), 'source_inventory_unchanged': True}, indent=2)+'\n', encoding='utf-8')
    print('PASS: 25 fresh and five in-place CLI publications/Restore, 25 prewrite refusals, independent graph/mesh/pixel preservation', flush=True)


if __name__ == '__main__':
    main()
