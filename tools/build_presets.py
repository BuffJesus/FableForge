#!/usr/bin/env python3
"""Rebuild the shipped presets (presets/*.preset.tng) from retail TNG text.

Each preset is a spatial cluster of Object things around an anchor definition
in one retail map: the blocks are copied verbatim (retail spelling; positions
are the source map's, the editor re-centres them on paste). Markers, cameras
and creatures are left out on purpose: creatures carry a world-space
InitialPos and villagers a village membership, which only the placer can set
right for the target map.

The source TNGs come from a scratch tree the GUI wrote (`save_level` after a
throwaway edit) or any loose data/Levels/FinalAlbion/<map>.tng:

  python tools/build_presets.py --src build/preset_src/data/Levels/FinalAlbion
"""
import argparse, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PRESETS = [
    # slug, name, description, map, anchor definition, radius (map units), keep-types, definition prefixes ("" = any)
    ("bandit_camp_fire", "Bandit camp fire", "Firewood, bar, barrels, kegs and buckets around a bandit camp fire (BanditCampMain). Add an enemy spawner from the Actors tab.",
     "BanditCampMain", "OBJECT_BANDIT_FIREWOOD_02", 9.0, ("Object",), ("",)),
    ("oakvale_fence_gate", "Oakvale fence and gate", "A run of Oakvale fence with its gate, posts and a bit of low wall (StartOakValeWest).",
     "StartOakValeWest", "OBJECT_OAKVALE_FENCEGATE_04", 7.0, ("Object",), ("OBJECT_OAKVALE_FENCE", "OBJECT_WALL_SMALL")),
    ("cottage_furniture", "Cottage furniture", "Table with stools, chairs, lamp, bookshelf, rugs and bed from an Oakvale cottage (StartOakValeWest).",
     "StartOakValeWest", "OBJECT_HOME_TABLE_3_STOOLS", 5.0, ("Object",), ("OBJECT_HOME", "OBJECT_CHAIR", "OBJECT_TABLE", "OBJECT_BS_", "OBJECT_BOOKSHELF", "OBJECT_RUG", "OBJECT_KHG_BED", "OBJECT_FLOWER", "OBJECT_CUPBOARD")),
    ("graveyard_corner", "Graveyard corner", "Graves, surrounds, a Celtic cross, railing and a bit of wall from the Oakvale memorial garden.",
     "OakvaleMemorialGarden_v2", "OBJECT_LARGE_CELTIC_CROSS_01", 8.0, ("Object",), ("",)),
]

BLOCK = re.compile(r"(NewThing (\w+);.*?EndThing;\r?\n)", re.S)


def parse(text):
    out = []
    for m in BLOCK.finditer(text):
        block, kind = m.group(1), m.group(2)
        d = re.search(r'DefinitionType "([^"]+)"', block)
        px = re.search(r"PositionX (-?[\d.]+);", block)
        py = re.search(r"PositionY (-?[\d.]+);", block)
        out.append((kind, d.group(1) if d else "", float(px.group(1)) if px else None, float(py.group(1)) if py else None, block))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join("build", "preset_src", "data", "Levels", "FinalAlbion"))
    ap.add_argument("--out", default=os.path.join(ROOT, "presets"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    ok = True
    for slug, name, desc, level, anchor, radius, kinds, prefixes in PRESETS:
        path = os.path.join(a.src, level + ".tng")
        if not os.path.exists(path):
            print(f"missing {path}"); ok = False; continue
        things = parse(open(path, encoding="latin-1").read())
        anchors = [t for t in things if t[1] == anchor and t[2] is not None]
        if not anchors:
            print(f"{slug}: no {anchor} in {level}"); ok = False; continue
        # the anchor with the most Objects around it
        best, bestSet = None, []
        for an in anchors:
            near = [t for t in things if t[0] in kinds and t[2] is not None and (t[2] - an[2]) ** 2 + (t[3] - an[3]) ** 2 <= radius * radius
                    and any(t[1].startswith(p) for p in prefixes)]
            if len(near) > len(bestSet): best, bestSet = an, near
        # skip the unbreakable-chunk definitions that only make sense in their map
        bestSet = [t for t in bestSet if not t[1].startswith(("OBJECT_COIN_GOLF", "OBJECT_OV_VILLAGER_CHARRED"))]
        with open(os.path.join(a.out, slug + ".preset.tng"), "w", encoding="latin-1", newline="") as f:
            f.write(f"// Albion Atlas preset: {name}\r\n// {desc}\r\nVersion 2;\r\nXXXSectionStart NULL;\r\n")
            for t in bestSet:
                f.write(t[4])
            f.write("XXXSectionEnd;\r\n")
        defs = sorted({t[1] for t in bestSet})
        print(f"{slug}: {len(bestSet)} things around {anchor} at ({best[2]:.1f}, {best[3]:.1f}): {', '.join(defs)}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
