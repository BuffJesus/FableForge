#!/usr/bin/env python3
"""Custom-theme success and failure recovery on copied banks; never writes the install."""
import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

from test_meshimport import find_root, pristine, write_png

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = ROOT / "build" / "theme_import_root"


def snapshot():
    result = {}
    for path in SCRATCH.rglob("*"):
        if path.is_file():
            with path.open("rb") as stream:
                result[path.relative_to(SCRATCH)] = hashlib.file_digest(stream, "sha256").hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    source = find_root(args.root)
    if not source:
        print("custom theme test skipped (no install)")
        return 0
    if SCRATCH.resolve().parent != (ROOT / "build").resolve():
        raise RuntimeError("unexpected theme scratch root")
    shutil.rmtree(SCRATCH, ignore_errors=True)
    try:
        banks = ("data/CompiledDefs/game.bin", "data/CompiledDefs/names.bin", "data/graphics/pc/textures.big")
        for rel in banks:
            target = SCRATCH / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(pristine(source, rel), target)
        write_png(SCRATCH / "base.png")
        write_png(SCRATCH / "cliff.png")
        (SCRATCH / "invalid.png").write_bytes(b"not a PNG")

        def run(name, cliff):
            return subprocess.run([str(ROOT / "build/forge.exe"), "theme-add", str(SCRATCH / "base.png"),
                                   name, "--cliff", str(SCRATCH / cliff), "--install", str(SCRATCH)],
                                  cwd=ROOT, capture_output=True, text=True, timeout=180)

        initial = run("GROUND_FORGE_THEME_TEST", "cliff.png")
        if initial.returncode:
            raise RuntimeError(initial.stderr)
        decoded = subprocess.run([str(ROOT / "build/forge-tools.exe"), "defs", "decode", str(SCRATCH),
                                  "docs/re_reference/def_schema.json", "GROUND_FORGE_THEME_TEST"],
                                 cwd=ROOT, capture_output=True, text=True, check=True).stdout
        fields = {line.split()[0]: line.split()[-1] for line in decoded.splitlines() if line.split()}
        assert fields["BaseTexture"] != fields["CliffBaseTexture"], decoded
        assert fields["BaseTexture"] == fields["BackgroundTexture"], decoded
        assert fields["CliffBaseTexture"] == fields["CliffBackgroundTexture"], decoded
        assert fields["BaseBumpMap"] == fields["CliffBumpMap"] == "0", decoded
        assert all((SCRATCH / (rel + ".forge-orig")).is_file() for rel in banks)

        def rejected(name, cliff):
            before = snapshot()
            result = run(name, cliff)
            okay = result.returncode != 0 and "error:" in result.stderr.lower() and before == snapshot()
            okay = okay and not list(SCRATCH.glob(".forge-theme-import-*"))
            print(name, "preserved every file" if okay else "FAILED", result.stderr.strip()[-240:])
            return okay

        okay = rejected("GROUND_FORGE_BAD_CLIFF", "invalid.png")
        if os.name == "nt":
            from ctypes import wintypes
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                          ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
            kernel.CreateFileW.restype = wintypes.HANDLE
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            kernel.CloseHandle.restype = wintypes.BOOL
            handle = kernel.CreateFileW(str(SCRATCH / banks[0]), 0x80000000, 3, None, 3, 0x80, None)
            if handle == ctypes.c_void_p(-1).value:
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                okay = rejected("GROUND_FORGE_COMMIT_FAIL", "cliff.png") and okay
            finally:
                kernel.CloseHandle(handle)
        else:
            print("late theme lock check skipped (Windows sharing mode required)")
        print("custom theme test", "OK" if okay else "FAILED")
        return 0 if okay else 1
    finally:
        if not args.keep:
            shutil.rmtree(SCRATCH, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
