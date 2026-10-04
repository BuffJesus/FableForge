#!/usr/bin/env python3
"""Preserve intact Aeon banks while adding Controller FMP and EgoCore DLL.

Uses a synthetic extracted-level scratch installation. Never runs the game or
certifies the author's manual installation procedure. Retail + Aeon must refuse.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_fmp_links import audit, package_records, read_bank

REPO = Path(__file__).resolve().parents[1]

def inventory(root):
    result = {}
    for path in root.rglob('*'):
        if not path.is_file(): continue
        digest = hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(block)
        result[path.relative_to(root).as_posix()] = digest.hexdigest()
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keep', action='store_true')
    parser.add_argument('--corpus', type=Path, default=REPO/'work/nexus_mods/_peek')
    parser.add_argument('--install', type=Path, default=Path('C:/Programs/Steam/steamapps/common/Fable The Lost Chapters'))
    parser.add_argument('--tool', type=Path, default=REPO/'build/forge-tools.exe')
    parser.add_argument('--cwd', type=Path, default=REPO)
    args = parser.parse_args()
    corpus, retail, tool = args.corpus.resolve(), args.install.resolve(), args.tool.resolve()
    aeon = corpus/'AeonEdition'
    fmp = corpus/'ControllerSupportFMP/ControllerSupport.fmp'
    dll = corpus/'ControllerSupportFMP/FableControllerSupport.dll'
    schema = tool.parent/'docs/re_reference/def_schema.json'
    if not schema.exists(): schema = REPO/'docs/re_reference/def_schema.json'
    required = [tool, aeon/'Data/CompiledDefs/game.bin', aeon/'Data/CompiledDefs/names.bin',
                aeon/'Data/graphics/graphics.big', aeon/'Data/Levels/FinalAlbion_RT.stb',
                retail/'data/Levels/FinalAlbion.wad', retail/'data/CompiledDefs/game.bin', fmp, dll]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        print('Aeon/controller check skipped: missing inputs', *missing, sep='\n  ')
        return 0
    work = Path(tempfile.mkdtemp(prefix='aeon_controller_', dir=REPO/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    command_index = 0
    def run(*argv, refuse=False):
        nonlocal command_index
        command_index += 1
        proc = subprocess.run([str(tool), *map(str, argv)], cwd=args.cwd.resolve(),
                              capture_output=True, text=True, timeout=300)
        (work/f'command-{command_index:02d}.json').write_text(json.dumps(
            {'args': list(map(str, argv)), 'returncode': proc.returncode}, indent=2), encoding='utf-8')
        (work/f'command-{command_index:02d}.log').write_text(proc.stdout+proc.stderr, encoding='utf-8')
        if refuse:
            assert proc.returncode != 0 and 'changed or new unnamed definition entries' in proc.stderr, proc.stderr
        else:
            assert proc.returncode == 0, proc.stdout[-1200:]+proc.stderr[-1200:]
        return proc.stdout
    try:
        source_before = inventory(aeon)
        root, out = work/'aeon-baseline', work/'merged'
        run('wad', 'extract', retail/'data/Levels/FinalAlbion.wad', root)
        shutil.copytree(aeon/'Data', root/'Data', dirs_exist_ok=True)
        levels = root/'data/Levels'
        shutil.copyfile(retail/'data/Levels/FinalAlbion.wad', levels/'_FinalAlbion.wad')
        assert not (levels/'FinalAlbion.wad').exists()
        baseline_before = inventory(root)
        (work/'baseline-inventory.json').write_text(json.dumps(baseline_before, indent=2), encoding='utf-8')
        intact = read_bank(root)
        assert intact == read_bank(aeon)
        rejected = work/'rejected'
        rejected.mkdir(); (rejected/'prior.txt').write_bytes(b'keep prior output')
        rejected_before = inventory(rejected)
        run('mods', 'merge', retail, rejected, '--with', aeon, refuse=True)
        assert inventory(rejected) == rejected_before
        controller = work/'source/FableControllerSupport'
        controller.mkdir(parents=True)
        shutil.copyfile(dll, controller/dll.name)
        decoded = run('defs', 'decode', root, schema, 'FABLE_XBOX_CONTROL_SCHEME_BASE')
        aeon_count = int(re.search(r'Controls\s+Vector_VCActionInputControl__ count=(\d+)', decoded).group(1))
        run('mods', 'add', root, fmp, '--name', 'Controller Support FMP')
        run('mods', 'add', root, controller, '--name', 'Controller Support DLL')
        order = json.loads(run('mods', 'list', root, '--json'))['mods']
        assert [entry['kind'] for entry in order] == ['fmp', 'egocore'], order
        run('mods', 'build', root, out)
        imported = package_records(fmp)['GameBINEntries']
        composed = read_bank(out)
        preserved = 0
        for index, entry in enumerate(intact):
            if entry['name'] not in imported:
                assert composed[index] == entry, (index, entry['name'], entry['definition'])
                preserved += 1
        links = audit(out)
        assert links['checked'] == 14 and not links['errors'], links
        decoded = run('defs', 'decode', out, schema, 'FABLE_XBOX_CONTROL_SCHEME_BASE')
        count = int(re.search(r'Controls\s+Vector_VCActionInputControl__ count=(\d+)', decoded).group(1))
        assert (aeon_count, count) == (70, 141)
        assert (out/'Mods/FableControllerSupport'/dll.name).read_bytes() == dll.read_bytes()
        assert not (out/'Mods/FableControllerSupport/Data').exists()
        assert 'FableControllerSupport\\FableControllerSupport.dll=1' in (out/'Mods.ini').read_text(encoding='utf-8')
        # Output is an overlay; preserve all baseline levels and assets.
        built_files = inventory(out)
        for relative, digest in baseline_before.items():
            if relative.lower().startswith('data/compileddefs/'): continue
            assert relative not in built_files or built_files[relative] == digest, relative
        current = inventory(root)
        assert all(current.get(relative) == digest for relative, digest in baseline_before.items())
        assert inventory(aeon) == source_before
        result = {'passed': True, 'baseline': 'synthetic extracted-level intact Aeon',
                  'preserved_existing_records': preserved,
                  'preserved_unnamed_records': sum(not e['name'] for e in intact),
                  'preserved_baseline_files': len(baseline_before), 'controller_bindings': [aeon_count, count],
                  'links': links, 'obsolete_overhaul_route_refused': True, 'gameplay_tested': False}
        (work/'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(f'PASS: intact Aeon -> Controller FMP -> DLL; {preserved} records preserved '
              f'({result["preserved_unnamed_records"]} unnamed), 14 links, 70 -> 141 bindings', flush=True)
        print('Baseline levels/assets unchanged; partial-overhaul route refused; no game execution.')
        return 0
    finally:
        if args.keep:
            print('scratch:', work)
        else:
            assert work.parent == (REPO/'build').resolve() and work.name.startswith('aeon_controller_')
            shutil.rmtree(work)

if __name__ == '__main__':
    raise SystemExit(main())
