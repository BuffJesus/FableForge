"""Read-only region label/minimap reference inventory for original PC source banks.

Checks text CRC candidates and PLAYER_GUI_PC/DEFAULT MiniMapGraphics tables
against GBANK_MAIN_PC metadata. It does not choose the active GUI definition,
merge asset banks, certify texture payloads or admit a composed source.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct
import subprocess

from audit_levsymbols import digest
from test_fmp_links import crc0


def text_symbol_candidates(entries):
    groups = defaultdict(list)
    for entry in entries:
        # CBankFile::InitEntry 02F9C380 requires a nonempty symbol before
        # inserting its CRC. Do not silently discard unsupported metadata.
        if not entry['name'] or '\0' in entry['name']:
            raise ValueError('unsupported empty or embedded-NUL text-bank symbol')
        groups[crc0(entry['name'])].append(entry)
    return dict(groups)


def review_text_query(label, groups):
    matches = groups.get(crc0(label), []) if label else []
    # CTCRegionDisplay 01F6F280 treats returned bank ID zero as not found,
    # before inspecting the entry type. Nonempty CRC zero is still a query.
    status = ('empty' if not label else 'ambiguous' if len(matches) > 1 else
              'literal_fallback' if not matches or matches[0]['id'] == 0 else
              'unsupported_text_type' if matches[0]['type'] not in (0, 1) else 'entry')
    return status, matches


def parse_minimap_table(data):
    if len(data) < 4:
        raise ValueError("truncated minimap table")
    count, = struct.unpack_from("<I", data)
    if count > (len(data) - 4) // 5:
        raise ValueError("minimap count exceeds available bytes")
    at, result = 4, {}
    for _ in range(count):
        end = data.find(b"\0", at)
        if end < at or end + 5 > len(data):
            raise ValueError("truncated minimap name/value")
        name = data[at:end].decode("ascii")
        value, = struct.unpack_from("<i", data, end + 1)
        if not name or name in result:
            raise ValueError("empty or duplicate minimap name")
        result[name] = value
        at = end + 5
    if at != len(data):
        raise ValueError("trailing minimap bytes")
    return result


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world-report", type=Path, required=True)
    parser.add_argument("--tool", type=Path, default=repo / "build/forge-tools.exe")
    parser.add_argument("--schema", type=Path, default=repo / "docs/re_reference/def_schema.json")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    world_path, tool, schema, output = (p.resolve() for p in
                                       (args.world_report, args.tool, args.schema, args.out))
    world = json.loads(world_path.read_text(encoding="utf-8"))
    roots = [Path(s["root"]).resolve() for s in world["sources"]]
    scripts = [Path(__file__).resolve(), Path(__file__).with_name("audit_levsymbols.py").resolve(),
               Path(__file__).with_name("test_fmp_links.py").resolve()]
    if output in [world_path, tool, schema, *scripts] or str(output) in world["input_sha256"] or any(
            output.is_relative_to(root) for root in roots):
        parser.error("report must not replace inputs or tooling")
    hashes = dict(world["input_sha256"])
    for path in [world_path, tool, schema, *scripts]:
        actual = digest(path)
        if hashes.setdefault(str(path), actual) != actual:
            raise RuntimeError(f"stale tool/input report: {path}")
    for path, expected in hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f"stale world report input: {path}")

    def command(*arguments):
        run = subprocess.run([str(tool), *map(str, arguments)], capture_output=True,
                             text=True, encoding="utf-8", check=False)
        if run.returncode:
            raise RuntimeError(run.stderr or run.stdout)
        return json.loads(run.stdout)

    report = {"scope": __doc__, "source_admitted": False, "sources": []}
    for source, root in zip(world["sources"], roots, strict=True):
        banks = {}
        for key, relative, bank_name in [("text", "data/lang/English/text.big", "TEXT_ENGLISH_MAIN"),
                                         ("textures", "data/graphics/pc/textures.big", "GBANK_MAIN_PC")]:
            path = root / relative
            actual = digest(path)
            if hashes.setdefault(str(path), actual) != actual:
                raise RuntimeError(f"stale asset input: {path}")
            matching = [b for b in command("big", "list", path, "--json")["banks"] if b["bank"] == bank_name]
            if len(matching) != 1:
                raise RuntimeError(f"expected exactly one {bank_name} bank")
            banks[key] = matching[0]["entries"]
        text_crcs, textures = text_symbol_candidates(banks['text']), defaultdict(list)
        for entry in banks["textures"]:
            textures[entry["id"]].append(entry)
        gui_tables = {}
        for name in ("PLAYER_GUI_PC", "PLAYER_GUI_DEFAULT"):
            decoded = command("defs", "decode", root, schema, name, "--json")
            if not decoded["all_tags_ok"] or decoded["leftover"] or decoded["definition"] != "PLAYER_GUI":
                raise RuntimeError(f"unsupported GUI definition layout: {name}")
            fields = [f for f in decoded["fields"] if f["name"] == "MiniMapGraphics"]
            if len(fields) != 1 or not fields[0]["tag_ok"] or fields[0]["type"] != "Map_JVCCharString__":
                raise RuntimeError("ambiguous minimap table field")
            gui_tables[name] = parse_minimap_table(bytes.fromhex(fields[0]["value"]))
        result = {"root": str(root), "gui_tables": gui_tables, "files": []}
        for file in source["files"]:
            rows = []
            for region in file["regions"]:
                label = region["display_name"] or region["region_name"]
                text_status, matches = review_text_query(label, text_crcs)
                row = {"region_slot": region["region_slot"], "region_name": region["region_name"],
                       "display_query": label, "display_status": text_status, "text_candidates": matches,
                       "minimap_query": region["minimap_graphic"], "minimap_by_gui": {}}
                for owner, table in gui_tables.items():
                    key = region["minimap_graphic"]
                    value = table.get(key)
                    entries = textures.get(value, [])
                    status = ("empty" if not key else "missing_key" if value is None else
                              "missing_texture" if not entries else "ambiguous_texture" if len(entries) > 1
                              else "unsupported_texture_type" if entries[0]["type"] != 0 else "entry")
                    row["minimap_by_gui"][owner] = {"status": status, "texture_id": value,
                                                    "texture_candidates": entries}
                rows.append(row)
            counts = {"display": dict(Counter(r["display_status"] for r in rows)),
                      "minimap": {owner: dict(Counter(r["minimap_by_gui"][owner]["status"] for r in rows))
                                  for owner in gui_tables}}
            result["files"].append({"path": file["path"], "counts": counts, "regions": rows})
            print(root.name, Path(file["path"]).relative_to(root), counts, flush=True)
        report["sources"].append(result)
    for path, expected in hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError(f"input changed during asset review: {path}")
    report["input_sha256"] = hashes
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
