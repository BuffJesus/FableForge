#!/usr/bin/env python3
"""Read-only CDefs ownership observations for private Creature visual cloning.

This is not a clone eligibility test: CDefs row types/indices and sharing are
observed independently; native lookup semantics and component payload relocation
must be verified separately before using the graph as a writer contract.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_fmp_links import read_bank, crc0


VISUAL_TYPES=('CCreatureDef','CAppearanceDef')


def listing(record):
    data=record['data']
    if len(data)<5:raise ValueError('truncated CDefs header')
    count=struct.unpack_from('<H',data,3)[0]
    end=5+12*count
    if end+4>len(data) or data[end:end+4]!=struct.pack('<I',crc0('Components')):
        raise ValueError('counted CDefs does not end at Components tag')
    return [dict(row=i,offset=5+12*i,class_crc=key,component=component,owner=owner)
            for i in range(count)
            for key,component,owner in [struct.unpack_from('<III',data,5+12*i)]]


def analyze(records):
    roots={i:listing(record) for i,record in enumerate(records) if record['definition']=='CREATURE'}
    users=defaultdict(list)
    for index,rows in roots.items():
        for row in rows:
            if row['class_crc'] in {crc0(t) for t in VISUAL_TYPES}:
                users[row['component']].append(index)
    findings=[];counts=Counter();kinds=Counter()
    for index,rows in roots.items():
        selected=[];issues=[]
        for kind in VISUAL_TYPES:
            matches=[row for row in rows if row['class_crc']==crc0(kind)]
            kinds[kind+'.rows']+=len(matches)
            if len(matches)!=1:
                issues.append(kind+': expected one row, found '+str(len(matches)))
            for original in matches:
                row=dict(original,kind=kind)
                component=row['component'];owner=row['owner']
                valid_component=component<len(records) and records[component]['definition']==kind
                valid_owner=owner in roots
                row.update(component_type=records[component]['definition'] if component<len(records) else None,
                           component_name=records[component]['name'] if component<len(records) else None,
                           owner_name=records[owner]['name'] if owner<len(records) else None,
                           owner_is_donor=owner==index,creature_users=users[component])
                if not valid_component:issues.append(kind+': component target class/range mismatch')
                if not valid_owner:issues.append(kind+': owner is not a Creature entry')
                owner_matches=[r for r in roots.get(owner,[]) if r['class_crc']==crc0(kind)]
                row['owner_same_component_rows']=[r['row'] for r in owner_matches if r['component']==component]
                if not row['owner_same_component_rows']:
                    issues.append(kind+': named owner has no matching component row')
                kinds[kind+('.own' if owner==index else '.inherited')]+=1
                if len(users[component])>1:kinds[kind+'.shared_rows']+=1
                selected.append(row)
        counts['empty_listings' if not rows else 'nonempty_listings']+=1
        counts['roots_with_observation_issues' if issues else 'roots_without_observation_issues']+=1
        findings.append(dict(index=index,name=records[index]['name'],component_count=len(rows),
                             visual_rows=selected,issues=issues))
    return dict(entries=len(records),creature_roots=len(roots),counts=dict(counts),
                observations=dict(kinds),unique_visual_components=len(users),records=findings,
                clone_eligibility_verified=False)


def self_test():
    def root(rows):
        data=b'\x01\x00\x01'+struct.pack('<H',len(rows))
        data+=b''.join(struct.pack('<III',crc0(kind),component,owner) for kind,component,owner in rows)
        return dict(definition='CREATURE',name='ROOT',data=data+struct.pack('<I',crc0('Components')))
    records=[root([(VISUAL_TYPES[0],2,0),(VISUAL_TYPES[1],3,0)]),
             root([(VISUAL_TYPES[0],2,0),(VISUAL_TYPES[1],3,0)]),
             dict(definition=VISUAL_TYPES[0],name='',data=b''),
             dict(definition=VISUAL_TYPES[1],name='',data=b'')]
    report=analyze(records)
    assert report['counts']['roots_without_observation_issues']==2
    assert report['observations']['CCreatureDef.own']==1
    assert report['observations']['CCreatureDef.inherited']==1
    assert report['records'][1]['visual_rows'][0]['creature_users']==[0,1]
    records[1]=root([(VISUAL_TYPES[0],3,99),(VISUAL_TYPES[0],2,0)])
    assert len(analyze(records)['records'][1]['issues'])==5
    records[1]=root([])
    assert analyze(records)['counts']['empty_listings']==1
    try:listing(dict(data=b'\x01\x00\x01\xff\xff'))
    except ValueError:pass
    else:raise AssertionError('truncated row listing accepted')
    print('Creature visual graph synthetic observations: PASS',flush=True)


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--payload-exe',type=Path,help='Also inspect canonical relocation candidates, in memory only')
    parser.add_argument('--schema',type=Path,default=repo/'docs/re_reference/def_schema.json')
    args=parser.parse_args();self_test()
    if args.install is None:return
    native=args.install.resolve();inputs=sorted((native/'data/CompiledDefs').glob('*.bin'))
    def hashes():
        return {str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    before=hashes();records=read_bank(native);report=analyze(records)
    args.work_root.resolve().mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='creature-visual-graph-',dir=args.work_root.resolve()))
    if args.payload_exe:
        result=subprocess.run([str(args.payload_exe.resolve()),'--creature-visual-audit',str(native),
            str(args.schema.resolve())],capture_output=True,text=True,encoding='utf-8',timeout=120)
        (work/'payload-audit.log').write_text(result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stderr
        payload_report=json.loads(result.stdout)
        assert payload_report['entries']==len(records) and not payload_report['clone_eligibility_verified']
        shape_roots={row['index']:row for row in report['records'] if not row['issues']}
        assert {row['root'] for row in payload_report['records']}==set(shape_roots)
        summary=Counter()
        for row in payload_report['records']:
            root=row['root'];visual=shape_roots[root]['visual_rows']
            nodes=[root]+[r['component'] for r in visual]
            assert [r['index'] for r in row['payloads']]==nodes
            maps={node:len(records)+i for i,node in enumerate(nodes)}
            blocked=False
            for payload in row['payloads']:
                node=payload['index'];original=records[node]['data'];changed=bytearray(original)
                assert payload['type']==records[node]['definition']
                summary[payload['type']+'.clean_roundtrip' if payload['clean_roundtrip'] else payload['type']+'.unclean']+=1
                for rewrite in payload['rewrites']:
                    at=rewrite['offset'];value=struct.unpack_from('<I',original,at)[0]
                    assert rewrite['domain']=='game' and value==rewrite['before'] and maps[value]==rewrite['after']
                    struct.pack_into('<I',changed,at,rewrite['after'])
                if node==root:
                    expected=[r['offset']+member for r in listing(records[root]) for member,key in ((4,'component'),(8,'owner')) if r[key] in maps]
                    assert sorted(r['offset'] for r in payload['rewrites'] if r['offset']<payload['prefix'])==sorted(expected)
                for candidate in payload['unresolved']:
                    assert struct.unpack_from('<I',original,candidate['offset'])[0]==candidate['value'] and candidate['value'] in maps
                if payload['unresolved']:
                    assert 'after_hex' not in payload;blocked=True
                    summary[payload['type']+'.unresolved_payloads']+=1
                else:assert bytes.fromhex(payload['after_hex'])==bytes(changed)
                summary[payload['type']+'.verified_rewrites']+=len(payload['rewrites'])
            summary['graphs_with_unresolved_candidates' if blocked else 'graphs_without_unresolved_candidates']+=1
        (work/'payloads.json').write_text(json.dumps(payload_report,indent=2)+'\n',encoding='utf-8')
        report['payload_observations']=dict(summary)
    assert hashes()==before,'native bank inputs changed'
    report['native_hashes_unchanged']=before
    (work/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Evidence retained at',work,flush=True)
    print(json.dumps({k:v for k,v in report.items() if k not in ('records','native_hashes_unchanged')},indent=2))


if __name__=='__main__':main()
