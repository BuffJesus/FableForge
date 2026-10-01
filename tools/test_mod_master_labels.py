#!/usr/bin/env python3
"""Long mod names must remain readable and selectable in the dependency popup."""
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
    parser.add_argument("--size", default="800x600")
    parser.add_argument("--scale", default="1.5")
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='mod-master-labels-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install'
    defs = root / 'data/CompiledDefs'
    defs.mkdir(parents=True)
    for name in ('game.bin', 'names.bin'):
        shutil.copyfile(args.root / 'data/CompiledDefs' / name, defs / name)
    tool = args.gui.resolve().with_name('forge-tools.exe')
    names = {'A': 'Foundation ## extended landscape resources and shared character equipment for the complete Albion adventure',
             'B': 'Adventure ## additional quests and village inhabitants requiring the foundation content collection'}
    for tag in ('A', 'B'):
        pack = work / ('Pack' + tag)
        (pack / 'data/Misc').mkdir(parents=True)
        (pack / 'assets').mkdir()
        (pack / 'forge_pack.json').write_text(json.dumps({'version': 1, 'name': 'Pack' + tag,
            'models': [], 'groundThemes': []}), encoding='utf-8')
        (pack / 'data/Misc/shared.txt').write_text('from pack ' + tag, encoding='utf-8')
        result = subprocess.run([str(tool), 'mods', 'add', str(root), str(pack), '--name', names[tag]],
            cwd=repo, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stdout + result.stderr
    script = work / 'popup.txt'
    commands = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
        f'set uiscale {args.scale}', 'mods_tab 1', 'frames 2',
        'reveal mod_row_1', 'frames 3', 'reveal mod_row_1', 'frames 3',
        'mouse_move mod_row_1', 'mouse_down right', 'frames 1', 'mouse_up right', 'frames 3',
        'assert_widget mod_requires_1_0', f'screenshot {(work / "popup.png").as_posix()}',
        'click mod_requires_label_1_0', 'frames 3', 'key_down Escape', 'frames 1', 'key_up Escape',
        'mod_move 1 0', 'frames 2', f"assert_mod_problems needs {names['A']} loaded before it",
        'reveal mod_row_0', 'frames 3', 'mouse_move mod_row_0',
        'mouse_down right', 'frames 1', 'mouse_up right', 'frames 3',
        'click mod_requires_0_1', 'frames 3', 'assert_mod_problems -',
        'click mod_requires_label_0_1', 'frames 3', f"assert_mod_problems needs {names['A']} loaded before it",
        'key_down Escape', 'frames 1', 'key_up Escape', 'quit', '']
    script.write_text('\n'.join(commands), encoding='utf-8')
    result = subprocess.run([str(args.gui.resolve()), '--auto', str(script), '--install', str(args.root),
        '--size', args.size], cwd=repo, capture_output=True, text=True, timeout=180,
        env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
    (work / 'gui_output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    log = Path(str(script) + '.log').read_text(encoding='utf-8')
    assert result.returncode == 0 and 'RESULT PASS' in log, log[-2200:]
    saved = json.loads((work / 'PackB/forge_pack.json').read_text())
    assert saved['requires'] == [names['A']]
    print('Long-name dependency popup selection and exact manifest requirement: PASS')


if __name__ == '__main__':
    main()
