#!/usr/bin/env python3
"""Resolve terrain-theme texture IDs in the main bank and protect drafts."""
import argparse,hashlib,json,os,struct,subprocess,tempfile
from pathlib import Path
from test_mod_field_details import write_bank
from test_texture_lookup import texture_bank

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe');p.add_argument('--cwd',type=Path,default=repo);p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-texture-ui-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True)
 fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))['CEngineThemeDef']['fields'];linked={'BaseTexture','BackgroundTexture','BaseBumpMap','CliffBaseTexture','CliffBackgroundTexture','CliffBumpMap'}
 for size,scale in [('1440x900',1),('800x600',1.5)]:
  for mode in ('exact','missing','ambiguous','duplicate-name','other-bank','truncated','negative','zero'):
   case=work/f'{size}-{mode}';root=case/'install';identity=999 if mode=='missing' else -1 if mode=='negative' else 0 if mode=='zero' else 7
   values=[identity if f['name'] in linked else 0 for f in fields]
   if mode=='truncated':values[0]=struct.pack('<i',identity)[:-1]
   write_bank(root,values,definition='ENGINE_THEME',record='THEME',fields=[f['name'] for f in fields]);folder=root/'data/CompiledDefs';(folder/'frontend.bin').write_bytes((folder/'game.bin').read_bytes())
   rows=[(7,'Correct',20),(8,'7',40),(9,'Stable',60)]
   if mode=='ambiguous':rows.append((7,'DuplicateID',80))
   if mode=='duplicate-name':rows.append((10,'Correct',80))
   bank=root/'data/graphics/pc/textures.big';texture_bank(bank,rows)
   if mode=='other-bank':bank.write_bytes(bank.read_bytes().replace(b'GBANK_MAIN_PC',b'GBANK_SIDE_PC'))
   levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
   hashes={q:hashlib.sha256(q.read_bytes()).hexdigest() for q in root.rglob('*') if q.is_file()}
   count=0 if mode in ('negative','zero') else 5 if mode=='truncated' else 6
   lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 0','texture_select 7','frames 3','assert_state texture_selected 7','assets_tab 6','wait_definitions','definition_select THEME','frames 3',f'assert_state definition_texture_links {count}']
   if mode not in ('truncated','negative','zero'):
    lines+=['reveal definition_texture_0','frames 3','click definition_texture_0','frames 3']
    if mode=='exact':
     lines+=['assert_state assets_tab 0','assert_state texture_selected Correct','assets_tab 6','frames 3','definition_edit 1','reveal definition_texture_0','frames 3','click definition_texture_0','frames 3','assert_state assets_tab 6','assert_state definition_editing 1','definition_cancel']
    else:lines+=['assert_state assets_tab 6','assert_state texture_selected 7']
   lines+=['definition_bank frontend.bin','wait_definitions','definition_select THEME','frames 3','assert_state definition_texture_links 0','quit'];script=case/'texture.txt';script.write_text('\n'.join(lines)+'\n')
   r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
   (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8');trace=Path(str(script)+'.log').read_text();(case/'process.json').write_text(json.dumps({'returncode':r.returncode})+'\n');assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,(r.returncode,trace[-6500:])
   assert all(hashlib.sha256(q.read_bytes()).hexdigest()==h for q,h in hashes.items());print(size,mode,'texture identity and protected source: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':16,'source_hashes_unchanged':True})+'\n')
if __name__=='__main__':main()
