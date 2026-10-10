#!/usr/bin/env python3
"""Read-only native-layout observations; not relocation or clone eligibility.

Layout evidence: FableWin CAnimationSet 01fa26b0/01fa27a0,
CAnimationEntry 01f9d270/01f9d590, component readers 01f9b300..01f9d200,
matching PDB EAnimComponent and member layouts. No numeric-match rewriting.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import tempfile
from test_fmp_links import read_bank, crc0
from audit_creature_visual_graph import listing


def decode_animation(data):
    spans=[];cursor=0
    def take(size,label):
        nonlocal cursor
        if size<0 or size>len(data)-cursor:raise ValueError('truncated '+label)
        offset=cursor;cursor+=size
        spans.append(dict(offset=offset,size=size,label=label))
        return data[offset:cursor]
    def word(label):return struct.unpack('<I',take(4,label))[0]
    count=word('entry_count')
    if count>(len(data)-cursor)//20:raise ValueError('entry count exceeds minimum extent')
    for i in range(count):
        prefix=f'entries.{i}.'
        word(prefix+'key');word(prefix+'BankIndex')
        word(prefix+'AnimName');word(prefix+'GroupName')
        components=word(prefix+'component_count')
        if components>(len(data)-cursor)//8:raise ValueError('component count exceeds minimum extent')
        seen=set()
        for j in range(components):
            kind=word(prefix+f'components.{j}.type')
            if kind not in range(13) or kind in seen:raise ValueError('unknown/duplicate component type')
            seen.add(kind);label=prefix+f'components.{j}.type{kind}'
            if kind==12:
                size=word(label+'.string_length');take(size,label+'.string_bytes')
            elif kind in (3,4):
                word(label+'.first');word(label+'.second')
            elif kind==6:
                take(1,label+'.bool0');take(1,label+'.bool1')
            elif kind==7:
                take(1,label+'.bool');take(4,label+'.x');take(4,label+'.y')
            else:
                word(label+('.name_offset' if kind in (8,11) else '.scalar'))
    for label in ('DefaultFlags','DefaultTransitionTime','DefaultDelay','DefaultGroup'):
        word(label)
    return cursor,spans,count


def self_test():
    data=struct.pack('<I',1)+struct.pack('<5I',99,2,3,4,3)
    data+=struct.pack('<II',0,123)+struct.pack('<I',6)+b'\x01\x00'
    data+=struct.pack('<II',12,3)+b'abc'+struct.pack('<4I',1,2,3,4)
    end,spans,count=decode_animation(data)
    assert end==len(data) and count==1
    assert any(s['label'].endswith('.string_bytes') and s['size']==3 for s in spans)
    for malformed in (data[:-1],struct.pack('<I',0xffffffff),
                      struct.pack('<6I',1,0,0,0,0,1)+struct.pack('<II',13,0)+b'\0'*16):
        try:decode_animation(malformed)
        except ValueError:pass
        else:raise AssertionError('malformed animation accepted')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path)
    parser.add_argument('--work-root',type=Path,default=Path(__file__).resolve().parents[1]/'build')
    args=parser.parse_args();self_test()
    if not args.install:
        print('Animation layout synthetic checks PASS');return
    sources=[args.install/'data/CompiledDefs'/name for name in ('game.bin','names.bin')]
    hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    records=read_bank(args.install);observations=[];failures=[];counts=Counter()
    decoded={}
    for index,record in enumerate(records):
        if record['definition']!='CAppearanceDef':continue
        data=record['data']
        try:
            if data[3:7]!=struct.pack('<I',crc0('Graphic')) or data[24:28]!=struct.pack('<I',crc0('Animation')):
                raise ValueError('unsupported appearance Graphic/Animation boundary')
            end,spans,count=decode_animation(data[28:])
            if data[28+end:32+end]!=struct.pack('<I',crc0('OutlineEffectAlphaRef')):
                raise ValueError('Animation does not end at next known field tag')
            decoded[index]=(end,spans);counts['appearance_payloads']+=1;counts['animation_entries']+=count
        except ValueError as error:failures.append(dict(index=index,error=str(error)))
    for index,record in enumerate(records):
        if record['definition']!='CREATURE':continue
        for row in listing(record):
            component=row['component']
            if row['class_crc']!=crc0('CAppearanceDef') or component not in decoded:continue
            end,spans=decoded[component];data=records[component]['data']
            for offset in range(28,28+end-3):
                if struct.unpack_from('<I',data,offset)[0]!=index:continue
                relative=offset-28
                overlaps=[s for s in spans if relative<s['offset']+s['size'] and relative+4>s['offset']]
                observations.append(dict(root=index,appearance=component,offset=offset,value=index,
                    spans=overlaps,whole_primitive=len(overlaps)==1 and overlaps[0]['offset']==relative and overlaps[0]['size']==4))
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==hashes[str(p)] for p in sources),'native bank changed'
    args.work_root.resolve().mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='creature-animation-audit-',dir=args.work_root.resolve()))
    report=dict(source_sha256=hashes,counts=dict(counts),layout_failures=failures,
                candidate_observations=observations,clone_eligibility_verified=False)
    (work/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(counts=dict(counts),layout_failures=len(failures),candidates=len(observations))))
    print('Read-only animation observations retained at',work)
    assert not failures,'appearance layout failures: inspect retained observations'


if __name__=='__main__':main()
