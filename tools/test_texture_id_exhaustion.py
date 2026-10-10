"""Texture append refuses ID overflow before changing an install or output."""
from pathlib import Path
import json
import subprocess
import tempfile
from PIL import Image
from test_texture_lookup import texture_bank
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_fmp_links import package_records


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='texture-id-exhaustion-',dir=repo/'build'))
    root=work/'install';write_bank(root,(1,2,3,4))
    bank=root/'data/graphics/pc/textures.big'
    image=work/'new.png';Image.new('RGBA',(2,2),(12,34,56,255)).save(image)
    output=work/'output.big';checks=[]
    print('Evidence retained at',work,flush=True)
    for route in ('core','install'):
        for maximum in (0xffffffff,0xfffffffe):
            texture_bank(bank,[(maximum,'Donor',20)])
            original=snapshot(root)
            output.write_bytes(b'previous output')
            if route=='core':
                args=['forge-tools.exe','texture','import',bank,output,'NewTexture',image,'--add','--format','argb8888']
            else:
                args=['forge.exe','texture-add','NewTexture',image,'--install',root,'--format','argb8888']
            result=subprocess.run([str(repo/'build'/args[0]),*map(str,args[1:])],capture_output=True,text=True,timeout=30)
            label=route+'-'+str(maximum)
            (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
            if maximum==0xffffffff:
                assert result.returncode!=0,(label,result.stdout,result.stderr)
                assert 'no free texture entry ID' in result.stdout+result.stderr
                assert snapshot(root)==original and output.read_bytes()==b'previous output'
            else:
                assert result.returncode==0,(label,result.stdout,result.stderr)
                records=package_records(output if route=='core' else bank,by_id=True)['GBANK_MAIN_PC']
                assert set(records)=={maximum,maximum+1}
                named=package_records(output if route=='core' else bank)['GBANK_MAIN_PC']
                assert named['NewTexture'][0]==records[maximum+1][0]
                if route=='core':assert snapshot(root)==original
            checks.append(label)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'texture ID boundary checks passed')


if __name__=='__main__':
    main()
