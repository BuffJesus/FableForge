#!/usr/bin/env python3
"""Deploy an archived FMP and exercise its object through Forge's native UI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from test_setup_restore_ui import level


NAME = 'OBJECT_HUGE_SHROOM_01_BIT_01'
MESH = 'MESH_HUGE_SHROOM_01_BIT_01'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='fmp-named-object-ui-', dir=repo/'build')).resolve()
    assert work.parent == (repo/'build').resolve()
    print('Evidence retained at', work, flush=True)
    source = {name: args.install/name for name in (
        'data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
        'data/CompiledDefs/script.bin', 'data/CompiledDefs/frontend.bin',
        'data/graphics/graphics.big', 'data/graphics/pc/textures.big')}
    hashes = {name: digest(path) for name, path in source.items()}
    root = work/'install'
    for name, original in source.items():
        target = root/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    maps = root/'data/Levels/FinalAlbion'
    maps.mkdir(parents=True)
    (maps/'NamedFmp.lev').write_bytes(level(4))
    (maps/'NamedFmp.tng').write_bytes(b'Version 2;\r\n')
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        payload = archive.read('mods/Huge Mushroom.fmp')
        assert hashlib.sha256(payload).hexdigest() == catalogue['mods/Huge Mushroom.fmp']['sha256']
    package = work/'huge_mushroom.fmp'
    package.write_bytes(payload)
    script = work/'object_ui.txt'
    lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
             'select NamedFmp', 'wait_loaded', 'mods_tab 1', 'frames 2',
             f'mod_add {package.as_posix()} Huge Mushroom', 'frames 2',
             'assert_state mods_count 1', 'mods_deploy', 'wait_mods', 'wait_ready',
             'assert_log mods deploy: done', 'mods_tab 0', 'assets_tab 1',
             f'model_search {MESH}', 'frames 3', 'assert_state models_filtered 1',
             f'model_select {MESH}', 'frames 3', 'assert_state model_ready 1',
             'assert_state model_users 1', 'assert_state model_vertices 6113',
             'assert_state model_triangles 4674',
             f'screenshot {(work/"model.png").as_posix()}',
             'assets_tab 0', 'edit 1', 'edit_tab 0', 'frames 3',
             f'place {NAME} NamedShroomProbe', 'frames 4',
             f'assert_log placed {NAME}', 'assert_state selected_mesh_instances 1',
             'frame_selected', 'frames 3',
             f'screenshot {(work/"placed.png").as_posix()}',
             'undo', 'frames 2', 'mods_tab 1', 'mods_undeploy', 'wait_mods',
             'wait_ready', 'assert_log mods undeploy: done', 'mods_tab 0',
             'assets_tab 1', f'model_search {MESH}', 'frames 3',
             'assert_state models_filtered 0',
             'quit']
    script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
    gui = args.gui.resolve()
    result = subprocess.run([str(gui), '--install', str(root), '--size', '1440x900',
                             '--auto', str(script)], cwd=repo, capture_output=True,
                            text=True, timeout=300,
                            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                                     APPDATA=str(work/'appdata'),
                                     LOCALAPPDATA=str(work/'localappdata')))
    (work/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    log_file = Path(str(script)+'.log')
    log = log_file.read_text(encoding='utf-8') if log_file.exists() else ''
    assert result.returncode == 0 and 'RESULT PASS' in log, log[-6000:]
    assert (work/'model.png').is_file() and (work/'placed.png').is_file()
    assert not (root/'forge_stage_manifest.json').exists()
    assert all(digest(root/name) == value for name, value in hashes.items())
    assert all(digest(args.install/name) == value for name, value in hashes.items())
    print('Named FMP object appears in model browser and selected viewport; '
          'undeploy restores banks:', work)


if __name__ == '__main__':
    main()
