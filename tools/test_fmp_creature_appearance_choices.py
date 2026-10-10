#!/usr/bin/env python3
"""Original appearance prefix choices retain their own reference group layouts."""
import argparse,hashlib,json,shutil,struct,tempfile,zipfile
from pathlib import Path
from test_fmp_creature_appearance_links import appearance
from test_fmp_identical_records import read_rows,write_rows
from test_fmp_links import package_records,read_bank,crc0
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run
from test_mod_file_choice_contributors import snapshot

def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,required=True);parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build');args=parser.parse_args()
    args.work_root.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='fmp-appearance-choices-',dir=args.work_root)).resolve()
    assert work.parent==args.work_root.resolve();print('Evidence retained at',work,flush=True)
    relatives=('data/CompiledDefs/game.bin','data/CompiledDefs/names.bin','data/lang/English/text.big',
        'data/graphics/pc/textures.big','data/graphics/graphics.big','data/Misc/pc/effects.big')
    inputs=[args.archive,args.tool,*(args.install/p for p in relatives)]
    before={str(p):digest(p) for p in inputs};base=work/'baseline'
    for relative in relatives:
        target=base/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(args.install/relative,target)
    owner='CCreatureDef - Good and Evil Male Apprentices';anchor=struct.pack('<I',crc0('InitialActions'))
    meshes=entries(base/'data/graphics/graphics.big','MBANK_ALLMESHES',include_payload=False)
    textures=entries(base/'data/graphics/pc/textures.big','GBANK_MAIN_PC',include_payload=False)
    expected={};packages=[];source_hashes={};prefix_sizes={};linked={}
    with zipfile.ZipFile(args.archive) as archive:
        catalogue={r['file']:r for r in json.loads(archive.read('catalogue.json'))}
        for index,label in enumerate(('A1','A2'),1):
            member=f'mods/Dark Apprentices (Variant {index}).fmp';raw=archive.read(member)
            assert hashlib.sha256(raw).hexdigest()==catalogue[member]['sha256']
            package=work/(label+'.fmp');package.write_bytes(raw);packages.append(package);source_hashes[str(package)]=digest(package)
            source=package_records(package)['GameBINEntries'][owner][0];data=bytearray(source[:source.index(anchor)])
            bodies,skeletons,*_=appearance(source);assert not skeletons
            report=json.loads(run(args.tool,'fmp','links',package,'--groups','--json').stdout)
            groups=next(r for r in report['records'] if r['record']==owner)['groups'];count=0
            for group in groups:
                p=group['path'].split('.')
                if p[0]!='4000099073':continue
                refs=[]
                for body in bodies:
                    if int(p[2])>=len(body):continue
                    model=body[int(p[2])]
                    if len(p)==4:refs.append((model['mesh'],meshes))
                    elif int(p[4])<len(model['textures']):
                        at=model['textures'][int(p[4])];refs.extend([(at,textures),(at+4,textures)])
                assert len(refs)==len(group['targets']);count+=len(refs)
                for (at,lookup),target in zip(refs,group['targets']):struct.pack_into('<I',data,at,lookup[target]['id'])
            expected[label]=data;prefix_sizes[label]=len(data);linked[label]=count
    assert prefix_sizes['A1']!=prefix_sizes['A2'] and expected['A1']!=expected['A2']
    checks=[];picks=work/'picks.txt'
    # Retail has no such named record. First exercise whole new-record choices.
    for label,package in zip(('A1','A2'),packages):run(args.tool,'mods','add',base,package,'--name',label)
    for choice in ('','A1','A2'):
        picks.write_text(owner+'\t'+choice+'\n' if choice else '',encoding='utf-8');saved=picks.read_bytes()
        output=work/('new-'+(choice or 'order'))
        report=json.loads(run(args.tool,'mods','merge',base,output,'--with',*packages,'--picks',picks,'--json').stdout)
        record=next(r for r in read_bank(output) if r['name']==owner);winner=choice or 'A2'
        assert record['data'][:record['data'].index(anchor)]==expected[winner] and picks.read_bytes()==saved
        conflict=next(r for r in report['defs']['conflicts'] if r['record']==owner)
        assert conflict['winner']==winner and not conflict['field_merged']
        checks.append({'baseline':'record absent','route':'merge','choice':choice or 'order','winner':winner})
    # Seed an existing named baseline using a clearly labelled derivative.
    # Change only its first MeshGroup word so both original prefixes contribute.
    seed_package=work/'baseline-seed-derivative.fmp';raw=packages[0].read_bytes();banks=read_rows(raw)
    row=next(r for b in banks if b['name']==b'GameBINEntries' for r in b['rows'] if r['symbol'].decode()==owner)
    bodies,*_=appearance(row['data']);group_at=next(body[0]['mesh']+4 for body in bodies if body)
    seeded=bytearray(row['data']);struct.pack_into('<I',seeded,group_at,struct.unpack_from('<I',seeded,group_at)[0]+1)
    row['data']=bytes(seeded);write_rows(seed_package,raw,banks)
    expected['vanilla']=bytearray(expected['A1']);expected['vanilla'][group_at:group_at+4]=seeded[group_at:group_at+4]
    seed=work/'seed';run(args.tool,'fmp','apply',base,seed_package,seed)
    for name in ('game.bin','names.bin'):shutil.copy2(seed/'data/CompiledDefs'/name,base/'data/CompiledDefs'/name)
    baseline_record=next(r for r in read_bank(base) if r['name']==owner)['data']
    baseline_tail=baseline_record[baseline_record.index(anchor):]
    outputs={}
    for route in ('merge','build'):
        output=work/route;outputs[route]=output
        command=['mods',route,base,output]
        if route=='merge':command+=['--with',*packages]
        for choice in ('','A1','A2','vanilla'):
            picks.write_text(owner+'\t'+choice+'\n' if choice else '',encoding='utf-8');saved=picks.read_bytes()
            report=json.loads(run(args.tool,*command,'--picks',picks,'--json').stdout)
            conflict=next(r for r in report['defs']['conflicts'] if r['record']==owner)
            field=next(f for f in conflict['field_conflicts'] if f['part_index']==0);winner=choice or 'A2'
            assert conflict['field_merged'] and field['winner']==winner and field['choice_key']==''
            data=next(r['data'] for r in read_bank(output) if r['name']==owner);boundary=data.index(anchor)
            assert data[:boundary]==expected[winner] and data[boundary:]==baseline_tail and picks.read_bytes()==saved
            checks.append({'baseline':'derived distinct MeshGroup','route':route,'choice':choice or 'order','winner':winner})
    run(args.tool,'mods','disable',base,'A1');refusals=[]
    for route,output in outputs.items():
        command=['mods',route,base,output]
        if route=='merge':command+=['--with',packages[1]]
        picks.write_text(owner+'\tA1\n',encoding='utf-8');saved=picks.read_bytes();prior=snapshot(output)
        failure=run(args.tool,*command,'--picks',picks,'--json',good=False)
        assert 'unavailable contributor A1' in failure.stderr and snapshot(output)==prior and picks.read_bytes()==saved
        refusals.append({'route':route,'reason':failure.stderr.strip()})
        picks.write_text('',encoding='utf-8');run(args.tool,*command,'--picks',picks,'--json')
        data=next(r['data'] for r in read_bank(output) if r['name']==owner)
        assert data[:data.index(anchor)]==expected['A2']
        checks.append({'route':route,'choice':'clear unavailable','winner':'A2'})
    assert source_hashes=={str(p):digest(p) for p in packages} and before=={str(p):digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks':checks,'refusals':refusals,'prefix_sizes':prefix_sizes,
        'appearance_words':linked,'baseline_derivative_sha256':digest(seed_package),'baseline_changed_word':group_at,
        'inputs_sha256':before|source_hashes,'inputs_unchanged':True,'gameplay_tested':False},indent=2))
    print('PASS:',len(checks),'exact original prefix selections;',len(refusals),'safe unavailable refusals',flush=True)

if __name__=='__main__':main()
