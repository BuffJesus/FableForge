#!/usr/bin/env python3
"""Check map-scoped session restore using isolated settings files."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='session-ui-',dir=repo/'build'))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir()
        settings=case/'settings.json'
        def run(name,lines,enabled=True):
            script=case/(name+'.txt');script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1')
            env.pop('FABLEFORGE_TEST_SETTINGS',None)
            if enabled: env['FABLEFORGE_TEST_SETTINGS']=str(settings)
            result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(args.install),
                '--size',size,'--auto',str(script)],cwd=repo,capture_output=True,text=True,timeout=180,env=env)
            log=Path(str(script)+'.log').read_text(encoding='utf-8')
            (case/(name+'.output.log')).write_text(log+result.stderr,encoding='utf-8')
            assert result.returncode==0 and 'RESULT PASS' in log,log[-4000:]+result.stderr
            return json.loads(settings.read_text(encoding='utf-8')) if settings.exists() else None
        first=run('capture',['wait_maps','wait_ready','select TeleporterGreatwood','wait_loaded','wait_foliage',
            'tour -1',f'set uiscale {scale}','edit 1','edit_tab 1','terrain_mode 16',
            'camera 11 13 45 0.4 0.7 25',
            'click menu_view','frames 3','click menu_map_camera','frames 3',
            'mouse_move slider_camera_fov','mouse_down Left','mouse_delta -400 0','frames 3',
            'mouse_up Left','frames 3','assert_state_min camera_fov_degrees 29.99',
            'assert_state_max camera_fov_degrees 30.01',
            'mouse_move slider_camera_fov','mouse_down Left','mouse_delta 400 0','frames 3',
            'mouse_up Left','frames 3','assert_state_min camera_fov_degrees 99.99',
            'assert_state_max camera_fov_degrees 100.01',
            'click button_camera_fov_reset','frames 3',
            'assert_state_min camera_fov_degrees 51.56','assert_state_max camera_fov_degrees 51.58',
            'mouse_move slider_camera_fov','key_down Ctrl','click slider_camera_fov','key_up Ctrl',
            'input_text 82','key_down Enter','key_up Enter','frames 3',
            'assert_state_min camera_fov_degrees 81.99','assert_state_max camera_fov_degrees 82.01',
            'assert_state doc_dirty 0','assert_state terrain_draft_dirty 0','mouse_move 20 20','frames 3',
            f'screenshot {(case/"camera-fov.png").as_posix()}',
            'key_down Escape','key_up Escape','frames 3','key_down Escape','key_up Escape','frames 3','quit'])
        assert first['lastMap']=='TeleporterGreatwood' and first['terrainMode']==16
        second=run('restore',['wait_maps','wait_ready','wait_loaded','wait_foliage',
            'assert_state selected TeleporterGreatwood','assert_state edit_mode 1',
            'assert_state edit_tab 1','assert_state terrain_mode 16',
            'assert_state_min camera_fov_degrees 81.99','assert_state_max camera_fov_degrees 82.01','quit'])
        assert second['camera']==first['camera'],(first['camera'],second['camera'])
        default=run('default-auto',['wait_maps','wait_ready','assert_state selected','quit'],False)
        assert default==second
        malformed=copy.deepcopy(first);malformed['camera']['pos'][0]='bad'
        settings.write_text(json.dumps(malformed),encoding='utf-8')
        invalid=run('invalid-camera',['wait_maps','wait_ready','wait_loaded','wait_foliage',
            'assert_state selected TeleporterGreatwood','assert_state edit_mode 1','quit'])
        assert isinstance(invalid['camera']['pos'][0],(int,float))
        different=dict(first);different['install']=str(case/'other-install')
        settings.write_text(json.dumps(different),encoding='utf-8')
        run('install-mismatch',['wait_maps','wait_ready','assert_state selected','quit'])
        missing=dict(first);missing['lastMap']='MissingMap'
        settings.write_text(json.dumps(missing),encoding='utf-8')
        run('missing-map',['wait_maps','wait_ready','assert_state selected','quit'])
        settings.write_text(json.dumps(first),encoding='utf-8')
        switched=run('switch',['wait_maps','wait_ready','wait_loaded','wait_foliage',
            'select LookoutPoint','wait_loaded','wait_foliage','quit'])
        assert switched['lastMap']=='LookoutPoint' and switched['camera']['map']=='LookoutPoint'
        assert switched['camera']!=first['camera']
        print('PASS session capture, restore, invalid camera and install/map isolation',size,flush=True)


if __name__=='__main__':
    main()
