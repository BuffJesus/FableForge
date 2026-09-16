#!/usr/bin/env python3
"""Attach to the running Fable.exe with dbgeng (via pybag) and report the first
exception: address, registers, a short backtrace and the faulting bytes. Used by
the in-game harness to turn "the game exited" into a root cause.

  python tools/ingame/crash_catcher.py [--seconds 240] [--out crash.json]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time

os.environ["WINDBG_DIR"] = r"C:\Windows\System32"   # dbgeng/dbghelp shipped with Windows
os.environ["_NT_SYMBOL_PATH"] = ""                  # no symbol-server round trips
sys.stdout.reconfigure(line_buffering=True)

from pybag import UserDbg  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=240)
    ap.add_argument("--out", default="crash.json")
    a = ap.parse_args()

    dbg = UserDbg()
    pids = dbg.pids_by_name("Fable.exe")
    if not pids:
        print("no Fable.exe"); return 2
    pid = pids[0][0]
    dbg.attach(pid)
    dbg._client._cli.AddProcessOptions(0x00000001)   # DEBUG_PROCESS_DETACH_ON_EXIT: a dead catcher never kills the game
    print(f"attached to {pid}")
    deadline = time.time() + a.seconds
    report = {"pid": pid, "exception": None}
    try:
        while time.time() < deadline:
            try:
                dbg.go(timeout=5000)
            except Exception as e:  # timeout / no event
                msg = str(e)
                if "timeout" in msg.lower() or "TIMEOUT" in msg:
                    continue
                print("go:", msg)
            # did we stop on an exception?
            try:
                ev = dbg._control.GetLastEventInformation()
            except Exception:
                ev = None
            try:
                status = dbg._control.GetExecutionStatus()
            except Exception:
                status = None
            if status is not None and status != 1:   # 1 = DEBUG_STATUS_GO
                regs = {}
                for name in ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp", "eip"):
                    try:
                        regs[name] = hex(getattr(dbg.reg, name))
                    except Exception as e:
                        regs[name] = f"error: {e}"
                frames = []
                try:
                    for f in dbg.backtrace_list()[:16]:
                        frames.append(hex(f.InstructionOffset))
                except Exception as e:
                    frames.append(f"error: {e}")
                eip = None
                try:
                    eip = dbg.reg.eip
                except Exception:
                    pass
                # symbol-less stack scan: every dword on the stack that points into
                # Fable.exe's code (0x401000..0x1200000) right after a call is a likely return address
                stack = []
                try:
                    esp = dbg.reg.esp
                    raw = dbg.read(esp, 0x800)
                    import struct
                    for i in range(0, len(raw) - 3, 4):
                        v = struct.unpack_from("<I", raw, i)[0]
                        if 0x401000 <= v < 0x1200000:
                            try:
                                prev = dbg.read(v - 5, 5)
                                is_call = prev[0] == 0xE8 or prev[3] == 0xFF or prev[2] == 0xFF
                            except Exception:
                                is_call = False
                            if is_call:
                                stack.append({"esp+%x" % i: hex(v)})
                except Exception as e:
                    stack.append({"error": str(e)})
                code = None
                try:
                    if eip is not None:
                        code = dbg.read(eip, 16).hex()
                except Exception:
                    pass
                report["exception"] = {"status": status, "event": str(ev), "eip": hex(eip) if eip is not None else None,
                                       "regs": regs, "frames": frames, "code": code, "stack_returns": stack[:40]}
                print(json.dumps(report["exception"], indent=2))
                break
    finally:
        try:
            dbg.detach()
        except Exception:
            pass
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    return 0 if report["exception"] else 1


if __name__ == "__main__":
    sys.exit(main())
