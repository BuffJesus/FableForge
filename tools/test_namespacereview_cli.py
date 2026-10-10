"""Read-only namespace CLI: mappings, lookup hazards, refusals and preservation."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

from test_fmp_links import crc0


def write_bank(root, entry_names, definition='OBJECT'):
    folder = root / 'data/CompiledDefs'
    folder.mkdir(parents=True)
    names, offsets = bytearray(), {}
    for name in [definition, '', *entry_names]:
        if name not in offsets:
            offsets[name] = len(names) + 4
            names.extend(struct.pack('<I', crc0(name)) + name.encode('ascii') + b'\0')
    (folder / 'names.bin').write_bytes(struct.pack('<5I', 0, 0xA8E36C34, len(offsets), len(names) + 4, 0) + names)
    table, chunks, chunk_table = bytearray(), bytearray(), bytearray()
    for slot, name in enumerate(entry_names):
        table.extend(struct.pack('<III', offsets[definition], offsets[name], slot))
        chunk_table.extend(struct.pack('<II', slot, len(chunks)))
        chunks.extend(zlib.compress(struct.pack('<H', 2) + b'opaque payload ' + bytes([slot])))
    chunk_table.extend(struct.pack('<II', len(entry_names), len(chunks)))
    (folder / 'game.bin').write_bytes(bytes(9) + struct.pack('<I', len(entry_names)) + table +
        struct.pack('<I', len(entry_names) + 1) + chunk_table + struct.pack('<I', len(chunks)) + chunks)


def main():
    tool = Path(__file__).resolve().parents[1] / 'build/forge-tools.exe'
    checks = 0
    with tempfile.TemporaryDirectory(prefix='forge-namespace-review-') as directory:
        root = Path(directory)
        base, first, second = [root / name for name in ('baseline', 'first-\u00e9', 'second')]
        write_bank(base, [''])
        write_bank(first, ['', 'A'])
        write_bank(second, ['', 'B'])
        collision = root / 'zero-crc'
        write_bank(collision, ['', 'LOOKUP_ZERO_SY2CT3I'])
        aliases = root / 'duplicates'
        write_bank(aliases, ['', 'A', 'A'])
        invalid = root / 'bad-crc'
        write_bank(invalid, ['', 'A'])
        path = invalid / 'data/CompiledDefs/names.bin'
        raw = bytearray(path.read_bytes())
        raw[-6] ^= 1  # CRC of the appended A; baseline name prefix stays intact.
        path.write_bytes(raw)
        malformed = []
        truncated = root / 'unterminated'
        write_bank(truncated, ['', 'A'])
        path = truncated / 'data/CompiledDefs/names.bin'
        path.write_bytes(path.read_bytes()[:-1])
        malformed.append((truncated, 'unterminated name'))
        for label, extra in [('trailing-byte', b'x'), ('hidden-name', struct.pack('<I', crc0('HIDDEN')) + b'HIDDEN\0')]:
            bad = root / label
            write_bank(bad, [''])
            path = bad / 'data/CompiledDefs/names.bin'
            path.write_bytes(path.read_bytes() + extra)
            malformed.append((bad, 'trailing bytes after declared names'))
        for label, row_field, reference in [('zero-ref', 17, 0), ('interior-ref', 17, 5),
                                             ('large-ref', 17, 0x7fffffff), ('class-ref', 13, 0),
                                             ('negative-ref', 17, -2), ('minimum-ref', 17, -0x80000000),
                                             ('negative-class-ref', 13, -2)]:
            bad = root / label
            write_bank(bad, [''])
            path = bad / 'data/CompiledDefs/game.bin'
            raw = bytearray(path.read_bytes())
            struct.pack_into('<i', raw, row_field, reference)
            path.write_bytes(raw)
            malformed.append((bad, 'invalid names.bin reference'))
        sentinel = root / 'negative-sentinel'
        write_bank(sentinel, [''])
        path = sentinel / 'data/CompiledDefs/game.bin'
        raw = bytearray(path.read_bytes())
        struct.pack_into('<i', raw, 17, -1)
        path.write_bytes(raw)
        startup_base, startup_first, startup_second, startup_short = [root / name for name in
            ('startup-base', 'startup-first', 'startup-second', 'startup-short')]
        write_bank(startup_base, ['ROOT'], 'ENVIRONMENT')
        write_bank(startup_first, ['ROOT', 'FIRST'], 'ENVIRONMENT')
        write_bank(startup_second, ['ROOT', 'SECOND'], 'ENVIRONMENT')
        write_bank(startup_short, ['ROOT'], 'ENVIRONMENT')
        same_base, same_source = root / 'same-base', root / 'same-source'
        write_bank(same_base, [''], 'HERO_STATS')
        write_bank(same_source, ['', ''], 'HERO_STATS')
        original = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*.bin')}

        def run(arguments, expected):
            nonlocal checks
            result = subprocess.run([str(tool), 'defs', 'review-namespace', *map(str, arguments)],
                                    capture_output=True, text=True, encoding='utf-8', timeout=30)
            assert result.returncode == expected, (arguments, result.returncode, result.stdout, result.stderr)
            checks += 1
            return result

        result = json.loads(run([base, first, second, '--json'], 0).stdout)
        assert result['analysis_complete'] and not result['source_admitted'] and not result['relocation_checked']
        assert result['planned_entries'] == 3 and not result['ambiguous_named_groups']
        assert result['sources'][1]['game_id_changes'] == [[1, 2]]
        assert result['sources'][0]['added_name_crc_groups'] == 1
        assert result['changed_source_candidate_groups'] == 0
        assert result['changed_startup_selections'] == 0 and result['missing_startup_selections'] == 8
        for order in [(startup_first, startup_second), (startup_second, startup_first)]:
            startup = json.loads(run([startup_base, *order, '--json'], 1).stdout)
            assert startup['changed_source_candidate_groups'] == 0
            assert startup['changed_startup_selections'] == 1
            rows = [s['startup_class_selections'][2] for s in startup['sources']]
            assert [r['status'] for r in rows] == ['unchanged', 'changed']
            assert rows[1]['source_in_planned_slots']['slot'] == 2 and rows[1]['planned']['slot'] == 1
        assert 'CHANGED startup ENVIRONMENT ordinal 1' in run([startup_base, startup_first, startup_second], 1).stdout
        startup = json.loads(run([startup_base, startup_first, startup_short, '--json'], 0).stdout)
        assert startup['sources'][1]['startup_class_selections'][2]['status'] == 'missing_source'
        assert startup['missing_startup_selections'] == 7
        startup = json.loads(run([same_base, same_source, same_source, '--json'], 1).stdout)
        assert startup['changed_source_candidate_groups'] == 0 and not startup['ambiguous_named_groups']
        row = startup['sources'][1]['startup_class_selections'][3]
        assert row['status'] == 'changed'
        assert row['source_in_planned_slots']['name'] == row['planned']['name'] == ''
        assert 'Namespace plan: 3 slots'  in run([base, first, second], 0).stdout
        result = json.loads(run([base, collision, '--json'], 1).stdout)
        assert result['analysis_complete'] and not result['source_admitted']
        group = result['ambiguous_named_groups'][0]
        assert group['crc'] == 0 and [r['slot'] for r in group['candidates']] == [0, 1]
        assert group['candidates'][0]['name'] == ''
        result = json.loads(run([base, first, aliases, '--json'], 1).stdout)
        assert result['analysis_complete'] and result['changed_source_candidate_groups'] == 2
        assert len(result['ambiguous_named_groups'][0]['candidates']) == 3
        for source in [invalid, root / 'missing']:
            result = json.loads(run([base, source, '--json'], 1).stdout)
            assert not result['analysis_complete'] and not result['source_admitted'] and result['reason']
        for source, reason in malformed:
            result = json.loads(run([base, source, '--json'], 1).stdout)
            assert not result['analysis_complete'] and reason in result['reason']
        assert json.loads(run([base, sentinel, '--json'], 0).stdout)['analysis_complete']
        for args in [[], [base], [base, '--json'], [base, first, '--unknown'],
                     [base, first, '--json', 'extra']]:
            assert 'usage:' in run(args, 2).stderr
        assert original == {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*.bin')}
    print(f'Namespace review CLI: {checks} checks passed; input files unchanged.')


if __name__ == '__main__':
    main()
