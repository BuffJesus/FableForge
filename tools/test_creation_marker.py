#!/usr/bin/env python3
"""An unusable creation marker must refuse a loose save and retain the draft."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    parser.add_argument('--gui', type=Path, default=repo / 'build/FableForge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='creation-marker-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    files = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
        'FinalAlbion.wad', 'FinalAlbion_RT.stb', 'FinalAlbion.gtg')]
    files += ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big']
    for name in files:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    original = {name: digest(root / name) for name in files}
    target = root / 'data/Levels/FinalAlbion/TeleporterGreatwood.tng'
    env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1')
    def gui(label, save, dirty):
        script = work / (label + '.txt')
        script.write_text('wait_maps\nwait_ready\nselect TeleporterGreatwood\nwait_loaded\nedit 1\n'
            'place OBJECT_BARREL_BREAKABLE MarkerProbe_' + label.replace('-', '_') + '\n' + save + '\nassert_state doc_dirty ' + str(dirty) + '\ndump_log\nquit\n')
        result = subprocess.run([str(args.gui.resolve()), '--install', str(root), '--auto', str(script)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=120)
        log = Path(str(script) + '.log').read_text()
        assert result.returncode == 0 and 'RESULT PASS' in log, log + result.stderr
    for suffix in ('.forge-created', '.atlas-created', '.forge-orig', '.atlas-orig'):
        marker = Path(str(target) + suffix)
        marker.mkdir(parents=True)
        sentinel = marker / 'preserve.txt'
        sentinel.write_bytes(b'not a marker file')
        gui(suffix[1:], 'save_level_refused', 1)
        assert not target.exists(), 'failed marker creation wrote an untracked TNG'
        assert sentinel.read_bytes() == b'not a marker file'
        sentinel.unlink()
        marker.rmdir()
    gui('retry', 'save_level', 0)
    assert target.is_file() and 'MarkerProbe' in target.read_text()
    assert Path(str(target) + '.forge-created').is_file()
    for suffix in ('.forge-orig', '.atlas-orig'):
        invalid = Path(str(target) + suffix)
        invalid.mkdir()
        sentinel = invalid / 'preserve.txt'
        sentinel.write_bytes(b'not an original backup')
        before = target.read_bytes()
        gui('existing_target-' + suffix[1:], 'save_level_refused', 1)
        assert target.read_bytes() == before and sentinel.read_bytes() == b'not an original backup'
        sentinel.unlink()
        invalid.rmdir()

    result = subprocess.run([str(args.exe.resolve()), 'restore', '--install', str(root), '--forget'],
        cwd=repo, env=env, capture_output=True, text=True, timeout=60)
    (work / 'restore.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0 and not target.exists()
    for suffix in ('.forge-orig', '.atlas-orig'):
        backup = Path(str(target) + suffix)
        backup.write_bytes(b'original loose TNG')
        gui('existing-' + suffix[1:], 'save_level', 0)
        assert not Path(str(target) + '.forge-created').exists(), 'save introduced contradictory metadata'
        assert backup.read_bytes() == b'original loose TNG'
        result = subprocess.run([str(args.exe.resolve()), 'restore', '--install', str(root), '--forget'],
            cwd=repo, env=env, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0 and target.read_bytes() == b'original loose TNG'
        assert not backup.exists()
        target.unlink()
    assert original == {name: digest(root / name) for name in files}
    assert set(p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()) == set(files)
    print('Creation marker refusal, draft preservation and retry: PASS')


if __name__ == '__main__':
    main()
