#!/usr/bin/env python3
"""Duplicate named source entries cannot identify a unique text contribution."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot
from test_mod_asset_choice_contributors import bank
from test_mod_text_unnamed_refusal import duplicate_bank
from test_fmp_texture_layers import entries
from test_mod_text_choice_contributors import text_bank


def across_banks(path, groups):
    """Independent compact BIG writer for symbols repeated across sub-banks."""
    raw = bytearray(b'BIGB'+struct.pack('<3I', 0, 0, 0)); directory_rows = []
    for index, (name, records) in enumerate(groups, 1):
        offsets = []
        for symbol, record in records:
            offsets.append(len(raw)); raw += record['payload']
        start = len(raw); raw += struct.pack('<I', 0)
        for (symbol, record), offset in zip(records, offsets):
            text = symbol.encode('ascii')
            raw += struct.pack('<7I', 0, record['id'], record['type'], len(record['payload']), offset, 0, len(text))+text
            raw += struct.pack('<3I', 0, 0, len(record['header']))+record['header']
        directory_rows.append(name.encode('ascii')+b'\0'+struct.pack('<5I', 0, len(records), start, len(raw)-start, index))
    directory = len(raw); raw += struct.pack('<I', len(groups))+b''.join(directory_rows)
    struct.pack_into('<I', raw, 8, directory); path.write_bytes(raw)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--complete-data', action='store_true')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-text-duplicate-refusal-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4))
    rel = Path('data/lang/English/text.big'); name = 'TEXT_ENGLISH_MAIN'
    text_bank(root, 'English', {'SHARED': 'original', 'OTHER': 'untouched'})
    original = entries(root/rel, name, None, True)
    source = work/'Ambiguous'; text_bank(source, 'English', {'SHARED': 'first', 'OTHER': 'second'})
    changed = entries(source/rel, name, None, True)
    checks = []; counter = 0
    def run(*arguments, good=True):
        nonlocal counter
        counter += 1
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
            capture_output=True, text=True, timeout=120)
        (work/f'command-{counter:03d}.json').write_text(json.dumps({
            'arguments': list(map(str, arguments)), 'returncode': result.returncode,
            'stdout': result.stdout, 'stderr': result.stderr}, indent=2))
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        for binary in ('script.bin', 'frontend.bin'):
            (complete/'data/CompiledDefs'/binary).write_bytes((complete/'data/CompiledDefs/game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    bank(source/rel, name, original); run('mods', 'add', root, source)
    picks = work/'picks.txt'; picks.write_text('')
    def command(route, output):
        cmd = ['mods', route, root]
        if route != 'conflicts': cmd += [output]
        if route == 'merge':
            cmd += ['--with', *([complete] if complete else []), source]
            if complete: cmd += ['--install-data']
        return cmd+['--picks', picks, '--json']
    cases = [('different values', [('SHARED', changed['SHARED']), ('SHARED', changed['OTHER'])]),
             ('identical values', [('SHARED', changed['SHARED']), ('SHARED', dict(changed['SHARED'], id=3))]),
             ('unchanged copies plus other edit', [('SHARED', original['SHARED']),
                ('SHARED', dict(original['SHARED'], id=3)), ('OTHER', changed['OTHER'])]),
             ('new symbol', [('NEW', changed['SHARED']), ('NEW', changed['OTHER'])]),
             ('same numeric ID', [('SHARED', changed['SHARED']), ('SHARED', dict(changed['OTHER'], id=1))]),
             ('across sub-banks', [('SHARED', changed['SHARED']), ('SHARED', changed['OTHER'])])]
    for route in ('merge', 'build', 'conflicts'):
        output = work/route; bank(source/rel, name, original); picks.write_text('')
        run(*command(route, output))
        if route != 'conflicts': (output/'sentinel').write_bytes(b'prior output')
        for label, records in cases:
            if label == 'across sub-banks':
                across_banks(source/rel, [(name, records[:1]), ('SECOND_TEXT_BANK', records[1:])])
            else: duplicate_bank(source/rel, name, records)
            symbol = records[0][0]
            for choice in ('', 'vanilla', 'Ambiguous'):
                picks.write_text(f'text:English|{symbol}\t{choice}\n' if choice else '')
                before = snapshot(output) if output.exists() else {}
                sources = [root, source]+([complete] if complete else [])
                inputs = {str(p): snapshot(p) for p in sources}
                failure = run(*command(route, output), good=False)
                assert f'mods text entry: English/{symbol} from Ambiguous' in failure.stderr
                assert 'duplicate named entries cannot be composed' in failure.stderr
                assert before == (snapshot(output) if output.exists() else {})
                assert inputs == {str(p): snapshot(p) for p in sources}
                checks.append({'route': route, 'case': label, 'choice': choice, 'refused_preserved': True})
        # Names retain case: two distinct symbols remain valid contributors.
        distinct = [('SHARED', changed['SHARED']), ('shared', dict(changed['OTHER'], id=3))]
        duplicate_bank(source/rel, name, distinct); picks.write_text('')
        run(*command(route, output))
        if route != 'conflicts':
            actual = entries(output/rel, name, None, True)
            assert actual['SHARED']['payload'] == changed['SHARED']['payload']
            assert actual['shared']['payload'] == changed['OTHER']['payload']
            assert actual['OTHER']['payload'] == original['OTHER']['payload']
        checks.append({'route': route, 'case_sensitive_names_and_recovery': True})
    # A byte-identical bank needs no symbolic composition, even with duplicates.
    duplicate_bank(root/rel, name, cases[0][1]); (source/rel).write_bytes((root/rel).read_bytes())
    for route in ('merge', 'build', 'conflicts'):
        run(*command(route, work/route))
        checks.append({'route': route, 'identical_duplicate_bank_no_op': True})
    (work/'result.json').write_text(json.dumps({'ok': True, 'checks': checks, 'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'duplicate text refusal/recovery checks')


if __name__ == '__main__': main()
