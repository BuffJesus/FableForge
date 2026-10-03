#!/usr/bin/env python3
"""Setup confirmations cancel on Escape, closing Setup and changing installs."""
import os
from pathlib import Path
import subprocess
import tempfile
from test_install_switch_ui import make_install
from test_setup_restore_ui import level


def main():
    repo = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix='setup-confirmation-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        first, second = case/'first', case/'second'
        for root, name in [(first, 'ConfirmA'), (second, 'ConfirmB')]:
            make_install(root, name, 9)
            target = root/'data/Levels/FinalAlbion'/(name+'.lev')
            Path(str(target)+'.forge-orig').write_bytes(level(4))
        before = {p: p.read_bytes() for root in [first, second] for p in root.rglob('*') if p.is_file()}
        def reveal(widget):
            return [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3']
        def confirm():
            return reveal('btn_restore_all')+['click btn_restore_all', 'frames 3',
                'assert_state confirm_pending 1']+reveal('btn_restore_confirm')+['assert_widget btn_restore_confirm']
        escape = ['key_down Escape', 'key_up Escape', 'frames 3']
        lines = ['wait_maps', 'wait_ready', 'select ConfirmA', 'wait_loaded',
            f'set uiscale {scale}', 'setup 1', 'frames 3']
        lines += confirm()+['key_down Enter', 'key_up Enter', 'frames 3',
            'assert_state confirm_pending 1', f'scan_install {(first/"data").as_posix()}',
            'wait_maps', 'wait_ready', 'frames 3', 'assert_state confirm_pending 1']+escape+['assert_state confirm_pending 0',
            'assert_state setup_open 1', 'assert_state terrain_center_height 9.000']
        lines += confirm()+reveal('btn_setup_continue')+['click btn_setup_continue', 'frames 3',
            'assert_state setup_open 0', 'assert_state confirm_pending 0', 'setup 1', 'frames 3']
        lines += reveal('btn_restore_all')+['assert_widget btn_restore_all']+escape
        lines += confirm()+[f'scan_install {second.as_posix()}', 'wait_maps', 'wait_ready',
            'frames 3', 'assert_state confirm_pending 0']
        lines += reveal('btn_restore_all')+['assert_widget btn_restore_all']+escape
        lines += confirm()+[f'scan_install {(case/"missing").as_posix()}', 'frames 3',
            'assert_state install_valid 0', 'assert_state confirm_pending 0', 'quit']
        script = case/'confirm.txt'
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        with (case/'output.log').open('w', encoding='utf-8') as output:
            result = subprocess.run([str(repo/'build/FableForge.exe'), '--install', str(first),
                '--size', size, '--auto', str(script)], cwd=repo, stdout=output,
                stderr=subprocess.STDOUT, timeout=60, env=dict(os.environ,
                    FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(case/'appdata'),
                    LOCALAPPDATA=str(case/'localappdata')))
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert {p: p.read_bytes() for root in [first, second] for p in root.rglob('*') if p.is_file()} == before
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-6000:]
        print(size, 'Enter ignored; Escape, close and install changes cancel without writes: PASS', flush=True)


if __name__ == '__main__':
    main()
