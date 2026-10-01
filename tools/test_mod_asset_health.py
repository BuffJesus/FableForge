#!/usr/bin/env python3
"""A mod adding a broken Graphic reference appears in Check conflicts, without blaming base defects."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


REPO = Path(__file__).resolve().parent.parent
RETAIL = Path(r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters")
TOOL = REPO / "build" / "forge-tools.exe"
GUI = REPO / "build" / "FableForge.exe"
SCHEMA = REPO / "docs" / "re_reference" / "def_schema.json"
NAME = "OBJECT_FORGE_MISSING_MESH_TEST"


def main():
    needed = [TOOL, GUI, SCHEMA, RETAIL / "data/CompiledDefs/game.bin",
              RETAIL / "data/CompiledDefs/names.bin", RETAIL / "data/graphics/graphics.big"]
    if any(not p.exists() for p in needed):
        print("mod asset health skipped (build or retail install missing)")
        return 0
    work = Path(tempfile.mkdtemp(prefix="mod_asset_health_", dir=REPO / "build"))
    try:
        root, source = work / "root", work / "source"
        defs = root / "data/CompiledDefs"
        defs.mkdir(parents=True)
        for name in ("game.bin", "names.bin"):
            shutil.copyfile(RETAIL / "data/CompiledDefs" / name, defs / name)
        graphics = root / "data/graphics"
        graphics.mkdir(parents=True)
        shutil.copyfile(RETAIL / "data/graphics/graphics.big", graphics / "graphics.big")

        def run(*args):
            result = subprocess.run([str(TOOL), *map(str, args)], cwd=REPO,
                                    capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f"{args}: {result.stdout[-800:]} {result.stderr[-800:]}")
            return result.stdout

        run("ui", "clone-object-model", root, "OBJECT_BARREL_UNBREAKABLE", NAME,
            "1000000", "--schema", SCHEMA, "--out", source)
        run("mods", "add", root, source, "--name", "Missing model probe")
        report = json.loads(run("mods", "conflicts", root, "--json"))
        health = report["asset_health"]
        assert health["status"] == "checked", health
        assert health["unparsed_defs"] == 0, health
        assert health["introduced"] == [{"definition": NAME, "type": "OBJECT",
                                          "mesh_id": 1000000}], health
        assert health["missing_total"] == health["baseline_missing"] + 1, health
        build_text = run("mods", "build", root, work / "merged")
        assert "warning: 1 new definition Graphic reference(s) point to missing mesh ids" in build_text

        script = work / "ui.txt"
        script.write_text("\n".join([
            "wait_maps", "wait_ready", f"set saveroot {root}", "mods_tab 1",
            "frames 2", "mods_conflicts", "wait_mods", "frames 2",
            "assert_state mods_missing_models 1", "assert_state mods_conflicts 0",
            "screenshot build/ui/mod_asset_health.png", "quit", ""]), encoding="utf-8")
        result = subprocess.run([str(GUI), "--auto", str(script), "--install", str(RETAIL)],
                                cwd=REPO, capture_output=True, text=True)
        log_path = Path(str(script) + ".log")
        log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
        assert result.returncode == 0 and "RESULT PASS" in log, log[-1800:] + result.stderr[-400:]
        print("PASS: Check conflicts reports one introduced missing model; base defects stay separate; GUI displays the count")
        return 0
    finally:
        assert work.resolve().is_relative_to((REPO / "build").resolve())
        shutil.rmtree(work)


if __name__ == "__main__":
    raise SystemExit(main())
