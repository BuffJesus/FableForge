#!/usr/bin/env python3
"""Build Expanded Chapters plus its separate graphics archive over scratch retail."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "work/nexus_mods/_peek/ExpandedChapters/FTECv1/Fable The Lost Chapters"
RETAIL = Path(r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters")
TOOL = REPO / "build/forge-tools.exe"


def main() -> int:
    required = [TOOL, SOURCE / "data/CompiledDefs/game.bin",
                SOURCE / "data/Levels/FinalAlbion",
                SOURCE / "data/Levels/FinalAlbion_RT.stb",
                SOURCE / "data/graphics/graphics.big",
                SOURCE / "data/graphics/pc/textures.big",
                RETAIL / "data/CompiledDefs/game.bin",
                RETAIL / "data/Levels/FinalAlbion.wad",
                RETAIL / "data/graphics/graphics.big"]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        print("Expanded Chapters corpus or retail install missing; skipped:", *missing, sep="\n  ")
        return 0

    work = Path(tempfile.mkdtemp(prefix="expanded_chapters_", dir=REPO / "build"))
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

        run("mods", "add", root, SOURCE, "--name", "Expanded Chapters")
        result = run("mods", "build", root, out)
        wad = out / "data/Levels/FinalAlbion.wad"
        assert wad.is_file() and not (out / "userst.ini").exists()
        assert (out / "data/graphics/graphics.big").is_file()
        assert (out / "data/graphics/pc/textures.big").is_file()
        assert (out / "data/Levels/FinalAlbion.wld").is_file()
        assert (out / "data/Levels/FinalAlbion_RT.stb").is_file()
        extracted = work / "extracted"
        run("wad", "extract", wad, extracted)
        source_levels = [path for path in (SOURCE / "data/Levels").rglob("*")
                         if path.is_file() and path.suffix.lower() in (".lev", ".tng")]
        assert len(source_levels) == 798, len(source_levels)
        for src in source_levels:
            rel = src.relative_to(SOURCE / "data/Levels")
            loose = out / "data/Levels" / rel
            packed = extracted / "Data/Levels" / rel
            assert packed.is_file(), str(rel)
            expected = loose.read_bytes() if loose.is_file() else src.read_bytes()
            assert packed.read_bytes() == expected, f"WAD payload differs: {rel}"
        built_levels = [path for path in (out / "data/Levels").rglob("*")
                        if path.is_file() and path.suffix.lower() in (".lev", ".tng")]
        for loose in built_levels:
            rel = loose.relative_to(out / "data/Levels")
            packed = extracted / "Data/Levels" / rel
            assert packed.is_file() and packed.read_bytes() == loose.read_bytes(), str(rel)
        summary = re.search(r"FinalAlbion\.wad rebuilt: (\d+) level file\(s\) repacked, (\d+) new level file\(s\) appended", result)
        assert summary and int(summary.group(1)) + int(summary.group(2)) == len(built_levels), result[-1500:]
        audit = json.loads(run("assets", "missing-mesh", out,
                               "docs/re_reference/def_schema.json", "--json"))
        assert not audit["missing"], audit["missing"][:12]
        print("Expanded Chapters: PASS")
        print(f"  source WAD payloads checked: {len(source_levels)}")
        print(f"  all built loose payloads checked: {len(built_levels)}")
        print(f"  WAD: {summary.group(1)} replaced, {summary.group(2)} appended")
        print(f"  direct missing mesh references: {len(audit['missing'])}")
        for line in result.splitlines():
            if "changes applied" in line or "level TNG merge:" in line:
                print(" ", line)
        return 0
    finally:
        assert work.resolve().is_relative_to((REPO / "build").resolve())
        shutil.rmtree(work)


if __name__ == "__main__":
    raise SystemExit(main())
