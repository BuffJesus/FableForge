"""Sparse glTF replacements import correctly; invalid sparse sources preserve banks."""
import argparse
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
from test_meshimport import write_png
from test_texture_lookup import texture_bank


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--khronos-sample',type=Path,help='Cached SimpleSparseAccessor.gltf (read-only)')
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='mesh-sparse-',dir=repo/'build'))
    root=work/'install'
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text())['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if field['name']=='Graphic' else 0 for field in fields]
    write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[field['name'] for field in fields])
    bank=root/'data/graphics/graphics.big'
    archive(bank,b'BIGB',[('MBANK_ALLMESHES',205,[(1,1,'Donor',b'opaque donor')])])
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    image=work/'new.png';write_png(image)
    binary=struct.pack('<9f',0,0,0,2,0,0,0,3,0)+bytes([1,2,0,0])+struct.pack('<6f',1,0,0,0,1,0)
    document={'asset':{'version':'2.0'},'buffers':[{'byteLength':len(binary)}],
              'bufferViews':[{'buffer':0,'byteLength':36},{'buffer':0,'byteOffset':36,'byteLength':2},
                             {'buffer':0,'byteOffset':40,'byteLength':24}],
              'accessors':[{'bufferView':0,'componentType':5126,'count':3,'type':'VEC3',
                            'sparse':{'count':2,'indices':{'bufferView':1,'componentType':5121},'values':{'bufferView':2}}}],
              'meshes':[{'primitives':[{'attributes':{'POSITION':0}}]}]}
    before=snapshot(root);checks=[]
    print('Evidence retained at',work,flush=True)
    def run(label,source,accepted,expected=None):
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        symbol='SPARSE_'+label.upper().replace('-','_')
        result=subprocess.run([str(repo/'build/forge.exe'),'mesh-import',str(source),symbol,
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert (result.returncode==0)==accepted,(label,result.stdout,result.stderr)
        if accepted:
            info=subprocess.run([str(repo/'build/forge-tools.exe'),'mesh-info',str(bank),'MESH_'+symbol,'--json'],
                                capture_output=True,text=True,check=True,timeout=30)
            geometry=json.loads(info.stdout)
            assert (geometry['vertices'],geometry['triangles'],geometry['bbox_min'],geometry['bbox_max'])==expected,geometry
            assert geometry['physics_index']!=0,geometry
            (work/(label+'-mesh.json')).write_text(info.stdout,encoding='utf-8')
        else:assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
        checks.append(label)
    for label in ('duplicate','descending','outside','zero-count','excess-count','short-values','strided-values','nonfinite'):
        doc=json.loads(json.dumps(document));data=bytearray(binary)
        if label=='duplicate':data[37]=1
        elif label=='descending':data[36:38]=bytes([2,1])
        elif label=='outside':data[37]=3
        elif label=='zero-count':doc['accessors'][0]['sparse']['count']=0
        elif label=='excess-count':doc['accessors'][0]['sparse']['count']=4
        elif label=='short-values':doc['bufferViews'][2]['byteLength']=23
        elif label=='strided-values':doc['bufferViews'][2]['byteStride']=12
        elif label=='nonfinite':struct.pack_into('<I',data,40,0x7fc00000)
        doc['buffers'][0]['uri']='data:application/octet-stream;base64,'+base64.b64encode(data).decode('ascii')
        source=work/(label+'.gltf');source.write_text(json.dumps(doc),encoding='utf-8')
        run(label,source,False)
    for label in ('base','zeros'):
        doc=json.loads(json.dumps(document))
        if label=='zeros':del doc['accessors'][0]['bufferView']
        doc['buffers'][0]['uri']='data:application/octet-stream;base64,'+base64.b64encode(binary).decode('ascii')
        source=work/(label+'.gltf');source.write_text(json.dumps(doc),encoding='utf-8')
        run(label,source,True,(3,1,[0,0,0],[100,0,100]))
    if args.khronos_sample:
        sample=args.khronos_sample.resolve();data=sample.parent/'SimpleSparseAccessor.bin'
        digest=hashlib.sha256(data.read_bytes()).hexdigest()
        run('khronos',sample,True,(14,12,[0,0,0],[600,0,400]))
        assert hashlib.sha256(data.read_bytes()).hexdigest()==digest
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'sparse import checks passed')


if __name__=='__main__':
    main()
