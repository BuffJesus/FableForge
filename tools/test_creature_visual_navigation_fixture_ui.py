#!/usr/bin/env python3
"""Tiny independent Creature links: numeric names and malformed-reference UI refusal."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib
from test_fmp_links import crc0
from test_definition_edit_ui import click
from test_texture_lookup import texture_bank


def fixture(root, repo):
    fields=json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))['CREATURE']['fields']
    def payload(rows, count=None):
        data=b'\x01\0\x01'+struct.pack('<H',len(rows) if count is None else count)
        data+=b''.join(struct.pack('<III',crc0(kind),index,0) for kind,index in rows)
        for field in fields:
            data+=struct.pack('<II',crc0(field['name']),99 if field['name']=='Components' else 0)
        return data
    valid=[('CCreatureDef',1),('CAppearanceDef',2)]
    entries=[('CREATURE','ROOT',payload(valid)),('CCreatureDef','2',b'\x01\0\x01'),
             ('CAppearanceDef','1',b'\x01\0\x01'),
             ('CREATURE','WRONG_CLASS',payload([('CCreatureDef',2),('CAppearanceDef',1)])),
             ('CREATURE','OUT_OF_RANGE',payload([('CCreatureDef',999),('CAppearanceDef',999)])),
             ('CREATURE','DUPLICATE_BODY',payload([('CCreatureDef',1),('CCreatureDef',1)])),
             ('CREATURE','EMPTY',payload([])),('CREATURE','BAD_COUNT',payload(valid,999)),
             ('CREATURE','BODY_ONLY',payload([('CCreatureDef',1)]))]
    folder=root/'data/CompiledDefs';folder.mkdir(parents=True)
    pool=bytearray();refs={}
    for kind,name,_ in entries:
        for value in (kind,name):
            if value not in refs:
                refs[value]=len(pool)+4;pool+=struct.pack('<I',crc0(value))+value.encode()+b'\0'
    (folder/'names.bin').write_bytes(struct.pack('<5I',0,0xa8e36c34,len(refs),len(pool)+4,0)+pool)
    counts={};table=bytearray();offsets=[];body=bytearray()
    for kind,name,data in entries:
        table+=struct.pack('<iii',refs[kind],refs[name],counts.get(kind,0));counts[kind]=counts.get(kind,0)+1
        offsets.append(2*len(entries)+len(body));body+=data
    compressed=zlib.compress(struct.pack('<'+'H'*len(entries),*offsets)+body)
    (folder/'game.bin').write_bytes(bytes(9)+struct.pack('<I',len(entries))+table+
                                  struct.pack('<I5I',2,0,0,len(entries),len(compressed),len(compressed))+compressed)
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='creature-visual-navigation-fixture-ui-',dir=args.work_root.resolve()))
    root=work/'install';fixture(root,repo)
    def snapshot():
        return {p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                for p in root.rglob('*') if p.is_file()}
    before=snapshot();print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir()
        lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 6','wait_definitions']
        for widget,target in [('btn_definition_body',1),('btn_definition_appearance',2)]:
            lines+=['definition_select ROOT','frames 3']+click(widget)+[f'assert_state definition_selected {target}']
        for name in ('WRONG_CLASS','OUT_OF_RANGE','DUPLICATE_BODY','EMPTY','BAD_COUNT'):
            lines+=['definition_select '+name,'clear_widget_registry','frames 3','assert_widget_absent btn_definition_body',
                    'assert_widget_absent btn_definition_appearance']
        lines+=['definition_select BODY_ONLY','clear_widget_registry','frames 3','assert_widget_absent btn_definition_appearance']
        lines+=click('btn_definition_body')+['assert_state definition_selected 1','quit']
        script=case/'navigation.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        result=subprocess.run([str(args.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
                              cwd=args.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',
                              APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),
                              capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
        (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6000:]
        assert snapshot()==before,'read-only navigation changed fixture files'
        print(size,'numeric names, five malformed/missing cases and valid body-only link: PASS',flush=True)
    (work/'result.json').write_text(json.dumps(dict(passed=True,source_hashes=before),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
