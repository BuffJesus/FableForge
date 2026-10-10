"""Model selection refuses ambiguous names/IDs without discarding the current preview."""
from pathlib import Path
import hashlib
import json
import os
import struct
import subprocess
import tempfile
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_meshimport import write_obj
from test_fmp_links import package_records
from test_texture_lookup import texture_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='model-names-ui-',dir=repo/'build'))
    root=work/'install';model=work/'cube.obj';write_obj(model)
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text())['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if f['name']=='Graphic' else 0 for f in fields]
    write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[f['name'] for f in fields])
    bank=root/'data/graphics/graphics.big'
    archive(bank,b'BIGB',[('MBANK_ALLMESHES',205,[(1,1,'Donor',b'opaque donor')])])
    textures=root/'data/graphics/pc/textures.big'
    texture_bank(textures,[(1,'Texture',20),(2,'1',40),(0xffffffff,'HighTexture',60),(3,'4294967295',80)])
    result=subprocess.run([str(repo/'build/forge.exe'),'mesh-import',str(model),'FIXTURE','--no-collision','--texture-id','1','--install',str(root)],
                          capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+result.stderr
    payload=package_records(bank,by_id=True)['MBANK_ALLMESHES'][2][0]
    result=subprocess.run([str(repo/'build/forge.exe'),'mesh-import',str(model),'HIGH_TEXTURE','--no-collision',
                           '--texture-id','4294967295','--install',str(root)],capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+result.stderr
    high_payload=package_records(bank,by_id=True)['MBANK_ALLMESHES'][3][0]
    rows=[(7,'0Other'),(8,'7'),(9,'Stable'),(10,'DupA'),(10,'DupB'),(11,'Same'),(12,'Same'),(0xffffffff,'Maximum'),(13,'OneRender')]
    archive(bank,b'BIGB',[('MBANK_ALLMESHES',205,[(identity,1,name,payload) for identity,name in rows]+
                        [(13,3,'HiddenPhysics',b'opaque'),(14,1,'HighTextureMesh',high_payload)])])
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
    before=hashlib.sha256(bank.read_bytes()).hexdigest()
    env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(work/'profile'))
    (work/'profile').mkdir()
    def run(label,lines,failures):
        textures_before=hashlib.sha256(textures.read_bytes()).hexdigest()
        script=work/(label+'.txt');script.write_text('\n'.join(['wait_maps','wait_ready','assets_tab 1','frames 3',*lines,'quit'])+'\n',encoding='utf-8')
        result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(root),'--auto',str(script)],
                              cwd=repo,env=env,capture_output=True,text=True,timeout=120)
        (work/(label+'-output.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        actual=[line for line in trace.splitlines() if 'FAIL ' in line]
        assert len(actual)==failures,(actual,trace[-3000:])
        assert (result.returncode==0)==(failures==0),trace[-3000:]
        assert all('model_select:' in line for line in actual),actual
        assert hashlib.sha256(bank.read_bytes()).hexdigest()==before
        assert hashlib.sha256(textures.read_bytes()).hexdigest()==textures_before
    print('Evidence retained at',work,flush=True)
    run('exact',['model_select 7','assert_state model_selected 7','assert_state model_ready 1',
                 'frames 3','reveal model_row_7','frames 3','click model_row_7',
                 'assert_state model_selected 0Other','assert_state model_ready 1',
                 'model_select 4294967295','assert_state model_selected Maximum','assert_state model_ready 1'],0)
    run('ambiguous',['model_select Stable','model_select 10','model_select DupA','model_select Same','model_select OneRender',
                     'assert_state model_selected Stable','assert_state model_ready 1'],4)
    run('material-link',['model_select Stable','frames 3','reveal model_texture_0_Diffuse',
                         'frames 3','click model_texture_0_Diffuse','frames 3',
                         'assert_state assets_tab 0','assert_state texture_selected Texture'],0)
    run('high-material-link',['model_select HighTextureMesh','assert_state model_ready 1',
                              'frames 3','reveal model_texture_0_Diffuse','frames 3','click model_texture_0_Diffuse',
                              'frames 3','assert_state assets_tab 0','assert_state texture_selected HighTexture'],0)
    texture_bank(textures,[(1,'Texture',20),(1,'Duplicate',40),(2,'1',60)])
    run('ambiguous-material',['model_select Stable','assert_state model_ready 1',
                              'assert_log texture id 1 is ambiguous in GBANK_MAIN_PC',
                              'frames 3','reveal model_texture_0_Diffuse','frames 3',
                              'click model_texture_0_Diffuse','frames 3','assert_state assets_tab 1'],0)
    (work/'result.json').write_text(json.dumps({'passed':True,'bank_sha256':before,'checks':5},indent=2)+'\n',encoding='utf-8')
    print('Model UI name/ID selection passed')


if __name__=='__main__':
    main()
