#!/usr/bin/env python3
"""Definition integers reject overflow and retain signed reference bit patterns."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from test_mod_field_details import write_bank
from test_fmp_links import read_bank

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe');p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-integer-bounds-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True)
 cases=[('int-min','int32','-2147483648',0x80000000),('int-max','int32','2147483647',0x7fffffff),('int-overflow','int32','2147483648',None),('int-underflow','int32','-2147483649',None),('int-large','int32','4294967296',None),('int-trailing','int32','12junk',None),('uint-max','uint32','4294967295',0xffffffff),('uint-hex','uint32','0xffffffff',0xffffffff),('uint-overflow','uint32','4294967296',None),('uint-negative','uint32','-1',None),('uint-negative-zero','uint32','-0',None),('uint-spaced-negative','uint32',' -1',None),('uint-wrap','uint32','-4294967295',None),('ref-sentinel','CDefString','-1',0xffffffff),('ref-min','CDefString','-2147483648',0x80000000),('ref-unsigned','CDefString','4294967295',0xffffffff),('ref-hex','CDefString','0xffffffff',0xffffffff),('ref-positive','CDefString','4',4),('ref-underflow','CDefString','-2147483649',None),('ref-wrap','CDefString','-4294967295',None),('ref-overflow','CDefString','4294967296',None),('ref-trailing','CDefString','-1junk',None)]
 for label,kind,value,expected in cases:
  case=work/label;root=case/'source';old=struct.pack('<I',4);write_bank(root,[old,struct.pack('<i',42)],definition='CIntegerFixtureDef',record='INTEGER',fields=['Value','Guard']);schema=case/'schema.json';schema.write_text(json.dumps({'CIntegerFixtureDef':{'fields':[{'name':'Value','type':kind},{'name':'Guard','type':'int32'}]}}))
  bank=root/'data/CompiledDefs/game.bin';names=root/'data/CompiledDefs/names.bin';hashes={q:hashlib.sha256(q.read_bytes()).hexdigest() for q in (bank,names)};record=read_bank(root)[0];output=case/'output'
  r=subprocess.run([str(a.tool.resolve()),'defs','set-field',str(bank),'INTEGER','Value',value,'--schema',str(schema),'--out',str(output/'data/CompiledDefs/game.bin'),'--json'],capture_output=True,text=True,encoding='utf-8');(case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8')
  if expected is None:assert r.returncode!=0 and not output.exists(),(label,r.stdout,r.stderr)
  else:
   assert r.returncode==0,(label,r.stdout,r.stderr)
   assert read_bank(output)[0]==dict(record,data=record['data'][:7]+struct.pack('<I',expected)+record['data'][11:])
   assert (output/'data/CompiledDefs/names.bin').read_bytes()==names.read_bytes()
   if kind=='CDefString':assert json.loads(r.stdout)['new_value']==str(expected if expected<0x80000000 else expected-0x100000000)
  assert all(hashlib.sha256(q.read_bytes()).hexdigest()==h for q,h in hashes.items());print(label,'numeric bounds and exact field delta: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':len(cases)})+'\n')
if __name__=='__main__':main()
