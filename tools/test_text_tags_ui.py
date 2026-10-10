#!/usr/bin/env python3
"""Edit ordered dialogue tags with independent payload/header/Restore checks."""
import argparse,os,struct,subprocess,tempfile,time
from pathlib import Path
from test_mod_field_details import write_bank
from test_mod_asset_choice_contributors import bank
from test_fmp_links import package_records
from test_text_content_edit_ui import payload,metadata

def tagged(content,tags,identifier=''):
    suffix=struct.pack('<Ii',1,2)+b'ANIM:CHEER\0'
    raw=payload(content,identifier)
    assert raw.endswith(suffix)
    return raw[:-len(suffix)]+struct.pack('<I',len(tags))+b''.join(struct.pack('<i',pos)+name+b'\0' for pos,name in tags)

def main():
    repo=Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    ap.add_argument('--cwd',type=Path,default=repo)
    ap.add_argument('--work-root',type=Path,default=repo/'build')
    a=ap.parse_args();work=Path(tempfile.mkdtemp(prefix='text-tags-',dir=a.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    def fill(widget,value):
        return [f'reveal {widget}','frames 3',f'click {widget}','frames 3','key_down Ctrl','key_down A','key_up A','key_up Ctrl','input_text '+value,'frames 3']
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir();root=case/'install';write_bank(root,(1,2,3,4))
        levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        target=root/'data/lang/English/text.big'
        records={'TXT_A':{'id':10,'type':0,'header':struct.pack('<I',0),'payload':tagged('Original',[(2,b'ANIM:CHEER')])},
            'TXT_OTHER':{'id':11,'type':0,'header':struct.pack('<I',0),'payload':payload('Other')},
            'TXT_LEGACY':{'id':12,'type':0,'header':struct.pack('<I',0),'payload':tagged('Legacy',[(1,b'\xe9TAG'),(3,b'ANIM:CHEER')])}}
        bank(target,'TEXT_MAIN',records);original=target.read_bytes();headers=metadata(target)
        first=case/'first.png';second=case/'second.png';draft=case/'draft.png';refused=case/'refused.png'
        lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 5','wait_texts','frames 3','text_select TXT_A','text_edit',
            'text_tag_set_refused 99 0 NO','text_tag_set_refused 0 0 \u03a9','text_tag_remove_refused 99',
            'reveal btn_text_tag_add','frames 3','click btn_text_tag_add','frames 3']
        lines+=fill('input_text_edit_tag_position','-7')
        lines+=fill('input_text_edit_tag_name','CAM:TEST\u03a9')
        lines+=['reveal btn_text_save','frames 3','click btn_text_save','frames 3','reveal btn_text_save_confirm','frames 3',
            'click btn_text_save_confirm','wait_texts','frames 3',f'screenshot {first.as_posix()}','frames 300',
            'text_select TXT_A','text_edit','reveal combo_text_edit_tags','frames 3','click combo_text_edit_tags','frames 3',
            'click text_tag_option_0','frames 3','reveal btn_text_tag_remove','frames 3','click btn_text_tag_remove','frames 3','text_save','wait_texts',
            'text_select TXT_A','text_add','text_add_name TXT_NEW','text_edit_content Added',
            'text_tag_set 0 -2147483648 ANIM:NEW','text_save','wait_texts',
            'text_select TXT_LEGACY','text_edit','text_tag_set_refused 0 2 ASCII','text_tag_set 1 2147483647 ANIM:EDITED','text_save','wait_texts',
            f'screenshot {second.as_posix()}','frames 300',
            'text_select TXT_A','text_edit','text_tag_set 0 4 Attempted',f'screenshot {draft.as_posix()}','frames 300',
            'text_save_refused','assert_log Text changed on disk',f'screenshot {refused.as_posix()}','frames 180',
            'reveal btn_text_edit_cancel','frames 3','click btn_text_edit_cancel','frames 3','restore_all','wait_ready','quit']
        script=case/'tags.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
        with (case/'output.log').open('w',encoding='utf-8') as log:
            process=subprocess.Popen([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=env,stdout=log,stderr=subprocess.STDOUT)
            try:
                def wait_for(path):
                    deadline=time.monotonic()+90
                    while not path.exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                    assert path.exists(),(case/'output.log').read_text(encoding='utf-8',errors='replace')[-3500:]
                wait_for(first)
                assert package_records(target,by_id=True)['TEXT_MAIN'][10][0]==tagged('Original',[(2,b'ANIM:CHEER'),(-7,b'CAM:TEST')])
                assert metadata(target)==headers and Path(str(target)+'.forge-orig').read_bytes()==original
                wait_for(second);current=package_records(target,by_id=True)['TEXT_MAIN']
                assert current[10][0]==tagged('Original',[(-7,b'CAM:TEST')])
                assert current[13][0]==tagged('Added',[(-2147483648,b'ANIM:NEW')],'TXT_NEW')
                assert current[12][0]==tagged('Legacy',[(1,b'\xe9TAG'),(2147483647,b'ANIM:EDITED')])
                for key,header in headers.items():assert metadata(target)[key]==header
                assert current[11][0]==records['TXT_OTHER']['payload']
                wait_for(draft)
                changed={k:dict(v) for k,v in records.items()};changed['TXT_A']['payload']=tagged('External',[(9,b'CAM:EXTERNAL')])
                bank(target,'TEXT_MAIN',changed);external=target.read_bytes();wait_for(refused)
                assert target.read_bytes()==external and Path(str(target)+'.forge-orig').read_bytes()==original
                assert process.wait(timeout=90)==0
                assert target.read_bytes()==original and Path(str(target)+'.forge-orig').read_bytes()==original
                assert not list(target.parent.glob('.forge-text-write-*'))
            finally:
                if process.poll() is None:process.kill();process.wait()
        print(size,'real tag inputs/add/remove, ordering, signed positions, legacy/stale preservation and Restore: PASS',flush=True)
if __name__=='__main__':main()
