#!/usr/bin/env python3
"""Saved level-thing winners must contribute; whole-file removals stay intact."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot


RELATIVE = Path('data/Levels/Test.tng')
KEY = 'tng:Test.tng|uid:123456'


def tng(root, shared=10, added=None, unrelated=99, *, partial=False, replace=False):
    header = '[Settings]\nReplace=true\n' if replace else '[Settings]\n' if partial else ''
    text = header+'Version 2;\nXXXSectionStart NULL;\n'
    for uid, health in [(123456, shared), (123457, unrelated), (123458, added)]:
        if health is None: continue
        text += ('NewThing Object;\nPlayer 4;\nUID '+str(uid)+';\n'
                 'DefinitionType "OBJECT_TEST";\nOpaqueValue "preserve";\n'
                 'Health '+str(health)+';\nEndThing;\n')
    path = root/RELATIVE
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text+'XXXSectionEnd;\n')


def values(root):
    text = (root/RELATIVE).read_text()
    return {int(uid): int(health) for uid, health in re.findall(
        r'UID\s+(\d+);(?:(?!EndThing;).)*?Health\s+(\d+);', text, re.S)}


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
    work = Path(tempfile.mkdtemp(prefix='mod-tng-choice-contributors-', dir=args.work_root)).resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4)); tng(root)
    (root/'data/Levels/Other.qst').write_text('// unrelated baseline registry\n')
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
                                capture_output=True, text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        for name in ('script.bin', 'frontend.bin'):
            (complete/'data/CompiledDefs'/name).write_bytes((complete/'data/CompiledDefs/game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    sources = [work/name for name in ('PackA', 'PackB', 'PackC')]
    tng(sources[0], 20, 40); tng(sources[1], 30, 50); tng(sources[2], unrelated=100)
    for source in sources: run('mods', 'add', root, source)
    inputs = [root, *sources] + ([complete] if complete else [])
    before_inputs = {str(p): snapshot(p) for p in inputs}
    picks = work/'picks.txt'; checks = []
    def review():
        report = json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
        return report['tng']['levels'][0].get('conflicts', [])
    for route in ('merge', 'build'):
        output = work/route
        command = ['mods', route, root, output]
        if route == 'merge':
            command += ['--with', *([complete] if complete else []), *sources]
            if complete: command += ['--install-data']
        command += ['--picks', picks]
        picks.write_text(''); run(*command)
        assert values(output) == {123456: 30, 123457: 100, 123458: 50}
        (output/'sentinel').write_bytes(b'prior output')
        for winner in ('RemovedPack', 'PackC', 'base', 'none'):
            picks.write_text(KEY+'\t'+winner+'\n'); before = snapshot(output)
            failed = run(*command, good=False)
            assert 'selects unavailable contributor '+winner in failed.stderr
            row = next(r for r in review() if r['thing'] == 'uid:123456')
            assert row['choice_unavailable'] and not row['overridden'] and row['winner'] == 'PackB'
            assert row['mods'] == ['PackA', 'PackB'] and snapshot(output) == before
            (work/f'{route}-{winner}.log').write_text(failed.stderr)
            checks.append({'route': route, 'winner': winner, 'refused_preserved_reviewable': True})
        for winner, health in [('PackA', 20), ('PackB', 30), ('vanilla', 10)]:
            picks.write_text(KEY+'\t'+winner+'\n'); run(*command)
            assert values(output) == {123456: health, 123457: 100, 123458: 50}
            checks.append({'route': route, 'winner': winner, 'exact_values': True})
        picks.write_text('tng:Test.tng|uid:123458\tvanilla\n'); run(*command)
        assert values(output) == {123456: 30, 123457: 100}
        checks.append({'route': route, 'case': 'merged absent baseline thing'})
        saved = [(p/RELATIVE).read_bytes() for p in sources]
        try:
            tng(sources[1], 20, 50)
            picks.write_text(KEY+'\tRemovedPack\n'); run(*command, good=False)
            row = next(r for r in review() if r['thing'] == 'uid:123456')
            assert row['choice_unavailable'] and row['mods'] == ['PackA', 'PackB']
            picks.write_text(KEY+'\tPackB\n'); run(*command)
            assert values(output) == {123456: 20, 123457: 100, 123458: 50}
            checks.append({'route': route, 'case': 'agreeing actual contributors'})
            tng(sources[1]); tng(sources[2])
            for mode in ('single-editor', 'partial', 'partial-delete', 'replace', 'new-level'):
                tng(sources[0], 20, 40, unrelated=None, partial=mode.startswith('partial'), replace=mode=='replace')
                if mode == 'partial-delete':
                    path = sources[0]/RELATIVE
                    path.write_text(path.read_text().replace('[Settings]\n', '[Settings]\nDeleteUIDs: 123457\n'))
                baseline = (root/RELATIVE).read_bytes()
                other_versions = [(p/RELATIVE).read_bytes() for p in sources[1:]]
                if mode == 'new-level':
                    (root/RELATIVE).unlink()
                    for source in sources[1:]: tng(source, shared=None, unrelated=None)
                try:
                    picks.write_text(KEY+'\tPackB\n'); before = snapshot(output)
                    run(*command, good=False)
                    assert snapshot(output) == before
                    picks.write_text(KEY+'\tPackA\n'); run(*command)
                    assert values(output)[123456] == 20
                    if mode != 'partial': assert 123457 not in values(output)
                    picks.write_text(KEY+'\tvanilla\n'); run(*command)
                    assert values(output).get(123456) == (None if mode=='new-level' else 10)
                    picks.write_text('tng:Test.tng|uid:123458\tvanilla\n'); run(*command)
                    assert 123458 not in values(output)
                    checks.append({'route': route, 'case': mode, 'refusal_baseline_absence_removals': True})
                finally:
                    (root/RELATIVE).write_bytes(baseline)
                    for source, data in zip(sources[1:], other_versions): (source/RELATIVE).write_bytes(data)
            tng(sources[0]); picks.write_text(KEY+'\tRemovedPack\n'); run(*command)
            checks.append({'route': route, 'case': 'all inactive'})
        finally:
            for source, data in zip(sources, saved): (source/RELATIVE).write_bytes(data)
        picks.write_text('tng:Test.tng|uid:999999\tRemovedPack\n'); run(*command)
        assert values(output) == {123456: 30, 123457: 100, 123458: 50}
        checks.append({'route': route, 'case': 'inactive key'})
        picks.write_text(''); run(*command)
        checks.append({'route': route, 'case': 'clear'})
    standalone = work/'standalone'
    run('tng', 'merge', root/RELATIVE, standalone/RELATIVE,
        *[source/RELATIVE for source in sources])
    assert values(standalone) == {123456: 30, 123457: 100, 123458: 50}
    checks.append({'case': 'standalone conservative merge'})
    missing_output = work/'missing-output.tng'; missing_output.write_bytes(b'prior bytes')
    run('tng', 'merge', work/'missing.tng', missing_output, sources[0]/RELATIVE, good=False)
    assert missing_output.read_bytes() == b'prior bytes'
    checks.append({'case': 'standalone missing baseline refuses before writes'})
    if args.gui:
        saved = root/'forge_mods_picks.txt'
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            case = work/size; case.mkdir()
            saved.write_text(KEY+'\tRemovedPack With Long Level And Thing Overrides\n')
            script = case/'review.txt'; widget = 'mod_unavailable_choice_0'
            lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
                     f'set uiscale {scale}', 'mods_tab 1', 'mods_conflicts', 'wait_mods', 'frames 3',
                     'assert_state mod_action_failed 0', 'assert_state mods_conflicts 2',
                     'assert_state mods_unavailable_choices 1', f'reveal {widget}', 'frames 3',
                     f'reveal {widget}', 'frames 3', f'dump_widget {widget}', 'clear_toasts',
                     'mouse_move viewport', f'screenshot {(case/"unavailable.png").as_posix()}',
                     'reveal mod_winner_0', 'frames 3', 'reveal mod_winner_0', 'frames 3',
                     'click mod_winner_0', 'frames 3', 'click mod_load_order_0', 'frames 3',
                     'assert_state mods_picks 0', 'assert_state mods_unavailable_choices 0',
                     'mods_conflicts', 'wait_mods', 'frames 3', 'assert_state mod_action_failed 0', 'quit']
            script.write_text('\n'.join(lines)+'\n')
            result = subprocess.run([str(args.gui.resolve()), '--install', str(args.install.resolve()),
                '--size', size, '--auto', str(script)], cwd=repo, capture_output=True, text=True,
                timeout=180, env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                    APPDATA=str(case/'appdata'), LOCALAPPDATA=str(case/'localappdata')))
            (case/'output.log').write_text(result.stdout+result.stderr)
            log = Path(str(script)+'.log').read_text()
            assert result.returncode == 0 and 'RESULT PASS' in log, log[-5000:]
            rect = re.search(r'widget_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
            clip = re.search(r'widget_clip_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
            assert rect and clip
            left, top, right, bottom = map(float, rect.groups()); cl, ct, cr, cb = map(float, clip.groups())
            assert cl <= left < right <= cr and ct <= top < bottom <= cb
            assert not saved.exists()
            output = case/'built'; run('mods', 'build', root, output)
            assert values(output) == {123456: 30, 123457: 100, 123458: 50}
            checks.append({'size': size, 'actual_dropdown_clear': True, 'warning_unclipped': True})
            print(size, 'TNG warning, dropdown clear and rebuilt values: PASS', flush=True)
    assert before_inputs == {str(p): snapshot(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks,
        'inputs_unchanged': True, 'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'TNG choice contributor and replacement checks')


if __name__ == '__main__': main()
