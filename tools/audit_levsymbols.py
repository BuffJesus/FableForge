"""Read-only exact-name inventory of validated LEV palettes against source game.bin.

Unique matches are evidence of name presence, not certified runtime resolution.
Ground/sound class checks use the native typed loaders; atmosphere uses the
ENVIRONMENT_THEME_DAY class-index consumer. Lookup aliases, fallback behavior,
class-index capacity and composed source admission remain separate.
Missing, duplicate and wrong-class names are explicit findings.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import subprocess

from test_fmp_links import read_bank


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def inventory(bank, maps):
    index = defaultdict(list)
    for slot, entry in enumerate(bank):
        if entry["name"]:
            index[entry["name"]].append({"slot": slot, "definition": entry["definition"]})
    references = {}
    for row in maps:
        for domain in ("ground", "atmosphere", "sound"):
            for name in row[domain]:
                key = (domain, name)
                if key not in references:
                    matches = index.get(name, [])
                    references[key] = {"domain": domain, "name": name, "matches": matches,
                        "status": "unique" if len(matches) == 1 else "missing" if not matches else "ambiguous",
                        "occurrences": 0, "files": set()}
                    # CMap::LoadFromFile: RTTI cast to CEngineThemeDef at
                    # 02232D7D; typed CSoundThemeDef lookup at 0223320C.
                    # Atmosphere: LoadFromFile converts global to class index;
                    # CEnvironment 0244FBE0 resolves that index in ENVIRONMENT_THEME_DAY.
                    expected = {"ground": "ENGINE_THEME", "sound": "SOUND_THEME",
                                "atmosphere": "ENVIRONMENT_THEME_DAY"}.get(domain)
                    references[key]["expected_definition"] = expected
                    references[key]["class_matches"] = (
                        matches[0]["definition"] == expected if expected and len(matches) == 1 else None)
                references[key]["occurrences"] += 1
                references[key]["files"].add(row["file"])
    result = []
    for key in sorted(references):
        row = references[key]
        row["files"] = sorted(row["files"])
        result.append(row)
    return result


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, default=repo / "build/fableforge_levstorage_tests.exe")
    parser.add_argument("--source", type=Path, action="append", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    roots = [p.resolve() for p in args.source]
    tool, output = args.tool.resolve(), args.out.resolve()
    protected = [tool, Path(__file__).resolve(), Path(__file__).with_name("test_fmp_links.py").resolve()]
    if output in protected or any(output.is_relative_to(root) for root in roots):
        parser.error("report must be outside source roots and must not replace tooling")
    tool_hash = digest(tool)
    report = {"scope": __doc__, "source_admitted": False, "tool_sha256": tool_hash, "sources": []}
    for root in roots:
        levels = root / "data/Levels"
        files = sorted(p for p in levels.rglob("*") if p.is_file() and p.suffix.lower() in (".lev", ".wad"))
        if not files:
            raise RuntimeError(f"no LEV/WAD inputs under {levels}")
        inputs = [*files, root / "data/CompiledDefs/game.bin", root / "data/CompiledDefs/names.bin"]
        before = {str(p): digest(p) for p in inputs}
        bank = read_bank(root, "game.bin")
        source = {"root": str(root), "input_sha256": before, "reviews": []}
        for path in [levels, *(p for p in files if p.suffix.lower() == ".wad")]:
            run = subprocess.run([str(tool), str(path), "--symbols"], capture_output=True,
                                 text=True, encoding="utf-8", check=False)
            if run.returncode:
                raise RuntimeError(f"LEV storage review failed: {run.stderr.strip()}")
            storage = json.loads(run.stdout)
            refs = inventory(bank, storage["symbols"])
            counts = {status: sum(r["status"] == status for r in refs)
                      for status in ("unique", "missing", "ambiguous")}
            counts["class_mismatches"] = sum(r["class_matches"] is False for r in refs)
            source["reviews"].append({"path": str(path), "maps": storage["maps"],
                                       "counts": counts, "references": refs})
            print(path, storage["maps"], counts, flush=True)
        if any(digest(Path(p)) != value for p, value in before.items()):
            raise RuntimeError(f"input changed during inventory: {root}")
        report["sources"].append(source)
    if digest(tool) != tool_hash:
        raise RuntimeError("inventory tool changed during review")
    report["script_sha256"] = digest(Path(__file__))
    report["bank_reader_sha256"] = digest(Path(__file__).with_name("test_fmp_links.py"))
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
