#!/usr/bin/env python3
"""Loaded model and texture views follow Mods deploy/undeploy at the save root."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_meshimport import write_glb, write_png
from test_setup_restore_ui import level


def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    parser.add_argument('--separate-root', action='store_true')
    args = parser.parse_args()
    gui, cwd = args.gui.resolve(), args.cwd.resolve()
    work = Path(tempfile.mkdtemp(prefix='mod-asset-refresh-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    copies = {name: args.install/name for name in ['data/CompiledDefs/game.bin',
        'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']}
    graphics = args.install/'data/graphics/graphics.big'
    if not graphics.is_file(): graphics = args.install/'data/graphics/pc/graphics.big'
    copies['data/graphics/graphics.big'] = graphics
    source_hashes = {p: digest(p) for p in copies.values()}
    selected = work/'selected-install'
    root = work/'save-root' if args.separate_root else selected
    for folder in dict.fromkeys([selected, root]):
        for name, source in copies.items():
            target = folder/name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        levels = folder/'data/Levels/FinalAlbion'
        levels.mkdir(parents=True)
        (levels/'AssetRefresh.lev').write_bytes(level(4))
        (levels/'AssetRefresh.tng').write_bytes(b'Version 2;\r\n')
    selected_hashes = {p: digest(p) for p in selected.rglob('*') if p.is_file()}
    baseline = {name: digest(root/name) for name in copies}
    level_hashes = {p: digest(p) for p in (root/'data/Levels/FinalAlbion').iterdir()}
    pack = work/'pack'
    pack.mkdir()
    write_glb(str(pack/'cube.glb')); write_png(str(pack/'cube.png'))
    (pack/'forge_pack.json').write_text(json.dumps({'version': 1, 'name': 'AssetRefresh',
        'models': [{'name': 'MOD_ASSET_REFRESH', 'model': 'cube.glb', 'texture': 'cube.png',
                    'donor': 'OBJECT_BARREL_UNBREAKABLE', 'collision': True}]}), encoding='utf-8')
    added = subprocess.run([str(gui.with_name('forge-tools.exe')), 'mods', 'add', str(root),
        str(pack), '--name', 'AssetRefresh'], cwd=cwd, capture_output=True, text=True, timeout=30)
    assert added.returncode == 0, added.stdout+added.stderr
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        case.mkdir()
        def button(widget):
            return [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3',
                    f'click {widget}', 'wait_mods', 'wait_ready', 'frames 3']
        lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
            f'set uiscale {scale}', 'assets_tab 0', 'frames 4', 'assets_tab 1',
            'model_select MESH_OBJECT_BARREL', 'frames 3', 'assert_state model_ready 1',
            'dump_state', 'mods_tab 1', 'frames 3']
        lines += button('btn_mods_deploy')+['assert_log mods deploy: done', 'mods_tab 0',
            'assets_tab 1', 'frames 3', 'assert_state model_ready 0',
            'model_search MESH_MOD_ASSET_REFRESH', 'frames 3', 'assert_state models_filtered 1',
            'model_select MESH_MOD_ASSET_REFRESH', 'frames 3', 'assert_state model_vertices 24',
            'assert_state model_triangles 12', 'assert_state model_users 1', 'assets_tab 0',
            'frames 3', 'texture_select MOD_ASSET_REFRESH_DIFFUSE', 'frames 3',
            'assert_state texture_selected MOD_ASSET_REFRESH_DIFFUSE', 'dump_state',
            f'screenshot {(case/"deployed.png").as_posix()}', 'mods_tab 1', 'frames 3']
        lines += button('btn_mods_undeploy')+['assert_log mods undeploy: done', 'mods_tab 0',
            'assets_tab 1', 'frames 3', 'assert_state model_ready 0',
            'assert_state models_filtered 0', 'assets_tab 0', 'frames 3',
            'assert_state texture_selected -', 'dump_state',
            f'screenshot {(case/"undeployed.png").as_posix()}', 'quit']
        script = case/'refresh.txt'
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        result = subprocess.run([str(gui), '--install', str(selected), '--size', size,
            '--auto', str(script)], cwd=cwd, capture_output=True, text=True, timeout=300,
            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                APPDATA=str(case/'appdata'), LOCALAPPDATA=str(case/'localappdata')))
        (case/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert all(digest(root/name) == value for name, value in baseline.items())
        assert all(digest(p) == value for p, value in level_hashes.items())
        assert not (root/'forge_stage_manifest.json').exists()
        assert not any(root.rglob('*.forgebak'))
        if args.separate_root:
            assert {p: digest(p) for p in selected.rglob('*') if p.is_file()} == selected_hashes
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-6000:]
        for key in ['models_count', 'textures_count']:
            counts = list(map(int, re.findall(r'\b'+key+r'=(\d+)', log)))
            assert len(counts) == 3 and counts[1] == counts[0]+1 and counts[2] == counts[0], (key, counts)
        print(size, 'separate root' if args.separate_root else 'selected install',
            'loaded views refresh on deploy/undeploy; exact bank recovery: PASS', flush=True)
    assert all(digest(p) == value for p, value in source_hashes.items())
    print('Source installation unchanged: PASS', flush=True)


if __name__ == '__main__':
    main()
