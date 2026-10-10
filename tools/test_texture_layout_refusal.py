"""Replacement must preserve unsupported texture layouts and transparency modes."""
from pathlib import Path
import argparse
import json
import os
import shutil
import struct
import subprocess
import tempfile
from PIL import Image
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_mod_bank_refusal import snapshot


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ui',action='store_true')
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='texture-layout-refusal-',dir=repo/'build'))
    image=work/'replacement.png';Image.new('RGBA',(2,2),(0,190,0,255)).save(image)
    print('Evidence retained at',work,flush=True)
    checks=[]
    for label,frames,depth,frame_width,byte_count in (
        ('atlas',2,0,1,16),('array',2,0,2,32),('volume',1,2,2,32),
        ('volume-one-slice',1,1,2,16),('volume-type-only',1,0,2,16),('depth-only',1,1,2,16),
        ('unsupported-dxt1-alpha',1,0,2,8),('unsupported-dxt3-alpha',1,0,2,16)):
        special=label in ('unsupported-dxt1-alpha','unsupported-dxt3-alpha')
        diagnostic='cannot preserve transparency mode' if special else 'single-frame 2D textures'
        source=work/(label+'.big');output=work/(label+'-out.big')
        archive(source,b'BIGB',[('GBANK_MAIN_PC',122,[(7,4 if label.startswith('volume') else 0,'Original',bytes([0,0,20,255])*(byte_count//4))])])
        data=bytearray(source.read_bytes());word=lambda at:struct.unpack_from('<I',data,at)[0]
        directory=word(8);fields=data.index(0,directory+4)+1
        entry=word(fields+8);entry+=4+8*word(entry)
        metadata=entry+28+word(entry+24)+8;assert word(metadata)==34;metadata+=4
        info=bytearray(34)
        struct.pack_into('<6H',info,0,2,2,depth,frame_width,2,frames)
        struct.pack_into('<I',info,12,1);info[17]=1
        struct.pack_into('<I',info,20,16*max(depth,1));info[28:]=bytes([1,32,8,8,8,8])
        if special:
            fmt,mode,tail=(31,1,bytes([3,4,0,0,0,0])) if label=='unsupported-dxt1-alpha' else (32,3,bytes([2,8,0,0,0,0]))
            struct.pack_into('<I',info,12,fmt);info[16]=mode
            struct.pack_into('<I',info,20,byte_count);info[28:]=tail
            offset=word(entry+16)
            # All-transparent BC1 block, or explicit alpha plus opaque BC2 colour.
            pixels=struct.pack('<HHI',0,65535,0xffffffff) if label=='unsupported-dxt1-alpha' else bytes([0x88])*8+struct.pack('<HHI',65535,0,0)
            data[offset:offset+byte_count]=pixels
        data[metadata:metadata+34]=info;source.write_bytes(data)
        verified=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','verify',str(source),'Original'],
                                capture_output=True,text=True,timeout=30)
        assert verified.returncode==0,(label,verified.stdout,verified.stderr)
        output.write_bytes(b'previous output')
        result=subprocess.run([str(repo/'build/forge-tools.exe'),'texture','import',str(source),str(output),
                               'Original',str(image),*([] if special else ['--format','argb8888'])],capture_output=True,text=True,timeout=30)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0,(label,result.stdout,result.stderr)
        assert diagnostic in result.stderr,(label,result.stderr)
        assert source.read_bytes()==data and output.read_bytes()==b'previous output',label
        root=work/(label+'-install');write_bank(root,(1,2,3,4))
        bank=root/'data/graphics/pc/textures.big';bank.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,bank)
        levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        before=snapshot(root)
        refused=subprocess.run([str(repo/'build/forge.exe'),'texture-replace','Original',str(image),'--install',str(root)],
                               capture_output=True,text=True,timeout=30)
        assert refused.returncode!=0 and diagnostic in refused.stderr,(label,refused.stderr)
        assert snapshot(root)==before,label
        if args.ui:
            from test_model_import_form import fill
            lines=['wait_maps','wait_ready','assets_tab 0','frames 3','texture_select Original','frames 3',
                   'reveal btn_tex_replace_toggle','frames 3','reveal btn_tex_replace_toggle','frames 3',
                   'click btn_tex_replace_toggle','frames 3','reveal input_tex_image','frames 3']
            lines+=fill('input_tex_image',image)
            lines+=['reveal btn_tex_replace','frames 3','reveal btn_tex_replace','frames 3','click btn_tex_replace','frames 3',
                    'assert_state texture_replace_failed 1','assert_log '+diagnostic,
                    'reveal texture_replace_error','frames 3','reveal texture_replace_error','frames 3',
                    'assert_widget texture_replace_error','quit']
            script=work/(label+'.txt');script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(work/'profile'),LOCALAPPDATA=str(work/'local'))
            ui=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(root),'--auto',str(script)],
                              cwd=repo,env=env,capture_output=True,text=True,timeout=120)
            (work/(label+'-ui-output.log')).write_text(ui.stdout+ui.stderr,encoding='utf-8')
            trace=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert ui.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-3500:]
            assert snapshot(root)==before,label
        checks.append(label)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks,'ui':args.ui},indent=2)+'\n',encoding='utf-8')
    print(len(checks),'texture layout refusal checks passed')


if __name__=='__main__':
    main()
