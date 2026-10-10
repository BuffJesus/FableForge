#!/usr/bin/env python3
"""Edit existing text content, preserve metadata/originals, and restore it."""
import argparse,json,os,struct,subprocess,tempfile
from pathlib import Path
from test_mod_field_details import write_bank
from test_mod_asset_choice_contributors import bank
from test_fmp_links import package_records

def payload(content,identifier='',speaker='HERO',speech_bank='Speech.lug'):
    def field(value):
        value=value.encode('ascii');return struct.pack('<I',len(value))+value
    return (content.encode('utf-16-le')+b'\0\0'+field(speech_bank)+field(speaker)+field(identifier)+
            struct.pack('<Ii',1,2)+b'ANIM:CHEER\0')

def metadata(path):
    raw=path.read_bytes();u32=lambda pos:struct.unpack_from('<I',raw,pos)[0]
    pos=u32(8);count=u32(pos);pos+=4;out={}
    for _ in range(count):
        end=next(i for i in range(pos,len(raw)) if raw[i] in (0,255));name=raw[pos:end];pos=end+1
        bank_id,entries,start,_,block_size=struct.unpack_from('<5I',raw,pos);pos+=20
        cursor=start+4+u32(start)*8
        for _ in range(entries):
            magic,entry_id,kind,length,offset,crc,size=struct.unpack_from('<7I',raw,cursor);cursor+=28
            identity_start=cursor
            cursor+=size+4;sources=u32(cursor);cursor+=4
            for _ in range(sources):cursor+=4+u32(cursor)
            cursor+=4+u32(cursor)
            out[(name,entry_id)]=(bank_id,block_size,magic,kind,crc,raw[identity_start:cursor])
    return out

def main():
    repo=Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    ap.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    ap.add_argument('--cwd',type=Path,default=repo)
    ap.add_argument('--work-root',type=Path,default=repo/'build')
    a=ap.parse_args();work=Path(tempfile.mkdtemp(prefix='text-content-edit-',dir=a.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir();root=case/'install';write_bank(root,(1,2,3,4))
        levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        target=root/'data/lang/English/text.big'
        records={'TXT_HELLO':{'id':10,'type':0,'header':struct.pack('<I',0),'payload':payload('Original Ω 漢字')},
            'TXT_OTHER':{'id':11,'type':0,'header':struct.pack('<I',0),'payload':payload('Unrelated content')},
            'GROUP':{'id':12,'type':1,'header':b'','payload':struct.pack('<II',1,10)},
            'NARRATORS':{'id':13,'type':2,'header':struct.pack('<I',1)+b'HERO\0','payload':b''},
            'TXT_OPAQUE':{'id':14,'type':0,'header':struct.pack('<I',0),'payload':payload('Opaque original')+b'unknown tail'}}
        bank(target,'TEXT_MAIN',records);original=target.read_bytes();original_metadata=metadata(target)
        exported=case/'edited.tsv';checkpoint=case/'edited.png';refused=case/'stale-refused.png'
        lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 5','wait_texts','frames 3',
            'text_select GROUP','text_edit_refused','text_select NARRATORS','text_edit_refused',
            'text_select TXT_OPAQUE','text_edit','text_edit_content Attempted opaque edit',
            'text_save_refused','assert_log Text payload has unsupported bytes',
            'reveal btn_text_edit_cancel','frames 3','click btn_text_edit_cancel','frames 3','assert_text_edit -',
            'text_select TXT_HELLO','reveal btn_text_edit','frames 3','click btn_text_edit','frames 3',
            'assert_text_edit TXT_HELLO','assert_widget input_text_edit_content','text_edit_content First edit Ω 漢字',
            'reveal btn_text_save','frames 3','click btn_text_save','frames 3',
            'reveal btn_text_save_confirm','frames 3','dump_widget btn_text_save_confirm',
            f'screenshot {(case/"confirm.png").as_posix()}','frames 3',
            'reveal btn_text_save_confirm','frames 3','click btn_text_save_confirm','frames 3','wait_texts','frames 3',
            'assert_text_edit -','text_select TXT_HELLO','assert_text_content First edit Ω 漢字',
            'text_edit','text_edit_content Second edit Ω 漢字','text_save','wait_texts','frames 3',
            'text_select TXT_HELLO','assert_text_content Second edit Ω 漢字',
            'text_export '+exported.as_posix(),'wait_text_export','text_edit',
            'text_edit_content Attempted stale overwrite',f'screenshot {checkpoint.as_posix()}',
            'frames 300','text_save_refused','assert_log Text changed on disk',
            f'screenshot {refused.as_posix()}','frames 180',
            'reveal btn_text_edit_cancel','frames 3','click btn_text_edit_cancel','frames 3',
            'restore_all','wait_ready','assets_tab 5','wait_texts','frames 3',
            'text_select TXT_HELLO','assert_text_content Original Ω 漢字','quit']
        script=case/'edit.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
        output=case/'output.log'
        with output.open('w',encoding='utf-8') as log:
            process=subprocess.Popen([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
                cwd=a.cwd.resolve(),env=env,stdout=log,stderr=subprocess.STDOUT)
            try:
                import time
                deadline=time.monotonic()+90
                while not checkpoint.exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                assert checkpoint.exists(),output.read_text(encoding='utf-8')[-2500:]
                assert Path(str(target)+'.forge-orig').read_bytes()==original
                result=subprocess.run([str(a.tool.resolve()),'text','show',str(target),'TXT_HELLO','--json'],
                    cwd=a.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',timeout=30)
                value=json.loads(result.stdout);assert result.returncode==0
                assert value['content']=='Second edit Ω 漢字'
                assert value['speaker']=='HERO' and value['speech_bank']=='Speech.lug' and value['identifier']==''
                assert value['tags']==[{'position':2,'name':'ANIM:CHEER'}],value
                assert value['id']==10
                current=package_records(target,by_id=True)['TEXT_MAIN']
                for record in records.values():
                    entry=current[record['id']]
                    if record['id']!=10:assert entry[0]==record['payload']
                assert current[10][0]==payload('Second edit Ω 漢字')
                assert metadata(target)==original_metadata,'entry/header/archive metadata changed'
                # Change the bank after the browser starts the next edit.
                records['TXT_HELLO']['payload']=payload('External change')
                bank(target,'TEXT_MAIN',records)
                external=target.read_bytes()
                deadline=time.monotonic()+60
                while not refused.exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                assert refused.exists(),output.read_text(encoding='utf-8')[-2500:]
                assert target.read_bytes()==external and Path(str(target)+'.forge-orig').read_bytes()==original
                rc=process.wait(timeout=90)
            finally:
                if process.poll() is None:process.terminate();process.wait(timeout=10)
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc==0 and 'RESULT PASS' in trace,trace[-4500:]
        assert target.read_bytes()==original and Path(str(target)+'.forge-orig').read_bytes()==original
        import csv
        with exported.open(encoding='utf-8-sig',newline='') as stream:rows=list(csv.DictReader(stream,delimiter='\t'))
        other=next(row for row in rows if row['name']=='TXT_OTHER');assert other['content']=='Unrelated content'
        narrators=next(row for row in rows if row['name']=='NARRATORS');assert json.loads(narrators['narrators'])==['HERO']
        group=next(row for row in rows if row['name']=='GROUP');assert json.loads(group['members'])==[10]
        assert not list(target.parent.glob('.forge-text-write-*'))
        print(size,'content edits, unchanged metadata/unrelated entries, one-time originals and Restore: PASS',flush=True)

if __name__=='__main__':main()
