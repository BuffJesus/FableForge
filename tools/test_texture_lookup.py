"""Texture exact-name priority and ambiguous shortcut refusal before writes."""
from pathlib import Path
import json
import struct
import subprocess
import tempfile
from PIL import Image
from test_big_entry_markers import archive, markers
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_fmp_links import package_records


def texture_bank(path, rows, side=2):
    archive(path,b'BIGB',[('GBANK_MAIN_PC',122,
        [(identity,0,name,bytes([0,0,red,255])*(side*side)) for identity,name,red in rows])])
    data=bytearray(path.read_bytes())
    word=lambda at:struct.unpack_from('<I',data,at)[0]
    directory=word(8); fields=data.index(0,directory+4)+1
    at=word(fields+8);at+=4+8*word(at)
    info=bytearray(34)
    for offset in (0,2,6,8):struct.pack_into('<H',info,offset,side)
    struct.pack_into('<H',info,10,1);struct.pack_into('<I',info,12,1)
    info[17]=1;struct.pack_into('<I',info,20,side*side*4);info[28:]=bytes([1,32,8,8,8,8])
    for _ in rows:
        at+=28+word(at+24)
        assert word(at+4)==0
        at+=8
        assert word(at)==34
        at+=4;data[at:at+34]=info;at+=34
    path.write_bytes(data)


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='texture-lookup-',dir=repo/'build'))
    root=work/'install';write_bank(root,(1,2,3,4))
    bank=root/'data/graphics/pc/textures.big'
    source=work/'replacement.png';Image.new('RGBA',(2,2),(0,190,0,255)).save(source)
    output=work/'export.png';checks=[]
    def run(label,exe,args,accepted):
        result=subprocess.run([str(repo/'build'/exe),*map(str,args)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert (result.returncode==0)==accepted,(label,result.stdout,result.stderr)
        checks.append(label)
        return result
    print('Evidence retained at',work,flush=True)
    for label,rows,query in (
        ('label',[(1,'[dev/a/Shared.tga]',10),(2,'[dev/b/Shared.tga]',20)],'Shared'),
        ('name',[(1,'Same',10),(2,'Same',20)],'Same'),
        ('id',[(7,'First',10),(7,'Second',20)],'7'),
        ('overflow',[(0xffffffff,'Last',10)],'4294967296'),
    ):
        texture_bank(bank,rows);before=snapshot(root)
        output.write_bytes(b'previous export')
        for command,dest in (('texture-export',output),('texture-replace',source)):
            result=run(label+'-'+command,'forge.exe',[command,query,dest,'--install',root],False)
            assert ('ambiguous' if label!='overflow' else 'no texture named') in result.stderr
            assert snapshot(root)==before
            assert output.read_bytes()==b'previous export'
    # The lower-level importer must also reject duplicate exact names before
    # replacing an already-existing output archive.
    texture_bank(bank,[(1,'Same',10),(2,'Same',20)])
    previous=work/'previous.big';previous.write_bytes(b'previous bank')
    before=snapshot(root)
    refused=run('core-duplicate','forge-tools.exe',['texture','import',bank,previous,'Same',source],False)
    assert 'ambiguous texture entry name' in refused.stderr
    assert previous.read_bytes()==b'previous bank' and snapshot(root)==before
    texture_bank(bank,[(1,'[dev/Exact.tga]',10),(2,'Exact',20)])
    run('exact-export','forge.exe',['texture-export','Exact',output,'--install',root],True)
    assert Image.open(output).convert('RGBA').getpixel((0,0))==(20,0,0,255)
    old=package_records(bank,by_id=True)['GBANK_MAIN_PC'][1]
    run('exact-replace','forge.exe',['texture-replace','Exact',source,'--install',root],True)
    assert package_records(bank,by_id=True)['GBANK_MAIN_PC'][1]==old
    run('replaced-export','forge.exe',['texture-export','Exact',output,'--install',root],True)
    assert Image.open(output).convert('RGBA').getpixel((0,0))==(0,190,0,255)
    texture_bank(bank,[(7,'CaseTexture',10)])
    original=package_records(bank,by_id=True)['GBANK_MAIN_PC'][7]
    added=run('case-add','forge.exe',['texture-add','casetexture',source,'--format','argb8888','--install',root],True)
    entries=package_records(bank,by_id=True)['GBANK_MAIN_PC']
    assert entries[7]==original and 8 in entries,entries.keys()
    assert markers(bank)=={7:0,8:42},markers(bank)
    assert 'new texture id 8' in added.stdout,added.stdout
    run('case-export','forge.exe',['texture-export','casetexture',output,'--install',root],True)
    assert Image.open(output).convert('RGBA').getpixel((0,0))==(0,190,0,255)
    for query,identity in (('CaseTexture',7),('casetexture',8)):
        verified=run('verify-'+query,'forge-tools.exe',['texture','verify',bank,query],True)
        assert 'id='+str(identity) in verified.stdout,verified.stdout
    before=snapshot(root)
    ambiguous=run('verify-case-ambiguous','forge-tools.exe',['texture','verify',bank,'CASETEXTURE'],False)
    assert 'ambiguous texture entry name' in ambiguous.stdout,ambiguous.stdout
    assert snapshot(root)==before
    texture_bank(bank,[(7,'CaseTexture',10)])
    verified=run('verify-case-fallback','forge-tools.exe',['texture','verify',bank,'CASETEXTURE'],True)
    assert 'id=7' in verified.stdout,verified.stdout
    texture_bank(bank,[(7,'Same',10),(8,'Same',20)])
    before=snapshot(root)
    ambiguous=run('verify-exact-ambiguous','forge-tools.exe',['texture','verify',bank,'Same'],False)
    assert 'ambiguous texture entry name' in ambiguous.stdout,ambiguous.stdout
    assert snapshot(root)==before

    (work/'result.json').write_text(json.dumps({'checks':checks,'passed':True},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'texture lookup checks passed')


if __name__=='__main__':
    main()
