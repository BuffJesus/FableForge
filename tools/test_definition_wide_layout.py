#!/usr/bin/env python3
"""Wide strings use native NUL-terminated UTF-16LE, without a character count."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from test_mod_field_details import write_bank
from test_fmp_links import read_bank

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe');p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-wide-layout-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True)
 wide=lambda x:x.encode('utf-16-le')+b'\0\0'
 cases=[('canonical',wide('OLD'),'EDIT',True),('empty',wide(''),'EDIT',True),('clear',wide('OLD'),'',True),('unicode-original',wide('\u03a9'),'EDIT',True),('ascii-boundary',wide('OLD'),'A'*127,True),('unicode-input',wide('OLD'),'\u6f22',True),('unterminated',wide('OLD')[:-2],'EDIT',False),('odd',wide('OLD')[:-1],'EDIT',False),('embedded',wide('A')+wide('B'),'EDIT',False),('trailer',wide('OLD')+b'XX','EDIT',False),('trailer-terminated',wide('OLD')+wide('TAIL'),'EDIT',False),('missing',b'','EDIT',False),('count-prefix',struct.pack('<H',3)+'OLD'.encode('utf-16-le'),'EDIT',False)]
 cases += [('accented',wide('OLD'),'caf\u00e9',True),('mixed',wide('OLD'),'A\u03a9\u6f22\U0001f642',True),('supplementary-original',wide('\U0001f642'),'\U0001f680',True),('upper-boundary',wide('OLD'),'\U0010ffff',True),('high-surrogate',b'\x00\xd8\0\0','EDIT',False),('low-surrogate',b'\x00\xdc\0\0','EDIT',False),('wrong-surrogate-pair',b'\x00\xd8A\0\0\0','EDIT',False)]
 for label,old,value,valid in cases:
  case=work/label;root=case/'source';write_bank(root,[old,struct.pack('<i',42)],definition='CWideFixtureDef',record='WIDE',fields=['Value','Guard']);schema=case/'schema.json';schema.write_text(json.dumps({'CWideFixtureDef':{'fields':[{'name':'Value','type':'CWideString'},{'name':'Guard','type':'int32'}]}}))
  bank=root/'data/CompiledDefs/game.bin';names=root/'data/CompiledDefs/names.bin';hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (bank,names)};record=read_bank(root)[0];output=case/'output';out=output/'data/CompiledDefs/game.bin'
  r=subprocess.run([str(a.tool.resolve()),'defs','set-field',str(bank),'WIDE','Value',value,'--schema',str(schema),'--out',str(out),'--json'],capture_output=True,text=True,encoding='utf-8');(case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8')
  if valid:
   assert r.returncode==0,(label,r.stdout,r.stderr)
   assert json.loads(json.loads(r.stdout)['new_value'])==value,(label,r.stdout)
   assert read_bank(output)[0]==dict(record,data=record['data'][:7]+wide(value)+record['data'][7+len(old):])
   assert (output/'data/CompiledDefs/names.bin').read_bytes()==names.read_bytes()
   decoded=subprocess.run([str(a.tool.resolve()),'defs','decode',str(output),str(schema),'WIDE','game.bin','--json'],capture_output=True,text=True,encoding='utf-8');assert decoded.returncode==0,(label,decoded.stdout,decoded.stderr)
   assert json.loads(json.loads(decoded.stdout)['fields'][0]['value'])==value

  else:assert r.returncode!=0 and not output.exists(),(label,r.stdout,r.stderr)
  assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
  print(label,'native UTF16 terminator, input/layout guards: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':len(cases)})+'\n')
if __name__=='__main__':main()
