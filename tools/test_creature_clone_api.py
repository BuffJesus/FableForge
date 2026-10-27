#!/usr/bin/env python3
"""Independent retail append/delta gate for the memory-only Creature clone API."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_fmp_links import read_bank, crc0


def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--exe',type=Path,default=repo/'build/fableforge_creatureclone_tests.exe')
    parser.add_argument('--audit-report',type=Path,required=True)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args();native=args.install.resolve();folder=native/'data/CompiledDefs'
    hashes={str(path):digest(path) for path in sorted(folder.glob('*.bin'))}
    audit=json.loads(args.audit_report.read_text(encoding='utf-8'))
    assert audit['native_hashes_unchanged']==hashes and audit['independent_delta_checks_passed']
    records=read_bank(native)
    work=Path(tempfile.mkdtemp(prefix='creature-clone-api-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True);output=work/'output'
    try:
        result=subprocess.run([str(args.exe.resolve()),str(native),str(repo/'docs/re_reference/def_schema.json'),str(output)],
            capture_output=True,text=True,encoding='utf-8',timeout=120)
        (work/'output.log').write_text(result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stderr
        report=json.loads(result.stdout);after=read_bank(output)
        assert after[:len(records)]==records and len(after)==report['entries']
        original=(folder/'game.bin').read_bytes();bank=(output/'data/CompiledDefs/game.bin').read_bytes()
        assert bank[:9]==original[:9] and bank[13:13+12*len(records)]==original[13:13+12*len(records)]
        old_names=(folder/'names.bin').read_bytes();new_names=(output/'data/CompiledDefs/names.bin').read_bytes()
        assert new_names[20:len(old_names)]==old_names[20:]
        audits={row['index']:row for row in audit['records']};accepted=refused=0
        for row in report['records']:
            donor=row['donor'];source=records[donor];layout=audits[donor];data=source['data']
            count=struct.unpack_from('<H',data,3)[0]
            blocked=bool(layout['unresolved']) or not count
            if 'refused' in row:
                assert blocked,(source['name'],row['refused']);refused+=1;continue
            assert not blocked,source['name'];appended=row['appended']
            assert appended==len(records)+accepted
            expected=bytearray(data)
            for index in range(count):
                for member in (4,8):
                    at=5+index*12+member
                    if struct.unpack_from('<I',data,at)[0]==donor:struct.pack_into('<I',expected,at,appended)
            clone=after[appended]
            assert clone['definition']=='CREATURE' and clone['name']=='CREATURE_API_PROBE_'+str(donor)
            name_at=20+struct.unpack_from('<I',bank,13+12*appended+4)[0]-4
            assert struct.unpack_from('<I',new_names,name_at)[0]==crc0(clone['name'])
            assert clone['data']==bytes(expected)
            assert struct.unpack_from('<I',bank,13+12*appended+8)[0]==sum(r['definition']=='CREATURE' for r in records)+accepted
            accepted+=1
        assert accepted and accepted+refused==len(audits)
        report.update(passed=True,accepted=accepted,refused=refused,native_hashes_unchanged=hashes)
        (work/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print('All prior records/identities/pool bytes and self-only appended deltas: PASS',accepted,'clones,',refused,'refused',flush=True)
    finally:
        assert {str(path):digest(path) for path in sorted(folder.glob('*.bin'))}==hashes,'native inputs changed'


if __name__=='__main__':main()
