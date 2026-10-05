#!/usr/bin/env python3
"""Case-only TNG/QST names share composition and saved-choice identity."""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot
from test_mod_tng_choice_contributors import tng
from test_mod_qst_choice_contributors import qst, TEST


def values(path, family):
    text = path.read_text()
    if family == 'tng':
        return {int(uid): int(health) for uid, health in re.findall(
            r'UID\s+(\d+);(?:(?!EndThing;).)*?Health\s+(\d+);', text, re.S)}
    assert TEST in text and '// retained preamble' in text and 'unknown trailing text' in text
    return {name: flag == 'TRUE' for name, flag in re.findall(r'AddQuest\("([^"]+)",\s*(TRUE|FALSE)\);', text)}


def put(folder, family, relative, first, second):
    if family == 'tng': tng(folder, first, unrelated=second)
    else: qst(folder, first, unchanged=second)
    source = folder/'data/Levels'/('Test.'+family)
    target = folder/'data/Levels'/relative
    if source.as_posix() != target.as_posix():
        target.parent.mkdir(parents=True, exist_ok=True)
        source.rename(target)


def write_wad(path, records):
    """Independent compact WAD fixture, with opaque entry metadata."""
    raw = bytearray(32); locations = []
    for name, payload in records:
        raw += b'\0'*((-len(raw)) % 2048)
        locations.append(len(raw)); raw += payload
    footer = len(raw); raw += struct.pack('<I', 0)
    for index, ((name, payload), offset) in enumerate(zip(records, locations)):
        encoded = name.encode('ascii'); info = b'opaque fixture metadata'
        raw += struct.pack('<7I', 42, index+1, 1, len(payload), offset, 17, len(encoded))+encoded
        raw += struct.pack('<3I', 29, 0, len(info))+info
    struct.pack_into('<I', raw, 20, len(records)); struct.pack_into('<I', raw, 28, footer)
    path.write_bytes(raw)


def wad_entries(path):
    raw = path.read_bytes(); count = struct.unpack_from('<I', raw, 20)[0]
    cursor = struct.unpack_from('<I', raw, 28)[0]
    stats = struct.unpack_from('<I', raw, cursor)[0]; cursor += 4+stats*8
    result = {}
    for _ in range(count):
        magic, ident, kind, size, offset, crc, length = struct.unpack_from('<7I', raw, cursor)
        assert magic == 42; cursor += 28
        name = raw[cursor:cursor+length].rstrip(b'\0').decode('ascii'); cursor += length
        timestamp, deps = struct.unpack_from('<2I', raw, cursor); cursor += 8
        dependencies = []
        for _ in range(deps):
            length = struct.unpack_from('<I', raw, cursor)[0]; cursor += 4
            dependencies.append(raw[cursor:cursor+length]); cursor += length
        length = struct.unpack_from('<I', raw, cursor)[0]; cursor += 4
        info = raw[cursor:cursor+length]; cursor += length
        result[name] = {'id': ident, 'type': kind, 'crc': crc, 'timestamp': timestamp,
                        'dependencies': dependencies, 'info': info, 'payload': raw[offset:offset+size]}
    return result


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--complete-data', action='store_true')
    parser.add_argument('--gui', type=Path)
    parser.add_argument('--install', type=Path)
    args = parser.parse_args()
    if args.gui and not args.install: parser.error('--gui requires --install')
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-level-case-identity-', dir=args.work_root)).resolve()
    print('Evidence retained at', work, flush=True)
    checks = []; gui_cases = []
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
            capture_output=True, text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    for family in ('tng', 'qst'):
        for nested in (False, True):
            for variant in range(3):
                case = work/(family+'-'+str(int(nested))+'-'+str(variant))
                root = case/'install'; write_bank(root, (1, 2, 3, 4))
                base_rel = Path('FinalAlbion/Test.'+family) if nested else Path('Test.'+family)
                a_rel = base_rel.with_suffix('.'+family.upper()) if variant == 1 else base_rel
                b_name = 'test.'+(family.upper() if variant == 2 else family)
                b_rel = Path('finalalbion')/b_name if nested else Path(b_name)
                baseline = (10, 99) if family == 'tng' else (False, True)
                authored = (20, 100) if family == 'tng' else (True, False)
                put(root, family, base_rel, *baseline)
                stock = family == 'tng' and nested and variant == 1
                wad_name = 'Data\\Levels\\FinalAlbion\\Test.tng'
                other_name = 'Data\\Misc\\Untouched.bin'
                stock_original = None
                if stock:
                    baseline_path = root/'data/Levels'/base_rel
                    write_wad(root/'data/Levels/FinalAlbion.wad',
                        [(wad_name, baseline_path.read_bytes()), (other_name, b'opaque unchanged WAD member')])
                    stock_original = wad_entries(root/'data/Levels/FinalAlbion.wad'); baseline_path.unlink()
                a, b = case/'PackA', case/'PackB'
                put(a, family, a_rel, authored[0], baseline[1])
                put(b, family, b_rel, baseline[0], authored[1])
                sources = [a, b]; complete = None
                if args.complete_data:
                    complete = case/'Complete'; write_bank(complete, (1, 2, 3, 4))
                    for binary in ('script.bin', 'frontend.bin'):
                        (complete/'data/CompiledDefs'/binary).write_bytes((complete/'data/CompiledDefs/game.bin').read_bytes())
                    run('mods', 'add', root, complete, '--install-data'); sources.insert(0, complete)
                for source in (a, b): run('mods', 'add', root, source)
                inputs = {str(p): snapshot(p) for p in [root, *sources]}
                picks = case/'picks.txt'; picks.write_text('')
                body = 'uid:123456' if family == 'tng' else 'Existing'
                alias = family+':'+str(base_rel).replace('/', '\\').upper()+'|'+body
                expected = {123456: authored[0], 123457: authored[1]} if family == 'tng' else {'Existing': authored[0], 'Unchanged': authored[1]}
                original = dict(expected); original[123456 if family == 'tng' else 'Existing'] = baseline[0]
                for route in ('merge', 'build'):
                    output = case/route
                    command = ['mods', route, root, output]
                    if route == 'merge':
                        command += ['--with', *sources]
                        if complete: command += ['--install-data']
                    command += ['--picks', picks, '--json']
                    report = json.loads(run(*command).stdout)
                    rows = report[family]['levels' if family == 'tng' else 'files']
                    assert len(rows) == 1 and rows[0]['mods'] == ['PackA', 'PackB']
                    assert rows[0]['mode'] == ('thing-merged' if family == 'tng' else 'statement-merged')
                    assert values(output/'data/Levels'/base_rel, family) == expected
                    if stock:
                        assert report['wad']['repacked'] == 1 and report['wad']['appended'] == 0
                        rewritten = wad_entries(output/'data/Levels/FinalAlbion.wad')
                        assert rewritten[other_name] == stock_original[other_name]
                        assert rewritten[wad_name]['payload'] == (output/'data/Levels'/base_rel).read_bytes()
                        assert {k:v for k,v in rewritten[wad_name].items() if k!='payload'} == \
                               {k:v for k,v in stock_original[wad_name].items() if k!='payload'}
                    assert len(list((output/'data/Levels').rglob('*.'+family))) == 1
                    (output/'sentinel').write_bytes(b'prior output')
                    checks.append({'case': case.name, 'route': route, 'disjoint_edits_coalesced': True})
                    for winner in ('RemovedPack', 'PackB'):
                        picks.write_text(alias+'\t'+winner+'\n'); before = snapshot(output)
                        failure = run(*command, good=False)
                        assert 'selects unavailable contributor '+winner in failure.stderr and snapshot(output) == before
                        report = json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
                        row = report[family]['levels' if family == 'tng' else 'files'][0]['conflicts'][0]
                        assert row['choice_unavailable'] and row['winner'] == 'PackA'
                        (case/(route+'-'+winner+'.log')).write_text(failure.stderr)
                        checks.append({'case': case.name, 'route': route, 'alias_refused': winner})
                    for winner, wanted in [('PackA', expected), ('vanilla', original)]:
                        picks.write_text(alias+'\t'+winner+'\n'); run(*command)
                        assert values(output/'data/Levels'/base_rel, family) == wanted
                        if stock:
                            rewritten = wad_entries(output/'data/Levels/FinalAlbion.wad')
                            assert rewritten[wad_name]['payload'] == (output/'data/Levels'/base_rel).read_bytes()
                            assert rewritten[other_name] == stock_original[other_name]
                        assert (output/'sentinel').read_bytes() == b'prior output'
                        checks.append({'case': case.name, 'route': route, 'alias_applied': winner})
                    picks.write_text('')
                assert inputs == {str(p): snapshot(p) for p in [root, *sources]}
                if nested and variant == 1:
                    # An uppercase authored extension must not alter installed spelling on restore.
                    run('mods', 'deploy', root)
                    try:
                        assert values(root/'data/Levels'/base_rel, family) == expected
                        if stock:
                            assert wad_entries(root/'data/Levels/FinalAlbion.wad')[wad_name]['payload'] == \
                                   (root/'data/Levels'/base_rel).read_bytes()
                        picks.write_text(alias+'\tvanilla\n')
                        report = json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
                        row = report[family]['levels' if family == 'tng' else 'files'][0]['conflicts'][0]
                        assert row['winner'] == 'vanilla' and row['overridden']
                    finally:
                        run('mods', 'undeploy', root); picks.write_text('')
                    assert inputs == {str(p): snapshot(p) for p in [root, *sources]}
                    checks.append({'case': case.name, 'uppercase_deployment_review_exact_undeploy': True})
                if nested and variant == 2: gui_cases.append((family, case, root, a, b, sources, base_rel, alias, expected, complete))
    if args.gui:
        for family, case, root, a, b, sources, relative, alias, expected, complete in gui_cases:
            # A later scan uses the other authored spelling; saved aliases must still match.
            run('mods', 'move', root, 'PackB', 1 if complete else 0)
            before_inputs = {str(p): snapshot(p) for p in [root, *sources]}
            saved = root/'forge_mods_picks.txt'
            for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
                view = case/size; view.mkdir(); saved.write_text(alias+'\tRemovedPack\n')
                script = view/'review.txt'
                lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}', f'set uiscale {scale}',
                    'mods_tab 1', 'mods_conflicts', 'wait_mods', 'frames 3', 'assert_state mod_action_failed 0',
                    'assert_state mods_conflicts 1', 'assert_state mods_unavailable_choices 1',
                    'assert_state mods_unreported_choices 0', 'assert_state mods_first_winner PackA',
                    'reveal mod_unavailable_choice_0', 'frames 3', 'reveal mod_unavailable_choice_0', 'frames 3',
                    'clear_toasts', 'mouse_move viewport', f'screenshot {(view/"alias.png").as_posix()}',
                    f'mod_pick {alias} PackA', 'frames 3', 'assert_state mods_unavailable_choices 0',
                    'assert_state mods_picks 1', 'mods_conflicts', 'wait_mods', 'frames 3',
                    'assert_state mods_first_winner PackA', 'reveal mod_winner_0', 'frames 3',
                    'reveal mod_winner_0', 'frames 3', 'click mod_winner_0', 'frames 3',
                    'click mod_load_order_0', 'frames 3', 'assert_state mods_picks 0',
                    'mods_conflicts', 'wait_mods', 'frames 3', 'assert_state mods_conflicts 0', 'quit']
                script.write_text('\n'.join(lines)+'\n')
                result = subprocess.run([str(args.gui.resolve()), '--install', str(args.install.resolve()),
                    '--size', size, '--auto', str(script)], cwd=repo, capture_output=True, text=True, timeout=180,
                    env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(view/'appdata'),
                             LOCALAPPDATA=str(view/'localappdata')))
                (view/'output.log').write_text(result.stdout+result.stderr)
                log = Path(str(script)+'.log').read_text()
                assert result.returncode == 0 and 'RESULT PASS' in log, log[-5000:]
                assert not saved.exists()
                output = view/'built'; run('mods', 'build', root, output)
                assert values(output/'data/Levels'/relative, family) == expected
                checks.append({'family': family, 'size': size, 'alias_review_selection_dropdown_clear': True})
                print(family, size, 'alias review, selection and dropdown clear: PASS', flush=True)
            assert before_inputs == {str(p): snapshot(p) for p in [root, *sources]}
    (work/'result.json').write_text(json.dumps({'ok': True, 'checks': checks, 'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'level case identity checks')


if __name__ == '__main__': main()
