#!/usr/bin/env python3
"""Check a real FMP's CThingComponentSet links and opaque-byte preservation."""
import argparse
import hashlib
import json
import subprocess
import tempfile
import zipfile
from pathlib import Path

from test_fmp_links import package_records, read_bank


PACKAGE = 'mods/No Quiver.fmp'
RECORD = 'OBJECT_QUIVER'


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    build = (repo/'build').resolve()
    with tempfile.TemporaryDirectory(prefix='fmp-components-', dir=build) as temporary:
        work = Path(temporary).resolve()
        assert work != build and work.is_relative_to(build)
        with zipfile.ZipFile(args.archive) as archive:
            row = next(r for r in json.loads(archive.read('catalogue.json')) if r['file'] == PACKAGE)
            payload = archive.read(PACKAGE)
            assert hashlib.sha256(payload).hexdigest() == row['sha256']
        package = work/'mod.fmp'; package.write_bytes(payload)
        links_call = subprocess.run([str(args.tool), 'fmp', 'links', str(package), '--json'],
                                    capture_output=True, timeout=90)
        assert links_call.returncode == 0, links_call.stderr.decode(errors='replace')
        declared = next(r['links'] for r in json.loads(links_call.stdout)['records']
                        if r['record'] == RECORD)
        component_links = [link for link in declared if '.CTC List.' in link['path']]
        assert len(component_links) == 5, component_links
        output = work/'output'
        result = subprocess.run([str(args.tool), 'fmp', 'apply', str(args.install),
                                 str(package), str(output)], capture_output=True, timeout=90)
        assert result.returncode == 0, result.stderr.decode(errors='replace')

        source_record = package_records(package)['GameBINEntries'][RECORD][0]
        built_record = next(e['data'] for e in read_bank(output) if e['name'] == RECORD)
        tag = int(component_links[0]['path'].split('.')[0]).to_bytes(4, 'big')
        def component_bytes(record):
            assert record.count(tag) == 1
            at = record.index(tag) + 4
            count = int.from_bytes(record[at:at+4], 'little')
            assert count == len(component_links)
            # This OBJECT layout has a 104-byte opaque tail after 9-byte records.
            return record[at:at+4 + 9*count + 104]
        before = bytearray(component_bytes(source_record))
        after = bytearray(component_bytes(built_record))
        names = (output/'data/CompiledDefs/names.bin').read_bytes()
        for link in component_links:
            index = int(link['path'].split('.')[-2])
            at = 4 + index * 9
            offset = int.from_bytes(after[at:at+4], 'little')
            assert names[20+offset:].split(b'\0', 1)[0].decode('ascii') == link['target']
            before[at:at+4] = after[at:at+4]
        assert before == after, 'component parameters, flags or opaque tail changed'
        print('Verified five component links and preserved all other component bytes')


if __name__ == '__main__':
    main()
