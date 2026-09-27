#!/usr/bin/env python3
"""Compare the compiled navigation geometry kernel with FableWin under Unicorn.

Requires Python unicorn, a GNU-compatible C++20 compiler, and the identified
debug executable supplied locally. No game process is launched or modified.
  python tools/verify_debug_nav.py --debug-exe <path/to/FableWin.exe>

Executes C2DLineF::IntersectsWith(box), including its real segment/normalization
instructions. Only the CRT fabs/sqrt imports are replaced by x87 math stubs.
This verifies a geometry primitive, not the entire navigation generator.
"""
import argparse
import hashlib
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEBUG_SHA256 = "a9d6d0977d9d7fc8242da4292ae7ed92e926ca80364a95c003d9b9f689e41845"


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


class Oracle:
    def __init__(self, path):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
        from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EAX
        self.registers = (UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EAX)
        exe = path.read_bytes()
        if hashlib.sha256(exe).hexdigest() != DEBUG_SHA256:
            raise ValueError("unrecognized debug executable: native addresses require the documented SHA256")
        pe = struct.unpack_from("<I", exe, 0x3c)[0]
        optional = pe + 24
        base = struct.unpack_from("<I", exe, optional + 28)[0]
        size = struct.unpack_from("<I", exe, optional + 56)[0]
        u = self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(base, (size + 4095) & ~4095)
        u.mem_write(base, exe[:struct.unpack_from("<I", exe, optional + 60)[0]])
        sections = optional + struct.unpack_from("<H", exe, pe + 20)[0]
        for i in range(struct.unpack_from("<H", exe, pe + 6)[0]):
            _, va, raw_size, raw_offset = struct.unpack_from("<IIII", exe, sections + i * 40 + 8)
            if raw_size:
                u.mem_write(base + va, exe[raw_offset:raw_offset + raw_size])
        for address, length in ((0, 4096), (0x10000000, 4096), (0x11000000, 0x100000), (0x12000000, 4096)):
            u.mem_map(address, length)

        def cstr(address):
            result = bytearray()
            while (value := u.mem_read(address + len(result), 1)[0]):
                result.append(value)
            return result.decode("ascii")

        imports = base + struct.unpack_from("<I", exe, optional + 104)[0]
        patched = set()
        for descriptor in range(imports, imports + 4096, 20):
            original, _, _, name, table = struct.unpack("<5I", u.mem_read(descriptor, 20))
            if not name:
                break
            for i in range(2000):
                ref = struct.unpack("<I", u.mem_read(base + original + i * 4, 4))[0]
                if not ref:
                    break
                if ref & 0x80000000:
                    continue
                function = cstr(base + ref + 2)
                if function not in ("fabs", "sqrt"):
                    continue
                address = 0x10000100 if function == "fabs" else 0x10000110
                # fld qword [esp+4]; fabs/fsqrt; ret (cdecl, return in ST0).
                u.mem_write(address, bytes.fromhex("dd442404d9e1c3" if function == "fabs" else "dd442404d9fac3"))
                u.mem_write(base + table + i * 4, struct.pack("<I", address))
                patched.add(function)
        if patched != {"fabs", "sqrt"}:
            raise ValueError("expected CRT math imports not found")

    def edges(self, line, box):
        eip, esp, ecx, eax = self.registers
        self.u.mem_write(0x12000000, struct.pack("<8f", *line, *box))
        self.u.mem_write(0x110ff000, struct.pack("<II", 0x10000000, 0x12000010))
        self.u.reg_write(esp, 0x110ff000)
        self.u.reg_write(ecx, 0x12000000)
        self.u.emu_start(0x017fe9a5, 0x10000000, count=100000)
        if self.u.reg_read(eip) != 0x10000000:
            raise RuntimeError("native intersection exceeded the instruction budget")
        return bool(self.u.reg_read(eax) & 255)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--debug-exe", type=Path, required=True)
    ap.add_argument("--cxx", help="GNU-compatible C++20 compiler; defaults to this worktree's CMake compiler")
    ap.add_argument("--count", type=int, default=10000)
    args = ap.parse_args()
    if args.count <= 0:
        ap.error("--count must be positive")
    oracle = Oracle(args.debug_exe)
    compiler = args.cxx
    cache = ROOT / "build/CMakeCache.txt"
    if not compiler and cache.exists():
        for line in cache.read_text().splitlines():
            if line.startswith("CMAKE_CXX_COMPILER:"):
                compiler = line.split("=", 1)[1]
                break
    compiler = compiler or "c++"
    rng = random.Random(1729)
    cases = []
    for i in range(args.count):
        x, y, size = rng.choice((0, 16, 96)), rng.choice((0, 32, 112)), rng.choice((0.5, 1, 2, 4, 32))
        box = (x, y, x + size, y + size)
        line = tuple(f32(origin + rng.uniform(-2, 3) * size) for origin in (x, y, x, y))
        if i % 2 == 0:
            line = (line[0], f32(y + size + rng.uniform(-0.0002, 0.0002)),
                    line[2], f32(y + size + rng.uniform(-0.0002, 0.0002)))
        cases.append((line, box))
    cases.extend((line, (0, 0, 1, 1)) for line in
                 ((-1, 1, 2, 1), (0.2, 1, 0.8, 1), (0.5, 0.5, 0.5, 0.5),
                  (1, 1, 1, 1), (-1, 1.00005, 2, 1.00005)))
    probe = r'''
#include "navgeometry.hpp"
#include <iostream>
int main() {
    forge::navmesh::Line a; forge::navmesh::DetailArea b;
    while (std::cin >> a.x0 >> a.y0 >> a.x1 >> a.y1 >> b.x0 >> b.y0 >> b.x1 >> b.y1) {
        using forge::navmesh::detail::intersects;
        std::cout << (intersects(a, {b.x0,b.y0,b.x1,b.y0}) || intersects(a, {b.x1,b.y0,b.x1,b.y1}) ||
                      intersects(a, {b.x0,b.y1,b.x1,b.y1}) || intersects(a, {b.x0,b.y0,b.x0,b.y1})) << '\n';
    }
}
'''
    build = (ROOT / "build").resolve()
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nav-oracle-", dir=build) as scratch:
        folder = Path(scratch).resolve()
        assert folder.is_relative_to(build)
        source, exe = folder / "probe.cpp", folder / ("probe.exe" if os.name == "nt" else "probe")
        source.write_text(probe)
        flags = ["-static", "-static-libgcc", "-static-libstdc++"] if os.name == "nt" else []
        subprocess.run([compiler, "-std=c++20", "-O3", *flags, "-I", str(ROOT / "libs/forgecore/include"),
                        "-I", str(ROOT / "libs/forgecore/src"), str(source), "-o", str(exe)], check=True)
        stdin = "".join(" ".join(map(str, (*line, *box))) + "\n" for line, box in cases)
        result = subprocess.run([str(exe)], input=stdin, capture_output=True, text=True, check=True)
        actual = list(map(int, result.stdout.split()))
    if len(actual) != len(cases):
        raise RuntimeError("compiled probe returned the wrong number of results")
    mismatches = []
    for (line, box), value in zip(cases, actual):
        expected = oracle.edges(line, box)
        if bool(value) != expected:
            mismatches.append((line, box, bool(value), expected))
    print(f"C++ / emulated FableWin edge intersections: {len(cases) - len(mismatches)}/{len(cases)} match")
    for mismatch in mismatches[:8]:
        print("line, box, C++, native:", mismatch)
    return 1 if mismatches else 0


if __name__ == "__main__":
    raise SystemExit(main())
