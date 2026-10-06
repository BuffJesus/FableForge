"""Read-only namespace allocation audit using independently decoded input banks.

Checks identity preservation/injectivity, complete slot coverage, name offsets,
and input hashes. BIN reference inventories remain evidence, not permission to
write merged banks: external references are outside this tool's scope.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from test_fmp_links import read_bank


def names(root):
    data = (root / 'data/CompiledDefs/names.bin').read_bytes()
    count, = struct.unpack_from('<I', data, 8)
    result = {}
    at = 20
    for _ in range(count):
        crc, = struct.unpack_from('<I', data, at)
        end = data.index(0, at + 4)
        result[at + 4 - 20] = (crc, data[at + 4:end].decode('ascii'))
        at = end + 1
    assert at == len(data), 'unparsed names bytes'
    return result


def hashes(roots):
    return {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
            for root in roots for name in ('game.bin', 'script.bin', 'frontend.bin', 'names.bin')
            for path in [root / 'data/CompiledDefs' / name]}


def audit(tool, schema, baseline, sources):
    before = hashes([baseline, *sources])
    run = subprocess.run([str(tool), str(baseline), str(schema), *map(str, sources)],
                         capture_output=True, text=True, encoding='utf-8')
    if run.returncode:
        raise RuntimeError(f'namespace audit tool failed ({run.returncode}): {run.stderr.strip()}')
    report = json.loads(run.stdout)
    base = read_bank(baseline, 'game.bin')
    base_names = names(baseline)
    identity = lambda entry: (entry['definition'], entry['name'])
    targets = {i: identity(entry) for i, entry in enumerate(base)}
    target_names = dict(base_names)
    decoded = [read_bank(root, 'game.bin') for root in sources]
    counts = [Counter(entry['name'] for entry in bank if entry['name']) for bank in decoded]
    ambiguous = {name for count in counts for name, n in count.items() if n > 1}
    baseline_named = {entry['name'] for entry in base if entry['name']}
    owners = {}
    prepared_sources = []
    for si, (root, bank, source) in enumerate(zip(sources, decoded, report['sources'], strict=True)):
        mapping = dict(source['game_map'])
        assert len(mapping) == len(source['game_map'])
        assert all(len(base) <= old < len(bank) and old != new for old, new in mapping.items())
        assigned = set()
        for old, entry in enumerate(bank):
            new = mapping.get(old, old)
            assert 0 <= new < report['entries'] and new not in assigned, (root, old, new)
            assigned.add(new)
            key = identity(entry)
            if old < len(base):
                assert new == old and key == identity(base[old])
            else:
                assert new >= len(base)
                if new in owners:
                    assert key[1] and key[1] not in ambiguous and key[1] not in baseline_named
                    assert owners[new] != si
                owners[new] = si
            assert targets.setdefault(new, key) == key, (root, old, new, key)
        pool = names(root)
        name_map = dict(source['name_map'])
        assert len(name_map) == len(source['name_map'])
        assert all(old in pool and old != new for old, new in name_map.items())
        for old, name in pool.items():
            new = name_map.get(old, old)
            if old in base_names:
                assert new == old and name == base_names[old]
            assert target_names.setdefault(new, name) == name, (root, old, new, name)
        banks = {'game.bin': bank}
        expected_game = [entry['data'] for entry in bank]
        for row in source['records']:
            if 'after_hex' not in row:
                assert row['unresolved'], 'missing successful relocated payload'
                continue
            if row['bank'] not in banks:
                banks[row['bank']] = read_bank(root, row['bank'])
            before_bytes = banks[row['bank']][row['slot']]['data']
            expected = bytearray(before_bytes)
            occupied = set()
            for rewrite in row['rewrites']:
                offset = rewrite['offset']
                assert not occupied.intersection(range(offset, offset + 4)), 'overlapping rewrites'
                occupied.update(range(offset, offset + 4))
                assert struct.unpack_from('<I', before_bytes, offset)[0] == rewrite['before']
                domain = mapping if rewrite['domain'] == 'game' else name_map
                assert domain[rewrite['before']] == rewrite['after']
                struct.pack_into('<I', expected, offset, rewrite['after'])
            assert bytes.fromhex(row['after_hex']) == expected, 'unintended relocated payload delta'
            if row['bank'] == 'game.bin':
                expected_game[row['slot']] = bytes(expected)
        if 'prepared_game' in source:
            assert len(source['prepared_game']) == len(bank), 'fabricated/missing contributors'
            for old, (entry, prepared, expected) in enumerate(zip(
                    bank, source['prepared_game'], expected_game, strict=True)):
                assert prepared['target'] == mapping.get(old, old), 'wrong prepared owner slot'
                assert (prepared['definition'], prepared['name']) == identity(entry)
                assert prepared['payload_sha256'] == hashlib.sha256(expected).hexdigest(), \
                    'prepared payload differs from independent exact deltas'
            prepared_sources.append(source['prepared_game'])
    assert set(targets) == set(range(report['entries'])), 'missing/unowned output slots'
    ordinals = {}
    counts_by_definition = Counter()
    for target in range(report['entries']):
        definition = targets[target][0]
        ordinals[target] = counts_by_definition[definition]
        counts_by_definition[definition] += 1
    for prepared in prepared_sources:
        for entry in prepared:
            assert entry['ordinal'] == ordinals[entry['target']], 'wrong prepared ordinal'
    if not report.get('preparation_error'):
        assert len(prepared_sources) == len(sources), 'missing prepared sources'
    at = 4
    for offset, (_, name) in sorted(target_names.items()):
        assert offset == at, 'name overlap or gap'
        at += len(name.encode('ascii')) + 5
    assert len(target_names) == report['names']
    assert hashes([baseline, *sources]) == before, 'audit changed input banks'
    report['input_sha256'] = before
    report['tool_sha256'] = hashlib.sha256(tool.read_bytes()).hexdigest()
    report['schema_sha256'] = hashlib.sha256(schema.read_bytes()).hexdigest()
    report['independent_checks'] = ['all source record identities preserved',
        'baseline slots and name offsets unchanged', 'source record mappings injective',
        'anonymous/ambiguous source additions never coalesced', 'all output slots owned',
        'all name references retain exact CRC/string', 'name pool has no overlaps or gaps',
        'successful relocated payloads match exact independent word deltas',
        'all input bank hashes unchanged']
    if len(prepared_sources) == len(sources):
        report['independent_checks'].append(
            'all prepared game contributors preserve ownership, canonical ordinals and exact payload deltas')
    return report


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo / 'build/fableforge_defnamespace_tests.exe')
    parser.add_argument('--schema', type=Path, default=repo / 'docs/re_reference/def_schema.json')
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--source', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    protected = [args.baseline.resolve(), *[p.resolve() for p in args.source]]
    output = args.out.resolve()
    if any(output.is_relative_to(root) for root in protected) or output in {
            args.tool.resolve(), args.schema.resolve()}:
        parser.error('audit report must be outside all input roots and must not replace tool/schema')
    report = audit(args.tool.resolve(), args.schema.resolve(), protected[0], protected[1:])
    args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"PASS: {report['entries']} slots, {report['names']} names; identity audit only")
    for source in report['sources']:
        print(f"{source['root']}: {source['rewrites']} verified rewrites, "
              f"{source['unresolved']} unresolved candidates")


if __name__ == '__main__':
    main()
