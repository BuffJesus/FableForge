#!/usr/bin/env python3
"""Build Fable: The Lost Content over scratch retail and audit its WAD payloads."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "work/nexus_mods/_peek/LostContent/Version 0.7.5/FableTLContent/The Mod/Fable The Lost Chapters"
RETAIL = Path(r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters")
TOOL = REPO / "build/forge-tools.exe"
AEON = REPO / "work/nexus_mods/_peek/AeonEdition"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--with-aeon", action="store_true", help="audit Aeon first, Lost Content second")
    args = parser.parse_args()
    required = [TOOL, SOURCE / "data/CompiledDefs/game.bin",
                SOURCE / "data/Levels/FinalAlbion",
                SOURCE / "data/graphics/graphics.big",
                RETAIL / "data/CompiledDefs/game.bin",
                RETAIL / "data/Levels/FinalAlbion.wad",
                RETAIL / "data/graphics/graphics.big"]
    if args.with_aeon:
        required += [AEON / "Data/CompiledDefs/game.bin", AEON / "Data/graphics/graphics.big"]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        print("Lost Content corpus or retail install missing; skipped:", *missing, sep="\n  ")
        return 0

    work = Path(tempfile.mkdtemp(prefix="lost_content_", dir=REPO / "build"))
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

        if args.with_aeon:
            run("mods", "add", root, AEON, "--name", "Aeon Edition")
        run("mods", "add", root, SOURCE, "--name", "Lost Content")
        result = run("mods", "build", root, out)
        assert "parks the retail WAD as _FinalAlbion.wad: skipped" in result
        assert not (out / "data/Levels/_FinalAlbion.wad").exists()
        assert not (out / "userst.ini").exists()
        wad = out / "data/Levels/FinalAlbion.wad"
        assert wad.is_file()
        extracted = work / "extracted"
        run("wad", "extract", wad, extracted)
        source_levels = [path for path in (SOURCE / "data/Levels").rglob("*")
                         if path.is_file() and path.suffix.lower() in (".lev", ".tng")]
        assert len(source_levels) >= 950, len(source_levels)
        loose_count = 0
        for src in source_levels:
            rel = src.relative_to(SOURCE / "data/Levels")
            loose = out / "data/Levels" / rel
            packed = extracted / "Data/Levels" / rel
            assert packed.is_file(), str(rel)
            expected = loose.read_bytes() if loose.is_file() else src.read_bytes()
            assert packed.read_bytes() == expected, f"WAD payload differs: {rel}"
            loose_count += loose.is_file()
        built_levels = [path for path in (out / "data/Levels").rglob("*")
                        if path.is_file() and path.suffix.lower() in (".lev", ".tng")]
        for loose in built_levels:
            rel = loose.relative_to(out / "data/Levels")
            packed = extracted / "Data/Levels" / rel
            assert packed.is_file() and packed.read_bytes() == loose.read_bytes(), str(rel)
        summary = re.search(r"FinalAlbion\.wad rebuilt: (\d+) level file\(s\) repacked, (\d+) new level file\(s\) appended", result)
        assert summary, result[-1500:]
        assert int(summary.group(1)) + int(summary.group(2)) == len(built_levels)
        if not args.with_aeon:
            assert int(summary.group(1)) + int(summary.group(2)) == loose_count
        audit = json.loads(run("assets", "missing-mesh", out,
                               "docs/re_reference/def_schema.json", "--json"))
        if not args.with_aeon:
            assert not audit["missing"], audit["missing"][:12]
        print("Aeon + Lost Content: AUDIT" if args.with_aeon else "Lost Content: PASS")
        print(f"  WAD payloads checked: {len(source_levels)} ({loose_count} loose overrides)")
        print(f"  all built loose level payloads checked: {len(built_levels)}")
        print(f"  WAD: {summary.group(1)} replaced, {summary.group(2)} appended")
        print(f"  direct missing mesh references: {len(audit['missing'])}")
        for row in audit["missing"][:12]:
            print(f"    {row['definition']} -> mesh {row['mesh_id']}")
        for line in result.splitlines():
            if "changes applied" in line or "level TNG merge:" in line or "field merge:" in line:
                print(" ", line)
        return 0
    finally:
        assert work.resolve().is_relative_to((REPO / "build").resolve())
        shutil.rmtree(work)


if __name__ == "__main__":
    raise SystemExit(main())
