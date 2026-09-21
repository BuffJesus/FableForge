#!/usr/bin/env python3
"""The 1.0 release's in-game probes (docs/RELEASE.md step 3), one command, against the real install.

Every probe deploys with the shipped exes, runs the retail game through tools/ingame/ingame_terrain_test.py
on a FRESH game (saves cache a map's entities and the region table -- docs/ENGINE_RULES.md), reads the
harness report, and the last stage puts the install back (forge restore) and proves it (forge backups
-> 0 differ). The game must be closed; every writer refuses while it runs.

Stages (default: all, in this order):
  things   in StartOakValeWest: a retextured barrel (BARREL_BRACED_1_24 tinted through forge texture-replace),
           a placed preset (Cottage furniture), a placed particle emitter, a placed fishing spot; deployed
           from the GUI's scripted mode; the harness finds the named things at their .tng positions and
           screenshots the spot with the hero standing on it
  compact  forge compact-stb on the live bank, then one full session (heights probe) on the compacted bank
  region   a blank level with its OWN region installed (forge blank-level --own-region new), a fresh game,
           a real region load into it (ForgeFSE's GoToMapSlotRetailTransition); the probe logs GetRegionName
           before and after. The map-screen click itself is the one thing left to a human.
  restore  forge restore, then forge backups must report 0 differ (always run last)
  mesh     (not default) a custom static mesh from forge mesh-import placed in StartOakValeWest, found by the
           harness, screenshotted; `mesh_undo` puts back only the files it touched (a staged mod bundle stays)

  python tools/ingame/release_probes.py [--stage things,compact,region,restore] [--dry-run] [--keep]
Reports: build/ingame/release/<stage>/report.json + the harness screenshots; summary printed at the end.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FORGE = ROOT / "build" / "forge.exe"
GUI = ROOT / "build" / "FableForge.exe"
HARNESS = ROOT / "tools" / "ingame" / "ingame_terrain_test.py"
OUT = ROOT / "build" / "ingame" / "release"
START_MAP = "StartOakValeWest"
CENTRE = (64.0, 112.0)          # the harness's default probe centre in the childhood map (walkable, near the start)
BARREL_TEX = "BARREL_BRACED_1_24"
EMITTER_FX = "BRAZIERFIREFINAL"
OWN_LEVEL = "ProbeOwnRegion"
OWN_SIZE = 64

DRY = False


def sh(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    print("  $", " ".join(str(c) for c in cmd), flush=True)
    if DRY:
        return subprocess.CompletedProcess(cmd, 0, "", "")
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)


def game_running() -> bool:
    r = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Fable.exe"], capture_output=True, text=True)
    return "Fable.exe" in r.stdout


def backups_differ() -> int | None:
    """Files FableForge's own writers changed (EDIT / NEW rows). A staged mod bundle (MODS rows) or an
    overlay (OVR) is somebody else's deploy and is not what a probe has to put back."""
    r = sh([FORGE, "backups"])
    if DRY: return 0
    return sum(1 for l in r.stdout.splitlines() if re.match(r"\s*(EDIT|NEW )\s", l))


def run_gui_script(name: str, lines: list[str]) -> bool:
    script = OUT / f"{name}.txt"
    script.parent.mkdir(parents=True, exist_ok=True)
    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
    r = sh([GUI, "--auto", script])
    log = script.with_suffix(".txt.log")
    if DRY:
        return True
    tail = log.read_text(errors="ignore").splitlines()[-6:] if log.exists() else []
    print("    " + "\n    ".join(tail))
    return r.returncode == 0 and any("RESULT PASS" in l for l in tail)


def run_harness(stage: str, args: list[str]) -> dict:
    report = OUT / stage / "report.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    r = sh([sys.executable, HARNESS, "--report", report, *args])
    if DRY:
        return {"ok": True, "dry": True}
    if r.stdout.strip():
        print("    " + "\n    ".join(r.stdout.strip().splitlines()[-8:]))
    if r.stderr.strip():
        print("    stderr: " + r.stderr.strip().splitlines()[-1])
    try:
        return json.loads(report.read_text())
    except Exception as e:
        return {"ok": False, "error": f"no report: {e}"}


def probe_log_lines(stage: str, key: str) -> list[str]:
    p = OUT / stage / "fse_probe.log"
    if not p.exists():
        return []
    return [l.split("ATLAS_PROBE|", 1)[1] for l in p.read_text(errors="ignore").splitlines() if f"ATLAS_PROBE|{key}|" in l]


# ---------------------------------------------------------------- stages

def stage_things() -> dict:
    out: dict = {"stage": "things"}
    # 1. the retextured barrel: export the retail slot, tint it, write it back (slot keeps size + format)
    png = OUT / "things" / "barrel.png"
    tinted = OUT / "things" / "barrel_tinted.png"
    png.parent.mkdir(parents=True, exist_ok=True)
    r = sh([FORGE, "texture-export", BARREL_TEX, png])
    if r.returncode != 0 and not DRY:
        return {**out, "ok": False, "error": "texture-export: " + r.stderr.strip()}
    if not DRY:
        from PIL import Image
        im = Image.open(png).convert("RGBA")
        px = im.load()
        w, h = im.size
        for y in range(h):          # an unmistakable tint: red channel up, green down, keeping the wood grain
            for x in range(w):
                rr, gg, bb, aa = px[x, y]
                px[x, y] = (min(255, rr + 90), gg // 2, bb // 2, aa)
        im.save(tinted)
    r = sh([FORGE, "texture-replace", BARREL_TEX, tinted])
    if r.returncode != 0 and not DRY:
        return {**out, "ok": False, "error": "texture-replace: " + r.stderr.strip()}
    out["texture"] = r.stdout.strip().splitlines()[-1:] if r.stdout else []

    # 2. the placements, from the GUI's scripted mode, deployed into the live install
    cx, cy = CENTRE
    ok = run_gui_script("things_place", [
        "wait_maps", "wait_ready", f"select {START_MAP}", "wait_loaded", "wait_foliage",
        "edit 1", "frames 2", "assert_state doc_loaded 1",
        f"camera {cx:g} {cy:g} 20 0.8 -60 24", "frames 2",
        "place OBJECT_BARREL_BREAKABLE ProbeBarrel", "frames 2",
        f"camera {cx + 4:g} {cy + 3:g} 20 0.8 -60 24", "frames 2",
        "place_emitter " + EMITTER_FX + " ProbeFx", "frames 2",
        f"camera {cx - 4:g} {cy + 3:g} 20 0.8 -60 24", "frames 2",
        "place_fishing_spot - ProbeFish", "frames 2",
        f"camera {cx:g} {cy + 8:g} 20 0.8 -60 24", "frames 2",
        "preset_place Cottage furniture", "frames 2",
        "save_level", "frames 2", "deploy_level", "frames 2", "assert_state doc_dirty 0",
        f"screenshot {OUT / 'things' / 'editor_after_deploy.png'}",
        "quit",
    ])
    if not ok:
        return {**out, "ok": False, "error": "GUI placement/deploy script failed (see build/ingame/release/things_place.txt.log)"}

    # 3. a fresh game finds each named thing where the .tng says; the hero stands on the spot for the shot
    rep = run_harness("things", ["--map", START_MAP, "--new-game", "--teleport", "--centre", f"{cx:g},{cy:g}",
                                 "--things", "ProbeBarrel,ProbeFx,ProbeFish"])
    out["things"] = rep.get("things")
    out["hero_after_teleport"] = rep.get("hero_after_teleport")
    out["notes"] = rep.get("notes")
    found = rep.get("things") or {}
    out["ok"] = rep.get("dry") or (bool(rep.get("ok")) and all(v.get("ok") for v in found.values()) and len(found) == 3)
    out["shot"] = str(OUT / "things" / "04_after_probe.png")
    out["manual"] = "the preset (no ScriptName) and the barrel's tint are judged from the screenshot"
    return out


def stage_compact() -> dict:
    out: dict = {"stage": "compact"}
    r = sh([FORGE, "compact-stb"])
    if r.returncode != 0 and not DRY:
        return {**out, "ok": False, "error": "compact-stb: " + (r.stderr.strip() or r.stdout.strip()[-300:])}
    out["compact"] = r.stdout.strip().splitlines()[-3:] if r.stdout else []
    cx, cy = CENTRE
    rep = run_harness("compact", ["--map", START_MAP, "--new-game", "--centre", f"{cx:g},{cy:g}"])
    out["compared"] = rep.get("compared"); out["worst_delta"] = rep.get("worst_delta"); out["notes"] = rep.get("notes")
    out["ok"] = bool(rep.get("ok"))
    return out


def stage_region() -> dict:
    out: dict = {"stage": "region"}
    r = sh([FORGE, "blank-level", OWN_LEVEL, "--size", f"{OWN_SIZE}x{OWN_SIZE}", "--height", "12",
            "--theme", "GROUND_FOREST_LEAVES", "--own-region", "new", "--display", "Probe Own Region"])
    if r.returncode != 0 and not DRY:
        return {**out, "ok": False, "error": "blank-level: " + (r.stderr.strip() or r.stdout.strip()[-400:])}
    out["install"] = r.stdout.strip().splitlines()[-4:] if r.stdout else []
    r = sh([FORGE, "entrance", OWN_LEVEL])
    out["entrance"] = r.stdout.strip().splitlines()[-2:] if r.stdout else []
    c = OWN_SIZE / 2
    rep = run_harness("region", ["--map", OWN_LEVEL, "--start-map", START_MAP, "--new-game", "--teleport", "--transition",
                                 "--centre", f"{c:g},{c:g}", "--radius", "8", "--step", "4"])
    out["region_before"] = probe_log_lines("region", "region0")
    out["region_after"] = probe_log_lines("region", "region")
    out["transition"] = probe_log_lines("region", "transition")
    out["compared"] = rep.get("compared"); out["worst_delta"] = rep.get("worst_delta"); out["notes"] = rep.get("notes")
    arrived = any(OWN_LEVEL.lower() in l.lower() or "probe" in l.lower() for l in out["region_after"])
    out["ok"] = rep.get("dry") or (bool(rep.get("ok")) and (arrived or bool(out["transition"])))
    out["manual"] = "map-screen travel through the entrance is the human check (docs/RELEASE.md step 3)"
    return out


def stage_restore() -> dict:
    out: dict = {"stage": "restore"}
    r = sh([FORGE, "restore"])
    out["restore"] = r.stdout.strip().splitlines()[-4:] if r.stdout else []
    n = backups_differ()
    out["differ"] = n
    out["ok"] = (n == 0) if not DRY else True
    return out


def stage_mesh() -> dict:
    """A custom static mesh (forge mesh-import) placed in the childhood map: a 1 m bright cube from a .glb
    written here (tools/test_meshimport.py's generator), with its collision hull. The harness finds the
    named thing and screenshots the hero standing at it; whether it renders (and whether the hero is
    stopped by it) is judged from the shot. Not in the default stage list. Puts back only the files it
    touched (graphics.big, textures.big, game.bin/names.bin, the map's TNG in the WAD) from their
    originals, so a staged mod bundle on the install stays as it is."""
    out: dict = {"stage": "mesh"}
    sys.path.insert(0, str(ROOT / "tools"))
    import test_meshimport as tm
    d = OUT / "mesh"; d.mkdir(parents=True, exist_ok=True)
    glb = d / "cube.glb"; png = d / "cube.png"
    if not DRY:
        tm.write_glb(glb)
        # a loud texture: magenta / cyan checks
        import struct, zlib
        w = h = 64
        rows = b"".join(b"\x00" + bytes(sum(([255, 0, 255] if ((x // 8 + y // 8) % 2) else [0, 255, 255] for x in range(w)), [])) for y in range(h))
        def chunk(t, dd): return struct.pack(">I", len(dd)) + t + dd + struct.pack(">I", zlib.crc32(t + dd) & 0xffffffff)
        png.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    r = sh([FORGE, "mesh-import", glb, "FORGE_PROBE_CUBE", "--texture", png])
    if r.returncode != 0 and not DRY:
        return {**out, "ok": False, "error": "mesh-import: " + (r.stderr.strip() or r.stdout.strip()[-400:])}
    out["import"] = r.stdout.strip().splitlines()[-5:] if r.stdout else []
    cx, cy = CENTRE
    ok = run_gui_script("mesh_place", [
        "wait_maps", "wait_ready", f"select {START_MAP}", "wait_loaded", "wait_foliage",
        "edit 1", "frames 2", "assert_state doc_loaded 1",
        f"camera {cx:g} {cy:g} 20 0.8 -60 24", "frames 2",
        "place OBJECT_FORGE_PROBE_CUBE ProbeCube", "frames 3",
        "assert_log placed OBJECT_FORGE_PROBE_CUBE",
        f"screenshot {d / 'editor_placed.png'}",
        "save_level", "frames 2", "deploy_level", "frames 2", "assert_state doc_dirty 0",
        "quit",
    ])
    if not ok:
        return {**out, "ok": False, "error": "GUI placement/deploy script failed (see build/ingame/release/mesh_place.txt.log)"}
    rep = run_harness("mesh", ["--map", START_MAP, "--new-game", "--teleport", "--centre", f"{cx:g},{cy:g}", "--things", "ProbeCube"])
    out["things"] = rep.get("things"); out["hero_after_teleport"] = rep.get("hero_after_teleport"); out["notes"] = rep.get("notes")
    found = rep.get("things") or {}
    out["ok"] = rep.get("dry") or (bool(rep.get("ok")) and all(v.get("ok") for v in found.values()) and len(found) == 1)
    out["shot"] = str(OUT / "mesh" / "04_after_probe.png")
    out["manual"] = "whether the cube renders (magenta/cyan checks, 1 m) and stops the hero is judged from the screenshot"
    return out


def stage_mesh_undo() -> dict:
    """Put back exactly what stage_mesh touched, from the originals (not `forge restore`: a staged
    mod bundle on the install must stay)."""
    out: dict = {"stage": "mesh_undo", "restored": []}
    root = Path(json.loads(sh([FORGE, "backups", "--json"]).stdout).get("root", "")) if False else None
    r = sh([FORGE, "backups"])
    lines = [l for l in r.stdout.splitlines() if "  " in l]
    import re as _re
    for l in lines:
        m = _re.match(r"\s*(same|EDIT|NEW |MODS|OVR )\s+(.*?)\s+\d{4}-\d\d-\d\d", l)
        if not m: continue
        path = Path(m.group(2).strip())
        leaf = path.name.lower()
        if leaf not in ("graphics.big", "textures.big", "game.bin", "names.bin", "finalalbion.wad", f"{START_MAP.lower()}.tng"): continue
        if m.group(1) != "EDIT": continue
        for sfx in (".forge-orig", ".atlas-orig"):
            b = Path(str(path) + sfx)
            if b.exists():
                if not DRY: shutil.copyfile(b, path)
                out["restored"].append(str(path)); break
    out["ok"] = True
    return out


STAGES = {"things": stage_things, "compact": stage_compact, "region": stage_region, "restore": stage_restore,
          "mesh": stage_mesh, "mesh_undo": stage_mesh_undo}


def main() -> int:
    global DRY
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage", default="things,compact,region,restore", help="comma-separated subset, in this order")
    ap.add_argument("--dry-run", action="store_true", help="print every command, run nothing")
    ap.add_argument("--keep", action="store_true", help="do not run the restore stage even if listed")
    a = ap.parse_args()
    DRY = a.dry_run
    stages = [s for s in a.stage.split(",") if s]
    if a.keep:
        stages = [s for s in stages if s != "restore"]
    for s in stages:
        if s not in STAGES:
            print("unknown stage", s); return 2
    if not DRY:
        for exe in (FORGE, GUI):
            if not exe.exists():
                print("missing", exe, "-- build first"); return 2
        if game_running():
            print("Fable.exe is running: close it first (every writer refuses while it runs)"); return 2
        n = backups_differ()
        print(f"install before: {n} file(s) differ from their backup")
        if n:
            print("the install is not at its backup state; run `forge restore` (or --stage restore) first"); return 2
    OUT.mkdir(parents=True, exist_ok=True)
    results = []
    for s in stages:
        print(f"\n=== {s} ===", flush=True)
        t0 = time.time()
        try:
            res = STAGES[s]()
        except Exception as e:  # a stage crashing must not skip the restore
            res = {"stage": s, "ok": False, "error": f"{type(e).__name__}: {e}"}
        res["seconds"] = round(time.time() - t0, 1)
        results.append(res)
        print(f"[{'PASS' if res.get('ok') else 'FAIL'}] {s} ({res['seconds']}s)" + (f"  {res.get('error')}" if res.get("error") else ""))
        if not res.get("ok") and s != "restore" and "restore" in stages:
            print("stage failed; skipping to restore so the install goes back")
            results.append({**stage_restore(), "after_failure": True})
            break
    (OUT / "summary.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print("\nsummary -> build/ingame/release/summary.json")
    for r in results:
        print(f"  {'PASS' if r.get('ok') else 'FAIL'}  {r['stage']}" + (f"   ({r['manual']})" if r.get("manual") else ""))
    return 0 if all(r.get("ok") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
