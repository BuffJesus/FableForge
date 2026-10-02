#!/usr/bin/env python3
"""Check owned recovery archives, process reopen, timer and real recovery controls."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='recovery-ui-',dir=repo/'build'))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size; case.mkdir()
        def base(folder):
            return ['wait_maps','wait_ready',f'set recovery_root {(folder/"archives").as_posix()}',
                f'set saveroot {(folder/"drafts").as_posix()}', 'set recovery_prompt 1',
                'select TeleporterGreatwood','wait_loaded','wait_foliage','edit 1','tour -1',f'set uiscale {scale}']
        def run(name,lines):
            script=case/(name+'.txt');script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(args.install),
                '--size',size,'--auto',str(script)],cwd=repo,capture_output=True,text=True,timeout=180,
                env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
            (case/(name+'.output.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
            log=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert result.returncode==0 and 'RESULT PASS' in log,log[-6000:]+result.stderr
        run('write',base(case)+['assert_state recovery_pending 0','terrain_set_all 20',
            'set place_snap_grid 0','camera 10 10 20 0 1.5 20','place OBJECT_BARREL_BREAKABLE RecoveryBarrel',
            'autosave_now','move_thing 5 0 0','terrain_set_all 21','wait_autosave','assert_autosave_exists 1',
            'assert_height 10 10 21','assert_state doc_dirty 1','assert_state terrain_draft_dirty 1','quit'])
        blocked=case/'drafts/data/Levels/FinalAlbion/TeleporterGreatwood.tng'
        blocked.mkdir(parents=True)
        archives_before={p:p.read_bytes() for p in (case/'archives').rglob('*.ffrecovery')}
        run('recover',base(case)+['assert_state recovery_pending 1','snapshot_document','snapshot_heights',
            f'screenshot {(case/"prompt.png").as_posix()}',
            'click btn_recovery_recover','frames 4','wait_foliage','assert_state recovery_pending 0',
            'assert_height 10 10 20','select_script RecoveryBarrel','assert_selected_xy 10 10','assert_state doc_dirty 1',
            'assert_state terrain_draft_dirty 1','undo','assert_document_same','assert_heights_changed 0',
            'redo','assert_height 10 10 20','select_script RecoveryBarrel','save_draft_refused',
            'assert_autosave_exists 1','assert_state doc_dirty 1','assert_state terrain_draft_dirty 1','quit'])
        assert all(p.read_bytes()==data for p,data in archives_before.items())
        assert blocked.resolve().is_relative_to(work)
        blocked.rmdir()
        run('save',base(case)+['click btn_recovery_recover','frames 4','wait_foliage',
            'assert_height 10 10 20','select_script RecoveryBarrel','save_draft',
            'assert_autosave_exists 0','assert_state doc_dirty 0','assert_state terrain_draft_dirty 0','quit'])
        timer=case/'timer'; timer.mkdir()
        run('timer',base(timer)+['set recovery_timer 1','set autosave 0','terrain_set_all 24',
            'autosave_due','frames 4','assert_autosave_exists 0','set autosave 1','frames 4',
            'wait_autosave','assert_autosave_exists 1','assert_state terrain_draft_dirty 1','quit'])
        run('discard',base(timer)+['assert_state recovery_pending 1','snapshot_document','snapshot_heights',
            'click btn_recovery_discard','frames 4','assert_state recovery_pending 0',
            'assert_autosave_exists 0','assert_document_same','assert_heights_changed 0','quit'])
        print('PASS recovery controls and timer',size,flush=True)


if __name__=='__main__':
    main()
