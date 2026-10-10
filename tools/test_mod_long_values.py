#!/usr/bin/env python3
"""Long typed conflict values must remain readable in both panel sizes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from test_mod_field_details import tag, write_bank

FIELDS = ('TimeOfSummon', 'BaseSummonCreatureName', 'FadeInTime', 'FadeOutTime',
          'ResetCreatureManaCost', 'SummonTintColour', 'EyeLeftDummyPointName', 'EyeRightDummyPointName')
RECORD = 'LONG_VALUE_COMPARISON_TEST'
TYPE = 'SPECIAL_ABILITIES_SUMMON_SPELL_DEF'

def value_widget(part, field):
    if part.startswith('source_'): return f'mod_field_source_0_{field}_{part[-1]}'
    return f'mod_field_{part}_0_{field}'


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    args = parser.parse_args()
    gui, cwd = args.gui.resolve(), args.cwd.resolve()
    work = Path(tempfile.mkdtemp(prefix='mod-long-values-', dir=repo/'build')).resolve()
    assert work.parent == (repo/'build').resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'
    sources = []
    expected = {}
    for index,label in enumerate(('Baseline','LongPackA','LongPackB')):
        folder = root if index == 0 else work/label
        vector = struct.pack('<I16I',16,*range(1000000000+index*100,1000000016+index*100))
        text = (f'{label} eye dummy point with a long value and exact byte escapes '.encode('ascii')+
            b'\xff\x01"\\ end\0')
        values = (struct.pack('<I',0), struct.pack('<i',-1 if index == 0 else index*256),
                  0, 0, 0, vector, text, b'RIGHT_EYE\0')
        write_bank(folder,values,definition=TYPE,record=RECORD,fields=FIELDS)
        expected[label] = tuple(struct.pack('<i',v) if isinstance(v,int) else v for v in values)
        if index: sources.append(folder)
    baseline = {p:hashlib.sha256(p.read_bytes()).hexdigest()
        for source in (root,*sources) for p in source.rglob('*.bin')}
    tool = gui.with_name('forge-tools.exe')
    def cli(label,*arguments):
        result = subprocess.run([str(tool),*map(str,arguments)],cwd=cwd,capture_output=True,text=True,timeout=60)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode == 0,result.stdout+result.stderr
        return result.stdout
    for pack in sources: cli('add-'+pack.name,'mods','add',root,pack,'--name',pack.name)
    report = json.loads(cli('report','mods','conflicts',root,'--json'))
    fields = report['defs']['conflicts'][0]['field_conflicts']
    assert [(f['field'],f['type']) for f in fields] == [
        (FIELDS[1],'CDefString'),(FIELDS[5],'Vector_J'),(FIELDS[6],'CCharString')]
    assert fields[0]['base_value'] == '-1'
    assert [v['value'] for v in fields[0]['values']] == ['256','512']
    assert '\\xff\\x01\\"\\\\' in fields[2]['values'][1]['value']
    output = work/'merged'
    cli('merge','mods','build',root,output,'--json')
    import zlib
    inflated = zlib.decompress((output/'data/CompiledDefs/game.bin').read_bytes()[49:])
    start, = struct.unpack_from('<H',inflated)
    wanted = b'\x01\0\x01'+b''.join(struct.pack('<I',tag(name))+value
        for name,value in zip(FIELDS,expected['LongPackB']))
    assert inflated[start:] == wanted
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case = work/size
        case.mkdir()
        lines = ['wait_maps','wait_ready',f'set saveroot {root.as_posix()}',f'set uiscale {scale}',
            'mods_tab 1','frames 2','mods_conflicts','wait_mods','frames 2','assert_state mods_field_conflicts 3',
            'reveal mod_fields_0','frames 3','reveal mod_fields_0','frames 3','click mod_fields_0','frames 3']
        for field in range(3):
            for part in ('value','base','source_0','source_1'):
                widget = value_widget(part,field)
                lines += [f'reveal {widget}','frames 3',f'reveal {widget}','frames 3',
                    f'assert_widget {widget}',f'dump_widget {widget}','mouse_move viewport','frames 3',
                    'clear_toasts',f'screenshot {(case/(str(field)+"-"+part+".png")).as_posix()}']
        lines += ['quit']
        script = case/'long-values.txt'
        script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        result = subprocess.run([str(gui),'--install',str(args.install),
            '--size',size,'--auto',str(script)],cwd=cwd,capture_output=True,text=True,timeout=180,
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log,log[-4000:]
        for field in range(3):
            for part in ('value','base','source_0','source_1'):
                widget = value_widget(part,field)
                bounds = re.search(r'widget_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)',log)
                assert bounds,widget
                left,top,right,bottom = map(float,bounds.groups())
                png = (case/(str(field)+'-'+part+'.png')).read_bytes()
                width,height = struct.unpack_from('>II',png,16)
                assert 0 <= left < right <= width and 0 <= top < bottom <= height,(widget,bounds.groups())
                if field > 0:
                    assert bottom-top >= 20*scale,(widget,'long value did not wrap')
        print(size,'reference values and wrapped vector/text comparisons: PASS',flush=True)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p,digest in baseline.items())
    print('Exact merged payload and unchanged source banks: PASS',flush=True)


if __name__ == '__main__':
    main()
