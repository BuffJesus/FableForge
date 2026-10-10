"""Explicit texture import options must be complete and recognized before writes."""
from pathlib import Path
import json
import subprocess
import tempfile
from PIL import Image
from test_texture_lookup import texture_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='texture-import-options-',dir=repo/'build'))
    bank=work/'source.big';texture_bank(bank,[(7,'Original',10)])
    image=work/'source.png';Image.new('RGBA',(2,2),(0,190,0,255)).save(image)
    before=bank.read_bytes();image_before=image.read_bytes()
    print('Evidence retained at',work,flush=True)
    cases=[('unknown-format',['--format','not-a-format'])]
    for option in ('--dims','--format','--bank','--tools','--python'):
        for suffix,values in (('missing',[]),('empty',['']),('next-option',['--add'])):
            cases.append((option[2:]+'-'+suffix,[option,*values]))
    checks=[]
    for label,options in cases:
        output=work/(label+'.big');output.write_bytes(b'previous output')
        result=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','import',str(bank),str(output),
                               'Original',str(image),*options],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0,(label,result.stdout,result.stderr)
        expected='unknown texture format' if label=='unknown-format' else 'needs a value'
        assert expected in result.stderr,(label,result.stdout,result.stderr)
        assert output.read_bytes()==b'previous output' and bank.read_bytes()==before,label
        assert image.read_bytes()==image_before,label
        checks.append(label)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'texture import option checks passed')


if __name__=='__main__':
    main()
