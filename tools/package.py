#!/usr/bin/env python3
"""Build a release zip: dist/FableForge-<version>-win64.zip with both exes,
README, LICENSE and third-party notices. Runs check_all first unless --no-check.

  python tools/package.py [--version 0.1.0] [--no-check] [--guide-only] [--output-dir dist]
"""
import argparse, os, re, shutil, subprocess, sys, zipfile
from pathlib import Path

def package_guide(root, output=None):
    guide_dir = root / "docs/walkthrough/aeon-controller"
    # A development ZIP name need not have a GitHub tag. Keep the standalone
    # README aligned with the tested application download in the walkthrough.
    guide = (guide_dir / "index.html").read_text(encoding="utf-8")
    link = re.search(r'https://github\.com/BuffJesus/FableForge/releases/tag/[^"\s]+', guide)
    download = link.group(0) if link else "https://github.com/BuffJesus/FableForge/releases"
    output = output if output is not None else root / "dist"
    output.mkdir(parents=True, exist_ok=True)
    guide_zip = output / "FableForge-Aeon-Controller-Guide.zip"
    with zipfile.ZipFile(guide_zip, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(guide_dir / "index.html", "AEON_CONTROLLER.html")
        for shot in ("01-order.png", "02-add.png", "03-check.png", "04-deploy.png"):
            archive.write(guide_dir / shot, shot)
        archive.writestr("README.txt",
            "Extract this ZIP, then open AEON_CONTROLLER.html in your browser.\n\n"
            "Download FableForge separately from:\n"
            f"{download}\n\n"
            "This ZIP contains only the illustrated guide, not the application or mods.\n")
    print(f"wrote {guide_zip.name} ({guide_zip.stat().st_size/1e6:.2f} MB; guide only)")


def main():
    root = Path(__file__).resolve().parents[1]
    os.chdir(root)
    cmake = (root / "CMakeLists.txt").read_text()
    version = re.search(r'project\(FableForge VERSION ([0-9.]+)', cmake).group(1)
    version += re.search(r'set\(FORGE_VERSION_SUFFIX "([^"]*)"\)', cmake).group(1)
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default=version)
    ap.add_argument("--no-check", action="store_true")
    ap.add_argument("--guide-only", action="store_true", help="Package only the illustrated Aeon/controller guide")
    ap.add_argument("--output-dir", type=Path, default=root / "dist", help="Folder for ZIPs and staging; use a separate folder for development builds")
    a = ap.parse_args()
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.]+)?', a.version):
        ap.error("version must be a semantic version, optionally with a prerelease suffix")
    output = a.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if a.guide_only:
        package_guide(root, output)
        return 0
    if not a.no_check and subprocess.run([sys.executable, "tools/check_all.py"]).returncode != 0:
        print("checks failed; not packaging"); return 1
    name = f"FableForge-{a.version}-win64"
    stage = output / name
    if stage.resolve().parent != output:
        raise RuntimeError("Package staging directory escaped output directory")
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
    readme = readme.replace("vendor/EgoCore-LICENSE.txt", "EgoCore-LICENSE.txt")
    readme = readme.replace("vendor/VENDORED.md", "THIRD_PARTY.md")
    readme += "\nIllustrated Aeon Edition + Controller Support setup: open [AEON_CONTROLLER.html](AEON_CONTROLLER.html) in your browser.\n"
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
    notices = (root / "vendor/VENDORED.md").read_text(encoding="utf-8")
    notices = notices.replace("../docs/PROFILING.md", "docs/PROFILING.md")
    (Path(stage) / "THIRD_PARTY.md").write_text(notices, encoding="utf-8")
    shutil.copy(os.path.join("vendor", "EgoCore-LICENSE.txt"), stage)
    shutil.copy(os.path.join("docs", "AUTOMATION.md"), stage)
    shutil.copy(os.path.join("docs", "EDITOR.md"), stage)
    shutil.copy(os.path.join("docs", "FIRST_LEVEL.md"), stage)
    shutil.copy(os.path.join("docs", "ENGINE_RULES.md"), stage)
    subprocess.run([sys.executable, "tools/gen_cli_reference.py"], check=False)
    shutil.copy(os.path.join("docs", "CLI.md"), stage)
    shutil.copytree(os.path.join("docs", "walkthrough"), os.path.join(stage, "walkthrough"))
    guide = (root / "docs/walkthrough/aeon-controller/index.html").read_text(encoding="utf-8")
    for shot in ("01-order.png", "02-add.png", "03-check.png", "04-deploy.png"):
        guide = guide.replace('"' + shot + '"', '"walkthrough/aeon-controller/' + shot + '"')
    (Path(stage) / "AEON_CONTROLLER.html").write_text(guide, encoding="utf-8")
    shutil.copytree("presets", os.path.join(stage, "presets"))
    shutil.copytree(os.path.join("docs", "re_reference"), os.path.join(stage, "docs", "re_reference"))   # forge-tools reads def_schema.json etc.
    shutil.copytree(os.path.join("docs", "modding"), os.path.join(stage, "docs", "modding"))   # the mod-pack / .fmp / load-order design the forge-tools mods family implements
    shutil.copytree(os.path.join("docs", "releases"), os.path.join(stage, "docs", "releases"))
    shutil.copy(os.path.join("docs", "FEATURE_GALLERY.md"), os.path.join(stage, "docs", "FEATURE_GALLERY.md"))
    nested_editor = (root / "docs/EDITOR.md").read_text(encoding="utf-8")
    nested_editor = nested_editor.replace("](walkthrough/", "](../walkthrough/")
    (Path(stage) / "docs/EDITOR.md").write_text(nested_editor, encoding="utf-8")
    # The profiling notice links these local guides; preserve their relative links.
    for doc in ("PROFILING.md", "WORLD_PERFORMANCE.md", "WORLD_RENDERING_RESEARCH.md",
                "HANDOFF_WORLD_UI.md", "ARENA_HALL_CONTENT_AUDIT.md", "EGOCORE_PARTICLES_20260929.md", "AUTOMATION.md"):
        shutil.copy(root / "docs" / doc, Path(stage) / "docs" / doc)
    shutil.copytree(os.path.join("docs", "screenshots"), os.path.join(stage, "docs", "screenshots"))
    for screenshot in (root / "docs").glob("screenshot_*.png"):
        shutil.copy(screenshot, os.path.join(stage, "docs", screenshot.name))
    zpath = output / (name + ".zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for folder, _, files in os.walk(stage):
            for f in files:
                p = os.path.join(folder, f)
                z.write(p, os.path.join(name, os.path.relpath(p, stage)))
    print(f"wrote {zpath} ({os.path.getsize(zpath)/1e6:.1f} MB)")
    for f in sorted(os.listdir(stage)): print(f"  {f:28s} {os.path.getsize(os.path.join(stage, f))/1e6:6.2f} MB")
    package_guide(root, output)
    return 0

if __name__ == "__main__":
    sys.exit(main())
