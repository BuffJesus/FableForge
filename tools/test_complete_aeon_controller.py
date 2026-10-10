#!/usr/bin/env python3
"""Forge installs original Aeon, legacy FMP and full runtime sources in scratch."""
import argparse, hashlib, json, shutil, subprocess, tempfile
from pathlib import Path
from test_fmp_links import audit, package_records, read_bank
from test_mod_bank_refusal import snapshot


def main():
    repo = Path(__file__).resolve().parents[1]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--install', type=Path, required=True)
    p.add_argument('--corpus', type=Path, required=True)
    p.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = p.parse_args()
    aeon = args.corpus/'AeonEdition'
    fmp = args.corpus/'ControllerSupportFMP/ControllerSupport.fmp'
    runtime = args.corpus/'FableControllerSupport/Mods/FableControllerSupport'
    assert (runtime/'Data').is_dir()
    for path in (aeon/'Data/CompiledDefs/frontend.bin', fmp, runtime/'FableControllerSupport.dll'):
        assert path.is_file(), path
    work = Path(tempfile.mkdtemp(prefix='complete-aeon-controller-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    base, out = work/'baseline', work/'built'
    defs = base/'data/CompiledDefs'; defs.mkdir(parents=True)
    for bank in ('names.bin', 'game.bin', 'script.bin', 'frontend.bin'):
        shutil.copyfile(args.install/'data/CompiledDefs'/bank, defs/bank)
    levels = base/'data/Levels'; levels.mkdir()
    shutil.copyfile(args.install/'data/Levels/FinalAlbion.wad', levels/'FinalAlbion.wad')
    print('Hashing complete source inventories', flush=True)
    before = {str(path): snapshot(path) for path in (aeon, fmp, runtime, base)}
    (work/'before.json').write_text(json.dumps(before, indent=2))
    def run(label, *argv):
        result = subprocess.run([str(args.tool.resolve()), *map(str, argv)],
                                capture_output=True, timeout=1800, cwd=repo)
        (work/(label+'.log')).write_bytes(result.stdout+result.stderr)
        assert result.returncode == 0, (label, result.stderr[-4000:])
        return result.stdout
    print('Installing original three sources through Forge', flush=True)
    report = json.loads(run('install', 'mods', 'merge', base, out, '--install-data',
                           '--with', aeon/'Data', fmp, runtime, '--json'))
    (work/'report.json').write_text(json.dumps(report, indent=2))
    links = audit(out)
    assert links['checked'] == 14 and not links['errors'], links
    imported = package_records(fmp)['GameBINEntries']
    original, composed = read_bank(aeon), read_bank(out)
    preserved = 0
    for index, entry in enumerate(original):
        if entry['name'] not in imported:
            assert composed[index] == entry, (index, entry['name'])
            preserved += 1
    # Following game definitions may extend names.bin; sibling payloads and
    # every authored asset/level must retain the complete overhaul's bytes.
    checked_assets = 0
    for rel, digest in before[str(aeon)].items():
        if Path(rel).name in ('game.bin', 'names.bin'): continue
        target = out/rel
        assert target.is_file(), rel
        assert hashlib.sha256(target.read_bytes()).hexdigest() == digest, rel
        checked_assets += 1
    assert (out/'Mods/FableControllerSupport/FableControllerSupport.dll').read_bytes() == (runtime/'FableControllerSupport.dll').read_bytes()
    assert not (out/'Mods/FableControllerSupport/Data').exists()
    assert report['runtime_plan'][0]['data_excluded']
    assert 'FableControllerSupport\\FableControllerSupport.dll=1' in (out/'Mods.ini').read_text()
    print('Checking rebuilt WAD and unchanged original sources', flush=True)
    extracted = work/'extracted'
    run('extract', 'wad', 'extract', out/'data/Levels/FinalAlbion.wad', extracted)
    checked_levels = 0
    for path in (aeon/'Data/Levels').rglob('*'):
        if path.suffix.lower() not in ('.lev', '.tng'): continue
        rel = path.relative_to(aeon)
        assert (extracted/rel).read_bytes() == path.read_bytes(), rel
        checked_levels += 1
    assert {str(path): snapshot(path) for path in (aeon, fmp, runtime, base)} == before
    result = {'passed': True, 'definition_records_preserved': preserved,
              'named_links_checked': 14, 'assets_checked': checked_assets,
              'authored_wad_levels_checked': checked_levels,
              'runtime_data_excluded': True, 'source_inventories_unchanged': True,
              'gameplay_tested': False, 'android_tested': False}
    (work/'result.json').write_text(json.dumps(result, indent=2))
    print('PASS:', json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
