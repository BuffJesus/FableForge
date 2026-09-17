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
ATLAS_START_MAP = "%(start_map)s"   -- where the hero begins; the teleport (if any) takes him to the target map
ATLAS_TRANSITION_SLOT = %(transition_slot)d   -- >0: use ForgeFSE's GoToMapSlotRetailTransition (a real region load) instead of a bare teleport
ATLAS_POINTS = { %(points)s }
ATLAS_TELEPORT = %(teleport)s   -- {x, y} world point to stand the hero on for the screenshot, or nil
ATLAS_THINGS = { %(things)s }   -- ScriptNames whose world position is reported
ATLAS_FOLLOW = %(follow)s       -- {x, y} world point: spawn a creature there and make it follow the hero, or nil
ATLAS_CREATURES = %(creatures)d -- >0: after this many seconds, list every creature within 60 units of the hero (spawner probe)
ATLAS_WALK = %(walk)s           -- {x, y} world point the hero walks to (script control) before the creature listing, or nil
ATLAS_FOLLOW_DEF = "%(follow_def)s"
ATLAS_FOLLOW_SECONDS = %(follow_seconds)d

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
            if mok and mname == ATLAS_START_MAP then hero = h; break end
            if i %% 10 == 0 then Q:Log("ATLAS_PROBE|waiting|map=" .. tostring(mname)) end
        end
    end
    if hero == nil then Q:Log("ATLAS_PROBE|error|hero never reached " .. ATLAS_START_MAP); return end
    Q:Pause(2.0)
    if not Q:NewScriptFrame() then return end
    local pok, p = pcall(function() return hero:GetPos() end)
    if pok and p then Q:Log(string.format("ATLAS_PROBE|hero|%%.3f|%%.3f|%%.3f", p.x or 0, p.y or 0, p.z or 0)) end
    do local rok, rname = pcall(function() return Q:GetRegionName() end); Q:Log("ATLAS_PROBE|region0|" .. tostring(rok) .. "|" .. tostring(rname)) end
    local tz = 0
    if ATLAS_TELEPORT then
        -- wait until the opening scenes are over (the harness keeps pressing Esc) so the screenshot shows the spot
        for i = 1, 90 do
            local cok, ctrl = pcall(function() return Q:IsHeroControlledByPlayer() end)
            if i == 1 or i %% 10 == 0 then Q:Log("ATLAS_PROBE|waitcontrol|" .. tostring(cok) .. "|" .. tostring(ctrl)) end
            if cok and ctrl then break end
            Q:Pause(1.0)
            if not Q:NewScriptFrame() then Q:Log("ATLAS_PROBE|error|thread ended while waiting for control"); return end
        end
        Q:Log("ATLAS_PROBE|control")
        pcall(function() tz = Q:GetGroundHeightAt(ATLAS_TELEPORT[1], ATLAS_TELEPORT[2]) end)
        local tok, terr
        if ATLAS_TRANSITION_SLOT > 0 then
            tok, terr = pcall(function() Q:GoToMapSlotRetailTransition(ATLAS_TRANSITION_SLOT, ATLAS_TELEPORT[1], ATLAS_TELEPORT[2], tz + 0.5) end)
            Q:Log("ATLAS_PROBE|transition|" .. ATLAS_TRANSITION_SLOT .. "|" .. tostring(tok) .. "|" .. tostring(terr))
        else
            tok, terr = pcall(function() Q:EntityTeleportToPosition(hero, {x = ATLAS_TELEPORT[1], y = ATLAS_TELEPORT[2], z = tz + 0.5}, 0.0) end)
            Q:Log("ATLAS_PROBE|teleport|" .. tostring(tok) .. "|" .. tostring(terr))
        end
        -- a teleport into another map streams that map in: wait for the hero to report it
        for i = 1, 60 do
            Q:Pause(1.0)
            if not Q:NewScriptFrame() then return end
            local mok, mname = pcall(function() return hero:GetCurrentMapName() end)
            if mok and mname == ATLAS_TARGET_MAP then Q:Log("ATLAS_PROBE|arrived|" .. tostring(mname) .. "|" .. i); break end
            if i %% 10 == 0 then Q:Log("ATLAS_PROBE|waitmap|" .. tostring(mname)) end
            if i == 60 then Q:Log("ATLAS_PROBE|error|hero never arrived in " .. ATLAS_TARGET_MAP .. " (in " .. tostring(mname) .. ")"); return end
        end
        Q:Pause(3.0)
        if not Q:NewScriptFrame() then return end
        pcall(function() tz = Q:GetGroundHeightAt(ATLAS_TELEPORT[1], ATLAS_TELEPORT[2]) end)
        local p2ok, p2 = pcall(function() return hero:GetPos() end)
        if p2ok and p2 then Q:Log(string.format("ATLAS_PROBE|hero2|%%.3f|%%.3f|%%.3f", p2.x or 0, p2.y or 0, p2.z or 0)) end
        local rok, rname = pcall(function() return Q:GetRegionName() end)
        Q:Log("ATLAS_PROBE|region|" .. tostring(rok) .. "|" .. tostring(rname))
    end
    for _, pt in ipairs(ATLAS_POINTS) do
        local ok, z = pcall(function() return Q:GetGroundHeightAt(pt[1], pt[2]) end)
        if ok then Q:Log(string.format("ATLAS_PROBE|z|%%.3f|%%.3f|%%.4f", pt[1], pt[2], z))
        else Q:Log(string.format("ATLAS_PROBE|zerr|%%.3f|%%.3f|%%s", pt[1], pt[2], tostring(z))) end
    end
    if ATLAS_WALK then
        local wz = 0
        pcall(function() wz = Q:GetGroundHeightAt(ATLAS_WALK[1], ATLAS_WALK[2]) end)
        -- GainControlAndMoveToPosition does not move the hero (tried: no motion, no error);
        -- a second teleport after a pause still counts as entering a trigger radius
        Q:Pause(3.0)
        if not Q:NewScriptFrame() then return end
        local wok, werr = pcall(function() Q:EntityTeleportToPosition(hero, {x = ATLAS_WALK[1], y = ATLAS_WALK[2], z = wz + 0.5}, 0.0) end)
        Q:Log("ATLAS_PROBE|walk|" .. tostring(wok) .. "|" .. tostring(werr))
    end
    if ATLAS_CREATURES > 0 then
        Q:Pause(ATLAS_CREATURES)
        if not Q:NewScriptFrame() then return end
        local hok, hp = pcall(function() return hero:GetPos() end)
        if hok and hp then Q:Log(string.format("ATLAS_PROBE|hero3|%%.3f|%%.3f|%%.3f", hp.x or 0, hp.y or 0, hp.z or 0)) end
        local cok, list = pcall(function() return Q:GetAllCreaturesExcludingHero() end)
        local n = 0
        if cok and list then
            for _, c in ipairs(list) do
                local pok, cp = pcall(function() return c:GetPos() end)
                local dok, dn = pcall(function() return c:GetDefName() end)
                if pok and cp and hok and hp then
                    local dx, dy = (cp.x or 0) - (hp.x or 0), (cp.y or 0) - (hp.y or 0)
                    if dx * dx + dy * dy <= 100 * 100 then
                        n = n + 1
                        Q:Log(string.format("ATLAS_PROBE|creature|%%s|%%.2f|%%.2f|%%.2f", tostring(dok and dn or "?"), cp.x or 0, cp.y or 0, cp.z or 0))
                    end
                end
            end
        end
        Q:Log("ATLAS_PROBE|creatures|" .. tostring(n))
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
        if ATLAS_FOLLOW then
            -- navigation probe: a creature spawned at ATLAS_FOLLOW follows the hero; where it
            -- ends up tells whether the nav tree lets it reach the hero's cell
            local fz = 0
            pcall(function() fz = Q:GetGroundHeightAt(ATLAS_FOLLOW[1], ATLAS_FOLLOW[2]) end)
            local cok, cre = pcall(function() return Q:CreateCreature(ATLAS_FOLLOW_DEF, {x = ATLAS_FOLLOW[1], y = ATLAS_FOLLOW[2], z = fz + 0.5}, "AtlasFollower") end)
            Q:Log("ATLAS_PROBE|create|" .. tostring(cok) .. "|" .. tostring(cre))
            if cok and cre ~= nil then
                Q:Pause(1.0)
                if not Q:NewScriptFrame() then return end
                -- a second thread takes script control of the creature and issues the
                -- (blocking) move so this thread can keep sampling positions
                ATLAS_CRE = cre
                ATLAS_MOVE_TARGET = {x = ATLAS_TELEPORT[1], y = ATLAS_TELEPORT[2], z = tz + 0.5}
                local fok, ferr = pcall(function() Q:CreateThread("AtlasMover", {}) end)
                Q:Log("ATLAS_PROBE|follow|" .. tostring(fok) .. "|" .. tostring(ferr))
                for i = 1, ATLAS_FOLLOW_SECONDS do
                    Q:Pause(1.0)
                    if not Q:NewScriptFrame() then return end
                    local pk, cp = pcall(function() return cre:GetPos() end)
                    local hk, hp = pcall(function() return hero:GetPos() end)
                    if pk and cp and hk and hp then
                        local d = math.sqrt((cp.x - hp.x) * (cp.x - hp.x) + (cp.y - hp.y) * (cp.y - hp.y))
                        Q:Log(string.format("ATLAS_PROBE|followpos|%%d|%%.3f|%%.3f|%%.3f|%%.3f", i, cp.x or 0, cp.y or 0, cp.z or 0, d))
                    end
                end
            end
        end
    end
    Q:Log("ATLAS_PROBE|done")
end

function AtlasMover(questObject)
    local Q = questObject
    if ATLAS_CRE == nil then return end
    local ok, err = pcall(function() ATLAS_CRE:GainControlAndMoveToPosition(ATLAS_MOVE_TARGET, 1.0, 1) end)
    Q:Log("ATLAS_PROBE|moved|" .. tostring(ok) .. "|" .. tostring(err))
end
'''


def ps(*args: str, timeout: int = 60) -> str:
    r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(GAMEWIN), *args],
                       capture_output=True, text=True, timeout=timeout)
    return (r.stdout or "") + (r.stderr or "")


def screen_diff(a: Path, b: Path) -> float:
    """Mean absolute pixel difference (0..255) between two captures; 0 when PIL is missing."""
    try:
        from PIL import Image, ImageChops, ImageStat
        ia, ib = Image.open(a).convert("L"), Image.open(b).convert("L")
        if ia.size != ib.size:
            ib = ib.resize(ia.size)
        return float(ImageStat.Stat(ImageChops.difference(ia, ib)).mean[0])
    except Exception:
        return 0.0


def click_until_change(x: int, y: int, before: Path, shots: Path, tag: str, wait: float = 4.0, attempts: int = 3) -> None:
    """The DirectInput frontend drops a click now and then: click, wait, and click
    again while the screen still looks like `before`."""
    for i in range(attempts):
        ps("-Action", "click", "-X", str(x), "-Y", str(y))
        time.sleep(wait)
        after = shots / f"{tag}_try{i}.png"
        ps("-Action", "capture", "-Output", str(after))
        if screen_diff(before, after) > 6.0:
            return
        print(f"  click ({x},{y}) did not change the screen (try {i + 1}); retrying", flush=True)


def game_running() -> bool:
    r = subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process Fable -ErrorAction SilentlyContinue) -ne $null"],
                       capture_output=True, text=True)
    return "True" in r.stdout


def kill_game() -> None:
    subprocess.run(["powershell", "-NoProfile", "-Command", "Stop-Process -Name Fable -Force -ErrorAction SilentlyContinue"], capture_output=True)


def wld_placement(root: Path, map_name: str) -> tuple[int, int]:
    return wld_map(root, map_name)[:2]


def wld_map(root: Path, map_name: str) -> tuple[int, int, int]:
    """(MapX, MapY, 1-based slot) of a map from FinalAlbion.wld."""
    text = (root / "data" / "Levels" / "FinalAlbion.wld").read_text(errors="ignore")
    for block in text.split("NewMap"):
        m = re.search(r'LevelName\s+"FinalAlbion\\%s\.lev"' % re.escape(map_name), block, re.I)
        if not m:
            continue
        mx = re.search(r"MapX\s+(-?\d+)", block)
        my = re.search(r"MapY\s+(-?\d+)", block)
        slot = re.match(r"\s*(\d+)\s*;", block)
        if mx and my:
            return int(mx.group(1)), int(my.group(1)), int(slot.group(1)) if slot else 0
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
    ap.add_argument("--walk", default="", help="map-local x,y the hero walks to after the teleport (before the --creatures listing)")
    ap.add_argument("--creatures", type=int, default=0, help="after N seconds in the target map, list every creature within 60 units of the hero (spawner probe; report gets 'creatures')")
    ap.add_argument("--things", default="", help="comma-separated ScriptNames: the probe reports their in-game positions, compared with the loose .tng")
    ap.add_argument("--transition", action="store_true", help="with --teleport: go through ForgeFSE's GoToMapSlotRetailTransition (a real region load: minimap, region state) instead of a bare entity teleport")
    ap.add_argument("--start-map", default="", help="map the hero starts in (default: --map); with --teleport the probe jumps to the target map and waits for it to stream in")
    ap.add_argument("--follow", default="", help="map-local x,y: spawn a creature there after the teleport and make it follow the hero (navigation probe; needs --teleport)")
    ap.add_argument("--follow-def", default="CREATURE_BOWERSTONE_POSH_VILLAGER_FEMALE_UNEMPLOYED")
    ap.add_argument("--follow-seconds", type=int, default=25)
    ap.add_argument("--follow-expect", choices=["", "near", "far"], default="", help="near: the creature must get within 2 units of the hero; far: it must never get closer than 2.5")
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

    mx, my, map_slot = wld_map(root, a.map)
    walk_xy = tuple(float(v) for v in a.walk.split(",")) if a.walk else None
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
    follow_pt = tuple(float(v) for v in a.follow.split(",")) if a.follow else None
    probe.write_text(PROBE_LUA % {"map": a.map, "start_map": a.start_map or a.map, "transition_slot": map_slot if a.transition else 0, "points": ", ".join("{%g, %g}" % p for p in world_pts),
                                  "teleport": ("{%g, %g}" % (mx + cx, my + cy)) if a.teleport else "nil",
                                  "things": ", ".join('"%s"' % t for t in thing_names),
                                  "creatures": a.creatures,
                                  "walk": ("{%g, %g}" % (mx + walk_xy[0], my + walk_xy[1])) if walk_xy else "nil",
                                  "follow": ("{%g, %g}" % (mx + follow_pt[0], my + follow_pt[1])) if follow_pt else "nil",
                                  "follow_def": a.follow_def, "follow_seconds": a.follow_seconds}, encoding="utf-8")
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
            click_until_change(512, 337, shots / "02_menu.png", shots, "continue")   # '0atlas - Continue Game'
            ps("-Action", "capture", "-Output", str(shots / "02b_saves.png"))
            click_until_change(207, 161, shots / "02b_saves.png", shots, "autosave")  # AutoSave
            time.sleep(31)                                   # load + opening movie
            ps("-Action", "key", "-Keys", "ESC")             # skip movie / scene
            time.sleep(6)
            ps("-Action", "key", "-Keys", "ESC")
        ps("-Action", "capture", "-Output", str(shots / "03_ingame.png"))
        # ---- wait for the probe
        deadline = time.time() + a.timeout
        done = False
        last_esc = 0.0
        stalled_len, stalled_since = -1, time.time()
        while time.time() < deadline:
            text = log.read_text(errors="ignore") if log.exists() else ""
            if "ATLAS_PROBE|done" in text or "ATLAS_PROBE|error" in text:
                done = True; break
            if not game_running():
                result["notes"].append("game exited before the probe finished"); break
            if a.teleport and "ATLAS_PROBE|control" not in text and time.time() - last_esc > 6:
                # skip whatever scene is playing until the hero is ours; tutorial boxes
                # ("Press Tab to talk..." with a Next button) only go away on a click
                ps("-Action", "key", "-Keys", "ESC")
                time.sleep(1.5)
                ps("-Action", "click", "-X", "512", "-Y", "600")
                last_esc = time.time()
            elif a.teleport and "ATLAS_PROBE|control" in text:
                # after the transition a tutorial box ("You have committed your first bad
                # deed" ... Next) pauses the script thread: when the probe log stops
                # growing, click the box's Next button
                if len(text) != stalled_len:
                    stalled_len, stalled_since = len(text), time.time()
                elif time.time() - stalled_since > 10 and time.time() - last_esc > 6:
                    ps("-Action", "click", "-X", "660", "-Y", "398")
                    last_esc = time.time()
            time.sleep(2)
        if a.transition:
            time.sleep(12)   # let the region state machine finish (loading screen, minimap init) before the shot
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
            creatures = re.findall(r"ATLAS_PROBE\|creature\|([^|]*)\|([-\d.]+)\|([-\d.]+)\|([-\d.]+)", text)
            if a.creatures:
                result["creatures"] = [{"def": c[0], "pos": [float(c[1]), float(c[2]), float(c[3])]} for c in creatures]
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
        follow_ok = True
        if follow_pt:
            samples = [(int(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(4)), float(m.group(5)))
                       for m in re.finditer(r"ATLAS_PROBE\|followpos\|(\d+)\|([-\d.]+)\|([-\d.]+)\|([-\d.]+)\|([-\d.]+)", text)]
            created = re.search(r"ATLAS_PROBE\|create\|(\w+)", text)
            result["follow"] = {"created": bool(created and created.group(1) == "true"), "samples": len(samples),
                                "expect": a.follow_expect or None}
            if samples:
                tail = sorted(d for _, _, _, _, d in samples[-5:])
                final = tail[len(tail) // 2]
                result["follow"]["final_distance"] = final
                result["follow"]["min_distance"] = min(d for _, _, _, _, d in samples)
                result["follow"]["track"] = [[t, round(x, 2), round(y, 2), round(d, 2)] for t, x, y, _, d in samples]
                if a.follow_expect == "near": follow_ok = result["follow"]["min_distance"] <= 2.0
                elif a.follow_expect == "far": follow_ok = result["follow"]["min_distance"] >= 2.5
            else:
                follow_ok = not a.follow_expect
        result["ok"] = done and compared == len(pts) and not result["mismatches"] and things_ok and follow_ok
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
