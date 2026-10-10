#!/usr/bin/env python3
"""Choose native group indices and refuse invalid changed targets before publication."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
from test_definition_groups_ui import write_fixture
from test_definition_edit_ui import click
from test_fmp_links import read_bank
from test_mod_field_details import tag

MODES=('save','repeat','numeric','duplicate','unnamed','folder','pack','legacy-noop','legacy-noop-hex',
       'legacy-repair','wrong-type','wrong-family','out-of-range','negative','stale-target','frontend')


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--only',nargs='+',choices=MODES)
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='definition-group-picker-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    cases=0
    for size,scale in (('1440x900',1),('800x600',1.5)):
        for mode in args.only or MODES:
            case=work/(size+'-'+mode);root=case/'install'
            group_names=('DUP','DUP') if mode=='duplicate' else ('FIRST','') if mode=='unnamed' else ('3','GROUP_NORMAL')
            group_types=('THING_GROUP','ENGINE_THEME_GROUP') if mode=='wrong-family' else ('THING_GROUP','THING_GROUP')
            folder=write_fixture(root,repo,group_names=group_names,group_types=group_types)
            bank_name='frontend.bin' if mode=='frontend' else 'game.bin'
            source=folder/bank_name;before=source.read_bytes();names_before=(folder/'names.bin').read_bytes()
            records=read_bank(root,bank_name)
            output=case/'output';destination=output if mode in ('folder','pack') else root
            bank=destination/'data/CompiledDefs'/bank_name
            selected=5 if mode in ('legacy-noop','legacy-repair') else 4 if mode=='legacy-noop-hex' else 0
            choice=2 if mode in ('numeric','legacy-repair') else 3
            accepted=mode in ('save','repeat','numeric','duplicate','unnamed','folder','pack','legacy-repair','frontend')
            manifest=None
            lines=['wait_maps','wait_ready',f'set uiscale {scale}']
            if mode=='folder':lines += [f'set saveroot {output.as_posix()}']
            if mode=='pack':
                output.mkdir();manifest=b'{"version":1,"name":"Group picker"}\n';(output/'forge_pack.json').write_bytes(manifest)
                lines += [f'pack_dest {output.as_posix()}']
            lines += ['assets_tab 6','wait_definitions','frames 3']
            if mode=='frontend':lines += ['definition_bank frontend.bin','wait_definitions','frames 3']
            lines += [f'definition_select {selected}','frames 3','definition_edit 1']
            if accepted and mode!='frontend':
                lines += click('combo_definition_edit_group')+['assert_widget input_definition_group_search']
                if mode=='numeric':
                    choice=3
                    lines += click('input_definition_group_search')+['input_text GROUP_NORMAL','frames 3']
                lines += click('definition_group_option_'+str(choice))
                lines += click('btn_definition_save')+click('btn_definition_save_confirm')
                lines += ['wait_ready','wait_definitions','frames 3','assert_state definition_editing 0',
                          'assert_state definition_group '+str(choice),f'screenshot {(case/"saved.png").as_posix()}']
                if mode=='repeat':
                    choice=2
                    lines += ['definition_edit 1','definition_value 2','definition_save','wait_ready','wait_definitions','frames 3','assert_state definition_group 2']
            elif mode=='frontend':
                # Other banks retain scalar behavior rather than guessing reference meaning.
                choice=0;lines += ['definition_value 0','definition_save','wait_definitions','frames 3','assert_state definition_editing 0']
            elif mode in ('legacy-noop','legacy-noop-hex'):
                if mode=='legacy-noop-hex':lines += ['definition_value 0x0']
                lines += ['definition_save','wait_definitions','frames 3','assert_state definition_editing 0']
            else:
                value={'wrong-type':'0','wrong-family':'3','out-of-range':'999','negative':'-1','stale-target':'3'}[mode]
                lines += ['definition_value '+value]
                if mode=='stale-target':lines += [f'screenshot {(case/"barrier.png").as_posix()}','frames 300']
                lines += ['definition_save_refused','assert_state definition_editing 1',
                          'assert_state definition_edit_failed 1','definition_cancel']
            lines += ['quit'];script=case/'groups.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            expected_source=before
            with (case/'output.log').open('w',encoding='utf-8') as log:
                process=subprocess.Popen([str(args.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
                    cwd=args.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),
                    stdout=log,stderr=subprocess.STDOUT)
                try:
                    if mode=='stale-target':
                        deadline=time.monotonic()+60
                        while not (case/'barrier.png').exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                        assert (case/'barrier.png').exists(),'draft barrier not reached'
                        changed=bytearray(before)
                        struct.pack_into('<I',changed,13+12*3+4,struct.unpack_from('<I',changed,13+12*2+4)[0])
                        expected_source=bytes(changed);source.write_bytes(expected_source)
                    returncode=process.wait(timeout=120)
                finally:
                    if process.poll() is None:process.terminate();process.wait(timeout=10)
            trace=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6500:]
            if accepted:
                after=read_bank(destination,bank_name);offset=records[selected]['data'].index(struct.pack('<I',tag('GroupDef')))+4
                expected=dict(records[selected],data=records[selected]['data'][:offset]+struct.pack('<i',choice)+records[selected]['data'][offset+4:])
                assert after==records[:selected]+[expected]+records[selected+1:]
                assert (bank.parent/'names.bin').read_bytes()==names_before
                if destination==root:
                    assert Path(str(bank)+'.forge-orig').read_bytes()==before
                else:
                    assert source.read_bytes()==before
                    assert Path(str(bank)+'.forge-created').is_file()
                restore=subprocess.run([str(args.forge.resolve()),'restore','--install',str(destination)],cwd=args.cwd.resolve(),capture_output=True,text=True,timeout=120)
                assert restore.returncode==0,restore.stdout+restore.stderr
                if destination==root:assert bank.read_bytes()==before
                else:assert not bank.exists() and not (bank.parent/'names.bin').exists()
            else:
                assert source.read_bytes()==expected_source and not list(root.rglob('*.forge-orig'))
            assert (folder/'names.bin').read_bytes()==names_before
            if manifest is not None:assert (output/'forge_pack.json').read_bytes()==manifest
            cases += 1;print(size,mode,'group picker, target refusal, exact delta and Restore: PASS',flush=True)
    (work/'result.json').write_text(json.dumps({'passed':True,'cases':cases})+'\n',encoding='utf-8')


if __name__=='__main__':main()
