#!/usr/bin/env python3
"""Tiny independent body fixtures: GUI save control and stale bank/name/graphics refusal."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import zlib
from test_fmp_links import crc0, read_bank
from test_definition_edit_ui import click
from test_mod_asset_choice_contributors import bank
from test_texture_lookup import texture_bank


def graphics_fixture(path, replacement_type=5):
    bank(path,'MBANK_ALLMESHES',{
        'MODEL_CURRENT':dict(id=10,type=5,payload=b'x',header=b''),
        'MODEL_REPLACEMENT':dict(id=20,type=replacement_type,payload=b'y',header=b'')})


def fixture(root, repo, model_id=10):
    def word(value):return struct.pack('<I',value)
    def field(name,value):return word(crc0(name))+word(value)
    payload=b'\x01\0\x01'+field('WoundMorphs',0)+word(0x01ab6cee)
    payload+=field('RandomAppearanceMorph',1)+word(1)+struct.pack('<4I',model_id,0,0,0)+b'\0'
    payload+=bytes(27)+word(0)+bytes(13)+field('Expressions',0)+word(0x01ab6d32)+field('Stats',0)
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))['CCreatureDef']['fields']
    payload+=b''.join(field(f['name'],0) for f in fields)
    folder=root/'data/CompiledDefs';folder.mkdir(parents=True)
    names=bytearray();refs={}
    for value in ('CCreatureDef','BODY'):
        refs[value]=len(names)+4;names+=word(crc0(value))+value.encode()+b'\0'
    (folder/'names.bin').write_bytes(struct.pack('<5I',0,0xa8e36c34,2,len(names)+4,0)+names)
    chunk=zlib.compress(struct.pack('<H',2)+payload)
    (folder/'game.bin').write_bytes(bytes(9)+word(1)+struct.pack('<3I',refs['CCreatureDef'],refs['BODY'],0)+
                                  struct.pack('<I5I',2,0,0,1,len(chunk),len(chunk))+chunk)
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    graphics_fixture(root/'data/graphics/graphics.big')


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='creature-body-stale-ui-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True);cases=0
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        for mode in ('save','stale-bank','stale-names','stale-graphics'):
            case=work/(size+'-'+mode);case.mkdir();root=case/'install';fixture(root,repo);output=case/'output'
            originals={p:p.read_bytes() for p in root.rglob('*') if p.is_file()}
            lines=['wait_maps','wait_ready',f'set uiscale {scale}',f'set saveroot {output.as_posix()}',
                   'assets_tab 6','wait_definitions','definition_select BODY','frames 3']
            lines+=click('definition_bind_body_0_0')+['assert_state definition_editing 1','definition_value 20']
            if mode=='save':
                lines+=click('btn_definition_save')+click('btn_definition_save_confirm')+[
                    'wait_ready','wait_definitions','frames 3','assert_state definition_editing 0']
            else:
                reason='The graphics source changed' if mode=='stale-graphics' else 'The source bank or name pool changed'
                lines+=[f'screenshot {(case/"barrier.png").as_posix()}','frames 300','definition_save_refused',
                        'assert_state definition_editing 1','assert_state definition_edit_failed 1',
                        'assert_log save definition: '+reason,'definition_cancel']
            lines+=['quit'];script=case/'stale.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            changed=None
            with (case/'output.log').open('w',encoding='utf-8') as log:
                process=subprocess.Popen([str(args.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
                                         cwd=repo,stdout=log,stderr=subprocess.STDOUT,
                                         env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')))
                try:
                    if mode!='save':
                        deadline=time.monotonic()+45
                        while not (case/'barrier.png').exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                        assert (case/'barrier.png').exists(),'draft barrier not reached'
                        changed=root/('data/graphics/graphics.big' if mode=='stale-graphics' else
                                      'data/CompiledDefs/'+('game.bin' if mode=='stale-bank' else 'names.bin'))
                        if mode=='stale-graphics':graphics_fixture(changed,1)
                        else:
                            data=bytearray(originals[changed]);data[0]^=1;changed.write_bytes(data)
                        mutated=changed.read_bytes()
                    returncode=process.wait(timeout=90)
                finally:
                    if process.poll() is None:process.terminate();process.wait(timeout=10)
            trace=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6000:]
            assert {p for p in root.rglob('*') if p.is_file()}==set(originals),'new source/recovery files'
            assert all(p.read_bytes()==(mutated if p==changed else data) for p,data in originals.items()),'source preservation differs'
            if mode=='save':
                expected=read_bank(root);payload=bytearray(expected[0]['data']);struct.pack_into('<I',payload,27,20);expected[0]['data']=bytes(payload)
                assert read_bank(output)==expected,'synthetic GUI body delta differs'
                result=subprocess.run([str(args.forge.resolve()),'restore','--install',str(output)],cwd=repo,capture_output=True,text=True,timeout=60)
                assert result.returncode==0,result.stdout+result.stderr
                assert not list(output.rglob('*.bin')) and not list(output.rglob('*.forge-created'))
            else:assert not output.exists(),'stale refusal created output'
            cases+=1;print(size,mode,'exact fixture preservation and guarded save: PASS',flush=True)
    (work/'result.json').write_text(json.dumps(dict(passed=True,cases=cases),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
