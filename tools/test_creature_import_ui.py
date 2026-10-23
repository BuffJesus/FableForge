#!/usr/bin/env python3
"""Exercise custom Creature import controls using tiny retained donor fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
from test_definition_edit_ui import click
from test_fmp_links import read_bank, package_records


def inventory(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def main():
    repo = Path(__file__).resolve().parents[1]
    driver_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--forge', type=Path, default=repo/'build/forge.exe')
    parser.add_argument('--fixtures', type=Path, default=os.environ.get('FABLEFORGE_CREATURE_FIXTURES'),
                        help='Retained tiny donor packet from test_skinned_input_donors.py')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    modes = ('save', 'stored', 'cancel', 'missing-slot', 'existing', 'image', 'image-dxt3', 'image-argb8888', 'pack', 'pack-preview', 'pack-image-preview', 'pack-existing-preview', 'close-review', 'stale-model', 'stale-sidecar', 'stale-image', 'stale-bank', 'stale-names', 'stale-graphics', 'stale-textures', 'stale-manifest', 'stale-model-alias', 'stale-destination-alias', 'destination', 'changed-draft')
    parser.add_argument('--only', nargs='+', choices=modes)
    args = parser.parse_args()
    if args.fixtures is None:
        print('SKIP custom Creature GUI: supply --fixtures or FABLEFORGE_CREATURE_FIXTURES with a retained tiny donor packet')
        return
    fixtures = args.fixtures.resolve(strict=True)
    assert json.loads((fixtures/'result.json').read_text())['passed'], 'donor fixture packet has no passed receipt'
    source = fixtures/'4521/source-install'
    before = inventory(fixtures)
    originals = read_bank(source)
    work = Path(tempfile.mkdtemp(prefix='creature-import-ui-', dir=args.work_root.resolve()))
    print('Evidence retained at', work, flush=True)
    # A byte-identical private runtime lets synthetic fixtures supply their own
    # schema through cwd, even when the supplied executable has bundled retail docs.
    # Never replace the source/package schema or write into its extracted tree.
    runtime = work/'runtime'
    runtime.mkdir()
    gui_source = args.gui.resolve(strict=True)
    gui = runtime/gui_source.name
    shutil.copyfile(gui_source, gui)
    gui_hash = hashlib.sha256(gui_source.read_bytes()).hexdigest()
    assert hashlib.sha256(gui.read_bytes()).hexdigest() == gui_hash
    for dll in gui_source.parent.glob('*.dll'):
        shutil.copyfile(dll, runtime/dll.name)
    cases = []
    try:
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            for mode in args.only or modes:
                has_image = mode.startswith('image') or mode in ('stale-image', 'pack-image-preview')
                preview = mode in ('pack-preview', 'pack-image-preview', 'pack-existing-preview')
                existing_texture = mode in ('existing', 'pack-existing-preview')
                case = work/(size+'-'+mode)
                case.mkdir()
                install = case/'install'
                shutil.copytree(source, install)
                levels = install/'data/Levels/FinalAlbion'
                levels.mkdir(parents=True)
                (levels/'unused.lev').write_bytes(b'')
                inputs = case/'inputs'
                inputs.mkdir()
                for name in ('source.gltf', 'source.bin', 'diffuse.png'):
                    shutil.copyfile(fixtures/'4521'/name, inputs/name)
                cwd = case/'cwd'
                schema = cwd/'docs/re_reference/def_schema.json'
                schema.parent.mkdir(parents=True)
                shutil.copyfile(fixtures/'4521/schema.json', schema)
                output = case/'output'
                pack = preview or mode in ('pack', 'stale-manifest')
                save_folder = case/'separate-save-folder' if pack else output
                manifest = None
                if pack:
                    output.mkdir()
                    manifest = output/'forge_pack.json'
                    manifest.write_text(json.dumps({'version': 1, 'name': 'Creature UI'}))
                original_manifest = manifest.read_bytes() if manifest else None
                lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}',
                         f'set saveroot {save_folder.as_posix()}']
                if pack:
                    lines += [f'pack_dest {output.as_posix()}']
                if preview:
                    lines += ['assets_tab 1', 'model_select 4521', 'assert_state model_ready 1']
                lines += ['assets_tab 6', 'wait_definitions',
                         'definition_select 1', 'frames 3']
                lines += click('btn_definition_clone_creature_private')
                for widget, text in [('input_definition_edit_value', 'GUI_ROOT'),
                                     ('input_definition_clone_body', 'GUI_BODY'),
                                     ('input_definition_clone_appearance', 'GUI_APPEARANCE')]:
                    lines += click(widget)+['input_text '+text, 'frames 3']
                lines += click('check_definition_creature_mesh')
                if mode != 'missing-slot':
                    lines += click('combo_definition_creature_slot')+click('definition_creature_slot_0')
                for widget, text in [('input_definition_creature_model', str(inputs/'source.gltf')),
                                     ('input_definition_creature_mesh_name', 'MESH_GUI_MODEL')]:
                    lines += click(widget)+['input_text '+text, 'frames 3']
                if existing_texture:
                    lines += click('input_definition_creature_texture_id')+['input_text 7', 'frames 3']
                if has_image:
                    lines += click('input_definition_creature_texture')+['input_text '+str(inputs/'diffuse.png'), 'frames 3']
                    lines += click('input_definition_creature_texture_name')+['input_text GUI_TEXTURE', 'frames 3']
                    if mode in ('image-dxt3', 'image-argb8888'):
                        lines += click('combo_definition_creature_format')+click('definition_creature_format_'+('1' if mode=='image-dxt3' else '2'))
                if mode == 'stored':
                    lines += click('check_definition_creature_stored')
                lines += [f'screenshot {(case/"draft.png").as_posix()}']
                if mode == 'close-review':
                    lines += ['close', 'frames 3', 'assert_state close_prompt 1', 'click btn_unsaved_cancel', 'frames 3',
                              'close', 'frames 3', 'click btn_unsaved_save', 'frames 3', 'assert_state close_prompt 0', 'assert_state definition_editing 1']
                if mode == 'cancel':
                    lines += click('btn_definition_edit_cancel')+['assert_state definition_editing 0']
                else:
                    lines += click('btn_definition_save')+['wait_definitions', 'frames 3']
                    if mode == 'missing-slot':
                        lines += ['assert_state definition_edit_failed 1', 'assert_state definition_editing 1', 'definition_cancel']
                    else:
                        lines += ['assert_state definition_edit_failed 0', f'screenshot {(case/"review.png").as_posix()}']
                        if mode.startswith('stale-'):
                            lines += [f'screenshot {(case/"barrier.png").as_posix()}', 'frames 300']
                        if mode == 'destination':
                            lines += [f'set saveroot {(case/"other-output").as_posix()}', 'wait_definitions', 'frames 3', 'definition_save_refused', 'assert_state definition_editing 1', 'definition_cancel']
                        if mode == 'changed-draft':
                            lines += click('input_definition_creature_mesh_name')+['input_text MESH_CHANGED', 'frames 3', 'definition_save_refused', 'assert_state definition_editing 1', 'definition_cancel']
                        elif mode != 'destination':
                            lines += click('btn_definition_save_confirm')+['wait_ready', 'wait_definitions', 'frames 3']
                            if mode.startswith('stale-') or mode == 'destination':
                                lines += ['assert_state definition_edit_failed 1', 'assert_state definition_editing 1', 'definition_cancel']
                            else:
                                lines += ['assert_state definition_edit_failed 0', 'assert_state definition_editing 0',
                                    'assert_state definition_selected 6', f'screenshot {(case/"created.png").as_posix()}']
                                if preview:
                                    lines += ['assets_tab 1', 'model_select 4522', 'assert_state model_selected MESH_GUI_MODEL',
                                              'assert_state model_ready 1', 'assert_state model_vertices 3', 'assert_state model_triangles 1',
                                              f'screenshot {(case/"model-preview.png").as_posix()}']
                                    lines += [f'assert_state model_textured_parts {1 if has_image or existing_texture else 0}']
                                    lines += [f'assert_state asset_context_root {install.as_posix()}']
                                    if mode == 'pack-image-preview':
                                        lines += click('btn_model_refresh')+['model_select 4522', 'assert_state model_textured_parts 1',
                                            f'assert_state asset_context_root {install.as_posix()}',
                                            f'set saveroot {install.as_posix()}', 'frames 3', 'model_search MESH_GUI_MODEL', 'frames 3',
                                            'assert_state models_filtered 0', 'model_select 4521', 'assert_state model_ready 1',
                                            f'assert_state asset_context_root {install.as_posix()}']
                lines += ['quit']
                script = case/'import.txt'
                script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
                with (case/'output.log').open('w', encoding='utf-8') as log:
                    process = subprocess.Popen([str(gui), '--install', str(install), '--size', size, '--auto', str(script)],
                        cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                        env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(case/'profile'), LOCALAPPDATA=str(case/'local')))
                    try:
                        if mode.startswith('stale-'):
                            deadline = time.monotonic()+60
                            while not (case/'barrier.png').exists() and process.poll() is None and time.monotonic()<deadline:
                                time.sleep(.02)
                            assert (case/'barrier.png').exists(), 'review barrier not reached'
                            targets = {'stale-model': inputs/'source.gltf', 'stale-sidecar': inputs/'source.bin', 'stale-image': inputs/'diffuse.png',
                                       'stale-bank': install/'data/CompiledDefs/game.bin', 'stale-names': install/'data/CompiledDefs/names.bin',
                                       'stale-graphics': install/'data/graphics/graphics.big', 'stale-textures': install/'data/graphics/pc/textures.big',
                                       'stale-manifest': manifest}
                            if mode in ('stale-model-alias', 'stale-destination-alias'):
                                if os.name != 'nt':
                                    raise AssertionError('junction acceptance requires Windows')
                                import _winapi
                                if mode == 'stale-model-alias':
                                    duplicate = case/'duplicate-inputs'
                                    shutil.copytree(inputs, duplicate)
                                    moved = case/'original-inputs'
                                    assert inputs.resolve().parent == case.resolve() and moved.parent == case
                                    inputs.rename(moved)
                                    _winapi.CreateJunction(str(duplicate), str(inputs))
                                else:
                                    duplicate = case/'other-output'
                                    duplicate.mkdir()
                                    _winapi.CreateJunction(str(duplicate), str(output))
                            else:
                                target = targets[mode]
                                with target.open('ab') as stream:
                                    stream.write(b' ')
                        returncode = process.wait(timeout=180)
                    finally:
                        if process.poll() is None:
                            process.terminate()
                            process.wait(timeout=10)
                trace = Path(str(script)+'.log').read_text(encoding='utf-8')
                assert returncode == 0 and 'RESULT PASS' in trace and 'FAIL ' not in trace, trace[-8000:]
                if preview or mode in ('save', 'stored', 'existing', 'image', 'image-dxt3', 'image-argb8888', 'pack', 'close-review'):
                    actual = read_bank(output)
                    assert actual[:6] == originals and len(actual) == 9
                    old_bank = (source/'data/CompiledDefs/game.bin').read_bytes()
                    new_bank = (output/'data/CompiledDefs/game.bin').read_bytes()
                    assert new_bank[13:13+6*12] == old_bank[13:13+6*12]
                    old_names = (source/'data/CompiledDefs/names.bin').read_bytes()
                    new_names = (output/'data/CompiledDefs/names.bin').read_bytes()
                    assert new_names[:8] == old_names[:8] and new_names[16:len(old_names)] == old_names[16:]
                    expected = [bytearray(originals[i]['data']) for i in (1, 3, 4)]
                    for offset, value in ((9, 7), (13, 6), (21, 8), (25, 6), (53, 6)):
                        struct.pack_into('<I', expected[0], offset, value)
                    struct.pack_into('<I', expected[1], 27, 4522)
                    for payload in expected[1:]:
                        struct.pack_into('<I', payload, len(payload)-12, 6)
                    for i, name in enumerate(('GUI_ROOT', 'GUI_BODY', 'GUI_APPEARANCE')):
                        assert actual[6+i]['name'] == name and actual[6+i]['data'] == expected[i]
                    graphics = package_records(output/'data/graphics/graphics.big', by_id=True)
                    original_graphics = package_records(source/'data/graphics/graphics.big', by_id=True)
                    assert graphics['MBANK_ALLMESHES'][4521] == original_graphics['MBANK_ALLMESHES'][4521]
                    assert set(graphics['MBANK_ALLMESHES']) == {4521, 4522}
                    texture_output = output/'data/graphics/pc/textures.big'
                    assert texture_output.exists() == has_image
                    textures = package_records(texture_output if texture_output.exists() else source/'data/graphics/pc/textures.big', by_id=True)
                    original_textures = package_records(source/'data/graphics/pc/textures.big', by_id=True)
                    for bank, entries in original_textures.items():
                        for index, entry in entries.items():
                            assert textures[bank][index] == entry
                    assert set(textures['GBANK_MAIN_PC']) == (set(original_textures['GBANK_MAIN_PC']) | ({8} if has_image else set()))
                    restored = subprocess.run([str(args.forge.resolve()), 'restore', '--install', str(output)], capture_output=True, text=True, timeout=60)
                    assert restored.returncode == 0, restored.stdout+restored.stderr
                    assert not list(output.rglob('*.bin')) and not list(output.rglob('*.big'))
                    assert not list(output.rglob('*.forge-created'))
                else:
                    assert not list(output.rglob('*.bin')) and not list(output.rglob('*.big')), 'refusal/cancel published outputs'
                    assert not list(output.rglob('*.forge-*')), 'refusal created recovery metadata'
                    if not pack and mode != 'stale-destination-alias':
                        assert not output.exists()
                    if mode == 'stale-destination-alias':
                        assert not list((case/'other-output').iterdir())
                    else:
                        assert not (case/'other-output').exists()
                if manifest:
                    assert manifest.read_bytes() == original_manifest+(b' ' if mode == 'stale-manifest' else b'')
                    assert not save_folder.exists(), 'pack import wrote to the independent save folder'
                cases.append({'size': size, 'mode': mode})
                print(size, mode, 'PASS', flush=True)
    finally:
        assert inventory(fixtures) == before, 'fixture sources changed'
    assert hashlib.sha256(gui_source.read_bytes()).hexdigest() == gui_hash
    assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest() == driver_hash, 'test driver changed during the run'
    (work/'result.json').write_text(json.dumps({'passed': True, 'cases': cases, 'gui_sha256': gui_hash,
        'driver_sha256': driver_hash,
        'schema_source': 'tiny donor fixture', 'runtime': 'byte-identical executable and adjacent DLL copies'}, indent=2)+'\n')


if __name__ == '__main__':
    main()
