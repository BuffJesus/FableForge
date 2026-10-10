#!/usr/bin/env python3
"""RGB input must follow native CRGBColour BGRA storage without changing neighbors."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from test_mod_field_details import write_bank,tag
from test_fmp_links import read_bank

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe');p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-colour-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True)
 cases=[('red','255,0,0',bytes([0,0,255,255])),('blue','0,0,255',bytes([255,0,0,255])),('green','0,255,0',bytes([0,255,0,255])),('alpha','11,22,33,44',bytes([33,22,11,44])),('transparent','255,128,64,0',bytes([64,128,255,0])),('negative','-1,2,3',None),('overflow','256,2,3',None),('large','4294967296,2,3',None),('arity','1,2',None)]
 for label,value,expected in cases:
  case=work/label;root=case/'source';write_bank(root,[struct.pack('<i',42),bytes([1,2,3,4])],definition='CPlayerDef',record='PLAYER',fields=['CharacterDef','Colour'])
  names=root/'data/CompiledDefs/names.bin';bank=root/'data/CompiledDefs/game.bin';before={f:hashlib.sha256(f.read_bytes()).hexdigest() for f in (names,bank)}
  output=case/'output';out=output/'data/CompiledDefs/game.bin'
  r=subprocess.run([str(a.tool.resolve()),'defs','set-field',str(bank),'PLAYER','Colour',value,'--schema',str(repo/'docs/re_reference/def_schema.json'),'--out',str(out),'--json'],capture_output=True,text=True,encoding='utf-8')
  (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8')
  if expected is None:assert r.returncode!=0 and not output.exists(),(label,r.stdout,r.stderr)
  else:
   assert r.returncode==0,(label,r.stdout,r.stderr)
   old=read_bank(root)[0];actual=read_bank(output)[0]
   assert actual==dict(old,data=old['data'][:-4]+expected)
   assert (output/'data/CompiledDefs/names.bin').read_bytes()==names.read_bytes()
  assert all(hashlib.sha256(f.read_bytes()).hexdigest()==h for f,h in before.items())
  print(label,'native BGRA encoding/refusal: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':len(cases),'native_storage':'BGRA','input_order':'RGBA'})+'\n')
if __name__=='__main__':main()
