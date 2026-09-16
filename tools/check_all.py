#!/usr/bin/env python3
"""Run every check: build, unit tests, retail CLI smoke, GUI suites.

  python tools/check_all.py [--no-build] [--count 8]
"""
import argparse, os, subprocess, sys, time

def run(name, cmd, **kw):
    t0 = time.time()
    r = subprocess.run(cmd, **kw)
    print(f"[{'PASS' if r.returncode == 0 else 'FAIL'}] {name} ({time.time() - t0:.1f}s)")
    return r.returncode == 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--count", type=int, default=8)
    a = ap.parse_args()
    ok = True
    if not a.no_build:
        ok &= run("build", ["cmake", "--build", "build"], capture_output=True)
    ok &= run("unit tests", [os.path.join("build", "albionatlas_tests.exe")], capture_output=True)
    ok &= run("lzo1x vs minilzo", [os.path.join("build", "albionatlas_lzo_tests.exe")], capture_output=True)
    ok &= run(f"retail smoke ({a.count} maps)", [sys.executable, "tools/retail_smoke.py", "--count", str(a.count)], capture_output=True)
    ok &= run("ui smoke", [sys.executable, "tools/ui_smoke.py"], capture_output=True)
    gui = os.path.join("build", "AlbionAtlasGUI.exe")
    ok &= run("ui paths", [gui, "--auto", "tests/ui/paths.txt"])
    ok &= run("ui controls", [gui, "--auto", "tests/ui/controls.txt"])
    ok &= run("ui foliage", [gui, "--auto", "tests/ui/foliage.txt"])
    ok &= run("ui region export", [gui, "--auto", "tests/ui/region.txt"])
    ok &= run("ui no-install", [gui, "--auto", "tests/ui/noinstall.txt", "--install", "D:/definitely/not/fable"])
    print("ALL PASS" if ok else "SOME CHECKS FAILED")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
