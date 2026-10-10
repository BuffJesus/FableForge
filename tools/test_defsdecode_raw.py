"""Independently reconstruct clean compiled records from diagnostic raw fields."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_mod_field_details import write_bank, RECORD
from test_mod_bank_refusal import snapshot
from test_fmp_links import read_bank


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='forge-defs-raw-') as directory:
        root = Path(directory)/'source'
        write_bank(root,(-1,15360,0,2147483647))
        before = snapshot(root)
        expected = read_bank(root)[0]['data']
        for selector in (RECORD,'0'):
            run = subprocess.run([str(args.tool.resolve()),'defs','decode',str(root),
                                  str(repo/'docs/re_reference/def_schema.json'),selector,'--json'],
                                 capture_output=True,text=True,encoding='utf-8',check=True)
            decoded = json.loads(run.stdout)
            assert decoded['all_tags_ok'] and not decoded['leftover']
            raw = bytes.fromhex(decoded['prefix_hex'])
            assert len(raw) == decoded['prefix_len']
            for field in decoded['fields']:
                raw += struct.pack('<I',int(field['tag'],16)) + bytes.fromhex(field['raw_hex'])
            assert raw == expected
        assert snapshot(root) == before
    print('PASS: definition JSON raw fields reconstruct exact bytes by name and slot; inputs preserved')


if __name__ == '__main__':
    main()
