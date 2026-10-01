#!/usr/bin/env python3
"""Invalid load-order CLI options must not edit metadata or undo a deployed stage."""
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
    parser.add_argument('--tool', type=Path, default=repo / 'build/forge-tools.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='mod-cli-inputs-', dir=repo / 'build')).resolve()
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
    run('deploy', 'deploy', [])
    assert shared.read_text() == 'from pack B'
    deployed = snapshot()
    invalid = [('undeploy', ['--typo']), ('deploy', ['--typo']), ('deploy', ['--picks']),
               ('deploy', ['--picks', '']), ('deploy', ['--picks', '--json']),
               ('undeploy', ['extra']), ('list', ['--typo']), ('list', ['extra']),
               ('remove', ['PackA', 'extra']), ('enable', ['PackA', '--typo']),
               ('disable', ['PackA', 'extra']), ('move', ['PackB', '0', 'extra'])]
    for value in ('0tail', '1.5', '', '+-1', '2147483648'):
        invalid += [('move', ['PackB', value]), ('add', [packs['C'], '--at', value])]
    invalid += [('add', [packs['C'], *options]) for options in
                (['--name'], ['--note'], ['--at'], ['--typo'], ['extra'], ['--name', ''], ['--note', '--json'])]
    invalid += [('conflicts', options) for options in (['--typo'], ['--picks'], ['--picks', '--json'])]
    invalid += [('build', [work / 'built', *options]) for options in
                (['--typo'], ['--fields'], ['--picks'], ['--picks', ''], ['--fields', '--json'])]
    for index, (verb, options) in enumerate(invalid):
        run('invalid_' + str(index), verb, options, 2)
        assert snapshot() == deployed, (verb, options)
    listing = json.loads(run('list_json', 'list', ['--json']))
    assert len(listing['mods']) == 2
    picks = work / 'choices.txt'
    picks.write_text('# use load order\n', encoding='utf-8')
    report = json.loads(run('conflicts_json', 'conflicts', ['--json', '--picks', picks]))
    assert [source['label'] for source in report['sources']] == ['PackA', 'PackB']
    assert report['picks'] == str(picks)
    assert snapshot() == deployed
    run('build_valid', 'build', [work / 'built', '--json', '--picks', picks])
    assert snapshot() == deployed
    run('undeploy', 'undeploy', [])
    assert snapshot() == original
    run('add_at', 'add', [packs['C'], '--at', '+0', '--name', 'PackC', '--note', 'valid note'])
    saved = json.loads((root / 'forge_mods.json').read_text())['mods']
    assert saved[0]['name'] == 'PackC' and saved[0]['note'] == 'valid note'
    run('move_valid', 'move', ['PackC', '+2'])
    run('remove_valid', 'remove', ['PackC'])
    assert snapshot() == original
    print('Mod CLI strict inputs, deployed-stage preservation and valid command retries: PASS', len(invalid))


if __name__ == '__main__':
    main()
