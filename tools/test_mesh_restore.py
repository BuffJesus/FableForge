#!/usr/bin/env python3
"""A real static-model import must Restore every asset bank, including graphics.big."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_meshimport import write_glb, write_png


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    parser.add_argument('--graphics-pc', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='mesh-restore-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    graphics = args.root / 'data/graphics/graphics.big'
    if not graphics.is_file():
        graphics = args.root / 'data/graphics/pc/graphics.big'
    copies = {name: args.root / name for name in ('data/CompiledDefs/game.bin',
        'data/CompiledDefs/names.bin', 'data/graphics/pc/textures.big')}
    copies['data/graphics/' + ('pc/' if args.graphics_pc else '') + 'graphics.big'] = graphics
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    for name, source in copies.items():
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    original = {name: digest(root / name) for name in copies}
    model, texture = work / 'cube.glb', work / 'wood.png'
    write_glb(str(model))
    write_png(str(texture))
    result = subprocess.run([str(args.exe.resolve()), 'mesh-import', str(model), 'FORGE_RESTORE_PROBE',
        '--texture', str(texture), '--install', str(root)], cwd=repo,
        capture_output=True, text=True, timeout=240)
    (work / 'import.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    assert all((root / (name + '.forge-orig')).is_file() for name in copies), 'import omitted a bank backup'
    assert all(digest(root / name) != value for name, value in original.items()), 'import did not exercise every bank'
    result = subprocess.run([str(args.exe.resolve()), 'backups', '--install', str(root)],
        cwd=repo, capture_output=True, text=True, timeout=60)
    (work / 'backups.log').write_text(result.stdout + result.stderr)
    listed_graphics = 'graphics.big' in result.stdout
    result = subprocess.run([str(args.exe.resolve()), 'restore', '--install', str(root), '--forget'],
        cwd=repo, capture_output=True, text=True, timeout=120)
    (work / 'restore.log').write_text(result.stdout + result.stderr)
    restored = {name: digest(root / name) for name in copies}
    extra = sorted(p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and p.relative_to(root).as_posix() not in original)
    (work / 'report.json').write_text(json.dumps({'graphics_listed': listed_graphics,
        'banks_restored': {name: restored[name] == value for name, value in original.items()}, 'extra_files': extra}, indent=2))
    assert result.returncode == 0, result.stdout + result.stderr
    assert listed_graphics and restored == original and not extra, 'model import did not Restore every bank'
    print('Real model import, all-bank listing and exact Restore: PASS')


if __name__ == '__main__':
    main()
