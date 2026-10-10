#!/usr/bin/env python3
"""Text composition refuses unapplied entries and preserves prior outputs."""
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


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--complete-data', action='store_true')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-text-payload-refusal-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4))
    rel = Path('data/lang/English/text.big'); bank_name = 'TEXT_ENGLISH_MAIN'
    text_bank(root, 'English', {'SHARED': 'original', 'OTHER': 'unchanged'})
    original = entries(root/rel, bank_name, None, True)
    original['GROUP'] = {'id': 3, 'type': 1, 'header': b'', 'payload': struct.pack('<2I', 1, 1)}
    bank(root/rel, bank_name, original)
    valid = work/'Valid'; text_bank(valid, 'English', {'SHARED': 'alternate', 'OTHER': 'unchanged'})
    bad = work/'Broken'; picks = work/'picks.txt'; picks.write_text('')
    checks = []
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)], capture_output=True,
                                text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        defs = complete/'data/CompiledDefs'
        for name in ('script.bin', 'frontend.bin'):
            (defs/name).write_bytes((defs/'game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    bank(bad/rel, bank_name, original)
    for source in (valid, bad): run('mods', 'add', root, source)
    sources = ([complete] if complete else []) + [valid, bad]
    def command(route, output):
        cmd = ['mods', route, root]
        if route != 'conflicts': cmd += [output]
        if route == 'merge':
            cmd += ['--with', *sources]
            if complete: cmd += ['--install-data']
        return cmd + ['--picks', picks, '--json']
    # Each failure follows a successful publication; rejection must not touch its bytes.
    cases = [('truncated string', 'SHARED', 0, b'\0', 'could not be applied'),
             ('invalid UTF16', 'SHARED', 0, b'\0\xdc\0\0', 'unexpected UTF-16 low surrogate'),
             ('truncated metadata', 'SHARED', 0, b'x\0\0\0'+struct.pack('<I', 999), 'could not be applied'),
             ('new malformed string', 'NEW', 0, b'\0', 'could not be applied'),
             ('group edit', 'GROUP', 1, struct.pack('<2I', 1, 2), 'unsupported type 1'),
             ('narrator edit', 'SHARED', 2, struct.pack('<I', 2), 'unsupported type 2'),
             ('unknown type', 'SHARED', 99, b'changed', 'unsupported type 99'),
             ('type-only change', 'SHARED', 1, original['SHARED']['payload'], 'unsupported type 1')]
    for route in ('merge', 'build', 'conflicts'):
        output = work/route
        bank(bad/rel, bank_name, original); picks.write_text('')
        run(*command(route, output))
        if route != 'conflicts':
            (output/'sentinel').write_bytes(b'prior successful publication')
            group = entries(output/rel, bank_name, {'GROUP'}, True)['GROUP']
            assert group['payload'] == original['GROUP']['payload'] and group['id'] == 3
        for label, name, kind, payload, reason in cases:
            changed = dict(original)
            changed[name] = {'id': 4 if name == 'NEW' else original[name]['id'],
                             'type': kind, 'header': b'', 'payload': payload}
            bank(bad/rel, bank_name, changed)
            before = snapshot(output) if output.exists() else {}
            inputs = {str(p): snapshot(p) for p in [root, *sources]}
            failed = run(*command(route, output), good=False)
            assert 'mods text entry: English/'+name+' from Broken' in failed.stderr and reason in failed.stderr
            assert (snapshot(output) if output.exists() else {}) == before
            assert inputs == {str(p): snapshot(p) for p in [root, *sources]}
            (work/(route+'-'+label.replace(' ', '-')+'.log')).write_text(failed.stderr)
            checks.append({'route': route, 'case': label, 'refused_preserved': True})
        # A malformed losing candidate must not prevent an explicit valid selection.
        bank(bad/rel, bank_name, {'SHARED': dict(original['SHARED'], payload=b'\0')})
        for winner, expected in [('Valid', 'alternate'), ('vanilla', 'original')]:
            picks.write_text('text:English|SHARED\t'+winner+'\n')
            run(*command(route, output))
            if route != 'conflicts':
                shared = entries(output/rel, bank_name, {'SHARED'}, True)['SHARED']
                assert decoded(shared['payload'])[0] == expected
                assert (output/'sentinel').read_bytes() == b'prior successful publication'
            checks.append({'route': route, 'winner': winner, 'malformed_loser_ignored': True})
        picks.write_text(''); bank(bad/rel, bank_name, original)
        baseline = (root/rel).read_bytes()
        try:
            (root/rel).unlink()
            before = snapshot(output) if output.exists() else {}
            failure = run(*command(route, output), good=False)
            assert 'mods text baseline: English/text.big is missing' in failure.stderr
            assert (snapshot(output) if output.exists() else {}) == before
            checks.append({'route': route, 'case': 'missing language baseline', 'refused_preserved': True})
        finally:
            (root/rel).write_bytes(baseline)
        broken_base = dict(original)
        broken_base['SHARED'] = dict(original['SHARED'], id=0)
        try:
            bank(root/rel, bank_name, broken_base)
            before = snapshot(output) if output.exists() else {}
            failure = run(*command(route, output), good=False)
            assert 'could not be applied: textbig: requested ID is zero or already in use' in failure.stderr
            assert (snapshot(output) if output.exists() else {}) == before
            checks.append({'route': route, 'case': 'unusable baseline ID', 'refused_preserved': True})
        finally:
            (root/rel).write_bytes(baseline)
    if complete:
        # The first complete snapshot may legitimately introduce a language bank;
        # its unedited groups are retained rather than numerically recomposed.
        (complete/rel).parent.mkdir(parents=True, exist_ok=True)
        (complete/rel).write_bytes((root/rel).read_bytes())
        (root/rel).unlink()
        output = work/'complete-language-baseline'
        run(*command('build', output))
        result_entries = entries(output/rel, bank_name, None, True)
        assert decoded(result_entries['SHARED']['payload'])[0] == 'alternate'
        assert result_entries['GROUP']['payload'] == original['GROUP']['payload']
        checks.append({'case': 'complete snapshot supplies language baseline', 'preserved_group': True})
    result = {'ok': True, 'complete_data': args.complete_data, 'checks': checks}
    (work/'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({'ok': True, 'checks': len(checks), 'work': str(work)}))


if __name__ == '__main__': main()
