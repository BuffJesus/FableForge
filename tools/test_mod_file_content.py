#!/usr/bin/env python3
"""Compare contested raw file identities, refreshed source bytes and label picks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
from test_mod_field_details import write_bank


def digest(data): return hashlib.sha256(data).hexdigest()


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    args=parser.parse_args();gui=args.gui.resolve();cwd=args.cwd.resolve()
    work=Path(tempfile.mkdtemp(prefix='mod-file-content-',dir=repo/'build')).resolve()
    print('Evidence retained at',work,flush=True)
    root=work/'install';write_bank(root,(1,2,3,4))
    base=b'baseline bytes\x00\xff';a=bytes(range(64));b=bytes(reversed(range(64)));updated=a[:-1]+b'\xff'
    baseline=root/'data/Misc/0_shared.bin';baseline.parent.mkdir(parents=True);baseline.write_bytes(base)
    (baseline.parent/'2_identical.bin').write_bytes(b'identical bytes')
    labels=['PackA','PackB','CopyA With A Long Compatibility And Appearance Description']
    sources=[]
    def cli(label,*arguments):
        result=subprocess.run([str(gui.with_name('forge-tools.exe')),*map(str,arguments)],
            cwd=cwd,capture_output=True,text=True,timeout=120)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stdout+result.stderr
        return result.stdout
    for index,label in enumerate(labels):
        source=work/('source'+str(index));folder=source/'data/Misc';folder.mkdir(parents=True)
        (folder/'0_shared.bin').write_bytes(b if index==1 else a)
        if index<2:(folder/'1_added.bin').write_bytes(bytes([index])*7)
        (folder/'2_identical.bin').write_bytes(b'identical bytes')
        (source/'forge_pack.json').write_text(json.dumps({'version':1,'name':label,'models':[],'groundThemes':[]}),encoding='utf-8')
        cli('add'+str(index),'mods','add',root,source,'--name',label);sources.append(source)
    picks=root/'forge_mods_picks.txt';original_pick=b'file:data/misc/0_shared.bin\tPackA\n';picks.write_bytes(original_pick)
    original={p:digest(p.read_bytes()) for folder in [root,*sources] for p in folder.rglob('*') if p.is_file() and p!=picks}
    def report(label):return json.loads(cli(label,'mods','conflicts',root,'--json'))
    def audit(report,expected_a):
        rows=report['files']['contested'];assert len(rows)==2
        shared,added=rows
        assert shared['path']=='data/misc/0_shared.bin' and added['path']=='data/misc/1_added.bin'
        assert shared['file_content']['baseline']=={'present':True,'bytes':len(base),'sha256':digest(base)}
        assert added['file_content']['baseline']=={'present':False}
        versions=shared['file_content']['versions'];assert [v['mod'] for v in versions]==labels
        for version,data in zip(versions,[expected_a,b,a]):
            assert version=={'mod':version['mod'],'present':True,'bytes':len(data),'sha256':digest(data)}
        assert not any(r['path'].endswith('2_identical.bin') for r in rows)
    audit(report('initial'),a)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir();picks.write_bytes(original_pick)
        source_file=sources[0]/'data/Misc/0_shared.bin';source_file.write_bytes(a)
        checkpoint=case/'checkpoint.png'
        def reveal(widget):return [f'reveal {widget}','frames 3',f'reveal {widget}','frames 3']
        lines=['wait_maps','wait_ready',f'set saveroot {root.as_posix()}',f'set uiscale {scale}',
               'mods_tab 1','mods_conflicts','wait_mods','frames 3','assert_state mods_conflicts 2',
               'assert_state mods_file_content_rows 2','assert_state mods_first_file_base_present 1',
               f'assert_state mods_first_file_base_sha256 {digest(base)}',
               f'assert_state mods_first_file_selected_sha256 {digest(a)}']
        lines+=reveal('mod_file_content_0')+['dump_widget mod_file_content_0','click mod_file_content_0','frames 3']
        for index in range(3):
            widget=f'mod_file_source_0_{index}'
            lines+=reveal(widget)+[f'assert_widget {widget}',f'dump_widget {widget}','mouse_move viewport',
                                  'clear_toasts',f'screenshot {(case/("source"+str(index)+".png")).as_posix()}']
        lines+=['mod_pick * vanilla','frames 3',f'assert_state mods_first_file_selected_sha256 {digest(base)}',
                'mod_pick * PackB','frames 3',f'assert_state mods_first_file_selected_sha256 {digest(b)}',
                'mod_pick * -','frames 3','dump_state',
                f'assert_state mods_first_file_selected_sha256 {digest(a)}','mod_pick * PackA','frames 3',
                f'screenshot {checkpoint.as_posix()}','frames 300',
                f'assert_state mods_first_file_selected_sha256 {digest(a)}',
                'mods_conflicts','wait_mods','frames 3',f'assert_state mods_first_file_selected_sha256 {digest(updated)}']
        lines+=reveal('mod_file_source_0_0')+['mouse_move viewport','clear_toasts',f'screenshot {(case/"updated.png").as_posix()}']
        lines+=reveal('mod_file_content_1')+['click mod_file_content_1','frames 3']
        lines+=reveal('mod_file_base_1')+['assert_widget mod_file_base_1','dump_widget mod_file_base_1',
                                       'mouse_move viewport','clear_toasts',f'screenshot {(case/"absent.png").as_posix()}','quit']
        script=case/'flow.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        with (case/'output.log').open('w',encoding='utf-8') as output:
            process=subprocess.Popen([str(gui),'--install',str(args.install.resolve()),'--size',size,'--auto',str(script)],
                cwd=cwd,stdout=output,stderr=subprocess.STDOUT,
                env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'appdata'),LOCALAPPDATA=str(case/'localappdata')))
            try:
                deadline=time.monotonic()+90
                while not checkpoint.exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.01)
                assert checkpoint.exists(),'source-update checkpoint missing'
                saved=picks.read_bytes();source_file.write_bytes(updated)
                assert len(a)==len(updated)
                rc=process.wait(timeout=120)
            finally:
                if process.poll() is None:process.terminate();process.wait(timeout=10)
        log=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc==0 and 'RESULT PASS' in log,log[-5000:]
        assert 'mods_first_winner='+labels[2] in log
        assert picks.read_bytes()==saved,'refresh rewrote the saved choice'
        for widget in ['mod_file_content_0','mod_file_source_0_0','mod_file_source_0_1','mod_file_source_0_2','mod_file_base_1']:
            rect=re.search(r'widget_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)',log);assert rect,widget
            left,top,right,bottom=map(float,rect.groups());width,height=map(int,size.split('x'))
            assert 0<=left<right<=width and 0<=top<bottom<=height,(widget,rect.groups())
            clip=re.search(r'widget_clip_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)',log);assert clip,widget
            cl,ct,cr,cb=map(float,clip.groups())
            assert cl<=left<right<=cr and ct<=top<bottom<=cb,(widget,'clipped',rect.groups(),clip.groups())
        audit(report('updated-'+size),updated)
        built=work/('built-'+size);cli('build-'+size,'mods','build',root,built,'--json')
        assert (built/'data/Misc/0_shared.bin').read_bytes()==updated
        assert (built/'data/Misc/1_added.bin').read_bytes()==bytes([1])*7
        assert not (built/'data/Misc/2_identical.bin').exists()
        source_file.write_bytes(a)
        assert all(digest(p.read_bytes())==h for p,h in original.items())
        print(size,'source snapshots, absent/identical versions, same-size refresh, picks and composed bytes: PASS',flush=True)
    (work/'result.json').write_text(json.dumps({'passed':True,'sizes':['1440x900','800x600'],'original_inputs_unchanged':True},indent=2),encoding='utf-8')


if __name__=='__main__':main()
