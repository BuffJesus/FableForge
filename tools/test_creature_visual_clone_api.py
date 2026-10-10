#!/usr/bin/env python3
"""Independent private root/body/appearance API output audit; no native writes.

Creates only a paired definition-bank result under a fresh owned evidence folder,
not a copied install. Publication, CLI, GUI and gameplay are separate gates.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_fmp_links import read_bank, crc0
from audit_creature_visual_graph import analyze, listing


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--exe',type=Path,default=repo/'build/fableforge_creaturevisualclone_tests.exe')
    parser.add_argument('--schema',type=Path,default=repo/'docs/re_reference/def_schema.json')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--payload-exe',type=Path,default=repo/'build/fableforge_defrelocate_tests.exe')
    args=parser.parse_args();sources=[args.install/'data/CompiledDefs'/name for name in ('game.bin','names.bin')]
    hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    originals=read_bank(args.install);observed=analyze(originals)
    shapes={r['index']:r for r in observed['records'] if not r['issues']}
    payload_result=subprocess.run([str(args.payload_exe.resolve()),'--creature-visual-audit',
        str(args.install.resolve()),str(args.schema.resolve())],capture_output=True,text=True,timeout=120)
    assert payload_result.returncode==0,payload_result.stderr
    payloads=json.loads(payload_result.stdout)
    by_root={r['root']:r['payloads'] for r in payloads['records']}
    args.work_root.resolve().mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='creature-private-api-',dir=args.work_root.resolve()));output=work/'output'
    result=subprocess.run([str(args.exe.resolve()),'--retail-audit',str(args.install.resolve()),
        str(args.schema.resolve()),str(output)],capture_output=True,text=True,timeout=180)
    (work/'command.log').write_text(result.stdout+'\n'+result.stderr,encoding='utf-8')
    assert result.returncode==0,result.stderr
    report=json.loads(result.stdout);created=read_bank(output);counts=Counter();expected_size=len(originals);seen=set()
    assert report['source_entries']==len(originals) and created[:len(originals)]==originals
    source_bank=sources[0].read_bytes();created_bank=(output/'data/CompiledDefs/game.bin').read_bytes()
    assert created_bank[:9]==source_bank[:9]
    assert created_bank[13:13+12*len(originals)]==source_bank[13:13+12*len(originals)],'original numeric table changed'
    ordinals=Counter(r['definition'] for r in originals)
    source_pool=sources[1].read_bytes();created_pool=(output/'data/CompiledDefs/names.bin').read_bytes()
    assert created_pool[20:len(source_pool)]==source_pool[20:],'original pool records changed'
    assert created_pool[:8]==source_pool[:8] and created_pool[16:20]==source_pool[16:20]
    cursor=len(source_pool);appended_names=0
    while cursor<len(created_pool):
        end=created_pool.index(0,cursor+4);name=created_pool[cursor+4:end].decode('ascii')
        assert struct.unpack_from('<I',created_pool,cursor)[0]==crc0(name),'appended name CRC mismatch'
        appended_names+=1;cursor=end+1
    assert struct.unpack_from('<I',created_pool,8)[0]==struct.unpack_from('<I',source_pool,8)[0]+appended_names
    assert struct.unpack_from('<I',created_pool,12)[0]==len(created_pool)-16
    for row in report['records']:
        donor=row['donor'];assert donor not in seen and originals[donor]['definition']=='CREATURE';seen.add(donor)
        if 'refused' in row:
            counts['refused']+=1
            assert donor not in shapes or any(p['unresolved'] for p in by_root[donor]),'unexpected refusal of a verified graph'
            continue
        assert donor in shapes and not any(p['unresolved'] for p in by_root[donor])
        slots=[row['root'],row['body'],row['appearance']];assert slots==list(range(expected_size,expected_size+3))
        expected_size+=3;counts['accepted']+=1
        visual=shapes[donor]['visual_rows'];nodes=[donor]+[r['component'] for r in visual]
        maps={node:slot for node,slot in zip(nodes,slots)}
        for n,(node,slot,payload) in enumerate(zip(nodes,slots,by_root[donor])):
            expected=bytearray(originals[node]['data'])
            for rewrite in payload['rewrites']:
                assert rewrite['domain']=='game' and rewrite['before'] in maps
                struct.pack_into('<I',expected,rewrite['offset'],maps[rewrite['before']])
            if n==0:
                selected={r['row']:i for i,r in enumerate(visual)}
                for original in listing(originals[donor]):
                    at=original['offset'];expected[at:at+12]=originals[donor]['data'][at:at+12]
                    if original['row'] in selected:
                        struct.pack_into('<II',expected,at+4,slots[1+selected[original['row']]],slots[0])
            assert created[slot]['data']==expected,'unexpected private graph payload delta'
            assert created[slot]['definition']==originals[node]['definition']
            assert struct.unpack_from('<I',created_bank,13+12*slot+8)[0]==ordinals[created[slot]['definition']]
            ordinals[created[slot]['definition']]+=1
        assert [created[s]['name'] for s in slots]==[
            f'FORGE_PRIVATE_API_ROOT_{donor}',f'FORGE_PRIVATE_API_BODY_{donor}',f'FORGE_PRIVATE_API_APPEARANCE_{donor}']
    assert seen=={i for i,r in enumerate(originals) if r['definition']=='CREATURE'}
    assert len(created)==expected_size==report['entries']
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==hashes[str(p)] for p in sources),'native input changed'
    summary=dict(counts=dict(counts),source_sha256=hashes,entries=len(created),
                 prior_entries_unchanged=True,original_pool_records_unchanged=True,
                 original_numeric_table_unchanged=True,appended_name_crcs_verified=True,
                 publisher_verified=False,gui_verified=False,gameplay_verified=False)
    (work/'result.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(summary['counts']));print('Private API evidence retained at',work)


if __name__=='__main__':main()
