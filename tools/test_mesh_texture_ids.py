"""Mesh-import texture IDs must be complete unsigned decimal values."""
from pathlib import Path
import json
import struct
import subprocess
import tempfile
from test_big_entry_markers import archive, markers
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_meshimport import write_obj, write_png
from test_fmp_links import package_records
from test_texture_lookup import texture_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='mesh-texture-ids-',dir=repo/'build'))
    root=work/'install';model=work/'cube.obj';write_obj(model)
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text())['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if f['name']=='Graphic' else 0 for f in fields]
    write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[f['name'] for f in fields])
    bank=root/'data/graphics/graphics.big'
    archive(bank,b'BIGB',[('MBANK_ALLMESHES',205,[(1,1,'Donor',b'opaque donor')])])
    textures=root/'data/graphics/pc/textures.big'
    texture_bank(textures,[(identity,'Texture_'+str(identity),20) for identity in (7,2147483647,2147483648,4294967295)])
    before=snapshot(root)
    print('Evidence retained at',work,flush=True)
    invalid=('1junk','1.5','+1','-1',' 1','1 ','4294967296','18446744073709551616','',None)
    checks=[]
    valid=('0','7','2147483647','2147483648','4294967295')
    for i,value in enumerate((*invalid,*valid)):
        args=[str(repo/'build/forge.exe'),'mesh-import',str(model),'NUMERIC_'+str(i),'--install',str(root),
              '--no-collision','--texture-id']
        if value is not None:args.append(value)
        result=subprocess.run(args,capture_output=True,text=True,timeout=30)
        (work/(str(i)+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        if value in valid:
            assert result.returncode==0,(result.stdout,result.stderr)
            info=subprocess.run([str(repo/'build/forge-tools.exe'),'mesh-info',str(bank),'MESH_NUMERIC_'+str(i),'--json'],
                                capture_output=True,text=True,check=True,timeout=30)
            mesh=json.loads(info.stdout)
            assert mesh['diffuse_of_material_0']==int(value),mesh
            assert mesh['texture_ids']==([int(value)] if int(value) else []),mesh
        else:
            assert result.returncode==2,(value,result.stdout,result.stderr)
            assert '--texture-id needs an unsigned decimal ID' in result.stderr
            assert snapshot(root)==before and not list(root.glob('.forge-model-import-*'))
        checks.append(value)
    for label,rows in [('missing-texture',[(8,'Other',20)]),
                       ('ambiguous-texture',[(7,'First',20),(7,'Second',40)]),
                       ('wrong-bank',None)]:
        if rows is None:
            archive(textures,b'BIGB',[('GBANK_FRONTEND_PC',122,[(7,0,'OtherBank',b'opaque')])])
        else:texture_bank(textures,rows)
        before=snapshot(root)
        result=subprocess.run([str(repo/'build/forge.exe'),'mesh-import',str(model),'REFERENCE_'+label.upper().replace('-','_'),
                               '--install',str(root),'--no-collision','--texture-id','7'],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0,(label,result.stdout,result.stderr)
        assert ('ambiguous' if label=='ambiguous-texture' else 'no ') in result.stderr,(label,result.stderr)
        assert snapshot(root)==before and not list(root.glob('.forge-model-import-*')),label
        checks.append(label)
    texture_bank(textures,[(7,'case_model_diffuse',10)])
    original=package_records(textures,by_id=True)['GBANK_MAIN_PC'][7]
    image=work/'new-texture.png';write_png(image)
    result=subprocess.run([str(repo/'build/forge.exe'),'mesh-import',str(model),'CASE_MODEL',
                           '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
    (work/'case-alias.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode==0,(result.stdout,result.stderr)
    info=subprocess.run([str(repo/'build/forge-tools.exe'),'mesh-info',str(bank),'MESH_CASE_MODEL','--json'],
                        capture_output=True,text=True,check=True,timeout=30)
    mesh=json.loads(info.stdout)
    assert mesh['diffuse_of_material_0']==8 and mesh['texture_ids']==[8],mesh
    entries=package_records(textures,by_id=True)['GBANK_MAIN_PC']
    assert entries[7]==original and 8 in entries,entries.keys()
    assert markers(textures)=={7:0,8:42},markers(textures)
    mesh_markers=markers(bank)
    assert mesh_markers[1]==0 and all(value==42 for identity,value in mesh_markers.items() if identity!=1),mesh_markers
    checks.append('case-alias-new-texture-reference')
    (work/'result.json').write_text(json.dumps({'passed':True,'values':checks},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'mesh texture-ID checks passed')


if __name__=='__main__':
    main()
