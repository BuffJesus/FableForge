"""Combined collision hull indices must fit without changing triangle references."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import tempfile
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_meshimport import write_png
from test_texture_lookup import texture_bank


def write_model(path,total):
    binary=bytearray();views=[];accessors=[];primitives=[]
    def accessor(data,count,kind,component):
        while len(binary)%4:binary.append(0)
        views.append({'buffer':0,'byteOffset':len(binary),'byteLength':len(data)})
        binary.extend(data)
        accessors.append({'bufferView':len(views)-1,'componentType':component,'count':count,'type':kind})
        return len(accessors)-1
    for count in (32768,total-32768):
        positions=bytearray(count*12)
        struct.pack_into('<3f',positions,12,1,0,0)
        struct.pack_into('<3f',positions,(count-1)*12,0,1,0)
        position=accessor(positions,count,'VEC3',5126)
        indices=accessor(struct.pack('<3H',0,1,count-1),3,'SCALAR',5123)
        primitives.append({'attributes':{'POSITION':position},'indices':indices})
    data=path.with_suffix('.bin');data.write_bytes(binary)
    path.write_text(json.dumps({'asset':{'version':'2.0'},'buffers':[{'uri':data.name,'byteLength':len(binary)}],
                               'bufferViews':views,'accessors':accessors,'meshes':[{'primitives':primitives}]}),encoding='utf-8')
    return data


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='mesh-collision-limit-',dir=repo/'build'))
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text())['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if f['name']=='Graphic' else 0 for f in fields]
    checks=[];print('Evidence retained at',work,flush=True)
    for total,collision,accepted in ((65536,True,True),(65537,True,False),(65537,False,True)):
        label=str(total)+('-collision' if collision else '-render');root=work/label
        write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[f['name'] for f in fields])
        archive(root/'data/graphics/graphics.big',b'BIGB',[('MBANK_ALLMESHES',205,[(1,1,'Donor',b'opaque donor')])])
        texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
        model=work/(label+'.gltf');data=write_model(model,total)
        hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (model,data)}
        args=[str(repo/'build/forge.exe'),'mesh-import',str(model),'HULL_LIMIT','--install',str(root)]
        if not collision:args.append('--no-collision')
        if not accepted:
            image=work/'new.png';write_png(image);args.extend(['--texture',str(image)])
        before=snapshot(root)
        result=subprocess.run(args,capture_output=True,text=True,timeout=60)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert (result.returncode==0)==accepted,(label,result.stdout,result.stderr)
        if not accepted:
            assert 'collision hull exceeds 65536 vertices' in result.stderr
            assert snapshot(root)==before
            assert not list(root.glob('.forge-model-import-*'))
        else:
            info=subprocess.run([str(repo/'build/forge-tools.exe'),'mesh-info',str(root/'data/graphics/graphics.big'),
                                 'MESH_HULL_LIMIT','--json'],capture_output=True,text=True,check=True,timeout=60)
            geometry=json.loads(info.stdout)
            assert geometry['vertices']==total and geometry['triangles']==2,geometry
            assert (geometry['physics_index']!=0)==collision,geometry
        assert all(hashlib.sha256(p.read_bytes()).hexdigest()==value for p,value in hashes.items())
        checks.append(label)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'collision limit checks passed')


if __name__=='__main__':
    main()
