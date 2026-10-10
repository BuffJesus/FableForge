#!/usr/bin/env python3
"""Append a donor-based string while preserving originals and refusing unsafe additions."""
import argparse,json,os,struct,subprocess,tempfile,time
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
    a=ap.parse_args();work=Path(tempfile.mkdtemp(prefix='text-donor-add-',dir=a.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir();root=case/'install';write_bank(root,(1,2,3,4))
        levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        target=root/'data/lang/English/text.big'
        records={'TXT_DONOR':{'id':10,'type':0,'header':struct.pack('<I',0),'payload':payload('Donor original Ω')},
            'TXT_OTHER':{'id':11,'type':0,'header':struct.pack('<I',0),'payload':payload('Other original')},
            'TXT_OPAQUE':{'id':12,'type':0,'header':struct.pack('<I',0),'payload':payload('Opaque')+b'tail'},
            'GROUP':{'id':30,'type':1,'header':b'','payload':struct.pack('<II',1,10)},
            'NARRATORS':{'id':31,'type':2,'header':struct.pack('<I',1)+b'HERO\0','payload':b''}}
        bank(target,'TEXT_MAIN',records);original=target.read_bytes();original_metadata=metadata(target)
        full=root/'data/lang/Full/text.big'
        full_records={'TXT_MAX':{'id':0xffffffff,'type':0,'header':struct.pack('<I',0),'payload':payload('Maximum ID')},
            'NARRATORS':records['NARRATORS']}
        bank(full,'TEXT_FULL',full_records);full_original=full.read_bytes()
        edited=case/'added.png';refused=case/'refused.png';script=case/'add.txt'
        cancel=['reveal btn_text_edit_cancel','frames 3','click btn_text_edit_cancel','frames 3','assert_text_edit -']
        lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 5','wait_texts','frames 3',
            'text_select GROUP','text_add_refused','text_select NARRATORS','text_add_refused',
            'text_select TXT_OPAQUE','text_add','text_add_name TXT_BAD','text_save_refused',
            'assert_log Donor payload has unsupported bytes']+cancel+[
            'text_language Full','wait_texts','frames 3','text_select TXT_MAX','text_add',
            'text_add_name TXT_NO_ID','text_save_refused','assert_log no free entry ID']+cancel+[
            'text_language English','wait_texts','frames 3','text_select TXT_DONOR',
            'reveal btn_text_add','frames 3','click btn_text_add','frames 3','assert_text_edit TXT_DONOR',
            'assert_widget input_text_add_name','text_save_refused','assert_log Enter a new text name before saving',
            'text_add_name TXT_OTHER','text_save_refused','assert_log That text name already exists',
            'text_add_name TXT_NEW','text_edit_content New content Ω 漢字',
            'reveal btn_text_save','frames 3','click btn_text_save','frames 3',
            'reveal btn_text_save_confirm','frames 3',f'screenshot {(case/"confirm.png").as_posix()}',
            'frames 3','reveal btn_text_save_confirm','frames 3','click btn_text_save_confirm','frames 3',
            'wait_texts','frames 3','assert_text_edit -','text_select TXT_NEW','assert_text_content New content Ω 漢字',
            'text_select TXT_DONOR','assert_text_content Donor original Ω',
            'text_add','text_add_name TXT_NEXT','text_edit_content Attempted stale donor',
            f'screenshot {edited.as_posix()}','frames 300','text_save_refused','assert_log Donor changed on disk',
            f'screenshot {refused.as_posix()}','frames 180']+cancel+[
            'restore_all','wait_ready','assets_tab 5','wait_texts','frames 3','text_select_refused TXT_NEW',
            'text_select TXT_DONOR','assert_text_content Donor original Ω','quit']
        script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
        with (case/'output.log').open('w',encoding='utf-8') as output:
            process=subprocess.Popen([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
                cwd=a.cwd.resolve(),env=env,stdout=output,stderr=subprocess.STDOUT)
            try:
                deadline=time.monotonic()+90
                while not edited.exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                assert edited.exists(),(case/'output.log').read_text(encoding='utf-8')[-3500:]
                entries=package_records(target,by_id=True)['TEXT_MAIN'];assert len(entries)==len(records)+1
                for record in records.values():assert entries[record['id']][0]==record['payload']
                assert entries[32][0]==payload('New content Ω 漢字','TXT_NEW')
                actual_metadata=metadata(target)
                assert all(actual_metadata[key]==value for key,value in original_metadata.items())
                donor=original_metadata[(b'TEXT_MAIN',10)];added=actual_metadata[(b'TEXT_MAIN',32)]
                assert added[:2]==donor[:2] and added[2]==42 and added[3:5]==donor[3:5]
                assert added[5]==b'TXT_NEW'+donor[5][len(b'TXT_DONOR'):]
                assert Path(str(target)+'.forge-orig').read_bytes()==original and full.read_bytes()==full_original
                new_record={'id':32,'type':0,'header':struct.pack('<I',0),'payload':payload('New content Ω 漢字','TXT_NEW')}
                records['TXT_DONOR']['payload']=payload('External donor change');records['TXT_NEW']=new_record
                bank(target,'TEXT_MAIN',records);external=target.read_bytes()
                deadline=time.monotonic()+60
                while not refused.exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                assert refused.exists(),(case/'output.log').read_text(encoding='utf-8')[-3500:]
                assert target.read_bytes()==external and Path(str(target)+'.forge-orig').read_bytes()==original
                rc=process.wait(timeout=90)
            finally:
                if process.poll() is None:process.terminate();process.wait(timeout=10)
        trace=Path(str(script)+'.log').read_text(encoding='utf-8');assert rc==0 and 'RESULT PASS' in trace,trace[-5000:]
        assert target.read_bytes()==original and Path(str(target)+'.forge-orig').read_bytes()==original
        assert full.read_bytes()==full_original and not Path(str(full)+'.forge-orig').exists()
        assert not list(target.parent.glob('.forge-text-write-*'))
        print(size,'donor preservation, unique ID/header copy, empty/duplicate/opaque/full/stale refusals and Restore: PASS',flush=True)

if __name__=='__main__':main()
