#!/usr/bin/env python3
"""Inline string writes must not discard bytes after a terminator or infer a missing one."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from test_mod_field_details import write_bank
from test_fmp_links import read_bank

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe');p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-string-layout-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True)
 cases=[('canonical',b'OLD\0','EDIT',True),('empty',b'\0','EDIT',True),('clear',b'OLD\0','',True),('highbyte',b'OLD\xff\0','EDIT',True),('long',b'A'*256+b'\0','EDIT',True),('unterminated',b'OLD','EDIT',False),('embedded',b'A\0B\0','EDIT',False),('trailer',b'OLD\0OPAQUE','EDIT',False),('extra-nul',b'OLD\0\0','EDIT',False),('missing',b'','EDIT',False)]
 for label,old,value,valid in cases:
  case=work/label;root=case/'source';write_bank(root,[old,b'OTHER\0',struct.pack('<i',42)],definition='CHeroSoulsCreatureDef',record='STRINGS',fields=['CreatureType','Location','NumCreatures'])
  bank=root/'data/CompiledDefs/game.bin';names=root/'data/CompiledDefs/names.bin';hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (bank,names)};record=read_bank(root)[0];output=case/'output';out=output/'data/CompiledDefs/game.bin'
  r=subprocess.run([str(a.tool.resolve()),'defs','set-field',str(bank),'STRINGS','CreatureType',value,'--schema',str(repo/'docs/re_reference/def_schema.json'),'--out',str(out),'--json'],capture_output=True,text=True,encoding='utf-8');(case/'output.log').write_text(r.stdout+r.stderr)
  if valid:
   assert r.returncode==0,(label,r.stdout,r.stderr)
   assert read_bank(output)[0]==dict(record,data=record['data'][:7]+value.encode()+b'\0'+record['data'][7+len(old):])
   assert (output/'data/CompiledDefs/names.bin').read_bytes()==names.read_bytes()
  else:assert r.returncode!=0 and not output.exists(),(label,r.stdout,r.stderr)
  assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
  print(label,'string layout and exact unrelated bytes: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':len(cases)})+'\n')
if __name__=='__main__':main()
