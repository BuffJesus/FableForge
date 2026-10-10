#!/usr/bin/env python3
"""Paired Creature clone/Restore on read-only retail input and owned outputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from test_fmp_links import read_bank, crc0


def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--schema',type=Path,default=repo/'docs/re_reference/def_schema.json')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args();native=args.install.resolve();folder=native/'data/CompiledDefs'
    inputs=sorted(folder.glob('*.bin'));hashes={str(path):digest(path) for path in inputs}
    records=read_bank(native);original=(folder/'game.bin').read_bytes();pool=(folder/'names.bin').read_bytes()
    work=Path(tempfile.mkdtemp(prefix='creature-clone-retail-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True);cases=0
    try:
        for donor_name in ('CREATURE_TRADER_01','CREATURE_VILLAGER_BASE','CREATURE_BOSS_BANDIT_KING','NULLDEF_CREATURE'):
            owner=next(i for i,record in enumerate(records) if record['name']==donor_name)
            accepted=donor_name not in ('CREATURE_BOSS_BANDIT_KING','NULLDEF_CREATURE')
            for mode in ('fresh','existing') if accepted else ('refused',):
                case=work/(donor_name+'-'+mode);case.mkdir();output=case/'output';dest=output/'data/CompiledDefs/game.bin'
                if mode=='existing':
                    dest.parent.mkdir(parents=True);shutil.copyfile(folder/'game.bin',dest);shutil.copyfile(folder/'names.bin',dest.parent/'names.bin')
                new='CREATURE_FORGE_CLI_'+str(owner)
                result=subprocess.run([str(args.tool.resolve()),'defs','clone-creature',str(folder/'game.bin'),donor_name,new,
                    '--schema',str(args.schema.resolve()),'--out',str(dest),'--json'],cwd=args.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',timeout=120)
                (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
                assert (result.returncode==0)==accepted,result.stdout+result.stderr
                if not accepted:
                    assert not output.exists();cases+=1;continue
                report=json.loads(result.stdout);assert report['donor_index']==owner and report['entry_index']==len(records)
                expected=bytearray(records[owner]['data']);count=struct.unpack_from('<H',expected,3)[0];offsets=[]
                for index in range(count):
                    for member in (4,8):
                        at=5+12*index+member
                        if struct.unpack_from('<I',expected,at)[0]==owner:
                            offsets.append(at);struct.pack_into('<I',expected,at,len(records))
                after=read_bank(output);assert after==records+[dict(records[owner],name=new,data=bytes(expected))]
                bank=dest.read_bytes();names=(dest.parent/'names.bin').read_bytes()
                assert bank[:9]==original[:9] and bank[13:13+12*len(records)]==original[13:13+12*len(records)]
                assert names[20:len(pool)]==pool[20:]
                row=13+12*len(records);assert struct.unpack_from('<I',bank,row+8)[0]==sum(r['definition']=='CREATURE' for r in records)
                name_at=20+struct.unpack_from('<I',bank,row+4)[0]-4;assert struct.unpack_from('<I',names,name_at)[0]==crc0(new)
                for path,old in ((dest,original),(dest.parent/'names.bin',pool)):
                    if mode=='existing':assert Path(str(path)+'.forge-orig').read_bytes()==old
                    else:assert Path(str(path)+'.forge-created').is_file()
                restored=subprocess.run([str(args.forge.resolve()),'restore','--install',str(output)],cwd=args.cwd.resolve(),capture_output=True,text=True,timeout=120)
                (case/'restore.log').write_text(restored.stdout+restored.stderr,encoding='utf-8');assert restored.returncode==0
                if mode=='existing':assert dest.read_bytes()==original and (dest.parent/'names.bin').read_bytes()==pool
                else:assert not dest.exists() and not (dest.parent/'names.bin').exists()
                cases+=1;print(donor_name,mode,len(offsets),'self slots, all prior records/pool and Restore: PASS',flush=True)
    finally:
        assert {str(path):digest(path) for path in inputs}==hashes,'native inputs changed'
    (work/'result.json').write_text(json.dumps({'passed':True,'cases':cases,'native_hashes_unchanged':hashes},indent=2)+'\n',encoding='utf-8')
    print('Retail paired Creature clone: PASS',cases,'cases',flush=True)


if __name__=='__main__':main()
