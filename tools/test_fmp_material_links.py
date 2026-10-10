#!/usr/bin/env python3
"""Verify a labelled original material-record derivative and dependency refusal."""
import argparse,copy,hashlib,json,shutil,struct,tempfile,zipfile
from pathlib import Path
from test_fmp_links import package_records,read_bank
from test_fmp_texture_layers import entries
from test_fmp_weapon_links import run
from test_fmp_identical_records import read_rows,write_rows
from test_mod_file_choice_contributors import snapshot

def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,required=True);parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build');args=parser.parse_args()
    args.work_root.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='fmp-material-links-',dir=args.work_root)).resolve()
    assert work.parent==args.work_root.resolve();print('Evidence retained at',work,flush=True)
    relatives=('data/CompiledDefs/game.bin','data/CompiledDefs/names.bin','data/lang/English/text.big',
        'data/graphics/pc/textures.big','data/graphics/graphics.big','data/Misc/pc/effects.big')
    inputs=[args.archive,args.tool,*(args.install/p for p in relatives)];before={str(p):digest(p) for p in inputs}
    base=work/'baseline'
    for relative in relatives:
        target=base/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(args.install/relative,target)
    member='mods/Object Gravity Hammer.fmp'
    with zipfile.ZipFile(args.archive) as archive:
        catalogue={r['file']:r for r in json.loads(archive.read('catalogue.json'))};raw=archive.read(member)
        assert hashlib.sha256(raw).hexdigest()==catalogue[member]['sha256']
    original=work/'original.fmp';original.write_bytes(raw);owner='MATERIAL_FLESH_HUMAN'
    dependency_refusals=[]
    for route in ('apply','merge'):
        output=work/('original-refused-'+route);output.mkdir();(output/'prior.txt').write_bytes(b'prior output')
        prior=snapshot(output)
        command=('fmp','apply',base,original,output) if route=='apply' else ('mods','merge',base,output,'--with',original)
        failure=run(args.tool,*command,good=False)
        (work/('original-refused-'+route+'.log')).write_text(failure.stdout+failure.stderr,encoding='utf-8')
        reason='missing texture SAWTOOTH_GREATSWORD' if route=='apply' else 'missing target MESH_SAWTOOTH_GREATSWORD'
        assert reason in failure.stderr and snapshot(output)==prior,(route,failure.stderr)
        dependency_refusals.append({'route':route,'reason':failure.stderr.strip()})
    package=work/'material-record-derivative.fmp';derivative_banks=read_rows(raw)
    derivative_banks=[b for b in derivative_banks if b['name'] in (b'GameBINEntries',b'GameBINLinkMetaData')]
    assert len(derivative_banks)==2
    for bank in derivative_banks:
        bank['rows']=[r for r in bank['rows'] if r['symbol'].decode()==owner];assert len(bank['rows'])==1
    write_rows(package,raw,derivative_banks);raw=package.read_bytes();package_hash=digest(package)
    source,_=package_records(package)['GameBINEntries'][owner]
    report=json.loads(run(args.tool,'fmp','links',package,'--groups','--json').stdout)
    groups=next(r['groups'] for r in report['records'] if r['record']==owner)
    assert len(groups)==8
    effects=entries(base/'data/Misc/pc/effects.big','PARTICLE_MAIN_PC',include_payload=False)
    pair_at=source.index(bytes.fromhex('2211cf93'))+4
    assert struct.unpack_from('<6I',source,pair_at)==(1,0,986,985,0,0)
    expected=bytearray(source);links=0;addressed=set()
    for group in groups:
        parts=group['path'].split('.');tag=int(parts[0]).to_bytes(4,'big');assert source.count(tag)==1
        at=source.index(tag)+4;count=struct.unpack_from('<I',source,at)[0];assert int(parts[2])<count
        if parts[0]=='571592595':
            assert len(group['targets'])==2;offsets=[at+8+int(parts[2])*20,at+12+int(parts[2])*20]
        else:
            assert parts[0] in ('1553013023','4282122264','3964128347') and len(group['targets'])==1
            offsets=[at+4+int(parts[2])*4]
        for at,target in zip(offsets,group['targets']):
            assert at not in addressed;addressed.add(at);struct.pack_into('<I',expected,at,effects[target]['id']);links+=1
    assert links==9
    direct,merged=work/'direct',work/'merged'
    run(args.tool,'fmp','apply',base,package,direct);run(args.tool,'mods','merge',base,merged,'--with',package)
    for relative in relatives:
        left=direct/relative if (direct/relative).is_file() else base/relative
        right=merged/relative if (merged/relative).is_file() else base/relative
        assert digest(left)==digest(right)
    for output in (direct,merged):
        record=next(r for r in read_bank(output) if r['name']==owner)
        assert record['definition']=='MATERIAL' and record['data']==expected
    print('Labelled material derivative:',links,'effect words, map key/floats and all other bytes preserved; bank parity PASS',flush=True)
    banks=read_rows(raw);mutations=[]
    for label,change in [('oversized-map','count'),('map-trailing-byte','trailing')]:
        changed=copy.deepcopy(banks);row=next(r for b in changed if b['name']==b'GameBINEntries' for r in b['rows'] if r['symbol'].decode()==owner)
        data=bytearray(row['data'])
        if change=='count':struct.pack_into('<I',data,pair_at,0xffffffff)
        else:data.append(0)
        row['data']=bytes(data);fixture=work/(label+'.fmp');write_rows(fixture,raw,changed);mutations.append((label,fixture))
    replacements=[('wrong-map-row',b'571592595.Blood Blending Effects.0.unknown',b'571592595.Blood Blending Effects.9.unknown'),
        ('wrong-map-member',b'571592595.Blood Blending Effects.0.unknown',b'571592595.Blood Blending Effects.0.Unknown'),
        ('wrong-vector-row',b'1553013023.Blood Splatter Effects.0.MaterialEffectID2',b'1553013023.Blood Splatter Effects.9.MaterialEffectID2'),
        ('wrong-vector-member',b'MaterialEffectID2',b'MaterialEffectID9'),
        ('missing-effect',b'BLOOD_BLENDABLE_RED_SMALL',b'X'*len(b'BLOOD_BLENDABLE_RED_SMALL'))]
    for label,old,new in replacements:
        changed=copy.deepcopy(banks);meta=next(r for b in changed if b['name']==b'GameBINLinkMetaData' for r in b['rows'] if r['symbol'].decode()==owner)
        assert old in meta['data'];meta['data']=meta['data'].replace(old,new)
        fixture=work/(label+'.fmp');write_rows(fixture,raw,changed);mutations.append((label,fixture))
    prior={route:snapshot(output) for route,output in [('apply',direct),('merge',merged)]};refusals=[]
    for label,fixture in mutations:
        for route,output in [('apply',direct),('merge',merged)]:
            command=('fmp','apply',base,fixture,output) if route=='apply' else ('mods','merge',base,output,'--with',fixture)
            failure=run(args.tool,*command,good=False);assert 'FMP' in failure.stderr and snapshot(output)==prior[route]
            refusals.append({'case':label,'route':route,'reason':failure.stderr.strip()})
        print(label,'both routes safely refuse: PASS',flush=True)
    assert digest(original)==catalogue[member]['sha256'] and digest(package)==package_hash
    assert before=={str(p):digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'file':member,'effect_links':links,'blend_links':2,'vector_links':7,
        'map_key_and_floats_unchanged':True,'all_other_bytes_unchanged':True,'effective_bank_parity':True,
        'original_dependency_refusals':dependency_refusals,'original_sha256':digest(original),
        'derivative_sha256':package_hash,'derivative_record':owner,
        'refusals':refusals,'inputs_sha256':before,'inputs_unchanged':True,'gameplay_tested':False},indent=2))
    print('PASS: labelled material pair/vectors,',len(refusals),'safe negative routes and both original dependency refusals',flush=True)

if __name__=='__main__':main()
