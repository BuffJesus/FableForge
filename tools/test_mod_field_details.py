#!/usr/bin/env python3
"""Check reported field conflicts, choice scope and independent merged values."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

FIELDS=('RandomVillagerMax','RandomGuardMax','RandomBanditMax','RegionDangerLevel')
RECORD='REGION_SCRIPT_OVERRIDE_TEST'
TYPE='CRegionScriptDef'

def tag(name):
    value=0
    for byte in name.encode('ascii'):
        value^=byte
        for _ in range(8): value=(value>>1)^(0xEDB88320 if value&1 else 0)
    return value

def write_bank(root,values):
    defs=root/'data/CompiledDefs';defs.mkdir(parents=True)
    names=bytearray(); offsets=[]
    for name in (TYPE,RECORD):
        offsets.append(len(names)+4)
        names+=struct.pack('<I',zlib.crc32(name.encode('ascii')))+name.encode('ascii')+b'\0'
    header=struct.pack('<5I',0,0xA8E36C34,2,len(names)-4,0)
    (defs/'names.bin').write_bytes(header+names)
    payload=b'\x01\0\x01'+b''.join(struct.pack('<Ii',tag(name),value) for name,value in zip(FIELDS,values))
    chunk=zlib.compress(struct.pack('<H',2)+payload)
    bank=bytes(9)+struct.pack('<IiiiI5I',1,*offsets,0,2,0,0,1,len(chunk),len(chunk))+chunk
    (defs/'game.bin').write_bytes(bank)

def read_values(root):
    bank=(root/'data/CompiledDefs/game.bin').read_bytes()
    count,=struct.unpack_from('<I',bank,9);assert count==1
    chunks,=struct.unpack_from('<I',bank,25);assert chunks==2
    inflated=zlib.decompress(bank[49:])
    start,=struct.unpack_from('<H',inflated);assert inflated[start:start+3]==b'\x01\0\x01'
    values=[]
    for index,name in enumerate(FIELDS):
        actual,value=struct.unpack_from('<Ii',inflated,start+3+index*8)
        assert actual==tag(name)
        values.append(value)
    return tuple(values)

def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='mod-field-details-',dir=repo/'build')).resolve()
    assert work.parent==(repo/'build').resolve()
    print('Evidence retained at',work,flush=True)
    root=work/'install';write_bank(root,(1,2,3,4))
    sources=[]
    for label,values in [('PackA',(10,20,3,4)),('PackB',(11,2,30,5)),('PackC',(1,21,31,4)),
                         ('PackD',(1,2,3,5))]:
        pack=work/label;write_bank(pack,values);sources.append(pack)
    baseline={p:hashlib.sha256(p.read_bytes()).hexdigest() for source in (root,*sources)
        for p in source.rglob('*.bin')}
    tool=repo/'build/forge-tools.exe'
    def cli(label,*args):
        result=subprocess.run([str(tool),*map(str,args)],cwd=repo,capture_output=True,text=True,timeout=120)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stdout+result.stderr
        return result.stdout
    for pack in sources: cli('add-'+pack.name,'mods','add',root,pack,'--name',pack.name)
    report=json.loads(cli('report','mods','conflicts',root,'--json'))
    row=report['defs']['conflicts'][0]
    assert row['record']==RECORD and row['field_merged']
    assert row['mods']==['PackA','PackB','PackC','PackD']
    assert [(f['field'],f['mods'],f['winner']) for f in row['field_conflicts']]==[
        (FIELDS[0],['PackA','PackB'],'PackB'),(FIELDS[1],['PackA','PackC'],'PackC'),
        (FIELDS[2],['PackB','PackC'],'PackC')]
    assert report['defs']['summary']['auto_merged_fields']==1
    assert [(f['part_index'],f['type'],f['base_value'],f['values']) for f in row['field_conflicts']]==[
        (1,'int32','1',[{'mod':'PackA','value':'10'},{'mod':'PackB','value':'11'}]),
        (2,'int32','2',[{'mod':'PackA','value':'20'},{'mod':'PackC','value':'21'}]),
        (3,'int32','3',[{'mod':'PackB','value':'30'},{'mod':'PackC','value':'31'}])]
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir()
        lines=['wait_maps','wait_ready',f'set saveroot {root.as_posix()}',f'set uiscale {scale}',
            'mods_tab 1','frames 2','mods_conflicts','wait_mods','frames 2',
            'assert_state mods_conflicts 1','assert_state mods_field_conflicts 3',
            'assert_state mods_count 4','assert_state mods_wins_3 0','assert_state mods_losses_3 0',
            'reveal mod_fields_0','frames 3','reveal mod_fields_0','frames 3','click mod_fields_0','frames 3']
        for index,winner in enumerate(('PackB','PackC','PackC')):
            lines += [f'assert_state mods_first_field_{index} {FIELDS[index]}',
                f'assert_state mods_first_field_winner_{index} {winner}']
        for index,value in enumerate((11,21,31)):
            lines += [f'assert_state mods_first_field_base_{index} {index+1}',
                f'assert_state mods_first_field_value_{index} {value}']
        for index,(wins,losses) in enumerate(((0,2),(1,1),(2,0))):
            lines += [f'assert_state mods_wins_{index} {wins}',f'assert_state mods_losses_{index} {losses}']
        lines += ['reveal mod_wins_1','frames 3','reveal mod_wins_1','frames 3',
            'assert_widget mod_wins_1','assert_widget mod_losses_1','mouse_move mod_wins_1','frames 80',
            'clear_toasts',f'screenshot {(case/"mixed-badges.png").as_posix()}']
        lines += ['reveal input_mod_conflict_search','frames 3','click input_mod_conflict_search',
            'input_text rAnDoMbAnDiT','frames 3','assert_state mods_conflicts_matching 1',
            'reveal mods_show_all_conflicts','frames 3','click mods_show_all_conflicts','frames 2']
        lines += ['clear_toasts',f'screenshot {(case/"load-order.png").as_posix()}',
            'reveal mod_field_0_2','frames 3','assert_widget mod_field_0_2',
            f'screenshot {(case/"last-field.png").as_posix()}','mod_pick * PackA','frames 2']
        for index,winner in enumerate(('PackA','PackA','PackC')):
            lines += [f'assert_state mods_first_field_winner_{index} {winner}']
        for index,value in enumerate((10,20,31)):
            lines += [f'assert_state mods_first_field_value_{index} {value}']
        for index,(wins,losses) in enumerate(((2,0),(0,2),(1,1))):
            lines += [f'assert_state mods_wins_{index} {wins}',f'assert_state mods_losses_{index} {losses}']
        lines += ['mod_pick * PackD','frames 2','assert_state mods_first_winner PackD',
            'assert_state mods_wins_3 0','assert_state mods_losses_3 0']
        for index,(winner,value) in enumerate(zip(('PackB','PackC','PackC'),(11,21,31))):
            lines += [f'assert_state mods_first_field_winner_{index} {winner}',
                f'assert_state mods_first_field_value_{index} {value}']
        lines += ['reveal mod_field_0_2','frames 3',
            f'screenshot {(case/"independent-choice.png").as_posix()}','mod_pick * PackA','frames 2']
        lines += ['reveal mod_field_0_2','frames 3',
            f'screenshot {(case/"picked.png").as_posix()}',
            'reveal mod_winner_0','frames 3','reveal mod_winner_0','frames 3',
            'click mod_winner_0','frames 2','assert_widget mod_vanilla_0',
            f'screenshot {(case/"vanilla-choice.png").as_posix()}',
            'click mod_vanilla_0','frames 2']
        for index in range(3): lines += [f'assert_state mods_first_field_winner_{index} vanilla']
        for index in range(3): lines += [f'assert_state mods_first_field_value_{index} {index+1}']
        for index in range(3):
            lines += [f'assert_state mods_wins_{index} 0',f'assert_state mods_losses_{index} 2']
        lines += ['mod_pick * -','frames 2']
        for index,winner in enumerate(('PackB','PackC','PackC')):
            lines += [f'assert_state mods_first_field_winner_{index} {winner}']
        for index,(wins,losses) in enumerate(((0,2),(1,1),(2,0))):
            lines += [f'assert_state mods_wins_{index} {wins}',f'assert_state mods_losses_{index} {losses}']
        lines += ['mods_conflicts','wait_mods','frames 2','assert_state mods_field_conflicts 3','quit']
        script=case/'fields.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(args.install),
            '--size',size,'--auto',str(script)],cwd=repo,capture_output=True,text=True,timeout=240,
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        log=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in log,log[-5000:]
        print(size,'field rows, actual changers, vanilla scope and clearing picks: PASS',flush=True)
    picks=root/'forge_mods_picks.txt'
    for choice,expected in [('',(11,21,31,5)),('PackA',(10,20,31,5)),
                            ('PackD',(11,21,31,5)),('vanilla',(1,2,3,5))]:
        if choice: picks.write_text(RECORD+'\t'+choice+'\n',encoding='utf-8')
        elif picks.exists(): picks.unlink()
        output=work/('merged-'+(choice or 'order'))
        cli('build-'+(choice or 'order'),'mods','build',root,output,'--json')
        assert read_values(output)==expected
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==hash for p,hash in baseline.items())
    print('Independent merged-bank values and source-byte preservation: PASS',flush=True)

if __name__=='__main__': main()
