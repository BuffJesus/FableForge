#!/usr/bin/env python3
"""CLI level resolution owns extraction files across concurrent calls and early returns."""
import argparse
from concurrent.futures import ThreadPoolExecutor
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
    parser.add_argument('--navigation', action='store_true', help='also exercise navigation diagnostics with real mesh banks')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='cli-level-workspace-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root, temp = work / 'install', work / 'temp'
    files = ['data/Levels/FinalAlbion.wad', 'data/Levels/FinalAlbion.wld',
             'data/Levels/FinalAlbion.bwd', 'data/CompiledDefs/game.bin']
    if args.navigation:
        files += ['data/CompiledDefs/names.bin', 'data/graphics/graphics.big',
                  'data/graphics/pc/textures.big', 'data/Levels/FinalAlbion_RT.stb']
    for name in files:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    wad = root / 'data/Levels/FinalAlbion.wad'
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    original = {p: digest(p) for p in root.rglob('*') if p.is_file()}
    marker = temp / 'FableForge/TeleporterGreatwood.lev'
    marker.parent.mkdir(parents=True)
    marker.write_bytes(b'unrelated CLI extraction')
    nav_marker = marker.parent / 'navlines/TeleporterGreatwood.lev'
    nav_marker.parent.mkdir()
    nav_marker.write_bytes(b'unrelated navigation extraction')
    env = dict(os.environ, TEMP=str(temp), TMP=str(temp))
    def run(name, *command, expected=0):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=60)
        (work / (name + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == expected, result.stdout + result.stderr
        if expected == 2:
            assert "ABSENT_TEST_THEME is not in TeleporterGreatwood's palette" in result.stderr
        return result.stdout
    def intact():
        assert marker.is_file() and marker.read_bytes() == b'unrelated CLI extraction', 'CLI changed or deleted an unowned extraction'
        assert not list(marker.parent.glob('cli-level-*')), 'CLI leaked its owned workspace'
        assert nav_marker.read_bytes() == b'unrelated navigation extraction', 'navigation diagnostic overwrote an unowned extraction'
    nav_outputs = {}
    if args.navigation:
        for command in ('nav-lines', 'nav-compare'):
            nav_outputs[command] = run(command, command, 'TeleporterGreatwood')
            intact()
    with ThreadPoolExecutor(max_workers=2) as pool:
        calls = [pool.submit(run, f'concurrent_{i}', 'info', 'TeleporterGreatwood') for i in range(2)]
        outputs = [call.result() for call in calls]
    intact()
    assert outputs[0] == outputs[1] and 'map 64x64' in outputs[0]
    run('early_return', 'blank-level', 'UnusedProbe', '--template', 'TeleporterGreatwood',
        '--theme', 'ABSENT_TEST_THEME', expected=2)
    intact()
    assert all(digest(p) == h for p, h in original.items())
    subprocess.run([str(repo / 'build/forge-tools.exe'), 'wad', 'extract', str(wad), str(root),
        'TeleporterGreatwood.' if args.navigation else 'TeleporterGreatwood.lev'],
        check=True, capture_output=True, timeout=60)
    if args.navigation:
        # Navigation regions may name a different script and neighbouring TNGs.
        subprocess.run([str(repo / 'build/forge-tools.exe'), 'wad', 'extract', str(wad), str(root),
            '.tng'], check=True, capture_output=True, timeout=60)
    loose = root / 'data/Levels/FinalAlbion/TeleporterGreatwood.lev'
    assert loose.is_file()
    renamed = wad.with_name('_FinalAlbion.wad')
    assert wad.resolve().is_relative_to(work) and renamed.resolve().is_relative_to(work)
    wad.rename(renamed)
    assert run('loose', 'info', 'TeleporterGreatwood') == outputs[0]
    assert run('explicit_path', 'info', str(loose)) == outputs[0]
    for command, output in nav_outputs.items():
        assert run(command + '_loose', command, 'TeleporterGreatwood') == output, 'loose navigation diagnostic changed'
    intact()
    assert not wad.exists() and digest(renamed) == original[wad]
    assert all(digest(p) == h for p, h in original.items() if p != wad), 'diagnostics changed a source bank'
    print('Concurrent extraction, early return, loose and explicit paths: PASS')


if __name__ == '__main__':
    main()
