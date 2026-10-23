#!/usr/bin/env python3
"""Verify confirmed thing/theme group picker deltas with read-only retail inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from test_definition_edit_ui import click
from test_fmp_links import read_bank
from test_mod_field_details import tag


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args();native=args.install.resolve();folder=native/'data/CompiledDefs'
    inputs=sorted(folder.glob('*.bin'));hashes={str(path):digest(path) for path in inputs}
    records=read_bank(native);original=(folder/'game.bin').read_bytes();names_before=(folder/'names.bin').read_bytes()
    work=Path(tempfile.mkdtemp(prefix='retail-definition-group-picker-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True);cases=0
    for record_name,family in (('OBJECT_FENCE_TEMPLATE','THING_GROUP'),('BUILDING_VILLAGE_TEMPLATE','THING_GROUP'),
                               ('CREATURE_VILLAGER_TEMPLATE','THING_GROUP'),('GROUND_BLEACHEDLONGGRASS','ENGINE_THEME_GROUP')):
        owners=[i for i,r in enumerate(records) if r['name']==record_name];assert len(owners)==1
        owner=owners[0];record=records[owner];key=struct.pack('<I',tag('GroupDef'));assert record['data'].count(key)==1
        start=record['data'].index(key)+4;old_group=struct.unpack_from('<I',record['data'],start)[0]
        choice=next(i for i,r in enumerate(records) if r['definition']==family and i!=old_group and r['name'] and not r['name'].startswith('NULLDEF'))
        decoded=subprocess.run([str(args.tool.resolve()),'defs','decode',str(native),str(repo/'docs/re_reference/def_schema.json'),record_name,'game.bin','--json'],
            cwd=args.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        assert decoded.returncode==0,decoded.stdout+decoded.stderr
        report=json.loads(decoded.stdout);fields=report['fields'];indices=[i for i,f in enumerate(fields) if f['name']=='GroupDef']
        assert len(indices)==1 and fields[indices[0]]['type']=='int32' and fields[indices[0]]['raw_hex']==record['data'][start:start+4].hex()
        for size,scale in (('1440x900',1),('800x600',1.5)):
            case=work/(size+'-'+record_name);case.mkdir();output=case/'output'
            (case/'decode.json').write_text(decoded.stdout,encoding='utf-8')
            lines=['wait_maps','wait_ready',f'set uiscale {scale}',f'set saveroot {output.as_posix()}',
                   'assets_tab 6','wait_definitions','definition_select '+record_name,'frames 3',f'definition_edit {indices[0]}']
            lines+=click('combo_definition_edit_group')+click('input_definition_group_search')
            lines+=['input_text '+records[choice]['name'],'frames 3',f'screenshot {(case/"picker.png").as_posix()}']
            lines+=click('definition_group_option_'+str(choice))+click('btn_definition_save')+click('btn_definition_save_confirm')
            lines+=['wait_ready','wait_definitions','frames 3','assert_state definition_editing 0',
                    f'assert_state definition_group {choice}',f'assert_state definition_selected {owner}','quit']
            script=case/'groups.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            try:
                result=subprocess.run([str(args.gui.resolve()),'--install',str(native),'--size',size,'--auto',str(script)],cwd=args.cwd.resolve(),
                    env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),
                    capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
                (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
                trace=Path(str(script)+'.log').read_text(encoding='utf-8')
                assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6500:]
                after=read_bank(output);expected=dict(record,data=record['data'][:start]+struct.pack('<I',choice)+record['data'][start+4:])
                assert after==records[:owner]+[expected]+records[owner+1:]
                bank=output/'data/CompiledDefs/game.bin';names=bank.parent/'names.bin'
                assert bank.read_bytes()[:13+12*len(records)]==original[:13+12*len(records)]
                assert names.read_bytes()==names_before
                restore=subprocess.run([str(args.forge.resolve()),'restore','--install',str(output)],cwd=args.cwd.resolve(),capture_output=True,text=True,timeout=120)
                (case/'restore.log').write_text(restore.stdout+restore.stderr,encoding='utf-8');assert restore.returncode==0
                for path in (bank,names):assert not path.exists() and not Path(str(path)+'.forge-created').exists()
            finally:
                assert {str(path):digest(path) for path in inputs}==hashes,'native inputs changed'
            cases+=1;print(size,family,len(records),'records, exact group delta and Restore: PASS',flush=True)
    (work/'result.json').write_text(json.dumps({'passed':True,'cases':cases,'records':len(records),'native_hashes_unchanged':hashes},indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
