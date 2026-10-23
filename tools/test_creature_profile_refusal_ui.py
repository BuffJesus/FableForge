#!/usr/bin/env python3
"""Malformed runtime profiles must refuse placement without changing a document."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from test_definition_placement_types_ui import fixture, inventory


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--schema',type=Path,default=repo/'docs/re_reference/def_schema.json')
    parser.add_argument('--prepare-only',action='store_true')
    args=parser.parse_args()
    args.work_root.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='profile-refusal-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    schema=json.loads(args.schema.read_text(encoding='utf-8'))
    driver_hash=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    gui_hash=None if args.prepare_only else hashlib.sha256(args.gui.resolve(strict=True).read_bytes()).hexdigest()
    cases=[]
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        for mode in ('count','name','verify','missing'):
            case=work/(size+'-'+mode)
            root=case/'install'
            fixture(root,schema,mode)
            before=inventory(root)
            lines=['wait_maps','wait_ready',f'set uiscale {scale}','select PlacementProbe','wait_loaded',
                   'edit 1','assert_state doc_things 0','assert_state doc_dirty 0',
                   'place_refused GUI_ROOT RefusedProbe','frames 2',
                   'assert_log editor: cannot read creature placement profile:',
                   'assert_state doc_things 0','assert_state doc_dirty 0',
                   'undo','frames 2','assert_state doc_things 0','assert_state doc_dirty 0','quit']
            script=case/'refuse.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            if not args.prepare_only:
                r=subprocess.run([str(args.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
                    cwd=args.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90,
                    env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')))
                (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8')
                trace=Path(str(script)+'.log').read_text(encoding='utf-8')
                assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-5000:]
            assert inventory(root)==before,'malformed-profile refusal changed files or recovery metadata'
            cases.append(dict(size=size,mode=mode,passed=not args.prepare_only))
    assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest()==driver_hash
    if gui_hash is not None:assert hashlib.sha256(args.gui.resolve().read_bytes()).hexdigest()==gui_hash
    (work/'result.json').write_text(json.dumps(dict(passed=not args.prepare_only,prepared_only=args.prepare_only,
        cases=cases,driver_sha256=driver_hash,gui_sha256=gui_hash),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
