"""Checked signed-delta addition for the audited CDiskFileWin32::MovePosition.

Entry: ECX=current unsigned position, EDX=signed delta. Exit: EAX=target,
other registers unchanged. Overflow/underflow follows the original throw path;
otherwise the original unsigned length check and flush/seek path still run.
"""
import argparse
import struct
from pathlib import Path

TEMPLATE = bytes.fromhex(
    '8d0411'       # lea eax,[ecx+edx]
    '85d2'         # test edx,edx
    '7809'         # js negative
    '3bc1'         # cmp eax,ecx
    '720e'         # jb reject: nonnegative delta wrapped below current
    'e900000000'   # jmp original length check
    '3bc1'         # negative: cmp eax,ecx
    '7705'         # ja reject: negative delta wrapped above current
    'e900000000'   # jmp original length check
    'e900000000')  # reject: jmp original negative-position exception
RELOCATIONS = ((12, 0x99acfe), (21, 0x99acfe), (26, 0x99acb7))


def build(origin):
    if not 0 <= origin <= 0xffffffff - len(TEMPLATE):
        raise ValueError('stub must fit in 32-bit address space')
    result = bytearray(TEMPLATE)
    for at, target in RELOCATIONS:
        struct.pack_into('<I', result, at, (target - (origin + at + 4)) & 0xffffffff)
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpp-header', type=Path, required=True)
    args = parser.parse_args()
    values = ','.join(f'0x{x:02x}' for x in TEMPLATE)
    relocations = '\n'.join(
        f'    relative = {"reject" if target == 0x99acb7 else "accept"} - (origin + {at + 4}u); '
        f'std::memcpy(bytes.data()+{at}, &relative, 4);' for at, target in RELOCATIONS)
    args.cpp_header.write_text(
        '#pragma once\n#include <array>\n#include <cstdint>\n#include <cstring>\n'
        'namespace texture_archive {\n'
        f'inline auto moveStub(std::uint32_t origin, std::uint32_t accept=0x99acfeu, '
        f'std::uint32_t reject=0x99acb7u) {{\n'
        f'    std::array<std::uint8_t,{len(TEMPLATE)}> bytes{{{values}}};\n'
        f'    std::uint32_t relative;\n{relocations}\n    return bytes;\n}}\n}}\n',
        encoding='utf-8')


if __name__ == '__main__':
    main()
