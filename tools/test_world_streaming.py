"""Repeat read-only world/detail/water UI checks, including native process exits.

Requires the installed-data fixtures used by tests/ui/world_{view,water}.txt.
Use --exe to compare a saved baseline executable with the current build.
"""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/FableForge.exe")
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--script", type=Path, nargs="+", help="Cycle these read-only automation scripts instead of the default pair")
    parser.add_argument("--frame-ms", type=int, default=4, choices=range(4, 101))
    parser.add_argument("--show", action="store_true", help="Show editor windows (hidden by default)")
    parser.add_argument("--size", help="Fixed outer window size, e.g. 1280x800, for pixel comparisons")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    startup = None
    if os.name == "nt" and not args.show:
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    for i in range(args.runs):
        script = args.script[i % len(args.script)].resolve() if args.script else ROOT / "tests/ui" / ("world_view.txt" if i % 2 == 0 else "world_water.txt")
        run = subprocess.run([str(args.exe.resolve()), "--auto", str(script), "--auto-frame-ms", str(args.frame_ms)] +
                             (["--size", args.size] if args.size else []), cwd=ROOT,
                             timeout=120, startupinfo=startup, creationflags=getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0))
        log_path = Path(str(script) + ".log")
        log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
        passed = run.returncode == 0 and "RESULT PASS" in log
        print(f"{i + 1}/{args.runs} {script.name}: {'PASS' if passed else 'FAIL'} (exit {run.returncode})", flush=True)
        if not passed:
            print(log[-2000:])
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
