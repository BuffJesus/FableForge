#!/usr/bin/env python3
"""Execute original displacement/hierarchy/scaling on bounded synthetic skeletons.

Only mesh count, local bone lookup and parent lookup are bridged. Quaternion,
matrix, keyframe and vector operations execute original bytes. No game launches.
The transposed path also executes native inverse-bind composition. This does
not execute animation sampling or vertex skinning.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP
from audit_creature_appearance_rng import EXPECTED_SHA


def multiply(a,b):
    return [sum(a[r*4+k]*b[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]


def local_matrix(q,p):
    x,y,z,w=q
    # Native ToMatrixTransposed (03382b80), including its norm-zero fallback:
    # its off-diagonal signs differ from the common glTF column-vector formula.
    norm=x*x+y*y+z*z+w*w
    factor=2/norm if norm else 1
    return [1-factor*(y*y+z*z),factor*(x*y+z*w),factor*(x*z-y*w),p[0],
            factor*(x*y-z*w),1-factor*(x*x+z*z),factor*(y*z+x*w),p[1],
            factor*(x*z+y*w),factor*(y*z-x*w),1-factor*(x*x+y*y),p[2],0,0,0,1]


def transpose(m):
    return [m[c*4+r] for r in range(4) for c in range(4)]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.resolve()==args.exe.resolve():raise ValueError('output aliases input')
    raw=args.exe.read_bytes();digest=hashlib.sha256(raw).hexdigest()
    if digest!=EXPECTED_SHA:raise ValueError('unsupported executable; no execution attempted')
    pe=pefile.PE(data=raw);base=pe.OPTIONAL_HEADER.ImageBase
    image=pe.get_memory_mapped_image();size=(len(image)+4095)&~4095
    machine=Uc(UC_ARCH_X86,UC_MODE_32);machine.mem_map(base,size);machine.mem_write(base,image)
    machine.mem_map(0,4096);machine.mem_map(0x10000000,0x20000)
    frames,scales,matrices,mesh,bones=0x10000100,0x10001000,0x10002000,0x10003000,0x10004000
    manager=0x10005000
    stack,stop=0x10018000,0x1001f000
    parents=[];executed={};calls={}
    bridges={0x017dc463:'mesh bone count',0x017b6f5b:'mesh local bone lookup',0x017d63b0:'bone parent lookup'}

    def hook(uc,address,length,unused):
        if address in bridges:
            sp=uc.reg_read(UC_X86_REG_ESP)
            this=uc.reg_read(UC_X86_REG_ECX)
            if address!=0x017d63b0 and this!=mesh:raise AssertionError('wrong mesh receiver at bridge')
            if address==0x017dc463:value,extra=len(parents),0
            elif address==0x017b6f5b:
                index,=struct.unpack('<I',uc.mem_read(sp+4,4))
                if index>=len(parents):raise AssertionError('bone lookup out of fixture')
                value,extra=bones+index*32,4
            else:
                index=(this-bones)//32
                if not 0<=index<len(parents) or this!=bones+index*32:
                    raise AssertionError('parent lookup out of fixture')
                value,extra=parents[index]&0xffffffff,0
            returned,=struct.unpack('<I',uc.mem_read(sp,4))
            uc.reg_write(UC_X86_REG_EAX,value);uc.reg_write(UC_X86_REG_ESP,sp+4+extra)
            uc.reg_write(UC_X86_REG_EIP,returned);calls[bridges[address]]=calls.get(bridges[address],0)+1
        elif base<=address<base+size:
            executed[address]=length

    machine.hook_add(UC_HOOK_CODE,hook)

    def call(entry,arguments,this=None):
        data=struct.pack('<I',stop)+struct.pack('<'+'I'*len(arguments),*arguments)
        machine.mem_write(stack-len(data),data);machine.reg_write(UC_X86_REG_ESP,stack-len(data))
        machine.reg_write(UC_X86_REG_ECX,manager if this is None else this)
        machine.emu_start(entry,stop,timeout=2000000,count=200000)
        if machine.reg_read(UC_X86_REG_EIP)!=stop or machine.reg_read(UC_X86_REG_ESP)!=stack:
            raise AssertionError(f'bounded stage {entry:08x} failed to return')

    quaternions=[(0,0,0,1),(0,0,1,0),(.5,.5,.5,.5),(.5,-.5,.5,.5),(0,0,0,0),(0,0,2,0)]
    positions=[(3,4,5),(2,3,4),(5,-2,1)]
    anisotropic=[(2,3,.5),(.75,2,4),(3,.5,1.25)]
    records=[]
    for topology,rotation,identity,cpu,transposed in itertools.product(
            [(-1,),(-1,0),(-1,0,1),(-1,0,0),(-1,-1,1)],range(len(quaternions)),[False,True],[0,1],[0,1]):
        parents=list(topology);count=len(parents)
        source_scales=[(1,1,1) if identity else anisotropic[i] for i in range(count)]
        qs=[quaternions[(rotation+i)%len(quaternions)] for i in range(count)]
        machine.mem_write(0x04bbfb19,bytes([cpu]))
        for i in range(count):
            machine.mem_write(frames+i*48,struct.pack('<12f',*qs[i],*positions[i],0,7,8,9,0))
            machine.mem_write(scales+i*12,struct.pack('<3f',*source_scales[i]))
        call(0x033823a0,[frames,scales,mesh])
        expected_positions=[list(p) if parents[i]<0 else [p[a]*source_scales[parents[i]][a] for a in range(3)]
                            for i,p in enumerate(positions[:count])]
        for i in range(count):
            actual=struct.unpack('<12f',machine.mem_read(frames+i*48,48))
            if list(actual[4:7])!=expected_positions[i] or actual[:4]!=qs[i] or actual[8:11]!=(7,8,9):
                raise AssertionError(('displacement',topology,rotation,i,actual,expected_positions[i]))
        call(0x033825f0,[frames,matrices,mesh,transposed])
        expected=[]
        for i in range(count):
            local=local_matrix(qs[i],expected_positions[i])
            expected.append(local if parents[i]<0 else multiply(expected[parents[i]],local))
        hierarchy=[]
        for i in range(count):
            actual=list(struct.unpack('<16f',machine.mem_read(matrices+i*64,64)))
            converted=actual if transposed else transpose(actual)
            if converted!=expected[i]:raise AssertionError(('hierarchy',topology,rotation,cpu,transposed,i,converted,expected[i]))
            hierarchy.append(converted)
        call(0x03382ed0,[scales,matrices,count,transposed])
        final=[]
        for i in range(count):
            target=expected[i][:]
            for row in range(3):
                for col in range(3):target[row*4+col]*=source_scales[i][col]
            actual=list(struct.unpack('<16f',machine.mem_read(matrices+i*64,64)))
            converted=actual if transposed else transpose(actual)
            if converted!=target:raise AssertionError(('post-scale',topology,rotation,cpu,transposed,i,converted,target))
            final.append(converted)
        inverse_binds=[local_matrix(quaternions[(i+1)%4],(-i-1,2,3)) for i in range(count)]
        skin=[]
        if transposed:
            for i in range(count):
                machine.mem_write(0x10006000,struct.pack('<16f',*inverse_binds[i]))
                call(0x02e8e0f0,[0x10006000],matrices+i*64)
                actual=list(struct.unpack('<16f',machine.mem_read(matrices+i*64,64)))
                target=multiply(final[i],inverse_binds[i])
                if actual!=target:raise AssertionError(('inverse-bind order',topology,rotation,cpu,i,actual,target))
                skin.append(actual)
        records.append(dict(parents=parents,rotation=rotation,identity=identity,cpu=cpu,transposed=transposed,
                            frames=[dict(rotation=qs[i],position=positions[i]) for i in range(count)],
                            scales=source_scales,inverse_binds=inverse_binds,skin=skin,
                            positions=expected_positions,hierarchy=hierarchy,final=final))
    if hashlib.sha256(args.exe.read_bytes()).hexdigest()!=digest:raise AssertionError('native input changed')
    ranges=[]
    for address,length in sorted(executed.items()):
        if ranges and ranges[-1][0]+ranges[-1][1]==address:ranges[-1][1]+=length
        else:ranges.append([address,length])
    report=dict(passed=True,cases=len(records),records=records,exe_sha256=digest,
                producer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),bridges=bridges,bridge_calls=calls,
                executed_ranges=[dict(address=f'{a:08x}',length=n,sha256=hashlib.sha256(pe.get_data(a-base,n)).hexdigest()) for a,n in ranges],
                scope=__doc__,signed_zero_ignored=True,inputs_unchanged=True)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'Creature pose stages: {len(records)} original-x86 cases passed; {args.output}')


if __name__=='__main__':main()
