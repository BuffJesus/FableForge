#!/usr/bin/env python3
"""Exercise Setup > Restore on a synthetic loose-level install, never the game."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = ROOT / "build" / "ui_setup_restore_root"


def level(height: float) -> bytes:
    b = bytearray()
    def u32(value: int) -> None:
        b.extend(struct.pack("<I", value))
    u32(25)
    b.extend((4, 25, 0, 0, 0))
    for _ in range(4):
        u32(0)
    b.extend((22, 8, 0, 0, 8))
    for value in (7, 0, 8, 8):
        u32(value)
    b.append(0)
    b.extend(bytes(33792))
    u32(1)
    u32(1)
    b.extend(bytes(33792))
    for _ in range(81):
        cell = bytearray(21)
        struct.pack_into("<f", cell, 5, height / 2048)
        cell[13] = 255
        b.extend(cell)
    struct.pack_into("<I", b, 21, len(b))
    u32(0)
    u32(0)
    return bytes(b)


def main() -> int:
    # The only tree this script deletes is its fixed child of this checkout's build directory.
    if SCRATCH.resolve().parent != (ROOT / "build").resolve():
        raise RuntimeError("scratch directory escaped the build directory")
    shutil.rmtree(SCRATCH, ignore_errors=True)
    levels = SCRATCH / "data" / "Levels" / "FinalAlbion"
    levels.mkdir(parents=True)
    defs = SCRATCH / "data" / "CompiledDefs"
    defs.mkdir(parents=True)
    (defs / "game.bin").write_bytes(b"x")
    lev = levels / "RestoreTest.lev"
    (levels / "RestoreTest.lev.forge-orig").write_bytes(level(4))
    (levels / "RestoreTest.tng").write_bytes(b"Version 2;\r\n")
    source = ROOT / "tests" / "ui" / "setup_restore.txt"
    try:
        for size, scale, extra in (("1280x720", None, False),
                                   ("1366x768", "1.5", False),
                                   ("800x600", "1.5", True)):
            lev.write_bytes(level(9))
            if extra:
                # Fill the backup list so the modal must scroll on a small display.
                for i in range(12):
                    file = levels / f"extra_{i:02}.lev"
                    file.write_bytes(b"changed")
                    Path(str(file) + ".forge-orig").write_bytes(b"original")
            script = source
            if scale:
                tag = "small_s15" if extra else "s15"
                script = ROOT / "build" / f"setup_restore_{tag}.txt"
                body = source.read_text(encoding="utf-8").replace("wait_maps\n", "wait_maps\nset uiscale 1.5\n")
                body = body.replace("setup_restore_", f"setup_restore_{tag}_")
                if extra:
                    body = body.replace("assert_state backups_differ 1\n", "assert_state backups_differ 13\n")
                script.write_text(body, encoding="utf-8")
            run = subprocess.run([str(ROOT / "build" / "FableForge.exe"), "--install", str(SCRATCH),
                                  "--size", size, "--auto", str(script)], cwd=ROOT, timeout=120)
            log = Path(str(script) + ".log").read_text(encoding="utf-8", errors="replace")
            ok = run.returncode == 0 and "RESULT PASS" in log and lev.read_bytes() == level(4)
            if extra:
                ok = ok and all((levels / f"extra_{i:02}.lev").read_bytes() == b"original"
                                for i in range(12))
            print("setup restore UI", size, scale or "default scale", "OK" if ok else "FAILED")
            if not ok:
                print("\n".join(log.splitlines()[-25:]))
                return 1
        return 0
    finally:
        shutil.rmtree(SCRATCH, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
