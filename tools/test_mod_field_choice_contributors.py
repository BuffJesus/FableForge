#!/usr/bin/env python3
"""Unavailable contested scalar choices refuse writes and remain reviewable."""
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
    work = Path(tempfile.mkdtemp(prefix='mod-field-choice-contributors-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4))
    sources = []
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
    for name, values in [('PackA', (10, 20, 3, 4)), ('PackB', (11, 2, 30, 5)), ('PackC', (1, 21, 31, 4))]:
        source = work/name; write_bank(source, values); sources.append(source)
        run('mods', 'add', root, source)
    inputs = [root, *sources] + ([complete] if complete else [])
    before_inputs = {str(p): snapshot(p) for p in inputs}
    picks = work/'picks.txt'
    report = json.loads(run('mods', 'conflicts', root, '--json').stdout)
    field = report['defs']['conflicts'][0]['field_conflicts'][0]
    key = field['choice_key']
    assert field['mods'] == ['PackA', 'PackB']
    checks = []
    for route in ('merge', 'build', 'defs'):
        output = work/route
        if route == 'defs':
            command = ['defs', 'merge', root, output, 'game.bin', *sources,
                       '--fields', repo/'docs/re_reference/def_schema.json']
        else:
            command = ['mods', route, root, output]
            if route == 'merge':
                command += ['--with', *([complete] if complete else []), *sources]
                if complete: command += ['--install-data']
        command += ['--picks', picks]
        picks.write_text(''); run(*command)
        (output/'sentinel').write_bytes(b'prior output')
        for winner in ('RemovedPack', 'PackC'):
            for record_choice in ('', RECORD+'\tPackA\n'):
                picks.write_text(record_choice+key+'\t'+winner+'\n')
                before = snapshot(output); saved = picks.read_bytes()
                failed = run(*command, good=False)
                assert 'selects unavailable contributor '+winner in failed.stderr
                assert snapshot(output) == before and picks.read_bytes() == saved
                (work/f'{route}-{winner}-{bool(record_choice)}.log').write_text(failed.stderr)
                report = json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
                field = report['defs']['conflicts'][0]['field_conflicts'][0]
                assert field['choice_unavailable'] and field['mods'] == ['PackA', 'PackB']
                assert field['winner'] == ('PackA' if record_choice else 'PackB')
                assert snapshot(output) == before and picks.read_bytes() == saved
                checks.append({'route': route, 'winner': winner, 'record_choice': bool(record_choice),
                               'refused_preserved_reviewable': True})
        for winner, expected in [('PackA', 10), ('vanilla', 1), ('base', 1), ('none', 1), ('-', 1)]:
            picks.write_text(key+'\t'+winner+'\n'); run(*command)
            assert read_values(output) == (expected, 21, 31, 5)
            assert (output/'sentinel').read_bytes() == b'prior output'
            checks.append({'route': route, 'winner': winner, 'accepted_exact_values': True})
        identity = json.loads(key[6:]); identity[3] = 'inactive-layout'
        picks.write_text('field:'+json.dumps(identity, separators=(',', ':'))+'\tRemovedPack\n')
        run(*command); assert read_values(output) == (11, 21, 31, 5)
        checks.append({'route': route, 'case': 'inactive layout choice', 'accepted': True})
        picks.write_text(''); run(*command)
        assert read_values(output) == (11, 21, 31, 5)
        checks.append({'route': route, 'case': 'clear', 'accepted': True})
    agreeing = work/'Agreeing'; write_bank(agreeing, (10, 22, 3, 4))
    for scenario, surviving, expected, contributors in [
        ('single-record-writer', [sources[0]], (10, 20, 3, 4), ['PackA']),
        ('single-field-writer', [sources[0], sources[2]], (10, 21, 31, 4), ['PackA']),
        ('agreeing-writers', [sources[0], agreeing], (10, 22, 3, 4), ['PackA', 'Agreeing']),
        ('no-field-writer', [sources[2]], (1, 21, 31, 4), []),
    ]:
        collapsed = work/scenario; write_bank(collapsed, (1, 2, 3, 4))
        for source in surviving: run('mods', 'add', collapsed, source)
        for route in ('merge', 'build', 'defs'):
            output = work/(scenario+'-'+route)
            command = (['defs', 'merge', collapsed, output, 'game.bin', *surviving,
                        '--fields', repo/'docs/re_reference/def_schema.json'] if route == 'defs'
                       else ['mods', route, collapsed, output])
            if route == 'merge': command += ['--with', *surviving]
            command += ['--picks', picks]
            picks.write_text(''); run(*command)
            assert read_values(output) == expected
            (output/'sentinel').write_bytes(b'previous collapsed output')
            before = snapshot(output)
            picks.write_text(key+'\tRemovedPack\n'); saved = picks.read_bytes()
            failed = run(*command, good=False)
            assert 'selects unavailable contributor RemovedPack' in failed.stderr
            assert snapshot(output) == before and picks.read_bytes() == saved
            review = json.loads(run('mods', 'conflicts', collapsed, '--picks', picks, '--json').stdout)
            selected = next(f for c in review['defs']['conflicts'] for f in c['field_conflicts']
                            if f['choice_key'] == key)
            assert selected['choice_unavailable'] and selected['mods'] == contributors
            picks.write_text(key+'\tvanilla\n'); run(*command)
            assert read_values(output) == (1, *expected[1:])
            if contributors:
                picks.write_text(key+'\t'+contributors[-1]+'\n'); run(*command)
                assert read_values(output) == expected
            identity = json.loads(key[6:]); identity[3] = 'inactive-layout'
            picks.write_text('field:'+json.dumps(identity, separators=(',', ':'))+'\tRemovedPack\n')
            run(*command); assert read_values(output) == expected
            checks.append({'route': route, 'case': scenario, 'refused_preserved_reviewable': True,
                           'baseline_and_available_choices': True, 'inactive_layout_ignored': True})
    assert before_inputs == {str(p): snapshot(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'inputs_unchanged': True,
                                               'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'scalar choice contributor checks')


if __name__ == '__main__': main()
