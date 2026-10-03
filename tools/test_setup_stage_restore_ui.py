#!/usr/bin/env python3
"""Setup restores a staged baseline and visibly reports orphaned stage backups."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time
from test_setup_restore_ui import level


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--orphan', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='setup-stage-restore-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        root = case/'install'
        bank = root/'data/CompiledDefs/game.bin'
        bank.parent.mkdir(parents=True); bank.write_bytes(b'x')
        levels = root/'data/Levels/FinalAlbion'
        levels.mkdir(parents=True)
        target = levels/'StageRestore.lev'
        target.write_bytes(level(4))
        (levels/'StageRestore.tng').write_bytes(b'Version 2;\r\n')
        base = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
        overlay = case/'overlay/data/Levels/FinalAlbion'
        overlay.mkdir(parents=True)
        (overlay/'StageRestore.lev').write_bytes(level(9))
        stage = subprocess.run([str(repo/'build/forge-tools.exe'), 'stage', str(root),
            str(case/'overlay')], cwd=repo, capture_output=True, text=True, timeout=30)
        (case/'stage.log').write_text(stage.stdout+stage.stderr)
        assert stage.returncode == 0, stage.stdout+stage.stderr
        staged_backup = Path(str(target)+'.forgebak')
        manifest = root/'forge_stage_manifest.json'
        original = Path(str(target)+'.forge-orig')
        if args.orphan:
            manifest.rename(case/manifest.name)
        else:
            original.write_bytes(level(9))
            stamp = staged_backup.stat().st_mtime_ns+2_000_000_000
            os.utime(original, ns=(stamp, stamp))
            target.write_bytes(level(7))
        before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
        def reveal(widget):
            return [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3']
        lines = ['wait_maps', 'wait_ready', 'select StageRestore', 'wait_loaded',
            f'set uiscale {scale}', 'setup 1', 'frames 3']
        lines += reveal('btn_restore_all')+['click btn_restore_all', 'frames 3']
        lines += reveal('btn_restore_confirm')+['click btn_restore_confirm', 'frames 3']
        if args.orphan:
            lines += ['assert_log outside any stage manifest', 'assert_state setup_restore_failed 1',
                'assert_state terrain_center_height 9.000']
            lines += reveal('setup_restore_error')+['assert_widget setup_restore_error', 'clear_toasts']
            lines += [f'screenshot {(case/"orphan.png").as_posix()}', 'frames 300']
            lines += reveal('btn_restore_all')+['click btn_restore_all', 'frames 3']
            lines += reveal('btn_restore_confirm')+['click btn_restore_confirm', 'frames 3',
                'wait_maps', 'wait_ready', 'wait_loaded', 'assert_state setup_restore_failed 0',
                'assert_state backups_differ 0', 'assert_state terrain_center_height 4.000']
        else:
            lines += ['wait_maps', 'wait_ready', 'wait_loaded', 'assert_log rebased',
                'assert_state setup_restore_failed 0', 'assert_state backups_differ 0',
                'assert_state terrain_center_height 4.000']
        lines += [f'screenshot {(case/"restored.png").as_posix()}', 'quit']
        script = case/'restore.txt'
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        with (case/'output.log').open('w', encoding='utf-8') as output:
            command = [str(repo/'build/FableForge.exe'), '--install', str(root),
                '--size', size, '--auto', str(script)]
            env = dict(os.environ,
                    FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(case/'appdata'),
                    LOCALAPPDATA=str(case/'localappdata'))
            if args.orphan:
                process = subprocess.Popen(command, cwd=repo, stdout=output,
                    stderr=subprocess.STDOUT, env=env)
                try:
                    deadline = time.monotonic()+60
                    while not (case/'orphan.png').exists() and process.poll() is None and time.monotonic() < deadline:
                        time.sleep(0.01)
                    assert (case/'orphan.png').exists(), 'orphan checkpoint missing'
                    assert {p: p.read_bytes() for p in root.rglob('*') if p.is_file()} == before
                    (case/manifest.name).rename(manifest)
                    rc = process.wait(timeout=60)
                finally:
                    if process.poll() is None:
                        process.terminate(); process.wait(timeout=10)
            else:
                rc = subprocess.run(command, cwd=repo, stdout=output,
                    stderr=subprocess.STDOUT, env=env, timeout=60).returncode
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc == 0 and 'RESULT PASS' in log, log[-5000:]
        expected = dict(base)
        if not args.orphan: expected[original] = level(4)
        assert {p: p.read_bytes() for p in root.rglob('*') if p.is_file()} == expected
        print(size, 'orphan preserved, explained and retried with recovered manifest' if args.orphan else 'stage baseline rebased and restored', 'PASS', flush=True)


if __name__ == '__main__':
    main()
