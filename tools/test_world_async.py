#!/usr/bin/env python3
"""Verify World drafts and undo while writes run against owned scratch packs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
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
        raise SystemExit('World async test requires world banks, definitions and textures')
    work = Path(tempfile.mkdtemp(prefix='world-async-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    root = work / 'install'
    for name in files:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target)
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    original = {name: digest(root / name) for name in files}
    temp = work / 'temp'
    marker = temp / 'FableForge/overworld/TeleporterGreatwood.lev'
    marker.parent.mkdir(parents=True)
    marker.write_bytes(b'owned by another operation')
    env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', TEMP=str(temp), TMP=str(temp))
    def run(name, commands, regions=True):
        pack = work / name
        pack.mkdir()
        (pack / 'forge_pack.json').write_text(json.dumps({'version': 1, 'name': name,
            'models': [], 'groundThemes': []}))
        initial = 'world_owner OrchardFarmEast Greatwood\nworld_sees Greatwood OrchardFarm 1\n' if regions else ''
        script = work / (name + '.txt')
        script.write_text(f'''wait_maps
wait_ready
pack_dest {pack}
world_tab 1
set world_3d 0
world_stitch 0
world_move TeleporterGreatwood 2048 8064
{initial}world_select TeleporterGreatwood
world_apply
assert_state file_job world_write
{commands}
dump_log
quit
''')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(root), '--auto', str(script)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=240)
        log = Path(str(script) + '.log').read_text()
        assert result.returncode == 0 and 'RESULT PASS' in log, log + result.stderr
        assert original == {rel: digest(root / rel) for rel in files}, 'World pack write changed source banks'
        assert marker.read_bytes() == b'owned by another operation', 'World write overwrote an unowned extracted LEV'
        assert not list((temp / 'FableForge').glob('world-lev-*')), 'World write leaked an owned LEV workspace'
        print(name + ': PASS', flush=True)
        return pack
    later = '''world_move TeleporterGreatwood 2048 8032
world_owner OrchardFarmEast OrchardFarm
world_sees Greatwood OrchardFarm 0
assert_state file_job world_write
wait_world
assert_state world_ok 1
assert_state world_pending 1
assert_state world_pending_owners 1
assert_state world_pending_sees 1
assert_state world_selected_pos 2048,8032
world_undo
assert_state world_pending_sees 0
world_undo
assert_state world_pending_owners 0
world_undo
assert_state world_pending 0
assert_state world_selected_pos 2048,8064
world_redo
world_redo
world_redo
assert_state world_pending 1
assert_state world_pending_owners 1
assert_state world_pending_sees 1'''
    packs = [run('later_edits', later)]
    packs.append(run('put_back', '''world_revert
assert_state world_pending 0
assert_state file_job world_write
wait_world
assert_state world_ok 1
assert_state world_pending 1
assert_state world_pending_owners 1
assert_state world_pending_sees 1
assert_state world_selected_pos 3072,2880
world_undo
assert_state world_pending 0
assert_state world_pending_owners 0
assert_state world_pending_sees 0
world_redo
assert_state world_pending 1
assert_state world_pending_owners 1
assert_state world_pending_sees 1'''))
    moves = '\n'.join(f'world_move TeleporterGreatwood 2048 {8032 if i % 2 == 0 else 8000}' for i in range(132))
    capped = run('bounded_undo', moves + '''
assert_state file_job world_write
wait_world
assert_state world_ok 1
assert_state world_pending 1
assert_state world_selected_pos 2048,8000
world_undo
assert_state world_pending 1
assert_state world_selected_pos 2048,8032''', regions=False)
    repeated = run('repeat', later + '''
world_apply
wait_world
assert_state world_ok 1
assert_state world_pending 0
assert_state world_pending_owners 0
assert_state world_pending_sees 0
assert_state world_selected_pos 2048,8032
assert_state world_can_undo 0''')
    def block(data, kind, needle):
        return next(value for value in re.findall(rb'New' + kind + rb'\s+\d+;.*?End' + kind + rb';', data, re.S) if needle in value)
    def verify(pack, y, owner, sees):
        data = (pack / 'data/Levels/FinalAlbion.wld').read_bytes()
        moved = block(data, b'Map', b'LevelScriptName "TeleporterGreatwood";')
        assert b'MapX 2048;' in moved and f'MapY {y};'.encode() in moved, 'pack contains later draft position'
        region = block(data, b'Region', b'ContainsMap "FinalAlbion\\OrchardFarmEast.lev";')
        assert f'RegionName "{owner}";'.encode() in region, 'pack contains later draft owner'
        greatwood = block(data, b'Region', b'RegionName "Greatwood";')
        assert (b'SeesMap "FinalAlbion\\OrchardFarm.lev";' in greatwood) == sees, 'pack contains later draft visibility'
    for pack in packs:
        verify(pack, 8064, 'Greatwood', True)
    verify(capped, 8064, 'OrchardFarm', False)
    verify(repeated, 8032, 'OrchardFarm', False)
    print('World snapshots, later edits, bounded undo and repeat write: PASS; evidence retained at', work)


if __name__ == '__main__':
    main()
