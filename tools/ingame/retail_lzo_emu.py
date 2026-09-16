#!/usr/bin/env python3
"""Run the RETAIL engine's lzo1x_decompress (Fable.exe @0x00c06b90) under Unicorn on an
LZO stream and return what the game would decode. The oracle for "does the engine accept
our encoder's output", without launching the game.

  python tools/ingame/retail_lzo_emu.py <stream.lzo> <expected-uncompressed-len> [--expect file.raw]
Also importable: decompress(stream: bytes, out_len: int) -> (rc, bytes, consumed)
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UcError
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_EIP

FABLE = Path(r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters\Fable.exe")
DECOMPRESS = 0x00C06B90        # _lzo1x_decompress
DECOMPRESS_SAFE = 0x00C08170   # _lzo1x_decompress_safe
DECOMPRESS_ASM = 0x00C069D0    # lzo1x_decompress_asm_fast: what the landscape loader (LoadCompressed) actually calls
IMAGE_BASE = 0x400000

_uc = None
_layout = {}


def _load() -> Uc:
    global _uc
    if _uc is not None:
        return _uc
    data = FABLE.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    sizeof_image = struct.unpack_from("<I", data, pe + 24 + 56)[0]
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    span = (sizeof_image + 0xFFFF) & ~0xFFFF
    uc.mem_map(IMAGE_BASE, span)
    table = pe + 24 + optsz
    for i in range(nsec):
        e = table + i * 40
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, e + 8)
        if rsize:
            uc.mem_write(IMAGE_BASE + vaddr, data[roff:roff + min(rsize, vsize if vsize else rsize)])
    # scratch: stack + buffers
    _layout["stack"] = 0x10000000
    uc.mem_map(_layout["stack"], 0x100000)
    _layout["heap"] = 0x20000000
    uc.mem_map(_layout["heap"], 0x4000000)   # 64 MB: in + out + out_len + wrk
    _uc = uc
    return uc


def decompress(stream: bytes, out_len: int, safe: bool = False, asm: bool = False) -> tuple[int, bytes, int]:
    uc = _load()
    heap = _layout["heap"]
    in_addr = heap
    out_addr = heap + 0x1000000
    outlen_addr = heap + 0x3000000
    wrk_addr = heap + 0x3000100
    sentinel = heap + 0x3FF0000
    uc.mem_write(in_addr, stream)
    uc.mem_write(out_addr, b"\0" * (out_len + 64))
    uc.mem_write(outlen_addr, struct.pack("<I", out_len + 64))
    uc.mem_write(sentinel, b"\xCC" * 16)
    sp = _layout["stack"] + 0x80000
    frame = struct.pack("<IIIIII", sentinel, in_addr, len(stream), out_addr, outlen_addr, wrk_addr)
    uc.mem_write(sp, frame)
    uc.reg_write(UC_X86_REG_ESP, sp)
    try:
        uc.emu_start(DECOMPRESS_ASM if asm else (DECOMPRESS_SAFE if safe else DECOMPRESS), sentinel, timeout=20_000_000, count=200_000_000)
    except UcError as e:
        return -100, b"", int(uc.reg_read(UC_X86_REG_EIP))
    rc = uc.reg_read(UC_X86_REG_EAX)
    produced = struct.unpack("<I", uc.mem_read(outlen_addr, 4))[0]
    out = bytes(uc.mem_read(out_addr, min(produced, out_len + 64)))
    return int(rc) if rc < 0x80000000 else int(rc) - (1 << 32), out, produced


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__); return 2
    stream = Path(sys.argv[1]).read_bytes()
    out_len = int(sys.argv[2])
    rc, out, produced = decompress(stream, out_len)
    print(f"rc={rc} produced={produced} expected={out_len}")
    if "--expect" in sys.argv:
        exp = Path(sys.argv[sys.argv.index("--expect") + 1]).read_bytes()
        same = out[:len(exp)] == exp
        print("matches expected:", same)
        if not same:
            for i, (a, b) in enumerate(zip(out, exp)):
                if a != b:
                    print("first difference at", i, "of", len(exp)); break
    return 0


if __name__ == "__main__":
    sys.exit(main())
