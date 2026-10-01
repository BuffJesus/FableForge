#!/usr/bin/env python3
"""Exercise staged lip sync edits and scratch export through the GUI."""

import argparse
import json
import pathlib
import shutil
import struct
import subprocess
import tempfile


def read_u32(stream):
    data = stream.read(4)
    if len(data) != 4:
        raise ValueError("short BIG integer")
    return struct.unpack("<I", data)[0]


def read_cstring(stream):
    data = bytearray()
    while (byte := stream.read(1)) not in (b"\x00", b"\xff", b""):
        data += byte
    return data.decode("ascii")


def lip_frame_count(path, bank_name, sound_id, first_weight=False):
    with open(path, "rb") as stream:
        if stream.read(4) != b"BIGB":
            raise ValueError("expected retail BIGB archive")
        read_u32(stream)
        directory = read_u32(stream)
        stream.seek(directory)
        count = read_u32(stream)
        starts = {}
        for _ in range(count):
            name = read_cstring(stream)
            read_u32(stream)
            entries = read_u32(stream)
            start = read_u32(stream)
            read_u32(stream)
            read_u32(stream)
            starts[name] = (start, entries)
        start, entries = starts[bank_name]
        stream.seek(start)
        stream.seek(read_u32(stream) * 8, 1)
        for _ in range(entries):
            read_u32(stream)  # magic
            record_id = read_u32(stream)
            read_u32(stream)  # type
            read_u32(stream)  # payload length
            payload = read_u32(stream)
            read_u32(stream)  # dev file type
            stream.seek(read_u32(stream), 1)  # name
            read_u32(stream)  # dev CRC
            for _ in range(read_u32(stream)):
                stream.seek(read_u32(stream), 1)
            stream.seek(read_u32(stream), 1)  # Info
            if record_id != sound_id:
                continue
            stream.seek(payload)
            for _ in range(read_u32(stream)):
                if len(stream.read(1)) != 1:
                    raise ValueError("short viseme ID")
                read_cstring(stream)
            read_u32(stream)  # frames per second
            count = read_u32(stream)
            if first_weight:
                keys = stream.read(1)
                if keys == b"\x00" or not keys or len(stream.read(1)) != 1:
                    raise ValueError("first lip sync frame has no key")
                weight = stream.read(1)
                if len(weight) != 1:
                    raise ValueError("short first key weight")
                return weight[0]
            return count
        raise ValueError(f"missing {bank_name}/{sound_id}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--install", required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    source = pathlib.Path(args.install) / "data" / "lang" / "English" / "dialogue.big"
    with tempfile.TemporaryDirectory(prefix="fableforge_dialogue_edit_") as temp:
        output = pathlib.Path(temp) / "dialogue.big"
        script = pathlib.Path(temp) / "edit.txt"
        script.write_text("\n".join([
            "wait_maps", "wait_ready", "assets_tab 4", "frames 3",
            "dialogue_select 0 2", "click button_dialogue_load", "frames 3",
            "assert_state dialogue_frames 45",
            "reveal slider_dialogue_key_0", "assert_widget slider_dialogue_key_0",
            "click slider_dialogue_key_0", "frames 2",
            "assert_state dialogue_staged 1",
            "reveal button_dialogue_insert_frame", "click button_dialogue_insert_frame",
            "frames 2", "assert_state dialogue_frames 46",
            "assert_state dialogue_staged 1",
            "screenshot build/ui/dialogue_edit.png",
            "dialogue_select 3 3", "click button_dialogue_load", "frames 3",
            "reveal button_dialogue_insert_frame", "click button_dialogue_insert_frame",
            "frames 2", "assert_state dialogue_staged 2",
            "dialogue_select 0 2", "click button_dialogue_load", "frames 3",
            "assert_state dialogue_frames 46",
            f"dialogue_export_path {output}",
            "reveal button_dialogue_export", "click button_dialogue_export",
            "frames 3", "assert_state dialogue_exported 1",
            "reveal button_dialogue_reset_line", "click button_dialogue_reset_line",
            "frames 2", "assert_state dialogue_frames 45",
            "assert_state dialogue_staged 1",
            "close", "frames 2", "assert_state close_prompt 1",
            "click btn_unsaved_save", "frames 2",
            "assert_state close_prompt 0", "assert_state dialogue_staged 1",
            "assert_state assets_tab 4",
            "close", "frames 2", "assert_state close_prompt 1",
            "click btn_unsaved_cancel", "frames 2",
            "assert_state close_prompt 0", "assert_state dialogue_staged 1",
            "quit", "",
        ]), encoding="utf-8")
        subprocess.run([str(root / "build" / "FableForge.exe"), "--auto", str(script),
                        "--install", str(args.install), "--size", "1440x860"],
                       cwd=root, check=True, timeout=90)
        log = pathlib.Path(str(script) + ".log").read_text(encoding="utf-8")
        if "RESULT PASS" not in log or not output.is_file():
            raise AssertionError(log[-2500:])
        # The archive reader below checks both edited sub-banks independently.
        for bank, sound in (("LIPSYNC_ENGLISH_MAIN", 2),
                            ("LIPSYNC_ENGLISH_SCRIPT_2", 3)):
            before = lip_frame_count(source, bank, sound)
            after = lip_frame_count(output, bank, sound)
            if after != before + 1:
                raise AssertionError(f"{bank}/{sound}: {before} -> {after} frames")
        pack = pathlib.Path(temp) / "LipPack"
        pack.mkdir()
        (pack / "forge_pack.json").write_text(json.dumps({
            "version": 1, "name": "Lip Test", "models": [], "groundThemes": []
        }), encoding="utf-8")
        pack_script = pathlib.Path(temp) / "pack.txt"
        pack_script.write_text("\n".join([
            "wait_maps", "wait_ready", "assets_tab 4", "frames 3",
            "dialogue_select 0 2", "click button_dialogue_load", "frames 3",
            "reveal slider_dialogue_key_0", "click slider_dialogue_key_0",
            "frames 2", "assert_state dialogue_staged 1",
            f"pack_dest {pack}",
            "reveal button_dialogue_add_pack", "click button_dialogue_add_pack",
            "frames 2", "assert_state dialogue_staged 0",
            "assert_state dialogue_pack_added 1", "quit", "",
        ]), encoding="utf-8")
        subprocess.run([str(root / "build" / "FableForge.exe"), "--auto", str(pack_script),
                        "--install", str(args.install), "--size", "1440x860"],
                       cwd=root, check=True, timeout=90)
        pack_log = pathlib.Path(str(pack_script) + ".log").read_text(encoding="utf-8")
        if "RESULT PASS" not in pack_log:
            raise AssertionError(pack_log[-2500:])
        manifest = json.loads((pack / "forge_pack.json").read_text(encoding="utf-8"))
        if len(manifest.get("lipSync", [])) != 1 or manifest["lipSync"][0]["soundId"] != 2:
            raise AssertionError("GUI pack recipe missing")
        base = pathlib.Path(temp) / "base"
        base_archive = base / "data" / "lang" / "English" / "dialogue.big"
        base_archive.parent.mkdir(parents=True)
        shutil.copyfile(source, base_archive)
        defs = base / "data" / "CompiledDefs"
        defs.mkdir(parents=True)
        for name in ("game.bin", "names.bin"):
            shutil.copyfile(pathlib.Path(args.install) / "data" / "CompiledDefs" / name,
                            defs / name)
        cli = root / "build" / "forge-tools.exe"
        subprocess.run([str(cli), "mods", "add", str(base), str(pack)],
                       check=True, capture_output=True, text=True, timeout=30)
        built = pathlib.Path(temp) / "built"
        result = subprocess.run([str(cli), "mods", "build", str(base), str(built), "--json"],
                                capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError(f"mods build failed: {result.stdout[-1500:]}\n{result.stderr[-1500:]}")
        report = json.loads(result.stdout[result.stdout.index("{"):])
        if not any("dialogue" in item for row in report.get("forge", [])
                   for item in row.get("added", [])):
            raise AssertionError("mods build did not report a dialogue recipe")
        built_archive = built / "data" / "lang" / "English" / "dialogue.big"
        want = manifest["lipSync"][0]["frames"][0][0][1]
        got = lip_frame_count(built_archive, "LIPSYNC_ENGLISH_MAIN", 2,
                              first_weight=True)
        if got != want:
            raise AssertionError(f"mods build wrote first weight {got}; recipe wants {want}")
        second = pathlib.Path(temp) / "SecondPack"
        second.mkdir()
        second_manifest = json.loads(json.dumps(manifest))
        second_manifest["name"] = "Second"
        other_weight = (want + 1) % 256
        second_manifest["lipSync"][0]["frames"][0][0][1] = other_weight
        (second / "forge_pack.json").write_text(json.dumps(second_manifest),
                                                 encoding="utf-8")
        subprocess.run([str(cli), "mods", "add", str(base), str(second),
                        "--name", "Second"], check=True, capture_output=True,
                       text=True, timeout=30)
        contested = pathlib.Path(temp) / "contested"
        conflict_result = subprocess.run([str(cli), "mods", "build", str(base),
                                          str(contested), "--json"], check=True,
                                         capture_output=True, text=True, timeout=90)
        conflict_report = json.loads(conflict_result.stdout[conflict_result.stdout.index("{"):])
        rows = conflict_report.get("lip_sync", {}).get("contested", [])
        if len(rows) != 1 or rows[0]["winner"] != "Second" or \
           rows[0]["soundId"] != 2 or len(rows[0]["mods"]) != 2 or \
           conflict_report["summary"]["lip_sync_contested"] != 1:
            raise AssertionError(f"lip sync conflict report: {rows}")
        if lip_frame_count(contested / "data" / "lang" / "English" / "dialogue.big",
                           "LIPSYNC_ENGLISH_MAIN", 2, first_weight=True) != other_weight:
            raise AssertionError("later lip sync recipe did not win")
        conflict_script = pathlib.Path(temp) / "conflict_gui.txt"
        conflict_script.write_text("\n".join([
            "wait_maps", "wait_ready", f"set saveroot {base}", "mods_tab 1",
            "frames 2", "mods_conflicts", "wait_mods", "frames 3",
            "assert_state mods_conflicts 1",
            "screenshot build/ui/dialogue_mod_conflict.png", "quit", "",
        ]), encoding="utf-8")
        subprocess.run([str(root / "build" / "FableForge.exe"), "--auto",
                        str(conflict_script), "--install", str(args.install)],
                       cwd=root, check=True, timeout=90)
        conflict_log = pathlib.Path(str(conflict_script) + ".log").read_text(encoding="utf-8")
        if "RESULT PASS" not in conflict_log:
            raise AssertionError(conflict_log[-2500:])
        subprocess.run([str(cli), "mods", "move", str(base), "Second", "0"],
                       check=True, capture_output=True, text=True, timeout=30)
        reverse_recipe = pathlib.Path(temp) / "reverse_recipe"
        reverse_result = subprocess.run([str(cli), "mods", "build", str(base),
                                         str(reverse_recipe), "--json"], check=True,
                                        capture_output=True, text=True, timeout=90)
        reverse_report = json.loads(reverse_result.stdout[reverse_result.stdout.index("{"):])
        if reverse_report["lip_sync"]["contested"][0]["winner"] != rows[0]["mods"][0] or \
           lip_frame_count(reverse_recipe / "data" / "lang" / "English" / "dialogue.big",
                           "LIPSYNC_ENGLISH_MAIN", 2, first_weight=True) != want:
            raise AssertionError("reordered lip sync conflict has the wrong winner")
        subprocess.run([str(cli), "mods", "remove", str(base), "Second"],
                       check=True, capture_output=True, text=True, timeout=30)
        whole_pack = pathlib.Path(temp) / "WholePack"
        whole_archive = whole_pack / "data" / "lang" / "English" / "dialogue.big"
        whole_archive.parent.mkdir(parents=True)
        shutil.copyfile(output, whole_archive)
        (whole_pack / "forge_pack.json").write_text(json.dumps({
            "version": 1, "name": "Whole Test", "models": [], "groundThemes": []
        }), encoding="utf-8")
        subprocess.run([str(cli), "mods", "add", str(base), str(whole_pack),
                        "--name", "Whole"], check=True, capture_output=True,
                       text=True, timeout=30)
        mixed = pathlib.Path(temp) / "mixed"
        mixed_result = subprocess.run([str(cli), "mods", "build", str(base),
                                       str(mixed), "--json"], check=True,
                                      capture_output=True, text=True, timeout=90)
        mixed_report = json.loads(mixed_result.stdout[mixed_result.stdout.index("{"):])
        if not any("later whole-file dialogue.big wins" in note
                   for row in mixed_report.get("forge", [])
                   for note in row.get("notes", [])):
            raise AssertionError("mods build did not explain the skipped recipe")
        if lip_frame_count(mixed / "data" / "lang" / "English" / "dialogue.big",
                           "LIPSYNC_ENGLISH_MAIN", 2) != 46:
            raise AssertionError("later whole-file archive did not win")
        subprocess.run([str(cli), "mods", "move", str(base), "Whole", "0"],
                       check=True, capture_output=True, text=True, timeout=30)
        reverse = pathlib.Path(temp) / "reverse"
        subprocess.run([str(cli), "mods", "build", str(base), str(reverse), "--json"],
                       check=True, capture_output=True, text=True, timeout=90)
        if lip_frame_count(reverse / "data" / "lang" / "English" / "dialogue.big",
                           "LIPSYNC_ENGLISH_MAIN", 2) != 45:
            raise AssertionError("later lip sync recipe did not win")
        print("dialogue edits: two-bank export, recipe conflict and pack order verified")


if __name__ == "__main__":
    main()
