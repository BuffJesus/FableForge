#!/usr/bin/env python3
"""Build Dragon Cliff Restored over a scratch retail WAD and verify root levels.

Requires the local ZIP extracted under work/nexus_mods/_peek/DragonCliffRestored.
Never deploys to the game install.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "work/nexus_mods/_peek/DragonCliffRestored/DragonCliffRestoredV2/Fable The Lost Chapters"
RETAIL = Path(r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters")
TOOL = REPO / "build/forge-tools.exe"


def main() -> int:
    required = [TOOL, SOURCE / "data/CompiledDefs/game.bin",
                SOURCE / "data/Levels/DragonCliff.lev",
                SOURCE / "data/Levels/FinalAlbion_RT.stb",
                RETAIL / "data/CompiledDefs/game.bin",
                RETAIL / "data/Levels/FinalAlbion.wad",
                RETAIL / "data/graphics/graphics.big"]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        print("Dragon Cliff corpus or retail install missing; skipped:", *missing, sep="\n  ")
        return 0

    work = Path(tempfile.mkdtemp(prefix="dragoncliff_", dir=REPO / "build"))
    root, out = work / "retail", work / "merged"
    try:
        for relative in ("data/CompiledDefs/game.bin", "data/CompiledDefs/names.bin",
                         "data/CompiledDefs/script.bin", "data/CompiledDefs/frontend.bin",
                         "data/Levels/FinalAlbion.wad", "data/Levels/FinalAlbion.wld",
                         "data/Levels/FinalAlbion.bwd", "data/Levels/FinalAlbion.gtg",
                         "data/graphics/graphics.big"):
            src = RETAIL / relative
            if src.is_file():
                dest = root / relative
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(src, dest)

        def run(*argv):
            proc = subprocess.run([str(TOOL), *map(str, argv)], cwd=REPO,
                                  capture_output=True, text=True)
            if proc.returncode:
                raise AssertionError(f"{' '.join(map(str, argv))}\n{proc.stdout[-1500:]}\n{proc.stderr[-1500:]}")
            return proc.stdout

        run("mods", "add", root, SOURCE, "--name", "Dragon Cliff Restored")
        result = run("mods", "build", root, out)
        wad = out / "data/Levels/FinalAlbion.wad"
        assert wad.is_file()
        extracted = work / "extracted"
        run("wad", "extract", wad, extracted)

        root_levels = sorted(path for path in (SOURCE / "data/Levels").iterdir()
                             if path.is_file() and path.suffix.lower() in (".lev", ".tng"))
        assert len(root_levels) == 16, [path.name for path in root_levels]
        assert {path.stem for path in root_levels} == {
            "DragonCliff", *(f"DragonCliff_Filler_{index:02d}" for index in range(1, 8))}
        for src in root_levels:
            loose = out / "data/Levels" / src.name
            packed = extracted / "Data/Levels" / src.name
            assert loose.is_file() and packed.is_file(), src.name
            assert loose.read_bytes() == packed.read_bytes() == src.read_bytes(), src.name
        for ext in (".lev", ".tng"):
            loose = out / "data/Levels/FinalAlbion" / f"HookCoast{ext}"
            packed = extracted / "Data/Levels/FinalAlbion" / f"HookCoast{ext}"
            assert loose.is_file() and packed.is_file(), f"HookCoast{ext}"
            assert loose.read_bytes() == packed.read_bytes(), f"HookCoast{ext}"
        assert (out / "data/Levels/FinalAlbion.wld").is_file()
        assert (out / "data/Levels/FinalAlbion_RT.stb").is_file()
        assert not (out / "userst.ini").exists()
        summary = re.search(r"FinalAlbion\.wad rebuilt: (\d+) level file\(s\) repacked, (\d+) new level file\(s\) appended", result)
        assert summary and int(summary.group(2)) == 16, result[-1200:]
        meshes = json.loads(run("assets", "missing-mesh", out,
                                "docs/re_reference/def_schema.json", "--json"))
        assert not meshes["missing"], meshes["missing"][:12]
        print("Dragon Cliff Restored: PASS")
        print(f"  root levels: {len(root_levels)} bytes verified in rebuilt WAD")
        print(f"  WAD: {summary.group(1)} replaced, {summary.group(2)} appended")
        print(f"  direct missing mesh references: {len(meshes['missing'])}")
        return 0
    finally:
        assert work.resolve().is_relative_to((REPO / "build").resolve())
        shutil.rmtree(work)


if __name__ == "__main__":
    raise SystemExit(main())
