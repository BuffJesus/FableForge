#!/usr/bin/env python3
"""Build a release zip: dist/AlbionAtlas-<version>-win64.zip with both exes,
README, LICENSE and third-party notices. Runs check_all first unless --no-check.

  python tools/package.py [--version 0.1.0] [--no-check]
"""
import argparse, os, shutil, subprocess, sys, zipfile

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="0.8.0")
    ap.add_argument("--no-check", action="store_true")
    a = ap.parse_args()
    if not a.no_check and subprocess.run([sys.executable, "tools/check_all.py"]).returncode != 0:
        print("checks failed; not packaging"); return 1
    name = f"AlbionAtlas-{a.version}-win64"
    stage = os.path.join("dist", name)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    for f in ["AlbionAtlasGUI.exe", "AlbionAtlas.exe"]:
        shutil.copy(os.path.join("build", f), stage)
        subprocess.run(["strip", os.path.join(stage, f)], check=False)
    shutil.copy("README.md", stage)
    shutil.copy("LICENSE", stage)
    shutil.copy(os.path.join("vendor", "VENDORED.md"), os.path.join(stage, "THIRD_PARTY.md"))
    shutil.copy(os.path.join("docs", "AUTOMATION.md"), stage)
    shutil.copy(os.path.join("docs", "EDITOR.md"), stage)
    zpath = os.path.join("dist", name + ".zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(stage):
            for f in files:
                p = os.path.join(root, f)
                z.write(p, os.path.join(name, os.path.relpath(p, stage)))
    print(f"wrote {zpath} ({os.path.getsize(zpath)/1e6:.1f} MB)")
    for f in sorted(os.listdir(stage)): print(f"  {f:28s} {os.path.getsize(os.path.join(stage, f))/1e6:6.2f} MB")
    return 0

if __name__ == "__main__":
    sys.exit(main())
