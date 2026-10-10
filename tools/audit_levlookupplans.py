"""Compare LEV palette CRC candidates and class-index widths across namespace plans.

Atmosphere uses a positive unsigned-byte class ordinal; sound uses a positive
signed-byte class ordinal. These checks retain every candidate and flag zero
separately from representable positive indices. Consumes hash-pinned audit reports.
Does not select duplicate-key winners,
certify runtime fallback or admit sources. No game/source files are written.
"""
import argparse
from collections import Counter
import json
from pathlib import Path

from audit_defnamespace import lookup_groups
from audit_levsymbols import digest
from test_fmp_links import read_bank, crc0


def planned_identities(plan):
    if plan.get("preparation_error"):
        raise RuntimeError("namespace preparation refused")
    targets = {i: (e["definition"], e["name"])
               for i, e in enumerate(read_bank(Path(plan["baseline"])))}
    for source in plan["sources"]:
        for entry in source["prepared_game"]:
            identity = (entry["definition"], entry["name"])
            if targets.setdefault(entry["target"], identity) != identity:
                raise RuntimeError("plan assigns incompatible identities to one target")
    if set(targets) != set(range(plan["entries"])):
        raise RuntimeError("plan target coverage incomplete")
    return targets


def compare_queries(targets, source_slots, references):
    # Source slots are in original record order, expressed in planned slots.
    source_slots = list(source_slots)
    planned = lookup_groups(targets)
    original = lookup_groups({slot: targets[slot] for slot in source_slots})
    def class_ordinals(slots):
        counts, ordinals = Counter(), {}
        for slot in slots:
            definition = targets[slot][0]
            ordinals[slot] = counts[definition]
            counts[definition] += 1
        return ordinals

    source_ordinals = class_ordinals(source_slots)
    planned_ordinals = class_ordinals(sorted(targets))

    index_domains = {
        'atmosphere': ('ENVIRONMENT_THEME_DAY', 255, 'nonzero_byte_ordinal'),
        'sound': ('SOUND_THEME', 127, 'nonzero_signed_byte_ordinal'),
    }

    def candidate_indices(candidates, ordinals, domain):
        expected, maximum, check = index_domains[domain]
        return [{"slot": candidate["slot"], "class_ordinal": ordinals[candidate["slot"]],
                 "expected_class": candidate["definition"] == expected,
                 check: 0 < ordinals[candidate["slot"]] <= maximum}
                for candidate in candidates]

    result = []
    for reference in references:
        crc = crc0(reference["name"])
        before, after = (original.get(crc, []), planned.get(crc, [])) if reference['name'] else ([], [])
        status = ("empty" if not reference['name'] else "changed" if before != after else "ambiguous" if len(after) > 1
                  else "unique" if after else "missing")
        row = {"domain": reference["domain"], "name": reference["name"], "crc": crc,
                       "status": status, "source_candidates_in_planned_slots": before,
                       "planned_candidates": after, "files": reference["files"]}
        domain = reference["domain"]
        if domain in index_domains:
            # Loader 0223393B uses a class ordinal; 02233B93 takes its low byte.
            # Consumer 0244FBE0 interprets nonzero cells in ENVIRONMENT_THEME_DAY.
            # Keep every CRC candidate; this does not choose a winner or fallback.
            # Sound loader 02233370 uses a class ordinal too; 02233CD9 stores
            # its low byte, and GetSoundAt 02238DD5 sign-extends that byte.
            row[f"{domain}_indices"] = {
                "source": candidate_indices(before, source_ordinals, domain),
                "planned": candidate_indices(after, planned_ordinals, domain),
                "scope": "Candidate class and positive byte-index checks only (sound is signed); "
                         "no runtime winner or fallback claim."}
        result.append(row)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--symbols", type=Path, required=True)
    parser.add_argument("--plan", type=Path, action="append", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    artifacts = [args.symbols.resolve(), *(p.resolve() for p in args.plan)]
    artifact_hashes = {str(p): digest(p) for p in artifacts}
    symbols = json.loads(artifacts[0].read_text(encoding="utf-8"))
    plans = [json.loads(p.read_text(encoding="utf-8")) for p in artifacts[1:]]
    roots = [Path(s["root"]).resolve() for s in symbols["sources"]]
    roots += [Path(p["baseline"]).resolve() for p in plans]
    roots += [Path(s["root"]).resolve() for p in plans for s in p["sources"]]
    output = args.out.resolve()
    if output in artifacts or any(output.is_relative_to(root) for root in roots) or output in {
        Path(__file__).resolve(), *(Path(__file__).with_name(name).resolve() for name in
                                  ("audit_defnamespace.py", "audit_levsymbols.py", "test_fmp_links.py"))}:
        parser.error("report must not replace input artifacts, source files or tooling")
    hashes = dict(artifact_hashes)
    for report in [*symbols["sources"], *plans]:
        for path, expected in report["input_sha256"].items():
            if hashes.setdefault(path, expected) != expected:
                raise RuntimeError(f"reports disagree on input hash: {path}")
    for path, expected in hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f"stale input report: {path}")
    result = {"scope": __doc__, "source_admitted": False, "input_sha256": hashes, "plans": []}
    by_root = {str(Path(s["root"]).resolve()).casefold(): s for s in symbols["sources"]}
    for artifact, plan in zip(artifacts[1:], plans, strict=True):
        targets = planned_identities(plan)
        row = {"plan": str(artifact), "sources": []}
        for source in plan["sources"]:
            inventory = by_root[str(Path(source["root"]).resolve()).casefold()]
            slots = [entry["target"] for entry in source["prepared_game"]]
            reviews = []
            for review in inventory["reviews"]:
                queries = compare_queries(targets, slots, review["references"])
                counts = {status: sum(q["status"] == status for q in queries)
                          for status in ("unique", "missing", "ambiguous", "changed")}
                for domain, check in (("atmosphere", "nonzero_byte_ordinal"),
                                      ("sound", "nonzero_signed_byte_ordinal")):
                    for side in ("source", "planned"):
                        counts[f"{domain}_{side}_index_findings"] = sum(
                            any(not c["expected_class"] or not c[check]
                                for c in q.get(f"{domain}_indices", {}).get(side, [])) for q in queries)
                reviews.append({"path": review["path"], "counts": counts, "references": queries})
                print(artifact.name, source["root"], review["maps"], counts, flush=True)
            row["sources"].append({"root": source["root"], "reviews": reviews})
        result["plans"].append(row)
    for path, expected in hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f"input changed during review: {path}")
    result["script_sha256"] = digest(Path(__file__))
    result["dependency_sha256"] = {name: digest(Path(__file__).with_name(name)) for name in
                                    ("audit_defnamespace.py", "audit_levsymbols.py", "test_fmp_links.py")}
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
