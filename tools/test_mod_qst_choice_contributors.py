#!/usr/bin/env python3
"""Quest-registration picks require contributors and preserve unrelated statements."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot


RELATIVE = Path('data/Levels/Test.qst')
KEY = 'qst:Test.qst|Existing'
TEST = 'AddTestQuest("Unchanged", "HSP", 2, "Display", "ini", "End", "Card");'


def qst(root, existing=False, added=None, unchanged=True, other=None):
    text = '// retained preamble\r\n'
    for name, value in [('Existing', existing), ('Unchanged', unchanged), ('Added', added), ('Other', other)]:
        if value is not None:
            text += 'AddQuest("'+name+'",\t'+('TRUE' if value else 'FALSE')+');\r\n'
    text += TEST+'\r\nunknown trailing text\r\n'
    path = root/RELATIVE; path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode('ascii'))


def values(root):
    text = (root/RELATIVE).read_text()
    assert TEST in text and '// retained preamble' in text and 'unknown trailing text' in text
    return {name: value=='TRUE' for name, value in re.findall(r'AddQuest\("([^"]+)",\s*(TRUE|FALSE)\);', text)}


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
    work = Path(tempfile.mkdtemp(prefix='mod-qst-choice-contributors-', dir=args.work_root)).resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4)); qst(root)
    (root/'data/Levels/Other.qst').write_text('// unrelated registry\n')
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)], capture_output=True,
                                text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        for name in ('script.bin', 'frontend.bin'):
            (complete/'data/CompiledDefs'/name).write_bytes((complete/'data/CompiledDefs/game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    sources = [work/name for name in ('PackA', 'PackB', 'PackC')]
    qst(sources[0], True, True); qst(sources[1], True, False); qst(sources[2], other=True)
    for source in sources: run('mods', 'add', root, source)
    inputs = [root, *sources] + ([complete] if complete else [])
    before_inputs = {str(p): snapshot(p) for p in inputs}
    picks = work/'picks.txt'; checks = []
    def review():
        report = json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
        return next(row for row in report['qst']['files'] if row['file']=='Test.qst').get('conflicts', [])
    for route in ('merge', 'build'):
        output = work/route; command = ['mods', route, root, output]
        if route == 'merge':
            command += ['--with', *([complete] if complete else []), *sources]
            if complete: command += ['--install-data']
        command += ['--picks', picks]
        picks.write_text(''); run(*command)
        expected = {'Existing': True, 'Unchanged': True, 'Added': False, 'Other': True}
        assert values(output) == expected
        (output/'sentinel').write_bytes(b'prior output')
        for winner in ('RemovedPack', 'PackC', 'base', 'none'):
            picks.write_text(KEY+'\t'+winner+'\n'); before = snapshot(output)
            failed = run(*command, good=False)
            assert 'selects unavailable contributor '+winner in failed.stderr
            row = next(r for r in review() if r['quest']=='Existing')
            assert row['choice_unavailable'] and not row['overridden'] and row['winner']=='PackB'
            assert [v['mod'] for v in row['wanted']] == ['PackA', 'PackB'] and snapshot(output)==before
            (work/f'{route}-{winner}.log').write_text(failed.stderr)
            checks.append({'route': route, 'winner': winner, 'refused_preserved_reviewable': True})
        for winner, flag in [('PackA', True), ('PackB', True), ('vanilla', False)]:
            picks.write_text(KEY+'\t'+winner+'\n'); run(*command)
            assert values(output) == dict(expected, Existing=flag)
            checks.append({'route': route, 'winner': winner, 'agreeing_flags_selected': True})
        for winner, flag in [('PackA', True), ('PackB', False), ('vanilla', None)]:
            picks.write_text('qst:Test.qst|Added\t'+winner+'\n'); run(*command)
            actual = dict(expected)
            if flag is None: del actual['Added']
            else: actual['Added'] = flag
            assert values(output)==actual
            checks.append({'route': route, 'winner': winner, 'case': 'new flag selection or baseline absence'})
        saved = [(p/RELATIVE).read_bytes() for p in sources]
        try:
            qst(sources[1], False, False)
            picks.write_text(KEY+'\tPackB\n'); before = snapshot(output); run(*command, good=False)
            assert snapshot(output)==before
            row = next(r for r in review() if r['quest']=='Existing')
            assert row['choice_unavailable'] and [v['mod'] for v in row['wanted']]==['PackA']
            picks.write_text(KEY+'\tvanilla\n'); run(*command)
            assert not values(output)['Existing']
            checks.append({'route': route, 'case': 'one contributor across multiple files'})
            for mode in ('single-editor', 'new-registry'):
                qst(sources[0], True, True, unchanged=None)
                for source in sources[1:]: (source/RELATIVE).write_bytes((root/RELATIVE).read_bytes())
                baseline = (root/RELATIVE).read_bytes()
                if mode=='new-registry':
                    (root/RELATIVE).unlink()
                    for source in sources[1:]: (source/RELATIVE).unlink()
                try:
                    picks.write_text(KEY+'\tPackB\n'); before = snapshot(output); run(*command, good=False)
                    assert snapshot(output)==before
                    picks.write_text(KEY+'\tPackA\n'); run(*command)
                    assert values(output)=={'Existing': True, 'Added': True}
                    picks.write_text(KEY+'\tvanilla\n'); run(*command)
                    assert values(output)==({'Added': True} if mode=='new-registry' else {'Existing': False, 'Added': True})
                    picks.write_text('qst:Test.qst|Added\tvanilla\n'); run(*command)
                    assert values(output)=={'Existing': True}
                    checks.append({'route': route, 'case': mode, 'flags_absence_and_other_removals_preserved': True})
                finally:
                    (root/RELATIVE).write_bytes(baseline)
                    for source in sources[1:]: (source/RELATIVE).write_bytes(baseline)
            for source in sources: (source/RELATIVE).write_bytes((root/RELATIVE).read_bytes())
            picks.write_text(KEY+'\tRemovedPack\n'); run(*command)
            checks.append({'route': route, 'case': 'all inactive'})
        finally:
            for source, data in zip(sources, saved): (source/RELATIVE).write_bytes(data)
        picks.write_text('qst:Test.qst|Unknown\tRemovedPack\n'); run(*command); assert values(output)==expected
        checks.append({'route': route, 'case': 'inactive key'})
        picks.write_text(''); run(*command); assert values(output)==expected
        checks.append({'route': route, 'case': 'clear'})
    standalone_source = work/'StandaloneA/Test.qst'
    standalone_source.parent.mkdir()
    standalone_source.write_bytes((sources[0]/RELATIVE).read_bytes())
    standalone = work/'standalone.qst'
    picks.write_text('Existing\tvanilla\nAdded\tvanilla\n')
    run('qst', 'merge', root/RELATIVE, standalone, standalone_source, '--picks', picks)
    assert standalone.read_bytes()==(root/RELATIVE).read_bytes()
    checks.append({'case': 'standalone single source vanilla restores exact baseline'})
    before = standalone.read_bytes(); picks.write_text('Existing\tRemovedPack\n')
    run('qst', 'merge', root/RELATIVE, standalone, standalone_source, '--picks', picks, good=False)
    assert standalone.read_bytes()==before
    checks.append({'case': 'standalone unavailable winner refuses before writes'})
    run('qst', 'merge', work/'missing.qst', standalone, standalone_source, good=False)
    assert standalone.read_bytes()==before
    checks.append({'case': 'standalone missing baseline refuses before writes'})
    picks.write_text(KEY+'\tPackA\nqst:Test.qst|Added\tPackA\n')
    run('mods', 'deploy', root, '--picks', picks)
    installed = snapshot(root)
    try:
        for route in ('merge', 'build', 'conflicts'):
            output = work/('staged-'+route)
            command = ['mods', route, root]
            if route!='conflicts': command.append(output)
            if route=='merge':
                command += ['--with', *([complete] if complete else []), *sources]
                if complete: command += ['--install-data']
            report = json.loads(run(*command, '--picks', picks, '--json').stdout)
            row = next(r for f in report['qst']['files'] for r in f.get('conflicts', []) if r['quest']=='Existing')
            assert not row['choice_unavailable'] and row['winner']=='PackA'
            assert [v['mod'] for v in row['wanted']]==['PackA','PackB']
            if route!='conflicts':
                assert values(output)=={'Existing':True,'Unchanged':True,'Added':True,'Other':True}
            assert snapshot(root)==installed
            checks.append({'route':route,'case':'staged recorded originals remain reviewable'})
    finally:
        run('mods','undeploy',root)
    if args.gui:
        saved = root/'forge_mods_picks.txt'
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            case = work/size; case.mkdir()
            saved.write_text(KEY+'\tRemovedPack With Long Quest Registration Overrides\n')
            script = case/'review.txt'; widget = 'mod_unavailable_choice_0'
            lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}', f'set uiscale {scale}',
                'mods_tab 1', 'mods_conflicts', 'wait_mods', 'frames 3', 'assert_state mod_action_failed 0',
                'assert_state mods_conflicts 2', 'assert_state mods_unavailable_choices 1',
                f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3', f'dump_widget {widget}',
                'clear_toasts', 'mouse_move viewport', f'screenshot {(case/"unavailable.png").as_posix()}',
                'reveal mod_winner_0', 'frames 3', 'reveal mod_winner_0', 'frames 3', 'click mod_winner_0',
                'frames 3', 'click mod_load_order_0', 'frames 3', 'assert_state mods_picks 0',
                'assert_state mods_unavailable_choices 0', 'mods_conflicts', 'wait_mods', 'frames 3',
                'assert_state mod_action_failed 0', 'quit']
            script.write_text('\n'.join(lines)+'\n')
            result = subprocess.run([str(args.gui.resolve()), '--install', str(args.install.resolve()),
                '--size', size, '--auto', str(script)], cwd=repo, capture_output=True, text=True, timeout=180,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(case/'appdata'),
                         LOCALAPPDATA=str(case/'localappdata')))
            (case/'output.log').write_text(result.stdout+result.stderr)
            log = Path(str(script)+'.log').read_text()
            assert result.returncode==0 and 'RESULT PASS' in log, log[-5000:]
            rect = re.search(r'widget_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
            clip = re.search(r'widget_clip_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
            assert rect and clip
            left, top, right, bottom = map(float, rect.groups()); cl, ct, cr, cb = map(float, clip.groups())
            assert cl<=left<right<=cr and ct<=top<bottom<=cb and not saved.exists()
            output = case/'built'; run('mods', 'build', root, output)
            assert values(output)=={'Existing': True, 'Unchanged': True, 'Added': False, 'Other': True}
            checks.append({'size': size, 'actual_dropdown_clear': True, 'warning_unclipped': True})
            print(size, 'QST warning, dropdown clear and rebuilt flags: PASS', flush=True)
    assert before_inputs=={str(p):snapshot(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks':checks,'inputs_unchanged':True,
                                               'complete_data':args.complete_data},indent=2))
    print('PASS:',len(checks),'QST choice contributor and preservation checks')


if __name__=='__main__': main()
