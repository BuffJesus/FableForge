#!/usr/bin/env python3
"""Window close protects text drafts and routes review back to Text."""
import argparse,os,struct,subprocess,tempfile
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
    a=ap.parse_args();work=Path(tempfile.mkdtemp(prefix='text-close-prompt-',dir=a.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        for mode in ('review-save','review-failed','mixed-dialogue','discard','cancel'):
            case=work/size/mode;case.mkdir(parents=True);root=case/'install';write_bank(root,(1,2,3,4))
            levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
            target=root/'data/lang/English/text.big'
            records={'TXT_A':{'id':10,'type':0,'header':struct.pack('<I',0),'payload':payload('Original')},
                'TXT_OTHER':{'id':11,'type':0,'header':struct.pack('<I',0),'payload':payload('Other')}}
            bank(target,'TEXT_MAIN',records);original=target.read_bytes();headers=metadata(target)
            lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 5','wait_texts','frames 3','text_select TXT_A','text_edit','text_edit_content Draft saved',
                'close','frames 3','assert_state close_prompt 1','assert_widget btn_unsaved_cancel','assert_text_edit TXT_A']
            if mode=='review-save':
                lines+=['click btn_unsaved_cancel','frames 3','assert_state close_prompt 0','assert_text_edit TXT_A',
                    'close','frames 3','click btn_unsaved_save','frames 3','assert_state close_prompt 0','assert_state assets_tab 5','assert_text_edit TXT_A',
                    'reveal btn_text_save','frames 3','click btn_text_save','frames 3','reveal btn_text_save_confirm','frames 3','click btn_text_save_confirm',
                    'wait_texts','frames 3','assert_text_edit -','close']
            elif mode=='review-failed':
                alternate=case/'alternate';alternate.mkdir()
                lines+=['click btn_unsaved_save','frames 3','assert_state close_prompt 0','assert_text_edit TXT_A',
                    f'set saveroot {alternate.as_posix()}','wait_texts','frames 3','text_save_refused',
                    'assert_log Text source or destination changed','close','frames 3','assert_state close_prompt 1',
                    'click btn_unsaved_cancel','frames 3','assert_state close_prompt 0','assert_text_edit TXT_A','quit']
            elif mode=='discard':lines+=['click btn_unsaved_discard']
            else:lines+=['click btn_unsaved_cancel','frames 3','assert_state close_prompt 0','assert_text_edit TXT_A','quit']
            if mode=='mixed-dialogue':
                dialogue=root/'data/lang/English/dialogue.big'
                raw=struct.pack('<I',1)+b'\x01AH\0'+struct.pack('<II',43,2)+b'\x01\x01\xff\x01\x01\0'
                bank(dialogue,'LIPSYNC_ENGLISH_MAIN',{'LIP_TEST':{'id':2,'type':1,'header':struct.pack('<f',2/43),'payload':raw}})
                dialogue_original=dialogue.read_bytes()
                def click(widget):return [f'reveal {widget}','frames 3',f'reveal {widget}','frames 3',f'click {widget}','frames 3']
                lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 5','wait_texts','frames 3','text_select TXT_A','text_edit','text_edit_content Draft saved',
                    'assets_tab 4','frames 3']
                lines+=click('header_dialogue_lookup')+['dialogue_select 0 2']
                lines+=click('button_dialogue_load')+click('button_dialogue_edit')+click('button_dialogue_insert_frame')
                lines+=['assert_state dialogue_staged 1','close','frames 3','assert_state close_prompt 1',
                    'click btn_unsaved_save','frames 3','assert_state close_prompt 0','assert_state assets_tab 5','assert_text_edit TXT_A',
                    'assert_state dialogue_staged 1','text_save','wait_texts','frames 3','close','frames 3','assert_state close_prompt 1',
                    'click btn_unsaved_save','frames 3','assert_state close_prompt 0','assert_state assets_tab 4','assert_state dialogue_staged 1','quit']
            script=case/'close.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
            r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=env,capture_output=True,timeout=90)
            trace=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert r.returncode==0 and 'RESULT PASS' in trace and 'ok   assert_state close_prompt 1' in trace,trace[-4500:]
            assert metadata(target)==headers
            if mode in ('review-save','mixed-dialogue'):
                actual=package_records(target,by_id=True)['TEXT_MAIN']
                assert actual[10][0]==payload('Draft saved') and actual[11][0]==records['TXT_OTHER']['payload']
                assert Path(str(target)+'.forge-orig').read_bytes()==original
            else:assert target.read_bytes()==original and not Path(str(target)+'.forge-orig').exists()
            if mode=='mixed-dialogue':assert dialogue.read_bytes()==dialogue_original
            print(size,mode,'text draft retained/reviewed or explicitly discarded: PASS',flush=True)
if __name__=='__main__':main()
