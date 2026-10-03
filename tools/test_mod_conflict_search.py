#!/usr/bin/env python3
"""Reach conflicts beyond the display cap through real search and choice filters."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from test_mod_field_details import write_bank


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    args = parser.parse_args()
    gui, cwd = args.gui.resolve(), args.cwd.resolve()
    work = Path(tempfile.mkdtemp(prefix='mod-conflict-search-', dir=repo/'build')).resolve()
    assert work.parent == (repo/'build').resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'
    write_bank(root, (1, 2, 3, 4))
    sources = []
    for label in ('SearchPackA', 'SearchPackB'):
        pack = work/label
        files = pack/'data/Misc'
        files.mkdir(parents=True)
        (pack/'forge_pack.json').write_text(json.dumps({'version': 1, 'name': label,
            'models': [], 'groundThemes': []}), encoding='utf-8')
        for index in range(401):
            (files/f'choice{index:03}.txt').write_text(label, encoding='utf-8')
        sources.append(pack)
        result = subprocess.run([str(gui.with_name('forge-tools.exe')), 'mods', 'add',
            str(root), str(pack), '--name', label], cwd=cwd, capture_output=True, text=True)
        assert result.returncode == 0, result.stdout+result.stderr
    baseline = {p: hashlib.sha256(p.read_bytes()).hexdigest()
        for source in (root, *sources) for p in source.rglob('*') if p.is_file()}
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        case.mkdir()
        lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
            f'set uiscale {scale}', 'mods_tab 1', 'frames 2', 'mods_conflicts',
            'wait_mods', 'frames 2', 'assert_state mods_conflicts 401',
            'assert_state mods_conflicts_matching 401',
            'assert_state mods_wins_0 0', 'assert_state mods_losses_0 401',
            'assert_state mods_wins_1 401', 'assert_state mods_losses_1 0',
            'reveal mods_choices_only', 'frames 3', 'reveal mods_choices_only', 'frames 3',
            'click mods_choices_only', 'frames 2', 'assert_state mods_choices_only 1',
            'assert_state mods_conflicts_matching 0',
            'reveal mods_show_all_conflicts', 'frames 3', 'click mods_show_all_conflicts', 'frames 2',
            'assert_state mods_conflicts_matching 401',
            'reveal input_mod_conflict_search', 'frames 3', 'click input_mod_conflict_search',
            'input_text ChOiCe400', 'frames 3', 'assert_state mods_conflicts_matching 1',
            'assert_state mods_first_matching_conflict file:data/misc/choice400.txt',
            'reveal mod_winner_400', 'frames 3', 'assert_widget mod_winner_400',
            'clear_toasts', f'screenshot {(case/"last-conflict.png").as_posix()}',
            'click mod_winner_400', 'frames 2', 'click mod_vanilla_400', 'frames 2',
            'assert_state mods_picks 1', 'reveal mods_choices_only', 'frames 3',
            'assert_state mods_wins_1 400', 'assert_state mods_losses_1 1',
            'click mods_choices_only', 'frames 2', 'assert_state mods_conflicts_matching 1',
            'assert_state mods_choices_only 1',
            'reveal mods_show_all_conflicts', 'frames 3', 'click mods_show_all_conflicts', 'frames 2',
            'reveal input_mod_conflict_search', 'frames 3', 'click input_mod_conflict_search',
            'input_text searchpacka', 'frames 3', 'assert_state mods_conflicts_matching 401',
            'reveal mods_show_all_conflicts', 'frames 3', 'click mods_show_all_conflicts', 'frames 2',
            'reveal mods_choices_only', 'frames 3', 'click mods_choices_only', 'frames 2',
            'assert_state mods_conflicts_matching 1', 'reveal mod_winner_400', 'frames 3',
            f'screenshot {(case/"saved-choice.png").as_posix()}',
            'click mod_winner_400', 'frames 2', 'click mod_load_order_400', 'frames 2',
            'assert_state mods_picks 0', 'assert_state mods_conflicts_matching 0',
            'reveal mods_show_all_conflicts', 'frames 3', 'click mods_show_all_conflicts', 'frames 2',
            'assert_state mods_conflicts_matching 401', 'quit']
        script = case/'search.txt'
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        result = subprocess.run([str(gui), '--install', str(args.install),
            '--size', size, '--auto', str(script)], cwd=cwd, capture_output=True, text=True,
            timeout=180, env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (case/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-5000:]
        print(size, 'case-insensitive search, last row, saved choices and reset: PASS', flush=True)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p, digest in baseline.items())
    print('Source mods and baseline bytes unchanged: PASS', flush=True)


if __name__ == '__main__':
    main()
