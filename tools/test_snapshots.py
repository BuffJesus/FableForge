#!/usr/bin/env python3
"""Check dated map snapshot capture, history and restore through real controls."""
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
    work=Path(tempfile.mkdtemp(prefix='snapshots-ui-',dir=repo/'build'))
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
        menu=['click menu_file','frames 3','click menu_recovery','frames 3']
        library=menu+['click menu_snapshots','frames 4','assert_state snapshots_open 1']
        restore=['click btn_recovery_recover','frames 4','wait_foliage']
        run('capture',base(case)+['set autosave 0','terrain_set_all 40',
            'set place_snap_grid 0','camera 10 10 40 0 1.5 20',
            'place OBJECT_BARREL_BREAKABLE SnapshotProp',
            'key_down Ctrl','key_down B','frames 3','key_up B','key_up Ctrl',
            'wait_autosave','terrain_set_all 41','select_script SnapshotProp','move_thing 5 0 0']+menu+[
            'click menu_snapshot_create','frames 3','wait_autosave']+library+[
            'assert_state snapshot_count 2',f'screenshot {(case/"library.png").as_posix()}',
            'click btn_snapshot_restore_1','frames 3','assert_state recovery_pending 1']+restore+[
            'assert_height 10 10 40','select_script SnapshotProp','assert_selected_xy 10 10',
            'undo','assert_height 10 10 41','select_script SnapshotProp','assert_selected_xy 15 10',
            'redo','assert_height 10 10 40','select_script SnapshotProp','assert_selected_xy 10 10',
            'save_draft','assert_state snapshot_count 2','quit'])
        history={p:p.read_bytes() for p in (case/'snapshots').rglob('*.ffsnapshot')}
        assert len(history)==2
        run('reopen',base(case)+['set autosave 0']+library+[
            'assert_state snapshot_count 2','terrain_set_all 42','autosave_now','wait_autosave',
            'assert_autosave_exists 1','click btn_snapshot_restore_1','frames 3',
            'click btn_recovery_discard','frames 3','assert_state recovery_pending 0',
            'assert_height 10 10 42','assert_autosave_exists 1','assert_state snapshot_count 2',
            'click btn_snapshot_restore_1','frames 3']+restore+[
            'assert_height 10 10 40','select_script SnapshotProp','assert_selected_xy 10 10',
            'save_draft','assert_autosave_exists 0','assert_state snapshot_count 2',
            'click btn_snapshot_delete_1','frames 3','assert_state snapshot_count 2',
            'assert_state confirm_pending 1','key_down Escape','key_up Escape','frames 3',
            'assert_state confirm_pending 0','assert_state snapshots_open 1','assert_state snapshot_count 2',
            'click btn_snapshot_delete_1','frames 3','click btn_snapshot_delete_confirm_1','frames 4',
            'assert_state snapshot_count 1','assert_state confirm_pending 0',
            'click btn_snapshot_restore_0','frames 3']+restore+[
            'assert_height 10 10 41','select_script SnapshotProp','assert_selected_xy 15 10',
            'select LookoutPoint','wait_loaded','wait_foliage','assert_state snapshots_open 0',
            'assert_state snapshot_count 0','quit'])
        remaining=list((case/'snapshots').rglob('*.ffsnapshot'))
        assert len(remaining)==1 and remaining[0].read_bytes()==history[remaining[0]]
        run('isolated',['wait_maps','wait_ready','select TeleporterGreatwood','wait_loaded',
            'wait_foliage','edit 1','tour -1','set autosave 0','terrain_set_all 42',
            'key_down Ctrl','key_down B','frames 3','key_up B','key_up Ctrl',
            'assert_state recovery_busy 0']+menu+['click menu_snapshot_create','frames 3',
            'assert_state recovery_busy 0','key_down Escape','key_up Escape','frames 2',
            'key_down Escape','key_up Escape','frames 2']+menu+['click menu_snapshots','frames 4',
            'assert_state snapshots_open 0','assert_state snapshot_count 0','quit'])
        print('PASS dated snapshot capture, reopen, restore, undo and deletion',size,flush=True)


if __name__=='__main__':
    main()
