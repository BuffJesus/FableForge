"""Send real Ctrl+F and text input to each asset search, including hidden panels."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='asset-search-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    find = ['key_down Ctrl', 'key_down F', 'key_up F', 'key_up Ctrl', 'frames 3']
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}', 'frames 3']
        for tab, query, state in [(1, '169', 'models_filtered 1'),
                                   (3, 'brazierfirefinal', 'effects_filtered 1'),
                                   (4, 'beefy', 'dialogue_search_results 1'),
                                   (0, 'graphic_old_village_objects', 'texture_search graphic_old_village_objects')]:
            lines += [f'assets_tab {tab}', 'frames 3', 'click btn_toggle_actions', 'frames 3',
                      'assert_state tool_panel_visible 0'] + find
            lines += ['assert_state tool_panel_visible 1', f'input_text {query}', 'frames 4',
                      'assert_state '+state, 'assert_state filter ']
        lines += ['textures_tab 0', 'frames 3', 'click btn_toggle_explorer', 'frames 3',
                  'assert_state map_list_visible 0'] + find
        lines += ['input_text greatwood', 'frames 3', 'assert_state map_list_visible 1',
                  'assert_state filter greatwood', 'quit', '']
        script = work/(size+'.txt')
        script.write_text('\n'.join(lines))
        result = subprocess.run([str(repo/'build/FableForge.exe'), '--install', str(args.install),
            '--auto', str(script), '--size', size], cwd=repo, capture_output=True, text=True,
            timeout=120, env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        log = Path(str(script)+'.log').read_text()
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-2500:]
        print(size, 'asset searches and hidden map/tool panel recovery: PASS', flush=True)


if __name__ == '__main__':
    main()
