#!/usr/bin/env python3
"""Whole overhaul trees plus Controller Support, with independent bank audits."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_fmp_links import audit, read_bank, package_records
from test_fmp_texture_layers import entries


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True,
                        help='Read-only baseline including definition, asset and world banks')
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--corpus', type=Path, default=repo/'work/nexus_mods/_peek')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--order', choices=('first', 'last', 'both'), default='both')
    parser.add_argument('--overhaul', action='append', choices=('ProjectSeasons', 'AlbionSecretsModpack'))
    parser.add_argument('--mixed', action='store_true', help='Add Ultimate/Better modular spells and three small mods')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='whole-bank-overhaul-acceptance-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    controller = args.corpus/'ControllerSupportFMP/ControllerSupport.fmp'
    dll = work/'FableControllerSupport'; dll.mkdir()
    original_dll = args.corpus/'ControllerSupportFMP/FableControllerSupport.dll'
    shutil.copy2(original_dll, dll/original_dll.name)
    banks = ('game.bin', 'script.bin', 'frontend.bin')
    controller_records = package_records(controller)['GameBINEntries']
    additional = []
    allowed_records = set(controller_records)
    allowed_anonymous_slots = set()
    allowed_text = set()
    if args.mixed:
        packages = [*sorted((args.corpus/'UltimateSpellPack').rglob('*.fmp')),
                    *sorted((args.corpus/'mfvicli/Mods/Better/Modular').glob('*.fmp')),
                    *sorted((args.corpus/'NoBaldnessWithHornsTLC').rglob('*.fmp'))]
        assert len(packages) == 27
        for package in packages:
            records = package_records(package)
            game_records = records.get('GameBINEntries', {})
            allowed_records.update(name for name in game_records if name)
            if '' in game_records:
                named_offsets = {value[1] for name, value in game_records.items() if name}
                allowed_anonymous_slots.update(index for index, value in
                    package_records(package, by_id=True)['GameBINEntries'].items() if value[1] not in named_offsets)
            for bank in ('Text', 'text'): allowed_text.update(records.get(bank, {}))
        additional = [*packages, args.corpus/'WaterWader/Mods/WaterWader', args.corpus/'EnableSprint/Mods/EnableSprint']
    checks = []
    for name in args.overhaul or ('ProjectSeasons', 'AlbionSecretsModpack'):
        source = args.corpus/name
        inputs = [args.tool, controller, original_dll]
        for extra in additional:
            inputs += [extra] if extra.is_file() else sorted(p for p in extra.rglob('*') if p.is_file())
        for root in (args.baseline, source):
            inputs += sorted((root/'data/CompiledDefs').glob('*.bin'))
            inputs += [root/'data/lang/English/text.big']
        before = {str(p): digest(p) for p in inputs}
        source_rows = {bank: read_bank(source, bank) for bank in banks}
        text = entries(source/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN')
        for order in ('first', 'last') if args.order == 'both' else (args.order,):
            sources = [controller, dll, *additional, source] if order == 'first' else [source, *additional, controller, dll]
            out = work/(name+'-'+order)
            result = subprocess.run([str(args.tool.resolve()), 'mods', 'merge', str(args.baseline),
                str(out), '--with', *map(str, sources), '--json'], capture_output=True, text=True, timeout=1200)
            (work/(name+'-'+order+'.stdout')).write_text(result.stdout)
            (work/(name+'-'+order+'.stderr')).write_text(result.stderr)
            assert result.returncode == 0, result.stderr
            assert 'failed' not in result.stderr.lower() and 'skipped' not in result.stderr.lower(), result.stderr
            report = json.loads(result.stdout)
            assert report['world']['mods'] == 1
            for bank in banks:
                actual = read_bank(out, bank); expected = source_rows[bank]
                assert len(actual) >= len(expected)
                for i, original in enumerate(expected):
                    assert actual[i]['name'] == original['name'] and actual[i]['definition'] == original['definition']
                    edited = original['name'] in allowed_records or (not original['name'] and i in allowed_anonymous_slots)
                    if bank != 'game.bin' or not edited:
                        assert actual[i] == original, (name, order, bank, i, original['name'])
                if bank != 'game.bin': assert actual == expected
                else: assert all(r['name'] in allowed_records for r in actual[len(expected):])
            links = audit(out); assert links['checked'] == 14 and not links['errors']
            actual_text = entries(out/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN')
            assert set(actual_text) - set(text) <= allowed_text
            assert set(text) <= set(actual_text)
            for symbol, original in text.items():
                assert actual_text[symbol]['id'] == original['id'], symbol
                if symbol not in allowed_text:
                    assert all(actual_text[symbol][key] == original[key] for key in ('type', 'header', 'payload')), symbol
            installed_dlls = list((out/'Mods').rglob(original_dll.name))
            assert len(installed_dlls) == 1 and digest(installed_dlls[0]) == before[str(original_dll)]
            for binary in ('FinalAlbion.bwd', 'FinalAlbion.wld'):
                assert (out/'data/Levels'/binary).is_file()
            assert before == {str(p): digest(p) for p in inputs}
            checks.append({'overhaul': name, 'controller_order': order, 'controller_references': 14,
                'sources': len(sources), 'mixed': args.mixed,
                'original_game_slots': len(source_rows['game.bin']), 'source_text_entries': len(text),
                'world': report['world'], 'inputs_sha256': before})
            (work/'result.json').write_text(json.dumps({'checks': checks, 'gameplay_tested': False,
                'baseline': str(args.baseline), 'complete': len(checks) ==
                len(args.overhaul or ('ProjectSeasons', 'AlbionSecretsModpack')) * (2 if args.order == 'both' else 1)}, indent=2))
            print(name, order, 'definition slots/payloads, text IDs/metadata, controller and world: PASS', flush=True)


if __name__ == '__main__': main()
