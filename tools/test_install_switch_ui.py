#!/usr/bin/env python3
"""Check install switching and stale editor state using disposable loose levels."""
from pathlib import Path
import json
import shutil
import subprocess
import sys

from test_setup_restore_ui import level

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = ROOT / "build" / "ui_install_switch_root"


def make_install(path: Path, name: str, height: float) -> None:
    levels = path / "data" / "Levels" / "FinalAlbion"
    levels.mkdir(parents=True)
    defs = path / "data" / "CompiledDefs"
    defs.mkdir(parents=True)
    (defs / "game.bin").write_bytes(b"x")
    (levels / f"{name}.lev").write_bytes(level(height))
    (levels / f"{name}.tng").write_bytes(b"Version 2;\r\n")


def main() -> int:
    if SCRATCH.resolve().parent != (ROOT / "build").resolve():
        raise RuntimeError("scratch directory escaped the build directory")
    shutil.rmtree(SCRATCH, ignore_errors=True)
    first, second = SCRATCH / "first", SCRATCH / "second"
    try:
        make_install(first, "SwitchA", 4)
        make_install(second, "SwitchB", 7)
        for install, names in ((first, ("FirstPack",)), (second, ("SecondPack", "ExtraPack"))):
            mods = []
            for name in names:
                pack = install / "FableForgeMods" / name
                pack.mkdir(parents=True)
                (pack / "forge_pack.json").write_text(json.dumps({"name": name}), encoding="utf-8")
                mods.append({"name": name, "kind": "forge", "source": str(pack)})
            (install / "forge_mods.json").write_text(json.dumps({"mods": mods}), encoding="utf-8")
            (install / "forge_mods_picks.txt").write_text(
                "".join(f"defs:{name}\t{name}\n" for name in names), encoding="utf-8")
        original = {p: p.read_bytes() for p in SCRATCH.rglob("*") if p.is_file()}
        script = ROOT / "build" / "install_switch.txt"
        body = (ROOT / "tests" / "ui" / "install_switch.txt").read_text(encoding="utf-8")
        body = body.replace("@ROOT_B@", str(second)).replace("@INVALID@", str(SCRATCH / "missing"))
        body = (body.replace("@ROOT_A@", str(first))
                .replace("@PACK_A@", (first / "FableForgeMods" / "FirstPack").as_posix())
                .replace("@PACK_B@", (second / "FableForgeMods" / "SecondPack").as_posix()))
        script.write_text(body, encoding="utf-8")
        run = subprocess.run([str(ROOT / "build" / "FableForge.exe"), "--install", str(first),
                              "--size", "800x600", "--auto", str(script)], cwd=ROOT, timeout=120)
        log = Path(str(script) + ".log").read_text(encoding="utf-8", errors="replace")
        okay = run.returncode == 0 and "RESULT PASS" in log
        okay = okay and (first / "data" / "Levels" / "FinalAlbion" / "SwitchA.lev").read_bytes() == level(4)
        okay = okay and {p: p.read_bytes() for p in SCRATCH.rglob("*") if p.is_file()} == original
        print("install switch UI", "OK" if okay else "FAILED")
        if not okay:
            print("\n".join(log.splitlines()[-35:]))
        return 0 if okay else 1
    finally:
        shutil.rmtree(SCRATCH, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
