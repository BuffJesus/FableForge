"""Use model import forms on copied banks and packs, preserving a draft entered during a job."""
import argparse
import os
from pathlib import Path
import json
import shutil
import subprocess
import tempfile

from test_meshimport import pristine, write_obj, write_png


def fill(widget, value):
    return ['reveal '+widget, 'frames 2', 'click '+widget,
            'key_down Ctrl', 'key_down A', 'key_up A', 'key_up Ctrl',
            'input_text '+str(value), 'frames 2']


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='model-import-form-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)

    def run(dest, lines, size):
        script = dest/'flow.txt'
        script.write_text('\n'.join(lines+['quit', '']))
        result = subprocess.run([str(repo/'build/FableForge.exe'), '--install', str(args.install),
            '--auto', str(script), '--size', size], cwd=repo, capture_output=True, text=True,
            timeout=180, env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        log = Path(str(script)+'.log').read_text()
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]

    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work/size
        root = dest/'install'
        (root/'data/CompiledDefs').mkdir(parents=True)
        for name in ('game.bin', 'names.bin'):
            shutil.copyfile(args.install/'data/CompiledDefs'/name, root/'data/CompiledDefs'/name)
        obj, png = dest/'my cube.obj', dest/'my texture.png'
        write_obj(obj); write_png(png)
        lines = ['wait_maps', 'wait_ready', f'set saveroot {root}', f'set uiscale {scale}',
                 'assets_tab 1', 'frames 3', 'click btn_model_import', 'frames 3']
        lines += fill('input_mesh_model', dest/'missing.obj') + fill('input_mesh_name', 'cube test')
        lines += fill('input_mesh_texture', png) + fill('input_new_pack', 'Preview Pack')
        lines += ['reveal btn_new_pack', 'click btn_new_pack', 'frames 3', 'assert_state mods_count 1',
                  'reveal btn_mesh_import', 'click btn_mesh_import', 'frames 3',
                  'assert_state mesh_import_failed 1']
        lines += fill('input_mesh_model', obj)
        lines += ['assets_tab 3', 'frames 2', 'assets_tab 1', 'frames 3', 'click btn_model_import',
                  'frames 3', 'reveal btn_mesh_import', 'click btn_mesh_import', 'frames 3',
                  'assert_state mesh_import_failed 0', 'reveal mesh_import_success', 'frames 3',
                  'assert_widget mesh_import_success', 'assert_state mesh_import_name ',
                  'screenshot '+(dest/'added.png').as_posix()]
        run(dest, lines, size)
        pack = root/'FableForgeMods/Preview_Pack'
        manifest = json.loads((pack/'forge_pack.json').read_text())
        model = manifest['models'][0]
        assert len(manifest['models']) == 1 and model['name'] == 'CUBE_TEST'
        assert (pack/model['model']).read_bytes() == obj.read_bytes()
        assert (pack/model['texture']).read_bytes() == png.read_bytes()
        for name in ('game.bin', 'names.bin'):
            assert (root/'data/CompiledDefs'/name).read_bytes() == (args.install/'data/CompiledDefs'/name).read_bytes()
        print(size, 'pack form failure/retry, exact assets and visible completion: PASS', flush=True)

    dest = work/'pending'
    root = dest/'install'
    for relative in ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                     'data/graphics/graphics.big', 'data/graphics/pc/textures.big']:
        target = root/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(pristine(str(args.install), relative), target)
    obj = dest/'cube.obj'; write_obj(obj)
    lines = ['wait_maps', 'wait_ready', f'set saveroot {root}', 'set uiscale 1.5',
             'assets_tab 1', 'frames 3', 'click btn_model_import', 'frames 3']
    lines += fill('input_mesh_model', obj) + fill('input_mesh_name', 'firstcube')
    lines += ['reveal btn_mesh_import', 'click btn_mesh_import', 'assert_state mesh_import_busy 1']
    lines += fill('input_mesh_name', 'nextcube')
    # Commit the edited name by moving focus before Escape closes the window.
    # Escape in the name field itself would legitimately cancel its text edit.
    lines += ['reveal input_mesh_texture', 'click input_mesh_texture',
              'assert_state mesh_import_name nextcube', 'assert_state mesh_import_busy 1',
              'key_down Escape', 'key_up Escape', 'wait_mesh_import', 'wait_ready', 'frames 3',
              'assert_state mesh_import_failed 0', 'assert_state mesh_import_name nextcube',
              'click btn_model_import', 'frames 3', 'reveal mesh_import_success', 'frames 3',
              'screenshot '+(dest/'preserved.png').as_posix(),
              'reveal btn_mesh_import', 'click btn_mesh_import', 'wait_mesh_import', 'wait_ready',
              'frames 3', 'assert_state mesh_import_failed 0', 'assert_state mesh_import_name ',
              'assert_log OBJECT_NEXTCUBE ready', 'reveal mesh_import_success', 'frames 3',
              'screenshot '+(dest/'second.png').as_posix()]
    run(dest, lines, '800x600')
    print('Pending draft preserved, imported next, unchanged completed form cleared: PASS', flush=True)


if __name__ == '__main__':
    main()
