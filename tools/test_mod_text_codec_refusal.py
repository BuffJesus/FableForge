#!/usr/bin/env python3
"""Refuse selected text payloads the codec would silently normalize or truncate."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot
from test_mod_asset_choice_contributors import bank
from test_fmp_texture_layers import entries
from test_mod_text_choice_contributors import text_bank


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--complete-data', action='store_true')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-text-codec-refusal-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4))
    rel = Path('data/lang/English/text.big'); name = 'TEXT_ENGLISH_MAIN'
    text_bank(root, 'English', {'SHARED': 'original', 'OTHER': 'untouched'})
    original = entries(root/rel, name, None, True)
    # Baseline payloads are retained raw when the source does not change them.
    original['OTHER']['payload'] += b'\x99opaque original tail'
    bank(root/rel, name, original)
    valid = work/'Valid'; text_bank(valid, 'English', {'SHARED': 'alternate'})
    canonical = entries(valid/rel, name, None, True)['SHARED']
    valid_records = dict(original, SHARED=canonical)
    bank(valid/rel, name, valid_records)
    broken = work/'Broken'; bank(broken/rel, name, original)
    checks = []; counter = 0
    def run(*arguments, good=True):
        nonlocal counter
        counter += 1
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
            capture_output=True, text=True, timeout=120)
        (work/f'command-{counter:03d}.json').write_text(json.dumps({
            'arguments': list(map(str, arguments)), 'returncode': result.returncode,
            'stdout': result.stdout, 'stderr': result.stderr}, indent=2))
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        for binary in ('script.bin', 'frontend.bin'):
            (complete/'data/CompiledDefs'/binary).write_bytes((complete/'data/CompiledDefs/game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    for source in (valid, broken): run('mods', 'add', root, source)
    picks = work/'picks.txt'; picks.write_text('')
    def command(route, output):
        cmd = ['mods', route, root]
        if route != 'conflicts': cmd += [output]
        if route == 'merge':
            cmd += ['--with', *([complete] if complete else []), valid, broken]
            if complete: cmd += ['--install-data']
        return cmd+['--picks', picks, '--json']
    payload = canonical['payload']
    # Independently alter the first counted ASCII field to include its NUL.
    offset = len('alternate'.encode('utf-16le'))+2
    length = struct.unpack_from('<I', payload, offset)[0]
    end = offset+4+length
    counted_nul = payload[:offset]+struct.pack('<I', length+1)+payload[offset+4:end]+b'\0'+payload[end:]
    assert payload.endswith(b'emphasis\0')
    variants = [('opaque tail', payload+b'\x99\x88opaque-tail'),
                ('unterminated tag', payload[:-1]), ('counted field NUL', counted_nul)]
    for route in ('merge', 'build', 'conflicts'):
        output = work/route; bank(broken/rel, name, original); picks.write_text('')
        run(*command(route, output))
        if route != 'conflicts': (output/'sentinel').write_bytes(b'prior output')
        for label, raw in variants:
            for symbol in ('SHARED', 'NEW'):
                changed = dict(original); changed[symbol] = dict(canonical, id=3 if symbol == 'NEW' else 1, payload=raw)
                bank(broken/rel, name, changed)
                before = snapshot(output) if output.exists() else {}
                sources = [root, valid, broken]+([complete] if complete else [])
                inputs = {str(p): snapshot(p) for p in sources}
                failure = run(*command(route, output), good=False)
                assert f'mods text entry: English/{symbol} from Broken' in failure.stderr
                reason = ('textbig: unterminated tag name' if label == 'unterminated tag'
                          else 'selected payload cannot be preserved by the string codec')
                assert reason in failure.stderr, failure.stderr
                assert before == (snapshot(output) if output.exists() else {})
                assert inputs == {str(p): snapshot(p) for p in sources}
                checks.append({'route': route, 'case': label, 'symbol': symbol, 'refused_preserved': True})
                for pick in ('vanilla', *(['Valid'] if symbol == 'SHARED' else [])):
                    picks.write_text(f'text:English|{symbol}\t{pick}\n')
                    report = json.loads(run(*command(route, output)).stdout)
                    if symbol == 'SHARED':
                        row = next(r for r in report['text']['contested'] if r['name'] == symbol)
                        assert row['winner'] == pick and row['mods'] == ['Valid', 'Broken'] and row['overridden']
                    if route != 'conflicts':
                        actual = entries(output/rel, name, None, True)
                        assert actual['OTHER']['payload'] == original['OTHER']['payload']
                        assert actual['SHARED']['payload'] == (original['SHARED']['payload'] if symbol == 'SHARED' and pick == 'vanilla' else canonical['payload'])
                        assert 'NEW' not in actual and (output/'sentinel').read_bytes() == b'prior output'
                    checks.append({'route': route, 'case': label, 'symbol': symbol, 'pick': pick, 'recovered': True})
                picks.write_text('')
        bank(broken/rel, name, original); run(*command(route, output))
        if route != 'conflicts':
            actual = entries(output/rel, name, None, True)
            assert actual['SHARED']['payload'] == canonical['payload']
            assert actual['OTHER']['payload'] == original['OTHER']['payload']
        checks.append({'route': route, 'canonical_change_and_untouched_opaque_original': True})
    (work/'result.json').write_text(json.dumps({'ok': True, 'checks': checks, 'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'text codec refusal/recovery checks')


if __name__ == '__main__': main()
