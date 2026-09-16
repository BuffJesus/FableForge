#!/usr/bin/env python3
"""Attach to Fable.exe and log every lzo1x_decompress(in, in_len, out, out_len*, wrk) call
(args + return address) until --seconds elapse. Output: JSON lines to --out."""
from __future__ import annotations
import argparse, json, os, struct, sys, time
os.environ["WINDBG_DIR"] = r"C:\Windows\System32"; os.environ["_NT_SYMBOL_PATH"] = ""
sys.stdout.reconfigure(line_buffering=True)
from pybag import UserDbg

LZO = 0x00C06B90
LZO_SAFE = 0x00C08170

def main() -> int:
    ap = argparse.ArgumentParser(); ap.add_argument("--seconds", type=int, default=180); ap.add_argument("--out", default="lzo_calls.jsonl")
    ap.add_argument("--max", type=int, default=5000)
    a = ap.parse_args()
    dbg = UserDbg()
    pids = dbg.pids_by_name("Fable.exe")
    if not pids: print("no Fable.exe"); return 2
    dbg.attach(pids[0][0]); dbg._client._cli.AddProcessOptions(0x00000001)
    dbg.bp(LZO); dbg.bp(LZO_SAFE)
    print("attached, breakpoint set")
    n = 0; deadline = time.time() + a.seconds
    with open(a.out, "w") as f:
        while time.time() < deadline and n < a.max:
            try:
                dbg.go(timeout=3000)
            except Exception as e:
                if "timeout" in str(e).lower(): continue
                print("go:", e); break
            try:
                status = dbg._control.GetExecutionStatus()
            except Exception:
                status = None
            if status == 1: continue
            try:
                eip = dbg.reg.eip
            except Exception:
                break
            if eip not in (LZO, LZO_SAFE):
                print("stopped elsewhere", hex(eip)); break
            esp = dbg.reg.esp
            raw = dbg.read(esp, 24)
            ret, pin, in_len, pout, poutlen, wrk = struct.unpack("<6I", raw)
            hdr = dbg.read(pin - 8, 8) if pin >= 8 else b""
            unc, comp = struct.unpack("<II", hdr) if len(hdr) == 8 else (None, None)
            outlen = struct.unpack("<I", dbg.read(poutlen, 4))[0]
            rec = {"fn": "safe" if eip == LZO_SAFE else "plain", "ret": hex(ret), "in": hex(pin), "in_len": in_len, "out": hex(pout), "out_len": outlen, "hdr_unc": unc, "hdr_comp": comp}
            f.write(json.dumps(rec) + "\n"); f.flush(); n += 1
    print("calls", n)
    try: dbg.detach()
    except Exception: pass
    return 0

if __name__ == "__main__":
    sys.exit(main())
