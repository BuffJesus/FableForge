#!/usr/bin/env python3
"""Sync vendor/forgecore from the FableForge working tree.

Copies libs/forgecore (headers + sources) and the header-only third-party
libraries it needs, then records the source commit in vendor/VENDORED.md.

Kept local (never overwritten):
  * vendor/forgecore/src/lzo.cpp  -- MIT clean-room LZO1X backend (src/lzo1x.*)
    instead of GPL minilzo. The forge::lzo API is identical.
Skipped:
  * audio.cpp/.hpp (miniaudio; the editor has no audio)

Run:  python tools/sync_forgecore.py [--source D:/Code/FableForge]
"""
from __future__ import annotations

import argparse
import filecmp
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "vendor" / "forgecore"
KEEP_LOCAL = {"src/lzo.cpp"}
SKIP = {"src/audio.cpp", "include/forge/audio.hpp"}
THIRD_PARTY = {"nlohmann": ["json.hpp"], "miniz": ["miniz.c", "miniz.h"]}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default="D:/Code/FableForge")
    ap.add_argument("--check", action="store_true", help="report differences, copy nothing")
    a = ap.parse_args()
    src = Path(a.source)
    lib = src / "libs" / "forgecore"
    if not lib.is_dir():
        print(f"no forgecore at {lib}", file=sys.stderr)
        return 2

    changed: list[str] = []
    files = sorted(p for p in lib.rglob("*") if p.is_file())
    for p in files:
        rel = p.relative_to(lib).as_posix()
        if rel in SKIP or rel in KEEP_LOCAL:
            continue
        dst = DEST / rel
        if dst.exists() and filecmp.cmp(p, dst, shallow=False):
            continue
        changed.append(rel)
        if not a.check:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, dst)
    # files that disappeared upstream
    for p in sorted(q for q in DEST.rglob("*") if q.is_file()):
        rel = p.relative_to(DEST).as_posix()
        if rel in KEEP_LOCAL or rel in SKIP:
            continue
        if not (lib / rel).exists():
            changed.append("(removed) " + rel)
            if not a.check:
                p.unlink()
    for name, names in THIRD_PARTY.items():
        for n in names:
            p = src / "third_party" / name / n
            dst = ROOT / "vendor" / "third_party" / name / n
            if p.exists() and not (dst.exists() and filecmp.cmp(p, dst, shallow=False)):
                changed.append(f"third_party/{name}/{n}")
                if not a.check:
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(p, dst)

    commit = subprocess.run(["git", "-C", str(src), "rev-parse", "--short", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", str(src), "status", "--porcelain", "libs/forgecore"],
                           capture_output=True, text=True).stdout.strip() != ""
    stamp = f"`{commit}`" + (" (working tree, uncommitted forgecore changes)" if dirty else "")
    if not a.check:
        md = ROOT / "vendor" / "VENDORED.md"
        text = md.read_text(encoding="utf-8")
        text = re.sub(r"at commit `[0-9a-f]+`[^\n]*", f"at commit {stamp}", text, count=1)
        md.write_text(text, encoding="utf-8")
    for c in changed:
        print(("would copy " if a.check else "copied ") + c)
    print(f"{len(changed)} file(s) {'differ' if a.check else 'updated'}; source {stamp}")
    return 1 if (a.check and changed) else 0


if __name__ == "__main__":
    sys.exit(main())
