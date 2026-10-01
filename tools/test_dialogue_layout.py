#!/usr/bin/env python3
"""Check the lip sync head and timeline at two compact window sizes."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests" / "ui" / "dialogue_head_eyes.txt"


def main() -> int:
    body = SOURCE.read_text(encoding="utf-8")
    for size in ("1280x720", "1024x600"):
        tag = size.replace("x", "_")
        script = ROOT / "build" / f"dialogue_head_layout_{tag}.txt"
        script.write_text(body.replace("wait_ready\n", "wait_ready\nset uiscale 1.5\n")
                          .replace("build/ui/head_", f"build/ui/head_{tag}_"), encoding="utf-8")
        run = subprocess.run([str(ROOT / "build" / "FableForge.exe"), "--size", size,
                              "--auto", str(script)], cwd=ROOT, timeout=120)
        log = Path(str(script) + ".log").read_text(encoding="utf-8", errors="replace")
        ok = run.returncode == 0 and "RESULT PASS" in log
        print("dialogue layout", size, "OK" if ok else "FAILED")
        if not ok:
            print("\n".join(log.splitlines()[-20:]))
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
