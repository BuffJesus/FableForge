"""Read-only retail regression: missing STB background coverage uses LEV colour.

Requires Pillow and the installed OakVale_Sea_02 fixture. Diagnostic PNGs are
written into an owned build/world-background-* directory, never into the game install.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/forge.exe")
    parser.add_argument("--install", type=Path)
    args = parser.parse_args()
    output = Path(tempfile.mkdtemp(prefix="world-background-", dir=ROOT / "build")).resolve()
    assert output.parent == (ROOT / "build").resolve()
    print("evidence retained at", output, flush=True)
    name = "OakVale_Sea_02"
    command = [str(args.exe.resolve()), "ground", name]
    if args.install:
        command += ["--install", str(args.install.resolve())]
    subprocess.run(command, cwd=output, check=True, timeout=60)
    with Image.open(output / f"{name}_engine_bg.png") as source, \
         Image.open(output / f"{name}_our_bake.png") as baked, \
         Image.open(output / f"{name}_lev_bake.png") as fallback:
        assert source.size == baked.size == fallback.size
        source = source.convert("RGBA")
        baked = baked.convert("RGB")
        fallback = fallback.convert("RGB")
        missing = matches = erroneous_black = 0
        for bg, actual, lev in zip(source.getdata(), baked.getdata(), fallback.getdata()):
            if bg[3] == 0:
                missing += 1
                matches += actual == lev
                erroneous_black += actual == (0, 0, 0) and lev != (0, 0, 0)
        assert missing > 1000, "Fixture did not exercise partial background coverage"
        # Foreground interpolation can extend slightly beyond the background;
        # the rest must match the existing LEV-theme fallback, not black pixels.
        assert matches / missing > .9, (matches, missing)
        assert erroneous_black == 0, erroneous_black
        print(f"PASS: {matches}/{missing} uncovered pixels match LEV colour; no false black pixels")


if __name__ == "__main__":
    main()
