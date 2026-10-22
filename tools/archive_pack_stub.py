"""Correct the x86 shift-by-32 carry in retail CPackedUIntArray packing.

Entry at A62D4A: ECX=carry shift (1..32), EDX=value, EAX=bits after
adding the current item. Exit to A62D53 preserves native registers/flags,
except that a 32-bit carry is correctly zero instead of copying the value.
"""
import argparse
from pathlib import Path
import struct

SITE=0xa62d4a
ORIGINAL=bytes.fromhex('8bfad3ef83e8208bef')
TEMPLATE=bytes.fromhex('31ff80f92074048bfad3ef83e8208befe900000000')


def build(origin,target=0xa62d53):
    if not 0<=origin<=0xffffffff-len(TEMPLATE):raise ValueError('stub must fit x86 address space')
    result=bytearray(TEMPLATE)
    struct.pack_into('<I',result,len(result)-4,(target-origin-len(result))&0xffffffff)
    return bytes(result)


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--cpp-header',type=Path,required=True);a=ap.parse_args()
    values=','.join(f'0x{x:02x}' for x in TEMPLATE);before=','.join(f'0x{x:02x}' for x in ORIGINAL)
    a.cpp_header.write_text('#pragma once\n#include <array>\n#include <cstdint>\n#include <cstring>\n'
        'namespace texture_archive {\n'
        f'inline constexpr std::array<std::uint8_t,{len(ORIGINAL)}> packCarryOriginal{{{before}}};\n'
        f'inline auto packCarryStub(std::uint32_t origin,std::uint32_t target=0xa62d53u) {{\n'
        f'    std::array<std::uint8_t,{len(TEMPLATE)}> bytes{{{values}}};\n'
        f'    const std::uint32_t relative=target-(origin+{len(TEMPLATE)}u);\n'
        f'    std::memcpy(bytes.data()+{len(TEMPLATE)-4},&relative,4);return bytes;\n'
        '}\n}\n')


if __name__=='__main__':main()
