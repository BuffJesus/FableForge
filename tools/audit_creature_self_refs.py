#!/usr/bin/env python3
"""Read-only donor self-reference audit; unresolved donors are findings, not accepted clones."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import sys
from test_fmp_links import read_bank
from test_mod_field_details import tag


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--exe',type=Path,default=repo/'build/fableforge_defrelocate_tests.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--clone-api',action='store_true',help='Also exercise the Creature clone API against this verified audit')
    args=parser.parse_args();native=args.install.resolve()
    inputs=sorted((native/'data/CompiledDefs').glob('*.bin'))
    hashes={str(path):digest(path) for path in inputs}
    records=read_bank(native)
    work=Path(tempfile.mkdtemp(prefix='creature-self-refs-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    result=subprocess.run([str(args.exe.resolve()),'--creature-self-audit',str(native),
        str(repo/'docs/re_reference/def_schema.json')],capture_output=True,text=True,encoding='utf-8',timeout=120)
    (work/'audit.log').write_text(result.stderr,encoding='utf-8')
    assert result.returncode==0,result.stderr
    report=json.loads(result.stdout);assert report['entries']==len(records)
    rows=report['records'];assert len(rows)==sum(r['definition']=='CREATURE' for r in records)
    accepted=blocked=total_rewrites=0
    for row in rows:
        owner=row['index'];record=records[owner];data=record['data']
        count=struct.unpack_from('<H',data,3)[0]
        prefix=5+count*12
        assert prefix==row['prefix'] and prefix<=len(data),record['name']
        assert data[prefix:prefix+4]==struct.pack('<I',tag('Components')),record['name']
        expected=[]
        for index in range(count):
            for member in (4,8):
                at=5+index*12+member
                if struct.unpack_from('<I',data,at)[0]==owner:expected.append(at)
        assert sorted(r['offset'] for r in row['rewrites'])==sorted(expected),record['name']
        changed=bytearray(data)
        for at in expected:struct.pack_into('<I',changed,at,len(records))
        if row['unresolved']:
            assert 'after_hex' not in row;blocked+=1
        else:
            assert bytes.fromhex(row['after_hex'])==bytes(changed),record['name']
            accepted+=1
        total_rewrites+=len(expected)
    assert {str(path):digest(path) for path in inputs}==hashes,'native inputs changed'
    report.update(accepted=accepted,blocked=blocked,verified_self_slots=total_rewrites,
        native_hashes_unchanged=hashes,independent_delta_checks_passed=True)
    (work/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if args.clone_api:
        subprocess.run([sys.executable,str(repo/'tools/test_creature_clone_api.py'),'--install',str(native),
            '--audit-report',str(work/'result.json'),'--work-root',str(args.work_root.resolve())],check=True)
    print('Independent CDefs/self-only deltas: PASS',len(rows),'donors;',accepted,'eligible,',blocked,
          'unresolved,',total_rewrites,'verified slots',flush=True)


if __name__=='__main__':main()
