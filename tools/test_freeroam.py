#!/usr/bin/env python3
"""Verify that the local Freeroam FMP's LEVs survive a scratch WAD build."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

REPO = Path(__file__).resolve().parent.parent
RETAIL = Path(r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters")
FMP = REPO / "work/nexus_mods/_peek/mfvicli/Mods/Other/Freeroam.fmp"
TOOL = REPO / "build/forge-tools.exe"


def run(*args):
    proc = subprocess.run([str(TOOL), *map(str, args)], cwd=REPO,
                          capture_output=True, text=True)
    if proc.returncode:
        raise AssertionError(f"{args}\n{proc.stdout[-1200:]}\n{proc.stderr[-1200:]}")
    return proc.stdout


def main():
    needed = [FMP, TOOL, RETAIL / "data/Levels/FinalAlbion.wad",
              RETAIL / "data/CompiledDefs/game.bin"]
    if missing := [p for p in needed if not p.is_file()]:
        print("Freeroam corpus or retail install missing; skipped:", *missing, sep="\n  ")
        return
    with tempfile.TemporaryDirectory(prefix="freeroam_", dir=REPO / "build") as tmp:
        work = Path(tmp)
        root = work / "retail"
        defs = root / "data/CompiledDefs"
        defs.mkdir(parents=True)
        for name in ("game.bin", "names.bin", "script.bin", "frontend.bin"):
            src = RETAIL / "data/CompiledDefs" / name
            if src.is_file():
                shutil.copyfile(src, defs / name)
        levels = root / "data/Levels"
        levels.mkdir(parents=True)
        shutil.copyfile(RETAIL / "data/Levels/FinalAlbion.wad", levels / "FinalAlbion.wad")
        run("mods", "add", root, FMP, "--name", "Freeroam")
        out = work / "built"
        report = run("mods", "build", root, out)
        assert "WAD levels: 76" in report, report[-2000:]
        extracted = work / "wad"
        run("wad", "extract", out / "data/Levels/FinalAlbion.wad", extracted)
        entries = json.loads(run("fmp", "list", FMP, "--json"))["banks"][0]["entries"]
        assert len(entries) == 76
        for entry in entries:
            rel = Path(entry["name"].replace("\\", "/"))
            packed = extracted / rel
            loose = out / "data/Levels" / Path(*rel.parts[2:])
            assert packed.read_bytes() == loose.read_bytes(), entry["name"]
            info = run("lev", "info", packed)
            counts = re.search(r"walkable: (\d+) of (\d+) cells", info)
            assert counts and counts.group(1) == counts.group(2), entry["name"]
        sample = extracted / "Data/Levels/FinalAlbion/Witchwood_9.lev"
        info = run("lev", "info", sample)
        assert re.search(r"walkable: 4225 of 4225 cells", info), info
        print("Freeroam.fmp: all 76 LEVs fully walkable and match rebuilt WAD")


if __name__ == "__main__":
    main()
