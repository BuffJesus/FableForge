#!/usr/bin/env python3
"""Read-only Creature CDefs ownership audit; optional compiled resolver comparison.

The independent model checks serialized triples, types and original parents.
The C++ resolver additionally requires clean reflected decoding. Invoke --reader
only inside an already guarded runtime lease, as for other named test tools.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from test_fmp_links import crc0, read_bank


def resolve(entries, index):
    def table(slot):
        if not 0 <= slot < len(entries) or entries[slot]['definition'] != 'CREATURE':
            raise ValueError('root/owner is not a Creature')
        data = entries[slot]['data']
        if len(data) < 5:
            raise ValueError('truncated root')
        count, = struct.unpack_from('<H', data, 3)
        end = 5 + 12 * count
        if not count or len(data) < end + 4:
            raise ValueError('truncated/empty CDefs')
        if struct.unpack_from('<I', data, end)[0] != crc0('Components'):
            raise ValueError('CDefs is not followed by Components')
        return list(struct.iter_unpack('<III', data[5:end]))

    rows = table(index)
    result = dict(root=index, count=len(rows))
    for kind, label in [('CCreatureDef', 'body'), ('CAppearanceDef', 'appearance')]:
        matches = [(n, row) for n, row in enumerate(rows) if row[0] == crc0(kind)]
        if len(matches) != 1:
            raise ValueError('missing/duplicate visual row')
        row, (_, target, owner) = matches[0]
        if target >= len(entries) or entries[target]['definition'] != kind:
            raise ValueError('visual target has wrong type/index')
        ownership = [r for r in table(owner) if r[0] == crc0(kind)]
        if len(ownership) != 1 or ownership[0][1] != target:
            raise ValueError('original parent does not identify the same visual')
        result[label], result[label + '_row'] = target, row
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reader', type=Path)
    parser.add_argument('--schema', type=Path)
    args = parser.parse_args()
    if args.reader and not args.schema:
        parser.error('--reader requires --schema')
    inputs = [args.install/'data/CompiledDefs'/name for name in ['game.bin', 'names.bin']]
    inputs += [p for p in [args.reader, args.schema] if p]
    if args.output.resolve() in [p.resolve() for p in inputs]:
        raise ValueError('output must differ from every input')
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    hashes = {str(p.resolve()): digest(p) for p in inputs}
    entries, records = read_bank(args.install), []
    for index, entry in enumerate(entries):
        if entry['definition'] != 'CREATURE':
            continue
        try:
            records.append(resolve(entries, index))
        except ValueError:
            records.append(dict(root=index, refused=True))
    if args.reader:
        child = subprocess.run([str(args.reader.resolve()), '--visual-graph-audit',
            str(args.install.resolve()), str(args.schema.resolve())],
            capture_output=True, text=True, check=True, timeout=60)
        actual = json.loads(child.stdout)['records']
        actual = [dict(root=r['root'], refused=True) if 'refused' in r else r for r in actual]
        if actual != records:
            raise AssertionError('compiled and independent visual graphs differ')
    if hashes != {str(p.resolve()): digest(p) for p in inputs}:
        raise AssertionError('an audit input changed')
    report = dict(records=records, input_hashes=hashes, roots=len(records),
        accepted=sum('refused' not in r for r in records),
        refused=sum('refused' in r for r in records), compiled_comparison=bool(args.reader),
        inputs_unchanged=True, scope='CDefs graph ownership only; not runtime selection/rendering')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f"Visual graphs: {report['accepted']} accepted, {report['refused']} refused; {args.output}")


if __name__ == '__main__':
    main()
