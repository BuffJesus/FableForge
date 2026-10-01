#!/usr/bin/env python3
"""Unreadable conflict choices must not replace an existing deployment."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
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
    parser.add_argument('--tool', type=Path, default=repo / 'build/forge-tools.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='mod-picks-inputs-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    for name in ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin'):
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.root / name, target)
    shared = root / 'data/Misc/shared.txt'
    shared.parent.mkdir(parents=True)
    shared.write_bytes(b'original shared file')
    packs = {}
    for tag in ('A', 'B', 'C'):
        pack = work / ('Pack' + tag)
        (pack / 'data/Misc').mkdir(parents=True)
        (pack / 'forge_pack.json').write_text(json.dumps({'version': 1, 'name': 'Pack' + tag,
            'models': [], 'groundThemes': []}), encoding='utf-8')
        (pack / 'data/Misc/shared.txt').write_text('from pack ' + tag, encoding='utf-8')
        packs[tag] = pack
    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    def run(label, verb, options, expected=0):
        result = subprocess.run([str(args.tool.resolve()), 'mods', verb, str(root), *map(str, options)],
            cwd=work, capture_output=True, text=True, timeout=180)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        (work / (label + '.json')).write_text(json.dumps({'verb': verb, 'options': list(map(str, options)),
            'returncode': result.returncode, 'expected': expected}, indent=2), encoding='utf-8')
        assert result.returncode == expected, (verb, options, result.stdout, result.stderr)
        return result.stdout
    for tag in ('A', 'B'):
        run('add_' + tag, 'add', [packs[tag], '--name', 'Pack' + tag])
    original = snapshot()
    picks = work / 'choices.txt'
    picks.write_text('file:data/misc/shared.txt\tPackA\n', encoding='utf-8')
    run('deploy', 'deploy', ['--picks', picks])
    assert shared.read_text() == 'from pack A'
    deployed = snapshot()
    missing = work / 'missing-picks.txt'
    directory = work / 'directory-picks'
    directory.mkdir()
    out = work / 'output'
    out.mkdir()
    (out / 'sentinel').write_bytes(b'previous build')
    def refused(label, path):
        for verb in ('deploy', 'conflicts', 'build'):
            options = ([out] if verb == 'build' else []) + ['--picks', path]
            run(label + '_' + verb, verb, options, 1)
            assert snapshot() == deployed
            assert [item.name for item in out.iterdir()] == ['sentinel']
            assert (out / 'sentinel').read_bytes() == b'previous build'
    refused('missing', missing)
    refused('directory', directory)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(picks), 0x80000000, 0, None, 3, 0x80, None)
    assert handle != wintypes.HANDLE(-1).value
    try:
        refused('locked', picks)
    finally:
        kernel.CloseHandle(handle)
    default = root / 'forge_mods_picks.txt'
    default.mkdir()
    run('default_directory', 'deploy', [], 1)
    assert snapshot() == deployed
    default.rmdir()
    order = root / 'forge_mods.json'
    saved = order.read_bytes()
    order.write_bytes(b'{malformed order')
    malformed = snapshot()
    run('malformed_order', 'deploy', ['--picks', picks], 1)
    assert snapshot() == malformed
    order.write_bytes(saved)
    for verb, options in [('deploy', ['--picks', picks]), ('list', ['--json']),
                          ('add', [packs['C'], '--name', 'PackC'])]:
        handle = kernel.CreateFileW(str(order), 0x80000000, 6, None, 3, 0x80, None)
        assert handle != wintypes.HANDLE(-1).value
        try:
            run('unreadable_order_' + verb, verb, options, 1)
        finally:
            kernel.CloseHandle(handle)
        assert snapshot() == deployed
    changed = json.loads(saved)
    changed['mods'][1]['source'] = str(work / 'missing-mod')
    order.write_text(json.dumps(changed), encoding='utf-8')
    missing_source = snapshot()
    for verb in ('deploy', 'conflicts', 'build'):
        options = ([out] if verb == 'build' else []) + ['--picks', picks]
        run('missing_source_' + verb, verb, options, 1)
        assert snapshot() == missing_source
        assert [item.name for item in out.iterdir()] == ['sentinel']
    order.write_bytes(saved)
    run('retry', 'deploy', ['--picks', picks])
    assert shared.read_text() == 'from pack A'
    run('undeploy', 'undeploy', [])
    assert snapshot() == original
    run('without_picks', 'deploy', [])
    assert shared.read_text() == 'from pack B'
    run('final_undeploy', 'undeploy', [])
    assert snapshot() == original
    # The shared reader also serves the direct definition and quest merge commands.
    quest_out = work / 'merged.qst'
    quest_out.write_bytes(b'previous quest output')
    for family, arguments in (
        ('qst', [work / 'base.qst', quest_out, work / 'mod.qst']),
        ('defs', [root, out, 'game.bin', packs['A']]),
    ):
        result = subprocess.run([str(args.tool.resolve()), family, 'merge', *map(str, arguments),
            '--picks', str(missing)], cwd=work, capture_output=True, text=True, timeout=60)
        (work / (family + '_missing.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert result.returncode == 1 and 'conflict choices' in result.stderr, result.stderr
        assert quest_out.read_bytes() == b'previous quest output'
        assert [item.name for item in out.iterdir()] == ['sentinel']
        assert snapshot() == original
    print('Unreadable picks, malformed order, deployed-stage preservation and valid retry: PASS')


if __name__ == '__main__':
    main()
