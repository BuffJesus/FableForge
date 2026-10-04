#!/usr/bin/env python3
"""Choose individual contested scalar fields; independently verify composed bytes."""
import argparse,ctypes,hashlib,json,os,re,subprocess,tempfile,time
from ctypes import wintypes
from pathlib import Path
from test_mod_field_details import write_bank,read_values,tag,FIELDS,RECORD,TYPE

def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    args=parser.parse_args();gui=args.gui.resolve();cwd=args.cwd.resolve()
    work=Path(tempfile.mkdtemp(prefix='mod-field-choices-',dir=repo/'build')).resolve()
    print('Evidence retained at',work,flush=True)
    root=work/'install';write_bank(root,(1,2,3,4));sources=[]
    def cli(label,*arguments):
        result=subprocess.run([str(gui.with_name('forge-tools.exe')),*map(str,arguments)],cwd=cwd,capture_output=True,text=True,timeout=120)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stdout+result.stderr
        return json.loads(result.stdout) if result.stdout.strip().startswith('{') else result.stdout
    for label,values in [('PackA',(10,20,3,4)),('PackB',(11,2,30,5)),('PackC',(1,21,31,4))]:
        source=work/label;write_bank(source,values);sources.append(source)
        cli('add-'+label,'mods','add',root,source,'--name',label)
    original={p:hashlib.sha256(p.read_bytes()).hexdigest() for folder in [root,*sources] for p in folder.rglob('*') if p.is_file()}
    picks=root/'forge_mods_picks.txt'
    report=cli('initial','mods','conflicts',root,'--json')
    fields=report['defs']['conflicts'][0]['field_conflicts']
    layout=hashlib.sha256(json.dumps([3,[[name,'int32',tag(name)] for name in FIELDS]],separators=(',',':')).encode()).hexdigest()
    keys=[]
    for index,field in enumerate(fields):
        key=field['choice_key'];assert key.startswith('field:')
        assert json.loads(key[6:])==['game.bin',RECORD,TYPE,layout,index+1,FIELDS[index]]
        keys.append(key)
    assert len(keys)==3 and len(set(keys))==3
    def reveal(widget):return [f'reveal {widget}','frames 3',f'reveal {widget}','frames 3']
    def choose(field,option):
        winner=f'mod_field_winner_0_{field}'
        return reveal(winner)+[f'click {winner}','frames 3',f'click mod_field_{option}_0_{field}','frames 3']
    def values(a,b,c):return [f'assert_state mods_first_field_value_0 {a}',f'assert_state mods_first_field_value_1 {b}',f'assert_state mods_first_field_value_2 {c}']
    def start(scale):return ['wait_maps','wait_ready',f'set saveroot {root.as_posix()}',f'set uiscale {scale}',
        'mods_tab 1','mods_conflicts','wait_mods','frames 3']
    def run(case,size,lines,unlock=None):
        case.mkdir(exist_ok=True);script=case/'flow.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        with (case/'output.log').open('w',encoding='utf-8') as output:
            process=subprocess.Popen([str(gui),'--install',str(args.install.resolve()),'--size',size,'--auto',str(script)],
                cwd=cwd,stdout=output,stderr=subprocess.STDOUT,env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',
                APPDATA=str(case/'appdata'),LOCALAPPDATA=str(case/'localappdata')))
            try:
                if unlock:
                    deadline=time.monotonic()+150
                    while not (case/'locked.png').exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.01)
                    assert (case/'locked.png').exists(),'locked field checkpoint missing'
                    unlock()
                rc=process.wait(timeout=180)
            finally:
                if process.poll() is None:process.terminate();process.wait(timeout=10)
        log=Path(str(script)+'.log').read_text(encoding='utf-8');assert rc==0 and 'RESULT PASS' in log,log[-6000:]
        return log
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    kernel.CreateFileW.restype=wintypes.HANDLE;kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        picks.unlink(missing_ok=True);case=work/size;case.mkdir()
        lines=['wait_maps','wait_ready',f'set saveroot {root.as_posix()}',f'set uiscale {scale}',
            'mods_tab 1','mods_conflicts','wait_mods','frames 3','mod_pick * PackB','frames 3']
        lines+=reveal('mod_fields_0')+['click mod_fields_0','frames 3']
        for field,source in [(0,0),(1,1),(2,1)]:
            winner=f'mod_field_winner_0_{field}'
            lines+=reveal(winner)+[f'dump_widget {winner}',f'click {winner}','frames 3',
                f'click mod_field_pick_0_{field}_{source}','frames 3']
        lines+=values(10,21,31)+['assert_state mods_picks 4','assert_state mods_unreported_choices 0',
            'assert_state mods_wins_0 1','assert_state mods_wins_1 0','assert_state mods_wins_2 2',
            'mod_pick * -','frames 3']
        lines+=reveal('mods_choices_only')+['click mods_choices_only','frames 3',
            'assert_state mods_conflicts_matching 1','assert_state mods_picks 3',
            'mod_pick * PackA','frames 3']+values(10,21,31)
        lines+=choose(0,'baseline')+values(1,21,31)
        lines+=choose(0,'reset')+values(10,21,31)+['assert_state mods_picks 3']
        # Restore explicit first-part choice via the real dropdown.
        lines+=reveal('mod_field_winner_0_0')+['click mod_field_winner_0_0','frames 3',
            'click mod_field_pick_0_0_0','frames 3','mod_pick * PackB','frames 3']
        lines+=values(10,21,31)+['mods_conflicts','wait_mods','frames 3']+values(10,21,31)
        lines+=reveal('mod_field_value_0_0')+['dump_widget mod_field_value_0_0','mouse_move viewport',
            'clear_toasts',f'screenshot {(case/"selected.png").as_posix()}',
            'mod_pick '+keys[0]+' RemovedPack','frames 3','assert_state mods_unavailable_choices 1']+values(11,21,31)
        lines+=reveal('mod_field_unavailable_0_0')+['dump_widget mod_field_unavailable_0_0','clear_toasts',
            f'screenshot {(case/"unavailable.png").as_posix()}']
        lines+=reveal('input_mod_conflict_search')+['click input_mod_conflict_search','input_text RemovedPack',
            'frames 3','assert_state mods_conflicts_matching 1']
        lines+=reveal('mods_show_all_conflicts')+['click mods_show_all_conflicts','frames 3',
            'mod_pick '+keys[0]+' PackA','frames 3','assert_state mods_unavailable_choices 0','quit']
        log=run(case,size,lines)
        for widget in ['mod_field_winner_0_0','mod_field_winner_0_1','mod_field_winner_0_2','mod_field_value_0_0','mod_field_unavailable_0_0']:
            rect=re.search(r'widget_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)',log)
            clip=re.search(r'widget_clip_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)',log)
            assert rect and clip,widget
            left,top,right,bottom=map(float,rect.groups());cl,ct,cr,cb=map(float,clip.groups())
            assert cl<=left<right<=cr and ct<=top<bottom<=cb,(widget,rect.groups(),clip.groups())
        saved=picks.read_bytes();output=case/'built'
        merged=cli('built-'+size,'mods','build',root,output,'--json')
        assert read_values(output)==(10,21,31,5)
        actual=dict(line.split('\t',1) for line in picks.read_text().splitlines() if line and not line.startswith('#'))
        assert actual=={RECORD:'PackB',keys[0]:'PackA',keys[1]:'PackC',keys[2]:'PackC'}
        assert picks.read_bytes()==saved
        wrong_identity=json.loads(keys[0][6:]);wrong_identity[0]='script.bin'
        wrong_key='field:'+json.dumps(wrong_identity,separators=(',',':'))
        picks.write_bytes(saved+(wrong_key+'\tPackA\n').encode())
        missing=work/('unreported-'+size)
        lines=start(scale)+['assert_state mods_unreported_choices 1','assert_state mods_picks 5']
        lines+=reveal('mod_unreported_choice_0')+['assert_widget mod_unreported_choice_0',
            'mouse_move viewport','clear_toasts',f'screenshot {(missing/"saved-field.png").as_posix()}']
        lines+=reveal('mod_clear_unreported_0')+['click mod_clear_unreported_0','frames 3',
            'assert_state mods_unreported_choices 0','assert_state mods_picks 4','quit']
        run(missing,size,lines)
        assert picks.read_bytes()==saved,'clearing another bank choice changed valid choices'
        locked=work/('locked-'+size)
        handle=kernel.CreateFileW(str(picks),0x80000000,1,None,3,0x80,None)
        assert handle!=wintypes.HANDLE(-1).value
        def unlock():
            nonlocal handle
            assert picks.read_bytes()==saved and not list(root.glob('.forge-mod-picks-*'))
            kernel.CloseHandle(handle);handle=None
        try:
            lines=start(scale)+reveal('mod_fields_0')+['click mod_fields_0','frames 3']
            lines+=choose(0,'baseline')+['assert_state mod_choice_failed 1']+values(10,21,31)
            lines+=reveal('mods_choice_error')+['clear_toasts',f'screenshot {(locked/"locked.png").as_posix()}',
                'frames 300']+choose(0,'baseline')+['assert_state mod_choice_failed 0']+values(1,21,31)+['quit']
            run(locked,size,lines,unlock)
            retry=locked/'built';cli('locked-built-'+size,'mods','build',root,retry,'--json')
            assert read_values(retry)==(1,21,31,5)
        finally:
            if handle is not None:kernel.CloseHandle(handle)
        picks.write_bytes(saved)
        # Same values/tags but a changed type schema must not reuse scalar keys.
        schema=json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))
        schema[TYPE]['fields'][0]['type']='uint32'
        changed=case/'changed-schema.json';changed.write_text(json.dumps(schema),encoding='utf-8')
        altered=case/'changed-layout-output'
        newer=cli('layout-'+size,'mods','build',root,altered,'--fields',changed,'--json')
        assert read_values(altered)==(11,21,30,5)
        assert all(field['choice_key'] not in keys for field in newer['defs']['conflicts'][0]['field_conflicts'])
        assert picks.read_bytes()==saved
        print(size,'actual scalar choices, record precedence/reset, badges, saved-only filter, exact merged bytes and layout guard: PASS',flush=True)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==sha for p,sha in original.items())
    print('Sources/baseline/order remain byte-exact; choice refresh and builds preserve picks: PASS',flush=True)

if __name__=='__main__':main()
