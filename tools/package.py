#!/usr/bin/env python3
"""Build a release zip: dist/FableForge-<version>-win64.zip with both exes,
README, LICENSE and third-party notices. Runs check_all first unless --no-check.

  python tools/package.py [--version 0.1.0] [--no-check]
"""
import argparse, os, shutil, subprocess, sys, zipfile

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="0.16.0")
    ap.add_argument("--no-check", action="store_true")
    a = ap.parse_args()
    if not a.no_check and subprocess.run([sys.executable, "tools/check_all.py"]).returncode != 0:
        print("checks failed; not packaging"); return 1
    name = f"FableForge-{a.version}-win64"
    stage = os.path.join("dist", name)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    for f in ["FableForge.exe", "forge.exe", "forge-tools.exe"]:
        shutil.copy(os.path.join("build", f), stage)
        subprocess.run(["strip", os.path.join(stage, f)], check=False)
    # the user docs sit flat next to README in the zip: the repo's docs/X.md links become X.md
    with open("README.md", encoding="utf-8") as f:
        readme = f.read()
    for doc in ["FIRST_LEVEL.md", "ENGINE_RULES.md", "EDITOR.md", "AUTOMATION.md", "CLI.md"]:
        readme = readme.replace("docs/" + doc, doc)
    with open(os.path.join(stage, "README.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write(readme)
    shutil.copy("LICENSE", stage)
    # defc (jamen/fable-defs, Zlib): the def compiler the EgoCore pack type needs for .def text mods.
    # Shipped when a build is at hand; the retail text Data/Defs tree is the user's (EgoCore users have it).
    defc = os.environ.get("FORGE_DEFC") or r"C:\Users\Cornelio\Documents\EgoCoreInspect\fable-defs\target\release\defc.exe"
    if os.path.exists(defc):
        shutil.copy(defc, os.path.join(stage, "defc.exe"))
        print("  defc.exe from", defc)
    else:
        print("  (no defc.exe at hand: the EgoCore .def text path will need FORGE_DEFC on the user's machine)")
    shutil.copy(os.path.join("vendor", "VENDORED.md"), os.path.join(stage, "THIRD_PARTY.md"))
    shutil.copy(os.path.join("docs", "AUTOMATION.md"), stage)
    shutil.copy(os.path.join("docs", "EDITOR.md"), stage)
    shutil.copy(os.path.join("docs", "FIRST_LEVEL.md"), stage)
    shutil.copy(os.path.join("docs", "ENGINE_RULES.md"), stage)
    subprocess.run([sys.executable, "tools/gen_cli_reference.py"], check=False)
    shutil.copy(os.path.join("docs", "CLI.md"), stage)
    shutil.copytree(os.path.join("docs", "walkthrough"), os.path.join(stage, "walkthrough"))
    shutil.copytree("presets", os.path.join(stage, "presets"))
    shutil.copytree(os.path.join("docs", "re_reference"), os.path.join(stage, "docs", "re_reference"))   # forge-tools reads def_schema.json etc.
    shutil.copytree(os.path.join("docs", "modding"), os.path.join(stage, "docs", "modding"))   # the mod-pack / .fmp / load-order design the forge-tools mods family implements
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
