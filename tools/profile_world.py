"""Capture the read-only world route with Tracy 0.13.1 and export CPU/GPU CSVs.

See docs/PROFILING.md for the opt-in build and Tracy tool download. Normal UI
automation includes a labelled 4 ms pacing sleep; it is not a GPU benchmark.
"""
import argparse
import bisect
import csv
from collections import defaultdict
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def summarize(path, begin=None, end=None):
    groups = defaultdict(list)
    with path.open(encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream):
            if begin is not None:
                start = float(row.get("ns_since_start", row.get("Time from start of program", 0)))
                if start < begin or (end is not None and start >= end):
                    continue
            value = row.get("exec_time_ns", row.get("GPU execution time"))
            if value is not None:
                groups[row["name"]].append(float(value) / 1e6)
    report = {}
    for name, values in groups.items():
        values.sort()
        def percentile(p):
            index = (len(values) - 1) * p
            lower = int(index)
            return values[lower] + (values[min(lower + 1, len(values) - 1)] - values[lower]) * (index - lower)
        report[name] = dict(samples=len(values), total_ms=sum(values),
                            median_ms=percentile(.5), p95_ms=percentile(.95),
                            p99_ms=percentile(.99), max_ms=values[-1])
    return report


def route_markers(path, script):
    names = {line.split(maxsplit=1)[1] for line in script.read_text(encoding="utf-8").splitlines()
             if line.startswith("profile_mark ")}
    markers, diagnostics = [], []
    # Tracy's message exporter does not quote commas inside message text.
    # The timestamp is always the last column; internal messages are not phases.
    for line in path.read_text(encoding="utf-8-sig").splitlines()[1:]:
        name, stamp = line.rsplit(",", 1)
        if name in names:
            markers.append((name, float(stamp)))
        else:
            diagnostics.append(name)
    return sorted(markers, key=lambda entry: entry[1]), diagnostics


def dispatch_delays(path):
    """Match each serial prepare to its first upload before the next prepare.

    A completion with no upload in that interval may have been discarded after
    camera movement; do not misreport the next map's upload as its dispatch.
    """
    with path.open(encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
    workers = sorted((float(r["ns_since_start"]), float(r["ns_since_start"])+float(r["exec_time_ns"]))
                     for r in rows if r["name"] == "World detail prepare")
    uploads = sorted(float(r["ns_since_start"]) for r in rows if r["name"] == "World paced upload")
    delays = []
    for i, (_, end) in enumerate(workers):
        at = bisect.bisect_left(uploads, end)
        limit = workers[i+1][0] if i+1 < len(workers) else float("inf")
        if at < len(uploads) and uploads[at] < limit:
            delays.append((uploads[at]-end)/1e6)
    ordered = sorted(delays)
    n = len(ordered)
    return dict(prepared_maps=len(workers), matched_uploads=n, unmatched=len(workers)-n,
                median_ms=(ordered[(n-1)//2]+ordered[n//2])/2 if n else None,
                max_ms=max(ordered) if n else None, total_ms=sum(ordered), delays_ms=delays)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build-profile-clang/FableForge.exe")
    parser.add_argument("--tracy-dir", type=Path, default=ROOT / "build/tools/tracy")
    parser.add_argument("--script", type=Path, default=ROOT / "tests/ui/world_profile.txt")
    parser.add_argument("--output", type=Path, default=ROOT / "build/profiles/world")
    parser.add_argument("--cold", action="store_true", help="Use a fresh isolated derived-tile cache (retained for inspection)")
    parser.add_argument("--show", action="store_true", help="Show the editor window (hidden by default; still renders on the GPU)")
    parser.add_argument("--frame-ms", type=int, default=4, choices=range(4, 101), help="Automation sleep per frame; use 33 to reduce GPU contention while gaming")
    args = parser.parse_args()
    capture = args.tracy_dir.resolve() / "tracy-capture.exe"
    export = args.tracy_dir.resolve() / "tracy-csvexport.exe"
    for path in (args.exe, args.script, capture, export):
        if not path.is_file():
            parser.error(f"Missing {path}; see docs/PROFILING.md")
    prefix = args.output.resolve()
    prefix.parent.mkdir(parents=True, exist_ok=True)
    trace = Path(str(prefix) + ".tracy")
    # A hidden capture still uses the GPU. Competing workloads invalidate FPS comparisons.
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0) | getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)
    startup = None
    if os.name == "nt" and not args.show:
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    environment = os.environ.copy()
    if args.cold:
        cache = tempfile.mkdtemp(prefix="world-tiles-", dir=prefix.parent)
        environment["FABLEFORGE_TILE_CACHE"] = cache
        print(f"Cold derived-tile cache: {cache}", flush=True)
    with Path(str(prefix) + "-capture.log").open("w", encoding="utf-8") as log:
        recorder = subprocess.Popen([str(capture), "-a", "127.0.0.1", "-o", str(trace), "-f"],
                                    stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
        try:
            run = subprocess.run([str(args.exe.resolve()), "--auto", str(args.script.resolve()), "--auto-frame-ms", str(args.frame_ms)],
                                 cwd=ROOT, env=environment, timeout=180, startupinfo=startup, creationflags=flags)
            capture_code = recorder.wait(timeout=30)
        finally:
            if recorder.poll() is None:
                recorder.terminate()
                recorder.wait(timeout=10)
    script_log = Path(str(args.script.resolve()) + ".log").read_text(encoding="utf-8", errors="replace")
    if run.returncode or capture_code or "RESULT PASS" not in script_log or not trace.is_file():
        raise RuntimeError(f"Capture failed: editor={run.returncode}, recorder={capture_code}. See {prefix}-capture.log and {args.script}.log")
    reports = {}
    for kind, switches in (("cpu", ["-u"]), ("cpu-self", ["-u", "-e"]), ("gpu", ["-g"]), ("messages", ["-m"]), ("plots", ["-u", "-p"])):
        output = Path(str(prefix) + f"-{kind}.csv")
        with output.open("w", encoding="utf-8", newline="") as stream:
            subprocess.run([str(export), *switches, str(trace)], stdout=stream,
                           check=True, timeout=120, creationflags=flags)
        if kind in ("cpu", "cpu-self", "gpu"):
            reports[kind] = summarize(output)
    if not reports["cpu"].get("World detail prepare") or not reports["gpu"]:
        raise RuntimeError("Trace is missing world CPU or GPU samples; inspect the exported CSVs")
    sizes = defaultdict(list)
    with Path(str(prefix) + "-plots.csv").open(encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream):
            if not row["src_file"] and row["name"] in ("World prepared geometry bytes", "World expanded equivalent bytes",
                                                       "Overview original water bytes", "Overview compact water bytes"):
                sizes[row["name"]].append(float(row["value"]))
    indexed = sizes["World prepared geometry bytes"]
    expanded = sizes["World expanded equivalent bytes"]
    if indexed and len(indexed) == len(expanded) and sum(expanded) > 0:
        reports["geometry"] = dict(prepared_maps=len(indexed), indexed_bytes=sum(indexed),
                                    expanded_equivalent_bytes=sum(expanded), reduction_fraction=1-sum(indexed)/sum(expanded))
        print(f"Prepared geometry: {sum(indexed)/1048576:.1f} MiB vs {sum(expanded)/1048576:.1f} MiB expanded equivalent "
              f"across {len(indexed)} prepared maps ({100*(1-sum(indexed)/sum(expanded)):.1f}% reduction; not peak residency)")
    markers, diagnostics = route_markers(Path(str(prefix) + "-messages.csv"), args.script)
    original_water = sizes["Overview original water bytes"]
    compact_water = sizes["Overview compact water bytes"]
    if original_water and len(original_water) == len(compact_water) and sum(original_water) > 0:
        reports["overview_water"] = dict(baked_maps=len(original_water), original_bytes=sum(original_water),
                                          compact_bytes=sum(compact_water), reduction_fraction=1-sum(compact_water)/sum(original_water))
        print(f"Overview water: {sum(compact_water)/1048576:.1f} MiB vs {sum(original_water)/1048576:.1f} MiB original "
              f"across {len(original_water)} baked maps (cache hits excluded)")
    reports["diagnostics"] = diagnostics
    reports["dispatch"] = dispatch_delays(Path(str(prefix) + "-cpu.csv"))
    if diagnostics:
        print("Profiler diagnostics (inspect before trusting GPU timings):", *sorted(set(diagnostics)), sep="\n  ")
    reports["phases"] = {}
    for (name, begin), (_, end) in zip(markers, markers[1:]):
        reports["phases"][name] = {kind: summarize(Path(str(prefix) + f"-{kind}.csv"), begin, end) for kind in ("cpu", "gpu")}
    Path(str(prefix) + "-summary.json").write_text(json.dumps(reports, indent=2), encoding="utf-8")
    print(f"Captured {trace}")
    for kind in ("cpu", "gpu"):
        print(f"{kind.upper()} zone times (inclusive; nested zones overlap):")
        for name, row in sorted(reports[kind].items(), key=lambda pair: pair[1]["total_ms"], reverse=True)[:12]:
            print(f"  {name}: n={row['samples']}, median={row['median_ms']:.3f} ms, "
                  f"p99={row['p99_ms']:.3f} ms, max={row['max_ms']:.3f} ms")


if __name__ == "__main__":
    main()
