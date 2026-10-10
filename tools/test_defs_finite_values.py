#!/usr/bin/env python3
"""Check typed definition float edits refuse non-finite values before writes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_mod_field_details import write_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=repo/'build/forge-tools.exe')
    args=parser.parse_args();exe=args.exe.resolve()
    work=Path(tempfile.mkdtemp(prefix='defs-finite-values-',dir=repo/'build'))
    print('Evidence retained at',work,flush=True)
    root=work/'install';definition='FINITE_WRITER_TEST';record='FINITE_RECORD'
    fields=[{'name':'Scalar','type':'float'},{'name':'Pair','type':'C2DVector'},
            {'name':'Triple','type':'C3DVector'},{'name':'Legacy','type':'float'}]
    values=[struct.pack('<f',1),struct.pack('<2f',2,3),struct.pack('<3f',4,5,6),struct.pack('<I',0x7fc00001)]
    write_bank(root,values,definition=definition,record=record,fields=[f['name'] for f in fields])
    schema=work/'schema.json';schema.write_text(json.dumps({definition:{'prefix_len':3,'fields':fields}}),encoding='utf-8')
    bank=root/'data/CompiledDefs/game.bin';names=bank.with_name('names.bin')
    before={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (bank,names,schema)}
    checks=0
    for bad in ('nan','NaN','nan(1)','inf','+inf','-inf','Infinity','-Infinity','1e-100','-1e-100'):
        for field,value in [('Scalar',bad),('Pair',bad+',2'),('Pair','2,'+bad),
                            ('Triple',bad+',2,3'),('Triple','1,'+bad+',3'),('Triple','1,2,'+bad)]:
            output=work/f'refused-{checks}.bin'
            result=subprocess.run([str(exe),'defs','set-field',str(bank),record,field,value,
                                   '--schema',str(schema),'--out',str(output)],capture_output=True,text=True,timeout=30)
            (work/f'refused-{checks}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
            assert result.returncode!=0 and field in result.stderr,(field,value,result.stdout,result.stderr)
            assert not output.exists(),(field,value)
            assert before=={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in before}
            checks+=1
    for field,value,wanted in [('Scalar','-0',struct.pack('<I',0x80000000)),
                               ('Scalar','+1.25e2',struct.pack('<f',125)),
                               ('Scalar','3.4028234663852886e38',struct.pack('<I',0x7f7fffff)),
                               ('Scalar','1.401298464324817e-45',struct.pack('<I',1)),
                               ('Scalar','-1.401298464324817e-45',struct.pack('<I',0x80000001)),
                               ('Scalar','0x1p-149',struct.pack('<I',1)),
                               ('Scalar',' 0x1p+1',struct.pack('<f',2)),
                               ('Pair','1.401298464324817e-45,0',struct.pack('<2I',1,0)),
                               ('Pair','-2.5, 0',struct.pack('<2f',-2.5,0)),
                               ('Triple','1, -2, 3.5',struct.pack('<3f',1,-2,3.5))]:
        output=work/f'accepted-{checks}.bin'
        result=subprocess.run([str(exe),'defs','set-field',str(bank),record,field,value,
                               '--schema',str(schema),'--out',str(output)],capture_output=True,text=True,timeout=30)
        assert result.returncode==0,(field,value,result.stdout,result.stderr)
        decoded=subprocess.run([str(exe),'defs','decode',str(root),str(schema),record,str(output),'--json'],
                               capture_output=True,text=True,check=True,timeout=30)
        report=json.loads(decoded.stdout)
        assert report['all_tags_ok'] and report['leftover']==0,report
        actual={f['name']:bytes.fromhex(f['raw_hex']) for f in report['fields']}
        assert actual=={f['name']:(wanted if f['name']==field else raw) for f,raw in zip(fields,values)}
        assert before=={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in before}
        checks+=1
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8')
    print(f'Definition finite writers: {checks} checks passed; source bytes unchanged.')


if __name__=='__main__':
    main()
