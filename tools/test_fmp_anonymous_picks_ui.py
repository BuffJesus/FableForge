#!/usr/bin/env python3
"""Check real anonymous definition conflicts and a saved winner in Mods UI."""
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


MEMBERS = {'bright': 'mods/Brighterlamp.fmp',
           'super': 'mods/Lamp-Super Bright.fmp'}


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
    work = Path(tempfile.mkdtemp(prefix='fmp-anonymous-ui-', dir=repo/'build')).resolve()
    assert work.parent == (repo/'build').resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'
    source = {name: args.install/name for name in (
        'data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
        'data/CompiledDefs/script.bin', 'data/CompiledDefs/frontend.bin',
        'data/graphics/graphics.big', 'data/graphics/pc/textures.big')}
    hashes = {name: digest(path) for name, path in source.items()}
    for name, original in source.items():
        target = root/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    maps = root/'data/Levels/FinalAlbion'
    maps.mkdir(parents=True)
    (maps/'LampProbe.lev').write_bytes(level(4))
    (maps/'LampProbe.tng').write_bytes(b'Version 2;\r\n')
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {row['file']: row for row in json.loads(archive.read('catalogue.json'))}
        for label, member in MEMBERS.items():
            payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            (work/(label+'.fmp')).write_bytes(payload)
    script = work/'anonymous_ui.txt'
    lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
             'mods_tab 1', 'frames 2',
             f'mod_add {(work/"bright.fmp").as_posix()} Brighterlamp',
             f'mod_add {(work/"super.fmp").as_posix()} Superbright',
             'frames 2', 'assert_state mods_count 2',
             'mods_conflicts', 'wait_mods', 'frames 2',
             'assert_state mods_conflicts 1', 'assert_state mods_field_conflicts 2',
             'mod_pick @unnamed:12869 Brighterlamp', 'frames 2',
             f'assert_file_contains {(root/"forge_mods_picks.txt").as_posix()} @unnamed:12869',
             f'screenshot {(work/"conflict.png").as_posix()}',
             'mods_deploy', 'wait_mods', 'wait_ready',
             'assert_log mods deploy: done',
             'mods_undeploy', 'wait_mods', 'wait_ready',
             'assert_log mods undeploy: done', 'quit']
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
    assert (work/'conflict.png').is_file()
    assert '@unnamed:12869\tBrighterlamp' in (root/'forge_mods_picks.txt').read_text(encoding='utf-8')
    assert not (root/'forge_stage_manifest.json').exists()
    assert all(digest(root/name) == value for name, value in hashes.items())
    assert all(digest(args.install/name) == value for name, value in hashes.items())
    print('Anonymous lamp conflict and saved winner appear in Mods UI:', work)


if __name__ == '__main__':
    main()
