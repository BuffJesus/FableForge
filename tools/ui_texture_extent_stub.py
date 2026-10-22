"""Build an experimental x86 extent callback trampoline; never installs it.

Callback ABI: unsigned __cdecl resolve(bank, runtimeIndex, axis, nativeExtent).
The callback must obey the x86 C ABI and must not throw across the trampoline.
"""
import struct

SITES = ((0x9dcadf, 0), (0x9dcb0e, 0), (0x9dcb34, 1), (0x9dcb6c, 1),
         (0x41a8fc, 0), (0x41a94d, 1))
ORIGINAL = bytes.fromhex('85c089442418')

def original(site):
    return bytes.fromhex('85c089442408') if site<0x500000 else ORIGINAL


def jump(source, destination):
    return b'\xe9' + struct.pack('<I', (destination - source - 5) & 0xffffffff)


def trampoline(address, callback, site, axis):
    if (site, axis) not in SITES:
        raise ValueError('unknown extent site/axis')
    # Save integer state. EBP anchors PUSHAD while ESP becomes 16-byte aligned.
    code = bytearray.fromhex('9c6089e581ec1002000083e4f0')
    code += bytes.fromhex('0fae0424')  # FXSAVE [ESP]: x87/MMX/SSE/MXCSR
    code += b'\x50\x6a' + bytes([axis])
    # Frontend cache reads retain ECX bank; ESI is UI object, index at +178.
    code += bytes.fromhex('ffb67801000051') if site<0x500000 else b'\x57\x56'
    code += b'\xe8' + struct.pack('<I', (callback-address-len(code)-5) & 0xffffffff)
    code += bytes.fromhex('83c41089451c0fae0c2489ec619d')
    # Restore original state except EAX; replay both overwritten instructions.
    code += original(site)
    code += jump(address + len(code), site + len(ORIGINAL))
    return bytes(code)


if __name__ == '__main__':
    import argparse
    from pathlib import Path
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpp-header',type=Path,required=True)
    args=parser.parse_args()
    code=trampoline(0x20000000,0x20001000,SITES[0][0],0)
    # The hardware test relocates the two relative branches and the axis byte.
    assert code[22]==0xe8 and code[-5]==0xe9 and code[18]==0x6a
    lines=['#pragma once','inline constexpr unsigned char extentStubTemplate[] = {',
           ','.join(hex(b) for b in code),'};',
           'inline constexpr unsigned extentCallOffset=22, extentAxisOffset=19;']
    frontend=trampoline(0x20000000,0x20001000,0x41a8fc,0)
    assert frontend[27]==0xe8
    lines+=['inline constexpr unsigned char frontendStubTemplate[] = {',
            ','.join(hex(b) for b in frontend),'};',
            'inline constexpr unsigned frontendCallOffset=27;']
    args.cpp_header.write_text('\n'.join(lines)+'\n',encoding='ascii')
