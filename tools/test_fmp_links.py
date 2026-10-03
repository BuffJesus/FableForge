#!/usr/bin/env python3
"""Audit Controller FMP named camera targets through standalone and composed banks."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib


def u32(data, offset): return struct.unpack_from('<I', data, offset)[0]


def crc0(name):
    crc = 0
    for byte in name.encode('ascii'):
        crc ^= byte
        for _ in range(8): crc = (crc >> 1) ^ (0xedb88320 if crc & 1 else 0)
    return crc


def read_bank(root):
    folder = root/'data/CompiledDefs'
    names_data = (folder/'names.bin').read_bytes()
    names, pos = {}, 20
    for _ in range(u32(names_data,8)):
        end = names_data.index(0,pos+4)
        names[pos+4-20] = names_data[pos+4:end].decode('ascii')
        pos = end+1
    data = (folder/'game.bin').read_bytes()
    count = u32(data,9)
    chunk_pos = 13+count*12
    chunks = [struct.unpack_from('<II',data,chunk_pos+4+i*8) for i in range(u32(data,chunk_pos))]
    start = chunk_pos+4+len(chunks)*8+4
    entries = []
    for (first,offset),(last,end) in zip(chunks,chunks[1:]):
        block = zlib.decompress(data[start+offset:start+end])
        offsets = list(struct.unpack_from('<'+'H'*(last-first),block))+[len(block)]
        for local,index in enumerate(range(first,last)):
            definition,name,_ = struct.unpack_from('<III',data,13+index*12)
            entries.append({'name':names.get(name,''),'definition':names[definition],
                'data':block[offsets[local]:offsets[local+1]]})
    assert len(entries)==count
    return entries


def audit(root):
    entries = read_bank(root)
    by_name = {e['name']:i for i,e in enumerate(entries) if e['name']}
    errors=[]
    checked=[]
    def scalar(record,field,target):
        data=entries[by_name[record]]['data']; tag=struct.pack('<I',crc0(field))
        assert data.count(tag)==1,(record,field)
        offset=data.index(tag)+4
        actual=u32(data,offset); expected=by_name[target]
        row={'record':record,'field':field,'target':target,'expected_id':expected,
             'actual_id':actual,'actual_target':entries[actual]['name'] if actual<len(entries) else '<out of range>'}
        checked.append(row)
        if actual!=expected: errors.append(row)
    for field,target in [('CameraManagerMain','CAMERA_MANAGER_MAIN'),
        ('CameraManagerCombat','CAMERA_MANAGER_COMBAT_MAIN'),
        ('CameraManagerPCMain','CAMERA_MANAGER_PC_MAIN'),
        ('CameraManagerPCCombat','CAMERA_MANAGER_PC_MAIN_COMBAT')]:
        scalar('CAMERA_MANAGER_SET_TEMPLATE',field,target)
    owner='CAMERA_MANAGER_PC_MAIN_COMBAT'; data=entries[by_name[owner]]['data']
    for field,targets in [('CameraList',['CAMERA_PC_NORMAL','CAMERA_PC_FAR']),
        ('SpecialityCameraList',['CAMERA_FIRST_PERSON','CAMERA_FACE','CAMERA_TORSO'])]:
        tag=struct.pack('<I',crc0(field)); assert data.count(tag)==1
        offset=data.index(tag)+4; assert u32(data,offset)==len(targets)
        for index,target in enumerate(targets):
            actual=u32(data,offset+4+index*4); expected=by_name[target]
            row={'record':owner,'field':field+'['+str(index)+']','target':target,
                'expected_id':expected,'actual_id':actual,
                'actual_target':entries[actual]['name'] if actual<len(entries) else '<out of range>'}
            checked.append(row)
            if actual!=expected: errors.append(row)
    for field,target in [('InitialMode','CAMERA_PC_NORMAL'),('ZTargetMode','CAMERA_PC_NORMAL'),
        ('ProjectileWeaponMode','CAMERA_PROJECTILE_WEAPON_CAM')]: scalar(owner,field,target)
    assert struct.unpack_from('<H',data,3)[0]==1
    assert u32(data,5)==crc0('CEntitySoundDef'),'component class CRC changed'
    actual=u32(data,13); expected=by_name['CAMERA_MANAGER_TEMPLATE']
    row={'record':owner,'field':'CDefs[0].InheritedEntryID','target':'CAMERA_MANAGER_TEMPLATE',
         'expected_id':expected,'actual_id':actual,'actual_target':entries[actual]['name'] if actual<len(entries) else '<out of range>'}
    checked.append(row)
    if actual!=expected: errors.append(row)
    return {'checked':len(checked)+1,'references':checked,'errors':errors}


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--fmp',type=Path,required=True)
    parser.add_argument('--aeon',type=Path,required=True)
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='fmp-links-',dir=repo/'build')).resolve()
    print('Evidence retained at',work,flush=True)
    tool=repo/'build/forge-tools.exe'; schema=repo/'docs/re_reference/def_schema.json'
    sources=[args.fmp]+[root/'data/CompiledDefs'/name for root in (args.install,args.aeon) for name in ('game.bin','names.bin')]
    def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
    before={p:digest(p) for p in sources}
    reports={}
    for label,base in [('retail',args.install),('aeon',args.aeon)]:
        output=work/label
        result=subprocess.run([str(tool),'fmp','apply',str(base),str(args.fmp),str(output)],capture_output=True,timeout=60)
        (work/(label+'.log')).write_bytes(result.stdout+result.stderr)
        assert result.returncode==0,(result.stdout+result.stderr).decode('utf-8',errors='replace')
        reports[label]=audit(output)
    base,aeon=work/'base',work/'aeon-source'
    for original,copy in [(args.install,base),(args.aeon,aeon)]:
        for name in ('game.bin','names.bin'):
            target=copy/'data/CompiledDefs'/name; target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(original/'data/CompiledDefs'/name,target)
    merged=work/'merged'
    result=subprocess.run([str(tool),'mods','merge',str(base),str(merged),'--with',str(aeon),'--with',str(args.fmp),'--fields',str(schema)],capture_output=True,timeout=120)
    (work/'merged.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode==0,(result.stdout+result.stderr).decode('utf-8',errors='replace')
    reports['merged']=audit(merged)
    assert {p:digest(p) for p in sources}==before
    (work/'result.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')
    for label,report in reports.items(): print(label,report['checked'],'references checked;',len(report['errors']),'mismatches',flush=True)
    assert all(not report['errors'] for report in reports.values()),'FMP named references do not resolve to their intended targets; see result.json'


if __name__=='__main__': main()
