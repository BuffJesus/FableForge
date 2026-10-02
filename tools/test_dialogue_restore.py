"""Deploy a dialogue recipe, restore through Setup, and verify the visible bank refresh."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from test_setup_restore_ui import level
from test_dialogue_edit import lip_frame_count


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='dialogue-restore-',dir=repo/'build')).resolve()
    print('Evidence retained at',work,flush=True)
    root=work/'install'
    source=args.install/'data/lang/English/dialogue.big'
    archive=root/'data/lang/English/dialogue.big'
    archive.parent.mkdir(parents=True)
    shutil.copyfile(source,archive)
    original=hashlib.sha256(archive.read_bytes()).hexdigest()
    original_frames=lip_frame_count(archive,'LIPSYNC_ENGLISH_MAIN',2)
    defs=root/'data/CompiledDefs';defs.mkdir(parents=True)
    for name in ('game.bin','names.bin'):
        shutil.copyfile(args.install/'data/CompiledDefs'/name,defs/name)
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True)
    (levels/'RestoreTest.lev').write_bytes(level(4))
    (levels/'RestoreTest.tng').write_text('Version 2;\n',encoding='ascii')
    pack=work/'LipPack';pack.mkdir()
    (pack/'forge_pack.json').write_text(json.dumps({'version':1,'name':'Restore dialogue test',
        'models':[],'groundThemes':[],'lipSync':[{'language':'English','bank':'LIPSYNC_ENGLISH_MAIN',
        'soundId':2,'fps':43,'durationBits':struct.unpack('<I',struct.pack('<f',2/43))[0],
        'dictionary':[{'id':1,'symbol':'AH'}],'frames':[[[1,255]],[[1,0]]]}]}),encoding='utf-8')
    tool=repo/'build/forge-tools.exe'
    for command in ([str(tool),'mods','add',str(root),str(pack)],
                    [str(tool),'mods','deploy',str(root)]):
        result=subprocess.run(command,cwd=repo,capture_output=True,text=True,timeout=90)
        assert result.returncode==0,result.stdout+result.stderr
    assert lip_frame_count(archive,'LIPSYNC_ENGLISH_MAIN',2)==2
    script=work/'restore.txt'
    script.write_text('\n'.join(['wait_maps','wait_ready','assets_tab 4','frames 3',
        'reveal header_dialogue_lookup','click header_dialogue_lookup','frames 3',
        'dialogue_select 0 2','reveal button_dialogue_load','click button_dialogue_load','frames 3',
        'assert_state dialogue_frames 2','setup 1','frames 3','reveal btn_restore_all','frames 3',
        'click btn_restore_all','frames 3','reveal btn_restore_confirm','frames 3',
        'click btn_restore_confirm','frames 3','wait_maps','wait_ready',
        'assert_state dialogue_loaded 0','assert_state backups_differ 0','setup 0','frames 3',
        'reveal button_dialogue_load','click button_dialogue_load','frames 3',
        f'assert_state dialogue_frames {original_frames}',f'screenshot {work.as_posix()}/restored.png','quit','']),encoding='utf-8')
    result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(root),'--auto',str(script)],
        cwd=repo,capture_output=True,text=True,timeout=120,
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
    (work/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    log=Path(str(script)+'.log').read_text(encoding='utf-8')
    assert hashlib.sha256(archive.read_bytes()).hexdigest()==original,'restore changed the original archive bytes'
    assert hashlib.sha256(source.read_bytes()).hexdigest()==original,'source install changed'
    assert not (root/'forge_stage_manifest.json').exists(),'staged deployment remains'
    assert result.returncode==0 and 'RESULT PASS' in log,log[-3000:]
    # Restoring disk files must not silently discard separately staged lip-sync edits.
    result=subprocess.run([str(tool),'mods','deploy',str(root)],cwd=repo,capture_output=True,text=True,timeout=90)
    assert result.returncode==0,result.stdout+result.stderr
    draft=work/'restore-staged.txt'
    body=script.read_text(encoding='utf-8').replace('wait_ready\nassets_tab','wait_ready\nset uiscale 1.5\nassets_tab',1)
    body=body.replace('setup 1\n',
        'reveal button_dialogue_edit\nframes 3\nclick button_dialogue_edit\nframes 3\n'
        'reveal button_dialogue_insert_frame\nframes 3\nclick button_dialogue_insert_frame\nframes 3\n'
        'assert_state dialogue_frames 3\nassert_state dialogue_staged 1\nsetup 1\n',1)
    body=body.replace('assert_state dialogue_loaded 0\n','assert_state dialogue_loaded 0\nassert_state dialogue_staged 1\n')
    body=body.replace(f'assert_state dialogue_frames {original_frames}\n','assert_state dialogue_frames 3\n')
    body=body.replace('/restored.png','/restored-staged.png')
    draft.write_text(body,encoding='utf-8')
    result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(root),'--auto',str(draft),'--size','800x600'],
        cwd=repo,capture_output=True,text=True,timeout=120,env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
    log=Path(str(draft)+'.log').read_text(encoding='utf-8')
    assert result.returncode==0 and 'RESULT PASS' in log,log[-3000:]
    assert hashlib.sha256(archive.read_bytes()).hexdigest()==original
    assert not (root/'forge_stage_manifest.json').exists()
    print('Compact restore preserves separately staged dialogue edits: PASS')
    print('Dialogue recipe deployment, Setup restore, exact archive bytes and preview refresh: PASS')


if __name__=='__main__':
    main()
