#!/usr/bin/env python3
"""Author text into a pack tree and compose it with unrelated edits safely."""
import json,os,struct,subprocess,tempfile
from pathlib import Path
import sys
sys.path.insert(0,str(Path('tools').resolve()))
from test_mod_field_details import write_bank
from test_mod_asset_choice_contributors import bank
from test_fmp_texture_layers import entries
from test_text_content_edit_ui import payload
def main():
    import argparse
    repo=Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    ap.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    ap.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    ap.add_argument('--cwd',type=Path,default=repo)
    ap.add_argument('--work-root',type=Path,default=repo/'build')
    a=ap.parse_args();workbase=Path(tempfile.mkdtemp(prefix='text-pack-destination-',dir=a.work_root.resolve()))
    print('Evidence retained at',workbase,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        work=workbase/size;work.mkdir()
        root=work/'install';write_bank(root,(1,2,3,4));levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        rel=Path('data/lang/English/text.big');bankname='TEXT_MAIN'
        records={'TXT_A':{'id':10,'type':0,'header':struct.pack('<I',0),'payload':payload('Original','TXT_A')},
         'TXT_OTHER':{'id':11,'type':0,'header':struct.pack('<I',0),'payload':payload('Other','TXT_OTHER')},
         '[NarratorList]':{'id':20,'type':2,'header':struct.pack('<I',2)+b'HERO\0FARMER\0','payload':b''}}
        bank(root/rel,bankname,records);original=(root/rel).read_bytes()
        pack=work/'Edited';pack.mkdir();(pack/'forge_pack.json').write_text(json.dumps({'version':1,'name':'Edited'}))
        script=work/'edit.txt';script.write_text('\n'.join(['wait_maps','wait_ready',f'set uiscale {scale}',f'pack_dest {pack.as_posix()}',
         'assets_tab 5','wait_texts','frames 3','text_select TXT_A','text_edit','text_edit_speaker FARMER',
         'text_edit_speechbank Edited.lug','text_edit_identifier AUDIO_KEY','text_tag_set 0 -7 ANIM:EDITED','text_save','wait_texts',
         'text_select TXT_A','text_add','text_add_name TXT_NEW','text_edit_content Added','text_save','wait_texts','quit'])+'\n')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(work/'profile'),LOCALAPPDATA=str(work/'local'))
        result=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=env,capture_output=True,timeout=90)
        assert result.returncode==0, result.stdout[-2500:]
        trace=Path(str(script)+'.log').read_text(encoding='utf-8');assert 'RESULT PASS' in trace,trace[-2500:]
        assert (root/rel).read_bytes()==original
        other=work/'Sibling';changed={k:dict(v) for k,v in records.items()};changed['TXT_OTHER']['payload']=payload('Sibling edit','TXT_OTHER');bank(other/rel,bankname,changed)
        def run(*args):
         r=subprocess.run([str(a.tool.resolve()),*map(str,args)],cwd=a.cwd.resolve(),capture_output=True,text=True,timeout=120)
         (work/('cli-'+str(len(list(work.glob('cli-*'))))+'.log')).write_text(r.stdout+'\n'+r.stderr,encoding='utf-8')
         assert r.returncode==0,(args,r.stdout[-3500:],r.stderr[-3500:]);return r
        for mod in (pack,other):run('mods','add',root,mod)
        output=work/'output';run('mods','build',root,output,'--json')
        actual=entries(output/rel,bankname,None,True);authored=entries(pack/rel,bankname,None,True)
        for name in ('TXT_A','TXT_NEW'):
         assert actual[name]['payload']==authored[name]['payload'] and actual[name]['header']==authored[name]['header'],name
        assert actual['TXT_OTHER']['payload']==changed['TXT_OTHER']['payload']
        assert actual['[NarratorList]']['header']==records['[NarratorList]']['header']
        assert (root/rel).read_bytes()==original
        print(work);print('GUI-authored content/references/speaker/tags/new-string composed with unrelated sibling edit: PASS')
        authored_bytes=(pack/rel).read_bytes();manifest=(pack/'forge_pack.json').read_bytes()
        alternate=work/'Alternate';alternate.mkdir();(alternate/'forge_pack.json').write_text('{"version":1,"name":"Alternate"}')
        invalid=work/'Invalid';invalid.mkdir();(invalid/'forge_pack.json').write_text('{broken')
        def auto(name,lines):
            path=work/(name+'.txt');path.write_text('\n'.join(['wait_maps','wait_ready',f'set uiscale {scale}']+lines+['quit'])+'\n',encoding='utf-8')
            r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(path)],cwd=a.cwd.resolve(),env=env,capture_output=True,timeout=90)
            trace=Path(str(path)+'.log').read_text(encoding='utf-8');assert r.returncode==0 and 'RESULT PASS' in trace,trace[-3500:]
        auto('guards',[f'pack_dest {pack.as_posix()}','assets_tab 5','wait_texts','frames 3','text_select TXT_A','text_edit',
            'text_edit_content Attempted',f'pack_dest {alternate.as_posix()}','wait_texts','frames 3','text_save_refused',
            'assert_log Text source or destination changed',f'pack_dest {invalid.as_posix()}','wait_texts','frames 3',
            'text_select TXT_A','text_edit_refused',f'pack_dest {(work/"Missing").as_posix()}','wait_texts','frames 3',
            'text_select TXT_A','text_edit_refused'])
        assert (pack/rel).read_bytes()==authored_bytes and (pack/'forge_pack.json').read_bytes()==manifest
        assert not (alternate/rel).exists() and not (invalid/rel).exists() and (root/rel).read_bytes()==original
        listed=subprocess.run([str(a.forge.resolve()),'backups','--install',str(pack)],cwd=a.cwd.resolve(),capture_output=True,text=True,timeout=30)
        assert listed.returncode==0 and 'backed-up file' in listed.stdout,listed.stdout+listed.stderr
        empty=subprocess.run([str(a.forge.resolve()),'restore','--install',str(alternate)],cwd=a.cwd.resolve(),capture_output=True,text=True,timeout=30)
        assert empty.returncode!=0 and 'tracked backup records' in empty.stderr,empty.stdout+empty.stderr
        restored=subprocess.run([str(a.forge.resolve()),'restore','--install',str(pack)],cwd=a.cwd.resolve(),capture_output=True,text=True,timeout=90)
        assert restored.returncode==0,restored.stdout+restored.stderr
        assert not (pack/rel).exists() and (pack/'forge_pack.json').read_bytes()==manifest and (root/rel).read_bytes()==original
        print(size,'pack destination, full metadata/donor composition, sibling merge, destination/manifest guards and folder Restore: PASS',flush=True)
if __name__=='__main__':main()
