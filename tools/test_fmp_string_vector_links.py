#!/usr/bin/env python3
"""Verify real FMP sound-vector links resolve to names.bin strings after apply."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import zipfile
from pathlib import Path


PACKAGE = 'mods/Divine and Unholy powers.fmp'
RECORDS = ('SPECIAL_ABILITIES_DIVINE_WRATH_DEF_INSTANCE',
           'SPECIAL_ABILITIES_UNHOLY_POWER_DEF_INSTANCE')
FIELDS = ('StartPowerupSound', 'LoopingPowerupSound')


def run(tool, *args):
    process = subprocess.run([str(tool), *map(str, args)], capture_output=True, timeout=90)
    assert process.returncode == 0, process.stderr.decode(errors='replace')
    return process.stdout


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fmp-string-vector-', dir=repo/'build') as temporary:
        work = Path(temporary).resolve()
        assert work != (repo/'build').resolve() and work.is_relative_to((repo/'build').resolve())
        with zipfile.ZipFile(args.archive) as archive:
            row = next(r for r in json.loads(archive.read('catalogue.json')) if r['file'] == PACKAGE)
            payload = archive.read(PACKAGE)
            assert hashlib.sha256(payload).hexdigest() == row['sha256']
        package = work/'mod.fmp'; package.write_bytes(payload)
        links = json.loads(run(args.tool, 'fmp', 'links', package, '--json'))['records']
        run(args.tool, 'fmp', 'apply', args.install, package, work/'output')
        names = (work/'output/data/CompiledDefs/names.bin').read_bytes()
        checked = 0
        for record in RECORDS:
            declared = next(r['links'] for r in links if r['record'] == record)
            decoded = json.loads(run(args.tool, 'defs', 'decode', work/'output',
                                     repo/'docs/re_reference/def_schema.json', record, '--json'))
            for field in decoded['fields']:
                if field['name'] not in FIELDS:
                    continue
                assert field['type'] == 'Vector_VCDefString__'
                values = [int(n) for n in re.findall(r'\d+', field['value'])][1:]
                assert len(values) == 4, (record, field)
                # ChocolateBox Control.ID writes the field tag in reverse byte order.
                path_tag = str(int.from_bytes(bytes.fromhex(field['tag']), 'little'))
                selected = [link for link in declared if link['path'].split('.')[0] == path_tag]
                assert len(selected) == 4, (record, field['name'], selected)
                for link in selected:
                    index = int(link['path'].split('.')[-2])
                    offset = values[index]
                    actual = names[20 + offset:].split(b'\0', 1)[0].decode('ascii')
                    assert actual == link['target'], (record, field['name'], index, actual, link['target'])
                    checked += 1
        assert checked == 16, checked
        print('Verified', checked, 'real FMP sound-vector links against output names.bin')


if __name__ == '__main__':
    main()
