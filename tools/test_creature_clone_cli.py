#!/usr/bin/env python3
"""Verify self-only Creature clones through paired publication and explicit Restore."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib
from test_fmp_links import read_bank, crc0
from test_definition_clone import deny_rename


def fixture(root):
    folder=root/'data/CompiledDefs';folder.mkdir(parents=True)
    def payload(owner,mode='normal'):
        data=bytearray(b'\x01\0\x01\x02\0')
        for value in (0x12345678,2,owner,0x87654321,owner,0):data+=struct.pack('<I',value)
        data+=struct.pack('<III',crc0('Components'),owner if mode=='unknown' else 0,0)
        data+=struct.pack('<II',crc0('GroupDef'),3)
        if mode=='bad-count':data[3]=3
        if mode=='empty':del data[5:29];data[3]=0
        return bytes(data)
    entries=[('OTHER','OTHER',b'opaque'),('CREATURE','DONOR',payload(1)),('CCreatureDef','SHARED',b'shared'),
        ('THING_GROUP','GROUP',b'\x01\0\x01\0\0'),('CREATURE','1',payload(4)),('CREATURE','DUP',payload(5)),
        ('CREATURE','DUP',payload(6)),('CREATURE','UNKNOWN',payload(7,'unknown')),
        ('CREATURE','BADCOUNT',payload(8,'bad-count')),('CREATURE','EMPTY',payload(9,'empty'))]
    pool=bytearray();refs={}
    for kind,name,_ in entries:
        for value in (kind,name):
            if value not in refs:refs[value]=len(pool)+4;pool+=struct.pack('<I',crc0(value))+value.encode()+b'\0'
    (folder/'names.bin').write_bytes(struct.pack('<5I',0,0xa8e36c34,len(refs),len(pool)+4,0)+pool)
    table=bytearray();counts={};offsets=[];data=bytearray()
    for kind,name,body in entries:
        table+=struct.pack('<iii',refs[kind],refs[name],counts.get(kind,0));counts[kind]=counts.get(kind,0)+1
        offsets.append(2*len(entries)+len(data));data+=body
    compressed=zlib.compress(struct.pack('<'+'H'*len(entries),*offsets)+data)
    (folder/'game.bin').write_bytes(bytes(9)+struct.pack('<I',len(entries))+table+
        struct.pack('<I5I',2,0,0,len(entries),len(compressed),len(compressed))+compressed)
    return folder


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args();work=Path(tempfile.mkdtemp(prefix='creature-clone-cli-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True);source=work/'source';folder=fixture(source)
    bank=folder/'game.bin';names=folder/'names.bin';original=bank.read_bytes();pool=names.read_bytes();records=read_bank(source)
    schema=work/'schema.json';schema.write_text(json.dumps({'CREATURE':{'fields':[{'name':'Components','type':'?'},{'name':'GroupDef','type':'int32'}]}}),encoding='utf-8')
    cases=0
    def run(output,donor='DONOR',accepted=True,extra=(),input_bank=bank,new='CREATURE_NEW'):
        nonlocal cases
        result=subprocess.run([str(args.tool.resolve()),'defs','clone-creature',str(input_bank),donor,new,
            '--schema',str(schema),'--out',str(output),'--json',*extra],cwd=args.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',timeout=60)
        (work/f'case-{cases}.log').write_text(result.stdout+result.stderr,encoding='utf-8');cases+=1
        assert (result.returncode==0)==accepted,result.stdout+result.stderr
        assert bank.read_bytes()==original and names.read_bytes()==pool,'source changed'
        assert not list(work.rglob('forge-def-clone-*')),'staging leaked'
        return result
    def verify(root,donor):
        after=read_bank(root);out=root/'data/CompiledDefs/game.bin';body=bytearray(records[donor]['data'])
        for offset in (13,21):struct.pack_into('<I',body,offset,len(records))
        assert after==records+[dict(records[donor],name='CREATURE_NEW',data=bytes(body))]
        assert out.read_bytes()[13:13+12*len(records)]==original[13:13+12*len(records)]
        assert (out.parent/'names.bin').read_bytes()[20:len(pool)]==pool[20:]
    def restore(root):
        result=subprocess.run([str(args.forge.resolve()),'restore','--install',str(root)],cwd=args.cwd.resolve(),capture_output=True,text=True,timeout=60)
        assert result.returncode==0,result.stdout+result.stderr
    for number,(donor,index) in enumerate((('DONOR',1),('1',4),('5',5))):
        root=work/f'fresh-{number}';out=root/'data/CompiledDefs/game.bin';report=json.loads(run(out,donor).stdout)
        assert report['donor_index']==index and report['entry_index']==len(records);verify(root,index)
        for path in (out,out.parent/'names.bin'):assert Path(str(path)+'.forge-created').is_file()
        restore(root);assert not out.exists() and not (out.parent/'names.bin').exists()
    for donor in ('OTHER','SHARED','DUP','MISSING','0junk','-1','999','UNKNOWN','BADCOUNT','EMPTY'):
        root=work/f'refusal-{cases}';run(root/'data/CompiledDefs/game.bin',donor,False);assert not root.exists()
    for new in ('','DONOR','X'*128,'BAD\nNAME','BAD\u00e9'):
        root=work/f'name-{cases}';run(root/'data/CompiledDefs/game.bin',accepted=False,new=new);assert not root.exists()
    for options in ([],['--schema'],['--schema',str(schema),'--bogus']):
        root=work/f'options-{cases}'
        result=subprocess.run([str(args.tool.resolve()),'defs','clone-creature',str(bank),'DONOR','NEW',
            '--out',str(root/'data/CompiledDefs/game.bin'),*options],cwd=args.cwd.resolve(),capture_output=True,text=True,timeout=60)
        (work/f'case-{cases}.log').write_text(result.stdout+result.stderr,encoding='utf-8');cases+=1
        assert result.returncode!=0 and not root.exists()
        assert bank.read_bytes()==original and names.read_bytes()==pool
    for mode in ('existing','rollback','pool-mismatch','orphan','bank-alias','names-alias','frontend','output-name','inplace'):
        root=work/mode;out=root/'data/CompiledDefs/game.bin';input_bank=bank
        if mode in ('existing','rollback','pool-mismatch','inplace'):shutil.copytree(source,root)
        else:out.parent.mkdir(parents=True)
        dest_names=out.parent/'names.bin'
        if mode=='pool-mismatch':dest_names.write_bytes(pool[:-1]+b'X')
        if mode=='orphan':Path(str(out)+'.forge-created').write_bytes(b'preserve')
        if mode=='bank-alias':os.link(bank,out);dest_names.write_bytes(pool)
        if mode=='names-alias':os.link(names,dest_names)
        if mode=='frontend':input_bank=folder/'frontend.bin';input_bank.write_bytes(original)
        if mode=='output-name':out=out.parent/'frontend.bin'
        if mode=='inplace':input_bank=out
        before={path:path.read_bytes() for path in root.rglob('*') if path.is_file()}
        if mode=='rollback':
            with deny_rename(dest_names):run(out,accepted=False)
        else:run(out,accepted=mode in ('existing','inplace'),input_bank=input_bank)
        if mode in ('existing','inplace'):
            verify(root,1)
            assert Path(str(out)+'.forge-orig').read_bytes()==original
            assert Path(str(dest_names)+'.forge-orig').read_bytes()==pool
            restore(root);assert out.read_bytes()==original and dest_names.read_bytes()==pool
        else:
            assert all(path.read_bytes()==value for path,value in before.items()),mode
    (work/'result.json').write_text(json.dumps({'passed':True,'cases':cases})+'\n',encoding='utf-8')
    print('Creature selector, self-only delta, refusal, paired rollback and Restore: PASS',cases,'cases',flush=True)


if __name__=='__main__':main()
