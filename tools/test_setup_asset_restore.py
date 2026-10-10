#!/usr/bin/env python3
"""Setup restore retires imported model/definition/texture views and restores banks."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_meshimport import write_glb, write_png
from test_setup_restore_ui import level


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    args = parser.parse_args()
    gui, cwd = args.gui.resolve(), args.cwd.resolve()
    work = Path(tempfile.mkdtemp(prefix='setup-asset-restore-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'
    graphics = args.install/'data/graphics/graphics.big'
    if not graphics.is_file(): graphics = args.install/'data/graphics/pc/graphics.big'
    copies = {name: args.install/name for name in ('data/CompiledDefs/game.bin',
        'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big')}
    copies['data/graphics/graphics.big'] = graphics
    def digest(path):
        with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()
    source_hashes = {p: digest(p) for p in copies.values()}
    for name, source in copies.items():
        target = root/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    original = {name: digest(root/name) for name in copies}
    levels = root/'data/Levels/FinalAlbion'
    levels.mkdir(parents=True)
    (levels/'AssetRestore.lev').write_bytes(level(4))
    (levels/'AssetRestore.tng').write_bytes(b'Version 2;\r\n')
    level_hashes = {p: digest(p) for p in levels.iterdir()}
    model, texture = work/'cube.glb', work/'cube.png'
    write_glb(str(model)); write_png(str(texture))
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        case.mkdir()
        result = subprocess.run([str(gui.with_name('forge.exe')), 'mesh-import', str(model),
            'SETUP_RESTORE_PROBE', '--texture', str(texture), '--install', str(root)],
            cwd=cwd, capture_output=True, text=True, timeout=240)
        (case/'import.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        assert result.returncode == 0, result.stdout+result.stderr
        assert all(digest(root/name) != value for name, value in original.items())
        assert all((root/(name+'.forge-orig')).is_file() for name in original)
        script = case/'restore.txt'
        lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}', 'assets_tab 0', 'frames 4',
            'texture_select SETUP_RESTORE_PROBE_DIFFUSE', 'frames 3',
            'assert_state texture_selected SETUP_RESTORE_PROBE_DIFFUSE',
            'reveal img_texture', 'frames 3', 'assert_widget img_texture',
            'mouse_move viewport', 'clear_toasts', f'screenshot {(case/"imported-texture.png").as_posix()}',
            'assets_tab 1', 'frames 4',
            'model_search MESH_SETUP_RESTORE_PROBE', 'frames 3', 'assert_state models_filtered 1',
            'model_select MESH_SETUP_RESTORE_PROBE', 'frames 3', 'assert_state model_ready 1',
            'assert_state model_vertices 24', 'assert_state model_triangles 12',
            'assert_state model_users 1', 'mouse_move viewport', 'clear_toasts',
            f'screenshot {(case/"imported.png").as_posix()}',
            'dump_state', 'setup 1', 'frames 3',
            'assert_state backups_differ 4', 'reveal btn_restore_all', 'frames 3',
            'click btn_restore_all', 'frames 3', 'reveal btn_restore_confirm', 'frames 3',
            'click btn_restore_confirm', 'frames 3', 'wait_maps', 'wait_ready', 'frames 3',
            'assert_state backups_differ 0', 'assert_state model_ready 0',
            'assert_state model_vertices 0', 'assert_state models_filtered 0', 'setup 0', 'frames 3',
            'mouse_move viewport', 'clear_toasts', f'screenshot {(case/"restored.png").as_posix()}',
            'assets_tab 0', 'frames 4', 'assert_state texture_selected -',
            'dump_state', 'assets_tab 1', 'frames 3',
            'model_search MESH_OBJECT_BARREL', 'frames 3', 'model_select MESH_OBJECT_BARREL',
            'frames 3', 'assert_state model_ready 1', 'assert_state model_vertices 96',
            'assert_state model_triangles 188', 'assert_state model_users 2',
            'assert_state model_selected MESH_OBJECT_BARREL', 'mouse_move viewport', 'clear_toasts',
            f'screenshot {(case/"retail-model.png").as_posix()}', 'quit']
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        result = subprocess.run([str(gui), '--install', str(root), '--size', size,
            '--auto', str(script)], cwd=cwd, capture_output=True, text=True, timeout=180,
            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                     APPDATA=str(work/'appdata'), LOCALAPPDATA=str(work/'localappdata')))
        (case/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-5000:]
        for key in ('models_count', 'textures_count'):
            counts = list(map(int, re.findall(r'\b'+key+r'=(\d+)', log)))
            assert len(counts) == 2 and counts[0] == counts[1]+1, (key, counts)
        assert all(digest(root/name) == value for name, value in original.items())
        # Setup restores the files while keeping their one-time originals.
        assert all(digest(root/(name+'.forge-orig')) == value for name, value in original.items())
        assert all(digest(p) == value for p, value in level_hashes.items())
        print(size, 'Setup buttons, retired model view, refreshed owners and exact banks: PASS', flush=True)
    assert all(digest(p) == value for p, value in source_hashes.items())
    print('Source installation unchanged: PASS', flush=True)


if __name__ == '__main__':
    main()
