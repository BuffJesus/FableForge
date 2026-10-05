#!/usr/bin/env python3
"""Independently audit original appearance model/texture/name reference words."""
import argparse,copy,hashlib,json,shutil,struct,tempfile,zipfile
from pathlib import Path
from test_fmp_links import package_records,read_bank,crc0
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run
from test_fmp_identical_records import read_rows,write_rows
from test_mod_file_choice_contributors import snapshot

def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def names(root):
    data=(root/'data/CompiledDefs/names.bin').read_bytes();at=20;result={}
    for _ in range(struct.unpack_from('<I',data,8)[0]):
        end=data.index(0,at+4);result.setdefault(data[at+4:end].decode('ascii'),at+4-20);at=end+1
    return result

def appearance(data):
    wounds=struct.unpack_from('<I',data,7)[0];at=11+wounds*44
    assert data[3:7]==bytes.fromhex('fff38e28')
    assert data[at:at+8]==bytes.fromhex('ee6cab01237dcbe3');at+=8
    random_count_at=at;bodies=struct.unpack_from('<I',data,at)[0];at+=4;layout=[]
    for _ in range(bodies):
        count=struct.unpack_from('<I',data,at)[0];at+=4;models=[]
        for _ in range(count):
            offset=at;textures=struct.unpack_from('<I',data,at+8)[0];at+=16;rows=[]
            for _ in range(textures):rows.append(at);at+=12
            at+=1;models.append({'mesh':offset,'textures':rows})
        layout.append(models)
    at+=27;count=struct.unpack_from('<I',data,at)[0];at+=4;skeletons=[]
    for _ in range(count):skeletons.append(at+8);at+=13
    at+=13;assert data[at:at+4]==bytes.fromhex('f0f1ca81')
    expressions=struct.unpack_from('<I',data,at+4)[0];expression_rows=at+8;at+=8+expressions*12
    assert data[at:at+8]==bytes.fromhex('326dab01b3bfa450')
    return layout,skeletons,random_count_at,expression_rows,wounds

def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,required=True);parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--record',help='audit a labelled one-record derivative, preserving its original record and metadata bytes')
    parser.add_argument('--members',nargs='+',default=['mods/Dark Apprentices (Variant 1).fmp',
        'mods/Dark Apprentices (Variant 2).fmp','mods/Hero Pack.fmp'])
    args=parser.parse_args();args.work_root.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='fmp-creature-appearance-',dir=args.work_root)).resolve()
    assert work.parent==args.work_root.resolve();print('Evidence retained at',work,flush=True)
    relatives=('data/CompiledDefs/game.bin','data/CompiledDefs/names.bin','data/lang/English/text.big',
        'data/graphics/pc/textures.big','data/graphics/graphics.big','data/Misc/pc/effects.big')
    inputs=[args.archive,args.tool,*(args.install/p for p in relatives)]
    before={str(p):digest(p) for p in inputs};base=work/'baseline'
    for relative in relatives:
        target=base/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(args.install/relative,target)
    schema=json.loads((repo/'docs/re_reference/def_schema.json').read_text())['CCreatureDef']['fields']
    fields={str(int.from_bytes(struct.pack('<I',crc0(f['name'])),'big')):f for f in schema}
    checks=[];refusals=[]
    with zipfile.ZipFile(args.archive) as archive:
        catalogue={r['file']:r for r in json.loads(archive.read('catalogue.json'))}
        for index,member in enumerate(args.members):
            case=work/str(index);case.mkdir();raw=archive.read(member)
            assert hashlib.sha256(raw).hexdigest()==catalogue[member]['sha256']
            package=case/('one-record-derivative.fmp' if args.record else 'original.fmp')
            if args.record:
                banks=read_rows(raw)
                for bank in banks:
                    if bank['name'] in (b'GameBINEntries',b'GameBINLinkMetaData'):
                        bank['rows']=[r for r in bank['rows'] if r['symbol'].decode()==args.record]
                        assert len(bank['rows'])==1
                write_rows(package,raw,banks);raw=package.read_bytes()
            else:package.write_bytes(raw)
            package_hash=digest(package);records=package_records(package)
            report=json.loads(run(args.tool,'fmp','links',package,'--groups','--json').stdout)
            selected=[r for r in report['records'] if any(g['path'].startswith('4000099073.') for g in r['groups'])]
            assert selected
            direct,merged=case/'direct',case/'merged'
            run(args.tool,'fmp','apply',base,package,direct)
            run(args.tool,'mods','merge',base,merged,'--with',package)
            def effective(output,relative):
                return output/relative if (output/relative).is_file() else base/relative
            for relative in relatives:assert digest(effective(direct,relative))==digest(effective(merged,relative))
            built=read_bank(direct);by_name={r['name']:r for r in built if r['name']}
            game_ids={r['name']:i for i,r in enumerate(built) if r['name']}
            name_ids=names(direct)
            mesh_ids={k:v['id'] for k,v in entries(effective(direct,'data/graphics/graphics.big'),'MBANK_ALLMESHES',include_payload=False).items()}
            texture_ids={k:v['id'] for k,v in entries(effective(direct,'data/graphics/pc/textures.big'),'GBANK_MAIN_PC',include_payload=False).items()}
            count=0;domains={'mesh':0,'texture':0,'name':0};all_links=0
            for row in selected:
                source,_=records['GameBINEntries'][row['record']]
                layout,skeletons,_,expression_rows,wounds=appearance(source);expected=bytearray(source)
                addressed=set()
                for group in row['groups']:
                    parts=group['path'].split('.');slots=[]
                    if parts[0]=='4000099073':
                        alternative=int(parts[2])
                        for body in layout:
                            if alternative>=len(body):continue
                            model=body[alternative]
                            if len(parts)==4:slots.append((model['mesh'],'mesh',mesh_ids))
                            else:
                                assert len(parts)==6 and parts[5]=='unknown';texture=int(parts[4])
                                if texture<len(model['textures']):
                                    at=model['textures'][texture];slots.extend([(at,'texture',texture_ids),(at+4,'texture',texture_ids)])
                        if len(parts)==4 and alternative<len(skeletons):slots.append((skeletons[alternative],'name',name_ids))
                        assert len(slots)==len(group['targets'])
                        count+=len(slots)
                        for _,domain,_ in slots:domains[domain]+=1
                    elif parts[0]=='4042377857':slots=[(expression_rows+int(parts[2])*12+8,'mesh',mesh_ids)]
                    elif parts[0]=='4294151720':
                        slot=0 if parts[3]=='Link to ID' else int(parts[3][-1]);assert int(parts[2])<wounds
                        slots=[(11+int(parts[2])*44+8+slot*4,'texture',texture_ids)]
                    else:
                        assert parts[0] in fields;field=fields[parts[0]]
                        tag=int(parts[0]).to_bytes(4,'big');assert source.count(tag)==1
                        at=source.index(tag)+4;lookup=name_ids if field['type'] in ('CDefString','Vector_VCDefString__') else game_ids
                        if len(parts)==4:
                            assert field['type'] in ('Vector_J','Vector_VCDefString__')
                            assert int(parts[2])<struct.unpack_from('<I',source,at)[0];at+=4+int(parts[2])*4
                        else:assert len(parts)==2
                        slots=[(at,'other',lookup)]
                    assert len(slots)==len(group['targets'])
                    for (at,_,lookup),target in zip(slots,group['targets']):
                        assert at not in addressed;addressed.add(at);struct.pack_into('<I',expected,at,lookup[target]);all_links+=1
                output=by_name[row['record']];assert output['definition']=='CCreatureDef' and output['data']==expected
            checks.append({'file':member,'derivative_record':args.record,'original_sha256':catalogue[member]['sha256'],
                'package_sha256':package_hash,'records':len(selected),'appearance_links':count,
                'all_creature_links':all_links,'domains':domains,'bank_parity':True})
            print(member,len(selected),'creatures',count,'appearance words',domains,'PASS',flush=True)
            banks=read_rows(raw);owner=selected[0]['record'];source_row=next(r for b in banks if b['name']==b'GameBINEntries' for r in b['rows'] if r['symbol'].decode()==owner)
            _,first_skeletons,count_at,_,_=appearance(source_row['data']);mutations=[]
            for label,at in [('oversized-wounds',7),('oversized-bodies',count_at),('oversized-models',count_at+4)]:
                changed=copy.deepcopy(banks);entry=next(r for b in changed if b['name']==b'GameBINEntries' for r in b['rows'] if r['symbol'].decode()==owner)
                data=bytearray(entry['data']);struct.pack_into('<I',data,at,0xffffffff);entry['data']=bytes(data)
                fixture=case/(label+'.fmp');write_rows(fixture,raw,changed);mutations.append((label,fixture))
            selected_groups=[g for g in selected[0]['groups'] if g['path'].startswith('4000099073.')]
            replacements=[('wrong-alternative',b'4000099073.Unknown.0.Unknown',b'4000099073.Unknown.9.Unknown')]
            for domain in ('mesh','texture','name'):
                candidates=[name for g in selected_groups for name in g['targets']
                    if name in {'mesh':mesh_ids,'texture':texture_ids,'name':name_ids}[domain]]
                if domain=='name':
                    candidates=[g['targets'][-1] for g in selected_groups
                        if len(g['path'].split('.'))==4 and int(g['path'].split('.')[2])<len(first_skeletons)]
                if candidates:
                    target=candidates[0].encode('ascii');replacements.append(('missing-'+domain,target,b'X'*len(target)))
            for label,old,new in replacements:
                changed=copy.deepcopy(banks)
                meta=next(r for b in changed if b['name']==b'GameBINLinkMetaData' for r in b['rows'] if r['symbol'].decode()==owner)
                assert old in meta['data'];meta['data']=meta['data'].replace(old,new)
                fixture=case/(label+'.fmp');write_rows(fixture,raw,changed);mutations.append((label,fixture))
            prior={route:snapshot(output) for route,output in [('apply',direct),('merge',merged)]}
            for label,fixture in mutations:
                for route,output in [('apply',direct),('merge',merged)]:
                    command=('fmp','apply',base,fixture,output) if route=='apply' else ('mods','merge',base,output,'--with',fixture)
                    failure=run(args.tool,*command,good=False)
                    assert 'FMP' in failure.stderr and snapshot(output)==prior[route]
                    refusals.append({'file':member,'case':label,'route':route,'reason':failure.stderr.strip()})
            assert digest(package)==package_hash
    assert before=={str(p):digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks':checks,'refusals':refusals,'inputs_sha256':before,
        'inputs_unchanged':True,'gameplay_tested':False},indent=2))
    print('PASS:',sum(r['appearance_links'] for r in checks),'appearance references;',len(refusals),'safe negative routes',flush=True)

if __name__=='__main__':main()
