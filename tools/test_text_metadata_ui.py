#!/usr/bin/env python3
"""Edit bounded ASCII speech references without changing record identity or tags."""
import argparse,os,struct,subprocess,tempfile,time
from pathlib import Path
from test_mod_field_details import write_bank
from test_mod_asset_choice_contributors import bank
from test_fmp_links import package_records
from test_text_content_edit_ui import payload,metadata

def main():
    repo=Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    ap.add_argument('--cwd',type=Path,default=repo)
    ap.add_argument('--work-root',type=Path,default=repo/'build')
    a=ap.parse_args();work=Path(tempfile.mkdtemp(prefix='text-metadata-',dir=a.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    def legacy(identifier=''):
        raw=b'\xe9Bank.lug';return payload('Legacy original',identifier).replace(struct.pack('<I',10)+b'Speech.lug',struct.pack('<I',len(raw))+raw,1)
    def fill(widget,value):
        return [f'reveal {widget}','frames 3',f'click {widget}','frames 3','key_down Ctrl','key_down A',
            'key_up A','key_up Ctrl','input_text '+value,'frames 3']
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir();root=case/'install';write_bank(root,(1,2,3,4))
        levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        target=root/'data/lang/English/text.big'
        records={'TXT_A':{'id':10,'type':0,'header':struct.pack('<I',1),'payload':payload('Original','ORIGINAL_KEY')},
            'TXT_OTHER':{'id':11,'type':0,'header':bytes(4),'payload':payload('Other',speaker='NONE')},
            'TXT_LEGACY':{'id':12,'type':0,'header':struct.pack('<I',1),'payload':legacy()},
            '[NarratorList]':{'id':20,'type':2,'header':struct.pack('<I',2)+b'NONE\0HERO\0','payload':b''}}
        bank(target,'TEXT_MAIN',records);original=target.read_bytes();headers=metadata(target)
        first=case/'first.png';checkpoint=case/'saved.png';draft=case/'draft.png';refused=case/'refused.png'
        lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 5','wait_texts','frames 3',
            'text_select TXT_A','text_edit','text_edit_speechbank_refused \u03a9',
            'text_edit_identifier_refused '+('X'*4096)]
        lines+=fill('input_text_edit_speechbank','EditedSpeech.lug\u03a9')
        lines+=fill('input_text_edit_identifier','AUDIO_KEY\u6f22')
        lines+=['reveal btn_text_save','frames 3','click btn_text_save','frames 3',
            'reveal btn_text_save_confirm','frames 3','click btn_text_save_confirm','frames 3',
            'wait_texts','frames 3',f'screenshot {first.as_posix()}','frames 300',
            'text_select TXT_A','text_edit','text_edit_speechbank -','text_edit_identifier -',
            'text_save','wait_texts','frames 3','text_select TXT_A','text_add','text_add_name TXT_NEW',
            'text_edit_identifier_refused CUSTOM_KEY','text_edit_speechbank NewSpeech.lug',
            'text_edit_content Added line','text_save','wait_texts','frames 3',
            'text_select TXT_LEGACY','text_edit','text_edit_speechbank_refused Replacement.lug',
            'text_edit_identifier LEGACY_KEY','text_save','wait_texts','frames 3',
            f'screenshot {checkpoint.as_posix()}','frames 300',
            'text_select TXT_A','text_edit','text_edit_identifier AttemptedOverwrite',
            f'screenshot {draft.as_posix()}','frames 300','text_save_refused','assert_log Text changed on disk',
            f'screenshot {refused.as_posix()}','frames 180',
            'reveal btn_text_edit_cancel','frames 3','click btn_text_edit_cancel','frames 3',
            'restore_all','wait_ready','assets_tab 5','wait_texts','frames 3','text_select TXT_A',
            'assert_text_content Original','quit']
        script=case/'metadata.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
        with (case/'output.log').open('w',encoding='utf-8') as log:
            process=subprocess.Popen([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=env,stdout=log,stderr=subprocess.STDOUT)
            try:
                def wait_for(path):
                    deadline=time.monotonic()+90
                    while not path.exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                    assert path.exists(),(case/'output.log').read_text(encoding='utf-8',errors='replace')[-3500:]
                wait_for(first)
                assert package_records(target,by_id=True)['TEXT_MAIN'][10][0]==payload('Original','AUDIO_KEY',speech_bank='EditedSpeech.lug')
                assert metadata(target)==headers and Path(str(target)+'.forge-orig').read_bytes()==original
                wait_for(checkpoint)
                current=package_records(target,by_id=True)['TEXT_MAIN'];changed=metadata(target)
                assert current[10][0]==payload('Original',speech_bank='')
                assert current[21][0]==payload('Added line','TXT_NEW',speech_bank='NewSpeech.lug')
                assert current[12][0]==legacy('LEGACY_KEY') and current[11][0]==records['TXT_OTHER']['payload']
                for key,header in headers.items():assert changed[key]==header
                added=changed[(b'TEXT_MAIN',21)];donor=headers[(b'TEXT_MAIN',10)]
                assert added[:2]==donor[:2] and added[2]==42 and added[3:5]==donor[3:5]
                assert added[-1][-8:]==donor[-1][-8:]
                assert Path(str(target)+'.forge-orig').read_bytes()==original
                wait_for(draft)
                records['TXT_A']['payload']=payload('Original',speech_bank='External.lug')
                records['TXT_LEGACY']['payload']=legacy('LEGACY_KEY')
                records['TXT_NEW']={'id':21,'type':0,'header':struct.pack('<I',1),'payload':payload('Added line','TXT_NEW',speech_bank='NewSpeech.lug')}
                bank(target,'TEXT_MAIN',records);external=target.read_bytes()
                wait_for(refused);assert target.read_bytes()==external and Path(str(target)+'.forge-orig').read_bytes()==original
                rc=process.wait(timeout=90)
            finally:
                if process.poll() is None:process.terminate();process.wait(timeout=10)
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc==0 and 'RESULT PASS' in trace,trace[-4500:]
        assert target.read_bytes()==original and Path(str(target)+'.forge-orig').read_bytes()==original
        assert not list(target.parent.glob('.forge-text-write-*'))
        print(size,'real reference inputs, ASCII filter, empty values, donor identity, legacy bytes, stale refusal and Restore: PASS',flush=True)

if __name__=='__main__':main()
