"""Mesh imports reserve collision/render IDs without wrapping to zero."""
from pathlib import Path
import json
import struct
import subprocess
import tempfile
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_meshimport import write_obj, write_png
from test_texture_lookup import texture_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='mesh-id-exhaustion-',dir=repo/'build'))
    model=work/'cube.obj';write_obj(model)
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text())['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if f['name']=='Graphic' else 0 for f in fields]
    checks=[]
    print('Evidence retained at',work,flush=True)
    for collision,maximum,accepted in ((False,0xffffffff,False),(True,0xffffffff,False),
                                      (True,0xfffffffe,False),(False,0xfffffffe,True),(True,0xfffffffd,True),
                                      (True,0x7ffffffe,True),(True,0x7fffffff,True)):
        label=('collision-' if collision else 'render-')+str(maximum)
        root=work/label
        write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[f['name'] for f in fields])
        bank=root/'data/graphics/graphics.big'
        archive(bank,b'BIGB',[('MBANK_ALLMESHES',205,[(maximum,1,'Donor',b'opaque donor')])])
        args=[str(repo/'build/forge.exe'),'mesh-import',str(model),'BOUNDARY','--install',str(root)]
        if not collision:args.append('--no-collision')
        if collision and maximum==0xfffffffe:
            # Exhaustion after preparing a new texture must roll back that
            # staged work as well as preserve the original graphics/defs.
            texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
            image=work/'new.png';write_png(image)
            args.extend(['--texture',str(image)])
        before=snapshot(root)
        result=subprocess.run(args,capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert (result.returncode==0)==accepted,(label,result.stdout,result.stderr)
        if not accepted:
            assert 'no free mesh entry IDs' in result.stderr
            assert snapshot(root)==before
            assert not list(root.glob('.forge-model-import-*'))
        else:
            listed=subprocess.run([str(repo/'build/forge-tools.exe'),'big','list',str(bank),'--json'],
                                  capture_output=True,text=True,check=True,timeout=30)
            rows=json.loads(listed.stdout)['banks'][0]['entries']
            identities={row['name']:row['id'] for row in rows}
            assert identities['Donor']==maximum
            assert identities['MESH_BOUNDARY']==maximum+(2 if collision else 1)
            if collision:assert identities['MESH_BOUNDARY[PHYSICS]']==maximum+1
            assert len(rows)==(3 if collision else 2) and all(row['id'] for row in rows)
            info=subprocess.run([str(repo/'build/forge-tools.exe'),'mesh-info',str(bank),'MESH_BOUNDARY','--json'],
                                capture_output=True,text=True,check=True,timeout=30)
            geometry=json.loads(info.stdout)
            expected_physics=maximum+1 if collision else 0
            assert geometry['physics_index']==expected_physics,geometry
            text=subprocess.run([str(repo/'build/forge-tools.exe'),'mesh-info',str(bank),'MESH_BOUNDARY'],
                                capture_output=True,text=True,check=True,timeout=30)
            assert 'physics '+str(expected_physics)+',' in text.stdout,text.stdout
        checks.append(label)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'mesh ID boundary checks passed')


if __name__=='__main__':
    main()
