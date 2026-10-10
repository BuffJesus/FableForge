"""Texture import refuses invalid dimensions before replacing an output bank."""
from pathlib import Path
import hashlib
import json
import subprocess
import shutil
import struct
import tempfile
from PIL import Image
from test_texture_lookup import texture_bank
from test_fmp_links import package_records
from test_mod_field_details import write_bank



def texture_info_offset(raw):
    u32=lambda at:struct.unpack_from('<I',raw,at)[0]
    directory=u32(8);fields=raw.index(0,directory+4)+1
    entry=u32(fields+8);entry+=4+8*u32(entry)
    header=entry+28+u32(entry+24)+8
    assert u32(header)==34
    return header+4


def texture_info(path):
    raw=path.read_bytes();at=texture_info_offset(raw)
    return raw[at:at+34]


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='texture-dimensions-',dir=repo/'build'))
    bank=work/'source.big';texture_bank(bank,[(7,'Original',10)])
    source=work/'source.png';Image.new('RGBA',(2,2),(0,190,0,255)).save(source)
    wide=work/'wide.png';Image.new('RGBA',(65536,1),(0,190,0,255)).save(wide)
    rounded=work/'rounded.png';Image.new('RGBA',(32769,1),(0,190,0,255)).save(rounded)
    larger=work/'larger.png';Image.new('RGBA',(4,4),(0,190,0,255)).save(larger)
    smaller=work/'smaller.png';Image.new('RGBA',(1,1),(0,190,0,255)).save(smaller)
    hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (bank,source,wide,rounded,larger,smaller)}
    original=package_records(bank,by_id=True)['GBANK_MAIN_PC'][7]
    print('Evidence retained at',work,flush=True)
    checks=[]
    install=work/'decode-install';write_bank(install,(1,2,3,4))
    decode_bank=install/'data/graphics/pc/textures.big';decode_bank.parent.mkdir(parents=True,exist_ok=True)
    cases=[('invalid-'+str(i),source,['--add','--dims',dims],False) for i,dims in enumerate(
        ('2x2junk','2x2x2','2','x2','2x','0x2','-2x2','+2x2',' 2x2','2x2 ',
         '65536x2','2x65536','999999999999999999999x2','3x2','65535x2','1x1'))]
    cases += [('source-overflow',wide,['--add'],False),
              ('rounded-overflow',rounded,['--add'],False),
              ('valid-small',source,['--add','--dims','2x2'],True),
              ('valid-large-width',source,['--add','--dims','32768x2'],True),
              ('valid-replace',source,[],True),
              ('replace-larger',larger,[],True),
              ('replace-smaller',smaller,[],True),
              ('replace-wide',wide,[],True)]
    for label,image,options,accepted in cases:
        out=work/(label+'.big');out.write_bytes(b'previous output')
        name='New' if '--add' in options else 'Original'
        result=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','import',str(bank),str(out),
                               name,str(image),'--format','argb8888',*options],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert (result.returncode==0)==accepted,(label,result.stdout,result.stderr)
        if accepted:
            records=package_records(out,by_id=True)['GBANK_MAIN_PC']
            if '--add' in options:assert records[7]==original and 8 in records
            else:
                assert list(records)==[7]
                # Replacing opaque ARGB must not enable the engine's alpha path.
                assert texture_info(out)[16]==0,(label,'transparency mode changed')
            exported=work/(label+'.png')
            shutil.copyfile(out,decode_bank)
            decoded=subprocess.run([str(repo/'build/forge.exe'),'texture-export',name,str(exported),'--install',str(install)],
                                   capture_output=True,text=True,timeout=30)
            assert decoded.returncode==0,(decoded.stdout,decoded.stderr)
            with Image.open(exported) as pixels:
                assert pixels.size==((32768,2) if label=='valid-large-width' else (2,2)),pixels.size
                assert pixels.convert('RGBA').getpixel((0,0))==(0,190,0,255)
        else:
            assert out.read_bytes()==b'previous output',label
            assert 'dimension' in result.stdout+result.stderr or 'bad --dims' in result.stderr,(label,result.stdout,result.stderr)
        assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==digest for p,digest in hashes.items())
        checks.append(label)
    # Mutate only the original slot's header, leaving the bank and pixel data
    # otherwise intact. A zero allocation must never reach the resampler.
    original_bytes=bank.read_bytes()
    word=lambda offset:struct.unpack_from('<I',original_bytes,offset)[0]
    directory=word(8);fields=original_bytes.index(0,directory+4)+1
    entry=word(fields+8);entry+=4+8*word(entry)
    metadata=entry+28+word(entry+24)+8
    assert word(metadata)==34
    metadata+=4
    for label,offset,value in [('zero-width',0,0),('zero-height',2,0),
                               ('zero-frame-width',6,0),('zero-frame-height',8,0),
                               ('wide-frame',6,3),('tall-frame',8,3)]:
        malformed=bytearray(original_bytes);struct.pack_into('<H',malformed,metadata+offset,value)
        damaged=work/(label+'-source.big');damaged.write_bytes(malformed)
        output=work/(label+'.big');output.write_bytes(b'previous output')
        result=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','import',str(damaged),str(output),
                               'Original',str(source)],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'invalid texture slot dimensions' in result.stderr,(label,result.stdout,result.stderr)
        assert output.read_bytes()==b'previous output' and damaged.read_bytes()==malformed,label
        checks.append(label)
    mode_bank=work/'conversion-source.big';texture_bank(mode_bank,[(7,'Original',10)],side=4)
    mode_bytes=mode_bank.read_bytes();mode_metadata=texture_info_offset(mode_bytes)
    for mode in (0,1):
        for label,options,expected_format,expected_mode in (
            ('implicit',[],1,mode),('same-format',['--format','argb8888'],1,mode),
            ('conversion',['--format','dxt3'],32,1)):
            label=f'transparency-{mode}-{label}'
            data=bytearray(mode_bytes);data[mode_metadata+16]=mode
            original_slot=work/(label+'-source.big');original_slot.write_bytes(data)
            output=work/(label+'.big')
            result=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','import',str(original_slot),
                                   str(output),'Original',str(source),*options],capture_output=True,text=True,timeout=30)
            (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
            assert result.returncode==0,(label,result.stdout,result.stderr)
            info=texture_info(output)
            assert info[16]==expected_mode and struct.unpack_from('<I',info,12)[0]==expected_format,label
            assert list(package_records(output,by_id=True)['GBANK_MAIN_PC'])==[7]
            assert original_slot.read_bytes()==data,label
            checks.append(label)
    # Exercise the actual DXT3 import/export path with every alpha value.
    gradient=work/'alpha-gradient.png'
    pixels=Image.new('RGBA',(4,64));pixels.putdata([(0,190,0,a) for a in range(256)]);pixels.save(gradient)
    gradient_bytes=gradient.read_bytes();output=work/'alpha-gradient.big'
    encoded=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','import',str(bank),str(output),
                            'AlphaGradient',str(gradient),'--add','--format','dxt3'],capture_output=True,text=True,timeout=30)
    assert encoded.returncode==0,(encoded.stdout,encoded.stderr)
    shutil.copyfile(output,decode_bank);exported=work/'alpha-gradient-decoded.png'
    decoded=subprocess.run([str(repo/'build/forge.exe'),'texture-export','AlphaGradient',str(exported),
                            '--install',str(install)],capture_output=True,text=True,timeout=30)
    assert decoded.returncode==0,(decoded.stdout,decoded.stderr)
    with Image.open(exported) as pixels:
        assert pixels.size==(4,64)
        alphas=list(pixels.convert('RGBA').getchannel('A').getdata())
        assert all(abs(actual-expected)<=8 for expected,actual in enumerate(alphas)),alphas
        assert alphas[0]==0 and alphas[-1]==255
    assert gradient.read_bytes()==gradient_bytes
    assert package_records(output,by_id=True)['GBANK_MAIN_PC'][7]==original
    checks.append('dxt3-alpha-gradient')
    for fmt in ('dxt1','dxt3'):
        for label,options,accepted in (
            ('auto',['--add'],True),('explicit',['--add','--dims','4x4'],True),
            ('too-small',['--add','--dims','2x2'],False),('small-slot-conversion',[],False)):
            label=f'compressed-{fmt}-{label}';output=work/(label+'.big');output.write_bytes(b'previous output')
            name='New' if '--add' in options else 'Original'
            encoded=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','import',str(bank),str(output),
                                    name,str(source),'--format',fmt,*options],capture_output=True,text=True,timeout=30)
            (work/(label+'.log')).write_text(encoded.stdout+encoded.stderr,encoding='utf-8')
            assert (encoded.returncode==0)==accepted,(label,encoded.stdout,encoded.stderr)
            if accepted:
                shutil.copyfile(output,decode_bank);exported=work/(label+'.png')
                decoded=subprocess.run([str(repo/'build/forge.exe'),'texture-export',name,str(exported),
                                        '--install',str(install)],capture_output=True,text=True,timeout=30)
                assert decoded.returncode==0,(decoded.stdout,decoded.stderr)
                with Image.open(exported) as pixels:
                    assert pixels.size==(4,4),pixels.size
                    assert abs(pixels.convert('RGBA').getpixel((0,0))[1]-190)<=8
                assert package_records(output,by_id=True)['GBANK_MAIN_PC'][7]==original
            else:
                assert 'dimensions must be at least 4x4' in encoded.stderr,(label,encoded.stderr)
                assert output.read_bytes()==b'previous output'
            checks.append(label)
    assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==digest for p,digest in hashes.items())
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks,'source_hashes':hashes},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'texture dimension checks passed')


if __name__=='__main__':
    main()
