#!/usr/bin/env python3
"""Unavailable FSE quest winners refuse writes while conflict review remains available."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot


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
    work = Path(tempfile.mkdtemp(prefix='mod-fse-choice-contributors-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4))
    def registry(folder, number, extra=False):
        (folder/'FSE').mkdir(parents=True, exist_ok=True)
        text = 'Quests = {\n    Alpha = { name="Alpha", file="Alpha/Alpha", id='+str(number)+', entity_scripts={} },\n'
        if extra: text += '    Beta = { name="Beta", file="Beta/Beta", id=60002, entity_scripts={} },\n'
        (folder/'FSE/quests.lua').write_text(text+'}\n')
    def values(folder):
        text = (folder/'FSE/quests.lua').read_text()
        return {name: int(number) for name, number in re.findall(r'(\w+)\s*=\s*\{[^\n]*?\bid=(\d+)', text)}
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
                                capture_output=True, text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    registry(root, 9002); sources = []; complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        defs = complete/'data/CompiledDefs'
        for name in ('script.bin', 'frontend.bin'):
            (defs/name).write_bytes((defs/'game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    for name, number, extra in [('PackA', 60001, False), ('PackB', 60005, False), ('PackC', 9002, True)]:
        source = work/name; registry(source, number, extra); sources.append(source)
        run('mods', 'add', root, source)
    inputs = [root, *sources] + ([complete] if complete else [])
    before_inputs = {str(p): snapshot(p) for p in inputs}
    picks = work/'picks.txt'; checks = []
    def review():
        return json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
    for route in ('merge', 'build'):
        output = work/route; command = ['mods', route, root, output]
        if route == 'merge':
            command += ['--with', *([complete] if complete else []), *sources]
            if complete: command += ['--install-data']
        command += ['--picks', picks]
        picks.write_text(''); run(*command)
        assert values(output) == {'Alpha': 60005, 'Beta': 60002}
        (output/'sentinel').write_bytes(b'prior output')
        for winner in ('RemovedPack', 'PackC', 'base', 'none'):
            picks.write_text('fse:Alpha\t'+winner+'\n'); before = snapshot(output); saved = picks.read_bytes()
            failed = run(*command, good=False)
            assert 'mods FSE choice: Alpha selects unavailable contributor '+winner in failed.stderr
            row = review()['fse']['contested'][0]
            assert row['choice_unavailable'] and not row['overridden'] and row['winner'] == 'PackB'
            assert row['mods'] == ['PackA', 'PackB']
            assert snapshot(output) == before and picks.read_bytes() == saved
            (work/f'{route}-{winner}.log').write_text(failed.stderr)
            checks.append({'route': route, 'winner': winner, 'refused_preserved_reviewable': True})
        for winner, expected in [('PackA', 60001), ('PackB', 60005), ('vanilla', 9002)]:
            picks.write_text('fse:Alpha\t'+winner+'\n'); run(*command)
            assert values(output) == {'Alpha': expected, 'Beta': 60002}
            assert (output/'sentinel').read_bytes() == b'prior output'
            checks.append({'route': route, 'winner': winner, 'accepted_exact_values': True})
        saved_source = (sources[1]/'FSE/quests.lua').read_bytes()
        saved_first = (sources[0]/'FSE/quests.lua').read_bytes()
        try:
            registry(sources[1], 9002)
            picks.write_text('fse:Alpha\tPackB\n'); before = snapshot(output)
            failed = run(*command, good=False)
            assert 'selects unavailable contributor PackB' in failed.stderr and snapshot(output) == before
            assert not review()['fse']['contested']
            picks.write_text('fse:Alpha\tPackA\n'); run(*command)
            assert values(output) == {'Alpha': 60001, 'Beta': 60002}
            checks.append({'route': route, 'case': 'one remaining contributor', 'refusal_and_valid_choice': True})
            registry(sources[0], 9002)
            picks.write_text('fse:Alpha\tRemovedPack\n'); run(*command)
            assert values(output) == {'Alpha': 9002, 'Beta': 60002}
            checks.append({'route': route, 'case': 'no current contributions', 'inactive': True})
        finally:
            (sources[0]/'FSE/quests.lua').write_bytes(saved_first)
            (sources[1]/'FSE/quests.lua').write_bytes(saved_source)
        original_base = (root/'FSE/quests.lua').read_bytes()
        try:
            (root/'FSE/quests.lua').write_text('Quests = {\n}\n')
            picks.write_text('fse:Alpha\tvanilla\n'); run(*command)
            assert values(output) == {'Beta': 60002}
            checks.append({'route': route, 'case': 'absent baseline entry', 'restored_absence': True})
        finally:
            (root/'FSE/quests.lua').write_bytes(original_base)
        picks.write_text('fse:Inactive\tRemovedPack\n'); run(*command)
        assert values(output) == {'Alpha': 60005, 'Beta': 60002}
        checks.append({'route': route, 'case': 'inactive entry', 'accepted': True})
        picks.write_text(''); run(*command)
        assert values(output) == {'Alpha': 60005, 'Beta': 60002}
        checks.append({'route': route, 'case': 'clear', 'accepted': True})
    if args.gui:
        saved = root/'forge_mods_picks.txt'
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            case = work/size; case.mkdir()
            saved.write_text('fse:Alpha\tRemovedPack With Long Quest And Appearance Overrides\n')
            script = case/'review.txt'; shot = case/'unavailable.png'
            lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
                     f'set uiscale {scale}', 'mods_tab 1', 'mods_conflicts', 'wait_mods', 'frames 3',
                     'assert_state mod_action_failed 0', 'assert_state mods_conflicts 1',
                     'assert_state mods_unavailable_choices 1', 'assert_state mods_first_winner PackB']
            for widget in ('mod_unavailable_choice_0',):
                lines += [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3', f'dump_widget {widget}']
            lines += ['clear_toasts', 'mouse_move viewport', f'screenshot {shot.as_posix()}',
                      'reveal mod_winner_0', 'frames 3', 'reveal mod_winner_0', 'frames 3',
                      'click mod_winner_0', 'frames 3', 'click mod_load_order_0', 'frames 3',
                      'assert_state mods_picks 0', 'assert_state mods_unavailable_choices 0',
                      'mods_conflicts', 'wait_mods', 'frames 3', 'assert_state mod_action_failed 0', 'quit']
            script.write_text('\n'.join(lines)+'\n')
            result = subprocess.run([str(args.gui.resolve()), '--install', str(args.install.resolve()),
                                     '--size', size, '--auto', str(script)], cwd=repo, capture_output=True,
                                    text=True, timeout=180,
                                    env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                                             APPDATA=str(case/'appdata'), LOCALAPPDATA=str(case/'localappdata')))
            (case/'output.log').write_text(result.stdout+result.stderr)
            log = Path(str(script)+'.log').read_text()
            assert result.returncode == 0 and 'RESULT PASS' in log, log[-5000:]
            rect = re.search(r'widget_rect mod_unavailable_choice_0 ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
            clip = re.search(r'widget_clip_rect mod_unavailable_choice_0 ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
            assert rect and clip
            left, top, right, bottom = map(float, rect.groups()); cl, ct, cr, cb = map(float, clip.groups())
            assert cl <= left < right <= cr and ct <= top < bottom <= cb, (rect.groups(), clip.groups())
            assert not saved.exists()
            output = case/'built'; run('mods', 'build', root, output)
            assert values(output) == {'Alpha': 60005, 'Beta': 60002}
            checks.append({'size': size, 'actual_dropdown_clear': True, 'warning_unclipped': True})
            print(size, 'FSE warning, dropdown clear and exact rebuilt registry: PASS', flush=True)
    assert before_inputs == {str(p): snapshot(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'inputs_unchanged': True,
                                               'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'FSE choice contributor checks')


if __name__ == '__main__': main()
