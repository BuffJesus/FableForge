#!/usr/bin/env python3
"""A full ground palette must refuse custom-theme creation before reaching bank I/O."""
from pathlib import Path
import shutil
import subprocess
import sys

from test_install_switch_ui import make_install
from test_setup_restore_ui import level

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = ROOT / "build" / "ui_theme_palette_root"


def main():
    if SCRATCH.resolve().parent != (ROOT / "build").resolve():
        raise RuntimeError("unexpected palette scratch root")
    shutil.rmtree(SCRATCH, ignore_errors=True)
    try:
        make_install(SCRATCH, "FullPalette", 4)
        data = bytearray(level(4))
        for i in range(256):
            name = f"GROUND_SLOT_{i}".encode()
            offset = 47 + i * 132
            data[offset:offset + len(name)] = name
        (SCRATCH / "data/Levels/FinalAlbion/FullPalette.lev").write_bytes(data)
        before = {p: p.read_bytes() for p in SCRATCH.rglob("*") if p.is_file()}
        script = ROOT / "build" / "custom_theme_full_palette.txt"
        script.write_text("""wait_maps
wait_ready
select FullPalette
wait_loaded
assert_state palette_named 256
assert_state terrain_dirty 0
custom_theme_refused nonexistent.png GROUND_NEW
assert_log the map's ground-theme palette is full
assert_state terrain_dirty 0
assert_state palette_named 256
quit
""", encoding="utf-8")
        run = subprocess.run([str(ROOT / "build/FableForge.exe"), "--install", str(SCRATCH),
                              "--auto", str(script)], cwd=ROOT, timeout=90)
        log = Path(str(script) + ".log").read_text(encoding="utf-8", errors="replace")
        okay = run.returncode == 0 and "RESULT PASS" in log and before == {
            p: p.read_bytes() for p in SCRATCH.rglob("*") if p.is_file()}
        print("custom theme full-palette UI", "OK" if okay else "FAILED")
        if not okay:
            print("\n".join(log.splitlines()[-20:]))
        return 0 if okay else 1
    finally:
        shutil.rmtree(SCRATCH, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
