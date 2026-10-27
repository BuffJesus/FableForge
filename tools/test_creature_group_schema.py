#!/usr/bin/env python3
"""Check the narrowly recovered Creature GroupDef against read-only retail records."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_fmp_links import read_bank
from test_mod_field_details import tag


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--schema', type=Path, default=repo/'docs/re_reference/def_schema.json')
    parser.add_argument('--cwd', type=Path, default=repo)
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    native = args.install.resolve()
    inputs = sorted((native/'data/CompiledDefs').glob('*.bin'))
    hashes = {str(path): digest(path) for path in inputs}
    records = read_bank(native)
    schema_path = args.schema.resolve()
    schema = json.loads(schema_path.read_text(encoding='utf-8'))
    fields = schema['CREATURE']['fields']
    group = [field for field in fields if field['name'] == 'GroupDef']
    assert len(group) == 1 and group[0]['type'] == 'int32'
    # Do not silently promote adjacent reconstructed unknown fields.
    for name in ('GiveHeroStatChangesOnBeingHit', 'RenderFadeDistance'):
        assert next(field for field in fields if field['name'] == name)['type'] == '?'
    work = Path(tempfile.mkdtemp(prefix='creature-group-schema-', dir=args.work_root.resolve()))
    print('Evidence retained at', work, flush=True)
    rows = []
    try:
        for index, record in enumerate(records):
            if record['definition'] != 'CREATURE':
                continue
            data = record['data']
            key = struct.pack('<I', tag('GroupDef'))
            if key not in data:
                assert record['name'].startswith('NULLDEF'), record['name']
                rows.append({'index': index, 'name': record['name'], 'null_record': True})
                continue
            assert data.count(key) == 1, record['name']
            start = data.index(key) + 4
            assert data[start+4:start+8] == struct.pack('<I', tag('RenderFadeDistance')), record['name']
            value = struct.unpack_from('<i', data, start)[0]
            result = subprocess.run([str(args.tool.resolve()), 'defs', 'decode', str(native),
                str(schema_path), str(index), 'game.bin', '--json'],
                cwd=args.cwd.resolve(), capture_output=True, text=True, encoding='utf-8', timeout=120)
            assert result.returncode == 0, result.stdout + result.stderr
            decoded = json.loads(result.stdout)
            matches = [field for field in decoded['fields'] if field['name'] == 'GroupDef']
            assert len(matches) == 1 and matches[0]['type'] == 'int32'
            assert matches[0]['raw_hex'] == data[start:start+4].hex()
            assert int(matches[0]['value']) == value
            target = records[value]['definition'] if 0 <= value < len(records) else None
            assert target == 'THING_GROUP' or value == 0, (record['name'], value, target)
            rows.append({'index': index, 'name': record['name'], 'offset': start,
                         'value': value, 'target_type': target})
    finally:
        assert {str(path): digest(path) for path in inputs} == hashes, 'native inputs changed'
    assert rows and any(row.get('target_type') == 'THING_GROUP' for row in rows)
    (work/'result.json').write_text(json.dumps({'passed': True, 'records': rows,
        'native_hashes_unchanged': hashes}, indent=2)+'\n', encoding='utf-8')
    print('Creature GroupDef schema and raw four-byte boundaries: PASS', len(rows), 'records', flush=True)


if __name__ == '__main__':
    main()
