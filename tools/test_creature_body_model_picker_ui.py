#!/usr/bin/env python3
"""Choose body mesh names/IDs, disambiguate labels and exclude invalid IDs."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from test_creature_body_stale_ui import fixture
from test_definition_edit_ui import click
from test_fmp_links import read_bank
from test_mod_asset_choice_contributors import bank


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='creature-body-model-picker-ui-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True);cases=0
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        for mode in ('named','name-search','id-search','unnamed','duplicate-name','exclusions'):
            case=work/(size+'-'+mode);case.mkdir();root=case/'install';fixture(root,repo);output=case/'output'
            graphics=root/'data/graphics/graphics.big'
            records={'CURRENT':dict(id=10,type=5,payload=b'x',header=b''),
                     'REPLACEMENT':dict(id=20,type=5,payload=b'y',header=b'')}
            if mode=='unnamed':records['']=records.pop('REPLACEMENT')
            if mode=='duplicate-name':
                records['DUP_A']=records.pop('REPLACEMENT')
                records['DUP_B']=dict(id=30,type=5,payload=b'z',header=b'')
            if mode=='exclusions':
                records.update(WRONG_TYPE=dict(id=30,type=1,payload=b'z',header=b''),
                    AMBIGUOUS_MESH=dict(id=40,type=5,payload=b'a',header=b''),
                    AMBIGUOUS_OTHER=dict(id=40,type=1,payload=b'b',header=b''))
            bank(graphics,'MBANK_ALLMESHES',records)
            if mode=='duplicate-name':graphics.write_bytes(graphics.read_bytes().replace(b'DUP_B',b'DUP_A'))
            originals={p:p.read_bytes() for p in root.rglob('*') if p.is_file()}
            expected=read_bank(root);choice=30 if mode=='duplicate-name' else 20
            payload=bytearray(expected[0]['data']);struct.pack_into('<I',payload,27,choice);expected[0]['data']=bytes(payload)
            lines=['wait_maps','wait_ready',f'set uiscale {scale}',f'set saveroot {output.as_posix()}',
                'assets_tab 6','wait_definitions','definition_select BODY','frames 3']
            lines+=click('definition_bind_body_0_0')+['assert_state definition_editing 1','clear_widget_registry','frames 3']
            lines+=click('combo_definition_body_model')+['assert_widget input_definition_body_model_search']
            if mode in ('name-search','id-search'):
                lines+=click('input_definition_body_model_search')+['input_text '+('replacement' if mode=='name-search' else '20'),
                    'clear_widget_registry','frames 3','assert_widget_absent definition_body_model_option_10']
            if mode=='exclusions':lines+=['assert_widget_absent definition_body_model_option_30','assert_widget_absent definition_body_model_option_40']
            if mode=='duplicate-name':lines+=['assert_widget definition_body_model_option_20','assert_widget definition_body_model_option_30']
            lines+=[f'screenshot {(case/"chooser.png").as_posix()}']+click('definition_body_model_option_'+str(choice))
            lines+=click('btn_definition_save')+click('btn_definition_save_confirm')+[
                'wait_ready','wait_definitions','frames 3','assert_state definition_editing 0','quit']
            script=case/'picker.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            with (case/'output.log').open('w',encoding='utf-8') as log:
                result=subprocess.run([str(args.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
                    cwd=repo,stdout=log,stderr=subprocess.STDOUT,timeout=90,
                    env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')))
            trace=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6000:]
            assert read_bank(output)==expected,'named model choice changed more than one word or chose wrong ID'
            assert (output/'data/CompiledDefs/names.bin').read_bytes()==originals[root/'data/CompiledDefs/names.bin']
            result=subprocess.run([str(args.forge.resolve()),'restore','--install',str(output)],cwd=repo,capture_output=True,text=True,timeout=60)
            assert result.returncode==0,result.stdout+result.stderr
            assert not list(output.rglob('*.bin')) and not list(output.rglob('*.forge-created'))
            assert {p:p.read_bytes() for p in root.rglob('*') if p.is_file()}==originals,'source fixture changed'
            cases+=1;print(size,mode,'named model choice, exact delta and Restore: PASS',flush=True)
    (work/'result.json').write_text(json.dumps(dict(passed=True,cases=cases),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
