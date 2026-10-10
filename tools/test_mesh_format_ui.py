"""Import sparse/strip models, report clipped UVs, and preserve banks/preview on refusal."""
import base64
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from PIL import Image
from test_big_entry_markers import archive, markers
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_meshimport import write_png
from test_texture_lookup import texture_bank
from test_fmp_links import package_records


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size',default='1440x900')
    parser.add_argument('--scale',type=float,default=1)
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='mesh-format-ui-',dir=repo/'build'))
    root=work/'install';bank=root/'data/graphics/graphics.big'
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text())['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if field['name']=='Graphic' else 0 for field in fields]
    write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[f['name'] for f in fields])
    archive(bank,b'BIGB',[('MBANK_ALLMESHES',205,[(1,1,'Donor',b'opaque donor')])])
    textures=root/'data/graphics/pc/textures.big'
    texture_bank(textures,[(1,'ui_format_sparse_diffuse',20)])
    old_texture=package_records(textures,by_id=True)['GBANK_MAIN_PC'][1]
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
    image=work/'diffuse.png';write_png(image)
    sparse=bytes([1,2,0,0])+struct.pack('<6f',1,0,0,0,1,0)
    strip=struct.pack('<12f',0,0,0,1,0,0,0,1,0,1,1,0)
    sparse_doc={'asset':{'version':'2.0'},'buffers':[],
                'bufferViews':[{'buffer':0,'byteLength':2},{'buffer':0,'byteOffset':4,'byteLength':24}],
                'accessors':[{'componentType':5126,'count':3,'type':'VEC3','sparse':{'count':2,
                    'indices':{'bufferView':0,'componentType':5121},'values':{'bufferView':1}}}],
                'meshes':[{'primitives':[{'attributes':{'POSITION':0}}]}]}
    strip_doc={'asset':{'version':'2.0'},'buffers':[],
               'bufferViews':[{'buffer':0,'byteLength':48}],
               'accessors':[{'bufferView':0,'componentType':5126,'count':4,'type':'VEC3'}],
               'meshes':[{'primitives':[{'attributes':{'POSITION':0},'mode':5}]}]}
    sources={}
    clipped_doc=json.loads(json.dumps(strip_doc))
    clipped_doc['bufferViews'].append({'buffer':0,'byteOffset':48,'byteLength':32})
    clipped_doc['accessors'].append({'bufferView':1,'componentType':5126,'count':4,'type':'VEC2'})
    clipped_doc['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']=1
    clipped=strip+struct.pack('<8f',8,0,8,1,8,0,8,1)
    for label,doc,data in [('SPARSE',sparse_doc,sparse),('STRIP',strip_doc,strip),('CLIPPED',clipped_doc,clipped)]:
        doc['buffers']=[{'byteLength':len(data),'uri':'data:application/octet-stream;base64,'+base64.b64encode(data).decode('ascii')}]
        source=work/(label+'.gltf');source.write_text(json.dumps(doc),encoding='utf-8');sources[label]=source
    invalid=json.loads(json.dumps(strip_doc))
    invalid['extensionsUsed']=['VENDOR_required'];invalid['extensionsRequired']=['VENDOR_required']
    sources['INVALID']=work/'INVALID.gltf';sources['INVALID'].write_text(json.dumps(invalid),encoding='utf-8')
    strided=json.loads(json.dumps(strip_doc));strided_bytes=strip+struct.pack('<4I',0,1,2,3)
    strided['bufferViews'].append({'buffer':0,'byteOffset':48,'byteLength':16,'byteStride':4})
    strided['accessors'].append({'bufferView':1,'componentType':5125,'count':4,'type':'SCALAR'})
    strided['meshes'][0]['primitives'][0]['indices']=1
    strided['buffers']=[{'byteLength':len(strided_bytes),'uri':'data:application/octet-stream;base64,'+base64.b64encode(strided_bytes).decode('ascii')}]
    sources['INVALID_STRIDE']=work/'INVALID_STRIDE.gltf'
    sources['INVALID_STRIDE'].write_text(json.dumps(strided),encoding='utf-8')
    missing_part=json.loads(json.dumps(strip_doc));missing_part['meshes'][0]['primitives'].append({'attributes':{}})
    sources['MISSING_PART']=work/'MISSING_PART.gltf'
    sources['MISSING_PART'].write_text(json.dumps(missing_part),encoding='utf-8')
    hashes={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in [image,*sources.values()]}
    env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(work/'profile'),LOCALAPPDATA=str(work/'local'))
    print('Evidence retained at',work,flush=True)
    results=[]
    for label in ('SPARSE','STRIP','CLIPPED','INVALID','MISSING_PART','INVALID_STRIDE'):
        before=snapshot(root);symbol='UI_FORMAT_'+label
        invalid_case=label in ('INVALID','MISSING_PART','INVALID_STRIDE')
        lines=['wait_maps','wait_ready',f'set uiscale {args.scale}',
               'assets_tab 0','frames 3','texture_select ui_format_sparse_diffuse',
               'assert_state texture_selected ui_format_sparse_diffuse',
               'assets_tab 1','frames 3','click btn_model_import',
               'frames 2','assert_widget btn_mesh_import','assert_widget input_mesh_model']
        if invalid_case:lines+=['model_select MESH_UI_FORMAT_STRIP','assert_state model_ready 1']
        lines += [f'mesh_import {sources[label].as_posix()} {symbol} {image.as_posix()}',
                  'wait_mesh_import','frames 2']
        if invalid_case:
            lines += ['assert_state mesh_import_failed 1','assert_widget mesh_import_error',
                      'assert_log '+{'INVALID':'unsupported required glTF extension: VENDOR_required',
                                     'MISSING_PART':'glTF mesh 0 primitive 1: triangle primitive is missing POSITION',
                                     'INVALID_STRIDE':'glTF mesh 0 primitive 0: triangle index buffer view must not define byteStride'}[label],
                      'assert_state model_selected MESH_UI_FORMAT_STRIP','assert_state model_ready 1']
        else:
            lines += ['assert_state mesh_import_failed 0',f'assert_log OBJECT_{symbol} ready',
                      'wait_ready','frames 3',f'model_select MESH_{symbol}','assert_state model_ready 1']
            if label=='CLIPPED':
                lines += ['assert_log model part 1: texture coordinates exceed the supported range on 4 vertices',
                          'assert_state mesh_import_note_count 1',
                          'reveal mesh_import_notes','frames 3','assert_widget mesh_import_notes',
                          'clear_toasts',f'screenshot {(work/"CLIPPED.png").as_posix()}',
                          f'mesh_import {sources["INVALID"].as_posix()} UI_FORMAT_RETRY {image.as_posix()}',
                          'wait_mesh_import','frames 2','assert_state mesh_import_failed 1',
                          'assert_state mesh_import_note_count 0','assert_widget mesh_import_error']
            else:lines += ['assert_state mesh_import_note_count 0']
            lines += ['click tool_close_model_import','frames 3',
                      'assert_state model_import_open 0','reveal model_texture_0_Diffuse','frames 3',
                      'reveal model_texture_0_Diffuse','frames 3','dump_widget model_texture_0_Diffuse',
                      'clear_toasts',f'screenshot {(work/(label+"-LINK.png")).as_posix()}',
                      'click model_texture_0_Diffuse','frames 3',
                      'assert_state assets_tab 0',f'assert_state texture_selected {symbol}_DIFFUSE',
                      f'texture_export {(work/(label+"-DIFFUSE.png")).as_posix()}']
        lines+=['quit']
        script=work/(label+'.txt');script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(root),'--size',args.size,'--auto',str(script)],
                              cwd=repo,env=env,capture_output=True,text=True,timeout=120)
        (work/(label+'-output.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-4500:]
        if invalid_case:assert snapshot(root)==before,'failed GUI import changed install files'
        else:
            info=subprocess.run([str(repo/'build/forge-tools.exe'),'mesh-info',str(bank),'MESH_'+symbol,'--json'],
                                capture_output=True,text=True,check=True,timeout=30)
            geometry=json.loads(info.stdout)
            assert (geometry['vertices'],geometry['triangles'])==((3,1) if label=='SPARSE' else (4,2)),geometry
            assert geometry['bbox_min']==[0,0,0] and geometry['bbox_max']==[100,0,100],geometry
            assert geometry['physics_index']!=0,geometry
            expected_texture={'SPARSE':2,'STRIP':3,'CLIPPED':4}[label]
            assert geometry['diffuse_of_material_0']==expected_texture,geometry
            assert geometry['texture_ids']==[expected_texture],geometry
            with Image.open(work/(label+'-DIFFUSE.png')) as exported, Image.open(image) as source:
                assert exported.size==source.size
                # DXT1 quantizes RGB to 5/6/5 bits; the old red fixture is well
                # outside this tolerance for the newly imported brown image.
                assert all(abs(a-b)<=4 for actual,expected in zip(exported.convert('RGBA').getdata(),source.convert('RGBA').getdata())
                           for a,b in zip(actual,expected)),'material link exported a stale texture'
            assert package_records(textures,by_id=True)['GBANK_MAIN_PC'][1]==old_texture
            for archive_path in (bank,textures):
                entry_markers=markers(archive_path)
                assert entry_markers[1]==0 and all(value==42 for identity,value in entry_markers.items() if identity!=1),entry_markers
            (work/(label+'-mesh.json')).write_text(info.stdout,encoding='utf-8')
        assert not list(root.glob('.forge-model-import-*'))
        assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest for path,digest in hashes.items())
        results.append(label)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':results,'source_hashes':hashes},indent=2)+'\n',encoding='utf-8')
    print(len(results),'mesh format UI workflows passed')


if __name__=='__main__':
    main()
