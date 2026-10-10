#!/usr/bin/env python3
"""Independent read-only body-part slot audit; not skeleton/runtime compatibility."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_fmp_links import read_bank, crc0


def body_slots(data):
    cursor=3
    def take(size):
        nonlocal cursor
        if size<0 or size>len(data)-cursor:raise ValueError('truncated prefix')
        start=cursor;cursor+=size;return start
    def word():return struct.unpack_from('<I',data,take(4))[0]
    def marker(value):
        if word()!=value:raise ValueError('unsupported control marker')
    def count(minimum):
        value=word()
        if value>4096 or value>(len(data)-cursor)//minimum:raise ValueError('invalid count')
        return value
    marker(crc0('WoundMorphs'));take(count(44)*44)
    marker(0x01ab6cee);marker(crc0('RandomAppearanceMorph'))
    slots=[];total_models=total_textures=0
    for body in range(count(4)):
        models=count(17);total_models+=models
        if total_models>4096:raise ValueError('model total')
        for alternative in range(models):
            offset=cursor;model=word();word()
            textures=count(12);total_textures+=textures
            if total_textures>4096:raise ValueError('texture total')
            word();take(textures*12+1)
            slots.append(dict(body_part=body,alternative=alternative,offset=offset,id=model))
    take(27);take(count(13)*13);take(13)
    marker(crc0('Expressions'));take(count(12)*12)
    marker(0x01ab6d32);marker(crc0('Stats'))
    # Stats remains opaque. The body contract ends at its validated marker;
    # don't guess this unrelated block's length or interpret its scalar values.
    return cursor,slots


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--payload-exe',type=Path,default=repo/'build/fableforge_defrelocate_tests.exe')
    parser.add_argument('--schema',type=Path,default=repo/'docs/re_reference/def_schema.json')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args();native=args.install.resolve()
    assert not args.work_root.resolve().is_relative_to(native)
    inputs=sorted((native/'data/CompiledDefs').glob('*.bin'))
    def hashes():return {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    before=hashes();records=read_bank(native)
    result=subprocess.run([str(args.payload_exe.resolve()),'--creature-body-audit',str(native),str(args.schema.resolve())],
                          cwd=repo,capture_output=True,text=True,timeout=120)
    assert result.returncode==0,result.stderr
    report=json.loads(result.stdout);rows=report['records']
    assert {r['index'] for r in rows}=={i for i,r in enumerate(records) if r['definition']=='CCreatureDef'}
    accepted=refused=slots=nonempty=0
    for row in rows:
        try:prefix,expected=body_slots(records[row['index']]['data'])
        except ValueError:
            assert 'refused' in row,row
            refused+=1;continue
        assert 'refused' not in row,row
        data=records[row['index']]['data'];boundary=row['prefix_length']
        assert prefix<=boundary and data[boundary:boundary+4]==struct.pack('<I',crc0('InitialActions')),row
        assert row['models']==expected,row
        accepted+=1;slots+=len(expected);nonempty+=bool(expected)
    assert accepted and slots,'no populated body slots exercised'
    assert hashes()==before,'source banks changed'
    work=Path(tempfile.mkdtemp(prefix='creature-body-model-audit-',dir=args.work_root.resolve()))
    report.update(dict(passed=True,accepted=accepted,refused=refused,body_slots=slots,nonempty_components=nonempty,
                       source_hashes=before,runtime_compatibility_verified=False))
    (work/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Evidence retained at',work,flush=True)
    print(f'{accepted} components accepted; {refused} refused; {slots} body slots in {nonempty} populated components: PASS')


if __name__=='__main__':main()
