"""Invalid mesh geometry, names and buffer URIs refuse without changing any bank."""
import argparse
from pathlib import Path
import base64
import hashlib
import json
import struct
import subprocess
import tempfile
from urllib.parse import quote
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_meshimport import write_glb, write_png
from test_texture_lookup import texture_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    args=parser.parse_args()
    forge,tool=args.forge.resolve(),args.tool.resolve()
    work=Path(tempfile.mkdtemp(prefix='mesh-invalid-geometry-',dir=repo/'build'))
    root=work/'install';model=work/'cube.glb';write_glb(model)
    original=model.read_bytes();json_length=struct.unpack_from('<I',original,12)[0]
    doc=json.loads(original[20:20+json_length]);binary=28+json_length
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text())['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if f['name']=='Graphic' else 0 for f in fields]
    write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[f['name'] for f in fields])
    archive(root/'data/graphics/graphics.big',b'BIGB',[('MBANK_ALLMESHES',205,[(1,1,'Donor',b'opaque donor')])])
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    image=work/'new.png';write_png(image)
    before=snapshot(root);checks=[]
    polygon=work/'polygon.obj'
    concave='v 0 0 0\nv 3 0 0\nv 3 3 0\nv 2 3 0\nv 2 1 0\nv 1 1 0\nv 1 3 0\nv 0 3 0\nf 1 2 3 4 5 6 7 8\n'
    import math
    large=''.join(f'v {math.cos(2*math.pi*i/1025)} {math.sin(2*math.pi*i/1025)} 0\n' for i in range(1025))
    large+='f '+' '.join(str(i+1) for i in range(1025))+'\n'
    for label,text in (('obj-crossing','v 0 0 0\nv 1 1 0\nv 0 1 0\nv 1 0 0\nf 1 2 3 4\n'),
                       ('obj-nonplanar',concave.replace('v 0 0 0','v 0 0 1',1)),('obj-face-limit',large)):
        polygon.write_text(text,encoding='utf-8')
        source_hash=hashlib.sha256(polygon.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(polygon),'INVALID_POLYGON',
                               '--texture-id','1','--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'OBJ' in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before and hashlib.sha256(polygon.read_bytes()).hexdigest()==source_hash,label
        assert not list(root.glob('.forge-model-import-*')),label
        checks.append(label)
    cases=[(attribute,bits) for attribute in ('POSITION','NORMAL','TEXCOORD_0')
           for bits in (0x7fc00000,0x7f800000,0xff800000)]
    cases.append(('POSITION',struct.unpack('<I',struct.pack('<f',3e37))[0]))
    print('Evidence retained at',work,flush=True)
    for attribute,bits in cases:
        raw=bytearray(original)
        index=doc['meshes'][0]['primitives'][0]['attributes'][attribute]
        accessor=doc['accessors'][index];view=doc['bufferViews'][accessor['bufferView']]
        offset=binary+view.get('byteOffset',0)+accessor.get('byteOffset',0)
        struct.pack_into('<I',raw,offset,bits);model.write_bytes(raw)
        label=attribute+'-'+hex(bits)
        result=subprocess.run([str(forge),'mesh-import',str(model),'INVALID_GEOMETRY',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'non-finite' in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        checks.append(label)
    raw=bytearray(original)
    accessor=doc['accessors'][doc['meshes'][0]['primitives'][0]['attributes']['POSITION']]
    view=doc['bufferViews'][accessor['bufferView']]
    offset=binary+view.get('byteOffset',0)+accessor.get('byteOffset',0)
    for i in range(accessor['count']*3):
        value=struct.unpack_from('<f',raw,offset+4*i)[0]
        struct.pack_into('<f',raw,offset+4*i,(value-(0.5 if i%3==1 else 0))*6e36)
    model.write_bytes(raw)
    result=subprocess.run([str(forge),'mesh-import',str(model),'INVALID_GEOMETRY',
                           '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
    (work/'sphere-overflow.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode!=0 and 'bounding sphere exceeds' in result.stderr,(result.stdout,result.stderr)
    assert snapshot(root)==before
    assert not list(root.glob('.forge-model-import-*'))
    checks.append('sphere-overflow')
    extra_doc=json.loads(json.dumps(doc));extra_doc['buffers'].append(dict(extra_doc['buffers'][0]))
    for view in extra_doc['bufferViews']:view['buffer']=1
    json_bytes=json.dumps(extra_doc).encode('utf-8')
    json_bytes+=b' '*((-len(json_bytes))%4)
    bin_chunk=original[binary-8:]
    bad_buffer=work/'unsupported-buffer.glb'
    bad_buffer.write_bytes(struct.pack('<III',0x46546c67,2,20+len(json_bytes)+len(bin_chunk))+
                           struct.pack('<II',len(json_bytes),0x4e4f534a)+json_bytes+bin_chunk)
    source_hash=hashlib.sha256(bad_buffer.read_bytes()).hexdigest()
    result=subprocess.run([str(forge),'mesh-import',str(bad_buffer),'INVALID_BUFFER',
                           '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
    (work/'unsupported-buffer.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode!=0 and 'no supported data source' in result.stderr,(result.stdout,result.stderr)
    assert snapshot(root)==before
    assert not list(root.glob('.forge-model-import-*'))
    assert hashlib.sha256(bad_buffer.read_bytes()).hexdigest()==source_hash
    checks.append('second-buffer-without-uri')
    encoded=base64.b64encode(original[binary:]).decode('ascii')
    for attribute,component,normalized in (
            ('POSITION',5125,False),('POSITION',5126,True),
            ('NORMAL',5125,False),('NORMAL',5126,True),
            ('TEXCOORD_0',5123,False),('TEXCOORD_0',5126,True),
            ('POSITION',None,False),('indices',None,False)):
        malformed=json.loads(json.dumps(doc))
        malformed['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
        primitive=malformed['meshes'][0]['primitives'][0]
        index=primitive['indices'] if attribute=='indices' else primitive['attributes'][attribute]
        accessor=malformed['accessors'][index]
        if component is None:
            accessor.pop('type' if attribute=='indices' else 'componentType')
        else:
            accessor['componentType']=component;accessor['normalized']=normalized
        label=f'encoding-{attribute}-{component}-{normalized}'
        source=work/(label+'.gltf');source.write_text(json.dumps(malformed),encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(source),'INVALID_ENCODING',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'glTF mesh 0 primitive 0:' in result.stderr,(label,result.stdout,result.stderr)
        if component is not None:
            assert attribute+' uses an unsupported component/normalization pair' in result.stderr,label
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash,label
        checks.append(label)
    for mode in (4,5,6):
        strided=json.loads(json.dumps(doc))
        primitive=strided['meshes'][0]['primitives'][0];primitive['mode']=mode
        accessor=strided['accessors'][primitive['indices']]
        assert accessor['componentType']==5123
        view=strided['bufferViews'][accessor['bufferView']]
        original_binary=original[binary:]
        start=view.get('byteOffset',0)+accessor.get('byteOffset',0)
        rows=b''.join(original_binary[start+2*i:start+2*i+2]+b'\0\0' for i in range(accessor['count']))
        padded=original_binary+b'\0'*((-len(original_binary))%4)
        offset=len(padded);padded+=rows
        accessor.pop('byteOffset',None)
        view.update(byteOffset=offset,byteLength=len(rows),byteStride=4)
        strided['buffers'][0].update(byteLength=len(padded),uri='data:application/octet-stream;base64,'+base64.b64encode(padded).decode('ascii'))
        label='strided-triangle-indices-'+str(mode)
        source=work/(label+'.gltf');source.write_text(json.dumps(strided),encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(source),'INVALID_INDEX_STRIDE',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'triangle index buffer view must not define byteStride' in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash,label
        checks.append(label)
    for component,offset,stride in ((5121,2,4),(5123,2,4),(5121,0,2)):
        unaligned=json.loads(json.dumps(doc))
        accessor=unaligned['accessors'][unaligned['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']]
        accessor.update(componentType=component,normalized=True,byteOffset=offset)
        width=2 if component==5121 else 4
        rows=b'\0'*(offset+(accessor['count']-1)*stride+width)
        padded=original[binary:]+b'\0'*((-len(original[binary:]))%4)
        start=len(padded);padded+=rows
        view=unaligned['bufferViews'][accessor['bufferView']]
        view.update(byteOffset=start,byteLength=len(rows))
        if stride==4:view['byteStride']=stride
        else:view.pop('byteStride',None)
        unaligned['buffers'][0].update(byteLength=len(padded),uri='data:application/octet-stream;base64,'+base64.b64encode(padded).decode('ascii'))
        label=f'unaligned-vertex-{component}-{offset}-{stride}'
        source=work/(label+'.gltf');source.write_text(json.dumps(unaligned),encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(source),'INVALID_VERTEX_ALIGNMENT',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'vertex attribute elements must be four-byte aligned' in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash,label
        checks.append(label)
    for attribute in ('POSITION','NORMAL','TEXCOORD_0'):
        oversized=json.loads(json.dumps(doc))
        oversized['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
        accessor=oversized['accessors'][oversized['meshes'][0]['primitives'][0]['attributes'][attribute]]
        accessor.pop('bufferView',None);accessor.pop('byteOffset',None);accessor['count']=1<<40
        label='oversized-'+attribute
        source=work/(label+'.gltf');source.write_text(json.dumps(oversized),encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(source),'OVERSIZED',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        expected='vertex overflow' if attribute=='POSITION' else 'same count as POSITION'
        assert result.returncode!=0 and expected in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
        checks.append(label)
    for extension in ('KHR_draco_mesh_compression','EXT_meshopt_compression','VENDOR_unknown'):
        required=json.loads(json.dumps(doc))
        required['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
        required['extensionsUsed']=[extension];required['extensionsRequired']=[extension]
        label='required-'+extension
        source=work/(label+'.gltf');source.write_text(json.dumps(required),encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(source),'REQUIRED_EXTENSION',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'unsupported required glTF extension: '+extension in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
        checks.append(label)
    for mode,count in ((4,30000003),(5,10000003),(6,10000003)):
        oversized=json.loads(json.dumps(doc))
        oversized['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
        oversized['accessors'].append({'componentType':5125,'count':count,'type':'SCALAR'})
        primitive=oversized['meshes'][0]['primitives'][0]
        primitive['indices']=len(oversized['accessors'])-1;primitive['mode']=mode
        label='oversized-indices-'+str(mode)
        source=work/(label+'.gltf');source.write_text(json.dumps(oversized),encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(source),'OVERSIZED_INDICES',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'preview triangle limit' in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
        checks.append(label)
    for label in ('missing-part-position','missing-attributes','missing-buffer-views'):
        malformed=json.loads(json.dumps(doc))
        malformed['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
        if label=='missing-part-position':malformed['meshes'][0]['primitives'].append({'attributes':{}})
        elif label=='missing-attributes':malformed['meshes'][0]['primitives'][0].pop('attributes')
        else:malformed.pop('bufferViews')
        source=work/(label+'.gltf');source.write_text(json.dumps(malformed),encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(source),'INVALID_PART',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        location='glTF mesh 0 primitive '+('1' if label=='missing-part-position' else '0')+':'
        assert result.returncode!=0 and location in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
        checks.append(label)
    for label,node_weights,mesh_weights in [('active-mesh-morph',None,[0.5]),('active-node-morph',[-0.5],[0]),('morph-count',[0,0],[0])]:
        morphed=json.loads(json.dumps(doc))
        morphed['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
        morphed['scenes']=[{'nodes':[0]}];morphed['nodes']=[{'mesh':0}]
        morphed['meshes'][0]['primitives'][0]['targets']=[{'POSITION':0}]
        morphed['meshes'][0]['weights']=mesh_weights
        if node_weights is not None: morphed['nodes'][0]['weights']=node_weights
        source=work/(label+'.gltf');source.write_text(json.dumps(morphed),encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(source),'MORPH_SHAPE',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'morph' in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
        checks.append(label)
    skinned=json.loads(json.dumps(doc))
    vertex_count=skinned['accessors'][0]['count']
    skin_binary=original[binary:binary+doc['buffers'][0]['byteLength']]
    joints_offset=len(skin_binary);skin_binary+=bytes(vertex_count*4)
    weights_offset=len(skin_binary);skin_binary+=struct.pack('<4f',1,0,0,0)*vertex_count
    skinned['buffers'][0]={'byteLength':len(skin_binary),'uri':'data:application/octet-stream;base64,'+base64.b64encode(skin_binary).decode()}
    for semantic,offset,length,component in [('JOINTS_0',joints_offset,vertex_count*4,5121),('WEIGHTS_0',weights_offset,vertex_count*16,5126)]:
        view=len(skinned['bufferViews']);accessor=len(skinned['accessors'])
        skinned['bufferViews'].append({'buffer':0,'byteOffset':offset,'byteLength':length})
        skinned['accessors'].append({'bufferView':view,'componentType':component,'count':vertex_count,'type':'VEC4'})
        skinned['meshes'][0]['primitives'][0]['attributes'][semantic]=accessor
    skinned['scenes']=[{'nodes':[0,1]}]
    skinned['nodes']=[{'mesh':0,'skin':0},{'translation':[5,0,0]}]
    skinned['skins']=[{'joints':[1]}]
    source=work/'active-skin.gltf';source.write_text(json.dumps(skinned),encoding='utf-8')
    source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
    result=subprocess.run([str(forge),'mesh-import',str(source),'SKIN_SHAPE',
                           '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
    (work/'active-skin.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode!=0 and 'skin' in result.stderr and 'bake' in result.stderr,(result.stdout,result.stderr)
    assert snapshot(root)==before and hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
    assert not list(root.glob('.forge-model-import-*'))
    checks.append('active-skin')
    transforms=[]
    for component in (3,7,11,15):
        matrix=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
        matrix[component]=2 if component==15 else 1
        transforms.append(('matrix-'+str(component),{'matrix':matrix},'must be affine'))
    for index,rotation in enumerate(([0,0,0,0],[0,0,0,2],[0,0,0.5,0.5])):
        transforms.append(('rotation-'+str(index),{'rotation':rotation},'rotation quaternion'))
    for label,transform,message in transforms:
        transformed=json.loads(json.dumps(doc))
        transformed['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
        transformed['scenes']=[{'nodes':[0]}];transformed['scene']=0
        transformed['nodes']=[dict(mesh=0,**transform)]
        source=work/(label+'.gltf');source.write_text(json.dumps(transformed),encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(source),'INVALID_TRANSFORM',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and message in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
        checks.append(label)
    for field,values in [('material',[-1,0.5,1,4294967296]),('mode',[-1,4.5,7,4294967300])]:
        for value in values:
            reference_doc=json.loads(json.dumps(doc))
            reference_doc['materials']=[{'name':'stone'}]
            reference_doc['meshes'][0]['primitives'][0][field]=value
            reference_doc['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
            label=field+'-'+str(value)
            source=work/(label+'.gltf');source.write_text(json.dumps(reference_doc),encoding='utf-8')
            source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
            result=subprocess.run([str(forge),'mesh-import',str(source),'INVALID_REFERENCE',
                                   '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
            (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
            assert result.returncode!=0 and field in result.stderr,(label,result.stdout,result.stderr)
            assert snapshot(root)==before,label
            assert not list(root.glob('.forge-model-import-*')),label
            assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
            checks.append(label)
    for label,asset in [('missing-version',{}),('old-version',{'version':'1.0'}),
                        ('future-minimum',{'version':'2.1','minVersion':'2.1'})]:
        version_doc=json.loads(json.dumps(doc));version_doc['asset']=asset
        version_doc['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
        source=work/(label+'.gltf');source.write_text(json.dumps(version_doc),encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        result=subprocess.run([str(forge),'mesh-import',str(source),'INVALID_VERSION',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'glTF asset' in result.stderr and 'version' in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
        checks.append(label)
    for attribute in ('NORMAL','TEXCOORD_0'):
        for field,value in [('count',23),('type','SCALAR')]:
            shape_doc=json.loads(json.dumps(doc))
            shape_doc['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
            accessor=shape_doc['meshes'][0]['primitives'][0]['attributes'][attribute]
            shape_doc['accessors'][accessor][field]=value
            label=attribute+'-'+field
            source=work/(label+'.gltf');source.write_text(json.dumps(shape_doc),encoding='utf-8')
            source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
            result=subprocess.run([str(forge),'mesh-import',str(source),'INVALID_SHAPE',
                                   '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
            (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
            assert result.returncode!=0 and attribute+' must be' in result.stderr,(label,result.stdout,result.stderr)
            assert snapshot(root)==before,label
            assert not list(root.glob('.forge-model-import-*')),label
            assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
            checks.append(label)
    invalid_name_doc=json.loads(json.dumps(doc))
    invalid_name_doc['buffers'][0]['uri']='data:application/octet-stream;base64,'+encoded
    invalid_name_doc['materials']=[{'name':'material\u0000suffix'}]
    invalid_name_doc['meshes'][0]['primitives'][0]['material']=0
    invalid_name=work/'invalid-name.gltf'
    invalid_name.write_text(json.dumps(invalid_name_doc),encoding='utf-8')
    source_hash=hashlib.sha256(invalid_name.read_bytes()).hexdigest()
    result=subprocess.run([str(forge),'mesh-import',str(invalid_name),'INVALID_NAME',
                           '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
    (work/'embedded-nul-name.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode!=0 and 'embedded NUL' in result.stderr,(result.stdout,result.stderr)
    assert snapshot(root)==before
    assert not list(root.glob('.forge-model-import-*'))
    assert hashlib.sha256(invalid_name.read_bytes()).hexdigest()==source_hash
    checks.append('embedded-nul-material-name')
    for label,uri in [('missing-encoding','data:application/octet-stream,'+encoded),
                      ('wrong-type','data:image/png;base64,'+encoded),
                      ('bad-alphabet','data:application/octet-stream;base64,'+encoded+'!'),
                      ('bad-padding','data:application/octet-stream;base64,'+encoded+'=trailing')]:
        doc['buffers'][0]['uri']=uri;model.write_text(json.dumps(doc),encoding='utf-8')
        result=subprocess.run([str(forge),'mesh-import',str(model),'INVALID_GEOMETRY',
                               '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'glTF buffer' in result.stderr,(label,result.stdout,result.stderr)
        assert snapshot(root)==before,label
        assert not list(root.glob('.forge-model-import-*')),label
        checks.append(label)
    vertices='v 0 0 0\nv 1 0 0\nv 0 1 0\n'
    for suffix in ('junk','.5','/2'):
        for kind,directive,face in (('position','v 0 0 1.0','f 1 2 3'),
                                    ('normal','vn 0 0 1.0','f 1//1 2//1 3//1'),
                                    ('uv','vt 0.25 1.0','f 1/1 2/1 3/1')):
            label=f'obj-{kind}-{len(checks)}';source=work/(label+'.obj')
            source.write_text(directive+suffix+'\n'+vertices+face+'\n',encoding='utf-8')
            source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
            result=subprocess.run([str(forge),'mesh-import',str(source),'INVALID_OBJ',
                                   '--texture',str(image),'--install',str(root)],capture_output=True,text=True,timeout=30)
            (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
            assert result.returncode!=0 and 'invalid OBJ' in result.stderr,(label,result.stdout,result.stderr)
            assert snapshot(root)==before and not list(root.glob('.forge-model-import-*')),label
            assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash,label
            checks.append(label)
    external=work/'cube \u00e9 #.bin';external.write_bytes(original[binary:])
    external_hash=hashlib.sha256(external.read_bytes()).hexdigest()
    for label,uri in [('embedded','data:application/gltf-buffer;base64,'+encoded),
                      ('external',quote(external.name,safe='')),
                      ('optional-extension','data:application/gltf-buffer;base64,'+encoded),
                      ('clipped-uv','data:application/gltf-buffer;base64,'+encoded),
                      ('decimal-fields','data:application/gltf-buffer;base64,'+encoded),
                      ('scientific-fields','data:application/gltf-buffer;base64,'+encoded)]:
        doc['buffers'][0]['uri']=uri
        def decimal_integers(value):
            if type(value) is int:return float(value)
            if isinstance(value,list):return [decimal_integers(child) for child in value]
            if isinstance(value,dict):return {key:decimal_integers(child) for key,child in value.items()}
            return value
        candidate=json.loads(json.dumps(doc))
        if label=='optional-extension':candidate['extensionsUsed']=['VENDOR_optional_geometry']
        if label=='clipped-uv':
            data=bytearray(original[binary:])
            accessor=candidate['accessors'][candidate['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']]
            view=candidate['bufferViews'][accessor['bufferView']]
            offset=view.get('byteOffset',0)+accessor.get('byteOffset',0)
            for vertex in range(2):struct.pack_into('<f',data,offset+vertex*view.get('byteStride',8),8)
            candidate['buffers'][0]['uri']='data:application/octet-stream;base64,'+base64.b64encode(data).decode('ascii')
        text=json.dumps(decimal_integers(candidate) if label.endswith('-fields') else candidate)
        if label=='scientific-fields':
            assert '"count": 24.0' in text
            text=text.replace('"count": 24.0','"count": 2.4e1')
        source=work/(label+'.gltf');source.write_text(text,encoding='utf-8')
        source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        name='URI_'+label.upper().replace('-','_')
        result=subprocess.run([str(forge),'mesh-import',str(source),name,
                               '--texture-id','1','--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,(label,result.stdout,result.stderr)
        if label=='optional-extension':assert 'ignored optional glTF extension: VENDOR_optional_geometry' in result.stdout
        if label=='clipped-uv':assert 'model part 1: texture coordinates exceed the supported range on 2 vertices' in result.stdout
        else:assert 'texture coordinates exceed' not in result.stdout
        info=subprocess.run([str(tool),'mesh-info',str(root/'data/graphics/graphics.big'),
                             'MESH_'+name,'--json'],capture_output=True,text=True,check=True,timeout=30)
        geometry=json.loads(info.stdout)
        assert geometry['vertices']==24 and geometry['triangles']==12,geometry
        assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
        assert hashlib.sha256(external.read_bytes()).hexdigest()==external_hash
        checks.append(label)
    source=work/'signed.obj'
    source.write_text('v +0 -0 0e0 1\nv +1.0 0E+2 -.0\nv .0 +1 0\n'
                      'vt +2.5e-1 +.5 0\nvn +0.0 -0 +1.0\nf 1/1/1 2/1/1 3/1/1',encoding='utf-8')
    source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
    result=subprocess.run([str(forge),'mesh-import',str(source),'SIGNED_OBJ',
                           '--texture-id','1','--install',str(root)],capture_output=True,text=True,timeout=30)
    (work/'signed-obj.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode==0,(result.stdout,result.stderr)
    info=subprocess.run([str(tool),'mesh-info',str(root/'data/graphics/graphics.big'),
                         'MESH_SIGNED_OBJ','--json'],capture_output=True,text=True,check=True,timeout=30)
    geometry=json.loads(info.stdout)
    assert geometry['vertices']==3 and geometry['triangles']==1,geometry
    assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
    checks.append('signed-obj')
    polygon.write_text(concave,encoding='utf-8')
    source_hash=hashlib.sha256(polygon.read_bytes()).hexdigest()
    result=subprocess.run([str(forge),'mesh-import',str(polygon),'CONCAVE_OBJ',
                           '--texture-id','1','--install',str(root)],capture_output=True,text=True,timeout=30)
    (work/'concave-obj.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode==0,(result.stdout,result.stderr)
    def mesh_info(name):
        info=subprocess.run([str(tool),'mesh-info',str(root/'data/graphics/graphics.big'),name,'--json'],
                            capture_output=True,text=True,check=True,timeout=30)
        return json.loads(info.stdout)
    geometry=mesh_info('MESH_CONCAVE_OBJ');hull=mesh_info('MESH_CONCAVE_OBJ[PHYSICS]')
    assert geometry['vertices']==8 and geometry['triangles']==6,geometry
    assert hull['type']==3 and hull['magic']=='>>>>3DMF' and hull['decoded']==hull['uncompressed'],hull
    assert geometry['physics_index']==hull['id'],(geometry,hull)
    assert next(chunk['size'] for chunk in hull['chunks'] if chunk['tag']=='TRIS')==4+6*6,hull
    assert hashlib.sha256(polygon.read_bytes()).hexdigest()==source_hash
    checks.append('concave-obj-with-linked-hull')
    for label,text,expected in (
        ('wide-finite-bounds','v -2e36 -1 0\nv 2e36 -1 0\nv 0 1 0\nf 1 2 3\n',{'MeshHeight':2,'ApproxMaxMeshHeight':2,'MeshRadius':2e36}),
        ('tall-finite-bounds','v -1 -2e36 0\nv 1 -2e36 0\nv 0 2e36 0\nf 1 2 3\n',{'MeshHeight':4e36,'ApproxMaxMeshHeight':4e36,'MeshRadius':1})):
        polygon.write_text(text,encoding='utf-8');source_hash=hashlib.sha256(polygon.read_bytes()).hexdigest()
        name=label.upper().replace('-','_')
        result=subprocess.run([str(forge),'mesh-import',str(polygon),name,'--texture-id','1','--no-collision',
                               '--install',str(root)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,(result.stdout,result.stderr)
        decoded=subprocess.run([str(tool),'defs','decode',str(root),str(repo/'docs/re_reference/def_schema.json'),
                                'OBJECT_'+name,'--json'],capture_output=True,text=True,check=True,timeout=30)
        fields_by_name={field['name']:field for field in json.loads(decoded.stdout)['fields']}
        for field,wanted in expected.items():
            actual=struct.unpack('<f',bytes.fromhex(fields_by_name[field]['raw_hex']))[0]
            assert math.isfinite(actual) and math.isclose(actual,wanted,rel_tol=1e-6),(label,field,actual,wanted)
        assert hashlib.sha256(polygon.read_bytes()).hexdigest()==source_hash
        checks.append(label)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'geometry/URI import checks passed')


if __name__=='__main__':
    main()
