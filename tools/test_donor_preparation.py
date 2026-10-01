#!/usr/bin/env python3
"""A malformed donor chunk must fail before minimap bank changes."""
import argparse
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
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='donor-prepare-', dir=repo / 'build')).resolve()
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
    unowned = root / 'data/graphics/pc/textures.big.atlas-tmp'
    unowned.write_bytes(b'unrelated minimap output')
    def snapshot():
        result = {}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(root).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return result
    original = snapshot()
    stb = root / 'data/Levels/FinalAlbion_RT.stb'
    bad_payload = work / 'bad_chunk.bin'
    bad_payload.write_bytes(bytes(16))
    bad_stb = work / 'bad.stb'
    replaced = subprocess.run([str(args.exe.resolve().with_name('forge-tools.exe')), 'stb', 'replace',
        str(stb), str(bad_stb), r'Data\Levels\FinalAlbion\TeleporterGreatwood.lev', str(bad_payload)],
        cwd=repo, capture_output=True, text=True, timeout=180)
    (work / 'fixture.log').write_text(replaced.stdout + replaced.stderr, encoding='utf-8')
    assert replaced.returncode == 0, replaced.stdout + replaced.stderr
    assert stb.resolve().is_relative_to(root.resolve()) and bad_stb.resolve().is_relative_to(work)
    bad_stb.replace(stb)
    damaged = snapshot()
    command = ['new-level', 'TeleporterGreatwood', 'PreparedDonorProbe', '--at', '6400,6400', '--own-region', 'new']
    def run(label, command):
        result = subprocess.run([str(args.exe.resolve()), *command, '--install', str(root)],
            cwd=repo, capture_output=True, text=True, timeout=240)
        (work / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result
    result = run('damaged', command)
    after = snapshot()
    report = {'returncode': result.returncode,
              'changed_originals': [name for name, value in damaged.items() if after.get(name) != value],
              'extra_files': sorted(set(after) - set(damaged))}
    (work / 'failure_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    assert result.returncode != 0 and 'terrain chunk' in result.stderr and after == damaged, report
    shutil.copyfile(args.root / 'data/Levels/FinalAlbion_RT.stb', stb)
    assert snapshot() == original
    result = run('retry', command)
    assert result.returncode == 0 and 'minimap:' in result.stdout and 'installed: map slot' in result.stdout, result.stdout + result.stderr
    restored = run('restore', ['restore', '--forget'])
    assert restored.returncode == 0 and snapshot() == original, restored.stdout + restored.stderr
    print('Donor chunk preparation refusal, unchanged minimap banks, retry and exact Restore: PASS')


if __name__ == '__main__':
    main()
