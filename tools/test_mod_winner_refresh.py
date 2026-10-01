#!/usr/bin/env python3
"""Clearing a saved conflict choice must display the actual load-order winner."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--gui', type=Path, default=repo / 'build/FableForge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='mod-winner-refresh-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    defs = root / 'data/CompiledDefs'
    defs.mkdir(parents=True)
    for name in ('game.bin', 'names.bin'):
        shutil.copyfile(args.root / 'data/CompiledDefs' / name, defs / name)
    tool = args.gui.resolve().with_name('forge-tools.exe')
    for tag in ('A', 'B'):
        pack = work / ('Pack' + tag)
        (pack / 'data/Misc').mkdir(parents=True)
        (pack / 'assets').mkdir()
        (pack / 'forge_pack.json').write_text(json.dumps({'version': 1, 'name': 'Pack' + tag,
            'models': [], 'groundThemes': []}), encoding='utf-8')
        (pack / 'data/Misc/shared.txt').write_text('from pack ' + tag, encoding='utf-8')
        result = subprocess.run([str(tool), 'mods', 'add', str(root), str(pack), '--name', 'Pack' + tag],
            cwd=repo, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stdout + result.stderr
    picks = root / 'forge_mods_picks.txt'
    picks.write_bytes(b'file:data/misc/shared.txt\tPackA\n')
    script = work / 'winners.txt'
    script.write_text('\n'.join(['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
        'mods_tab 1', 'frames 2', 'mods_conflicts', 'wait_mods', 'frames 2',
        'assert_state mods_conflicts 1', 'assert_state mods_first_winner PackA',
        'reveal mod_winner_0', 'click mod_winner_0', 'frames 2',
        'click mod_load_order_0', 'frames 2', 'assert_state mods_first_winner PackB',
        'reveal mod_winner_0', 'frames 3', 'mouse_move viewport', 'frames 2',
        f'screenshot {(work / "cleared.png").as_posix()}',
        'mod_pick * vanilla', 'frames 2', 'assert_state mods_first_winner vanilla',
        'mod_pick * -', 'frames 2', 'assert_state mods_first_winner PackB',
        'mod_pick * PackA', 'frames 2', 'assert_state mods_first_winner PackA',
        'mod_pick * -', 'frames 2', 'assert_state mods_first_winner PackB',
        'mods_conflicts', 'wait_mods', 'frames 2', 'assert_state mods_first_winner PackB',
        'reveal mod_winner_0', 'frames 3',
        'mouse_move viewport', 'frames 2', f'screenshot {(work / "refreshed.png").as_posix()}',
        'mods_deploy', 'wait_mods', 'quit', '']), encoding='utf-8')
    result = subprocess.run([str(args.gui.resolve()), '--auto', str(script), '--install', str(args.root)],
        cwd=repo, capture_output=True, text=True, timeout=180,
        env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
    (work / 'gui_output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    log = Path(str(script) + '.log').read_text(encoding='utf-8')
    assert result.returncode == 0 and 'RESULT PASS' in log, log[-2000:]
    assert not picks.exists() or 'file:data/misc/shared.txt' not in picks.read_text(encoding='utf-8')
    assert (root/'data/Misc/shared.txt').read_text(encoding='utf-8') == 'from pack B'
    print('Saved choice, actual dropdown reset, vanilla reset, refreshed report and deployed winner: PASS')


if __name__ == '__main__':
    main()
