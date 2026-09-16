#!/usr/bin/env python3
"""In-game terrain oracle: does the RUNNING RETAIL ENGINE see the heights we deployed?

What it does (everything is undone afterwards):
  1. installs a Lua probe (FSE/atlas_probe.lua + a Main() hook appended to a host
     quest script, default FSE/PartyMode/PartyMode.lua) that, once the hero is in the
     target map, calls Quest:GetGroundHeightAt (= CWorldMap::GetGroundSizeZAt, the
     engine's own ground query) at a grid of world points and logs them;
  2. launches the game through FSE_Launcher.exe, drives the fullscreen frontend
     with real mouse/keyboard input (title -> profile '0atlas' -> Continue Game ->
     AutoSave -> skip the opening scene);
  3. waits for the probe's 'done' line in FSE/FableScriptExtender.log, screenshots
     the game, kills it, restores the master script and the log;
  4. compares every probed height with the bilinear LEV height at that point
     (loose data/Levels/FinalAlbion/<map>.lev, i.e. what Atlas deployed) and
     writes a report.

Prerequisites (one-time): a profile folder 'My Games/Fable/Saves/0atlas' whose
AutoSave sits in the target map (copy any Oakvale childhood autosave; the name
sorts first so its position in the profile list is deterministic).

  python tools/ingame/ingame_terrain_test.py [--map StartOakValeWest]
        [--centre 64,112] [--radius 20] [--step 4] [--tolerance 0.05]
        [--game-root <dir>] [--keep-game] [--report build/ingame/report.json]
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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GAMEWIN = HERE / "gamewin.ps1"

DEFAULT_ROOT = r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters"
HOOK_TAG = "-- ATLAS-PROBE-HOOK"

PROBE_LUA = r'''-- Albion Atlas in-game terrain probe (installed by tools/ingame/ingame_terrain_test.py,
-- removed after the run). Logs the engine's ground height at a grid of world points.
ATLAS_TARGET_MAP = "%(map)s"
ATLAS_POINTS = { %(points)s }
ATLAS_TELEPORT = %(teleport)s   -- {x, y} world point to stand the hero on for the screenshot, or nil
ATLAS_THINGS = { %(things)s }   -- ScriptNames whose world position is reported

function AtlasProbe(questObject)
    local Q = questObject
    Q:Log("ATLAS_PROBE|armed|map=" .. ATLAS_TARGET_MAP)
    local hero = nil
    for i = 1, 180 do
        Q:Pause(1.0)
        if not Q:NewScriptFrame() then return end
        local ok, h = pcall(function() return Q:GetHero() end)
        if ok and h ~= nil then
            local mok, mname = pcall(function() return h:GetCurrentMapName() end)
            if mok and mname == ATLAS_TARGET_MAP then hero = h; break end
            if i %% 10 == 0 then Q:Log("ATLAS_PROBE|waiting|map=" .. tostring(mname)) end
        end
    end
    if hero == nil then Q:Log("ATLAS_PROBE|error|hero never reached " .. ATLAS_TARGET_MAP); return end
    Q:Pause(2.0)
    if not Q:NewScriptFrame() then return end
    local pok, p = pcall(function() return hero:GetPos() end)
    if pok and p then Q:Log(string.format("ATLAS_PROBE|hero|%%.3f|%%.3f|%%.3f", p.x or 0, p.y or 0, p.z or 0)) end
    for _, pt in ipairs(ATLAS_POINTS) do
        local ok, z = pcall(function() return Q:GetGroundHeightAt(pt[1], pt[2]) end)
        if ok then Q:Log(string.format("ATLAS_PROBE|z|%%.3f|%%.3f|%%.4f", pt[1], pt[2], z))
        else Q:Log(string.format("ATLAS_PROBE|zerr|%%.3f|%%.3f|%%s", pt[1], pt[2], tostring(z))) end
    end
    for _, name in ipairs(ATLAS_THINGS) do
        local tok, thing = pcall(function() return Q:GetThingWithScriptName(name) end)
        if tok and thing ~= nil then
            local pok2, tp = pcall(function() return thing:GetPos() end)
            if pok2 and tp then Q:Log(string.format("ATLAS_PROBE|thing|%%s|%%.3f|%%.3f|%%.3f", name, tp.x or 0, tp.y or 0, tp.z or 0))
            else Q:Log("ATLAS_PROBE|thing|" .. name .. "|nopos") end
        else
            Q:Log("ATLAS_PROBE|thing|" .. name .. "|missing")
        end
    end
    if ATLAS_TELEPORT then
        local tz = 0
        pcall(function() tz = Q:GetGroundHeightAt(ATLAS_TELEPORT[1], ATLAS_TELEPORT[2]) end)
        local tok, terr = pcall(function() Q:EntityTeleportToPosition(hero, {x = ATLAS_TELEPORT[1], y = ATLAS_TELEPORT[2], z = tz + 0.5}, 0.0) end)
        Q:Log("ATLAS_PROBE|teleport|" .. tostring(tok) .. "|" .. tostring(terr))
        Q:Pause(3.0)
        if not Q:NewScriptFrame() then return end
        local p2ok, p2 = pcall(function() return hero:GetPos() end)
        if p2ok and p2 then Q:Log(string.format("ATLAS_PROBE|hero2|%%.3f|%%.3f|%%.3f", p2.x or 0, p2.y or 0, p2.z or 0)) end
    end
    Q:Log("ATLAS_PROBE|done")
end
'''


def ps(*args: str, timeout: int = 60) -> str:
    r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(GAMEWIN), *args],
                       capture_output=True, text=True, timeout=timeout)
    return (r.stdout or "") + (r.stderr or "")


def game_running() -> bool:
    r = subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process Fable -ErrorAction SilentlyContinue) -ne $null"],
                       capture_output=True, text=True)
    return "True" in r.stdout


def kill_game() -> None:
    subprocess.run(["powershell", "-NoProfile", "-Command", "Stop-Process -Name Fable -Force -ErrorAction SilentlyContinue"], capture_output=True)


def wld_placement(root: Path, map_name: str) -> tuple[int, int]:
    text = (root / "data" / "Levels" / "FinalAlbion.wld").read_text(errors="ignore")
    for block in text.split("NewMap"):
        m = re.search(r'LevelName\s+"FinalAlbion\\%s\.lev"' % re.escape(map_name), block, re.I)
        if not m:
            continue
        mx = re.search(r"MapX\s+(-?\d+)", block)
        my = re.search(r"MapY\s+(-?\d+)", block)
        if mx and my:
            return int(mx.group(1)), int(my.group(1))
    raise SystemExit(f"{map_name} not placed in FinalAlbion.wld")


def tng_positions(root: Path, map_name: str, names: list[str]) -> dict[str, tuple[float, float, float]]:
    """Map-local PositionX/Y/Z of things by ScriptName from the loose .tng."""
    p = root / "data" / "Levels" / "FinalAlbion" / f"{map_name}.tng"
    out: dict[str, tuple[float, float, float]] = {}
    if not p.exists():
        return out
    for block in p.read_text(errors="ignore").split("NewThing"):
        m = re.search(r'ScriptName\s+"?([^;"]+)"?;', block)
        if not m or m.group(1) not in names:
            continue
        pos = [re.search(r"Position%s\s+([-\d.]+);" % ax, block) for ax in "XYZ"]
        if all(pos):
            out[m.group(1)] = tuple(float(v.group(1)) for v in pos)
    return out


def lev_heights(lev: str, pts: list[tuple[float, float]]) -> dict[tuple[float, float], float | None]:
    exe = ROOT / "build" / "AlbionAtlas.exe"
    out = subprocess.run([str(exe), "heights", lev, *[f"{x},{y}" for x, y in pts]], capture_output=True, text=True)
    res: dict[tuple[float, float], float | None] = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) != 2:
            continue
        x, y = (float(v) for v in parts[0].split(","))
        res[(x, y)] = None if parts[1] == "outside" else float(parts[1])
    return res


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", default="StartOakValeWest")
    ap.add_argument("--centre", default="64,112")
    ap.add_argument("--radius", type=float, default=20)
    ap.add_argument("--step", type=float, default=4)
    ap.add_argument("--tolerance", type=float, default=0.05)
    ap.add_argument("--game-root", default=DEFAULT_ROOT)
    ap.add_argument("--keep-game", action="store_true")
    ap.add_argument("--report", default=str(ROOT / "build" / "ingame" / "report.json"))
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--teleport", action="store_true", help="stand the hero on the centre point before the final screenshot")
    ap.add_argument("--new-game", action="store_true", help="start a fresh game (profile '0aa' is recreated) instead of continuing the '0atlas' save; needed to see .tng changes, saves cache region entities")
    ap.add_argument("--things", default="", help="comma-separated ScriptNames: the probe reports their in-game positions, compared with the loose .tng")
    ap.add_argument("--trace-bp", default="", help="break on this Fable.exe address during the run and dump a stack scan (tools/ingame/trace_bp_stack.py)")
    ap.add_argument("--trace-lzo", action="store_true", help="log every lzo1x_decompress call during the run (tools/ingame/trace_lzo_calls.py)")
    ap.add_argument("--catch-crash", action="store_true", help="attach the dbgeng crash catcher once the game window is up (report gets the faulting address)")
    ap.add_argument("--host", default="PartyMode/PartyMode.lua",
                    help="quest script (relative to FSE/) whose Main() hosts the probe thread; it must be a quest that runs in every session")
    a = ap.parse_args()

    root = Path(a.game_root)
    fse = root / "FSE"
    master = fse / a.host
    probe = fse / "atlas_probe.lua"
    log = fse / "FableScriptExtender.log"
    report_path = Path(a.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    shots = report_path.parent

    saves_root = Path(os.path.expanduser("~")) / "Documents" / "My Games" / "Fable" / "Saves"
    if a.new_game:
        shutil.rmtree(saves_root / "0aa", ignore_errors=True)   # the frontend recreates it; it must sort first
    elif not (saves_root / "0atlas").is_dir():
        print(f"missing profile folder {saves_root / '0atlas'}: copy an Oakvale autosave profile there first", file=sys.stderr)
        return 2
    # the oracle: the loose .lev Atlas deploys, else the WAD copy (resolved by the CLI)
    lev = root / "data" / "Levels" / "FinalAlbion" / f"{a.map}.lev"
    lev_arg = str(lev) if lev.exists() else a.map
    if game_running():
        print("Fable.exe is already running; close it first", file=sys.stderr)
        return 2

    mx, my = wld_placement(root, a.map)
    cx, cy = (float(v) for v in a.centre.split(","))
    pts: list[tuple[float, float]] = []
    r, s = a.radius, a.step
    y = cy - r
    while y <= cy + r + 1e-6:
        x = cx - r
        while x <= cx + r + 1e-6:
            pts.append((round(x, 3), round(y, 3)))
            x += s
        y += s
    expected = lev_heights(lev_arg, pts)
    pts = [p for p in pts if expected.get(p) is not None]
    world_pts = [(mx + x, my + y) for x, y in pts]

    # ---- install the probe
    master_backup = master.read_bytes()
    log_backup = log.read_bytes() if log.exists() else b""
    thing_names = [t for t in a.things.split(",") if t]
    probe.write_text(PROBE_LUA % {"map": a.map, "points": ", ".join("{%g, %g}" % p for p in world_pts),
                                  "teleport": ("{%g, %g}" % (mx + cx, my + cy)) if a.teleport else "nil",
                                  "things": ", ".join('"%s"' % t for t in thing_names)}, encoding="utf-8")
    # The probe thread is started BEFORE the host's own Main (which may loop forever).
    hook = (f"\n{HOOK_TAG} (installed by Albion Atlas tools/ingame; removed after the run)\n"
            f"local _atlasMain = Main\n"
            f"function Main(quest)\n"
            f"    pcall(function()\n"
            f"        local f, err = loadfile([[{probe.as_posix()}]])\n"
            f"        if f then f(); quest:CreateThread(\"AtlasProbe\", {{}}) else quest:Log(\"ATLAS_PROBE|error|\" .. tostring(err)) end\n"
            f"    end)\n"
            f"    return _atlasMain(quest)\n"
            f"end\n")
    master.write_text(master_backup.decode("utf-8", "replace") + hook, encoding="utf-8")
    if log.exists():
        log.write_text("", encoding="utf-8")

    result = {"map": a.map, "placement": [mx, my], "points": len(pts), "ok": False, "mismatches": [], "notes": []}
    try:
        subprocess.Popen([str(root / "FSE_Launcher.exe")], cwd=str(root))
        if "window" not in ps("-Action", "wait", "-Seconds", "90", timeout=120):
            result["notes"].append("no game window"); raise RuntimeError("no game window")
        catcher = None
        if a.trace_bp:
            catcher = subprocess.Popen([sys.executable, "-u", str(HERE / "trace_bp_stack.py"), "--addr", a.trace_bp, "--seconds", str(a.timeout + 120), "--out", str(shots / "bp_stack.json")],
                                       stdout=open(shots / "trace_bp.log", "w"), stderr=subprocess.STDOUT)
            time.sleep(3)
        if a.trace_lzo:
            catcher = subprocess.Popen([sys.executable, "-u", str(HERE / "trace_lzo_calls.py"), "--seconds", str(a.timeout + 120), "--out", str(shots / "lzo_calls.jsonl"), "--max", "40000"],
                                       stdout=open(shots / "trace_lzo.log", "w"), stderr=subprocess.STDOUT)
            time.sleep(3)
        if a.catch_crash:
            catcher = subprocess.Popen([sys.executable, "-u", str(HERE / "crash_catcher.py"), "--seconds", str(a.timeout + 120), "--out", str(shots / "crash.json")],
                                       stdout=open(shots / "crash_catcher.log", "w"), stderr=subprocess.STDOUT)
            time.sleep(3)
        time.sleep(14)                                   # title screen
        ps("-Action", "capture", "-Output", str(shots / "01_title.png"))
        ps("-Action", "click", "-X", "512", "-Y", "400")  # "Press Left Mouse Button"
        time.sleep(4)
        if a.new_game:
            ps("-Action", "click", "-X", "512", "-Y", "209")  # New Profile
            time.sleep(3)
            ps("-Action", "key", "-Keys", "END BACK BACK BACK BACK BACK BACK BACK BACK BACK BACK 0 A A")
            time.sleep(1)
            ps("-Action", "click", "-X", "784", "-Y", "697")  # Apply
            time.sleep(4)
            ps("-Action", "capture", "-Output", str(shots / "02_menu.png"))
            ps("-Action", "click", "-X", "512", "-Y", "337")  # '0aa - New Game'
            time.sleep(45)                                   # intro movie + first load
            for _ in range(4):
                ps("-Action", "key", "-Keys", "ESC")         # skip the opening scenes
                time.sleep(10)
        else:
            ps("-Action", "click", "-X", "512", "-Y", "254")  # profile '0atlas' (first row)
            time.sleep(4)
            ps("-Action", "capture", "-Output", str(shots / "02_menu.png"))
            ps("-Action", "click", "-X", "512", "-Y", "337")  # '0atlas - Continue Game'
            time.sleep(4)
            ps("-Action", "click", "-X", "207", "-Y", "161")  # AutoSave
            time.sleep(35)                                   # load + opening movie
            ps("-Action", "key", "-Keys", "ESC")             # skip movie / scene
            time.sleep(6)
            ps("-Action", "key", "-Keys", "ESC")
        ps("-Action", "capture", "-Output", str(shots / "03_ingame.png"))
        # ---- wait for the probe
        deadline = time.time() + a.timeout
        done = False
        while time.time() < deadline:
            text = log.read_text(errors="ignore") if log.exists() else ""
            if "ATLAS_PROBE|done" in text or "ATLAS_PROBE|error" in text:
                done = True; break
            if not game_running():
                result["notes"].append("game exited before the probe finished"); break
            time.sleep(2)
        ps("-Action", "capture", "-Output", str(shots / "04_after_probe.png"))
        text = log.read_text(errors="ignore") if log.exists() else ""
        (shots / "fse_probe.log").write_text("\n".join(l for l in text.splitlines() if "ATLAS_PROBE" in l or "GetGroundHeightAt" in l), encoding="utf-8")
        if not done:
            result["notes"].append("probe timed out")
        # ---- compare
        got: dict[tuple[float, float], float] = {}
        for line in text.splitlines():
            m = re.match(r".*ATLAS_PROBE\|z\|([-\d.]+)\|([-\d.]+)\|([-\d.]+)", line)
            if m:
                got[(round(float(m.group(1)) - mx, 3), round(float(m.group(2)) - my, 3))] = float(m.group(3))
        hero = re.search(r"ATLAS_PROBE\|hero\|([-\d.]+)\|([-\d.]+)\|([-\d.]+)", text)
        if hero:
            result["hero"] = [float(hero.group(i)) for i in (1, 2, 3)]
        hero2 = re.search(r"ATLAS_PROBE\|hero2\|([-\d.]+)\|([-\d.]+)\|([-\d.]+)", text)
        if hero2:
            result["hero_after_teleport"] = [float(hero2.group(i)) for i in (1, 2, 3)]
        compared = 0; worst = 0.0
        for p in pts:
            if p not in got:
                continue
            e = expected[p]; g = got[p]
            compared += 1
            d = abs(g - e); worst = max(worst, d)
            if d > a.tolerance:
                result["mismatches"].append({"local": list(p), "lev": e, "engine": g, "delta": d})
        result["compared"] = compared
        result["worst_delta"] = worst
        things_ok = True
        if thing_names:
            expected_things = tng_positions(root, a.map, thing_names)
            result["things"] = {}
            for name in thing_names:
                m = re.search(r"ATLAS_PROBE\|thing\|%s\|([-\d.]+)\|([-\d.]+)\|([-\d.]+)" % re.escape(name), text)
                exp = expected_things.get(name)
                if not m or not exp:
                    result["things"][name] = {"found": bool(m), "expected": exp}; things_ok = False; continue
                got = [float(m.group(i)) for i in (1, 2, 3)]
                dxy = max(abs(got[0] - (mx + exp[0])), abs(got[1] - (my + exp[1])))
                dz = abs(got[2] - exp[2])
                okt = dxy <= 0.05 and dz <= 0.5
                result["things"][name] = {"found": True, "engine": got, "expected_world": [mx + exp[0], my + exp[1], exp[2]], "ok": okt}
                things_ok = things_ok and okt
        result["ok"] = done and compared == len(pts) and not result["mismatches"] and things_ok
    finally:
        crash = shots / "crash.json"
        if a.catch_crash:
            for _ in range(20):
                if crash.exists():
                    break
                time.sleep(1)
            if crash.exists():
                try:
                    result["crash"] = json.loads(crash.read_text())
                except Exception:
                    pass
        if not a.keep_game:
            kill_game()
        master.write_bytes(master_backup)
        try:
            probe.unlink()
        except OSError:
            pass
        # keep the run's FSE log next to the report, restore the previous one
        try:
            if log.exists():
                shutil.copy(log, shots / "FableScriptExtender.run.log")
            log.write_bytes(log_backup)
        except OSError:
            pass

    report_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "mismatches"}, indent=2))
    if result["mismatches"]:
        print(f"{len(result['mismatches'])} mismatches, first: {result['mismatches'][:5]}")
    print("RESULT", "PASS" if result["ok"] else "FAIL")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
