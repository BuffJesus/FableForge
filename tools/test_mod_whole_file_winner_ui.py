#!/usr/bin/env python3
"""Whole-file winner previews retain removals when entry choices are cleared."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot
from test_mod_tng_choice_contributors import tng, values as tng_values
from test_mod_qst_choice_contributors import qst, values as qst_values


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-whole-file-winner-', dir=args.work_root)).resolve()
    print('Evidence retained at', work, flush=True)
    checks = []
    for family in ('tng', 'qst'):
        case = work/family; root = case/'install'; write_bank(root, (1, 2, 3, 4))
        (root/'data/Levels').mkdir(parents=True)
        (root/'data/Levels/Other.qst').write_text('// unrelated baseline registry\n')
        sources = [case/name for name in ('PackA', 'PackB', 'PackC')]
        key = 'tng:Test.tng|uid:123456' if family == 'tng' else 'qst:Test.qst|Existing'
        if family == 'tng':
            tng(sources[0], 20); tng(sources[1], 30); tng(sources[2], None)
        else:
            qst(sources[0], True); qst(sources[1], False); qst(sources[2], None)
        def run(*arguments, good=True):
            result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
                capture_output=True, text=True, timeout=120)
            assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
            return result
        for source in sources: run('mods', 'add', root, source)
        before_inputs = {str(p): snapshot(p) for p in [root, *sources]}
        saved = root/'forge_mods_picks.txt'
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            view = case/size; view.mkdir()
            saved.write_text(key+'\tRemovedPack\n')
            report = json.loads(run('mods', 'conflicts', root, '--json').stdout)
            (view/'native-review.json').write_text(json.dumps(report, indent=2))
            parent = report[family]['levels' if family == 'tng' else 'files'][0]
            row = parent['conflicts'][0]
            assert row['winner'] == 'PackC' and row['choice_unavailable']
            output = view/'built'; output.mkdir(); (output/'sentinel').write_bytes(b'prior output')
            before = snapshot(output)
            failed = run('mods', 'build', root, output, good=False)
            assert 'selects unavailable contributor RemovedPack' in failed.stderr
            assert snapshot(output) == before
            script = view/'review.txt'
            lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
                f'set uiscale {scale}', 'mods_tab 1', 'mods_conflicts', 'wait_mods', 'frames 3',
                'assert_state mods_conflicts 1', 'assert_state mods_first_winner PackC',
                'assert_state mods_unavailable_choices 1', 'reveal mod_winner_0', 'frames 3',
                'reveal mod_winner_0', 'frames 3', 'clear_toasts', 'mouse_move viewport',
                f'screenshot {(view/"unavailable.png").as_posix()}',
                'click mod_winner_0', 'frames 3', 'click mod_load_order_0', 'frames 3',
                'assert_state mods_picks 0', 'assert_state mods_first_winner PackC',
                'mod_pick * PackA', 'frames 3', 'assert_state mods_first_winner PackA',
                'mods_conflicts', 'wait_mods', 'frames 3', 'assert_state mods_first_winner PackA',
                'reveal mod_winner_0', 'frames 3', 'reveal mod_winner_0', 'frames 3',
                'click mod_winner_0', 'frames 3', 'click mod_load_order_0', 'frames 3',
                'assert_state mods_first_winner PackC', 'assert_state mods_picks 0',
                f'screenshot {(view/"cleared.png").as_posix()}',
                'mods_conflicts', 'wait_mods', 'frames 3',
                'assert_state mods_conflicts 0', 'assert_state mod_action_failed 0', 'quit']
            script.write_text('\n'.join(lines)+'\n')
            result = subprocess.run([str(args.gui.resolve()), '--install', str(args.install.resolve()),
                '--size', size, '--auto', str(script)], cwd=repo, capture_output=True, text=True,
                timeout=180, env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                    APPDATA=str(view/'appdata'), LOCALAPPDATA=str(view/'localappdata')))
            (view/'output.log').write_text(result.stdout+result.stderr)
            log = Path(str(script)+'.log').read_text()
            assert result.returncode == 0 and 'RESULT PASS' in log, log[-5000:]
            assert not saved.exists()
            run('mods', 'build', root, output)
            values = tng_values(output) if family == 'tng' else qst_values(output)
            assert (123456 if family == 'tng' else 'Existing') not in values
            assert (output/'sentinel').read_bytes() == b'prior output'
            saved.write_text(key+'\tPackA\n')
            run('mods', 'build', root, output)
            values = tng_values(output) if family == 'tng' else qst_values(output)
            assert values[123456 if family == 'tng' else 'Existing'] == (20 if family == 'tng' else True)
            saved.write_text(key+'\tvanilla\n')
            run('mods', 'build', root, output)
            values = tng_values(output) if family == 'tng' else qst_values(output)
            assert (123456 if family == 'tng' else 'Existing') not in values
            saved.unlink()
            checks.append({'family': family, 'size': size, 'copy_winner_and_removal_preserved': True})
            print(family, size, 'preview, cached-choice clearing and built removal: PASS', flush=True)
        assert before_inputs == {str(p): snapshot(p) for p in [root, *sources]}
    (work/'result.json').write_text(json.dumps({'ok': True, 'checks': checks}, indent=2))


if __name__ == '__main__': main()
