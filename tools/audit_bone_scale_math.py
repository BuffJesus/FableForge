#!/usr/bin/env python3
"""Bounded original-x86 vector arithmetic used by positive skeletal combination.

No container/parameter/animation bridges: runs subtraction, multiplication, +=,
their constructor and its original thunk. This is arithmetic evidence only, not
execution of the complete CombineMorph routine or skeleton deformation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP
from audit_creature_appearance_rng import EXPECTED_SHA


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--census', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reader', type=Path, help='Optional C++ boneconfig test; invoke inside a guarded lease')
    args = parser.parse_args()
    if args.output.resolve() in [p.resolve() for p in [args.exe, args.census, args.reader] if p]:
        raise ValueError('output must differ from inputs')
    raw = args.exe.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_SHA:
        raise ValueError('unsupported native executable; no execution attempted')
    census_raw = args.census.read_bytes()
    census = json.loads(census_raw)
    pe = pefile.PE(data=raw)
    ranges = [(0x018c0af0,82), (0x019072b0,90), (0x01ded8a0,67),
              (0x018a0be0,42), (0x018409f9,5)]
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    pages = set()
    for address, size in ranges:
        page = address & ~4095
        if page not in pages:
            machine.mem_map(page,4096)
            pages.add(page)
        machine.mem_write(address,pe.get_data(address-pe.OPTIONAL_HEADER.ImageBase,size))
    machine.mem_map(0x10000000,0x10000)
    incoming, identity, delta, scaled, accumulated = [0x10000100+i*0x100 for i in range(5)]
    stop, stack = 0x10007000, 0x10008000
    machine.mem_write(identity,struct.pack('<3f',1,1,1))

    def call(entry, this, arguments, returned):
        data = struct.pack('<I',stop)+arguments
        machine.mem_write(stack-len(data),data)
        machine.reg_write(UC_X86_REG_ESP,stack-len(data))
        machine.reg_write(UC_X86_REG_ECX,this)
        machine.emu_start(entry,stop,timeout=100000,count=256)
        if (machine.reg_read(UC_X86_REG_EIP)!=stop or
            machine.reg_read(UC_X86_REG_ESP)!=stack or
            machine.reg_read(UC_X86_REG_EAX)!=returned):
            raise AssertionError('bounded native arithmetic did not return normally')

    cases = 0
    weights = [0, f32(.1), .5, 1]
    bases = [(1,1,1),(.75,2,.25),(100000,-2,4)]
    samples = []
    vectors = []
    for resource in census['resources']:
        for bone in resource['bones']:
            source = [f32(v) for v in bone['scale']]
            for weight in weights:
                for base in bases:
                    machine.mem_write(incoming,struct.pack('<3f',*source))
                    machine.mem_write(accumulated,struct.pack('<3f',*base))
                    call(0x019072b0,incoming,struct.pack('<II',delta,identity),delta)
                    call(0x018c0af0,delta,struct.pack('<If',scaled,weight),scaled)
                    call(0x01ded8a0,accumulated,struct.pack('<I',scaled),accumulated)
                    actual = bytes(machine.mem_read(accumulated,12))
                    expected = struct.pack('<3f',*[f32(f32(a)+f32(f32(v-1)*weight))
                                                  for a,v in zip(base,source)])
                    if actual != expected:
                        raise AssertionError((resource['name'],bone['name'],weight,base,actual.hex(),expected.hex()))
                    vectors.append(dict(source=source,weight=weight,base=base,result=actual.hex()))
                    if len(samples)<4:
                        samples.append(dict(source=source,weight=weight,base=base,result=actual.hex()))
                    cases += 1
    if hashlib.sha256(args.exe.read_bytes()).hexdigest()!=digest or args.census.read_bytes()!=census_raw:
        raise AssertionError('audit input changed')
    report = dict(passed=True,cases=cases,resources=len(census['resources']),
        executable_sha256=digest,census_sha256=hashlib.sha256(census_raw).hexdigest(),
        producer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        instruction_ranges=[dict(address=f'{a:08x}',length=n,
            sha256=hashlib.sha256(pe.get_data(a-pe.OPTIONAL_HEADER.ImageBase,n)).hexdigest()) for a,n in ranges],
        samples=samples,inputs_unchanged=True,bridges=[],scope=__doc__)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    if args.reader:
        binary_hash = hashlib.sha256(args.reader.read_bytes()).hexdigest()
        vector_path = args.output.with_suffix('.vectors.json')
        if vector_path.exists() or vector_path.resolve() in [args.exe.resolve(),args.census.resolve(),args.reader.resolve()]:
            raise ValueError('comparison vector output must be fresh and differ from inputs')
        vector_path.write_text(json.dumps([{k:v for k,v in item.items() if k!='result'}
                                          for item in vectors])+'\n',encoding='utf-8')
        # Caller owns the shared lease; repeat the conservative process guard
        # immediately before this executable launch after offline emulation.
        import psutil
        active = [p.info for p in psutil.process_iter(['pid','name'])
                  if any(key in (p.info['name'] or '').lower() for key in ['fable','xenia','v83'])]
        if active:
            raise RuntimeError(f'compiled comparison process guard refused: {active}')
        child = subprocess.run([str(args.reader.resolve()),'--scale-math-audit',str(vector_path.resolve())],
                               capture_output=True,text=True,check=True,timeout=30)
        actual = json.loads(child.stdout)
        if len(actual)!=len(vectors) or any(struct.pack('<3I',*row).hex()!=item['result']
                                           for row,item in zip(actual,vectors)):
            raise AssertionError('compiled positive scale combination differs from native arithmetic')
        if hashlib.sha256(args.reader.read_bytes()).hexdigest()!=binary_hash:
            raise AssertionError('compiled reader changed during audit')
        report.update(compiled_comparisons=len(actual),reader_sha256=binary_hash,
                      vector_sha256=hashlib.sha256(vector_path.read_bytes()).hexdigest())
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'Bone scale arithmetic: {cases} original-x86 comparisons passed; {args.output}')


if __name__ == '__main__':
    main()
