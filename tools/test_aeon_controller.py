#!/usr/bin/env python3
"""Compose Aeon Edition, Controller Support.fmp, and its EgoCore DLL on a scratch retail root.

Uses locally extracted mod corpus; never deploys to the game install.
Run: python tools/test_aeon_controller.py [--keep]
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


REPO = Path(__file__).resolve().parent.parent
CORPUS = REPO / "work" / "nexus_mods" / "_peek"
RETAIL = Path(r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters")
TOOL = REPO / "build" / "forge-tools.exe"
AEON = CORPUS / "AeonEdition"
FMP = CORPUS / "ControllerSupportFMP" / "ControllerSupport.fmp"
DLL = CORPUS / "ControllerSupportFMP" / "FableControllerSupport.dll"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep", action="store_true", help="keep the scratch input and output")
    args = parser.parse_args()
    required = [TOOL, AEON / "Data" / "CompiledDefs" / "game.bin",
                AEON / "Data" / "graphics" / "graphics.big",
                AEON / "Data" / "graphics" / "pc" / "textures.big",
                AEON / "Data" / "Bones" / "bandit_short.bncfg",
                AEON / "Data" / "Levels" / "FinalAlbion_RT.stb", FMP, DLL,
                RETAIL / "data" / "CompiledDefs" / "game.bin",
                RETAIL / "data" / "Levels" / "FinalAlbion.wad",
                RETAIL / "data" / "graphics" / "graphics.big"]
    missing = [str(p) for p in required if not p.exists()]
    if missing:
        print("Aeon/controller corpus or retail install missing; skipped:", *missing, sep="\n  ")
        return 0

    work = Path(tempfile.mkdtemp(prefix="aeon_controller_", dir=REPO / "build"))
    root, out = work / "retail", work / "merged"
    try:
        defs = root / "data" / "CompiledDefs"
        defs.mkdir(parents=True)
        for name in ("game.bin", "names.bin", "script.bin", "frontend.bin"):
            src = RETAIL / "data" / "CompiledDefs" / name
            if src.exists():
                shutil.copyfile(src, defs / name)
        levels = root / "data" / "Levels"
        levels.mkdir(parents=True)
        shutil.copyfile(RETAIL / "data" / "Levels" / "FinalAlbion.wad",
                        levels / "FinalAlbion.wad")
        controller = work / "source" / "FableControllerSupport"
        controller.mkdir(parents=True)
        shutil.copyfile(DLL, controller / DLL.name)

        def run(*argv):
            proc = subprocess.run([str(TOOL), *map(str, argv)], cwd=REPO,
                                  capture_output=True, text=True)
            if proc.returncode:
                raise AssertionError(f"{' '.join(map(str, argv))}\n{proc.stdout[-1200:]}\n{proc.stderr[-1200:]}")
            return proc.stdout

        run("mods", "add", root, AEON, "--name", "Aeon Edition")
        aeon_out = work / "aeon_only"
        run("mods", "build", root, aeon_out)
        aeon_decoded = run("defs", "decode", aeon_out, "docs/re_reference/def_schema.json",
                           "FABLE_XBOX_CONTROL_SCHEME_BASE")
        aeon_count = int(re.search(r"Controls\s+Vector_VCActionInputControl__ count=(\d+)", aeon_decoded).group(1))
        run("mods", "add", root, FMP, "--name", "Controller Support FMP")
        run("mods", "add", root, controller, "--name", "Controller Support DLL")
        order = json.loads(run("mods", "list", root, "--json"))["mods"]
        assert [m["kind"] for m in order] == ["tree", "fmp", "egocore"], order
        result = run("mods", "build", root, out)
        built_dll = out / "Mods" / "FableControllerSupport" / DLL.name
        assert built_dll.read_bytes() == DLL.read_bytes()
        assert not (built_dll.parent / "Data").exists(), "DLL-only handoff must not recompile EgoCore text defs"
        ini = (out / "Mods.ini").read_text(encoding="utf-8")
        assert "FableControllerSupport\\FableControllerSupport.dll=1" in ini, ini
        assert (out / "data" / "CompiledDefs" / "game.bin").is_file()
        wad = out / "data" / "Levels" / "FinalAlbion.wad"
        assert wad.is_file() and wad.stat().st_size > 100_000_000
        wad_listing = run("wad", "list", wad)
        assert "BarrowFields.tng" in wad_listing
        wad_files = work / "wad_extracted"
        extracted = run("wad", "extract", wad, wad_files)
        extracted_count = re.search(r"extracted (\d+) of (\d+) entries", extracted)
        assert extracted_count and int(extracted_count.group(1)) == int(extracted_count.group(2)), extracted
        base_wad_listing = run("wad", "list", root / "data" / "Levels" / "FinalAlbion.wad")
        base_count = int(re.search(r": (\d+) entries", base_wad_listing).group(1))
        assert int(extracted_count.group(1)) == base_count + 47, extracted
        loose = out / "data" / "Levels" / "FinalAlbion"
        aeon_loose = AEON / "Data" / "Levels" / "FinalAlbion"
        bundled = wad_files / "Data" / "Levels" / "FinalAlbion"
        aeon_levels = sorted(p for p in aeon_loose.iterdir() if p.suffix.lower() in (".lev", ".tng"))
        root_levels = sorted(p for p in (AEON / "Data" / "Levels").iterdir()
                             if p.is_file() and p.suffix.lower() in (".lev", ".tng"))
        assert len(aeon_levels) == 841, len(aeon_levels)
        assert {p.name for p in root_levels} == {"creature_hub.lev", "creature_hub.tng"}
        source_differences = []
        loose_overrides = 0
        for src in aeon_levels + root_levels:
            merged = (loose if src in aeon_levels else out / "data" / "Levels") / src.name
            packed = (bundled if src in aeon_levels else wad_files / "Data" / "Levels") / src.name
            assert packed.is_file(), src.name
            merged_bytes = merged.read_bytes() if merged.is_file() else src.read_bytes()
            if merged.is_file():
                loose_overrides += 1
            assert packed.read_bytes() == merged_bytes, f"WAD and expected build differ: {src.name}"
            if src in root_levels:
                assert merged_bytes == src.read_bytes(), f"root level changed unexpectedly: {src.name}"
            if merged_bytes != src.read_bytes():
                source_differences.append(src.name)
        graphics = out / "data" / "graphics" / "graphics.big"
        assert graphics.is_file() and graphics.stat().st_size > 200_000_000
        assert (out / "data" / "Levels" / "FinalAlbion_RT.stb").is_file()
        assert (out / "data" / "Bones" / "bandit_short.bncfg").read_bytes() == (AEON / "Data" / "Bones" / "bandit_short.bncfg").read_bytes()
        decoded = run("defs", "decode", out, "docs/re_reference/def_schema.json",
                      "FABLE_XBOX_CONTROL_SCHEME_BASE")
        merged_count = int(re.search(r"Controls\s+Vector_VCActionInputControl__ count=(\d+)", decoded).group(1))
        # Controller Support's FMP ships a full 141-entry keyboard/pad scheme.
        # Aeon's replacement has 70; the FMP intentionally wins this field.
        assert (aeon_count, merged_count) == (70, 141), (aeon_count, merged_count)
        assert "0 field conflicts, 0 whole-record conflicts" in result, result[-1200:]
        wad_summary = re.search(r"FinalAlbion\.wad rebuilt: (\d+) level file\(s\) repacked, 47 new level file\(s\) appended", result)
        assert wad_summary and int(wad_summary.group(1)) == loose_overrides - 47, result[-1200:]
        mesh_audit = json.loads(run("assets", "missing-mesh", out,
                                    "docs/re_reference/def_schema.json", "--json"))
        assert mesh_audit["unparsed_defs"] == 0, mesh_audit["unparsed_defs"]
        assert not mesh_audit["missing"], mesh_audit["missing"][:12]
        print("PASS: Aeon Edition -> Controller Support FMP -> EgoCore DLL")
        print("  order:", ", ".join(m["kind"] for m in order))
        print(f"  controller binding count: Aeon {aeon_count} -> merged {merged_count}; DLL registered in Mods.ini")
        print(f"  FinalAlbion.wad: all {len(aeon_levels) + len(root_levels)} Aeon level payloads match the effective build ({loose_overrides} loose overrides); {len(source_differences)} differ from the source")
        for name in source_differences[:8]:
            print("    source delta:", name)
        print(f"  missing model references against Aeon graphics.big: {len(mesh_audit['missing'])}")
        for row in mesh_audit["missing"][:8]:
            print(f"    {row['definition']} -> mesh {row['mesh_id']}")
        for line in result.splitlines():
            if "changes applied" in line or "conflict" in line or "FinalAlbion.wad" in line:
                print(" ", line[:200])
        return 0
    finally:
        if args.keep:
            print("scratch:", work)
        else:
            # The only recursive deletion is the temp directory we created under build/.
            assert work.resolve().is_relative_to((REPO / "build").resolve())
            shutil.rmtree(work)


if __name__ == "__main__":
    raise SystemExit(main())
