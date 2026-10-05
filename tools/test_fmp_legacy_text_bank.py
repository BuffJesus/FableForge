#!/usr/bin/env python3
"""Check original lowercase text banks, alias parity and complete refusal preservation."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zipfile

from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def decoded(data):
    pos = 0
    while data[pos:pos+2] != b'\0\0':
        assert pos + 2 <= len(data)
        pos += 2
    content = data[:pos].decode('utf-16-le'); pos += 2
    fields = []
    for _ in range(3):
        size = struct.unpack_from('<I', data, pos)[0]; pos += 4
        assert pos + size <= len(data)
        fields.append(data[pos:pos+size].rstrip(b'\0')); pos += size
    count = struct.unpack_from('<I', data, pos)[0]; pos += 4
    tags = []
    for _ in range(count):
        at = struct.unpack_from('<i', data, pos)[0]; pos += 4
        end = data.index(0, pos)
        tags.append((at, data[pos:end])); pos = end + 1
    assert pos == len(data), 'unparsed trailing text data'
    return content, fields, tags


def snapshot(root):
    return {str(p.relative_to(root)): digest(p) for p in root.rglob('*') if p.is_file()}


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    text_rel = Path('data/lang/English/text.big')
    inputs = [args.archive, args.install/text_rel, args.install/'data/CompiledDefs/game.bin',
              args.install/'data/CompiledDefs/names.bin', args.tool]
    before = {str(p): digest(p) for p in inputs}
    work = Path(tempfile.mkdtemp(prefix='fmp-legacy-text-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    baseline = entries(args.install/text_rel, 'TEXT_ENGLISH_MAIN')
    checks = []; refused = 0
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for index, name in enumerate(('Briar Attempt', 'For Lazy Person')):
            member = f'mods/{name}.fmp'; payload = archive.read(member)
            assert hashlib.sha256(payload).hexdigest() == catalogue[member]['sha256']
            package = work/f'{index}.fmp'; package.write_bytes(payload)
            source = entries(package, 'text'); assert source
            assert all(e['type'] == 0 for e in source.values())
            upper = bytearray(payload)
            directory = struct.unpack_from('<I', upper, 8)[0]
            bank_count = struct.unpack_from('<I', upper, directory)[0]
            cursor = directory + 4
            text_row = None
            for _ in range(bank_count):
                end = upper.index(0, cursor)
                if upper[cursor:end] == b'text':
                    text_row = bytes(upper[cursor:end+21])
                    upper[cursor] = ord('T')
                cursor = end + 21
            assert text_row
            upper_package = work/f'{index}-uppercase.fmp'; upper_package.write_bytes(upper)
            outputs = []
            for spelling, path in [('lower', package), ('upper', upper_package)]:
                direct, merged = work/f'{index}-{spelling}-direct', work/f'{index}-{spelling}-merged'
                run(args.tool, 'fmp', 'apply', args.install, path, direct)
                run(args.tool, 'mods', 'merge', args.install, merged, '--with', path)
                for relative in (text_rel, Path('data/CompiledDefs/game.bin'), Path('data/CompiledDefs/names.bin')):
                    assert digest(direct/relative) == digest(merged/relative)
                for bank in ('script.bin', 'frontend.bin'):
                    assert digest(direct/'data/CompiledDefs'/bank) == digest(args.install/'data/CompiledDefs'/bank)
                built = entries(direct/text_rel, 'TEXT_ENGLISH_MAIN')
                assert set(built) == set(baseline) | set(source)
                for symbol, original in baseline.items():
                    assert built[symbol]['id'] == original['id']
                    assert built[symbol]['type'] == original['type']
                    assert built[symbol]['header'] == original['header']
                    if symbol in source:
                        assert decoded(built[symbol]['payload']) == decoded(source[symbol]['payload'])
                    else:
                        assert built[symbol]['payload'] == original['payload']
                for symbol, original in source.items():
                    assert built[symbol]['type'] == 0
                    assert decoded(built[symbol]['payload']) == decoded(original['payload'])
                    if symbol not in baseline:
                        assert built[symbol]['id'] not in {e['id'] for e in baseline.values()}
                assert len({e['id'] for e in built.values()}) == len(built)
                outputs.append((direct, merged))
            assert snapshot(outputs[0][0]) == snapshot(outputs[1][0])
            first = next(iter(source.values()))
            invalid_type = bytearray(payload)
            struct.pack_into('<I', invalid_type, first['record_offset']+8, 1)
            truncated = bytearray(payload)
            struct.pack_into('<I', truncated, first['record_offset']+12, 2)
            ambiguous = bytearray(payload)
            rows = bytes(payload[directory+4:])
            struct.pack_into('<I', ambiguous, 8, len(ambiguous))
            ambiguous += struct.pack('<I', bank_count+1) + rows + b'T' + text_row[1:]
            for label, bad_payload in [('type', invalid_type), ('truncated', truncated), ('ambiguous', ambiguous)]:
                bad = work/f'{index}-bad-{label}.fmp'; bad.write_bytes(bad_payload)
                for command, output in [('apply', outputs[0][0]), ('merge', outputs[0][1])]:
                    prior = snapshot(output)
                    if command == 'apply':
                        run(args.tool, 'fmp', 'apply', args.install, bad, output, good=False)
                    else:
                        run(args.tool, 'mods', 'merge', args.install, output, '--with', bad, good=False)
                    assert snapshot(output) == prior
                    refused += 1
            checks.append({'file': member, 'strings': len(source),
                           'added_strings': len(set(source)-set(baseline)),
                           'untouched_strings': len(set(baseline)-set(source))})
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'safe_refusals': refused,
        'alias_output_parity': True, 'inputs_unchanged': True, 'gameplay_tested': False}, indent=2))
    print('PASS: original lowercase text mods; alias parity, unchanged baseline entries and 12 safe refusals')


if __name__ == '__main__':
    main()
