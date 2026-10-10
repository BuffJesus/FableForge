"""Read-only WLD/BWD region-definition CRC inventory and optional plan comparison.

Storage must pass the public reviewer. Empty names are recorded separately.
Display text, minimap assets, runtime fallback and source admission remain outside
this inventory. Divergent WLD/BWD contents are retained as separate inputs.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess

from audit_defnamespace import lookup_groups, names
from audit_levlookupplans import compare_queries, planned_identities
from audit_levsymbols import digest
from test_fmp_links import read_bank, crc0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, default=Path(__file__).resolve().parents[1] / "build/forge-tools.exe")
    parser.add_argument("--source", type=Path, action="append", required=True)
    parser.add_argument("--plan", type=Path, action="append", default=[])
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    roots = [p.resolve() for p in args.source]
    plans = [(p.resolve(), json.loads(p.read_text(encoding="utf-8"))) for p in args.plan]
    tool, output = args.tool.resolve(), args.out.resolve()
    protected = roots + [Path(p["baseline"]).resolve() for _, p in plans]
    protected += [Path(s["root"]).resolve() for _, p in plans for s in p["sources"]]
    scripts = [Path(__file__).resolve(), *(Path(__file__).with_name(name).resolve() for name in
               ("audit_defnamespace.py", "audit_levlookupplans.py", "audit_levsymbols.py", "test_fmp_links.py"))]
    if output in [tool, *scripts, *(p for p, _ in plans)] or any(output.is_relative_to(p) for p in protected):
        parser.error("report must not replace source inputs, plans or tooling")
    hashes = {str(p): digest(p) for p in [tool, *scripts, *(p for p, _ in plans)]}
    for _, plan in plans:
        for path, expected in plan["input_sha256"].items():
            if hashes.setdefault(path, expected) != expected:
                raise RuntimeError(f"input reports disagree: {path}")
    for path, expected in hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f"stale input report: {path}")
    target_plans = [(path, plan, planned_identities(plan)) for path, plan in plans]
    report = {"scope": __doc__, "source_admitted": False, "sources": []}
    for root in roots:
        files = sorted(p for p in (root / "data/Levels").rglob("*")
                       if p.is_file() and p.suffix.lower() in (".wld", ".bwd"))
        if not files:
            raise RuntimeError(f"no world files under {root}")
        for path in [*files, root / "data/CompiledDefs/game.bin", root / "data/CompiledDefs/names.bin"]:
            expected = digest(path)
            if hashes.setdefault(str(path), expected) != expected:
                raise RuntimeError(f"stale source bank: {path}")
        if any(stored != crc0(name) for stored, name in names(root).values()):
            raise RuntimeError(f"stored name CRC/text disagreement: {root}")
        bank = read_bank(root)
        groups = lookup_groups({i: (e["definition"], e["name"]) for i, e in enumerate(bank)})
        source = {"root": str(root), "files": []}
        for path in files:
            run = subprocess.run([str(tool), "world", "review-storage", str(path), "--json"],
                                 capture_output=True, text=True, encoding="utf-8", check=False)
            if run.returncode:
                raise RuntimeError(f"world storage refused: {run.stdout or run.stderr}")
            regions = json.loads(run.stdout)["units"][0]["unresolved_region_symbols"]
            references = []
            for region in regions:
                name = region["region_definition"]
                matches = groups.get(crc0(name), []) if name else []
                status = "empty" if not name else "missing" if not matches else "unique" if len(matches) == 1 else "ambiguous"
                references.append({**region, "status": status, "crc_candidates": matches})
            counts = dict(Counter(r["status"] for r in references))
            row = {"path": str(path), "counts": counts, "regions": references, "plans": []}
            queries = [{"domain": "region", "name": r["region_definition"], "files": [str(path)]}
                       for r in references if r["region_definition"]]
            for artifact, plan, targets in target_plans:
                owners = [s for s in plan["sources"] if Path(s["root"]).resolve() == root]
                if len(owners) != 1:
                    raise RuntimeError(f"plan must contain source exactly once: {root}")
                compared = compare_queries(targets, [e["target"] for e in owners[0]["prepared_game"]], queries)
                row["plans"].append({"plan": str(artifact), "counts": dict(Counter(r["status"] for r in compared)),
                                     "references": compared})
            source["files"].append(row)
            print(root.name, path.relative_to(root), counts, flush=True)
        report["sources"].append(source)
    for path, expected in hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f"input changed during review: {path}")
    report["input_sha256"] = hashes
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
