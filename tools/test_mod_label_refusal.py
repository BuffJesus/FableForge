#!/usr/bin/env python3
"""Ambiguous mod labels refuse before publication and preserve legacy recovery."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--gui', type=Path)
    parser.add_argument('--install', type=Path)
    args = parser.parse_args()
    if args.gui and not args.install: parser.error('--gui requires --install')
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-label-refusal-', dir=args.work_root)).resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1,2,3,4))
    relative = Path('data/Misc/shared.bin')
    (root/relative).parent.mkdir(parents=True); (root/relative).write_bytes(b'BASE')
    source = work/'vanilla'; (source/relative).parent.mkdir(parents=True)
    (source/relative).write_bytes(b'MOD!')
    counter = 0; checks = []
    def run(*arguments, good=True):
        nonlocal counter
        counter += 1
        result = subprocess.run([str(args.tool.resolve()), *map(str,arguments)],
            capture_output=True, text=True, encoding='utf-8', timeout=120)
        (work/f'command-{counter:03d}.json').write_text(json.dumps({
            'arguments':list(map(str,arguments)), 'returncode':result.returncode,
            'stdout':result.stdout, 'stderr':result.stderr},indent=2),encoding='utf-8')
        assert (result.returncode == 0) == good, (arguments,result.stderr)
        return result
    for name in ('vanilla','VaNiLlA',' Pack','Pack ','Pack\tA','Pack\nA','Pack\rA','Pack\x7f'):
        before = snapshot(root); source_before = snapshot(source)
        run('mods','add',root,source,'--name',name,good=False)
        assert snapshot(root) == before and snapshot(source) == source_before
        checks.append({'invalid_registration':name,'preserved':True})
    before = snapshot(root); run('mods','add',root,source,good=False)
    assert snapshot(root) == before
    checks.append({'inferred_reserved_registration':True})
    output = work/'output'; output.mkdir(); (output/'sentinel').write_bytes(b'unrelated')
    before = snapshot(root); out_before = snapshot(output)
    run('mods','merge',root,output,'--with',source,good=False)
    assert snapshot(root) == before and snapshot(output) == out_before
    checks.append({'inferred_reserved_bare_merge':True})
    run('mods','add',root,source,'--name','Vanilla Plus')
    picks = work/'picks.txt'; picks.write_text('file:data/misc/shared.bin\tVanilla Plus\n')
    run('mods','build',root,output,'--picks',picks)
    assert (output/relative).read_bytes() == b'MOD!'
    picks.write_text('file:data/misc/shared.bin\tvanilla\n')
    run('mods','build',root,output,'--picks',picks)
    assert (output/relative).read_bytes() == b'BASE'
    checks.append({'valid_name_and_baseline_choices_distinct':True})
    run('mods','deploy',root)
    order_path = root/'forge_mods.json'
    valid = json.loads(order_path.read_text(encoding='utf-8'))
    for name in ('vanilla','Pack\nInjected',' Pack',''):
        order = json.loads(json.dumps(valid)); order['mods'][0]['name'] = name
        order_path.write_text(json.dumps(order),encoding='utf-8')
        for command in (('mods','build',root,output),('mods','conflicts',root,'--json'),('mods','deploy',root)):
            before = snapshot(root); out_before = snapshot(output)
            run(*command,good=False)
            assert snapshot(root) == before and snapshot(output) == out_before
            checks.append({'legacy_invalid_label':name,'route':command[1],'staged_install_preserved':True})
        run('mods','list',root)
    order_path.write_text(json.dumps(valid),encoding='utf-8')
    run('mods','undeploy',root)
    baseline = snapshot(root)
    legacy = json.loads(json.dumps(valid)); legacy['mods'][0]['name'] = 'vanilla'
    order_path.write_text(json.dumps(legacy),encoding='utf-8')
    run('mods','disable',root,'0')
    run('mods','conflicts',root,'--json')
    run('mods','remove',root,'0')
    assert json.loads(order_path.read_text(encoding='utf-8'))['mods'] == []
    assert {k:v for k,v in snapshot(root).items() if k != 'forge_mods.json'} == {k:v for k,v in baseline.items() if k != 'forge_mods.json'}
    checks.append({'disabled_legacy_read_remove_recovery':True})
    if args.gui:
        def button(widget):
            return [f'reveal {widget}','frames 3',f'reveal {widget}','frames 3',f'click {widget}','frames 3']
        for size, scale in [('1440x900',1),('800x600',1.5)]:
            view = work/size; view.mkdir()
            script = view/'labels.txt'
            lines = ['wait_maps','wait_ready',f'set saveroot {root.as_posix()}',f'set uiscale {scale}',
                     'mods_tab 1','frames 3']
            lines += button('input_mod_path')+[f'input_text {source.as_posix()}']
            lines += button('input_mod_name')+['input_text vanilla']+button('btn_mod_add')
            lines += ['assert_state mods_count 0','assert_log Mod name vanilla is reserved',
                      f'screenshot {(view/"refused.png").as_posix()}']
            # The refused form keeps its source/name; inserting valid text retries it.
            lines += button('input_mod_name')+['input_text Plus']+button('btn_mod_add')
            lines += ['assert_state mods_count 1','assert_log mods: added "',
                      'mod_remove 0','assert_state mods_count 0',
                      f'screenshot {(view/"recovered.png").as_posix()}','quit']
            script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            result = subprocess.run([str(args.gui.resolve()),'--install',str(args.install.resolve()),
                '--size',size,'--auto',str(script)],cwd=repo,capture_output=True,text=True,encoding='utf-8',timeout=180,
                env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(view/'appdata'),LOCALAPPDATA=str(view/'localappdata')))
            (view/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
            log = Path(str(script)+'.log').read_text(encoding='utf-8')
            assert result.returncode == 0 and 'RESULT PASS' in log,log[-4000:]
            assert json.loads(order_path.read_text(encoding='utf-8'))['mods'] == []
            checks.append({'size':size,'actual_add_refusal_retained_form_and_retry':True})
    from test_mod_tng_choice_contributors import tng, values, KEY
    tng(root)
    first = work/'first'/'Pack'; tng(first,20)
    for leaf in ('Pack','pAcK'):
        second = work/('second-'+leaf)/leaf; tng(second,30)
        for choice in ('', KEY+'\tPack\n', KEY+'\tvanilla\n'):
            picks.write_text(choice)
            before = snapshot(root); out_before = snapshot(output)
            sources_before = {str(p):snapshot(p) for p in (first,second)}
            run('mods','merge',root,output,'--with',first,second,'--picks',picks,good=False)
            assert snapshot(root) == before and snapshot(output) == out_before
            assert sources_before == {str(p):snapshot(p) for p in (first,second)}
            checks.append({'duplicate_inferred_label':leaf,'choice':choice,'refused_preserved':True})
    second = work/'second-Pack'/'Pack'
    run('mods','add',root,first,'--name','First Pack')
    run('mods','add',root,second,'--name','Second Pack')
    valid = json.loads(order_path.read_text(encoding='utf-8'))
    for choice, expected in [('',30),(KEY+'\tFirst Pack\n',20),(KEY+'\tSecond Pack\n',30),(KEY+'\tvanilla\n',10)]:
        picks.write_text(choice); run('mods','build',root,output,'--picks',picks)
        assert values(output)[123456] == expected
        checks.append({'distinct_registration_choice':choice,'independent_health':expected})
    before_stage = {k:v for k,v in snapshot(root).items() if k != 'forge_mods.json'}
    run('mods','deploy',root)
    for alias in ('First Pack','fIrSt PaCk'):
        invalid = json.loads(json.dumps(valid)); invalid['mods'][1]['name'] = alias
        order_path.write_text(json.dumps(invalid),encoding='utf-8')
        for command in (('mods','build',root,output),('mods','conflicts',root,'--json'),('mods','deploy',root)):
            before = snapshot(root); out_before = snapshot(output)
            failure = run(*command,good=False)
            assert 'Mod labels must be distinct' in failure.stderr
            assert snapshot(root) == before and snapshot(output) == out_before
            checks.append({'duplicate_legacy_label':alias,'route':command[1],'staged_preserved':True})
        run('mods','list',root)
    run('mods','disable',root,'1'); run('mods','conflicts',root,'--json')
    run('mods','remove',root,'1')
    run('mods','undeploy',root)
    assert values(root)[123456] == 10
    assert {k:v for k,v in snapshot(root).items() if k != 'forge_mods.json'} == before_stage
    checks.append({'duplicate_legacy_disable_remove_and_exact_baseline':True})
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2))
    print('PASS:',len(checks),'registration, choice identity, staged preservation and recovery checks')


if __name__ == '__main__': main()
