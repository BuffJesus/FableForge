#!/usr/bin/env python3
"""Controller preservation, choices and removal in a real 31-source scratch stack.

The two Slow Time variants and overlapping spell packs deliberately create
conflicts. This is composition coverage, not a recommended gameplay load order.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile

from test_fmp_creature_optional_links import digest
from test_fmp_links import audit, crc0, package_records, read_bank
from test_fmp_weapon_links import run
from test_mod_file_choice_contributors import snapshot


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--corpus', type=Path, default=repo/'work/nexus_mods/_peek')
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='controller-stack-lifecycle-', dir=args.work_root)).resolve()
    assert work.parent == args.work_root.resolve()
    print('Evidence retained at', work, flush=True)
    corpus = args.corpus.resolve()
    controller = corpus/'ControllerSupportFMP/ControllerSupport.fmp'
    original_dll = corpus/'ControllerSupportFMP/FableControllerSupport.dll'
    ultimate = sorted((corpus/'UltimateSpellPack').rglob('*.fmp'))
    better = sorted((corpus/'mfvicli/Mods/Better/Modular').glob('*.fmp'))
    small = sorted((corpus/'NoBaldnessWithHornsTLC').rglob('*.fmp'))
    extras = [*small, corpus/'WaterWader/Mods/WaterWader', corpus/'EnableSprint/Mods/EnableSprint']
    assert (len(ultimate), len(better), len(small)) == (18, 8, 1)
    relatives = ('data/CompiledDefs/game.bin', 'data/CompiledDefs/names.bin',
                 'data/graphics/graphics.big', 'data/graphics/pc/textures.big',
                 'data/lang/English/text.big', 'data/Misc/pc/effects.big')
    inputs = [args.tool, controller, original_dll, *ultimate, *better, *small]
    for folder in extras[-2:]: inputs += [p for p in folder.rglob('*') if p.is_file()]
    baseline_sources = []
    base = work/'baseline'
    for relative in relatives:
        source = args.install/relative
        backup = Path(str(source)+'.retail-bak')
        if relative.startswith('data/CompiledDefs/') and backup.exists(): source = backup
        baseline_sources.append(source)
        target = base/relative; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    inputs += baseline_sources
    before = {str(p): digest(p) for p in inputs}
    baseline_hashes = {r: digest(base/r) for r in relatives}
    dll = work/'sources/FableControllerSupport'; dll.mkdir(parents=True)
    shutil.copy2(original_dll, dll/original_dll.name)
    sources = [controller, dll, *ultimate, *better, *extras]
    labels = ['ControllerFMP', 'ControllerDLL', *[p.stem for p in ultimate+better],
              small[0].stem, 'WaterWader', 'EnableSprint']
    assert len(set(labels)) == len(labels) == 31
    for source, label in zip(sources, labels):
        run(args.tool, 'mods', 'add', base, source, '--name', label)
    expected_controller = package_records(controller)['GameBINEntries']
    out = work/'built'; picks = work/'picks.txt'; picks.write_text('')
    checks = []

    def build(label, speed=None):
        report = json.loads(run(args.tool, 'mods', 'build', base, out, '--picks', picks, '--json').stdout)
        records = {r['name']: r for r in read_bank(out) if r['name']}
        links = audit(out); assert links['checked'] == 14 and not links['errors']
        for name in ('FABLE_XBOX_CONTROL_SCHEME_BASE', 'PLAYER_GUI_PC', 'PLAYER_GUI_DEFAULT'):
            assert records[name]['data'] == expected_controller[name][0], (label, name)
        scheme = records['FABLE_XBOX_CONTROL_SCHEME_BASE']['data']
        tag = struct.pack('<I', crc0('Controls')); assert scheme.count(tag) == 1
        assert struct.unpack_from('<I', scheme, scheme.index(tag)+4)[0] == 141
        installed_dlls = list((out/'Mods').rglob('FableControllerSupport.dll'))
        assert len(installed_dlls) == 1 and digest(installed_dlls[0]) == digest(original_dll)
        if speed is not None:
            data = records['SPECIAL_ABILITIES_BERSERK_DEF_INSTANCE']['data']
            tag = struct.pack('<I', crc0('SpeedMultiplier')); assert data.count(tag) == 1
            assert struct.unpack_from('<f', data, data.index(tag)+4)[0] == speed
        checks.append({'case': label, 'bindings': 141, 'references': 14,
                       'conflicts': len(report['defs']['conflicts']), 'berserk_speed': speed})
        print(label, 'controller bindings/UI/DLL/references: PASS', flush=True)
        return report

    build('controller-first', 2.25)
    for label in ('ControllerFMP', 'ControllerDLL'):
        run(args.tool, 'mods', 'move', base, label, '30')
    build('controller-last', 2.25)
    for package in reversed(better): run(args.tool, 'mods', 'move', base, package.stem, '0')
    report = build('spell-order-reversed', 2.0)
    conflict = next(c for c in report['defs']['conflicts'] if c['record'] == 'SPECIAL_ABILITIES_BERSERK_DEF_INSTANCE')
    field = next(f for f in conflict['field_conflicts'] if f['field'] == 'SpeedMultiplier')
    assert field['choice_key']
    picks.write_text(field['choice_key']+'\tBetterBerserk\n', encoding='utf-8')
    build('explicit-spell-choice', 2.25)
    run(args.tool, 'mods', 'disable', base, 'BetterBerserk')
    saved, previous = picks.read_bytes(), snapshot(out)
    failure = run(args.tool, 'mods', 'build', base, out, '--picks', picks, '--json', good=False)
    assert 'unavailable contributor BetterBerserk' in failure.stderr
    assert snapshot(out) == previous and picks.read_bytes() == saved
    picks.write_text(''); build('clear-unavailable-choice', 2.0)
    run(args.tool, 'mods', 'enable', base, 'BetterBerserk')
    build('reenable-spell', 2.0)
    for label in ('ControllerFMP', 'ControllerDLL'): run(args.tool, 'mods', 'disable', base, label)
    run(args.tool, 'mods', 'build', base, out, '--picks', picks, '--json')
    control = work/'without-controller'
    run(args.tool, 'mods', 'merge', base, control, '--with', *better, *ultimate, *extras, '--json')
    for filename in ('game.bin', 'names.bin'):
        assert (out/'data/CompiledDefs'/filename).read_bytes() == (control/'data/CompiledDefs'/filename).read_bytes()
    assert not list(out.rglob('FableControllerSupport.dll'))
    for label in ('ControllerFMP', 'ControllerDLL'): run(args.tool, 'mods', 'remove', base, label)
    run(args.tool, 'mods', 'build', base, out, '--picks', picks, '--json')
    for filename in ('game.bin', 'names.bin'):
        assert (out/'data/CompiledDefs'/filename).read_bytes() == (control/'data/CompiledDefs'/filename).read_bytes()
    assert not list(out.rglob('FableControllerSupport.dll'))
    for source, label in zip((controller, dll), ('ControllerFMP', 'ControllerDLL')):
        run(args.tool, 'mods', 'add', base, source, '--name', label)
    build('readd-controller', 2.0)
    assert baseline_hashes == {r: digest(base/r) for r in relatives}
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'sources': list(map(str, sources)),
        'unavailable_choice_refusal': failure.stderr.strip(), 'disable_and_remove_match_independent_stack': True,
        'inputs_sha256': before, 'baseline_sources': list(map(str, baseline_sources)),
        'baseline_data_unchanged': True, 'inputs_unchanged': True, 'gameplay_tested': False}, indent=2))
    print('PASS: 31-source controller stack, field choices, disable/remove/readd and unchanged inputs', flush=True)


if __name__ == '__main__':
    main()
