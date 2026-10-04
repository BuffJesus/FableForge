#!/usr/bin/env python3
"""Actual Add controls accept Data, mark complete installation and round-trip it."""
import json, os, re, shutil, subprocess, tempfile
from pathlib import Path
from PIL import Image
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_setup_restore_ui import level


def main():
    repo = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix='complete-data-ui-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    def button(name, wait=False):
        lines = [f'reveal {name}', 'frames 3', f'reveal {name}', 'frames 3', f'click {name}', 'frames 3']
        return lines+(['wait_mods', 'wait_ready', 'frames 3'] if wait else [])
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        root, source = case/'install', case/'Overhaul'
        for path, values in [(root, (1,2,3,4)), (source, (91,92,93,94))]:
            write_bank(path, values, record='')
            for bank in ('script.bin', 'frontend.bin'):
                shutil.copyfile(path/'data/CompiledDefs/game.bin', path/'data/CompiledDefs'/bank)
        levels = root/'data/Levels/FinalAlbion'; levels.mkdir(parents=True)
        (levels/'OwnedMap.lev').write_bytes(level(4))
        (levels/'OwnedMap.tng').write_bytes(b'Version 2;\r\n')
        (source/'data/Misc').mkdir(); (source/'data/Misc/new.asset').write_bytes(b'complete asset')
        baseline, original = snapshot(root), snapshot(source)
        script = case/'actions.txt'
        screenshot = case/'complete.png'
        lines = ['wait_maps','wait_ready',f'set uiscale {scale}','mods_tab 1','frames 3']
        lines += button('input_mod_path')+[f'text_input {(source/"data").as_posix()}', 'frames 3']
        lines += button('btn_mod_add')+['assert_state mods_count 1', 'assert_state mod_action_failed 0']
        lines += button('btn_mods_conflicts', True)+['assert_state mod_action_failed 0']
        lines += ['reveal mod_install_summary','frames 3','reveal mod_install_summary','frames 3',
                  'assert_widget mod_install_summary','dump_widget mod_install_summary',f'screenshot {screenshot.as_posix()}']
        lines += button('btn_mods_deploy', True)+['assert_log mods deploy: done','assert_state mod_action_failed 0']
        lines += button('btn_mods_conflicts', True)+['assert_state mod_action_failed 0']
        lines += button('btn_mods_deploy', True)+['assert_state mod_action_failed 0']
        lines += button('btn_mods_undeploy', True)+['assert_log mods undeploy: done','assert_state mod_action_failed 0','quit']
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        with (case/'output.log').open('w', encoding='utf-8') as log:
            proc = subprocess.Popen([str(repo/'build/FableForge.exe'), '--install', str(root),
                '--size', size, '--auto', str(script)], cwd=repo, stdout=log, stderr=subprocess.STDOUT,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(case/'appdata'),
                         LOCALAPPDATA=str(case/'localappdata')))
            try: rc = proc.wait(timeout=300)
            finally:
                if proc.poll() is None: proc.terminate(); proc.wait(timeout=10)
        trace = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc == 0 and 'RESULT PASS' in trace, trace[-5000:]
        order = json.loads((root/'forge_mods.json').read_text())['mods']
        assert len(order) == 1 and order[0]['install_data'] and Path(order[0]['source']) == source
        assert {k:v for k,v in snapshot(root).items() if k != 'forge_mods.json'} == baseline
        assert snapshot(source) == original
        match = re.search(r'widget_rect mod_install_summary ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', trace)
        assert match
        x1,y1,x2,y2 = map(float, match.groups())
        with Image.open(screenshot) as image:
            assert 0 <= x1 < x2 <= image.width and 0 <= y1 < y2 <= image.height
        print(size, 'actual Add Data, complete install, check/redeploy/undeploy: PASS', flush=True)
    (work/'result.json').write_text(json.dumps({'passed': True, 'sizes': ['1440x900','800x600'],
                                               'gameplay_tested': False}, indent=2))


if __name__ == '__main__': main()
