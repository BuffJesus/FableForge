#!/usr/bin/env python3
"""Fixed-width definition edits must reject opaque trailers and truncated values."""
import argparse, hashlib, json, struct, subprocess, tempfile
from pathlib import Path
from test_mod_field_details import write_bank, tag
from test_fmp_links import read_bank

def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build');args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='definition-layout-',dir=args.work_root.resolve()));checks=0
    print('Evidence retained at',work,flush=True)
    cases=[('int32',struct.pack('<i',4),'99',struct.pack('<i',99)),
        ('uint32',struct.pack('<I',4),'99',struct.pack('<I',99)),
        ('float',struct.pack('<f',4),'1.5',struct.pack('<f',1.5)),
        ('bool',b'\0','true',b'\1'),('CDefString',struct.pack('<I',4),'99',struct.pack('<I',99)),
        ('C2DVector',struct.pack('<2f',1,2),'3,4',struct.pack('<2f',3,4)),
        ('C3DVector',struct.pack('<3f',1,2,3),'4,5,6',struct.pack('<3f',4,5,6)),
        ('CRGBColour',bytes([1,2,3,4]),'5,6,7,8',bytes([7,6,5,8])),
        ('ColourRGBA',bytes([1,2,3,4]),'5,6,7,8',bytes([5,6,7,8]))]
    for kind,old,value,new in cases:
        for label,payload in [('canonical',old),('trailer',old+b'OPAQUE'),('truncated',old[:-1])]:
            case=work/(kind+'-'+label);root=case/'source'
            write_bank(root,[payload],definition='CScalarFixtureDef',record='FIXTURE',fields=['Value'])
            schema=case/'schema.json';schema.write_text(json.dumps({'CScalarFixtureDef':{'fields':[{'name':'Value','type':kind}]}}),encoding='utf-8')
            originals={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*.bin')}
            outroot=case/'output';out=outroot/'data/CompiledDefs/game.bin'
            r=subprocess.run([str(args.tool.resolve()),'defs','set-field',str(root/'data/CompiledDefs/game.bin'),
                'FIXTURE','Value',value,'--schema',str(schema),'--out',str(out),'--json'],capture_output=True,text=True,encoding='utf-8')
            (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8');checks+=1
            if label=='canonical':
                assert r.returncode==0,(kind,r.stdout,r.stderr)
                assert read_bank(outroot)[0]['data']==b'\x01\0\x01'+struct.pack('<I',tag('Value'))+new
            else:
                assert r.returncode!=0 and not outroot.exists(),(kind,label,r.stdout,r.stderr)
            assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in originals.items())
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks})+'\n',encoding='utf-8')
    print('PASS:',checks,'canonical edits and unsupported-width refusals; inputs unchanged')

if __name__=='__main__':main()
