#!/usr/bin/env python3
"""Identical TNG copies retain real contributors and whole-file removals."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot
from test_mod_tng_choice_contributors import tng, values, RELATIVE, KEY


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--complete-data', action='store_true')
    parser.add_argument('--gui', type=Path)
    parser.add_argument('--install', type=Path)
    args = parser.parse_args()
    if args.gui and not args.install: parser.error('--gui requires --install')
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-tng-identical-contributors-', dir=args.work_root)).resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4)); tng(root)
    a, b, c = [work/name for name in ('PackA', 'PackB', 'PackC')]
    tng(a, 20, 40, unrelated=None)
    (b/RELATIVE).parent.mkdir(parents=True)
    (b/RELATIVE).write_bytes((a/RELATIVE).read_bytes())
    # Real derivative packs share a level while differing elsewhere. Exact
    # duplicate entire packages are correctly rejected by mods add.
    for source in (a, b):
        extra = source/'data/Misc'/(source.name+'.txt')
        extra.parent.mkdir(parents=True); extra.write_bytes(b'independent authored content')
    complete = None
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
            capture_output=True, text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        for binary in ('script.bin', 'frontend.bin'):
            (complete/'data/CompiledDefs'/binary).write_bytes((complete/'data/CompiledDefs/game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    for source in (a, b): run('mods', 'add', root, source)
    sources = ([complete] if complete else [])+[a, b]
    inputs = {str(p): snapshot(p) for p in [root, *sources]}
    picks = work/'picks.txt'; picks.write_text(''); checks = []
    def command(route, output, order=None):
        cmd = ['mods', route, root, output]
        if route == 'merge':
            cmd += ['--with', *(order if order is not None else sources)]
            if complete: cmd += ['--install-data']
        return cmd+['--picks', picks, '--json']
    def row(report): return report['tng']['levels'][0]
    def origin(output): return json.loads((output/'forge_mods_provenance.json').read_text())['levels']['Test.tng']
    for route in ('merge', 'build'):
        output = work/route; report = json.loads(run(*command(route, output)).stdout)
        assert row(report)['mode'] == 'single-editor' and row(report)['mods'] == ['PackA', 'PackB']
        assert values(output) == {123456: 20, 123458: 40} and origin(output)['uid:123456'] == 'PackB'
        assert (output/RELATIVE).read_bytes() == (b/RELATIVE).read_bytes()
        (output/'sentinel').write_bytes(b'previous publication')
        checks.append({'route': route, 'identical_copy_default_and_removal_preserved': True})
        for winner in ('PackA', 'PackB'):
            picks.write_text(KEY+'\t'+winner+'\n'); report = json.loads(run(*command(route, output)).stdout)
            conflict = row(report)['conflicts'][0]
            assert conflict['mods'] == ['PackA', 'PackB'] and conflict['winner'] == winner
            assert conflict['overridden'] and not conflict['choice_unavailable']
            assert values(output) == {123456: 20, 123458: 40}
            assert origin(output)['uid:123456'] == winner and origin(output)['uid:123458'] == 'PackB'
            checks.append({'route': route, 'valid_identical_contributor': winner})
        for key, expected in [(KEY, {123456: 10, 123458: 40}),
                              ('tng:Test.tng|uid:123458', {123456: 20})]:
            picks.write_text(key+'\tvanilla\n'); run(*command(route, output))
            assert values(output) == expected and (output/'sentinel').read_bytes() == b'previous publication'
            checks.append({'route': route, 'vanilla_original_or_absence': key})
        picks.write_text(KEY+'\tRemovedPack\n'); before = snapshot(output)
        failure = run(*command(route, output), good=False)
        assert 'selects unavailable contributor RemovedPack' in failure.stderr and snapshot(output) == before
        report = json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
        conflict = row(report)['conflicts'][0]
        assert conflict['mods'] == ['PackA', 'PackB'] and conflict['winner'] == 'PackB' and conflict['choice_unavailable']
        checks.append({'route': route, 'missing_choice_refused_and_reviewable': True})
        picks.write_text('')
    assert inputs == {str(p): snapshot(p) for p in [root, *sources]}
    tng(c, 10, unrelated=100); run('mods', 'add', root, c); sources.append(c)
    for route in ('merge', 'build'):
        output = work/(route+'-mixed')
        report = json.loads(run(*command(route, output)).stdout)
        assert row(report)['mods'] == ['PackA', 'PackB', 'PackC'] and row(report)['mode'] == 'thing-merged'
        assert values(output) == {123456: 20, 123457: 100, 123458: 40}
        assert origin(output)['uid:123456'] == 'PackB' and origin(output)['uid:123458'] == 'PackB'
        picks.write_text(KEY+'\tPackA\n'); report = json.loads(run(*command(route, output)).stdout)
        assert row(report)['conflicts'][0]['mods'] == ['PackA', 'PackB']
        assert origin(output)['uid:123456'] == 'PackA'
        picks.write_text(KEY+'\tPackC\n'); before = snapshot(output)
        failed = run(*command(route, output), good=False)
        assert 'selects unavailable contributor PackC' in failed.stderr and snapshot(output) == before
        checks.append({'route': route, 'mixed_groups_preserve_origin_and_contributor_scope': True})
        picks.write_text('')
    tng(c, 30, unrelated=100)
    for route in ('merge', 'build'):
        output = work/(route+'-contested'); picks.write_text(KEY+'\tPackA\n')
        report = json.loads(run(*command(route, output)).stdout)
        assert row(report)['conflicts'][0]['mods'] == ['PackA', 'PackB', 'PackC']
        assert row(report)['conflicts'][0]['winner'] == 'PackA'
        assert values(output) == {123456: 20, 123457: 100, 123458: 40}
        checks.append({'route': route, 'identical_group_plus_different_editor': True})
    picks.write_text('')
    output = work/'nonconsecutive'
    report = json.loads(run(*command('merge', output, ([complete] if complete else [])+[a, c, b])).stdout)
    assert row(report)['conflicts'][0]['mods'] == ['PackA', 'PackC', 'PackB']
    assert row(report)['conflicts'][0]['winner'] == 'PackB' and origin(output)['uid:123456'] == 'PackB'
    checks.append({'case': 'nonconsecutive identical editors retain load order'})
    # Return to the all-identical profile for GUI/staged recovery.
    run('mods', 'remove', root, 'PackC'); sources.remove(c)
    original_a = (a/RELATIVE).read_bytes(); original_b = (b/RELATIVE).read_bytes()
    try:
        for mode in ('partial', 'replacement', 'partial-delete'):
            tng(a, 20, 40, unrelated=None, partial=mode!='replacement', replace=mode=='replacement')
            if mode == 'partial-delete':
                text = (a/RELATIVE).read_text().replace('[Settings]\n', '[Settings]\nDeleteUIDs: 123457\n')
                (a/RELATIVE).write_text(text)
            (b/RELATIVE).write_bytes((a/RELATIVE).read_bytes())
            wanted = {123456: 20, 123458: 40}
            if mode == 'partial': wanted[123457] = 99
            for route in ('merge', 'build'):
                output = work/(route+'-'+mode); picks.write_text('')
                report = json.loads(run(*command(route, output)).stdout)
                assert row(report)['mode'] == ('replaced' if mode=='replacement' else 'thing-merged')
                assert values(output) == wanted and origin(output)['uid:123456'] == 'PackB'
                picks.write_text(KEY+'\tPackA\n'); report = json.loads(run(*command(route, output)).stdout)
                assert row(report)['conflicts'][0]['mods'] == ['PackA', 'PackB']
                assert row(report)['conflicts'][0]['winner'] == 'PackA'
                assert values(output) == wanted and origin(output)['uid:123456'] == 'PackA'
                checks.append({'route': route, 'identical_mode': mode, 'default_and_selected_origins_preserved': True})
    finally:
        (a/RELATIVE).write_bytes(original_a); (b/RELATIVE).write_bytes(original_b); picks.write_text('')
    saved = root/'forge_mods_picks.txt'
    if args.gui:
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            view = work/size; view.mkdir(); saved.write_text(KEY+'\tRemovedPack\n')
            script = view/'review.txt'
            lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}', f'set uiscale {scale}',
                'mods_tab 1', 'mods_conflicts', 'wait_mods', 'frames 3', 'assert_state mods_conflicts 1',
                'assert_state mods_unavailable_choices 1', 'assert_state mods_first_winner PackB',
                'mod_pick * PackA', 'frames 3', 'assert_state mods_first_winner PackA',
                'assert_state mods_unavailable_choices 0', 'mods_conflicts', 'wait_mods', 'frames 3',
                'assert_state mods_first_winner PackA', 'reveal mod_winner_0', 'frames 3',
                'reveal mod_winner_0', 'frames 3', 'clear_toasts', 'mouse_move viewport',
                f'screenshot {(view/"earlier-contributor.png").as_posix()}',
                'click mod_winner_0', 'frames 3', 'click mod_load_order_0', 'frames 3',
                'assert_state mods_first_winner PackB', 'assert_state mods_picks 0', 'quit']
            script.write_text('\n'.join(lines)+'\n')
            result = subprocess.run([str(args.gui.resolve()), '--install', str(args.install.resolve()),
                '--size', size, '--auto', str(script)], cwd=repo, capture_output=True, text=True, timeout=180,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(view/'appdata'),
                         LOCALAPPDATA=str(view/'localappdata')))
            (view/'output.log').write_text(result.stdout+result.stderr)
            log = Path(str(script)+'.log').read_text()
            assert result.returncode == 0 and 'RESULT PASS' in log and not saved.exists(), log[-5000:]
            output = view/'built'; run('mods', 'build', root, output)
            assert values(output) == {123456: 20, 123458: 40}
            checks.append({'size': size, 'earlier_contributor_review_and_dropdown_clear': True})
            print(size, 'identical contributor selection and default restoration: PASS', flush=True)
    before_stage = snapshot(root)
    run('mods', 'deploy', root)
    try:
        picks.write_text(KEY+'\tPackA\n')
        report = json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
        assert row(report)['conflicts'][0]['mods'] == ['PackA', 'PackB']
        assert row(report)['conflicts'][0]['winner'] == 'PackA'
        output = work/'staged-build'; run(*command('build', output))
        assert values(output) == {123456: 20, 123458: 40} and origin(output)['uid:123456'] == 'PackA'
        checks.append({'case': 'staged originals retain identical contributor choices'})
    finally: run('mods', 'undeploy', root)
    assert snapshot(root) == before_stage
    assert {str(p): snapshot(p) for p in sources} == {str(p): inputs[str(p)] for p in sources}
    (work/'result.json').write_text(json.dumps({'ok': True, 'checks': checks, 'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'identical TNG contributor checks')


if __name__ == '__main__': main()
