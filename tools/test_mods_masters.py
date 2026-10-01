#!/usr/bin/env python3
"""The Mods tab as a plugin list: two FableForge packs in a scratch order; B requires
A (forge_pack.json "requires", set through the GUI) -- fine while A loads first, flagged
once B is moved above it, and `mods build` reports it too; both packs ship the same
file, so Check conflicts gives the later one "wins" and the earlier one "loses".
Screenshots under build/ui/. Needs the Fable install for the GUI's context (skips
cleanly without one); nothing is written outside build/.

  python tools/test_mods_masters.py [--root <fable-root>] [--keep]
"""
import argparse, json, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_INSTALL = r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.environ.get("FABLE_ROOT", DEFAULT_INSTALL))
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--size", default="1280x720")
    ap.add_argument("--scale", default="1")
    a = ap.parse_args()
    if not os.path.exists(os.path.join(a.root, "data", "CompiledDefs", "game.bin")):
        print("mods masters test skipped (no install)")
        return 0
    gui = os.path.join(ROOT, "build", "FableForge.exe")
    tools = os.path.join(ROOT, "build", "forge-tools.exe")
    work = os.path.join(ROOT, "build", "mods_masters")
    if os.path.dirname(os.path.realpath(work)) != os.path.realpath(os.path.join(ROOT, "build")):
        raise RuntimeError("unexpected mod test scratch root")
    shutil.rmtree(work, ignore_errors=True)
    scratch = os.path.join(work, "install")
    os.makedirs(os.path.join(scratch, "data", "CompiledDefs"))
    for f in ("game.bin", "names.bin"):
        shutil.copyfile(os.path.join(a.root, "data", "CompiledDefs", f), os.path.join(scratch, "data", "CompiledDefs", f))
    packs = {}
    for tag in ("A", "B"):
        p = os.path.join(work, "Pack" + tag)
        os.makedirs(os.path.join(p, "assets")); os.makedirs(os.path.join(p, "data", "Misc"))
        json.dump({"version": 1, "name": "Pack " + tag, "models": [], "groundThemes": []}, open(os.path.join(p, "forge_pack.json"), "w"))
        open(os.path.join(p, "data", "Misc", "shared.txt"), "w").write("from pack " + tag + "\n")
        packs[tag] = p
    ok = True
    os.makedirs(os.path.join(ROOT, "build", "ui"), exist_ok=True)
    script = os.path.join(work, "gui.txt")
    def reveal(widget):
        return [f"reveal {widget}", "frames 3", f"reveal {widget}", "frames 3"]
    commands = [
        "wait_maps", "wait_ready", f"set uiscale {a.scale}", f"set saveroot {scratch}", "mods_tab 1", "frames 2",
        f"mod_add {packs['A']} PackA", f"mod_add {packs['B']} PackB", "frames 2"]
    commands += reveal("mod_row_1") + [
        "mouse_move mod_row_1", "mouse_down right", "frames 1", "mouse_up right", "frames 3",
        "click mod_requires_1_0", "frames 3", "key_down Escape", "frames 1", "key_up Escape",
        "frames 2", "assert_mod_problems -",
        "mods_conflicts", "wait_mods", "frames 3"]
    commands += reveal("mod_row_0") + ["clear_toasts", "mouse_move viewport", "frames 3",
        "screenshot build/ui/mods_badges.png"]
    commands += reveal("mod_up_1") + [
        "click mod_up_1", "frames 3", "assert_mod_problems needs PackA loaded before it"]
    commands += reveal("mod_row_0") + ["clear_toasts", "mouse_move viewport", "frames 3",
        "screenshot build/ui/mods_masters.png"]
    commands += reveal("mod_enabled_1") + [
        "click mod_enabled_1", "frames 2", "assert_mod_problems which is disabled",
        "click mod_enabled_1", "frames 2", "click mod_up_1", "frames 2", "assert_mod_problems -"]
    commands += reveal("mod_row_1") + [
        "mouse_move mod_row_1", "mouse_down left", "frames 1", "mouse_delta 0 -10", "frames 2",
        "mouse_move mod_row_0", "frames 3", "mouse_up left", "frames 3",
        "assert_mod_problems needs PackA loaded before it"]
    commands += reveal("mod_up_1") + ["click mod_up_1", "frames 2", "assert_mod_problems -", "dump_log", "quit"]
    open(script, "w").write("\n".join(commands) + "\n")
    r = subprocess.run([gui, "--auto", script, "--install", a.root, "--size", a.size], capture_output=True, text=True, timeout=120)
    log = open(script + ".log", encoding="utf-8", errors="replace").read() if os.path.exists(script + ".log") else ""
    if r.returncode != 0 or "RESULT PASS" not in log: print("GUI:"); print(log[-2000:]); ok = False
    pj = json.load(open(os.path.join(packs["B"], "forge_pack.json")))
    if pj.get("requires") != ["PackA"]: print("PackB's forge_pack.json requires:", pj.get("requires")); ok = False
    order = json.load(open(os.path.join(scratch, "forge_mods.json")))
    if [m["name"] for m in order["mods"]] != ["PackA", "PackB"] or not all(m["enabled"] for m in order["mods"]):
        print("UI order/enabled state was not saved:", order); ok = False
    # the composer says it too
    subprocess.run([tools, "mods", "move", scratch, "PackB", "0"], capture_output=True, text=True)
    rr = subprocess.run([tools, "mods", "build", scratch, os.path.join(work, "out"), "--json"], capture_output=True, text=True)
    try: rep = json.loads(rr.stdout[rr.stdout.index("{"):])
    except Exception: rep = {}; print("no JSON report:", rr.stdout[-600:], rr.stderr[-600:])
    if not any("loaded before" in m.get("problem", "") for m in rep.get("masters", [])): print("mods build masters:", rep.get("masters")); ok = False
    if not a.keep: shutil.rmtree(work, ignore_errors=True)
    print("mods masters test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
