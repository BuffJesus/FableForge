#!/usr/bin/env python3
"""Private GUI controls and independent native deltas; no game/texture copies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from audit_creature_visual_graph import analyze, listing
from test_definition_edit_ui import click
from test_fmp_links import read_bank

MODES=('save','cancel','duplicate-body','empty-root','empty-body','empty-appearance','destination','close-review')

def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--payload-exe',type=Path,default=repo/'build/fableforge_defrelocate_tests.exe')
    parser.add_argument('--schema',type=Path,default=repo/'docs/re_reference/def_schema.json')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--only',nargs='+',choices=MODES)
    args=parser.parse_args();native=args.install.resolve()
    assert not args.work_root.resolve().is_relative_to(native),'evidence must be outside the source install'
    paths=[native/'data/CompiledDefs'/name for name in ('game.bin','names.bin')]
    source_bank=paths[0].read_bytes();source_pool=paths[1].read_bytes()
    hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    originals=read_bank(native);graph=analyze(originals)
    result=subprocess.run([str(args.payload_exe.resolve()),'--creature-visual-audit',str(native),str(args.schema.resolve())],
        capture_output=True,text=True,timeout=120)
    assert result.returncode==0,result.stderr
    payloads={row['root']:row['payloads'] for row in json.loads(result.stdout)['records']}
    donor=next(row for row in graph['records'] if not row['issues'] and
        not any(payload['unresolved'] for payload in payloads[row['index']]))
    nodes=[donor['index']]+[row['component'] for row in donor['visual_rows']]
    slots=list(range(len(originals),len(originals)+3));maps=dict(zip(nodes,slots))
    expected=[]
    for number,(node,payload) in enumerate(zip(nodes,payloads[donor['index']])):
        data=bytearray(originals[node]['data'])
        for rewrite in payload['rewrites']:
            assert rewrite['domain']=='game' and rewrite['before'] in maps
            struct.pack_into('<I',data,rewrite['offset'],maps[rewrite['before']])
        if number==0:
            selected={row['row']:index for index,row in enumerate(donor['visual_rows'])}
            for row in listing(originals[node]):
                at=row['offset']
                if row['row'] in selected:struct.pack_into('<II',data,at+4,slots[1+selected[row['row']]],slots[0])
                else:data[at:at+12]=originals[node]['data'][at:at+12]
        expected.append(bytes(data))
    work=Path(tempfile.mkdtemp(prefix='creature-private-ui-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True);cases=0
    try:
        for size,scale in [('1440x900',1),('800x600',1.5)]:
            for mode in args.only or MODES:
                case=work/(size+'-'+mode);case.mkdir();output=case/'output'
                names=['FORGE_GUI_PRIVATE_ROOT','FORGE_GUI_PRIVATE_BODY','FORGE_GUI_PRIVATE_APPEARANCE']
                if mode=='duplicate-body':names[1]=originals[donor['index']]['name']
                if mode.startswith('empty-'):names[('root','body','appearance').index(mode[6:])]=''
                lines=['wait_maps','wait_ready',f'set uiscale {scale}',f'set saveroot {output.as_posix()}',
                    'assets_tab 6','wait_definitions',f'definition_select {donor["index"]}','frames 3']
                lines+=click('btn_definition_clone_creature_private')+['assert_state definition_editing 1']
                lines+=['definition_clone_refused','definition_clone_creature_refused',
                    'definition_bank_refused frontend.bin','assert_state definition_editing 1']
                for widget,name in zip(('input_definition_edit_value','input_definition_clone_body','input_definition_clone_appearance'),names):
                    if name:lines+=click(widget)+['input_text '+name,'frames 3']
                lines+=[f'screenshot {(case/"draft.png").as_posix()}']
                if mode=='cancel':lines+=click('btn_definition_edit_cancel')+['assert_state definition_editing 0']
                elif mode=='duplicate-body' or mode.startswith('empty-') or mode=='destination':
                    if mode=='destination':lines+=[f'set saveroot {(case/"other-output").as_posix()}','wait_definitions','frames 3']
                    lines+=['definition_save_refused','assert_state definition_editing 1','definition_cancel']
                else:
                    if mode=='close-review':
                        lines+=['close','frames 3','assert_state close_prompt 1','click btn_unsaved_cancel','frames 3',
                            'assert_state definition_editing 1','close','frames 3','click btn_unsaved_save','frames 3','assert_state close_prompt 0']
                    lines+=click('btn_definition_save')+[f'screenshot {(case/"confirmation.png").as_posix()}']
                    lines+=click('btn_definition_save_confirm')+['wait_ready','wait_definitions','frames 3',
                        'assert_state definition_editing 0',f'assert_state definition_selected {slots[0]}',
                        f'screenshot {(case/"created.png").as_posix()}']
                lines+=['quit'];script=case/'private.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
                result=subprocess.run([str(args.gui.resolve()),'--install',str(native),'--size',size,'--auto',str(script)],
                    cwd=repo,capture_output=True,text=True,encoding='utf-8',timeout=150,
                    env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')))
                (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
                trace=Path(str(script)+'.log').read_text(encoding='utf-8')
                assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-7000:]
                if mode in ('save','close-review'):
                    created=read_bank(output);assert len(created)==len(originals)+3 and created[:len(originals)]==originals
                    for node,slot,name,data in zip(nodes,slots,names,expected):
                        assert created[slot]==dict(originals[node],name=name,data=data)
                    bank=output/'data/CompiledDefs/game.bin';pool=bank.parent/'names.bin'
                    assert bank.read_bytes()[13:13+12*len(originals)]==source_bank[13:13+12*len(originals)]
                    assert pool.read_bytes()[20:len(source_pool)]==source_pool[20:]
                    for path in (bank,pool):assert Path(str(path)+'.forge-created').is_file()
                    restore=subprocess.run([str(args.forge.resolve()),'restore','--install',str(output)],
                        cwd=repo,capture_output=True,text=True,timeout=60)
                    assert restore.returncode==0,restore.stdout+restore.stderr
                    assert not bank.exists() and not pool.exists()
                else:
                    assert not output.exists() and not (case/'other-output').exists(),'refusal/cancel published files'
                cases+=1;print(size,mode,'private controls and preservation: PASS',flush=True)
    finally:
        assert {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}==hashes,'native inputs changed'
    (work/'result.json').write_text(json.dumps({'passed':True,'cases':cases,'donor':donor['index'],
        'native_hashes':hashes,'full_install_copies':False,'gameplay_tested':False},indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
