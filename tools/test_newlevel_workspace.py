#!/usr/bin/env python3
"""Own-region blank creation, entrance, minimap and palette scratch ownership."""
import argparse
import hashlib
import json
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
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    parser.add_argument('--gui', type=Path, default=repo / 'build/FableForge.exe')
    parser.add_argument('--loose', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='newlevel-workspace-', dir=repo / 'build')).resolve()
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
    names = ['newlevel/WorkspaceProbe.lev', 'entrance/WorkspaceProbe.lev',
        'minimap/WorkspaceProbe.lev', 'minimap/WorkspaceProbe_minimap.png']
    for level in re.findall(r'LevelName "([^"]+)";', (root / 'data/Levels/FinalAlbion.wld').read_text()):
        names.append('newlevel/' + Path(level.replace('\\', '/')).stem + '.palette.lev')
    markers = []
    for name in names:
        path = temp / 'FableForge' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'unrelated new-level workspace file')
        markers.append(path)
    env = dict(os.environ, TEMP=str(temp), TMP=str(temp), FABLEFORGE_AUTOMATION_HIDDEN='1')
    def run(name, *command):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=240)
        (work / (name + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout
    output = run('create', 'blank-level', 'WorkspaceProbe', '--template', 'TeleporterGreatwood',
        '--at', '6400,6400', '--own-region', '--height', '12')
    assert 'authored from scratch' in output and 'region entrance + WorkspaceProbeHSP added' in output
    assert 'minimap:' in output and 'installed: map slot' in output
    assert 'WorkspaceProbeHSP' in run('entrance', 'entrance', 'WorkspaceProbe')
    image = work / 'minimap.png'
    run('minimap', 'minimap-bake', 'WorkspaceProbe', str(image), '--region', 'WorkspaceProbe')
    png = image.read_bytes()
    assert png[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack_from('>II', png, 16) == (256, 256)
    if args.loose:
        lev = root / 'data/Levels/FinalAlbion/WorkspaceProbe.lev'
        assert not (root / 'data/Levels/FinalAlbion.wad').exists()
    else:
        subprocess.run([str(repo / 'build/forge-tools.exe'), 'wad', 'extract',
            str(root / 'data/Levels/FinalAlbion.wad'), str(work / 'authored'), 'WorkspaceProbe.lev'],
            check=True, capture_output=True, timeout=60)
        lev = work / 'authored/data/Levels/FinalAlbion/WorkspaceProbe.lev'
    authored = digest(lev)
    script = work / 'palette.txt'
    script.write_text('''wait_maps
wait_ready
select TeleporterGreatwood
wait_loaded
edit 1
edit_tab 3
new_level_blank 3 12 64 64
frames 3
reveal btn_new_level
frames 3
assert_widget btn_new_level
dump_log
quit
''')
    result = subprocess.run([str(args.gui.resolve()), '--install', str(root), '--auto', str(script)],
        cwd=repo, env=env, capture_output=True, text=True, timeout=180)
    log = Path(str(script) + '.log').read_text()
    assert result.returncode == 0 and 'RESULT PASS' in log, log + result.stderr
    assert 'new level: ' not in log, 'palette preparation failed'
    run('restore', 'restore', '--forget')
    assert original == {name: digest(root / name) for name in files}
    assert {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()} == set(files)
    changed = [str(p.relative_to(temp)) for p in markers if not p.is_file() or p.read_bytes() != b'unrelated new-level workspace file']
    (work / 'report.json').write_text(json.dumps({'restored_exactly': True, 'lev_sha256': authored,
        'minimap_sha256': digest(image), 'changed_unowned_files': changed}, indent=2))
    assert not changed, 'new-level helpers changed unowned files: ' + ', '.join(changed)
    for prefix in ('entrance-lev-', 'minimap-lev-', 'minimap-png-', 'template-palette-', 'blank-level-'):
        assert not list((temp / 'FableForge').glob(prefix + '*')), 'owned workspace leaked: ' + prefix
    print('Blank level, entrance, minimap, GUI palette and exact Restore: PASS')


if __name__ == '__main__':
    main()
