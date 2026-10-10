#!/usr/bin/env python3
"""Exercise the Models replacement controls against a fresh tiny install."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import struct
import subprocess
import tempfile
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_meshimport import write_glb
from test_mesh_replace import read_archive,signature
from test_texture_lookup import texture_bank


def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    args=parser.parse_args();gui=args.gui.resolve();forge=gui.with_name('forge.exe')
    work=Path(tempfile.mkdtemp(prefix='mesh-replace-ui-',dir=repo/'build'));root=work/'install'
    print('Evidence retained at',work,flush=True)
    schema=json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'));fields=schema['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if field['name']=='Graphic' else 0 for field in fields]
    write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[field['name'] for field in fields])
    archive(root/'data/graphics/graphics.big',b'BIGB',[('MBANK_ALLMESHES',205,[(1,1,'Donor',b'opaque donor')])])
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
    seed=work/'cube.glb';write_glb(str(seed))
    subprocess.run([str(forge),'mesh-import',str(seed),'UI_REPLACE','--texture-id','1','--install',str(root)],
                   capture_output=True,text=True,check=True,timeout=30)
    valid=work/'replacement.obj';valid.write_text('v 0 0 0\nv 2 0 0\nv 0 2 0\nf 1 2 3\n',encoding='utf-8')
    invalid=work/'two-slots.obj';invalid.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl A\nf 1 2 3\nusemtl B\nf 1 2 3\n',encoding='utf-8')
    graphics=root/'data/graphics/graphics.big';initial=read_archive(graphics)
    original={path:hashlib.sha256(path.read_bytes()).hexdigest() for path in root.rglob('*') if path.is_file()}
    env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(work/'profile'),LOCALAPPDATA=str(work/'local'))
    for refused,source in [(True,invalid),(False,valid)]:
        label='refusal' if refused else 'success'
        lines=['wait_maps','wait_ready','assets_tab 1','frames 3','model_select MESH_UI_REPLACE',
               'assert_state model_ready 1','assert_state model_vertices 24','assert_state model_triangles 12',
               'reveal btn_model_replace','frames 2','click btn_model_replace','frames 3',
               'assert_widget input_mesh_replace_model','click input_mesh_replace_model','input_text '+source.as_posix(),
               'frames 3','assert_widget btn_mesh_replace','click btn_mesh_replace','wait_mesh_replace','frames 3']
        if refused:
            lines+=['assert_state mesh_replace_failed 1','assert_widget mesh_replace_error',
                    'assert_log replacement material slot count must match','assert_state model_selected MESH_UI_REPLACE',
                    'assert_state model_ready 1','assert_state model_vertices 24','assert_state model_triangles 12']
        else:
            lines+=['wait_ready','assert_state mesh_replace_failed 0','assert_widget mesh_replace_success',
                    'assert_state model_selected MESH_UI_REPLACE','assert_state model_ready 1',
                    'assert_state model_vertices 3','assert_state model_triangles 1']
        lines+=['quit'];script=work/(label+'.txt');script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        result=subprocess.run([str(gui),'--install',str(root),'--size','1440x900','--auto',str(script)],
                              cwd=repo,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (work/(label+'-output.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-4500:]
        for path,digest in original.items():
            if refused or path!=graphics: assert hashlib.sha256(path.read_bytes()).hexdigest()==digest,path
        assert not list(root.glob('.forge-mesh-replace-*'))
    after=read_archive(graphics)
    for before_bank,after_bank in zip(initial,after):
        for before,entry in zip(before_bank['records'],after_bank['records']):
            if before['name']!=b'MESH_UI_REPLACE':
                assert signature([{**before_bank,'records':[before]}])==signature([{**after_bank,'records':[entry]}])
    assert all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in original.items() if path!=graphics)
    staged=work/'staged';quad=work/'quad.obj'
    quad.write_text('v 0 0 0\nv 2 0 0\nv 2 2 0\nv 0 2 0\nf 1 2 3 4\n',encoding='utf-8')
    base_hashes={path:hashlib.sha256(path.read_bytes()).hexdigest() for path in root.rglob('*') if path.is_file()}
    lines=['wait_maps','wait_ready','assets_tab 1','frames 3','model_select MESH_UI_REPLACE',
           'assert_state model_vertices 3','assert_state context_ready 1',f'set saveroot {staged.as_posix()}',
           'reveal btn_model_replace','frames 2','click btn_model_replace','frames 3',
           'click input_mesh_replace_model','input_text '+quad.as_posix(),'frames 3',
           'click btn_mesh_replace','wait_mesh_replace','frames 3','assert_state context_ready 1',
           'assert_state mesh_replace_failed 0','assert_widget mesh_replace_success',
           'assert_state model_selected MESH_UI_REPLACE','assert_state model_ready 1',
           'assert_state model_vertices 4','assert_state model_triangles 2',
           'assert_state mesh_replace_succeeded 1','set saveroot','frames 3',
           'assert_state mesh_replace_succeeded 0','assert_state model_selected MESH_UI_REPLACE',
           'assert_state model_vertices 3','assert_state model_triangles 1',
           f'set saveroot {staged.as_posix()}','frames 3','assert_state model_vertices 4',
           'assert_state model_triangles 2',f'set saveroot {(work/"empty-output").as_posix()}',
           'frames 3','assert_state model_vertices 3','assert_state model_triangles 1','quit']
    script=work/'staged.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    result=subprocess.run([str(gui),'--install',str(root),'--size','1440x900','--auto',str(script)],
                          cwd=repo,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
    (work/'staged-output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    trace=Path(str(script)+'.log').read_text(encoding='utf-8')
    assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-4500:]
    assert all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in base_hashes.items())
    assert [path.relative_to(staged).as_posix() for path in staged.rglob('*') if path.is_file()]==['data/graphics/graphics.big']
    staged_banks=read_archive(staged/'data/graphics/graphics.big')
    for before_bank,after_bank in zip(after,staged_banks):
        for before,entry in zip(before_bank['records'],after_bank['records']):
            if before['name']!=b'MESH_UI_REPLACE':
                assert signature([{**before_bank,'records':[before]}])==signature([{**after_bank,'records':[entry]}])
    helper_source=work/'hold.cpp'
    helper_source.write_text('#include <windows.h>\n#include <cstdio>\nint main(){puts("ready");fflush(stdout);Sleep(60000);}\n',encoding='utf-8')
    compiler=re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$',
                      (repo/'build/CMakeCache.txt').read_text(encoding='utf-8'),re.MULTILINE).group(1)
    helper=staged/'Fable.exe'
    subprocess.run([compiler,str(helper_source),'-o',str(helper)],capture_output=True,check=True,timeout=60)
    running_hashes={path:hashlib.sha256(path.read_bytes()).hexdigest() for path in staged.rglob('*') if path.is_file()}
    process=subprocess.Popen([str(helper)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        assert process.stdout.readline().strip()=='ready'
        script=work/'running-destination.txt'
        lines=['wait_maps','wait_ready',f'set saveroot {staged.as_posix()}','assets_tab 1','frames 3',
               'model_select MESH_UI_REPLACE','reveal btn_model_replace','frames 2',
               'click btn_model_replace','frames 3','click input_mesh_replace_model',
               'input_text '+valid.as_posix(),'frames 3','click btn_mesh_replace','wait_mesh_replace',
               'frames 3','assert_state mesh_replace_failed 1','assert_widget mesh_replace_error',
               'assert_log Fable is running',f'mesh_import {valid.as_posix()} GUARDED_DESTINATION',
               'wait_mesh_import','assert_state mesh_import_failed 1',
               'assert_log Fable is running from this install','quit']
        script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        result=subprocess.run([str(gui),'--install',str(root),'--size','1440x900','--auto',str(script)],
                              cwd=repo,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (work/'running-destination-output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-4500:]
        assert all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in running_hashes.items())
        assert all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in base_hashes.items())
        assert not list(staged.glob('.forge-mesh-replace-*'))
    finally:
        process.terminate();process.communicate(timeout=10)
    (work/'result.json').write_text(json.dumps({'passed':True,'workflows':['refusal','success','staged','running-destination']},indent=2)+'\n',encoding='utf-8')
    print('Static mesh replacement UI: four workflows passed; previews refreshed, running destination refused and original files preserved.')


if __name__=='__main__':
    main()
