#!/usr/bin/env python3
"""Original GB packs retain their compatible whole-bank identities; content-only
copies retain the level/asset stress checks. Definition and language banks are excluded only in the owned test
fixture, which is not a complete mod installation. Checks the
composer's GB-pack rules: the parked WAD and the settings file are not layers, the loose levels
are repacked into FinalAlbion.wad, the new level folder rides along, provenance covers every
changed level. Skips without the extracted pack (work/nexus_mods/_peek/ProjectSeasons) or the
install. Owned scratch under build/. Nothing touches the install.

  python tools/test_gbpack.py [--root <fable install>] [--keep]
"""
import argparse, json, os, re, shutil, subprocess, sys, tempfile, time
from pathlib import Path
from mod_content_fixture import content_only, accept_stable_overhaul

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKS = {
    "seasons": [("Project Seasons", os.path.join(ROOT, "work", "nexus_mods", "_peek", "ProjectSeasons"))],
    "modpack": [("AlbionSecrets Modpack", os.path.join(ROOT, "work", "nexus_mods", "_peek", "AlbionSecretsModpack"))],
    "both": [("Project Seasons", os.path.join(ROOT, "work", "nexus_mods", "_peek", "ProjectSeasons")),
             ("AlbionSecrets Modpack", os.path.join(ROOT, "work", "nexus_mods", "_peek", "AlbionSecretsModpack"))],
}
UFP = os.path.join(ROOT, "work", "nexus_mods", "_peek", "UnofficialFablePatch", "Patches", "game.bin.patch")


def find_root(explicit: str) -> str:
    if explicit:
        return explicit
    for c in [r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters",
              r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters"]:
        if os.path.exists(os.path.join(c, "data", "CompiledDefs", "game.bin")):
            return c
    return ""


def pristine(root, rel):
    """the install's file, preferring the retail bytes a deploy or an editor write put aside"""
    for sfx in (".retail-bak", ".forgebak", ".forge-orig", ".atlas-orig", ""):
        p = os.path.join(root, rel + sfx)
        if os.path.exists(p): return p
    return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="")
    ap.add_argument("--pack", choices=PACKS, default="seasons")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument('--skip-graphics', action='store_true',
                    help='Exclude bulk graphics from the level/WAD fixture; individual full-source bank audits still run')
    ap.add_argument('--corpus', type=Path, default=Path(ROOT)/'work/nexus_mods/_peek')
    a = ap.parse_args()
    root = find_root(a.root)
    corpus_root = Path(ROOT)/'work/nexus_mods/_peek'
    packs = [(name, str(a.corpus.resolve()/Path(path).relative_to(corpus_root))) for name, path in PACKS[a.pack]]
    if not root or any(not os.path.isdir(os.path.join(path, "data")) for _, path in packs):
        print(f"gb-pack test skipped (no install, or {a.pack} not extracted under work/nexus_mods/_peek)"); return 0
    os.chdir(ROOT)
    tool = os.path.join(ROOT, "build", "forge-tools.exe")
    work = Path(tempfile.mkdtemp(prefix='gb-content-', dir=Path(ROOT)/'build')).resolve()
    assert work.parent == (Path(ROOT)/'build').resolve()
    print('Evidence retained at', work, flush=True)
    scratch = str(work/'root'); out = str(work/'out')
    for d in ("data/Levels/FinalAlbion", "data/CompiledDefs", "data/graphics/pc", "data/lang/English"):
        os.makedirs(os.path.join(scratch, d))
    for rel in ("data/CompiledDefs/game.bin", "data/CompiledDefs/names.bin", "data/CompiledDefs/script.bin", "data/CompiledDefs/frontend.bin",
                "data/Levels/FinalAlbion.wad", "data/Levels/FinalAlbion.wld", "data/Levels/FinalAlbion.bwd", "data/Levels/FinalAlbion.gtg",
                "data/Levels/FinalAlbion.qst", "data/Levels/FinalAlbion_RT.stb", "data/lang/English/text.big"):
        src = pristine(root, rel)
        if not src: print("install lacks", rel); return 1
        shutil.copyfile(src, os.path.join(scratch, rel))
    ok = True

    command_index = 0
    def run(*args):
        nonlocal command_index
        command_index += 1
        r = subprocess.run([tool, *args], capture_output=True, text=True, timeout=300)
        (work/f'command-{command_index:02d}.log').write_text(r.stdout+r.stderr, encoding='utf-8')
        if r.returncode != 0:
            nonlocal ok; ok = False
            print("rc", r.returncode, "for", " ".join(args)); print(r.stdout[-800:], r.stderr[-400:])
        return r

    fixtures = []
    for index, (pack_name, pack_path) in enumerate(packs):
        accept_stable_overhaul(tool, scratch, pack_path, work/f'accepted-{index}', ROOT, work/f'acceptance-{index}')
        fixture = content_only(pack_path, work/'content'/pack_name, include_graphics=not a.skip_graphics)
        fixtures.append(fixture)
    t0 = time.time()
    options = [argument for fixture in fixtures for argument in ('--with', str(fixture))]
    r = run("mods", "merge", scratch, out, *options)
    dt = time.time() - t0
    o = r.stdout
    for pack_name, _ in packs:
        if f"{pack_name} parks the retail WAD as _FinalAlbion.wad: skipped" not in o: print("parked WAD not recognised:", pack_name); ok = False
    if "ships userst.ini: not applied" not in o: print("userst.ini not skipped"); ok = False
    repacked = re.search(r"FinalAlbion\.wad rebuilt: (\d+) level file\(s\) repacked", o)
    if not repacked or int(repacked.group(1)) < 400: print("loose levels not repacked:", [l for l in o.splitlines() if "wad" in l.lower()]); ok = False
    tng = re.search(r"level TNG merge: (\d+) levels \((\d+) thing-merged, (\d+) single-editor copies\), (\d+) thing conflicts", o)
    if not tng: print("TNG merge absent:", [l for l in o.splitlines() if "TNG" in l]); ok = False
    elif len(packs) == 1 and int(tng.group(4)) != 0:
        print("single pack has unexpected TNG conflicts:", tng.group(0)); ok = False
    elif len(packs) > 1 and (int(tng.group(2)) == 0 or int(tng.group(4)) == 0):
        print("overlapping TNG edits were not reported:", tng.group(0)); ok = False
    if os.path.exists(os.path.join(out, "data", "Levels", "_FinalAlbion.wad")) or os.path.exists(os.path.join(out, "userst.ini")): print("parked WAD / settings copied into the build"); ok = False
    if len(os.listdir(os.path.join(out, "data", "Levels", "ProjectAutumn"))) != 101: print("ProjectAutumn folder not carried"); ok = False
    if not os.path.exists(os.path.join(out, "data", "Levels", "FinalAlbion_RT.stb")): print("STB layer missing"); ok = False
    prov = json.load(open(os.path.join(out, "forge_mods_provenance.json")))
    if tng and len(prov["levels"]) != int(tng.group(1)): print("provenance levels", len(prov["levels"])); ok = False
    # the retail WAD wins over loose files in-engine: every level Seasons ships is now inside the rebuilt WAD
    lst = run("wad", "list", os.path.join(out, "data", "Levels", "FinalAlbion.wad")).stdout
    if "arena.tng" not in lst.lower(): print("rebuilt WAD lacks the levels"); ok = False
    extracted = os.path.join(scratch, "extracted")
    run("wad", "extract", os.path.join(out, "data", "Levels", "FinalAlbion.wad"), extracted)
    checked = 0
    out_levels = os.path.join(out, "data", "Levels")
    for folder in (out_levels, os.path.join(out_levels, "FinalAlbion")):
        for name in os.listdir(folder):
            if not name.lower().endswith((".lev", ".tng")): continue
            loose = os.path.join(folder, name)
            packed = os.path.join(extracted, "Data", "Levels", os.path.relpath(loose, out_levels))
            if not os.path.isfile(packed) or open(loose, "rb").read() != open(packed, "rb").read():
                print("built WAD and loose file differ:", os.path.relpath(loose, out_levels)); ok = False
            checked += 1
    if repacked and checked != int(repacked.group(1)):
        print("WAD payload check count differs:", checked, repacked.group(1)); ok = False
    print("WAD payloads checked:", checked)
    print(f"{a.pack} build took {dt:.1f}s")
    for line in o.splitlines():
        if "changes applied" in line or "TNG merge:" in line or "FinalAlbion.wad rebuilt:" in line:
            print(line)
    if not a.keep:
        for target in [Path(scratch), Path(out), *fixtures]:
            assert target.resolve().is_relative_to(work) and target.resolve() != work
            shutil.rmtree(target, ignore_errors=True)
    (work/'result.json').write_text(json.dumps({'passed': ok, 'scope': 'compatible full-source bank audit and content-only level/asset fixtures',
        'fixture_graphics_included': not a.skip_graphics,
        'wad_payloads_checked': checked, 'complete_mod_installation': False}, indent=2), encoding='utf-8')
    print("GB full-source bank audit and content-only fixture", "PASS" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
