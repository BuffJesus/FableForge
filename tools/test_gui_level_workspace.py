#!/usr/bin/env python3
"""Exercise GUI preview, document, fit and export extraction ownership on scratch banks."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/FableForge.exe')
    parser.add_argument('--loose', action='store_true')
    parser.add_argument('--world', action='store_true', help='also build overview tiles and load World detail')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='gui-level-workspace-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root, temp = work / 'install', work / 'temp'
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
        'FinalAlbion.wad', 'FinalAlbion_RT.stb', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']
    for name in files:
        destination = root / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, destination)
    if args.loose:
        wad = root / 'data/Levels/FinalAlbion.wad'
        subprocess.run([str(repo / 'build/forge-tools.exe'), 'wad', 'extract', str(wad), str(root)],
            check=True, capture_output=True, timeout=180)
        renamed = wad.with_name('_FinalAlbion.wad')
        assert wad.resolve().is_relative_to(work) and renamed.resolve().is_relative_to(work)
        wad.rename(renamed)
        files = [p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()]
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    original = {name: digest(root / name) for name in files}
    shared = temp / 'FableForge'
    shared.mkdir(parents=True)
    markers = []
    tile_markers = []
    for name in re.findall(r'LevelName "([^"]+)";', (root / 'data/Levels/FinalAlbion.wld').read_text()):
        marker = shared / name.replace('\\', '/').split('/')[-1]
        marker.write_bytes(b'unrelated GUI extraction')
        markers.append(marker)
        if args.world:
            for worker in range(6):
                tile_marker = shared / 'worldtiles' / (marker.stem + f'_{worker}.lev')
                tile_marker.parent.mkdir(exist_ok=True)
                tile_marker.write_bytes(b'unrelated world tile extraction')
                tile_markers.append(tile_marker)
    template = (repo / 'tests/ui/fit_neighbours.txt').read_text()
    template = template.replace('set saveroot build/ui_fit_scratch\n', '')
    template = template.replace('build/ui/', (work / 'screens').as_posix() + '/')
    template = template.replace('quit', f'''click chip_neighbours
wait_neighbours
assert_log neighbours: 4 maps around Greatwood_Filler_04
set outdir {work / 'export'}
set format glb
set textures 0
set things 0
set foliage 0
export
wait_export
assert_state export_ok 1
quit''')
    if args.world:
        template = template.replace('quit', f'''set world_detail 0
set world_auto_detail 0
set world_detail_limit 1
set world_objects 0
set world_creatures 0
set world_plants 0
world_tab 1
wait_world_tiles
set world_3d 1
world_camera 3515 815 5 0.8 0.62 200
set world_detail 1
wait_world_detail
assert_state world_detail_maps 1
screenshot {(work / 'screens/world.png').as_posix()}
set world_detail 0
wait_world_detail_idle
quit''')
    script = work / 'fit_export.txt'
    script.write_text(template)
    env = dict(os.environ, TEMP=str(temp), TMP=str(temp), FABLEFORGE_AUTOMATION_HIDDEN='1')
    if args.world:
        profile = work / 'profile'
        profile.mkdir()
        env['LOCALAPPDATA'] = str(profile)
        env['FABLEFORGE_TILE_CACHE'] = str(profile / 'FableForge/worldtiles')
    result = subprocess.run([str(args.exe.resolve()), '--install', str(root), '--auto', str(script)],
        cwd=repo, env=env, capture_output=True, text=True, timeout=1000 if args.world else 240)
    log = Path(str(script) + '.log').read_text()
    assert result.returncode == 0 and 'RESULT PASS' in log, log + result.stderr
    assert all(p.is_file() and p.read_bytes() == b'unrelated GUI extraction' for p in markers), 'GUI overwrote an unowned extraction'
    assert not list(shared.glob('gui-level-*')), 'GUI leaked an owned extraction'
    assert all(p.is_file() and p.read_bytes() == b'unrelated world tile extraction' for p in tile_markers), 'World tiles changed an unowned extraction'
    assert not list(shared.glob('world-tile-*')), 'World tiles leaked an owned extraction'
    glb = (work / 'export/Greatwood_Filler_04.glb').read_bytes()
    assert struct.unpack_from('<III', glb) == (0x46546C67, 2, len(glb))
    assert original == {name: digest(root / name) for name in files}, 'GUI changed source banks'
    if args.loose:
        assert not (root / 'data/Levels/FinalAlbion.wad').exists(), 'GUI recreated an active WAD'
    print('GUI fit/undo/redo, preview/export and extraction ownership: PASS')


if __name__ == '__main__':
    main()
