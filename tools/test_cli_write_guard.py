#!/usr/bin/env python3
"""High-level CLI install writes must refuse a running process from that install."""
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
    if os.name != 'nt':
        raise SystemExit('this process-image guard check requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='cli-write-guard-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    names = ['data/Levels/' + name for name in ('FinalAlbion.wld', 'FinalAlbion.bwd',
        'FinalAlbion.gtg', 'FinalAlbion.wad', 'FinalAlbion_RT.stb')]
    names += ['data/CompiledDefs/game.bin']
    for name in names:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file() and path.name != 'Fable.exe':
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    source = work / 'hold.cpp'
    source.write_text('#include <windows.h>\n#include <cstdio>\nint main() { puts("ready"); fflush(stdout); Sleep(180000); }\n')
    cache = (repo / 'build/CMakeCache.txt').read_text()
    compiler = re.search(r'^CMAKE_CXX_COMPILER:FILEPATH=(.+)$', cache, re.MULTILINE).group(1)
    helper = root / 'Fable.exe'
    subprocess.run([compiler, str(source), '-o', str(helper)], check=True, capture_output=True, timeout=60)
    def run(label, *command):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=90)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result
    commands = [
        ['entrance', 'TeleporterGreatwood', '1', '2', '3'],
        ['blank-level', 'GuardProbe', '--template', 'TeleporterGreatwood', '--at', '6400,6400'],
        ['new-level', 'TeleporterGreatwood', 'GuardProbe', '--at', '6400,6400'],
        ['region-props', 'Greatwood', '--display', 'GuardProbe'],
        ['world-stitch', 'TeleporterGreatwood'],
        ['world-move', 'TeleporterGreatwood', '6400', '6400'],
        ['world-owner', 'TeleporterGreatwood', 'Greatwood'],
        ['world-sees', 'Greatwood', 'TeleporterGreatwood', '1'],
        ['minimap-register', 'GUARD_PROBE', '1'],
        ['compact-stb'],
        ['chunk-zcheck', 'TeleporterGreatwood', '0', '--write'],
        ['texture-replace', 'Missing', 'missing.png'],
        ['texture-add', 'GuardProbe', 'missing.png'],
        ['theme-add', 'missing.png', 'GUARD_PROBE'],
        ['mesh-import', 'missing.obj', 'GUARD_PROBE'],
    ]
    process = subprocess.Popen([str(helper)], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        assert process.stdout.readline().strip() == 'ready'
        for command in commands:
            result = run('blocked_' + command[0], *command)
            unchanged = snapshot() == original
            if result.returncode == 0 or 'Fable.exe is running' not in result.stdout + result.stderr or not unchanged:
                (work / 'failure.json').write_text(json.dumps({'command': command, 'exit': result.returncode, 'unchanged': unchanged}, indent=2))
                raise AssertionError('write command was not safely refused: ' + ' '.join(command))
        for label, command in (
                ('entrance_read', ['entrance', 'TeleporterGreatwood']),
                ('world_read', ['world']),
                ('backups_read', ['backups']),
                ('compact_dry', ['compact-stb', '--dry-run']),
                ('stitch_dry', ['world-stitch', 'TeleporterGreatwood', '--dry-run'])):
            result = run(label, *command)
            assert result.returncode == 0, result.stdout + result.stderr
        assert snapshot() == original
    finally:
        process.terminate()
        process.communicate(timeout=10)
    result = run('after_exit', 'entrance', 'TeleporterGreatwood', '1', '2', '3')
    assert result.returncode == 0, result.stdout + result.stderr
    assert snapshot() != original
    result = run('restore', 'restore', '--forget')
    assert result.returncode == 0 and snapshot() == original, result.stdout + result.stderr
    sibling = work / 'install-other/Fable.exe'
    sibling.parent.mkdir()
    shutil.copyfile(helper, sibling)
    helper.unlink()
    process = subprocess.Popen([str(sibling)], cwd=sibling.parent, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        assert process.stdout.readline().strip() == 'ready'
        result = run('sibling_write', 'entrance', 'TeleporterGreatwood', '1', '2', '3')
        assert result.returncode == 0, result.stdout + result.stderr
        result = run('sibling_restore', 'restore', '--forget')
        assert result.returncode == 0 and snapshot() == original, result.stdout + result.stderr
    finally:
        process.terminate()
        process.communicate(timeout=10)
    (work / 'report.json').write_text(json.dumps({'blocked_commands': [c[0] for c in commands],
        'readonly_commands_allowed': True, 'exit_retry_and_restore': True, 'sibling_write_allowed': True}, indent=2))
    print('CLI write refusal, read-only access, exit retry and exact Restore: PASS')


if __name__ == '__main__':
    main()
