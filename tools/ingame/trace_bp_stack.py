#!/usr/bin/env python3
"""Break once on an address in Fable.exe and dump a symbol-less stack scan (return
addresses into the module) plus args. python trace_bp_stack.py --addr 0xbdc160 [--hits 3]"""
from __future__ import annotations
import argparse, json, os, struct, sys, time
os.environ["WINDBG_DIR"] = r"C:\Windows\System32"; os.environ["_NT_SYMBOL_PATH"] = ""
sys.stdout.reconfigure(line_buffering=True)
from pybag import UserDbg

def main() -> int:
    ap = argparse.ArgumentParser(); ap.add_argument("--addr", required=True); ap.add_argument("--hits", type=int, default=3)
    ap.add_argument("--seconds", type=int, default=240); ap.add_argument("--out", default="bp_stack.json")
    a = ap.parse_args(); addr = int(a.addr, 16)
    dbg = UserDbg(); pids = dbg.pids_by_name("Fable.exe")
    if not pids: print("no Fable.exe"); return 2
    dbg.attach(pids[0][0]); dbg._client._cli.AddProcessOptions(0x00000001); dbg.bp(addr)
    print("attached; bp", hex(addr))
    hits = []; deadline = time.time() + a.seconds
    while time.time() < deadline and len(hits) < a.hits:
        try: dbg.go(timeout=3000)
        except Exception as e:
            if "timeout" in str(e).lower(): continue
            print("go:", e); break
        try: status = dbg._control.GetExecutionStatus()
        except Exception: status = None
        if status == 1: continue
        eip = dbg.reg.eip
        if eip != addr: print("stopped elsewhere", hex(eip)); break
        esp = dbg.reg.esp; raw = dbg.read(esp, 0x600)
        args = list(struct.unpack_from("<8I", raw, 0))
        rets = []
        for i in range(0, len(raw) - 3, 4):
            v = struct.unpack_from("<I", raw, i)[0]
            if 0x401000 <= v < 0x1200000:
                try:
                    prev = dbg.read(v - 5, 5)
                    if prev[0] == 0xE8 or prev[3] == 0xFF or prev[2] == 0xFF: rets.append((i, v))
                except Exception: pass
        rec = {"eip": hex(eip), "ecx": hex(dbg.reg.ecx), "stack_top": [hex(v) for v in args], "returns": [{"esp+%x" % i: hex(v)} for i, v in rets[:30]]}
        print(json.dumps(rec)); hits.append(rec)
    json.dump(hits, open(a.out, "w"), indent=2)
    try: dbg.detach()
    except Exception: pass
    return 0

if __name__ == "__main__":
    sys.exit(main())
