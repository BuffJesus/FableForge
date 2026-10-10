#!/usr/bin/env python3
"""Choose existing names-pool offsets and independently verify saved field bytes."""
import argparse,hashlib,json,os,struct,subprocess,tempfile,threading,time
from pathlib import Path
from test_mod_field_details import write_bank,tag
from test_texture_lookup import texture_bank
from test_fmp_links import read_bank
from test_definition_edit_ui import click

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe');p.add_argument('--cwd',type=Path,default=repo);p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-name-picker-ui-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True)
 fields=[f['name'] for f in json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))['UI']['fields']]
 modes=('choose','duplicate','none','invalid','interior','negative','noop','legacy-noop','search','large-search','restore','destination','stale-names')
 for size,scale in [('1440x900',1),('800x600',1.5)]:
  for mode in modes:
   case=work/f'{size}-{mode}';root=case/'install';values=[bytes(4)]*len(fields);write_bank(case/'seed',values,definition='UI',record='REFERENCES',fields=fields)
   pool=bytearray((case/'seed/data/CompiledDefs/names.bin').read_bytes());offsets=[]
   pool_names=['FONT_A','FONT_B','FONT_B']
   if mode=='large-search':pool_names += [f'UNUSED_{i:04}' for i in range(512)]+['FAR_FONT']
   for name in pool_names:
    offsets.append(len(pool)-20+4);pool+=struct.pack('<I',tag(name))+name.encode()+b'\0'
   struct.pack_into('<I',pool,8,2+len(pool_names));struct.pack_into('<I',pool,12,len(pool)-16)
   original_offset=0 if mode=='legacy-noop' else offsets[0];values[4]=struct.pack('<i',original_offset);write_bank(root,values,definition='UI',record='REFERENCES',fields=fields)
   folder=root/'data/CompiledDefs';names=folder/'names.bin';names.write_bytes(pool);target=folder/'game.bin';original=target.read_bytes();old_records=read_bank(root)
   levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'');texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
   hashes={q:hashlib.sha256(q.read_bytes()).hexdigest() for q in root.rglob('*') if q.is_file() and q!=target}
   lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 6','wait_definitions','definition_select REFERENCES','definition_edit 4','definition_bank_refused frontend.bin','definition_select_refused REFERENCES']
   chosen=offsets[-1] if mode=='large-search' else offsets[2] if mode=='duplicate' else offsets[1]
   if mode in ('choose','duplicate','none','search','large-search','restore'):
    chosen=-1 if mode=='none' else chosen
    lines+=click('combo_definition_edit_name')
    if mode in ('search','large-search'):lines+=click('input_definition_name_search')+['input_text '+('FAR_FONT' if mode=='large-search' else 'FONT_B'),'frames 3']
    if mode in ('choose','large-search'):lines += [f'screenshot {(case/"picker.png").as_posix()}']
    lines+=click('definition_name_option_none' if mode=='none' else f'definition_name_option_{chosen}')
    lines+=click('btn_definition_save')+click('btn_definition_save_confirm')+['wait_ready','wait_definitions','definition_select REFERENCES',f'assert_definition_field Font {chosen}']
   elif mode=='destination':lines += [f'definition_value {chosen}',f'set saveroot {(case/"output").as_posix()}','frames 3','wait_definitions','definition_save_refused','assert_state definition_editing 1','definition_cancel']
   elif mode=='stale-names':lines += [f'definition_value {chosen}',f'screenshot {(case/"draft.png").as_posix()}','frames 120','definition_save_refused','assert_state definition_editing 1','definition_cancel']
   elif mode in ('invalid','interior','negative'):
    chosen=999999 if mode=='invalid' else offsets[1]+1 if mode=='interior' else -2
    lines += [f'definition_value {chosen}','definition_save_refused','assert_state definition_editing 1','definition_cancel']
   else:lines += ['definition_save','wait_definitions','definition_select REFERENCES']
   if mode=='restore':lines+=['restore_all','wait_ready','wait_definitions','definition_select REFERENCES',f'assert_definition_field Font {original_offset}']
   lines+=['quit'];script=case/'picker.txt';script.write_text('\n'.join(lines)+'\n')
   changed=[]
   def mutate_names():
    deadline=time.monotonic()+30
    while not (case/'draft.png').exists() and time.monotonic()<deadline:time.sleep(.01)
    assert (case/'draft.png').exists()
    edited=bytearray(pool);struct.pack_into('<I',edited,20+offsets[0]-4,tag('FONT_C'));edited[20+offsets[0]:20+offsets[0]+6]=b'FONT_C';names.write_bytes(edited);changed.append(bytes(edited))
   worker=threading.Thread(target=mutate_names) if mode=='stale-names' else None
   if worker:worker.start()
   r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
   if worker:worker.join(timeout=31);assert len(changed)==1 and not worker.is_alive()
   (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8');trace=Path(str(script)+'.log').read_text();(case/'process.json').write_text(json.dumps({'returncode':r.returncode})+'\n');assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,(r.returncode,trace[-6000:])
   assert all(hashlib.sha256(q.read_bytes()).hexdigest()==h for q,h in hashes.items() if mode!='stale-names' or q!=names)
   if mode=='stale-names':assert names.read_bytes()==changed[0]
   if mode in ('choose','duplicate','none','search','large-search'):
    records=read_bank(root);assert len(records)==len(old_records)==1
    # Independently compare the complete original payload, changing only Font's four bytes.
    before=old_records[0]['data'];after=records[0]['data'];needle=struct.pack('<I',tag('Font'))+struct.pack('<i',original_offset);start=before.index(needle)+4
    assert after==before[:start]+struct.pack('<i',chosen)+before[start+4:]
    assert Path(str(target)+'.forge-orig').read_bytes()==original
   elif mode=='restore':assert target.read_bytes()==original and Path(str(target)+'.forge-orig').read_bytes()==original
   else:assert target.read_bytes()==original and not Path(str(target)+'.forge-orig').exists()
   print(size,mode,'name choice, draft protection and exact field delta: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':2*len(modes)})+'\n')
if __name__=='__main__':main()
