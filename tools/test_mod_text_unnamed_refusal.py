#!/usr/bin/env python3
"""Changed unnamed text records refuse composition; originals stay intact."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot
from test_mod_asset_choice_contributors import bank
from test_fmp_texture_layers import entries
from test_mod_text_choice_contributors import text_bank
from test_fmp_legacy_text_bank import decoded


def duplicate_bank(path, name, records):
    """Independent writer for a repeated unnamed TOC occurrence."""
    raw = bytearray(b'BIGB'+struct.pack('<3I', 0, 0, 0)); offsets = []
    for symbol, record in records:
        offsets.append(len(raw)); raw += record['payload']
    start = len(raw); raw += struct.pack('<I', 0)
    for (symbol, record), offset in zip(records, offsets):
        text = symbol.encode('ascii')
        raw += struct.pack('<7I', 0, record['id'], record['type'], len(record['payload']),
                           offset, 0, len(text))+text
        raw += struct.pack('<3I', 0, 0, len(record['header']))+record['header']
    size = len(raw)-start; directory = len(raw)
    raw += struct.pack('<I', 1)+name.encode('ascii')+b'\0'
    raw += struct.pack('<5I', 0, len(records), start, size, 1)
    struct.pack_into('<I', raw, 8, directory)
    path.write_bytes(raw)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--complete-data', action='store_true')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-text-unnamed-refusal-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4))
    text_bank(root, 'English', {'SHARED': 'original'})
    rel = Path('data/lang/English/text.big'); name = 'TEXT_ENGLISH_MAIN'
    original = entries(root/rel, name, None, True)
    original[''] = dict(original['SHARED'], id=2, header=b'opaque metadata')
    bank(root/rel, name, original)
    source = work/'Edited'; text_bank(source, 'English', {'SHARED': 'modified'})
    edited = entries(source/rel, name, None, True)
    edited[''] = dict(original[''])
    bank(source/rel, name, edited)
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
            capture_output=True, text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        for binary in ('script.bin', 'frontend.bin'):
            (complete/'data/CompiledDefs'/binary).write_bytes((complete/'data/CompiledDefs/game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    run('mods', 'add', root, source)
    picks = work/'picks.txt'; picks.write_text('')
    checks = []
    def command(route, output):
        cmd = ['mods', route, root]
        if route != 'conflicts': cmd += [output]
        if route == 'merge':
            cmd += ['--with', *([complete] if complete else []), source]
            if complete: cmd += ['--install-data']
        return cmd + ['--picks', picks, '--json']
    cases = [('changed payload', dict(original[''], payload=edited['SHARED']['payload']), name),
             ('new ID', dict(original[''], id=3), name),
             ('changed type', dict(original[''], type=1), name),
             ('changed metadata', dict(original[''], header=b'changed opaque metadata'), name),
             ('different bank', dict(original['']), 'TEXT_ENGLISH_OTHER'),
             ('new unnamed group', dict(original[''], id=3, type=1, payload=struct.pack('<2I', 1, 1)), name),
             ('duplicate occurrence', dict(original['']), name)]
    for route in ('merge', 'build', 'conflicts'):
        output = work/route
        bank(source/rel, name, edited); picks.write_text('')
        run(*command(route, output))
        if route != 'conflicts':
            actual = entries(output/rel, name, None, True)
            assert decoded(actual['SHARED']['payload'])[0] == 'modified'
            assert actual['']['payload'] == original['']['payload']
            assert actual['']['header'] == original['']['header'] and actual['']['id'] == 2
            (output/'sentinel').write_bytes(b'previous result')
        checks.append({'route': route, 'unchanged_unnamed_preserved': True})
        for label, entry, bank_name in cases:
            changed = dict(edited); changed[''] = entry
            bank(source/rel, bank_name, changed)
            if label == 'duplicate occurrence':
                duplicate_bank(source/rel, bank_name, [('SHARED', edited['SHARED']), ('', entry), ('', entry)])
            before = snapshot(output) if output.exists() else {}
            inputs = {str(p): snapshot(p) for p in [root, source] + ([complete] if complete else [])}
            for winner in ('', 'vanilla'):
                picks.write_text('text:English|SHARED\tvanilla\n' if winner else '')
                failure = run(*command(route, output), good=False)
                assert 'English/unnamed ID '+str(entry['id'])+' in '+bank_name+' from Edited' in failure.stderr
                assert 'unnamed entry changes cannot be composed' in failure.stderr
                assert (snapshot(output) if output.exists() else {}) == before
                assert inputs == {str(p): snapshot(p) for p in [root, source] + ([complete] if complete else [])}
                checks.append({'route': route, 'case': label, 'named_pick': winner, 'refused_preserved': True})
            (work/(route+'-'+label.replace(' ', '-')+'.log')).write_text(failure.stderr)
        picks.write_text(''); bank(source/rel, name, edited)
    if complete:
        (complete/rel).parent.mkdir(parents=True, exist_ok=True)
        (complete/rel).write_bytes((root/rel).read_bytes()); (root/rel).unlink()
        output = work/'complete-snapshot'
        run(*command('build', output))
        actual = entries(output/rel, name, None, True)
        assert actual['']['payload'] == original['']['payload'] and actual['']['header'] == original['']['header']
        checks.append({'case': 'complete snapshot introduces unchanged unnamed record', 'preserved': True})
    (work/'result.json').write_text(json.dumps({'ok': True, 'checks': checks,
        'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'unnamed text refusal/preservation checks')


if __name__ == '__main__': main()
