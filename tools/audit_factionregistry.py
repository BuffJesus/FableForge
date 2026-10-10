"""Review faction registry CRC candidates and source-local faction relationships.

CFactionManager 02442B40 registers FACTION class ordinals 1..count-1 using
instance-name CRCs. EnemyFactions/AlliedFactions use this registry, not the
global definition-name table. No duplicate winner, runtime state, composition
or source admission is certified. TNG FactionName consumers are outside this audit.
Optional plans compare existing registry and relationship query candidates only.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import struct
import subprocess

from audit_defnamespace import names
from audit_levsymbols import digest
from audit_levlookupplans import planned_identities
from test_fmp_links import read_bank, crc0


def string_array(raw):
    if len(raw)<4:
        raise ValueError('truncated faction string array')
    count,=struct.unpack_from('<I',raw)
    if count>len(raw)-4:
        raise ValueError('faction string count exceeds extent')
    at=4
    result=[]
    for _ in range(count):
        end=raw.index(0,at)
        result.append(raw[at:end].decode('ascii'))
        at=end+1
    if at!=len(raw):
        raise ValueError('trailing faction array bytes')
    return result


def registry(entries):
    return registry_slots({i:(e['definition'],e['name']) for i,e in enumerate(entries)},range(len(entries)))


def registry_slots(targets,slots):
    groups=defaultdict(list)
    ordinal=0
    slots=list(slots)
    if len(set(slots))!=len(slots) or any(slot not in targets for slot in slots):
        raise ValueError('invalid faction source-to-plan slot map')
    for slot in slots:
        definition,name=targets[slot]
        if definition!='FACTION':
            continue
        if ordinal:
            groups[crc0(name)].append({'slot':slot,'class_ordinal':ordinal,'name':name})
        ordinal+=1
    return dict(groups)


def lookup(groups,name):
    # Unlike GetDefGlobalIndexFromName, this registry lookup hashes even empty
    # relation strings. CTCEnemy separately skips its empty authored override.
    candidates=groups.get(crc0(name),[])
    return {'name':name,'crc':crc0(name),'candidates':candidates,
            'status':'missing' if not candidates else 'ambiguous' if len(candidates)>1 else 'unique'}


def compare_registry(targets,source_slots,query_names):
    before=registry_slots(targets,source_slots)
    after=registry_slots(targets,sorted(targets))
    rows=[]
    for name in sorted(set(query_names)):
        source,planned=lookup(before,name),lookup(after,name)
        # Ordinals control registration (zero excluded), but map keys are CRCs:
        # changing a positive ordinal alone does not change query identity.
        identity=lambda r: sorted(c['slot'] for c in r['candidates'])
        rows.append({'name':name,'crc':planned['crc'],
                     'status':'changed' if identity(source)!=identity(planned) else planned['status'],
                     'source_candidates_in_planned_slots':source['candidates'],
                     'planned_candidates':planned['candidates']})
    return rows


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,action='append',required=True)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--schema',type=Path,default=repo/'docs/re_reference/def_schema.json')
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--plan',type=Path,action='append',default=[])
    args=parser.parse_args()
    roots=[p.resolve() for p in args.source]
    tool,schema,output=(p.resolve() for p in (args.tool,args.schema,args.out))
    dependencies=[tool,schema,Path(__file__).resolve(),*(Path(__file__).with_name(n).resolve() for n in
                  ('audit_defnamespace.py','audit_levsymbols.py','audit_levlookupplans.py','test_fmp_links.py'))]
    plans=[(p.resolve(),json.loads(p.read_text(encoding='utf-8'))) for p in args.plan]
    protected=[*roots,*(Path(p['baseline']).resolve() for _,p in plans),
               *(Path(s['root']).resolve() for _,p in plans for s in p['sources'])]
    if output in [*dependencies,*(p for p,_ in plans)] or any(output.is_relative_to(root) for root in protected):
        parser.error('report must not replace source files or tooling')
    hashes={str(p):digest(p) for p in dependencies}
    for path,plan in plans:
        hashes[str(path)]=digest(path)
        for filename,expected in plan['input_sha256'].items():
            if hashes.setdefault(filename,expected)!=expected or digest(Path(filename))!=expected:
                raise RuntimeError('stale or inconsistent namespace input: '+filename)
    planned=[(path,plan,planned_identities(plan)) for path,plan in plans]
    report={'scope':__doc__,'source_admitted':False,'sources':[]}
    findings=0
    for root in roots:
        for name in ('game.bin','names.bin'):
            path=root/'data/CompiledDefs'/name
            actual=digest(path)
            if hashes.setdefault(str(path),actual)!=actual:
                raise RuntimeError('source changed after namespace verification: '+str(path))
        if any(stored!=crc0(name) for stored,name in names(root).values()):
            raise RuntimeError('stored name CRC mismatch')
        # Reuse the core namespace validator before deriving class ordinals from
        # file order. CRC findings are allowed here: this registry is narrower.
        checked=subprocess.run([str(tool),'defs','review-namespace',str(root),str(root),'--json'],
                               capture_output=True,text=True,encoding='utf-8',timeout=30)
        validation=json.loads(checked.stdout)
        if checked.returncode not in (0,1) or not validation.get('analysis_complete'):
            raise RuntimeError('faction input namespace refused: '+validation.get('reason','invalid review'))
        bank=read_bank(root);groups=registry(bank)
        records=sorted((r for rows in groups.values() for r in rows),key=lambda r:r['slot'])
        ambiguous=[{'crc':crc,'candidates':rows} for crc,rows in groups.items() if len(rows)>1]
        findings+=len(ambiguous)+(not records)
        source={'root':str(root),'canonical_namespace_checked':True,'registered_entries':records,
                'ambiguous_groups':ambiguous,'relationships':[]}
        for record in records:
            run=subprocess.run([str(tool),'defs','decode',str(root),str(schema),str(record['slot']),'--json'],
                               capture_output=True,text=True,encoding='utf-8',check=True,timeout=30)
            decoded=json.loads(run.stdout)
            if not decoded['all_tags_ok'] or decoded['leftover'] or decoded['definition']!='FACTION':
                raise RuntimeError('unclean faction definition decode')
            raw=bytes.fromhex(decoded['prefix_hex'])
            for field in decoded['fields']:
                raw+=struct.pack('<I',int(field['tag'],16))+bytes.fromhex(field['raw_hex'])
            if raw!=bank[record['slot']]['data']:
                raise RuntimeError('faction reconstruction disagrees with independent bank reader')
            for key in ('EnemyFactions','AlliedFactions'):
                fields=[f for f in decoded['fields'] if f['name']==key and f['type']=='Vector_VCCharString__']
                if len(fields)!=1:
                    raise RuntimeError('missing/ambiguous faction relationship field')
                for index,name in enumerate(string_array(bytes.fromhex(fields[0]['raw_hex']))):
                    row=lookup(groups,name)
                    row.update(owner_slot=record['slot'],owner_name=record['name'],field=key,array_index=index)
                    source['relationships'].append(row)
                    findings+=row['status']!='unique'
        source['plans']=[]
        for path,plan,targets in planned:
            owners=[s for s in plan['sources'] if Path(s['root']).resolve()==root]
            if len(owners)!=1:
                raise RuntimeError('plan must contain source exactly once: '+str(root))
            queries=[r['name'] for r in records]+[r['name'] for r in source['relationships']]
            comparisons=compare_registry(targets,[e['target'] for e in owners[0]['prepared_game']],queries)
            count=sum(r['status']!='unique' for r in comparisons)
            findings+=count
            source['plans'].append({'path':str(path),'findings':count,'queries':comparisons})
        report['sources'].append(source)
        print(root.name,len(records),'registered,',len(source['relationships']),'relationships,',
              sum(r['status']!='unique' for r in source['relationships']),'relationship findings,',
              sum(p['findings'] for p in source['plans']),'planned query findings')
    for path,expected in hashes.items():
        if digest(Path(path))!=expected:
            raise RuntimeError('input changed during faction review: '+path)
    report.update(input_sha256=hashes,findings=findings)
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return int(findings!=0)


if __name__=='__main__':
    raise SystemExit(main())
