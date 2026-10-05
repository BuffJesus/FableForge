#!/usr/bin/env python3
"""Unavailable language/string winners refuse builds without rewriting outputs."""
import argparse
import json
import os
from pathlib import Path
import struct
import re
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot
from test_mod_asset_choice_contributors import bank
from test_fmp_texture_layers import entries
from test_fmp_legacy_text_bank import decoded


def text_bank(root, language, items):
    records = {}
    for index, (name, text) in enumerate(items.items()):
        payload = text.encode('utf-16-le')+b'\0\0'
        for value in (b'SpeechBank', b'Speaker', name.encode('ascii')):
            payload += struct.pack('<I', len(value))+value
        payload += struct.pack('<Ii', 1, 1)+b'emphasis\0'
        records[name] = {'id': index+1, 'type': 0, 'header': b'', 'payload': payload}
    bank(root/'data/lang'/language/'text.big', 'TEXT_'+language.upper()+'_MAIN', records)


def text_values(root, language):
    found = entries(root/'data/lang'/language/'text.big', 'TEXT_'+language.upper()+'_MAIN', None, True)
    return {name: decoded(entry['payload'])[0] for name, entry in found.items()}


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
    work = Path(tempfile.mkdtemp(prefix='mod-text-choice-contributors-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4))
    for language, text in [('English', 'baseline'), ('French', 'original')]:
        text_bank(root, language, {'SHARED': text, 'UNCHANGED': 'untouched'})
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
                                capture_output=True, text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    sources = []; complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        defs = complete/'data/CompiledDefs'
        for name in ('script.bin', 'frontend.bin'):
            (defs/name).write_bytes((defs/'game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    for name, english, french, extra in [('PackA', 'first Ω', 'premier', False),
                                         ('PackB', 'last 漢字', 'dernier', False),
                                         ('PackC', 'baseline', 'tierce', True)]:
        source = work/name
        items = {'SHARED': english, 'UNCHANGED': 'untouched'}
        if extra: items['OTHER'] = 'independent'
        text_bank(source, 'English', items)
        text_bank(source, 'French', {'SHARED': french, 'UNCHANGED': 'untouched'})
        sources.append(source); run('mods', 'add', root, source)
    inputs = [root, *sources] + ([complete] if complete else [])
    before_inputs = {str(p): snapshot(p) for p in inputs}
    picks = work/'picks.txt'; checks = []
    def review():
        return json.loads(run('mods', 'conflicts', root, '--picks', picks, '--json').stdout)
    def expected(output, shared):
        assert text_values(output, 'English') == {'SHARED': shared, 'UNCHANGED': 'untouched', 'OTHER': 'independent'}
        assert text_values(output, 'French') == {'SHARED': 'tierce', 'UNCHANGED': 'untouched'}
        original = entries(root/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN', {'UNCHANGED'}, True)['UNCHANGED']
        actual = entries(output/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN', {'UNCHANGED'}, True)['UNCHANGED']
        assert actual['payload'] == original['payload'] and actual['id'] == original['id']
    for route in ('merge', 'build'):
        output = work/route; command = ['mods', route, root, output]
        if route == 'merge':
            command += ['--with', *([complete] if complete else []), *sources]
            if complete: command += ['--install-data']
        command += ['--picks', picks]
        picks.write_text(''); run(*command); expected(output, 'last 漢字')
        (output/'sentinel').write_bytes(b'prior output')
        for winner in ('RemovedPack', 'PackC', 'base', 'none'):
            picks.write_text('text:English|SHARED\t'+winner+'\n'); before = snapshot(output); saved = picks.read_bytes()
            failed = run(*command, good=False)
            assert 'mods text choice: English/SHARED selects unavailable contributor '+winner in failed.stderr
            row = next(r for r in review()['text']['contested'] if r['language'] == 'English')
            assert row['choice_unavailable'] and not row['overridden'] and row['winner'] == 'PackB'
            assert row['mods'] == ['PackA', 'PackB']
            assert snapshot(output) == before and picks.read_bytes() == saved
            (work/f'{route}-{winner}.log').write_text(failed.stderr)
            checks.append({'route': route, 'winner': winner, 'refused_preserved_reviewable': True})
        for winner, content in [('PackA', 'first Ω'), ('PackB', 'last 漢字'), ('vanilla', 'baseline')]:
            picks.write_text('text:English|SHARED\t'+winner+'\n'); run(*command); expected(output, content)
            assert (output/'sentinel').read_bytes() == b'prior output'
            selected = root if winner == 'vanilla' else sources[0 if winner == 'PackA' else 1]
            source = entries(selected/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN', {'SHARED'}, True)['SHARED']
            result = entries(output/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN', {'SHARED'}, True)['SHARED']
            assert result['payload'] == source['payload'] and result['id'] == 1
            checks.append({'route': route, 'winner': winner, 'accepted_exact_payload_language_scope': True})
        english_path = Path('data/lang/English/text.big')
        old_b = (sources[1]/english_path).read_bytes(); old_a = (sources[0]/english_path).read_bytes()
        try:
            text_bank(sources[1], 'English', {'SHARED': 'baseline', 'UNCHANGED': 'untouched'})
            picks.write_text('text:English|SHARED\tPackB\n'); before = snapshot(output)
            failed = run(*command, good=False)
            assert 'selects unavailable contributor PackB' in failed.stderr and snapshot(output) == before
            assert not any(r['language'] == 'English' for r in review()['text']['contested'])
            picks.write_text('text:English|SHARED\tPackA\n'); run(*command); expected(output, 'first Ω')
            checks.append({'route': route, 'case': 'one remaining contributor', 'refusal_and_valid_choice': True})
            text_bank(sources[0], 'English', {'SHARED': 'baseline', 'UNCHANGED': 'untouched'})
            picks.write_text('text:English|SHARED\tRemovedPack\n'); run(*command); expected(output, 'baseline')
            checks.append({'route': route, 'case': 'no current contributions', 'inactive': True})
        finally:
            (sources[0]/english_path).write_bytes(old_a); (sources[1]/english_path).write_bytes(old_b)
        old_base = (root/english_path).read_bytes()
        try:
            text_bank(root, 'English', {'UNCHANGED': 'untouched'})
            picks.write_text('text:English|SHARED\tvanilla\n'); run(*command)
            assert text_values(output, 'English') == {'UNCHANGED': 'untouched', 'OTHER': 'independent'}
            checks.append({'route': route, 'case': 'absent baseline entry', 'restored_absence': True})
        finally:
            (root/english_path).write_bytes(old_base)
        picks.write_text('text:English|INACTIVE\tRemovedPack\n'); run(*command); expected(output, 'last 漢字')
        checks.append({'route': route, 'case': 'inactive entry', 'accepted': True})
        picks.write_text(''); run(*command); expected(output, 'last 漢字')
        checks.append({'route': route, 'case': 'clear', 'accepted': True})
    if args.gui:
        saved = root/'forge_mods_picks.txt'
        for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
            case = work/size; case.mkdir()
            saved.write_text('text:English|SHARED\tRemovedPack With Long Dialogue And Translation Overrides\n')
            report = json.loads(run('mods', 'conflicts', root, '--json').stdout)
            index = next(i for i, row in enumerate(report['text']['contested']) if row['language'] == 'English')
            script = case/'review.txt'; widget = f'mod_unavailable_choice_{index}'
            lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
                     f'set uiscale {scale}', 'mods_tab 1', 'mods_conflicts', 'wait_mods', 'frames 3',
                     'assert_state mod_action_failed 0', 'assert_state mods_conflicts 2',
                     'assert_state mods_unavailable_choices 1', f'reveal {widget}', 'frames 3',
                     f'reveal {widget}', 'frames 3', f'dump_widget {widget}', 'clear_toasts',
                     'mouse_move viewport', f'screenshot {(case/"unavailable.png").as_posix()}',
                     f'reveal mod_winner_{index}', 'frames 3', f'reveal mod_winner_{index}', 'frames 3',
                     f'click mod_winner_{index}', 'frames 3', f'click mod_load_order_{index}', 'frames 3',
                     'assert_state mods_picks 0', 'assert_state mods_unavailable_choices 0',
                     'mods_conflicts', 'wait_mods', 'frames 3', 'assert_state mod_action_failed 0', 'quit']
            script.write_text('\n'.join(lines)+'\n')
            result = subprocess.run([str(args.gui.resolve()), '--install', str(args.install.resolve()),
                                     '--size', size, '--auto', str(script)], cwd=repo, capture_output=True,
                                    text=True, timeout=180,
                                    env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                                             APPDATA=str(case/'appdata'), LOCALAPPDATA=str(case/'localappdata')))
            (case/'output.log').write_text(result.stdout+result.stderr)
            log = Path(str(script)+'.log').read_text()
            assert result.returncode == 0 and 'RESULT PASS' in log, log[-5000:]
            rect = re.search(r'widget_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
            clip = re.search(r'widget_clip_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
            assert rect and clip
            left, top, right, bottom = map(float, rect.groups()); cl, ct, cr, cb = map(float, clip.groups())
            assert cl <= left < right <= cr and ct <= top < bottom <= cb, (rect.groups(), clip.groups())
            assert not saved.exists()
            output = case/'built'; run('mods', 'build', root, output); expected(output, 'last 漢字')
            checks.append({'size': size, 'actual_dropdown_clear': True, 'warning_unclipped': True})
            print(size, 'text warning, dropdown clear and exact rebuilt multilingual payloads: PASS', flush=True)
    assert before_inputs == {str(p): snapshot(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'inputs_unchanged': True,
                                               'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'text choice contributor checks')


if __name__ == '__main__': main()
