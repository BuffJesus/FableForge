"""Cutout replacement preserves alpha pixels, native mode, identity and Restore."""
from pathlib import Path
import argparse
import json
import os
import struct
import subprocess
import tempfile
from PIL import Image
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot
from test_texture_dimensions import texture_info
from test_fmp_links import package_records


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--ui',action='store_true')
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='texture-cutout-',dir=repo/'build'))
    print('Evidence retained at',work,flush=True)
    image=work/'replacement.png';pixels=Image.new('RGBA',(4,4))
    alphas=[0,127,128,255]*4
    pixels.putdata([(0,255,0,a) for a in alphas]);pixels.save(image)
    checks=[]
    for fmt,mode in ((31,2),(32,4)):
        root=work/str(mode);write_bank(root,(1,2,3,4))
        bank=root/'data/graphics/pc/textures.big'
        payload=struct.pack('<HHI',0,65535,0xffffffff) if fmt==31 else bytes([0x88])*8+struct.pack('<HHI',65535,0,0)
        archive(bank,b'BIGB',[('GBANK_MAIN_PC',122,[(7,0,'Original',payload)])])
        raw=bytearray(bank.read_bytes());word=lambda at:struct.unpack_from('<I',raw,at)[0]
        directory=word(8);fields=raw.index(0,directory+4)+1
        record=word(fields+8);record+=4+8*word(record)
        at=record+28+word(record+24)+8;assert word(at)==34;at+=4
        info=bytearray(34);struct.pack_into('<6H',info,0,4,4,0,4,4,1)
        struct.pack_into('<I',info,12,fmt);info[16]=mode;info[17]=1
        struct.pack_into('<I',info,20,len(payload));info[28:]=bytes([3,4,0,0,0,0] if fmt==31 else [2,8,0,0,0,0])
        raw[at:at+34]=info;bank.write_bytes(raw)
        levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        before=snapshot(root)
        verified=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','verify',str(bank),'Original'],capture_output=True,text=True,timeout=30)
        assert verified.returncode==0,(verified.stdout,verified.stderr)
        output=work/(str(mode)+'-conversion.big');output.write_bytes(b'previous output')
        converted=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','import',str(bank),str(output),
                                  'Original',str(image),'--format','argb8888'],capture_output=True,text=True,timeout=30)
        assert converted.returncode!=0 and 'cutout texture format conversion' in converted.stderr,converted.stderr
        assert output.read_bytes()==b'previous output' and snapshot(root)==before
        def run(label,args):
            result=subprocess.run([str(repo/'build/forge.exe'),*map(str,args),'--install',str(root)],capture_output=True,text=True,timeout=30)
            (work/(str(mode)+'-'+label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
            assert result.returncode==0,(mode,label,result.stdout,result.stderr)
        if args.ui:
            from test_model_import_form import fill
            lines=['wait_maps','wait_ready','assets_tab 0','frames 3','texture_select Original','frames 3',
                   'reveal btn_tex_replace_toggle','frames 3','reveal btn_tex_replace_toggle','frames 3',
                   'click btn_tex_replace_toggle','frames 3','reveal input_tex_image','frames 3']
            lines+=fill('input_tex_image',image)
            lines+=['reveal btn_tex_replace','frames 3','reveal btn_tex_replace','frames 3','click btn_tex_replace',
                    'frames 3','wait_ready','assert_state texture_replace_failed 0','assert_state texture_selected Original','quit']
            script=work/(str(mode)+'-ui.txt');script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(work/'profile'),LOCALAPPDATA=str(work/'local'))
            ui=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(root),'--auto',str(script)],
                              cwd=repo,env=env,capture_output=True,text=True,timeout=120)
            trace=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert ui.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-3500:]
        else:
            run('replace',['texture-replace','Original',image])
        assert list(package_records(bank,by_id=True)['GBANK_MAIN_PC'])==[7]
        updated=texture_info(bank)
        assert updated[:17]==info[:17],(updated.hex(),info.hex())
        exported=work/(str(mode)+'.png');run('export',['texture-export','Original',exported])
        with Image.open(exported) as result:
            assert result.size==(4,4)
            decoded=list(result.convert('RGBA').getchannel('A').getdata())
            if fmt==31:assert decoded==[0 if a<128 else 255 for a in alphas],decoded
            else:assert all(abs(a-b)<=8 for a,b in zip(alphas,decoded)),decoded
        run('restore',['restore','--forget']);assert snapshot(root)==before
        checks.append({'format':fmt,'transparency':mode,'restore_exact':True})
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks,'ui':args.ui},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'cutout replacement checks passed')


if __name__=='__main__':
    main()
