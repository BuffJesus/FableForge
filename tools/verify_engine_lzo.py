#!/usr/bin/env python3
"""Decode every retail STB frame re-encoded by our LZO1X encoder with the RETAIL ENGINE's own
decoder (lzo1x_decompress_asm_fast @0x00C069D0 in Fable.exe, run under Unicorn) and compare
with the original bytes. minilzo accepting a stream is not enough: the i386 assembly decoder
disagrees with the C one on tokens right after an initial 1..3-byte literal run.
Needs: the retail install, build/albionatlas_lzo_tests.exe, `pip install unicorn`."""
import os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "ingame"))
try:
    from retail_lzo_emu import decompress
except Exception as e:
    print("skip: unicorn / Fable.exe unavailable:", e); sys.exit(0)
d = Path(tempfile.mkdtemp(prefix="atlas_lzo_"))
env = dict(os.environ, ALBION_LZO_DUMP=str(d))
subprocess.run([str(ROOT / "build" / "albionatlas_lzo_tests.exe")], env=env, capture_output=True)
n = ok = 0; bad = []
for f in sorted(d.iterdir()):
    if f.suffix != ".raw": continue
    raw = f.read_bytes(); s = (d / (f.stem + ".ours.lzo")).read_bytes()
    rc, out, _ = decompress(s, len(raw), asm=True)
    n += 1
    if rc == 0 and out[:len(raw)] == raw: ok += 1
    else: bad.append(f.stem)
for f in d.iterdir(): f.unlink()
d.rmdir()
print(f"engine asm decoder: {ok}/{n} frames decode byte-exact" + (f"; FAILED {bad[:5]}" if bad else ""))
sys.exit(0 if ok == n and n > 0 else 1)
