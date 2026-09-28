"""Run the UI tour (tests/ui/scale_tour.template) at several window sizes and UI scales and
collect the screenshots under build/ui/scale/<size>_s<scale>_NN_<screen>.png for a layout review.

  python tools/ui_scale_sweep.py                 # the default matrix
  python tools/ui_scale_sweep.py 1366x768:1.0    # one or more size:scale pairs

The effective UI scale is monitor DPI x a window-height factor (0.85 .. 1.25) x the UI-scale
setting (0.8 .. 1.5), so the matrix spans small laptops to 4K and both ends of the setting.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT = ["1280x720:1.0", "1366x768:0.8", "1366x768:1.5", "1920x1080:1.0", "1920x1080:1.5",
           "2560x1440:1.0", "2560x1440:0.8", "3840x2160:1.0"]


def main():
    pairs = sys.argv[1:] or DEFAULT
    template = open(os.path.join(ROOT, "tests", "ui", "scale_tour.template"), encoding="utf-8").read()
    os.makedirs(os.path.join(ROOT, "build", "ui", "scale"), exist_ok=True)
    gui = os.path.join(ROOT, "build", "FableForge.exe")
    ok = True
    for pair in pairs:
        size, scale = pair.split(":")
        tag = f"{size}_s{scale.replace('.', '')}"
        script = os.path.join(ROOT, "build", f"scale_tour_{tag}.txt")
        with open(script, "w", encoding="utf-8", newline="\n") as f:
            f.write(template.replace("@TAG@", tag).replace("@SCALE@", scale))
        subprocess.run([gui, "--size", size, "--auto", script], cwd=ROOT, timeout=900)
        log = open(script + ".log", encoding="utf-8", errors="replace").read()
        result = "PASS" if "RESULT PASS" in log else "FAIL"
        ok &= result == "PASS"
        print(f"{tag}: {result}")
        if result != "PASS":
            for line in log.splitlines():
                if "FAIL" in line: print("   ", line.strip())
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
