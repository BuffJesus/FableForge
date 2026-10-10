#!/usr/bin/env python3
"""Edit fixed-width vector drafts, preserve neighbors, and reject unsupported layouts."""
import argparse,hashlib,json,os,struct,subprocess,tempfile
from pathlib import Path
from test_mod_field_details import write_bank,tag
from test_texture_lookup import texture_bank
from test_fmp_links import read_bank
from test_definition_edit_ui import click

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe');p.add_argument('--cwd',type=Path,default=repo)
 p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-vector-ui-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True)
 cases=0
 for size,scale in [('1440x900',1),('800x600',1.5)]:
  for count in (2,3):
   for mode in ('save','precision','noop','nan-noop','subnormal-noop','invalid','trailer','truncated'):
    cases+=1;case=work/f'{size}-{count}-{mode}';root=case/'install';kind='CQuickAccessItemButtonGuiDef' if count==2 else 'CHeroPostcardGeneratorDef'
    field_index=1 if count==2 else 2
    old=struct.pack('<'+'f'*count,*range(1,count+1))
    if mode=='precision':old=struct.pack('<fI',1,0x3f800001)+(struct.pack('<f',-0.0) if count==3 else b'')
    if mode=='subnormal-noop':old=struct.pack('<I',1)+struct.pack('<f',-0.0)*(count-1)
    if mode=='nan-noop':old=struct.pack('<I',0x7fc01234)+struct.pack('<f',-0.0)*(count-1)
    if mode=='trailer':old+=b'OPAQUE'
    if mode=='truncated':old=old[:-1]
    if count==2:fields=['ButtonGraphic','Position','Offset'];values=[struct.pack('<i',42),old,struct.pack('<2f',5,6)]
    else:fields=['MeshAreaTLPos','MeshAreaBRPos','DollCentreOffset','DollBoundingSphereRadius'];values=[struct.pack('<2f',1,2),struct.pack('<2f',3,4),old,struct.pack('<f',5)]
    write_bank(root,values,definition=kind,record='VECTOR',fields=fields)
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    bank=root/'data/CompiledDefs/game.bin';names=root/'data/CompiledDefs/names.bin';before=bank.read_bytes();names_before=names.read_bytes();payload=read_bank(root)[0]['data']
    lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 6','wait_definitions','frames 3','definition_select VECTOR','frames 3']
    if mode in ('trailer','truncated'):lines+=[f'definition_edit_refused {field_index}']
    else:
     lines+=click(f'btn_definition_edit_{field_index}')+['assert_state definition_editing 1']
     if mode in ('save','precision'):
      lines+=click('input_definition_vector_0')+['key_down Ctrl','key_down A','key_up A','key_up Ctrl','input_text 9','frames 3']
      lines+=click('btn_definition_save')+click('btn_definition_save_confirm')+['wait_ready','wait_file_job','wait_definitions','frames 3','assert_state definition_editing 0']
     elif mode=='invalid':
      for value in ('1,2,3,4','1e99,'+','.join(['2']*(count-1)),'1junk,'+','.join(['2']*(count-1)),'nan,'+','.join(['2']*(count-1))):
       lines+=['definition_value '+value,'definition_save_refused','assert_state definition_editing 1']
      lines+=['definition_cancel']
     else:lines+=['definition_save','wait_ready','wait_file_job','wait_definitions','frames 3','assert_state definition_editing 0']
    lines+=['quit'];script=case/'vectors.txt';script.write_text('\n'.join(lines)+'\n')
    r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
    (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8');trace=Path(str(script)+'.log').read_text()
    assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-5000:]
    backup=Path(str(bank)+'.forge-orig')
    if mode in ('save','precision'):
     start=payload.index(struct.pack('<I',tag(fields[field_index])))+4
     expected=payload[:start]+struct.pack('<f',9)+payload[start+4:]
     assert read_bank(root)[0]['data']==expected and backup.read_bytes()==before
     assert 'click input_definition_vector_0' in trace and 'click btn_definition_save_confirm' in trace
    else:assert bank.read_bytes()==before and not backup.exists()
    assert names.read_bytes()==names_before
    print(size,count,mode,'vector controls and exact bytes: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':cases})+'\n')
if __name__=='__main__':main()
