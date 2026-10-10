#!/usr/bin/env python3
"""DeleteUIDs must agree with effective winners, saved choices and provenance."""
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
    work = Path(tempfile.mkdtemp(prefix='mod-tng-deletion-choices-', dir=args.work_root)).resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4)); tng(root)
    (root/'data/Levels/Other.qst').write_text('// untouched baseline registry\n')
    edited = work/'Edited'; tng(edited, shared=20, added=40)
    removed = work/'Removed'; tng(removed, shared=None, unrelated=None, partial=True)
    path = removed/RELATIVE
    path.write_text(path.read_text().replace('[Settings]\n', '[Settings]\nDeleteUIDs: 123456 123458\n'))
    counter = 0; checks = []
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
    for source in (edited, removed): run('mods', 'add', root, source)
    inputs = {str(p): snapshot(p) for p in (edited, removed)}
    picks = work/'picks.txt'; picks.write_text('')
    def command(route, output):
        cmd = ['mods', route, root, output]
        if route == 'merge':
            order = json.loads((root/'forge_mods.json').read_text())['mods']
            cmd += ['--with', *[m['source'] for m in order if m['enabled']]]
            if complete: cmd += ['--install-data']
        return cmd+['--picks', picks, '--json']
    def parent(report): return report['tng']['levels'][0]
    def origin(output):
        p = output/'forge_mods_provenance.json'
        return json.loads(p.read_text()).get('levels', {}).get('Test.tng', {}) if p.exists() else {}
    for reversed_order in (False, True):
        if reversed_order: run('mods', 'move', root, 'Removed', 1 if complete else 0)
        for route in ('merge', 'build'):
            output = work/(route+('-reversed' if reversed_order else ''))
            output.mkdir()
            (output/'forge_mods_provenance.json').write_text(json.dumps({'version': 1, 'mods': ['Previous'],
                'levels': {'Test.tng': {'uid:123456': 'Previous'}}}))
            picks.write_text(''); report = json.loads(run(*command(route, output)).stdout)
            assert values(output) == {123457: 99}
            assert parent(report)['things'] == 1 and parent(report)['deleted'] == 2
            assert parent(report)['applied'] == 0 and parent(report)['added'] == 0
            for uid in (123456, 123458):
                row = next(r for r in parent(report)['conflicts'] if r['thing'] == 'uid:'+str(uid))
                assert row['winner'] == 'removed' and row['removal_mods'] == ['Removed']
                assert not row['overridden'] and not row['choice_unavailable']
                assert 'uid:'+str(uid) not in origin(output)
            (output/'sentinel').write_bytes(b'prior output')
            checks.append({'route': route, 'reversed_order': reversed_order, 'effective_removal_and_no_ghost_origin': True})
            for key, winner in ((KEY, 'Edited'), (KEY, 'vanilla'), ('tng:Test.tng|uid:123458', 'Edited')):
                picks.write_text(key+'\t'+winner+'\n')
                before = snapshot(output); before_root = snapshot(root)
                failure = run(*command(route, output), good=False)
                assert 'mods TNG choice:' in failure.stderr and 'is removed by DeleteUIDs from Removed' in failure.stderr
                assert snapshot(output) == before and snapshot(root) == before_root
                report = json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
                row = next(r for r in parent(report)['conflicts'] if r['thing'] == key.split('|')[1])
                assert row['winner'] == 'removed' and row['choice_unavailable'] and not row['overridden']
                assert row['removal_mods'] == ['Removed'] and row['mods'] == ['Edited']
                checks.append({'route': route, 'reversed_order': reversed_order, 'key': key, 'winner': winner, 'refused_reviewable': True})
            # Vanilla for an added thing already means absence, so no restoration is blocked.
            picks.write_text('tng:Test.tng|uid:123458\tvanilla\n'); run(*command(route, output))
            assert values(output) == {123457: 99} and 'uid:123458' not in origin(output)
            checks.append({'route': route, 'reversed_order': reversed_order, 'vanilla_new_thing_absence': True})
            picks.write_text('')
    # The deletion-only source must also surface a blocked baseline restoration.
    run('mods', 'disable', root, 'Edited'); picks.write_text(KEY+'\tvanilla\n')
    report = json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
    row = parent(report)['conflicts'][0]
    assert row['winner'] == 'removed' and row['choice_unavailable'] and row['mods'] == []
    failure = run(*command('build', work/'deletion-only'), good=False)
    assert 'is removed by DeleteUIDs from Removed' in failure.stderr
    picks.write_text(''); report = json.loads(run(*command('build', work/'deletion-only')).stdout)
    assert values(work/'deletion-only') == {123457: 99}
    checks.append({'deletion_only_baseline_pick_refused_and_reviewable': True})
    run('mods', 'enable', root, 'Edited')
    # Disabling the removal makes the same saved contributor meaningful again.
    run('mods', 'disable', root, 'Removed'); picks.write_text(KEY+'\tEdited\n')
    report = json.loads(run(*command('build', work/'removal-disabled')).stdout)
    assert values(work/'removal-disabled') == {123456: 20, 123457: 99, 123458: 40}
    assert parent(report)['conflicts'][0]['winner'] == 'Edited'
    assert origin(work/'removal-disabled')['uid:123456'] == 'Edited'
    checks.append({'disable_removal_restores_valid_saved_contributor': True})
    run('mods', 'enable', root, 'Removed'); picks.write_text('')
    twin = work/'RemovedTwin'; (twin/RELATIVE).parent.mkdir(parents=True)
    (twin/RELATIVE).write_bytes((removed/RELATIVE).read_bytes())
    (twin/'data/Misc').mkdir(); (twin/'data/Misc/identity.txt').write_text('distinct package identity')
    run('mods', 'add', root, twin)
    run('mods', 'move', root, 'RemovedTwin', 2 if complete else 1)
    report = json.loads(run(*command('build', work/'identical-removals')).stdout)
    assert parent(report)['conflicts'][0]['removal_mods'] == ['Removed', 'RemovedTwin']
    assert values(work/'identical-removals') == {123457: 99}
    picks.write_text(KEY+'\tEdited\n')
    failure = run(*command('build', work/'identical-removals'), good=False)
    assert 'DeleteUIDs from Removed, RemovedTwin' in failure.stderr
    checks.append({'identical_removal_files_retain_every_source_label': True})
    run('mods', 'remove', root, 'RemovedTwin'); picks.write_text('')
    saved = root/'forge_mods_picks.txt'
    if args.gui:
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            view = work/size; view.mkdir(); saved.write_text(KEY+'\tEdited\n')
            script = view/'review.txt'
            lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}', f'set uiscale {scale}',
                'mods_tab 1', 'mods_conflicts', 'wait_mods', 'frames 3',
                'assert_state mods_first_winner removed', 'assert_state mods_unavailable_choices 1',
                'assert_state mods_unreported_choices 0', 'reveal mod_winner_0', 'frames 3',
                'reveal mod_winner_0', 'frames 3', 'clear_toasts', 'mouse_move viewport',
                f'screenshot {(view/"blocked.png").as_posix()}',
                'click mod_winner_0', 'frames 3', 'click mod_load_order_0', 'frames 3',
                'assert_state mods_picks 0', 'assert_state mods_unavailable_choices 0',
                'assert_state mods_first_winner removed', 'mods_conflicts', 'wait_mods', 'frames 3',
                'assert_state mods_first_winner removed', 'assert_state mod_action_failed 0',
                f'screenshot {(view/"cleared.png").as_posix()}', 'quit']
            script.write_text('\n'.join(lines)+'\n')
            result = subprocess.run([str(args.gui.resolve()), '--install', str(args.install.resolve()),
                '--size', size, '--auto', str(script)], cwd=repo, capture_output=True, text=True, timeout=180,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(view/'appdata'),
                         LOCALAPPDATA=str(view/'localappdata')))
            (view/'output.log').write_text(result.stdout+result.stderr)
            log = Path(str(script)+'.log').read_text()
            assert result.returncode == 0 and 'RESULT PASS' in log and not saved.exists(), log[-5000:]
            run(*command('build', view/'built')); assert values(view/'built') == {123457: 99}
            checks.append({'size': size, 'effective_removal_review_and_dropdown_clear': True})
    before_stage = snapshot(root); run('mods', 'deploy', root)
    try:
        picks.write_text(KEY+'\tEdited\n'); before = snapshot(root)
        report = json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
        assert parent(report)['conflicts'][0]['winner'] == 'removed'
        assert snapshot(root) == before
        run(*command('build', work/'staged'), good=False)
        assert snapshot(root) == before
    finally: run('mods', 'undeploy', root)
    assert snapshot(root) == before_stage and inputs == {str(p): snapshot(p) for p in (edited, removed)}
    checks.append({'staged_originals_review_and_exact_undeployment': True})
    (work/'result.json').write_text(json.dumps({'ok': True, 'checks': checks, 'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'TNG deletion/choice checks')


if __name__ == '__main__': main()
