#!/usr/bin/env python3
"""Definition field writes require exact unique names or complete numeric indices."""
import argparse, hashlib, json, struct, subprocess, tempfile, zlib
from pathlib import Path
from test_mod_field_details import FIELDS, tag
from test_fmp_links import read_bank

def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build');args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='definition-selector-',dir=args.work_root.resolve()));root=work/'source'
    folder=root/'data/CompiledDefs';folder.mkdir(parents=True)
    names=bytearray();refs={}
    for name in ('CRegionScriptDef','123','DUP','NORMAL'):
        refs[name]=len(names)+4;names+=struct.pack('<I',tag(name))+name.encode()+b'\0'
    (folder/'names.bin').write_bytes(struct.pack('<5I',0,0xA8E36C34,len(refs),len(names)+4,0)+names)
    labels=['123','DUP','DUP',None,'NORMAL']
    payloads=[b'\x01\0\x01'+b''.join(struct.pack('<Ii',tag(f),10*i+j) for j,f in enumerate(FIELDS)) for i in range(len(labels))]
    offsets=[];body=bytearray()
    for p in payloads:offsets.append(2*len(labels)+len(body));body+=p
    chunk=zlib.compress(struct.pack('<'+'H'*len(labels),*offsets)+body)
    data=bytes(9)+struct.pack('<I',len(labels))
    data+=b''.join(struct.pack('<iii',refs['CRegionScriptDef'],-1 if n is None else refs[n],i) for i,n in enumerate(labels))
    data+=struct.pack('<I5I',2,0,0,len(labels),len(chunk),len(chunk))+chunk
    (folder/'game.bin').write_bytes(data)
    before={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.iterdir()}
    print('Evidence retained at',work,flush=True)
    rejected=['0junk',' 0','+0','-1','0.5','184467440737095516160','DUP','MISSING','999','']
    accepted=[('123',0),('3',3),('NORMAL',4),('2',2)]
    for number,(selector,index) in enumerate([(s,None) for s in rejected]+accepted):
        outroot=work/f'output-{number}';out=outroot/'data/CompiledDefs/game.bin'
        r=subprocess.run([str(args.tool.resolve()),'defs','set-field',str(folder/'game.bin'),selector,FIELDS[0],'99',
            '--schema',str(repo/'docs/re_reference/def_schema.json'),'--out',str(out),'--json'],capture_output=True,text=True,encoding='utf-8')
        (work/f'case-{number}.log').write_text(r.stdout+r.stderr,encoding='utf-8')
        if index is None:
            assert r.returncode!=0 and not outroot.exists(),(selector,r.stdout,r.stderr)
        else:
            assert r.returncode==0,(selector,r.stdout,r.stderr)
            assert json.loads(r.stdout)['entry_index']==index
            rows=read_bank(outroot);assert len(rows)==len(labels)
            for i,row in enumerate(rows):
                expected=payloads[i] if i!=index else payloads[i][:7]+struct.pack('<i',99)+payloads[i][11:]
                assert row['data']==expected,(selector,i)
        assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in before.items())
    (work/'result.json').write_text(json.dumps({'passed':True,'refusals':len(rejected),'accepted':len(accepted)})+'\n',encoding='utf-8')
    print('PASS: malformed/ambiguous selectors write nothing; accepted edits change only the selected field')

if __name__=='__main__':main()
