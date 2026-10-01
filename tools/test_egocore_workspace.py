#!/usr/bin/env python3
"""Compile the real Controller Support text mod concurrently into separate roots."""
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
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge-tools.exe')
    parser.add_argument('--defc', type=Path, default=Path(os.environ.get('FORGE_DEFC',
        r'C:\Users\Cornelio\Documents\EgoCoreInspect\fable-defs\target\release\defc.exe')))
    parser.add_argument('--defs-text', type=Path, default=Path(os.environ.get('FORGE_DEFS_TEXT',
        r'D:\Documents\FableTLC\unified_build\UnifiedFable\Data\Defs')))
    args = parser.parse_args()
    mod = repo / 'work/nexus_mods/_peek/FableControllerSupport/Mods/FableControllerSupport'
    assert mod.is_dir() and args.defc.is_file() and args.defs_text.is_dir(), 'requires Controller Support corpus, defc and retail text defs'
    work = Path(tempfile.mkdtemp(prefix='egocore-workspace-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    temp = work / 'temp'
    marker = temp / 'forge_egocore/FableControllerSupport/unrelated.txt'
    marker.parent.mkdir(parents=True)
    marker.write_bytes(b'unrelated EgoCore workspace')
    env = dict(os.environ, TEMP=str(temp), TMP=str(temp), FORGE_DEFC=str(args.defc), FORGE_DEFS_TEXT=str(args.defs_text))
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    original = {name: digest(args.root / 'data/CompiledDefs' / name) for name in ('game.bin', 'names.bin')}
    roots = []
    for index in range(2):
        root = work / f'install_{index}'
        defs = root / 'data/CompiledDefs'
        defs.mkdir(parents=True)
        for name in original:
            shutil.copyfile(args.root / 'data/CompiledDefs' / name, defs / name)
        result = subprocess.run([str(args.exe.resolve()), 'mods', 'add', str(root), str(mod)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        roots.append(root)
    def build(index):
        output = work / f'output_{index}'
        result = subprocess.run([str(args.exe.resolve()), 'mods', 'build', str(roots[index]), str(output)],
            cwd=repo, env=env, capture_output=True, text=True, timeout=240)
        (work / f'build_{index}.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'egocore FableControllerSupport: 3 .def file(s)' in result.stdout, 'text layer was not compiled: ' + result.stdout
        values = {name: digest(output / 'data/CompiledDefs' / name) for name in original}
        assert values['game.bin'] != original['game.bin'], 'text overrides did not change definitions'
        assert (output / 'Mods/FableControllerSupport/FableControllerSupport.dll').read_bytes() == (mod / 'FableControllerSupport.dll').read_bytes()
        return values
    with ThreadPoolExecutor(max_workers=2) as pool:
        outputs = list(pool.map(build, range(2)))
    assert outputs[0] == outputs[1], 'concurrent compiles produced different definition banks'
    failed_compiler = work / 'fail-defc.cmd'
    failed_compiler.write_text('@echo off\necho intentional compiler failure\nexit /b 1\n')
    failed_env = dict(env, FORGE_DEFC=str(failed_compiler))
    failed = subprocess.run([str(args.exe.resolve()), 'mods', 'build', str(roots[0]), str(work / 'failure_output')],
        cwd=repo, env=failed_env, capture_output=True, text=True, timeout=60)
    (work / 'compiler_failure.log').write_text(failed.stdout + failed.stderr)
    assert 'its .def overrides were NOT applied' in failed.stderr and 'intentional compiler failure' in failed.stderr
    assert marker.is_file() and marker.read_bytes() == b'unrelated EgoCore workspace', 'normalization removed an unowned directory'
    assert not list((temp / 'forge_egocore').glob('defs-*')), 'normalization leaked an owned directory'
    for root in roots:
        assert all(digest(root / 'data/CompiledDefs' / name) == h for name, h in original.items())
    print('Concurrent real text-mod compiles, DLL staging and workspace ownership: PASS')


if __name__ == '__main__':
    main()
