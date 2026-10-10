#!/usr/bin/env python3
"""Create a text-bank edit in a separate folder and recover the created file."""
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
    a=ap.parse_args();work=Path(tempfile.mkdtemp(prefix='text-folder-destination-',dir=a.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir();root=case/'base';write_bank(root,(1,2,3,4))
        levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        source=root/'data/lang/English/text.big'
        records={'TXT_DONOR':{'id':10,'type':0,'header':struct.pack('<I',0),'payload':payload('Original')},
                 'TXT_OTHER':{'id':11,'type':0,'header':struct.pack('<I',0),'payload':payload('Other')}}
        bank(source,'TEXT_MAIN',records);original=source.read_bytes();headers=metadata(source)
        destination=case/'mod folder';defs=destination/'data/CompiledDefs';defs.mkdir(parents=True)
        (defs/'game.bin').write_bytes((root/'data/CompiledDefs/game.bin').read_bytes())
        target=destination/'data/lang/English/text.big'
        lines=['wait_maps','wait_ready',f'set uiscale {scale}',f'set saveroot {destination.as_posix()}',
            'assets_tab 5','wait_texts','frames 3','text_select TXT_DONOR','text_edit',
            'text_edit_content Folder edit','text_save','wait_texts','frames 3',
            'text_select TXT_DONOR','assert_text_content Folder edit','text_add',
            'text_add_name TXT_NEW','text_edit_content New folder string','text_save',
            'wait_texts','frames 3','text_select TXT_NEW','assert_text_content New folder string','quit']
        script=case/'edit.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
        result=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
            cwd=a.cwd.resolve(),env=env,capture_output=True,text=True,encoding='utf-8',timeout=120)
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace,trace[-4000:]
        assert source.read_bytes()==original and not Path(str(source)+'.forge-orig').exists()
        assert Path(str(target)+'.forge-created').is_file() and not Path(str(target)+'.forge-orig').exists()
        current=package_records(target,by_id=True)['TEXT_MAIN']
        assert current[10][0]==payload('Folder edit') and current[11][0]==records['TXT_OTHER']['payload']
        assert current[12][0]==payload('New folder string','TXT_NEW')
        target_headers=metadata(target)
        for key,value in headers.items():assert target_headers[key]==value
        assert not list(target.parent.glob('.forge-text-write-*'))
        restored=subprocess.run([str(a.forge.resolve()),'restore','--install',str(destination)],
            cwd=a.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',timeout=30)
        assert restored.returncode==0 and not target.exists() and not Path(str(target)+'.forge-created').exists(),restored.stdout+restored.stderr
        assert source.read_bytes()==original
        # Orphaned recovery records must not be overwritten by bank creation.
        for suffix in ('.forge-orig','.atlas-orig','.forge-created','.atlas-created','.forgebak','.ovrbak'):
            recovery=Path(str(target)+suffix);recovery.write_bytes(b'preserve recovery evidence')
            guard=case/('guard-'+suffix[1:]+'.txt')
            guard.write_text('\n'.join(['wait_maps','wait_ready',f'set saveroot {destination.as_posix()}',
                'assets_tab 5','wait_texts','frames 3','text_select TXT_DONOR','text_edit',
                'text_edit_content Refused creation','text_save_refused',
                'assert_log Text destination has recovery records','quit'])+'\n',encoding='utf-8')
            result=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(guard)],
                cwd=a.cwd.resolve(),env=env,capture_output=True,text=True,encoding='utf-8',timeout=90)
            trace=Path(str(guard)+'.log').read_text(encoding='utf-8')
            assert result.returncode==0 and 'RESULT PASS' in trace,trace[-3000:]
            assert not target.exists() and recovery.read_bytes()==b'preserve recovery evidence'
            assert source.read_bytes()==original and not list(target.parent.glob('.forge-text-write-*'))
            recovery.unlink() # This test owns exactly the record it just created.
        # A destination appearing after a draft starts must survive save refusal.
        checkpoint=case/'draft-ready.png';race=case/'destination-appeared.txt'
        race.write_text('\n'.join(['wait_maps','wait_ready',f'set saveroot {destination.as_posix()}',
            'assets_tab 5','wait_texts','frames 3','text_select TXT_DONOR','text_edit',
            'text_edit_content Refused overwrite',f'screenshot {checkpoint.as_posix()}',
            'frames 300','text_save_refused','assert_log Text source or destination changed','quit'])+'\n',encoding='utf-8')
        process=subprocess.Popen([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(race)],
            cwd=a.cwd.resolve(),env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            deadline=time.monotonic()+60
            while not checkpoint.exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
            assert checkpoint.exists(),'draft checkpoint not reached'
            target.write_bytes(b'external destination must survive')
            rc=process.wait(timeout=90)
        finally:
            if process.poll() is None:process.terminate();process.wait(timeout=10)
        trace=Path(str(race)+'.log').read_text(encoding='utf-8')
        assert rc==0 and 'RESULT PASS' in trace,trace[-3000:]
        assert target.read_bytes()==b'external destination must survive' and source.read_bytes()==original
        assert not Path(str(target)+'.forge-created').exists() and not Path(str(target)+'.forge-orig').exists()
        assert not list(target.parent.glob('.forge-text-write-*'))
        print(size,'new folder bank, edit/add preservation, creation tracking and Restore: PASS',flush=True)

if __name__=='__main__':main()
