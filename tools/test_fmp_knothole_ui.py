#!/usr/bin/env python3
"""Deploy named Knothole children into a scratch install and place them in Forge."""
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


PACKAGE = 'mods/Knothole Glade Children.fmp'
BOY = 'CREATURE_KH_VILLAGER_BOY'
GIRL = 'CREATURE_KH_VILLAGER_GIRL'
MESH = 'MESH_BS_MALE_CHILD_UNCLOTHED_01'


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
    work = Path(tempfile.mkdtemp(prefix='fmp-knothole-ui-', dir=repo/'build')).resolve()
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
    (maps/'KnotholeProbe.lev').write_bytes(level(4))
    (maps/'KnotholeProbe.tng').write_bytes(b'Version 2;\r\n')
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        payload = archive.read(PACKAGE)
        assert hashlib.sha256(payload).hexdigest() == catalogue[PACKAGE]['sha256']
    package = work/'knothole_children.fmp'
    package.write_bytes(payload)
    script = work/'knothole_ui.txt'
    lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
             'select KnotholeProbe', 'wait_loaded', 'mods_tab 1', 'frames 2',
             f'mod_add {package.as_posix()} Knothole Children', 'frames 2',
             'assert_state mods_count 1', 'mods_deploy', 'wait_mods', 'wait_ready',
             'assert_log mods deploy: done', 'mods_tab 0', 'assets_tab 1',
             f'model_search {MESH}', 'frames 3', 'assert_state models_filtered 1',
             f'model_select {MESH}', 'frames 3', 'assert_state model_ready 1',
             f'screenshot {(work/"model.png").as_posix()}',
             'assets_tab 0', 'edit 1', 'edit_tab 0', 'frames 3',
             f'place {BOY} KnotholeBoyProbe', 'frames 4',
             f'assert_log placed {BOY}', 'assert_state selected_creature_sex 1',
             'wait_foliage', 'assert_state selected_mesh_instances 1',
             'frame_selected', 'frames 3',
             f'screenshot {(work/"boy.png").as_posix()}',
             'undo', 'frames 2',
             f'place {GIRL} KnotholeGirlProbe', 'frames 4',
             f'assert_log placed {GIRL}', 'assert_state selected_creature_sex 2',
             'wait_foliage', 'assert_state selected_mesh_instances 1',
             'frame_selected', 'frames 3',
             f'screenshot {(work/"girl.png").as_posix()}',
             'undo', 'frames 2', 'mods_tab 1', 'mods_undeploy', 'wait_mods',
             'wait_ready', 'assert_log mods undeploy: done', 'quit']
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
    assert all((work/name).is_file() for name in ('model.png', 'boy.png', 'girl.png'))
    assert not (root/'forge_stage_manifest.json').exists()
    assert all(digest(root/name) == value for name, value in hashes.items())
    assert all(digest(args.install/name) == value for name, value in hashes.items())
    print('Knothole boy and girl placed in Forge and undeploy restored banks:', work)


if __name__ == '__main__':
    main()
