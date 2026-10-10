#!/usr/bin/env python3
"""Body binding controls, independent native delta and Restore without asset copies."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from audit_creature_body_models import body_slots, file_hash
from test_definition_edit_ui import click
from test_fmp_links import read_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--cwd',type=Path,default=repo)
    args=parser.parse_args();native=args.install.resolve()
    assert not args.work_root.resolve().is_relative_to(native)
    inputs=sorted((native/'data/CompiledDefs').glob('*.bin'))+[native/'data/graphics/graphics.big']
    def hashes():return {str(p):file_hash(p) for p in inputs}
    before=hashes();records=read_bank(native);choice=None
    for index,record in enumerate(records):
        if record['definition']!='CCreatureDef':continue
        try:_,slots=body_slots(record['data'])
        except ValueError:continue
        for slot in slots:
            other=next((s for s in slots if s['body_part']==slot['body_part'] and s['id']!=slot['id']),None)
            if other:choice=dict(entry=index,**slot,replacement=other['id']);break
        if choice:break
    assert choice
    assert not any(r['name']==str(choice['entry']) for r in records)
    work=Path(tempfile.mkdtemp(prefix='creature-body-binding-ui-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True);cases=0
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        for mode in ('save','cancel','invalid','noop','destination','close-review'):
            case=work/(size+'-'+mode);case.mkdir();output=case/'output'
            widget=f'definition_bind_body_{choice["body_part"]}_{choice["alternative"]}'
            lines=['wait_maps','wait_ready',f'set uiscale {scale}',f'set saveroot {output.as_posix()}',
                   'assets_tab 6','wait_definitions',f'definition_select {choice["entry"]}','frames 3']
            lines+=click(widget)+['assert_state definition_editing 1','definition_clone_refused',
                                 'definition_bank_refused frontend.bin']
            value=choice['id'] if mode=='noop' else 999999 if mode=='invalid' else choice['replacement']
            lines+=click('input_definition_edit_value')+['key_down Ctrl','key_down A','key_up A','key_up Ctrl','input_text '+str(value),'frames 3',
                        f'screenshot {(case/"draft.png").as_posix()}']
            if mode=='cancel':lines+=click('btn_definition_edit_cancel')+['assert_state definition_editing 0']
            elif mode=='close-review':
                lines+=['close','frames 3','assert_state close_prompt 1','click btn_unsaved_cancel','frames 3',
                        'assert_state definition_editing 1']+click('btn_definition_edit_cancel')
            elif mode in ('invalid','destination'):
                if mode=='destination':lines+=[f'set saveroot {(case/"other").as_posix()}','frames 3']
                lines+=['definition_save_refused','assert_state definition_editing 1','definition_cancel']
            else:
                lines+=click('btn_definition_save')+[f'screenshot {(case/"confirmation.png").as_posix()}']
                lines+=click('btn_definition_save_confirm')+['wait_ready','wait_definitions','frames 3','assert_state definition_editing 0']
            lines+=['quit'];script=case/'binding.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            result=subprocess.run([str(args.gui.resolve()),'--install',str(native),'--size',size,'--auto',str(script)],
                                  cwd=args.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',
                                  APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),
                                  capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
            (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
            trace=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6500:]
            if mode=='save':
                expected=[dict(r) for r in records];data=bytearray(expected[choice['entry']]['data'])
                struct.pack_into('<I',data,choice['offset'],value);expected[choice['entry']]['data']=bytes(data)
                assert read_bank(output)==expected,'GUI body delta differs'
                bank=output/'data/CompiledDefs/game.bin';names=bank.parent/'names.bin'
                assert names.read_bytes()==(native/'data/CompiledDefs/names.bin').read_bytes()
                restore=subprocess.run([str(args.forge.resolve()),'restore','--install',str(output)],cwd=args.cwd.resolve(),
                                       capture_output=True,text=True,timeout=60)
                assert restore.returncode==0,restore.stdout+restore.stderr
                assert not bank.exists() and not names.exists()
            else:assert not output.exists() and not (case/'other').exists(),'refusal/cancel/no-op wrote output'
            assert hashes()==before,'native source changed'
            cases+=1;print(size,mode,'body controls and preservation: PASS',flush=True)
    (work/'result.json').write_text(json.dumps(dict(passed=True,cases=cases,choice=choice,source_hashes=before),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
