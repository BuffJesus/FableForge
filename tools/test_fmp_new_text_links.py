#!/usr/bin/env python3
"""Check FMP TextID relocation across direct apply, load order and text picks.

Uses the archived EXP And Hero Mod as a real link-bearing donor. The fixture
changes one equal-length metadata target and adds a type-0 Text bank entry;
no game installation or archive input is modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from test_fmp_links import crc0, read_bank
from test_fmp_texture_layers import entries


OLD = b"TEXT_GUI_HERO_ABILITY_ASSASSIN_RUSH"
NEW = b"TEXT_GUI_HERO_ABILITY_ASSASSIN_TEST"
EXTRA = "TEXT_FORGE_EXTRA_BEFORE_HERO"
HERO = "HERO_ABILITY_SKILL_ASSASSIN_RUSH"


def run(tool, work, label, *args, ok=True):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True,
                            timeout=120, cwd=tool.parent.parent)
    (work / (label + ".log")).write_bytes(result.stdout + result.stderr)
    assert (result.returncode == 0) == ok, (label, result.stderr.decode(errors="replace"))
    return result


def add_text_bank(source, payload, name):
    """Append a Text entry; preserve all original BIG payloads and bank tables."""
    raw = bytearray(source)
    directory = struct.unpack_from("<I", raw, 8)[0]
    count = struct.unpack_from("<I", raw, directory)[0]
    rows = bytearray(raw[directory + 4:])
    payload_offset = len(raw)
    raw += payload
    bank_start = len(raw)
    symbol = name.encode("ascii")
    raw += struct.pack("<I", 0)
    raw += struct.pack("<7I", 0, 0, 0, len(payload), payload_offset, 0,
                       len(symbol)) + symbol
    raw += struct.pack("<III", 0, 0, 0)
    bank_length = len(raw) - bank_start
    position = 0
    found = False
    for _ in range(count):
        end = rows.index(0, position)
        bank_name = bytes(rows[position:end])
        fields = end + 1
        if bank_name == b"text":
            assert struct.unpack_from("<I", rows, fields + 4)[0] == 0
            rows[position:end] = b"Text"
            bank_id = struct.unpack_from("<I", rows, fields)[0]
            struct.pack_into("<5I", rows, fields, bank_id, 1, bank_start,
                             bank_length, 1)
            found = True
        position = fields + 20
    assert found and position == len(rows), "expected empty legacy text bank"
    new_directory = len(raw)
    raw += struct.pack("<I", count) + rows
    struct.pack_into("<I", raw, 8, new_directory)
    return bytes(raw)


def hero_name_id(root):
    data = next(e["data"] for e in read_bank(root) if e["name"] == HERO)
    tag = struct.pack("<I", crc0("Name"))
    assert data.count(tag) == 1
    offset = data.index(tag) + 4
    return struct.unpack_from("<I", data, offset)[0], data, offset


def text_id(root, name, language="English"):
    path = root / "data/lang" / language / "text.big"
    return entries(path, "TEXT_ENGLISH_MAIN", {name}, False)[name]["id"]


def minimal_texture_bank(path, wanted):
    """Build a metadata-only scratch bank; FMP links use these names and IDs."""
    retail = entries(path, "GBANK_MAIN_PC", wanted, False)
    assert set(retail) == wanted
    with path.open("rb") as stream:
        header = bytearray(stream.read(16))
    raw = bytearray(header)
    offsets = {}
    for name in sorted(wanted):
        offsets[name] = len(raw)
        raw += b"\x00"
    table_start = len(raw)
    raw += struct.pack("<I", 0)
    for name in sorted(wanted):
        symbol = name.encode("ascii")
        raw += struct.pack("<7I", 0, retail[name]["id"], 0, 1,
                           offsets[name], 0, len(symbol)) + symbol
        raw += struct.pack("<III", 0, 0, 0)
    table_length = len(raw) - table_start
    directory = len(raw)
    raw += struct.pack("<I", 1) + b"GBANK_MAIN_PC\0"
    raw += struct.pack("<5I", 0, len(wanted), table_start, table_length, 1)
    struct.pack_into("<I", raw, 8, directory)
    return bytes(raw)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fmp", required=True, type=Path)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--tool", type=Path, default=repo / "build/forge-tools.exe")
    args = parser.parse_args()
    fmp, install, tool = args.fmp.resolve(), args.install.resolve(), args.tool.resolve()
    originals = {path: hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in (fmp, install / "data/CompiledDefs/game.bin",
                              install / "data/CompiledDefs/names.bin",
                              install / "data/lang/English/text.big")}
    work = Path(tempfile.mkdtemp(prefix="fmp-new-text-links-", dir=repo / "build"))
    print("Evidence retained at", work, flush=True)
    assert len(OLD) == len(NEW)
    source = fmp.read_bytes()
    assert source.count(OLD) == 1
    metadata_only = work / "new-target.fmp"
    metadata_only.write_bytes(source.replace(OLD, NEW))
    text_source = work / "text-source"
    text_file = text_source / "data/lang/English/text.big"
    text_file.parent.mkdir(parents=True)
    run(tool, work, "add-target", "text", "set",
        install / "data/lang/English/text.big", NEW.decode(), "New hero ability text",
        "--out", text_file)
    payload = bytes(entries(text_file, "TEXT_ENGLISH_MAIN", {NEW.decode()})
                    [NEW.decode()]["payload"])
    with_text = work / "with-text.fmp"
    with_text.write_bytes(add_text_bank(metadata_only.read_bytes(), payload, NEW.decode()))
    malformed = work / "malformed-text.fmp"
    malformed.write_bytes(add_text_bank(metadata_only.read_bytes(), b"\x00", NEW.decode()))

    run(tool, work, "missing-direct", "fmp", "apply", install, metadata_only,
        work / "missing-direct", ok=False)
    assert not (work / "missing-direct/data/CompiledDefs/game.bin").exists()
    run(tool, work, "malformed-direct", "fmp", "apply", install, malformed,
        work / "malformed-direct", ok=False)
    assert not (work / "malformed-direct/data/CompiledDefs/game.bin").exists()

    original = work / "original"
    direct = work / "direct"
    one = work / "one-mod"
    run(tool, work, "original", "fmp", "apply", install, fmp, original)
    run(tool, work, "direct", "fmp", "apply", install, with_text, direct)
    run(tool, work, "one-mod", "mods", "merge", install, one, "--with", with_text)
    direct_id, direct_data, offset = hero_name_id(direct)
    original_id, original_data, _ = hero_name_id(original)
    assert direct_id == text_id(direct, NEW.decode())
    assert direct_data == original_data[:offset] + struct.pack("<I", direct_id) + original_data[offset + 4:]
    assert original_id != direct_id
    for bank in ("game.bin", "names.bin"):
        assert (direct / "data/CompiledDefs" / bank).read_bytes() == \
               (one / "data/CompiledDefs" / bank).read_bytes(), bank
    assert direct_id == text_id(one, NEW.decode())

    extra = work / "extra-source"
    extra_text = extra / "data/lang/English/text.big"
    extra_text.parent.mkdir(parents=True)
    run(tool, work, "add-earlier", "text", "set",
        install / "data/lang/English/text.big", EXTRA, "Earlier independent text",
        "--out", extra_text)
    shifted = work / "shifted"
    run(tool, work, "shifted", "mods", "merge", install, shifted,
        "--with", extra, "--with", with_text)
    shifted_id, _, _ = hero_name_id(shifted)
    assert text_id(shifted, EXTRA) == direct_id
    assert shifted_id == direct_id + 1 == text_id(shifted, NEW.decode())

    external = work / "external-text"
    run(tool, work, "external", "mods", "merge", install, external,
        "--with", text_source, "--with", metadata_only)
    assert hero_name_id(external)[0] == text_id(external, NEW.decode())

    # Two language banks must receive the same ID; a collision or existing
    # mismatched ID refuses before fmp apply writes any output bank.
    dual = work / "two-languages"
    defs = dual / "data/CompiledDefs"
    defs.mkdir(parents=True)
    for bank in ("game.bin", "names.bin", "script.bin", "frontend.bin"):
        src = install / "data/CompiledDefs" / bank
        if src.exists():
            shutil.copyfile(src, defs / bank)
    for language in ("English", "Test"):
        path = dual / "data/lang" / language / "text.big"
        path.parent.mkdir(parents=True)
        shutil.copyfile(install / "data/lang/English/text.big", path)
    link_report = json.loads(run(tool, work, "source-links", "fmp", "links",
                                 fmp, "--json").stdout)
    icons = {link["target"] for record in link_report["records"]
             for link in record["links"]
             if link["path"].endswith(".Hero Ability Hud Icon ID")}
    texture = dual / "data/graphics/pc/textures.big"
    texture.parent.mkdir(parents=True)
    texture.write_bytes(minimal_texture_bank(
        install / "data/graphics/pc/textures.big", icons))
    dual_out = work / "dual-output"
    run(tool, work, "dual", "fmp", "apply", dual, with_text, dual_out)
    assert hero_name_id(dual_out)[0] == text_id(dual_out, NEW.decode())
    assert text_id(dual_out, NEW.decode(), "Test") == direct_id

    collision = work / "collision"
    shutil.copytree(dual, collision)
    run(tool, work, "add-collision", "text", "set",
        dual / "data/lang/Test/text.big", "TEXT_FORGE_COLLIDER", "collision",
        "--id", direct_id, "--out", collision / "data/lang/Test/text.big")
    failed = work / "collision-output"
    run(tool, work, "collision", "fmp", "apply", collision, with_text, failed,
        ok=False)
    assert not (failed / "data/CompiledDefs/game.bin").exists()

    mismatch = work / "mismatch"
    shutil.copytree(dual, mismatch)
    run(tool, work, "change-locale-id", "text", "set",
        dual / "data/lang/Test/text.big", OLD.decode(), "localized baseline",
        "--id", direct_id, "--out", mismatch / "data/lang/Test/text.big")
    failed = work / "mismatch-output"
    result = run(tool, work, "mismatch", "fmp", "apply", mismatch, fmp,
                 failed, ok=False)
    assert b"has no matching TextID" in result.stderr
    assert not (failed / "data/CompiledDefs/game.bin").exists()

    failed = work / "locale-shift-output"
    result = run(tool, work, "locale-shift", "mods", "merge", dual, failed,
                 "--with", extra, "--with", with_text, ok=False)
    assert b"has no matching TextID" in result.stderr
    assert not (failed / "data/CompiledDefs/game.bin").exists()
    aligned_extra = work / "aligned-extra"
    shutil.copytree(extra, aligned_extra)
    aligned_test = aligned_extra / "data/lang/Test/text.big"
    aligned_test.parent.mkdir(parents=True)
    shutil.copyfile(extra_text, aligned_test)
    aligned = work / "aligned-output"
    run(tool, work, "aligned", "mods", "merge", dual, aligned,
        "--with", aligned_extra, "--with", with_text)
    assert hero_name_id(aligned)[0] == direct_id + 1
    assert text_id(aligned, NEW.decode()) == text_id(aligned, NEW.decode(), "Test") == direct_id + 1

    picks = work / "picks.txt"
    picks.write_text("text:English|" + NEW.decode() + "\tvanilla\n")
    refused = work / "picked-vanilla"
    result = run(tool, work, "picked-vanilla", "mods", "merge", install,
                 refused, "--with", text_source, "--with", metadata_only,
                 "--picks", picks, ok=False)
    assert b"missing target" in result.stderr
    assert not (refused / "data/CompiledDefs/game.bin").exists()

    assert all(hashlib.sha256(path.read_bytes()).hexdigest() == digest
               for path, digest in originals.items())
    report = {"direct_text_id": direct_id, "shifted_text_id": shifted_id,
              "original_hero_text_id": original_id, "checks": 13,
              "gameplay_tested": False}
    (work / "result.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("FAIL:", exc, file=sys.stderr)
        raise
