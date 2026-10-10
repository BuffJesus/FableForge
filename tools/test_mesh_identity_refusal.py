"""Mesh import refuses ambiguous donors and collisions with generated entry names."""
from pathlib import Path
import json
import struct
import subprocess
import tempfile
import zlib
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_meshimport import write_obj, write_png
from test_texture_lookup import texture_bank
from test_fmp_links import package_records


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='mesh-identity-',dir=repo/'build'))
    model=work/'cube.obj';write_obj(model)
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text())['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if f['name']=='Graphic' else 0 for f in fields]
    cases=[('existing-hull',True,'MESH_IDENTITY[PHYSICS]',False,False),
           ('unused-hull',False,'MESH_IDENTITY[PHYSICS]',False,True),
           ('existing-model',False,'MESH_IDENTITY',False,False),
           ('duplicate-donor',False,'Donor',True,False),
           ('unique-donor',True,'Donor',False,True)]
    results=[]
    print('Evidence retained at',work,flush=True)
    for label,collision,existing,duplicate,accepted in cases:
        root=work/label
        write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[f['name'] for f in fields])
        if duplicate:
            path=root/'data/CompiledDefs/game.bin';raw=path.read_bytes()
            payload=zlib.decompress(raw[49:])[2:]
            chunk=zlib.compress(struct.pack('<HH',4,4+len(payload))+payload+payload)
            # Two distinct slots deliberately share the exact donor name.
            path.write_bytes(raw[:9]+struct.pack('<I',2)+raw[13:25]+raw[13:21]+struct.pack('<I',1)+
                             struct.pack('<6I',2,0,0,2,len(chunk),len(chunk))+chunk)
        bank=root/'data/graphics/graphics.big'
        archive(bank,b'BIGB',[('MBANK_ALLMESHES',205,[(1,1,existing,b'preserved donor')])])
        image=None
        if label=='existing-hull':
            texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
            image=work/'new.png';write_png(image)
        before=snapshot(root)
        args=[str(repo/'build/forge.exe'),'mesh-import',str(model),'IDENTITY','--install',str(root)]
        if not collision:args.append('--no-collision')
        if image:args.extend(['--texture',str(image)])
        result=subprocess.run(args,capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert (result.returncode==0)==accepted,(label,result.stdout,result.stderr)
        if not accepted:
            assert ('ambiguous donor' if duplicate else 'already has a mesh named') in result.stderr,result.stderr
            assert snapshot(root)==before
            assert not list(root.glob('.forge-model-import-*'))
        else:
            records=package_records(bank,by_id=True)['MBANK_ALLMESHES']
            assert records[1][0]==b'preserved donor'
            assert len(records)==(3 if collision else 2)
        results.append(label)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':results},indent=2)+'\n',encoding='utf-8')
    print(len(results),'mesh identity checks passed')


if __name__=='__main__':
    main()
