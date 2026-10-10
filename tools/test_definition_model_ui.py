#!/usr/bin/env python3
"""Open confirmed object model IDs without name ambiguity or source writes."""
import argparse,hashlib,json,os,struct,subprocess,tempfile
from pathlib import Path
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_meshimport import write_obj
from test_fmp_links import package_records
from test_texture_lookup import texture_bank

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe');p.add_argument('--forge',type=Path,default=repo/'build/forge.exe');p.add_argument('--cwd',type=Path,default=repo);p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-model-ui-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True)
 fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))['OBJECT']['fields']
 def definitions(root,model,width=17,record="OBJ_MODEL"):
  values=[struct.pack('<III f B',5,model,0,1,0)[:width] if f['name']=='Graphic' else 0 for f in fields]
  write_bank(root,values,definition='OBJECT',record=record,fields=[f['name'] for f in fields])
  folder=root/'data/CompiledDefs';(folder/'frontend.bin').write_bytes((folder/'game.bin').read_bytes())
 seed=work/'seed';definitions(seed,1,record="OBJECT_BARREL_UNBREAKABLE");bank=seed/'data/graphics/graphics.big';archive(bank,b'BIGB',[('MBANK_ALLMESHES',205,[(1,1,'Donor',b'opaque')])]);texture_bank(seed/'data/graphics/pc/textures.big',[(1,'Texture',20)])
 obj=work/'cube.obj';write_obj(obj)
 r=subprocess.run([str(a.forge.resolve()),'mesh-import',str(obj),'FIXTURE','--no-collision','--texture-id','1','--install',str(seed)],capture_output=True,text=True,timeout=30);assert r.returncode==0,r.stdout+r.stderr
 payload=package_records(bank,by_id=True)['MBANK_ALLMESHES'][2][0]
 for size,scale in [('1440x900',1),('800x600',1.5)]:
  for mode,identity in [('exact',7),('missing',999),('ambiguous',10),('truncated',7)]:
   case=work/f'{size}-{mode}';root=case/'install';definitions(root,identity,16 if mode=='truncated' else 17)
   archive(root/'data/graphics/graphics.big',b'BIGB',[('MBANK_ALLMESHES',205,[(7,1,'Correct',payload),(8,1,'7',payload),(10,1,'DupA',payload),(10,1,'DupB',payload)])]);texture_bank(root/'data/graphics/pc/textures.big',[(1,'Texture',20)])
   levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
   hashes={q:hashlib.sha256(q.read_bytes()).hexdigest() for q in root.rglob('*') if q.is_file()}
   lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 1','model_select 7','frames 3','assert_state model_selected 7','assets_tab 6','wait_definitions','definition_select OBJ_MODEL','frames 3']
   if mode=='truncated':lines+=['assert_state definition_model -']
   else:
    lines+=[f'assert_state definition_model {identity}','reveal definition_model_12','frames 3','assert_widget definition_model_12','click definition_model_12','frames 3']
    if mode=='exact':
     lines+=['assert_state assets_tab 1','assert_state model_selected Correct','assert_state model_ready 1','assets_tab 6','frames 3','definition_edit 1','reveal definition_model_12','frames 3','click definition_model_12','frames 3','assert_state assets_tab 6','assert_state definition_editing 1','definition_cancel']
    else:lines+=['assert_state assets_tab 6','assert_state model_selected 7']
   lines+=['definition_bank frontend.bin','wait_definitions','definition_select OBJ_MODEL','frames 3','assert_state definition_model -','quit']
   script=case/'model.txt';script.write_text('\n'.join(lines)+'\n');r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
   (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8');trace=Path(str(script)+'.log').read_text();(case/'process.json').write_text(json.dumps({'returncode':r.returncode})+'\n');assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,(r.returncode,trace[-6500:])
   assert all(hashlib.sha256(q.read_bytes()).hexdigest()==h for q,h in hashes.items());print(size,mode,'model identity and protected source: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':8,'source_hashes_unchanged':True})+'\n')
if __name__=='__main__':main()
