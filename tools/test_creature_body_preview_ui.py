#!/usr/bin/env python3
"""Read-only body model preview links and draft protection at both window sizes."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from audit_creature_body_models import body_slots, mesh_directory, file_hash
from test_definition_edit_ui import click
from test_fmp_links import read_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args();native=args.install.resolve()
    assert not args.work_root.resolve().is_relative_to(native)
    graphics=native/'data/graphics/graphics.big'
    inputs=sorted((native/'data/CompiledDefs').glob('*.bin'))+[graphics]
    def hashes():return {str(p):file_hash(p) for p in inputs}
    before=hashes();records=read_bank(native);directory=mesh_directory(graphics)
    candidates=[]
    for index,record in enumerate(records):
        if record['definition']!='CCreatureDef':continue
        try:_,slots=body_slots(record['data'])
        except ValueError:continue
        for slot in slots:
            targets=[entry for entry in directory if entry['id']==slot['id']]
            if len(targets)==1 and targets[0]['type']==5:
                candidates.append(dict(index=index,**slot,name=targets[0]['name']))
    chosen=[candidates[0],next(row for row in candidates if row['body_part']>0 or row['alternative']>0)]
    # Component selectors prefer names over numeric strings. Guard this fixture
    # choice so native record indices cannot be accidentally redirected.
    for row in chosen:assert not any(r['name']==str(row['index']) for r in records)
    work=Path(tempfile.mkdtemp(prefix='creature-body-preview-ui-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir()
        lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 6','wait_definitions']
        for row in chosen:
            widget=f'definition_body_model_{row["body_part"]}_{row["alternative"]}'
            lines+=['assets_tab 6',f'definition_select {row["index"]}','frames 3','definition_clone',
                    'assert_state definition_editing 1']
            lines+=click(widget)+['assert_state assets_tab 6',f'assert_state definition_selected {row["index"]}',
                                  'assert_state definition_editing 1','definition_cancel']
            lines+=click(widget)+['assert_state assets_tab 1','assert_state model_ready 1',
                                  f'assert_state model_selected {row["name"]}']
        lines+=[f'screenshot {(case/"preview.png").as_posix()}','quit']
        script=case/'preview.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        result=subprocess.run([str(args.gui.resolve()),'--install',str(native),'--size',size,'--auto',str(script)],
                              cwd=args.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',
                              APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),
                              capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6500:]
        assert hashes()==before,'source bank changed'
        print(size,'body-slot preview, explicit mesh identity and draft guards: PASS',flush=True)
    (work/'result.json').write_text(json.dumps(dict(passed=True,chosen=chosen,source_hashes=before),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
