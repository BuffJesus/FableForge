#!/usr/bin/env python3
"""Compose stable whole-bank namespaces with granular unnamed-record choices."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from test_fmp_links import read_bank, rewrite_bank
from test_mod_field_details import write_bank, tag, FIELDS, TYPE
from test_mod_file_choice_contributors import snapshot
from test_mod_bank_refusal import fmp
from test_mod_text_choice_contributors import text_bank
from test_fmp_texture_layers import entries


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--gui', type=Path)
    parser.add_argument('--install', type=Path)
    args = parser.parse_args()
    if args.gui and not args.install: parser.error('--gui requires --install')
    work = Path(tempfile.mkdtemp(prefix='mod-whole-bank-layout-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    base = work/'baseline'; write_bank(base, (1, 2, 3, 4), record='')
    def payload(values):
        return b'\x01\0\x01'+b''.join(struct.pack('<Ii', tag(k), v) for k, v in zip(FIELDS, values))
    a, b = work/'BankA', work/'BankB'
    original = read_bank(base)
    additions = [dict(name='', definition=TYPE, data=payload((0, 1, 2, 3))),
                 dict(name='ADDED_OWNER', definition=TYPE, data=payload((1, 0, 2, 3)))]
    rewrite_bank(base, a, [dict(original[0], data=payload((10, 20, 3, 4))), *additions])
    rewrite_bank(base, b, [dict(original[0], data=payload((11, 2, 30, 4))), *additions])
    package = work/'Added.fmp'; fmp(package, [('GameBINEntries', [payload((90, 91, 92, 93))])])
    inputs = {str(p): snapshot(p) for p in (base, a, b, package)}
    picks = work/'picks.txt'; picks.write_text('')
    out = work/'output'; checks = []
    def run(*argv, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, argv)], capture_output=True,
                                text=True, timeout=120)
        assert (result.returncode == 0) == good, (argv, result.stdout, result.stderr)
        return result
    for source in (a, b):
        run('defs', 'merge', base, out, 'game.bin', source)
        assert read_bank(out) == read_bank(source)
        assert (out/'data/CompiledDefs/names.bin').read_bytes()[20:] == (source/'data/CompiledDefs/names.bin').read_bytes()[20:]
        checks.append(source.name+' exact solo bank and name pool')
    run('defs', 'merge', a, out, 'game.bin', base)
    assert read_bank(out) == [original[0], *additions]
    checks.append('shorter shared-prefix source preserves baseline additions')
    for sources in ((a, b), (b, a)):
        for route in ('defs', 'mods'):
            command = (['defs', 'merge', base, out, 'game.bin', *sources, '--fields',
                        repo/'docs/re_reference/def_schema.json'] if route == 'defs' else
                       ['mods', 'merge', base, out, '--with', *sources])
            picks.write_text('')
            report = json.loads(run(*command, '--picks', picks, '--json').stdout)
            defs = report if route == 'defs' else report['defs']
            conflict = next(c for c in defs['conflicts'] if c['record'] == '@unnamed:0')
            assert conflict['index'] == 0 and conflict['field_merged']
            key = conflict['field_conflicts'][0]['choice_key']; assert key
            for winner, first in [('BankA', 10), ('BankB', 11), ('vanilla', 1)]:
                picks.write_text(key+'\t'+winner+'\n')
                run(*command, '--picks', picks)
                rows = read_bank(out)
                assert rows[0]['data'] == payload((first, 20, 30, 4))
                assert rows[1:] == additions
            previous = snapshot(out); picks.write_text(key+'\tMissing\n'); saved = picks.read_bytes()
            error = run(*command, '--picks', picks, good=False)
            assert 'unavailable contributor Missing' in error.stderr
            assert snapshot(out) == previous and picks.read_bytes() == saved
            checks.append(route+' granular choices '+sources[0].name+' first')
    picks.write_text('')
    for sources in ((package, a), (a, package)):
        run('mods', 'merge', base, out, '--with', *sources)
        rows = read_bank(out)
        assert rows[:3] == read_bank(a)
        assert len(rows) == 4 and rows[3]['data'] == payload((90, 91, 92, 93))
        pool = (a/'data/CompiledDefs/names.bin').read_bytes()
        assert (out/'data/CompiledDefs/names.bin').read_bytes()[20:len(pool)] == pool[20:]
        checks.append('FMP additions follow fixed slots '+sources[0].name+' first')
    for case in ('slot', 'ordinal', 'appended-ordinal', 'name-pool'):
        bad = work/case; shutil.copytree(b, bad)
        if case == 'name-pool':
            file = bad/'data/CompiledDefs/names.bin'; raw = bytearray(file.read_bytes()); raw[20] ^= 1
        else:
            file = bad/'data/CompiledDefs/game.bin'; raw = bytearray(file.read_bytes())
            offset = 45 if case == 'appended-ordinal' else 21 if case == 'ordinal' else 29
            struct.pack_into('<I', raw, offset, 999 if 'ordinal' in case else 4)
        file.write_bytes(raw)
        previous = snapshot(out)
        error = run('mods', 'merge', base, out, '--with', a, bad, good=False)
        assert 'whole-bank' in error.stderr and snapshot(out) == previous
        checks.append(case+' collision refuses before writes')
    assert inputs == {str(p): snapshot(p) for p in (base, a, b, package)}
    bank_sources = []
    for original_root in (base, a, b):
        copied = work/('siblings-'+original_root.name); shutil.copytree(original_root, copied)
        for bank in ('script.bin', 'frontend.bin'):
            shutil.copyfile(copied/'data/CompiledDefs/game.bin', copied/'data/CompiledDefs'/bank)
        bank_sources.append(copied)
    sibling_base, sibling_a, sibling_b = bank_sources
    text_bank(sibling_base, 'English', {'SHARED': 'baseline'})
    for source in (sibling_a, sibling_b):
        text_bank(source, 'English', {'SHARED': 'baseline', 'RAW': source.name})
    small = work/'SmallText'
    text_bank(small, 'English', {'SHARED': 'baseline', 'SMALL': 'small addition'})
    inputs = {str(p): snapshot(p) for p in bank_sources}
    command = ['mods', 'merge', sibling_base, out, '--with', sibling_a, sibling_b]
    report = json.loads(run(*command, '--json').stdout)
    assert set(report['definition_banks']) == {'script.bin', 'frontend.bin'}
    fields = {}
    for row in report['defs']['conflicts']:
        if row['record'].endswith('@unnamed:0'):
            fields[row.get('bank', 'game.bin')] = row['field_conflicts'][0]['choice_key']
    assert set(fields) == {'game.bin', 'script.bin', 'frontend.bin'}
    picks.write_text(fields['game.bin']+'\tsiblings-BankA\n'+
                     fields['script.bin']+'\tvanilla\n'+
                     'frontend.bin:@unnamed:0\tsiblings-BankA\n')
    run(*command, '--picks', picks)
    for bank, expected in [('game.bin', 10), ('script.bin', 1), ('frontend.bin', 10)]:
        decoded = work/('read-'+bank); (decoded/'data/CompiledDefs').mkdir(parents=True)
        shutil.copyfile(out/'data/CompiledDefs/names.bin', decoded/'data/CompiledDefs/names.bin')
        shutil.copyfile(out/'data/CompiledDefs'/bank, decoded/'data/CompiledDefs/game.bin')
        rows = read_bank(decoded)
        assert rows[0]['data'] == payload((expected, 20, 30, 4)) and rows[1:] == additions
    assert inputs == {str(p): snapshot(p) for p in bank_sources}
    checks.append('game/script/frontend granular choices are independently scoped')
    for sources in ((small, sibling_a), (sibling_a, small)):
        run('mods', 'merge', sibling_base, out, '--with', *sources)
        text = entries(out/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN')
        assert text['RAW']['id'] == 2 and text['SMALL']['id'] == 3
        checks.append('whole-bank text IDs reserved '+sources[0].name+' first')
    world = work/'BrokenWorld/data/Levels'; world.mkdir(parents=True)
    (world/'FinalAlbion.bwd').write_bytes(b'not a world bank')
    before = snapshot(out)
    refused = run('mods', 'merge', base, out, '--with', a, world.parent.parent, good=False)
    assert 'world merge failed' in refused.stderr and snapshot(out) == before
    checks.append('world failure refuses publication instead of silently skipping a layer')
    if args.install:
        world_base = work/'WorldBaseline'; shutil.copytree(base, world_base)
        (world_base/'data/Levels').mkdir()
        shutil.copyfile(args.install/'data/Levels/FinalAlbion.bwd', world_base/'data/Levels/FinalAlbion.bwd')
        before = snapshot(out)
        refused = run('mods', 'merge', world_base, out, '--with', world.parent.parent, good=False)
        assert 'world source' in refused.stderr and snapshot(out) == before
        checks.append('malformed world source with valid baseline refuses publication')
    if args.gui:
        for source in (sibling_a, sibling_b): run('mods', 'add', sibling_base, source)
        saved_choices = sibling_base/'forge_mods_picks.txt'
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            saved_choices.unlink(missing_ok=True)
            case = work/('gui-'+size); case.mkdir()
            lines = ['wait_maps', 'wait_ready', f'set saveroot {sibling_base.as_posix()}',
                     f'set uiscale {scale}', 'mods_tab 1', 'mods_conflicts', 'wait_mods', 'frames 3']
            def reveal(widget): return [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3']
            for row, option in [(0, 'pick_0'), (1, 'baseline'), (2, 'pick_1')]:
                lines += reveal(f'mod_fields_{row}')+[f'click mod_fields_{row}', 'frames 3']
                lines += reveal(f'mod_field_winner_{row}_0')+[f'click mod_field_winner_{row}_0', 'frames 3']
                widget = (f'mod_field_pick_{row}_0_{option[-1]}' if option.startswith('pick_') else
                          f'mod_field_baseline_{row}_0')
                lines += [f'click {widget}', 'frames 3']
            lines += ['assert_state mods_picks 3', 'assert_state mods_unreported_choices 0', 'quit']
            script = case/'flow.txt'; script.write_text('\n'.join(lines)+'\n')
            with (case/'output.log').open('w') as log:
                result = subprocess.run([str(args.gui.resolve()), '--install', str(args.install.resolve()),
                    '--size', size, '--auto', str(script)], cwd=repo, stdout=log, stderr=subprocess.STDOUT,
                    env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(case/'appdata'),
                             LOCALAPPDATA=str(case/'localappdata')), timeout=240)
            log = Path(str(script)+'.log').read_text()
            assert result.returncode == 0 and 'RESULT PASS' in log, log[-6000:]
            actual_picks = dict(line.split('\t', 1) for line in saved_choices.read_text().splitlines()
                                if line and not line.startswith('#'))
            assert actual_picks == {fields['game.bin']: 'siblings-BankA', fields['frontend.bin']: 'vanilla',
                                    fields['script.bin']: 'siblings-BankB'}
            run('mods', 'build', sibling_base, case/'built')
            for bank, expected in [('game.bin', 10), ('frontend.bin', 1), ('script.bin', 11)]:
                row = read_bank(case/'built', bank)[0]
                assert row['data'] == payload((expected, 20, 30, 4)), (size, bank)
            checks.append(size+' GUI selects and persists independent bank fields')
            print(size, 'bank-scoped GUI choices and exact built bytes: PASS', flush=True)
    (work/'result.json').write_text(json.dumps({'checks': checks, 'inputs_unchanged': True}, indent=2))
    print('PASS:', len(checks), 'whole-bank layout and granular-choice checks', flush=True)


if __name__ == '__main__': main()
