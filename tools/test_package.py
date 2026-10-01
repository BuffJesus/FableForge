#!/usr/bin/env python3
"""Smoke-test an extracted local package from an unrelated working directory."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile
from urllib.parse import unquote


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    args = parser.parse_args()
    archive = args.archive.resolve()
    work = Path(tempfile.mkdtemp(prefix='package-smoke-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    unpack = work / 'package %FORGE_PACKAGE_TOKEN% & files'
    unpack.mkdir()
    with zipfile.ZipFile(archive) as source:
        assert source.testzip() is None, 'archive CRC failure'
        for member in source.infolist():
            target = (unpack / member.filename).resolve()
            assert target.is_relative_to(unpack), member.filename
        source.extractall(unpack)
    packages = [path for path in unpack.iterdir() if path.is_dir()]
    assert len(packages) == 1
    package = packages[0]
    for name in ('FableForge.exe', 'forge.exe', 'forge-tools.exe', 'README.md', 'LICENSE',
                 'docs/re_reference/def_schema.json', 'presets'):
        assert (package / name).exists(), name
    pending = [package / name for name in ('README.md', 'FIRST_LEVEL.md', 'EDITOR.md',
        'ENGINE_RULES.md', 'AUTOMATION.md', 'THIRD_PARTY.md')]
    visited, broken, links = set(), [], 0
    while pending:
        document = pending.pop()
        if document in visited: continue
        visited.add(document)
        for link in re.findall(r'\]\(([^)]+)\)', document.read_text(encoding='utf-8')):
            link = link.split('#')[0]
            if not link or re.match(r'[A-Za-z]+:', link): continue
            links += 1
            target = (document.parent / unquote(link)).resolve()
            if not target.is_file() or not target.is_relative_to(package):
                broken.append({'document': str(document.relative_to(package)), 'target': link})
            elif target.suffix == '.md': pending.append(target)
    (work / 'doc-links.json').write_text(json.dumps({'checked': links, 'broken': broken}, indent=2), encoding='utf-8')
    assert not broken, broken
    guide = package / 'AEON_CONTROLLER.html'
    assert guide.is_file()
    pictures = re.findall(r'<img[^>]+src="([^"]+)"', guide.read_text(encoding='utf-8'))
    assert len(pictures) == 4
    for picture in pictures:
        target = (package / picture).resolve()
        assert target.is_relative_to(package) and target.is_file(), picture
    print('packaged documentation links: PASS', links, flush=True)
    cwd = work / 'unrelated working directory'
    cwd.mkdir()
    env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', FORGE_PACKAGE_TOKEN='expanded')
    checks = []
    def run(label, command):
        result = subprocess.run(list(map(str, command)), cwd=cwd, env=env,
            capture_output=True, text=True, timeout=600)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        checks.append({'check': label, 'returncode': result.returncode})
        assert result.returncode == 0, result.stdout[-2000:] + result.stderr[-2000:]
        print(label + ': PASS', flush=True)
    run('defs', [package / 'forge-tools.exe', 'defs', 'list', args.root.resolve(), 'game.bin', 'OBJECT_HERO'])
    run('retail', [sys.executable, repo / 'tools/retail_smoke.py', '--exe', package / 'forge.exe',
        '--count', '8', '--install', args.root.resolve(), '--out', work / 'exports'])
    run('gui', [sys.executable, repo / 'tools/ui_smoke.py', '--exe', package / 'FableForge.exe',
        '--install', args.root.resolve(), '--cwd', cwd])
    run('mod_paths', [sys.executable, repo / 'tools/test_mod_gui_paths.py', '--gui', package / 'FableForge.exe',
        '--root', args.root.resolve(), '--cwd', cwd])
    with archive.open('rb') as stream:
        archive_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
    (work / 'result.json').write_text(json.dumps({'archive': str(archive), 'sha256': archive_hash,
        'package': str(package), 'checks': checks, 'passed': True}, indent=2), encoding='utf-8')
    print('Extracted package, retail exports, GUI pixels and literal-path mod workflow: PASS')


if __name__ == '__main__':
    main()
