"""Audited retail BIG metadata input guards, generated from instruction recipes.

Only the absolute-file directory read and the entry-list input-reader closure
change. Directory parsing in its relative memory stream retains its native cap.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SITES=(
    (0x9a8aff,'ecx',0x9a8b54),
    (0x9cfbfa,'ecx',0x9cfc2e),(0x9cfc5c,'ecx',0x9cfc94),
    (0x9cfcb5,'ecx',0x9cfced),(0x9cfd3c,'ecx',0x9cfd74),
    (0x9cfd8a,'ecx',0x9cfdc2),(0x9cfde0,'ecx',0x9cfe18),
    (0x9cfe2e,'ecx',0x9cfe66),(0x9cfe7c,'ecx',0x9cfeb4),
    (0x9cfeca,'ecx',0x9cff02),(0x9cff24,'ecx',0x9cff5c),
    (0x9cff72,'ecx',0x9cffaa),(0x9d0010,'ecx',0x9d0048),
    (0x9d00b6,'eax',0x9d00fc),
    (0x9963ae,'ecx',0x9963e2),(0x996425,'eax',0x996467),
    (0x993cd5,'ecx',0x993d0a),(0x993d6e,'eax',0x993dbb),
)


def patches():
    rows=[]
    for address,register,skip in SITES:
        opcode={'eax':b'\x3d','ecx':b'\x81\xf9'}[register]
        distance=skip-(address+len(opcode)+4+2)
        if not 0<=distance<128:raise ValueError('guard branch must be short forward JA')
        tail=bytes((0x77,distance))
        rows.append((address,opcode+struct.pack('<I',0x7fffffff)+tail,
                     opcode+struct.pack('<I',0xffffffff)+tail))
    return rows


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cpp-header',type=Path)
    ap.add_argument('--verify-exe',type=Path)
    ap.add_argument('--report',type=Path)
    args=ap.parse_args();rows=patches();sha=None
    if args.verify_exe:
        import pefile
        data=args.verify_exe.read_bytes();sha=hashlib.sha256(data).hexdigest()
        if sha!='41dc91090ae853715ac06d2e9fc96e5d545381d197ed55d624c642f34509ac10':
            raise ValueError('unknown executable')
        pe=pefile.PE(data=data)
        for address,before,after in rows:
            if pe.get_data(address-pe.OPTIONAL_HEADER.ImageBase,len(before))!=before:
                raise ValueError(f'guard instruction recipe differs at {address:x}')
    if args.cpp_header:
        lines=['#pragma once','#include "patch_transaction.hpp"','#include <array>',
               'namespace texture_archive {']
        for i,(_,before,after) in enumerate(rows):
            for name,code in (('before',before),('after',after)):
                values=','.join(f'0x{x:02x}' for x in code)
                lines.append(f'inline constexpr std::uint8_t stream_{name}_{i}[]{{{values}}};')
        lines.append(f'inline auto streamPatches() {{ return std::array<texture_font::CodePatch,{len(rows)}>{{{{')
        for i,(address,_,_) in enumerate(rows):
            lines.append(f'    {{0x{address:x},stream_before_{i},stream_after_{i}}},')
        lines+=['}}; }','}']
        args.cpp_header.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    if args.report:
        args.report.write_text(json.dumps(dict(exeSha256=sha,sites=[dict(address=hex(a),before=b.hex(),after=c.hex()) for a,b,c in rows]),indent=2)+'\n')
    if args.verify_exe:print('PASS all 18 generated metadata-guard instruction recipes match the original PE')


if __name__=='__main__':main()
