#!/usr/bin/env python3
"""Choose a known narrator in Text drafts, preserve other metadata and restore."""
import argparse,os,struct,subprocess,tempfile,time
from pathlib import Path
from test_mod_field_details import write_bank
from test_mod_asset_choice_contributors import bank
from test_fmp_links import package_records
from test_text_content_edit_ui import payload,metadata

def main():
    repo=Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    ap.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    ap.add_argument('--cwd',type=Path,default=repo)
    ap.add_argument('--work-root',type=Path,default=repo/'build')
    a=ap.parse_args();work=Path(tempfile.mkdtemp(prefix='text-speaker-picker-',dir=a.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir();root=case/'install';write_bank(root,(1,2,3,4))
        levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        target=root/'data/lang/English/text.big'
        records={'TXT_DONOR':{'id':10,'type':0,'header':struct.pack('<I',1),'payload':payload('Original')},
            'TXT_OTHER':{'id':11,'type':0,'header':bytes(4),'payload':payload('Other',speaker='NONE')},
            'TXT_BAD_INDEX':{'id':12,'type':0,'header':struct.pack('<I',99),'payload':payload('Bad index')},
            '[NarratorList]':{'id':20,'type':2,'header':struct.pack('<I',4)+b'NONE\0HERO\0FARMER\0\0','payload':b''}}
        bank(target,'TEXT_MAIN',records);original=target.read_bytes();headers=metadata(target)
        checkpoint=case/'saved.png';draft=case/'stale-draft.png';refused=case/'stale-refused.png'
        lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 5','wait_texts','frames 3',
            'text_select TXT_BAD_INDEX','text_edit','text_edit_speaker FARMER','text_save_refused',
            'assert_log speaker header index is out of range',
            'reveal btn_text_edit_cancel','frames 3','click btn_text_edit_cancel','frames 3',
            'text_select TXT_DONOR','text_edit','text_edit_speaker_refused UNKNOWN',
            'reveal combo_text_edit_speaker','frames 3','click combo_text_edit_speaker','frames 3',
            'assert_widget text_speaker_option_2','click text_speaker_option_2','frames 3',
            'text_save','wait_texts','frames 3','text_select TXT_DONOR',
            'text_add','text_add_name TXT_NEW','text_edit_speaker -','text_edit_content Added line',
            'text_save','wait_texts','frames 3',f'screenshot {checkpoint.as_posix()}',
            'frames 300','text_select TXT_DONOR','text_edit','text_edit_speaker HERO',
            f'screenshot {draft.as_posix()}','frames 300','text_save_refused',
            'assert_log Speaker header changed on disk',f'screenshot {refused.as_posix()}',
            'frames 180','reveal btn_text_edit_cancel','frames 3','click btn_text_edit_cancel','frames 3',
            'restore_all','wait_ready','assets_tab 5','wait_texts','frames 3','text_select TXT_DONOR',
            'assert_text_content Original','quit']
        script=case/'picker.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
        with (case/'output.log').open('w',encoding='utf-8') as log:
            process=subprocess.Popen([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=env,stdout=log,stderr=subprocess.STDOUT)
            try:
                def wait_for(path):
                    deadline=time.monotonic()+90
                    while not path.exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                    assert path.exists(),(case/'output.log').read_text(encoding='utf-8')[-3500:]
                wait_for(checkpoint)
                current=package_records(target,by_id=True)['TEXT_MAIN'];changed=metadata(target)
                assert current[10][0]==payload('Original',speaker='FARMER')
                assert current[21][0]==payload('Added line','TXT_NEW',speaker='')
                key=(b'TEXT_MAIN',10)
                assert changed[key][:-1]==headers[key][:-1] and changed[key][-1][:-4]==headers[key][-1][:-4]
                assert struct.unpack('<I',changed[key][-1][-4:])[0]==2
                assert struct.unpack('<I',changed[(b'TEXT_MAIN',21)][-1][-4:])[0]==3
                for name,record in records.items():
                    if record['id']!=10:
                        assert current[record['id']][0]==record['payload']
                        assert changed[(b'TEXT_MAIN',record['id'])]==headers[(b'TEXT_MAIN',record['id'])]
                assert Path(str(target)+'.forge-orig').read_bytes()==original
                wait_for(draft)
                records['TXT_DONOR']['payload']=payload('Original',speaker='FARMER')
                records['TXT_DONOR']['header']=bytes(4)
                records['TXT_NEW']={'id':21,'type':0,'header':struct.pack('<I',3),'payload':payload('Added line','TXT_NEW',speaker='')}
                bank(target,'TEXT_MAIN',records);external=target.read_bytes()
                wait_for(refused);assert target.read_bytes()==external
                assert Path(str(target)+'.forge-orig').read_bytes()==original
                rc=process.wait(timeout=90)
            finally:
                if process.poll() is None:process.terminate();process.wait(timeout=10)
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc==0 and 'RESULT PASS' in trace,trace[-4500:]
        assert target.read_bytes()==original and Path(str(target)+'.forge-orig').read_bytes()==original
        assert not list(target.parent.glob('.forge-text-write-*'))
        destination=case/'mod folder';defs=destination/'data/CompiledDefs';defs.mkdir(parents=True)
        (defs/'game.bin').write_bytes((root/'data/CompiledDefs/game.bin').read_bytes())
        copied=destination/'data/lang/English/text.big';folder_script=case/'folder.txt'
        folder_script.write_text('\n'.join(['wait_maps','wait_ready',f'set uiscale {scale}',
            f'set saveroot {destination.as_posix()}','assets_tab 5','wait_texts','frames 3',
            'text_select TXT_DONOR','text_edit','text_edit_speaker FARMER','text_save',
            'wait_texts','frames 3','text_select TXT_DONOR','assert_text_content Original','quit'])+'\n',encoding='utf-8')
        result=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(folder_script)],
            cwd=a.cwd.resolve(),env=env,capture_output=True,text=True,encoding='utf-8',timeout=90)
        trace=Path(str(folder_script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace,trace[-3500:]
        assert package_records(copied,by_id=True)['TEXT_MAIN'][10][0]==payload('Original',speaker='FARMER')
        assert struct.unpack('<I',metadata(copied)[(b'TEXT_MAIN',10)][-1][-4:])[0]==2
        assert Path(str(copied)+'.forge-created').is_file() and not Path(str(copied)+'.forge-orig').exists()
        assert target.read_bytes()==original and Path(str(target)+'.forge-orig').read_bytes()==original
        result=subprocess.run([str(a.forge.resolve()),'restore','--install',str(destination)],
            cwd=a.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',timeout=30)
        assert result.returncode==0 and not copied.exists() and not Path(str(copied)+'.forge-created').exists(),result.stdout+result.stderr
        assert target.read_bytes()==original and not list(copied.parent.glob('.forge-text-write-*'))
        print(size,'real speaker picker, empty-speaker donor, stale refusal, folder creation and exact Restore: PASS',flush=True)

if __name__=='__main__':main()
