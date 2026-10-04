#!/usr/bin/env python3
"""Exercise original three sources via actual GUI Add and capture the guide."""
import argparse, json, os, shutil, subprocess, time
from pathlib import Path
from test_mod_bank_refusal import snapshot


def main():
    repo = Path(__file__).resolve().parents[1]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('evidence', type=Path)
    p.add_argument('--corpus', type=Path, required=True)
    p.add_argument('--install', type=Path, required=True)
    args = p.parse_args()
    work = args.evidence.resolve()
    assert work.parent == (repo/'build').resolve() and work.name.startswith('complete-aeon-controller-')
    assert json.loads((work/'result.json').read_text())['passed']
    root = work/'baseline'
    assert not (root/'forge_stage_manifest.json').exists()
    # Complete the owned baseline's GUI preview assets; the live install is read only.
    shutil.copytree(args.install/'data/graphics', root/'data/graphics', dirs_exist_ok=True)
    original = {k:v for k,v in snapshot(root).items() if k != 'forge_mods.json'}
    sources = [args.corpus/'AeonEdition/Data', args.corpus/'ControllerSupportFMP/ControllerSupport.fmp',
               args.corpus/'FableControllerSupport/Mods/FableControllerSupport']
    shots = repo/'docs/walkthrough/aeon-controller'
    def button(name, wait=False):
        lines = [f'reveal {name}','frames 3',f'reveal {name}','frames 3',f'click {name}','frames 3']
        return lines+(['wait_mods','wait_ready','frames 3'] if wait else [])
    for size,scale in [('1440x1000',1),('800x600',1.5)]:
        case = work/(f'gui-{size}-{time.time_ns()}'); case.mkdir()
        if (root/'forge_mods.json').exists(): (root/'forge_mods.json').unlink()
        script = case/'guide.txt'
        # Never use preexisting guide illustrations as deployment checkpoints.
        # Publish captures into docs only after the entire owned GUI run passes.
        images = case
        lines = ['wait_maps','wait_ready','setup 0',f'set uiscale {scale}','mods_tab 1','frames 4']
        for index, source in enumerate(sources):
            lines += button('input_mod_path')+[f'text_input {source.as_posix()}','frames 3']
            lines += button('btn_mod_add')+[f'assert_state mods_count {index+1}']
        lines += ['assert_state mod_action_failed 0','clear_toasts',
                  f'screenshot {(images/"01-order.png").as_posix()}']
        lines += button('input_mod_path_browse')+[f'screenshot {(images/"02-add.png").as_posix()}',
                  'key_down Escape','key_up Escape','frames 3']
        lines += button('btn_mods_conflicts',True)+['assert_state mod_action_failed 0',
                  'assert_log mods conflicts: 0 conflict(s) across 3 mod(s)',
                  'reveal mod_install_summary','frames 3','assert_widget mod_install_summary',
                  f'screenshot {(images/"03-check.png").as_posix()}']
        lines += button('btn_mods_deploy',True)+['assert_log mods deploy: done',
                  'assert_state mod_action_failed 0','clear_toasts',
                  f'screenshot {(images/"04-deploy.png").as_posix()}','frames 300']
        lines += button('btn_mods_conflicts',True)+['assert_state mod_action_failed 0']
        lines += button('btn_mods_deploy',True)+['assert_state mod_action_failed 0']
        lines += button('btn_mods_undeploy',True)+['assert_log mods undeploy: done',
                  'assert_state mod_action_failed 0','quit']
        script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        with (case/'output.log').open('w',encoding='utf-8') as log:
            proc = subprocess.Popen([str(repo/'build/FableForge.exe'),'--install',str(root),
                '--auto',str(script),'--size',size],cwd=repo,stdout=log,stderr=subprocess.STDOUT,
                env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'appdata'),
                         LOCALAPPDATA=str(case/'localappdata')))
            try:
                deadline = time.monotonic()+1800
                checkpoint = images/'04-deploy.png'
                while not checkpoint.exists() and proc.poll() is None and time.monotonic()<deadline:
                    time.sleep(.1)
                assert checkpoint.exists(), 'GUI did not reach successful deployment'
                for rel in ('data/CompiledDefs/game.bin','data/CompiledDefs/names.bin',
                            'Mods/FableControllerSupport/FableControllerSupport.dll','Mods.ini'):
                    assert (root/rel).read_bytes() == (work/'built'/rel).read_bytes(), rel
                assert not (root/'Mods/FableControllerSupport/Data').exists()
                rc = proc.wait(timeout=max(1,deadline-time.monotonic()))
            finally:
                if proc.poll() is None: proc.terminate(); proc.wait(timeout=10)
        trace = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc == 0 and 'RESULT PASS' in trace, trace[-5000:]
        order = json.loads((root/'forge_mods.json').read_text())['mods']
        assert len(order)==3 and order[0]['install_data']
        assert Path(order[0]['source'])==sources[0].parent
        assert {k:v for k,v in snapshot(root).items() if k!='forge_mods.json'} == original
        if scale == 1:
            for name in ('01-order.png','02-add.png','03-check.png','04-deploy.png'):
                shutil.copyfile(case/name, shots/name)
        print(size,'original three sources, actual Add/check/deploy/redeploy/undeploy: PASS',flush=True)
    (work/'gui-result.json').write_text(json.dumps({'passed':True,'sizes':['1440x1000','800x600'],
                                                  'gameplay_tested':False},indent=2))


if __name__ == '__main__': main()
