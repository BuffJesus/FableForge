#!/usr/bin/env python3
"""Tiny body-binding pack routing, source preference, draft guards and Restore."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
from test_creature_body_stale_ui import fixture
from test_definition_edit_ui import click
from test_fmp_links import read_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='creature-body-pack-ui-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True);cases=0
    manifest=b'{"version":1,"name":"Body binding test"}\n'
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        for mode in ('fresh','existing','destination','manifest'):
            case=work/(size+'-'+mode);case.mkdir();root=case/'install';fixture(root,repo)
            pack=case/'pack';pack.mkdir()
            if mode=='existing':fixture(pack,repo,20)
            (pack/'forge_pack.json').write_bytes(manifest)
            source=pack if mode=='existing' else root
            originals={p:p.read_bytes() for p in root.rglob('*') if p.is_file()}
            pack_originals={p:p.read_bytes() for p in pack.rglob('*') if p.is_file()}
            records=read_bank(source);replacement=10 if mode=='existing' else 20
            lines=['wait_maps','wait_ready',f'set uiscale {scale}',f'pack_dest {pack.as_posix()}',
                   'assets_tab 6','wait_definitions','definition_select BODY','frames 3']
            lines+=click('definition_bind_body_0_0')+['assert_state definition_editing 1',f'definition_value {replacement}']
            if mode in ('fresh','existing'):
                lines+=click('btn_definition_save')+click('btn_definition_save_confirm')+[
                    'wait_ready','wait_definitions','frames 3','assert_state definition_editing 0']
            else:
                if mode=='destination':
                    other=case/'other';other.mkdir();(other/'forge_pack.json').write_bytes(manifest)
                    lines+=[f'pack_dest {other.as_posix()}','frames 3']
                    reason='Definition source or destination changed'
                else:
                    lines+=[f'screenshot {(case/"barrier.png").as_posix()}','frames 300']
                    reason='Choose a valid FableForge pack'
                lines+=['definition_save_refused','assert_state definition_editing 1',
                        'assert_state definition_edit_failed 1','assert_log save definition: '+reason,'definition_cancel']
            lines+=['quit'];script=case/'pack.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            with (case/'output.log').open('w',encoding='utf-8') as log:
                process=subprocess.Popen([str(args.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
                    cwd=repo,stdout=log,stderr=subprocess.STDOUT,
                    env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')))
                try:
                    if mode=='manifest':
                        deadline=time.monotonic()+45
                        while not (case/'barrier.png').exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                        assert (case/'barrier.png').exists(),'draft barrier not reached'
                        (pack/'forge_pack.json').unlink()
                    returncode=process.wait(timeout=90)
                finally:
                    if process.poll() is None:process.terminate();process.wait(timeout=10)
            trace=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6000:]
            assert {p:p.read_bytes() for p in root.rglob('*') if p.is_file()}==originals,'base source changed'
            if mode in ('fresh','existing'):
                expected=[dict(record) for record in records]
                payload=bytearray(expected[0]['data']);struct.pack_into('<I',payload,27,replacement);expected[0]['data']=bytes(payload)
                assert read_bank(pack)==expected,'pack source preference or one-word delta differs'
                folder=pack/'data/CompiledDefs'
                source_names=source/'data/CompiledDefs/names.bin'
                assert (folder/'names.bin').read_bytes()==(pack_originals[source_names] if mode=='existing' else originals[source_names])
                for name in ('game.bin','names.bin'):
                    path=folder/name
                    if mode=='existing':
                        if name=='game.bin':assert Path(str(path)+'.forge-orig').read_bytes()==pack_originals[path]
                        else:assert not Path(str(path)+'.forge-orig').exists(),'unchanged names need no backup'
                    else:assert Path(str(path)+'.forge-created').is_file()
                result=subprocess.run([str(args.forge.resolve()),'restore','--install',str(pack)],cwd=repo,capture_output=True,text=True,timeout=60)
                assert result.returncode==0,result.stdout+result.stderr
            expected_pack=dict(pack_originals)
            if mode=='existing':
                original_bank=pack/'data/CompiledDefs/game.bin'
                expected_pack[Path(str(original_bank)+'.forge-orig')]=pack_originals[original_bank]
            if mode=='manifest':del expected_pack[pack/'forge_pack.json']
            assert {p:p.read_bytes() for p in pack.rglob('*') if p.is_file()}==expected_pack,'pack preservation/Restore differs'
            if mode=='destination':assert {p.name:p.read_bytes() for p in other.rglob('*') if p.is_file()}=={'forge_pack.json':manifest}
            cases+=1;print(size,mode,'pack routing, preservation and draft guard: PASS',flush=True)
    (work/'result.json').write_text(json.dumps(dict(passed=True,cases=cases),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
