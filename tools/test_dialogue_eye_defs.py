"""Compare Dialogue head eye presets with the installed retail creature defs."""
import argparse
import json
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--install", required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
forge = root / "build/forge-tools.exe"
schema = root / "docs/re_reference/def_schema.json"

# Def name, body mesh ID, EyeGraphic mesh ID and RenderSizeX.
expected = {
    "CREATURE_BANDIT_LIEUTENANT": (4520, 8112, 1.21),
    "CREATURE_BS_VILLAGER_FEMALE": (5120, 8112, 1.34),
    "CREATURE_BS_VILLAGER_MALE": (5149, 8112, 1.34),
    "CREATURE_BS_VILLAGER_BOY": (5109, 8112, 1.30),
}
for name, (body_id, eye_id, size) in expected.items():
    result = subprocess.run(
        [forge, "defs", "decode", args.install, schema, name, "game.bin", "--json"],
        check=True, text=True, capture_output=True,
    )
    entry = json.loads(result.stdout)
    assert entry["name"] == name and entry["all_tags_ok"] and not entry["leftover"], name
    graphic = bytes.fromhex(next(f["value"] for f in entry["fields"] if f["name"] == "Graphic"))
    body_type, actual_body = struct.unpack_from("<Ii", graphic)
    tag = graphic.find(bytes.fromhex("7d74f392"))  # def.xml: EyeGraphic
    assert tag >= 0, f"{name}: no EyeGraphic"
    eye_type, actual_eye, _, actual_size, _ = struct.unpack_from("<IiIfB", graphic, tag + 4)
    assert (body_type, actual_body, eye_type, actual_eye) == (4, body_id, 5, eye_id), name
    assert abs(actual_size - size) < 1e-5, f"{name}: eye size {actual_size} != {size}"
    print(f"{name}: body {actual_body}, eye {actual_eye}, size {actual_size:.2f}")
print("PASS: four Dialogue eye presets match installed game.bin")
