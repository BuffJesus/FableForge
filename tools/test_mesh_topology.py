"""glTF strips/fans become saved triangle lists with collision; bad counts preserve banks."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_texture_lookup import texture_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='mesh-topology-',dir=repo/'build'))
    root=work/'install';bank=root/'data/graphics/graphics.big'
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text())['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if field['name']=='Graphic' else 0 for field in fields]
    write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[f['name'] for f in fields])
    archive(bank,b'BIGB',[('MBANK_ALLMESHES',205,[(1,1,'Donor',b'opaque donor')])])
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    checks=[]
    print('Evidence retained at',work,flush=True)
    for mode in (5,6):
        # Both layouts describe the same square with counterclockwise triangles.
        points=[(0,0,0),(1,0,0),(0,1,0),(1,1,0)] if mode==5 else [(0,0,0),(1,0,0),(1,1,0),(0,1,0)]
        for indexed in (False,True):
            for mirrored in (False,True):
                binary=struct.pack('<12f',*(value for point in points for value in point))
                doc={'asset':{'version':'2.0'},'buffers':[{}],
                     'bufferViews':[{'buffer':0,'byteLength':48}],
                     'accessors':[{'bufferView':0,'componentType':5126,'count':4,'type':'VEC3'}],
                     'meshes':[{'primitives':[{'attributes':{'POSITION':0},'mode':mode}]}]}
                if indexed:
                    binary+=bytes([0,1,2,3])
                    doc['bufferViews'].append({'buffer':0,'byteOffset':48,'byteLength':4})
                    doc['accessors'].append({'bufferView':1,'componentType':5121,'count':4,'type':'SCALAR'})
                    doc['meshes'][0]['primitives'][0]['indices']=1
                if mirrored:
                    doc['scenes']=[{'nodes':[0]}];doc['nodes']=[{'mesh':0,'scale':[-1,1,1]}]
                doc['buffers'][0]={'byteLength':len(binary),'uri':'data:application/octet-stream;base64,'+base64.b64encode(binary).decode('ascii')}
                for valid in (False,True):
                    candidate=json.loads(json.dumps(doc))
                    if not valid:candidate['accessors'][1 if indexed else 0]['count']=2
                    label=f'mode{mode}_indexed{int(indexed)}_mirror{int(mirrored)}_valid{int(valid)}'
                    source=work/(label+'.gltf');source.write_text(json.dumps(candidate),encoding='utf-8')
                    digest=hashlib.sha256(source.read_bytes()).hexdigest();before=snapshot(root)
                    symbol='TOPOLOGY_'+label.upper()
                    result=subprocess.run([str(repo/'build/forge.exe'),'mesh-import',str(source),symbol,
                                           '--texture-id','1','--install',str(root)],capture_output=True,text=True,timeout=30)
                    (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
                    assert (result.returncode==0)==valid,(label,result.stdout,result.stderr)
                    if valid:
                        info=subprocess.run([str(repo/'build/forge-tools.exe'),'mesh-info',str(bank),'MESH_'+symbol,'--json'],
                                            capture_output=True,text=True,check=True,timeout=30)
                        geometry=json.loads(info.stdout)
                        assert geometry['vertices']==4 and geometry['triangles']==2,geometry
                        assert geometry['bbox_min']==([-100,0,0] if mirrored else [0,0,0]),geometry
                        assert geometry['bbox_max']==([0,0,100] if mirrored else [100,0,100]),geometry
                        assert geometry['physics_index']!=0,geometry
                        (work/(label+'-mesh.json')).write_text(info.stdout,encoding='utf-8')
                    else:
                        assert 'at least three' in result.stderr,(label,result.stderr)
                        assert snapshot(root)==before,label
                    assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
                    assert not list(root.glob('.forge-model-import-*')),label
                    checks.append(label)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'topology import checks passed')


if __name__=='__main__':
    main()
