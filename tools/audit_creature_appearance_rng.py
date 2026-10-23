#!/usr/bin/env python3
"""Compare the recovered appearance RNG with bounded original FableWin x86 execution.

Read-only evidence tool; requires pefile and unicorn. This checks the RNG primitive,
not body-selection ordering, serialization, animation or rendered compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP


EXPECTED_SHA = 'a9d6d0977d9d7fc8242da4292ae7ed92e926ca80364a95c003d9b9f689e41845'
ENTRY = 0x018cf370
ROTATE_THUNK = 0x0183fdab
ROTATE = 0x018bad60


def recovered(seed, bound):
    word = (seed * 0x24a1 + 0x24df) & 0xffffffff
    seed = ((word >> 13) | (word << 19)) & 0xffffffff
    return seed, seed % bound if bound else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raw = args.exe.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_SHA:
        raise ValueError('unsupported executable identity; no execution attempted')
    if args.output.resolve() == args.exe.resolve():
        raise ValueError('evidence output must differ from executable')
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    ranges = [(ENTRY, 87), (ROTATE_THUNK, 5), (ROTATE, 18)]
    for address, length in ranges:
        machine.mem_map(address & ~0xfff, 0x1000)
        machine.mem_write(address, pe.get_data(address-base, length))
    stack, seed_at, stop = 0x10010000, 0x10020000, 0x10030000
    for address in (stack-0x1000, seed_at, stop):
        machine.mem_map(address, 0x1000)
    bounds = [0, 1, 2, 3, 7, 50, 100, 255, 4096, 0x7fffffff, 0xffffffff]
    seeds = [0, 1, 2, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff]
    seeds += [int.from_bytes(hashlib.sha256(str(i).encode('ascii')).digest()[:4], 'little')
              for i in range(25)]
    comparisons = 0
    samples = []
    for initial in seeds:
        for bound in bounds:
            seed = initial
            for step in range(4):
                expected_seed, expected_result = recovered(seed, bound)
                machine.mem_write(stack-4, struct.pack('<I', stop))
                machine.mem_write(seed_at, struct.pack('<I', seed))
                machine.reg_write(UC_X86_REG_ESP, stack-4)
                machine.reg_write(UC_X86_REG_ECX, bound)
                machine.reg_write(UC_X86_REG_EDX, seed_at)
                machine.emu_start(ENTRY, stop, timeout=100000, count=128)
                if (machine.reg_read(UC_X86_REG_ESP) != stack or
                        machine.reg_read(UC_X86_REG_EIP) != stop):
                    raise AssertionError('bounded execution did not return normally')
                actual_seed, = struct.unpack('<I', machine.mem_read(seed_at, 4))
                actual_result = machine.reg_read(UC_X86_REG_EAX)
                if (actual_seed, actual_result) != (expected_seed, expected_result):
                    raise AssertionError((initial, bound, step, actual_seed, actual_result))
                if initial in (0, 0xffffffff) and bound == 3:
                    samples.append(dict(initial=initial, step=step, seed=actual_seed, result=actual_result))
                seed = actual_seed
                comparisons += 1
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != digest:
        raise AssertionError('executable changed during audit')
    report = dict(executable=str(args.exe.resolve()), sha256=digest,
                  instruction_ranges=[dict(address=f'{a:08x}', length=n,
                      sha256=hashlib.sha256(pe.get_data(a-base, n)).hexdigest()) for a, n in ranges],
                  seeds=len(seeds), bounds=bounds, sequence_length=4,
                  comparisons=comparisons, mismatches=0, samples=samples,
                  executable_unchanged=True,
                  scope='Original bounded x86 RNG execution only; not creature rendering acceptance.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'Appearance RNG: {comparisons} original-x86 comparisons passed; {args.output}')


if __name__ == '__main__':
    main()
