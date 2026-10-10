#!/usr/bin/env python3
"""Read-only text lookup must be strict, unambiguous and header-aware."""
import argparse,hashlib,json
from pathlib import Path
import struct,subprocess,tempfile
from test_big_entry_markers import archive
from test_mod_asset_choice_contributors import bank

def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    args=parser.parse_args();work=Path(tempfile.mkdtemp(prefix='text-show-identity-',dir=repo/'build'))
    print('Evidence retained at',work,flush=True)
    payload=lambda value:value.encode('utf-16-le')+bytes(18)
    source=work/'identity.big'
    archive(source,b'BIGB',[('TEXT_A',106,[(7,0,'123',payload('Numeric symbol Ω')),
        (123,0,'TEXT_TARGET',payload('Numeric ID')),(80,0,'TEXT_DUP',payload('First')),
        (90,0,'TEXT_ID_A',payload('First ID')),(0xffffffff,0,'TEXT_MAX',payload('Maximum ID'))]),
        ('TEXT_B',106,[(123,0,'TEXT_OTHER',payload('Other bank')),
        (81,0,'TEXT_DUP',payload('Second')),(90,0,'TEXT_ID_B',payload('Second ID'))])])
    narration=work/'narrators.big'
    bank(narration,'TEXT_A',{'NARRATORS':{'id':60,'type':2,'header':struct.pack('<I',2)+b'HERO\0GUILD\0','payload':b''}})
    malformed=work/'malformed.big'
    bank(malformed,'TEXT_A',{'NARRATORS':{'id':60,'type':2,'header':struct.pack('<I',1)+b'BROKEN','payload':b''}})
    nonempty=work/'narrator-payload.big'
    bank(nonempty,'TEXT_A',{'NARRATORS':{'id':60,'type':2,'header':struct.pack('<I',1)+b'HERO\0','payload':b'junk'}})
    originals={path:hashlib.sha256(path.read_bytes()).hexdigest() for path in (source,narration,malformed,nonempty)}
    checks=0
    def run(path,query,accepted):
        nonlocal checks
        result=subprocess.run([str(args.tool.resolve()),'text','show',str(path),query,'--json'],
            cwd=args.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)
        (work/f'lookup-{checks}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        assert all(hashlib.sha256(p.read_bytes()).hexdigest()==value for p,value in originals.items())
        assert (result.returncode==0)==accepted,(query,result.stdout,result.stderr)
        checks+=1
        return json.loads(result.stdout) if accepted else result
    for query in ('123','7','0007'):
        row=run(source,query,True);assert row['name']=='123' and row['id']==7 and row['content']=='Numeric symbol Ω'
    assert run(source,'TEXT_TARGET',True)['content']=='Numeric ID'
    assert run(source,'4294967295',True)['content']=='Maximum ID'
    for query in ('123junk','123.0','+123',' 123','123 ','-1','4294967296','18446744073709551616',
                  'TEXT_DUP','90','MISSING',''):
        run(source,query,False)
    assert run(narration,'NARRATORS',True)['narrators']==['HERO','GUILD']
    assert run(narration,'60',True)['narrators']==['HERO','GUILD']
    run(malformed,'NARRATORS',False)
    run(nonempty,'NARRATORS',False)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks,'source_hashes_unchanged':True},indent=2)+'\n',encoding='utf-8')
    print('Text show:',checks,'strict lookup/header checks passed; all banks unchanged.')

if __name__=='__main__':main()
