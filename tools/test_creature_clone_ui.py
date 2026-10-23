#!/usr/bin/env python3
"""Exercise distinct Creature clone controls, source/draft guards and paired Restore."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
from test_creature_clone_cli import fixture
from test_definition_edit_ui import click
from test_fmp_links import read_bank, crc0
from test_texture_lookup import texture_bank

MODES=('save','numeric','indexed-duplicate','folder','pack','cancel','close-review','destination',
       'stale-bank','stale-names','unknown','bad-count','empty-list','non-creature','frontend','empty-name','duplicate-name','install-switch')


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--only',nargs='+',choices=MODES)
    args=parser.parse_args();fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))['CREATURE']['fields']
    work=Path(tempfile.mkdtemp(prefix='creature-clone-ui-',dir=args.work_root.resolve()));print('Evidence retained at',work,flush=True);cases=0
    for size,scale in (('1440x900',1),('800x600',1.5)):
        for mode in args.only or MODES:
            case=work/(size+'-'+mode);root=case/'install';folder=fixture(root,fields)
            levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
            texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
            bank_name='frontend.bin' if mode=='frontend' else 'game.bin'
            if mode=='frontend':shutil.copyfile(folder/'game.bin',folder/bank_name)
            source=folder/bank_name;names=folder/'names.bin';original=source.read_bytes();pool=names.read_bytes();records=read_bank(root,bank_name)
            output=case/'output';destination=output if mode in ('folder','pack') else root;bank=destination/'data/CompiledDefs'/bank_name
            owner={'numeric':4,'indexed-duplicate':5,'unknown':7,'bad-count':8,'empty-list':9,'non-creature':0}.get(mode,1)
            selector='1' if mode=='numeric' else 'DONOR' if owner==1 else str(owner)
            accepted=mode in ('save','numeric','indexed-duplicate','folder','pack','close-review')
            preflight=mode in ('unknown','bad-count','empty-list','non-creature','frontend')
            manifest=b'{"version":1,"name":"Creature clone"}\n';new='GUI_CREATURE_NEW'
            lines=['wait_maps','wait_ready',f'set uiscale {scale}']
            if mode=='folder':lines += [f'set saveroot {output.as_posix()}']
            if mode=='pack':output.mkdir();(output/'forge_pack.json').write_bytes(manifest);lines += [f'pack_dest {output.as_posix()}']
            lines += ['assets_tab 6','wait_definitions','frames 3']
            if mode=='frontend':lines += ['definition_bank frontend.bin','wait_definitions','frames 3']
            lines += ['definition_select '+selector,'frames 3']
            if preflight:
                lines += ['definition_clone_creature_refused','assert_state definition_editing 0','assert_state definition_creature_cloning 0']
            else:
                lines += click('btn_definition_clone_creature')+['assert_state definition_editing 1','assert_state definition_creature_cloning 1',
                    'definition_clone_refused','definition_clone_creature_refused','definition_select_refused OTHER','definition_bank_refused frontend.bin']
                if mode!='empty-name':lines += click('input_definition_edit_value')+['input_text '+('DONOR' if mode=='duplicate-name' else new),'frames 3']
                if mode in ('stale-bank','stale-names'):lines += [f'screenshot {(case/"barrier.png").as_posix()}','frames 300']
                if mode=='destination':lines += [f'set saveroot {output.as_posix()}','wait_definitions','frames 3']
                if mode=='install-switch':
                    fixture(case/'other',fields);lines += [f'scan_install {(case/"other").as_posix()}','frames 3',
                        'assert_log install: save or discard the current edits before changing folders','assert_state definition_creature_cloning 1']
                if mode in ('cancel','install-switch'):lines += ['definition_cancel','assert_state definition_creature_cloning 0']
                elif accepted:
                    if mode=='close-review':lines += ['close','frames 3','assert_state close_prompt 1','click btn_unsaved_cancel','frames 3',
                        'assert_state definition_creature_cloning 1','close','frames 3','click btn_unsaved_save','frames 3','assert_state close_prompt 0']
                    lines += click('btn_definition_save')+[f'screenshot {(case/"confirmation.png").as_posix()}']
                    lines += click('btn_definition_save_confirm')+['wait_ready','wait_definitions','frames 3',
                        'assert_state definition_editing 0','assert_state definition_creature_cloning 0',f'assert_state definition_selected {len(records)}',
                        f'screenshot {(case/"created.png").as_posix()}']
                else:lines += ['definition_save_refused','assert_state definition_editing 1','assert_state definition_creature_cloning 1','definition_cancel']
            lines += ['quit'];script=case/'creature.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            expected_source=original;expected_names=pool
            with (case/'output.log').open('w',encoding='utf-8') as log:
                process=subprocess.Popen([str(args.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=args.cwd.resolve(),
                    env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),stdout=log,stderr=subprocess.STDOUT)
                try:
                    if mode in ('stale-bank','stale-names'):
                        deadline=time.monotonic()+60
                        while not (case/'barrier.png').exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                        assert (case/'barrier.png').exists(),'draft barrier not reached'
                        if mode=='stale-bank':expected_source=bytes([original[0]^1])+original[1:];source.write_bytes(expected_source)
                        else:expected_names=bytes([pool[0]^1])+pool[1:];names.write_bytes(expected_names)
                    returncode=process.wait(timeout=120)
                finally:
                    if process.poll() is None:process.terminate();process.wait(timeout=10)
            trace=Path(str(script)+'.log').read_text(encoding='utf-8');assert returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-7000:]
            if accepted:
                expected=bytearray(records[owner]['data'])
                for at in (13,21):struct.pack_into('<I',expected,at,len(records))
                assert read_bank(destination)==records+[dict(records[owner],name=new,data=bytes(expected))]
                raw=bank.read_bytes();assert raw[13:13+12*len(records)]==original[13:13+12*len(records)]
                target_names=bank.parent/'names.bin';assert target_names.read_bytes()[20:len(pool)]==pool[20:]
                for path,old in ((bank,original),(target_names,pool)):
                    if destination==root:assert Path(str(path)+'.forge-orig').read_bytes()==old
                    else:assert Path(str(path)+'.forge-created').is_file()
                restore=subprocess.run([str(args.forge.resolve()),'restore','--install',str(destination)],cwd=args.cwd.resolve(),capture_output=True,text=True,timeout=60)
                assert restore.returncode==0,restore.stdout+restore.stderr
                if destination==root:assert bank.read_bytes()==original and target_names.read_bytes()==pool
                else:assert not bank.exists() and not target_names.exists()
            else:
                assert source.read_bytes()==expected_source and names.read_bytes()==expected_names
                assert not list(root.rglob('*.forge-orig'))
            if mode=='pack':assert (output/'forge_pack.json').read_bytes()==manifest
            if destination!=root:assert source.read_bytes()==original and names.read_bytes()==pool
            cases+=1;print(size,mode,'Creature clone controls, self-only delta, draft guards and Restore: PASS',flush=True)
    (work/'result.json').write_text(json.dumps({'passed':True,'cases':cases})+'\n',encoding='utf-8')


if __name__=='__main__':main()
