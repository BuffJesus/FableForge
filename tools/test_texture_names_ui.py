"""Hidden texture selection: exact names, ambiguous shortcuts and numeric IDs."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
from PIL import Image
from test_mod_field_details import write_bank
from test_texture_lookup import texture_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='texture-names-ui-',dir=repo/'build'))
    root=work/'install';write_bank(root,(1,2,3,4))
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
    bank=root/'data/graphics/pc/textures.big'
    texture_bank(bank,[(1,'[dev/Exact.tga]',10),(2,'Exact',20),
        (3,'[dev/a/Shared.tga]',30),(4,'[dev/b/Shared.tga]',40),
        (7,'IdSevenA',50),(7,'IdSevenB',60),(0xffffffff,'Maximum',70)])
    before=hashlib.sha256(bank.read_bytes()).hexdigest()
    env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(work/'profile'))
    (work/'profile').mkdir()
    def run(label,lines,failures):
        script=work/(label+'.txt');script.write_text('\n'.join(['wait_maps','wait_ready','assets_tab 0','frames 3',*lines,'quit'])+'\n',encoding='utf-8')
        result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(root),'--auto',str(script)],
                              cwd=repo,env=env,capture_output=True,text=True,timeout=120)
        (work/(label+'-output.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        actual=[line for line in trace.splitlines() if 'FAIL ' in line]
        assert len(actual)==failures,(actual,trace[-2500:])
        assert (result.returncode==0)==(failures==0),trace[-2500:]
        assert ('RESULT FAIL' if failures else 'RESULT PASS') in trace
        assert hashlib.sha256(bank.read_bytes()).hexdigest()==before
        return trace
    print('Evidence retained at',work,flush=True)
    exact=work/'exact.png'
    run('exact',['texture_select Exact','texture_export '+exact.as_posix()],0)
    assert Image.open(exact).convert('RGBA').getpixel((0,0))==(20,0,0,255)
    preserved=work/'preserved.png'
    trace=run('ambiguous',['texture_select Exact','texture_select Shared','texture_select 7',
        'assert_log textures: ambiguous texture name, label or ID: Shared',
        'assert_log textures: ambiguous texture name, label or ID: 7',
        'texture_export '+preserved.as_posix()],2)
    assert all('texture_select:' in line for line in trace.splitlines() if 'FAIL ' in line)
    assert Image.open(preserved).convert('RGBA').getpixel((0,0))==(20,0,0,255)
    run('maximum',['texture_select 4294967295','assert_state texture_selected Maximum'],0)
    (work/'result.json').write_text(json.dumps({'passed':True,'bank_sha256':before,'checks':3},indent=2)+'\n',encoding='utf-8')
    print('Texture UI name/ID selection passed')


if __name__=='__main__':
    main()
