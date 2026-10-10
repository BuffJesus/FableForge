#!/usr/bin/env python3
"""FMP record identity failures refuse before writes; legacy replacements work."""
import argparse,hashlib,json,struct,subprocess,tempfile,zlib
from pathlib import Path
from test_mod_field_details import write_bank,read_values,RECORD,TYPE
from test_mod_bank_refusal import snapshot
from test_fmp_links import package_records,read_bank

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write_fmp(path,entries):
    raw=bytearray(b'B\0\0\0'+struct.pack('<III',0,0,459));locations=[]
    for name,definition,payload in entries:
        locations.append((name,definition,len(raw),len(payload)));raw+=payload
    start=len(raw);raw+=struct.pack('<I',0)
    for name,definition,offset,length in locations:
        name=name.encode();definition=definition.encode()+b'\0'
        raw+=struct.pack('<7I',0,0,0,length,offset,0,len(name))+name
        raw+=struct.pack('<III',0,0,len(definition))+definition
    length=len(raw)-start;directory=len(raw)
    raw+=struct.pack('<I',1)+b'Objects\0'+struct.pack('<5I',0,len(entries),start,length,1)
    struct.pack_into('<I',raw,8,directory);path.write_bytes(raw)
    assert len(package_records(path)['Objects'])==len(entries)

def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    args=parser.parse_args();tool=args.tool.resolve();cwd=args.cwd.resolve()
    work=Path(tempfile.mkdtemp(prefix='fmp-record-refusal-',dir=repo/'build'))
    print('Evidence retained at',work,flush=True)
    base,source,out=work/'base',work/'source',work/'output'
    write_bank(base,(1,2,3,4));write_bank(source,(10,2,3,4))
    payload=zlib.decompress((source/'data/CompiledDefs/game.bin').read_bytes()[49:])[2:]
    out.mkdir();(out/'prior.txt').write_bytes(b'prior output')
    checks=[]
    cases=[('new-untyped','NEW_RECORD','-', 'has no definition type'),
           ('new-empty-type','NEW_RECORD','', 'has no definition type'),
           ('unnamed','',TYPE,'has no matching blank-name baseline identity'),
           ('wrong-type',RECORD,'DifferentDefinition','different definition type'),
           ('reserved-anonymous-name','@unnamed:0',TYPE,'reserved anonymous identity namespace'),
           ('embedded-nul-name','BAD\0NAME',TYPE,'embedded NUL')]
    for label,name,definition,reason in cases:
        package=work/(label+'.fmp')
        # First contribution is valid: a later invalid one must abort the whole package.
        write_fmp(package,[('VALID_TYPED_ADDITION',TYPE,payload),(name,definition,payload)])
        for mode in ('standalone','composed'):
            command=['fmp','apply',base,package,out] if mode=='standalone' else ['mods','merge',base,out,'--with',package]
            before=snapshot(base);prior=snapshot(out);package_before=digest(package)
            result=subprocess.run([str(tool),*map(str,command)],cwd=cwd,capture_output=True,timeout=120)
            (work/(label+'-'+mode+'.log')).write_bytes(result.stdout+result.stderr)
            assert result.returncode!=0 and reason.encode() in result.stderr,(label,result.stderr)
            assert snapshot(base)==before and snapshot(out)==prior and digest(package)==package_before
            checks.append(label+'-'+mode)
    for definition in ('','-',TYPE):
        package=work/('valid-'+str(len(checks))+'.fmp')
        write_fmp(package,[(RECORD,definition,payload)])
        for mode in ('standalone','composed'):
            command=['fmp','apply',base,package,out] if mode=='standalone' else ['mods','merge',base,out,'--with',package]
            before=snapshot(base);package_before=digest(package)
            result=subprocess.run([str(tool),*map(str,command)],cwd=cwd,capture_output=True,timeout=120)
            (work/('valid-'+str(len(checks))+'.log')).write_bytes(result.stdout+result.stderr)
            assert result.returncode==0,result.stderr
            assert read_values(out)==(10,2,3,4)
            assert snapshot(base)==before and digest(package)==package_before
            checks.append('replacement-'+repr(definition)+'-'+mode)
    package=work/'typed-addition.fmp';write_fmp(package,[('NEW_TYPED_RECORD',TYPE,payload)])
    for mode in ('standalone','composed'):
        command=['fmp','apply',base,package,out] if mode=='standalone' else ['mods','merge',base,out,'--with',package]
        before=snapshot(base);package_before=digest(package)
        result=subprocess.run([str(tool),*map(str,command)],cwd=cwd,capture_output=True,timeout=120)
        (work/('typed-addition-'+mode+'.log')).write_bytes(result.stdout+result.stderr)
        assert result.returncode==0,result.stderr
        entries=read_bank(out)
        assert len(entries)==2 and entries[1]['name']=='NEW_TYPED_RECORD'
        assert entries[1]['definition']==TYPE and entries[1]['data']==payload
        assert snapshot(base)==before and digest(package)==package_before
        checks.append('typed-addition-'+mode)
    package=work/'oversized-record.fmp'
    write_fmp(package,[(RECORD,TYPE,payload + bytes(65535-len(payload)))])
    for mode in ('standalone','composed'):
        command=['fmp','apply',base,package,out] if mode=='standalone' else ['mods','merge',base,out,'--with',package]
        before=snapshot(base);prior=snapshot(out);package_before=digest(package)
        result=subprocess.run([str(tool),*map(str,command)],cwd=cwd,capture_output=True,timeout=120)
        (work/('oversized-'+mode+'.log')).write_bytes(result.stdout+result.stderr)
        assert result.returncode!=0 and b'chunk capacity' in result.stderr,result.stderr
        assert snapshot(base)==before and snapshot(out)==prior and digest(package)==package_before
        checks.append('oversized-record-'+mode)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks,'gameplay_tested':False},indent=2),encoding='utf-8')
    print('PASS:',len(checks),'record refusal and supported replacement cases',flush=True)

if __name__=='__main__':main()
