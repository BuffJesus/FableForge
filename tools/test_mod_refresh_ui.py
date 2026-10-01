#!/usr/bin/env python3
"""Deploy/undeploy a new-level pack in one GUI, outside the Mods panel.

Creates an owned scratch install; checks map-list/preview refresh, preservation
of an unsaved draft, and byte-exact restoration of every copied source bank.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', default=os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters'))
    args = ap.parse_args()
    repo = Path(__file__).resolve().parents[1]
    source = Path(args.root)
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
             'FinalAlbion.wad', 'FinalAlbion_RT.stb', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']
    if not all((source / name).is_file() for name in files):
        raise SystemExit('mod refresh test requires stock world banks, definitions and textures')
    work = Path(tempfile.mkdtemp(prefix='mod-refresh-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    root = work / 'install'
    for name in files:
        dest = root / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, dest)
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    original = {name: digest(root / name) for name in files}
    pack = work / 'pack'
    pack.mkdir()
    (pack / 'forge_pack.json').write_text(json.dumps({'version': 1, 'name': 'Refresh Probe',
                                                   'models': [], 'groundThemes': []}))
    env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1')
    def run(name, body):
        script = work / (name + '.txt')
        script.write_text('wait_maps\nwait_ready\nselect TeleporterGreatwood\nwait_loaded\n' + body + '\ndump_log\nquit\n')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(root), '--auto', str(script)],
                                cwd=repo, env=env, capture_output=True, text=True, timeout=240)
        log = Path(str(script) + '.log').read_text()
        assert result.returncode == 0 and 'RESULT PASS' in log, log + result.stderr
        print(name + ': PASS', flush=True)
    run('create', f'''pack_dest {pack}
new_level RefreshProbe 6400 6400 Greatwood
wait_new_level
frames 3
assert_log new level RefreshProbe written into pack''')
    assert (pack / 'data/Levels/FinalAlbion.wld').is_file()
    assert (pack / 'stb/RefreshProbe.chunk').is_file()
    packed = {p.relative_to(pack): digest(p) for p in pack.rglob('*') if p.is_file()}
    listing = subprocess.check_output([str(repo / 'build/forge.exe'), 'list', '--install', str(root)], text=True)
    count = int(listing.split()[0])
    for name, draft, after in [
        ('clean', '', ''),
        ('world', '', ''),
        ('draft', '''edit 1
place OBJECT_BARREL_BREAKABLE KeepDraft
terrain_mode 0
brush 3 10
terrain_stroke 3 3 1''', '''assert_state doc_dirty 1
assert_state terrain_dirty 1
assert_log kept the unsaved map draft
undo
assert_state terrain_dirty 0
assert_state doc_dirty 1
undo
assert_state doc_dirty 0'''),
    ]:
        leave_mods = 'world_tab 1\nset world_3d 1' if name == 'world' else 'mods_tab 0'
        before_mods = 'world_tab 1\nset world_3d 1\nframes 8' if name == 'world' else 'mods_tab 1'
        run(name, f'''{draft}
mod_add {pack} RefreshProbe
{before_mods}
mods_deploy
{leave_mods}
wait_file_job
wait_ready
assert_log mods deploy: done
assert_state maps {count + 1}
world_tab 0
{after}
select RefreshProbe
wait_loaded
assert_state selected RefreshProbe
assert_state preview_has_mesh 1
clear_toasts
screenshot {work / (name + '_deployed.png')}
mods_undeploy
wait_file_job
wait_ready
assert_log mods undeploy: done
assert_state maps {count}
assert_state doc_loaded 0
assert_state preview_has_mesh 0
mod_remove 0''')
        assert original == {rel: digest(root / rel) for rel in files}, 'undeploy changed original banks'
        assert packed == {p.relative_to(pack): digest(p) for p in pack.rglob('*') if p.is_file()}, 'deployment changed the pack'
    broken = work / 'game.bin.patch'
    broken.write_bytes(b'not a valid binary patch')
    run('failed_redeploy', f'''mod_add {pack} RefreshProbe
mods_deploy
mods_tab 0
wait_file_job
wait_ready
assert_log mods deploy: done
assert_state maps {count + 1}
select RefreshProbe
wait_loaded
assert_state preview_has_mesh 1
mod_add {broken} BrokenPatch
mods_deploy
wait_file_job
wait_ready
assert_log mods deploy: FAILED
assert_log refreshing after a failed write
assert_state maps {count}
assert_state doc_loaded 0
assert_state preview_has_mesh 0
mod_remove 1
mod_remove 0''')
    assert original == {rel: digest(root / rel) for rel in files}, 'failed redeploy changed original banks'
    assert packed == {p.relative_to(pack): digest(p) for p in pack.rglob('*') if p.is_file()}, 'failed redeploy changed the pack'
    print('same-window refresh and byte-exact undeploy: PASS; evidence retained at', work)


if __name__ == '__main__':
    main()
