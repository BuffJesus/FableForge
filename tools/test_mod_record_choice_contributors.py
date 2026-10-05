#!/usr/bin/env python3
"""Unavailable active definition-record choices refuse before publication."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from test_mod_field_details import write_bank, read_values, RECORD
from test_mod_file_choice_contributors import snapshot


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--complete-data', action='store_true')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-record-choice-contributors-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4))
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
                                capture_output=True, text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        defs = complete/'data/CompiledDefs'
        for name in ('script.bin', 'frontend.bin'):
            (defs/name).write_bytes((defs/'game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    sources = []
    for name, values in [('PackA', (10, 20, 3, 4)), ('PackB', (11, 2, 30, 5)), ('PackC', (1, 2, 3, 4))]:
        source = work/name; write_bank(source, values); sources.append(source)
        run('mods', 'add', root, source)
    inputs = [root, *sources] + ([complete] if complete else [])
    before_inputs = {str(p): snapshot(p) for p in inputs}
    original_banks = [{name: (source/'data/CompiledDefs'/name).read_bytes()
                       for name in ('game.bin', 'names.bin')} for source in sources]
    def bank(index, baseline=False):
        for name, data in original_banks[index].items():
            (sources[index]/'data/CompiledDefs'/name).write_bytes(
                (root/'data/CompiledDefs'/name).read_bytes() if baseline else data)
    picks = work/'picks.txt'; checks = []
    def review():
        return json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
    for route in ('merge', 'build', 'defs', 'defs-whole'):
        output = work/route
        if route.startswith('defs'):
            command = ['defs', 'merge', root, output, 'game.bin', *sources]
            if route == 'defs': command += ['--fields', repo/'docs/re_reference/def_schema.json']
        else:
            command = ['mods', route, root, output]
            if route == 'merge':
                command += ['--with', *([complete] if complete else []), *sources]
                if complete: command += ['--install-data']
        command += ['--picks', picks]
        picks.write_text(''); run(*command)
        (output/'sentinel').write_bytes(b'prior output')
        for winner in ('RemovedPack', 'PackC'):
            picks.write_text(RECORD+'\t'+winner+'\n'); before = snapshot(output); saved = picks.read_bytes()
            failed = run(*command, good=False)
            assert 'mods record choice:' in failed.stderr and 'selects unavailable contributor '+winner in failed.stderr
            row = review()['defs']['conflicts'][0]
            assert row['choice_unavailable'] and not row['overridden'] and row['mods'] == ['PackA', 'PackB']
            assert row['field_conflicts'][0]['winner'] == 'PackB'
            assert snapshot(output) == before and picks.read_bytes() == saved
            checks.append({'route': route, 'winner': winner, 'refused_preserved_reviewable': True})
        for winner, first in [('PackA', 10), ('PackB', 11), ('vanilla', 1), ('base', 1), ('none', 1), ('-', 1)]:
            picks.write_text(RECORD+'\t'+winner+'\n'); run(*command)
            expected = (first, 20, 30, 5)
            if route == 'defs-whole':
                expected = (10, 20, 3, 4) if winner == 'PackA' else (11, 2, 30, 5) if winner == 'PackB' else (1, 2, 3, 4)
            assert read_values(output) == expected
            checks.append({'route': route, 'winner': winner, 'accepted_exact_values': True})
        bank(1, baseline=True)
        try:
            picks.write_text(RECORD+'\tPackB\n'); before = snapshot(output)
            failed = run(*command, good=False)
            assert 'selects unavailable contributor PackB' in failed.stderr
            assert not review()['defs']['conflicts'] and snapshot(output) == before
            picks.write_text(RECORD+'\tPackA\n'); run(*command)
            assert read_values(output) == (10, 20, 3, 4)
            checks.append({'route': route, 'case': 'one remaining contributor', 'refusal_and_valid_choice': True})
            bank(0, baseline=True)
            picks.write_text(RECORD+'\tRemovedPack\n'); run(*command)
            assert read_values(output) == (1, 2, 3, 4)
            checks.append({'route': route, 'case': 'no current contributions', 'inactive': True})
        finally:
            bank(0); bank(1)
        picks.write_text('INACTIVE_RECORD\tRemovedPack\n'); run(*command)
        default_values = (11, 2, 30, 5) if route == 'defs-whole' else (11, 20, 30, 5)
        assert read_values(output) == default_values
        checks.append({'route': route, 'case': 'inactive record', 'accepted': True})
        picks.write_text(''); run(*command)
        assert read_values(output) == default_values and (output/'sentinel').read_bytes() == b'prior output'
        checks.append({'route': route, 'case': 'clear', 'accepted': True})
    assert before_inputs == {str(p): snapshot(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'inputs_unchanged': True,
                                               'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'record choice contributor checks')


if __name__ == '__main__': main()
