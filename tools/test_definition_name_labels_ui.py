#!/usr/bin/env python3
"""Resolve exact owning names-table offsets without inferring record IDs."""
import argparse,hashlib,json,os,struct,subprocess,tempfile,threading,time
from pathlib import Path
from test_mod_field_details import write_bank,tag
from test_texture_lookup import texture_bank

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe');p.add_argument('--cwd',type=Path,default=repo);p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-name-labels-ui-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True)
 fields=[f['name'] for f in json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))['UI']['fields']]
 modes=('valid','duplicate','numeric','empty','spaces','inside','crc','absent','null','zero','negative','trailer','truncated','refresh')
 for size,scale in [('1440x900',1),('800x600',1.5)]:
  for mode in modes:
   case=work/f'{size}-{mode}';root=case/'install';values=[bytes(4)]*len(fields);write_bank(case/'seed',values,definition='UI',record='REFERENCES',fields=fields)
   folder=root/'data/CompiledDefs';names=folder/'names.bin';pool=bytearray((case/'seed/data/CompiledDefs/names.bin').read_bytes());label='224' if mode=='numeric' else '' if mode=='empty' else 'FONT LABEL' if mode=='spaces' else 'FONT_LABEL'
   offset=len(pool)-20+4;pool+=struct.pack('<I',tag(label))+label.encode()+b'\0';added=1
   if mode=='duplicate':offset=len(pool)-20+4;pool+=struct.pack('<I',tag(label))+label.encode()+b'\0';added+=1
   struct.pack_into('<I',pool,8,2+added);struct.pack_into('<I',pool,12,len(pool)-16)
   stored=offset+1 if mode=='inside' else offset-4 if mode=='crc' else len(pool)+100 if mode=='absent' else -1 if mode=='null' else 0 if mode=='zero' else -2 if mode=='negative' else offset
   raw=struct.pack('<i',stored)
   if mode=='trailer':raw+=b'OPAQUE'
   if mode=='truncated':raw=raw[:-1]
   values[4]=raw;write_bank(root,values,definition='UI',record='REFERENCES',fields=fields);names.write_bytes(pool);bank=folder/'game.bin';(folder/'frontend.bin').write_bytes(bank.read_bytes())
   levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'');texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
   hashes={q:hashlib.sha256(q.read_bytes()).hexdigest() for q in root.rglob('*') if q.is_file()};resolved=mode in ('valid','duplicate','numeric','empty','spaces','refresh');expected=label or '(empty name)' if resolved else '-'
   lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 6','wait_definitions','definition_select REFERENCES','frames 3',f'assert_state definition_name_refs {int(resolved)}',f'assert_state definition_name_ref_4 {expected}','definition_edit_refused 4']
   if mode not in ('trailer','truncated'):lines[-1:]=['definition_edit 4','definition_cancel']
   if mode in ('empty','spaces'):lines=[line for line in lines if not line.startswith('assert_state definition_name_ref_4 ')]+['dump_state']
   if resolved:lines+=['reveal definition_name_reference_4','frames 3','assert_widget definition_name_reference_4']
   if mode=='refresh':lines+=['frames 120','reveal btn_definition_refresh','frames 3','click btn_definition_refresh','wait_definitions','definition_select REFERENCES','frames 3','assert_state definition_name_ref_4 FONT_OTHER']
   final='FONT_OTHER' if mode=='refresh' else expected
   lines+=['definition_bank frontend.bin','wait_definitions','definition_select REFERENCES','frames 3',f'assert_state definition_name_refs {int(resolved)}',f'assert_state definition_name_ref_4 {final}','quit'];script=case/'names.txt';script.write_text('\n'.join(lines)+'\n');tracepath=Path(str(script)+'.log');updates=[]
   if mode in ('empty','spaces'):
    lines=[line for line in lines if not line.startswith('assert_state definition_name_ref_4 ')];lines.insert(-1,'dump_state');script.write_text('\n'.join(lines)+'\n')
   def update():
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
     if tracepath.exists() and 'ok   assert_state definition_name_ref_4 FONT_LABEL' in tracepath.read_text():
      changed=bytearray(pool);struct.pack_into('<I',changed,20+offset-4,tag('FONT_OTHER'));changed[20+offset:20+offset+10]=b'FONT_OTHER';names.write_bytes(changed);updates.append(bytes(changed));return
     time.sleep(.01)
   worker=threading.Thread(target=update) if mode=='refresh' else None
   if worker:worker.start()
   r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
   if worker:worker.join(timeout=16);assert not worker.is_alive() and len(updates)==1,'fixture refresh did not happen'
   (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8');trace=tracepath.read_text();(case/'process.json').write_text(json.dumps({'returncode':r.returncode})+'\n');assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,(r.returncode,trace[-6500:])
   if mode in ('empty','spaces'):assert trace.count('definition_name_ref_4='+expected)>=2,trace
   assert all(hashlib.sha256(q.read_bytes()).hexdigest()==h for q,h in hashes.items() if q!=names or mode!='refresh')
   if mode=='refresh':assert names.read_bytes()==updates[0]
   assert not list(root.rglob('*.forge-orig'));print(size,mode,'owning names offsets, raw fallback and protected source: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':2*len(modes)})+'\n')
if __name__=='__main__':main()
