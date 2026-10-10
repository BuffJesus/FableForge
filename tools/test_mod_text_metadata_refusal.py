#!/usr/bin/env python3
"""Selected text TOC deltas must not silently retain baseline metadata."""
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
from test_fmp_legacy_text_bank import decoded


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--complete-data', action='store_true')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-text-metadata-refusal-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4))
    text_bank(root, 'English', {'SHARED': 'original', 'OTHER': 'unchanged'})
    rel = Path('data/lang/English/text.big'); name = 'TEXT_ENGLISH_MAIN'
    original = entries(root/rel, name, None, True)
    for record in original.values(): record['header'] = struct.pack('<I', 0)
    original['[NarratorList]'] = {'id': 3, 'type': 2, 'header': struct.pack('<I', 2)+b'Speaker\0FARMER\0', 'payload': b''}
    bank(root/rel, name, original)
    valid = work/'Valid'; text_bank(valid, 'English', {'SHARED': 'alternate', 'OTHER': 'unchanged'})
    valid_records = entries(valid/rel, name, None, True)
    for record in valid_records.values(): record['header'] = struct.pack('<I', 0)
    valid_records['[NarratorList]'] = dict(original['[NarratorList]'])
    bank(valid/rel, name, valid_records)
    broken = work/'Broken'; bank(broken/rel, name, original)
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
            capture_output=True, text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        for binary in ('script.bin', 'frontend.bin'):
            (complete/'data/CompiledDefs'/binary).write_bytes((complete/'data/CompiledDefs/game.bin').read_bytes())
        run('mods', 'add', root, complete, '--install-data')
    for source in (valid, broken): run('mods', 'add', root, source)
    picks = work/'picks.txt'; picks.write_text(''); checks = []
    def command(route, output):
        cmd = ['mods', route, root]
        if route != 'conflicts': cmd += [output]
        if route == 'merge':
            cmd += ['--with', *([complete] if complete else []), valid, broken]
            if complete: cmd += ['--install-data']
        return cmd+['--picks', picks, '--json']
    cases = [('speaker index only', 'SHARED', dict(original['SHARED'], header=struct.pack('<I', 2)), 'unsupported TOC metadata changes'),
             ('speaker plus content', 'SHARED', dict(valid_records['SHARED'], header=struct.pack('<I', 2)), 'unsupported TOC metadata changes'),
             ('narrator list only', '[NarratorList]', dict(original['[NarratorList]'], header=struct.pack('<I', 2)+b'Speaker\0HERO\0'), 'unsupported type 2'),
             ('new string metadata', 'NEW', dict(valid_records['SHARED'], id=4, header=struct.pack('<I', 2)), 'unsupported TOC metadata changes')]
    for route in ('merge', 'build', 'conflicts'):
        output = work/route; bank(broken/rel, name, original); picks.write_text('')
        run(*command(route, output))
        if route != 'conflicts': (output/'sentinel').write_bytes(b'prior output')
        for label, symbol, record, reason in cases:
            changed = dict(original); changed[symbol] = record; bank(broken/rel, name, changed)
            before = snapshot(output) if output.exists() else {}
            inputs = {str(p): snapshot(p) for p in [root, valid, broken]+([complete] if complete else [])}
            failure = run(*command(route, output), good=False)
            assert 'mods text entry: English/'+symbol+' from Broken' in failure.stderr and reason in failure.stderr
            assert (snapshot(output) if output.exists() else {}) == before
            assert inputs == {str(p): snapshot(p) for p in [root, valid, broken]+([complete] if complete else [])}
            (work/(route+'-'+label.replace(' ', '-')+'.log')).write_text(failure.stderr)
            checks.append({'route': route, 'case': label, 'refused_preserved': True})
            picks.write_text('text:English|'+symbol+'\tvanilla\n')
            report = json.loads(run(*command(route, output)).stdout)
            if route != 'conflicts':
                actual = entries(output/rel, name, None, True)
                assert 'NEW' not in actual
                assert actual['[NarratorList]']['header'] == original['[NarratorList]']['header']
                assert actual['SHARED']['header'] == original['SHARED']['header']
                assert (output/'sentinel').read_bytes() == b'prior output'
            checks.append({'route': route, 'case': label, 'vanilla_bypasses_delta': True})
            if symbol == 'SHARED':
                picks.write_text('text:English|SHARED\tValid\n')
                report = json.loads(run(*command(route, output)).stdout)
                row = next(r for r in report['text']['contested'] if r['name'] == 'SHARED')
                assert row['winner'] == 'Valid' and row['overridden'] and row['mods'] == ['Valid', 'Broken']
                if route != 'conflicts':
                    actual = entries(output/rel, name, {'SHARED'}, True)['SHARED']
                    assert decoded(actual['payload'])[0] == 'alternate' and actual['header'] == original['SHARED']['header']
                checks.append({'route': route, 'case': label, 'valid_contributor_bypasses_delta': True})
            picks.write_text('')
        changed = dict(original); changed['NEW'] = dict(valid_records['SHARED'], id=4)
        bank(broken/rel, name, changed); run(*command(route, output))
        if route != 'conflicts':
            actual = entries(output/rel, name, None, True)
            assert actual['NEW']['header'] == original['SHARED']['header'] and actual['NEW']['id'] == 4
            assert decoded(actual['NEW']['payload'])[0] == 'alternate'
        checks.append({'route': route, 'matching_new_string_metadata': True})
        # A real speaker change has matching payload and narrator-list index.
        payload = bytearray('spoken'.encode('utf-16-le')+b'\0\0')
        for value in (b'SpeechBank', b'FARMER', b''):
            payload += struct.pack('<I', len(value))+value
        payload += struct.pack('<I', 0)
        changed['NEW'] = dict(changed['NEW'], header=struct.pack('<I', 1), payload=bytes(payload))
        bank(broken/rel, name, changed); run(*command(route, output))
        if route != 'conflicts':
            actual = entries(output/rel, name, None, True)
            assert actual['NEW']['header'] == struct.pack('<I', 1)
            assert decoded(actual['NEW']['payload'])[1][1] == b'FARMER'
            assert actual['NEW']['payload'] == bytes(payload)
            assert actual['[NarratorList]']['header'] == original['[NarratorList]']['header']
        checks.append({'route': route, 'validated_speaker_metadata_preserved': True})
        # Some real mod strings deliberately carry a different payload speaker;
        # preserving a valid source index must not rewrite either representation.
        changed['SHARED'] = dict(valid_records['SHARED'], header=struct.pack('<I', 1))
        bank(broken/rel, name, changed); run(*command(route, output))
        if route != 'conflicts':
            actual = entries(output/rel, name, None, True)['SHARED']
            assert actual['header'] == struct.pack('<I', 1)
            assert actual['payload'] == valid_records['SHARED']['payload']
        checks.append({'route': route, 'authored_speaker_difference_preserved': True})
    (work/'result.json').write_text(json.dumps({'ok': True, 'checks': checks, 'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'text metadata refusal/recovery checks')


if __name__ == '__main__': main()
