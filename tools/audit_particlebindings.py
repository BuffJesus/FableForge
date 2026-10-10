"""Compare accepted loose TNG particle names with a nominated effects bank.

Uses a hash-pinned thing-text coverage report; refused files and WAD/GTG bodies
are not scanned. CBankFile::CreateSymbolMap 02F9C130 excludes names containing
'[' (IsSymbolPublic 02F9C100). CSymbolMap uses string contents, not name CRCs.
This checks metadata candidates only: active bank selection/inheritance, complete
archive storage, particle payloads, dependent assets and playback remain unchecked.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import subprocess

from audit_levsymbols import digest


def particle_symbols(entries):
    groups=defaultdict(list)
    for entry in entries:
        name=entry['name']
        if not name or '\0' in name or not name.isascii() or not 0<entry['id']<=0x7fffffff:
            raise ValueError('unsupported particle symbol metadata')
        if '[' not in name:
            groups[name].append({'id':entry['id'],'type':entry['type'],'name':name})
    return dict(groups)


def listed_particle_entries(document):
    banks=[bank for bank in document['banks'] if bank['bank']=='PARTICLE_MAIN_PC']
    if len(banks)!=1:
        raise ValueError('expected one nominated PARTICLE_MAIN_PC bank')
    bank=banks[0]
    if len(bank['entries'])!=bank['count']:
        raise ValueError('particle listing is filtered or incomplete')
    return bank['entries']


def particle_fields(text):
    # Called only after whole-file coverage acceptance and exact source hash
    # verification. The reviewed TNG subset has one directive per line and
    # permits this quoted field only inside CTCDParticleEmitter.
    result=[]
    for line,raw in enumerate(text.splitlines(),1):
        fields=raw.strip().split(None,1)
        if not fields or fields[0]!='ParticleTypeName':
            continue
        if len(fields)!=2 or not fields[1].endswith(';'):
            raise ValueError('particle field disagrees with accepted text syntax')
        value=fields[1][:-1].strip()
        if len(value)<2 or not value.startswith('"') or not value.endswith('"') or '"' in value[1:-1]:
            raise ValueError('particle field disagrees with accepted text syntax')
        result.append({'name':value[1:-1],'line':line})
    return result


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--coverage',type=Path,required=True)
    parser.add_argument('--effects',type=Path,required=True)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    coverage,effects,tool,output=(p.resolve() for p in (args.coverage,args.effects,args.tool,args.out))
    artifact=json.loads(coverage.read_text(encoding='utf-8'))
    roots=[Path(s['root']).resolve() for s in artifact['sources']]
    dependencies=[Path(__file__).resolve(),Path(__file__).with_name('audit_levsymbols.py').resolve()]
    protected=[coverage,effects,tool,*dependencies]
    if output in protected or any(output.is_relative_to(root) for root in roots):
        parser.error('report must not replace source data or tooling')
    hashes={str(p):digest(p) for p in protected}
    for source in artifact['sources']:
        for path,expected in source['input_sha256'].items():
            if hashes.setdefault(path,expected)!=expected or digest(Path(path))!=expected:
                raise RuntimeError('stale thing-text coverage: '+path)
    listed=subprocess.run([str(tool),'big','list',str(effects),'--json'],
                          capture_output=True,text=True,encoding='utf-8',check=True,timeout=30)
    groups=particle_symbols(listed_particle_entries(json.loads(listed.stdout)))
    report={'scope':__doc__,'coverage':str(coverage),'effects':str(effects),'source_admitted':False,
            'archive_storage_checked':False,'public_symbols':sum(map(len,groups.values())),'sources':[]}
    findings=0
    for source,root in zip(artifact['sources'],roots,strict=True):
        levels=root/'data/Levels'
        reviews=[r for r in source['reviews'] if Path(r['path']).resolve()==levels.resolve()]
        if len(reviews)!=1:
            raise RuntimeError('missing/ambiguous loose-file coverage')
        refused={Path(r['path']).resolve() for r in reviews[0]['refusals']}
        known={Path(p).resolve() for p in source['input_sha256'] if Path(p).suffix.lower()=='.tng'}
        actual={p.resolve() for p in levels.rglob('*') if p.is_file() and p.suffix.lower()=='.tng'}
        if known!=actual:
            raise RuntimeError('loose TNG file set changed since coverage')
        references=[]
        for path in sorted(known-refused):
            for row in particle_fields(path.read_text(encoding='ascii')):
                matches=groups.get(row['name'],[])
                row.update(file=str(path),candidates=matches,
                           status='missing' if not matches else 'ambiguous' if len(matches)>1 else 'unique')
                findings+=row['status']!='unique'
                references.append(row)
        report['sources'].append({'root':str(root),'accepted_loose_tng':len(known-refused),
            'refused_loose_tng':len(known&refused),'references':references})
        print(root.name,len(references),'particle fields,',len({r['name'] for r in references}),'names,',
              sum(r['status']!='unique' for r in references),'findings')
    for path,expected in hashes.items():
        if digest(Path(path))!=expected:
            raise RuntimeError('input changed during particle review: '+path)
    report.update(input_sha256=hashes,findings=findings)
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return int(findings!=0)


if __name__=='__main__':
    raise SystemExit(main())
