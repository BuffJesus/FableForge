#!/usr/bin/env python3
"""Small independent private Creature CLI/publisher delta and refusal gate."""
import argparse
import json
import shutil
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib
from test_fmp_links import read_bank, crc0
from test_definition_clone import deny_rename


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='creature-private-cli-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    folder=work/'source/data/CompiledDefs';folder.mkdir(parents=True)
    def field(name,value):return struct.pack('<II',crc0(name),value)
    def root():
        data=b'\x01\0\x01\x03\0'
        for kind,node in [('CCreatureDef',1),('CAppearanceDef',2),('CBrainDef',3)]:
            data+=struct.pack('<III',crc0(kind),node,0)
        # Opaque scalar must not accidentally equal the moved donor ID: this
        # fixture is for publication, not a claim about an unclassified field.
        return data+field('Components',99)+field('Self',0)
    entries=[('CREATURE','DONOR',root()),('CCreatureDef','BODY',b'\x01\0\x01'+field('Link',0)),
        ('CAppearanceDef','APPEARANCE',b'\x01\0\x01'+field('Link',0)),('CBrainDef','BRAIN',b'opaque')]
    refs={};pool=bytearray();table=bytearray();offsets=[];payload=bytearray()
    for kind,name,_ in entries:
        for text in (kind,name):
            if text not in refs:refs[text]=len(pool)+4;pool+=struct.pack('<I',crc0(text))+text.encode()+b'\0'
    for kind,name,data in entries:
        table+=struct.pack('<iii',refs[kind],refs[name],0)
        offsets.append(2*len(entries)+len(payload));payload+=data
    compressed=zlib.compress(struct.pack('<4H',*offsets)+payload)
    names=folder/'names.bin';bank=folder/'game.bin'
    names.write_bytes(struct.pack('<5I',0,0xa8e36c34,len(refs),len(pool)+4,0)+pool)
    bank.write_bytes(bytes(9)+struct.pack('<I',4)+table+struct.pack('<6I',2,0,0,4,len(compressed),len(compressed))+compressed)
    schema=work/'schema.json';schema.write_text(json.dumps({
        'CREATURE':{'fields':[{'name':'Components','type':'uint32'},{'name':'Self','type':'CDefIndex'}]},
        'CCreatureDef':{'fields':[{'name':'Link','type':'CDefIndex'}]},
        'CAppearanceDef':{'fields':[{'name':'Link','type':'CDefIndex'}]}}),encoding='utf-8')
    original=bank.read_bytes();old_pool=names.read_bytes();before=read_bank(work/'source');cases=0
    def run(root_name='NEW_ROOT',body_name='NEW_BODY',appearance_name='NEW_APPEARANCE',donor='DONOR',accepted=True,output_root=None,input_bank=None):
        nonlocal cases
        output=output_root or work/f'case-{cases}';target=output/'data/CompiledDefs/game.bin'
        result=subprocess.run([str(args.tool.resolve()),'defs','clone-creature-private',str(input_bank or bank),donor,
            root_name,body_name,appearance_name,'--schema',str(schema),'--out',str(target),'--json'],
            cwd=repo,capture_output=True,text=True,encoding='utf-8',timeout=60)
        (work/f'case-{cases}.log').write_text(result.stdout+result.stderr,encoding='utf-8');cases+=1
        assert (result.returncode==0)==accepted,result.stdout+result.stderr
        assert bank.read_bytes()==original and names.read_bytes()==old_pool
        assert not list(work.rglob('forge-def-clone-*'))
        if not accepted:
            if output_root is None:assert not output.exists()
            return
        report=json.loads(result.stdout)
        assert [report['entry_index'],report['body_index'],report['appearance_index']]==[4,5,6]
        assert report['visual_components_private'] and report['assets_shared']
        after=read_bank(output);assert len(after)==7 and after[:4]==before
        expected=bytearray(before[0]['data'])
        for offset,value in [(9,5),(13,4),(21,6),(25,4),(53,4)]:struct.pack_into('<I',expected,offset,value)
        assert after[4]['data']==bytes(expected)
        for node,slot in [(1,5),(2,6)]:
            expected=bytearray(before[node]['data']);struct.pack_into('<I',expected,7,4)
            assert after[slot]['data']==bytes(expected)
        assert target.read_bytes()[13:61]==original[13:61]
        assert (target.parent/'names.bin').read_bytes()[20:len(old_pool)]==old_pool[20:]
        if output_root is None:
            for path in (target,target.parent/'names.bin'):assert Path(str(path)+'.forge-created').is_file()
    run();run(donor='0')
    for donor in ('BODY','MISSING','0junk','-1','999'):run(donor=donor,accepted=False)
    for value in ('','DONOR','BAD\nNAME','X'*128,'BAD\u00e9'):
        for slot in range(3):
            values=['NEW_ROOT','NEW_BODY','NEW_APPEARANCE'];values[slot]=value
            run(*values,accepted=False)
    run('SAME','SAME','OTHER',accepted=False)
    run('plumless','buckeroo','OTHER',accepted=False)
    for mode in ('existing','in-place','rollback','pool-mismatch'):
        output=work/mode;shutil.copytree(work/'source',output)
        target=output/'data/CompiledDefs/game.bin';target_names=target.parent/'names.bin'
        if mode=='pool-mismatch':target_names.write_bytes(old_pool[:-1]+b'X')
        prior_bank=target.read_bytes();prior_names=target_names.read_bytes()
        if mode=='rollback':
            with deny_rename(target_names):run(output_root=output,accepted=False)
        elif mode=='pool-mismatch':run(output_root=output,accepted=False)
        else:
            run(output_root=output,input_bank=target if mode=='in-place' else None)
            assert Path(str(target)+'.forge-orig').read_bytes()==original
            assert Path(str(target_names)+'.forge-orig').read_bytes()==old_pool
            restored=subprocess.run([str(args.forge.resolve()),'restore','--install',str(output)],
                cwd=repo,capture_output=True,text=True,timeout=60)
            assert restored.returncode==0,restored.stdout+restored.stderr
        assert target.read_bytes()==prior_bank and target_names.read_bytes()==prior_names
    (work/'result.json').write_text(json.dumps({'passed':True,'cases':cases})+'\n',encoding='utf-8')
    print('Private CLI identities, selected ownership, prior bytes and refusal: PASS',cases,'cases',flush=True)


if __name__=='__main__':main()
