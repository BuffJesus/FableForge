#!/usr/bin/env python3
"""Check concurrent mod commands on separate owned scratch installs and temp cleanup."""
import argparse
import bz2
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', default=os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters'))
    args = ap.parse_args()
    repo = Path(__file__).resolve().parents[1]
    source = Path(args.root)
    banks = ['data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin']
    if not all((source / rel).is_file() for rel in banks):
        raise SystemExit('workspace test requires game.bin and names.bin')
    work = Path(tempfile.mkdtemp(prefix='mod-workspaces-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    temp = work / 'temp'
    temp.mkdir()
    env = dict(os.environ, TEMP=str(temp), TMP=str(temp))
    sentinels = []
    for name in ('forge_mods_merge', 'forge_mods_deploy', 'forge_mods_conflicts', 'forge_patch_probe'):
        path = temp / name / 'owned_elsewhere.txt'
        path.parent.mkdir()
        path.write_text('another command owns this folder')
        sentinels.append(path)
    def check_temp():
        for path in sentinels:
            assert path.is_file() and path.read_text() == 'another command owns this folder', f'unowned temp content removed: {path}'
        owned = temp / 'FableForge'
        assert not owned.exists() or not list(owned.iterdir()), f'command leaked owned workspace: {owned}'
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    roots, packs, originals = [], [], []
    for tag in ('A', 'B'):
        root, pack = work / ('install' + tag), work / ('pack' + tag)
        for rel in banks:
            target = root / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / rel, target)
        (pack / 'data/Misc').mkdir(parents=True)
        (pack / 'data/Misc/workspace.txt').write_text('pack ' + tag)
        (pack / 'forge_pack.json').write_text(json.dumps({'version': 1, 'name': tag,
            'models': [], 'groundThemes': []}))
        roots.append(root)
        packs.append(pack)
        originals.append({rel: digest(root / rel) for rel in banks})
    def run(index, verb, *extra, expected=0, label=''):
        result = subprocess.run([str(repo / 'build/forge-tools.exe'), 'mods', verb,
            str(roots[index]), *map(str, extra)], cwd=repo, env=env,
            capture_output=True, text=True, timeout=120)
        (work / (label + '.log' if label else f'{index}_{verb}_{expected}.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        assert (result.returncode == 0) == (expected == 0), result.stdout + result.stderr
        return result
    # A no-op BSDIFF40 still exercises the parsed BIN probe in each process.
    identity = work / 'identity' / 'game.bin.patch'
    identity.parent.mkdir()
    size = (source / banks[0]).stat().st_size
    with (source / banks[0]).open('rb') as stream:
        stream.seek(-1, 2)
        last = stream.read(1)
    control = bz2.compress(struct.pack('<QQQ', size - 1, 1, 0))
    difference = bz2.compress(bytes(size - 1))
    extra = bz2.compress(last)
    identity.write_bytes(b'BSDIFF40' + struct.pack('<QQQ', len(control), len(difference), size)
                         + control + difference + extra)
    for index, pack in enumerate(packs):
        run(index, 'add', pack, '--name', f'Pack{index}')
        run(index, 'add', identity, '--name', 'BaselinePatch')
    with ThreadPoolExecutor(max_workers=2) as pool:
        reports = list(pool.map(lambda index: run(index, 'conflicts', '--json'), range(2)))
        for index, result in enumerate(reports):
            report = json.loads(result.stdout[result.stdout.index('{'):])
            assert report['sources'][0]['label'] == f'Pack{index}', 'conflict report used the other process source'
        check_temp()
        for index, root in enumerate(roots):
            assert originals[index] == {rel: digest(root / rel) for rel in banks}, 'conflicts wrote source banks'
        list(pool.map(lambda index: run(index, 'deploy'), range(2)))
        check_temp()
        for index, root in enumerate(roots):
            assert (root / 'data/Misc/workspace.txt').read_text() == 'pack ' + ('A', 'B')[index], 'deployment crossed workspaces'
        list(pool.map(lambda index: run(index, 'undeploy'), range(2)))
    for index, root in enumerate(roots):
        assert originals[index] == {rel: digest(root / rel) for rel in banks}, 'undeploy changed original banks'
        assert not (root / 'data/Misc/workspace.txt').exists(), 'undeploy retained added file'
    broken = work / 'game.bin.patch'
    broken.write_bytes(b'not a valid patch')
    run(0, 'add', broken, '--name', 'BrokenPatch')
    failure = run(0, 'conflicts', '--json', expected=1, label='invalid_patch')
    assert 'patch too small' in failure.stderr, failure.stderr
    check_temp()
    run(0, 'remove', 'BrokenPatch')
    # Valid patch framing that produces an invalid BIN reaches probe cleanup.
    control = bz2.compress(struct.pack('<QQQ', 0, 4, 0))
    difference = bz2.compress(b'')
    extra = bz2.compress(b'bad!')
    broken.write_bytes(b'BSDIFF40' + struct.pack('<QQQ', len(control), len(difference), 4)
                       + control + difference + extra)
    run(0, 'add', broken, '--name', 'InvalidBin')
    failure = run(0, 'conflicts', '--json', expected=1, label='invalid_bin')
    assert 'result does not parse' in failure.stderr, failure.stderr
    check_temp()
    print('concurrent conflicts/deploy/undeploy and failure cleanup: PASS; evidence retained at', work)


if __name__ == '__main__':
    main()
