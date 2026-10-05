#!/usr/bin/env python3
"""Windows Unicode file aliases keep load order, picks, reviews and output ownership."""
import argparse
import hashlib
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
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--complete-data',action='store_true')
    parser.add_argument('--gui',type=Path)
    parser.add_argument('--install',type=Path)
    args = parser.parse_args()
    if os.name != 'nt': parser.error('requires real Windows filesystem case behavior')
    if args.gui and not args.install: parser.error('--gui requires --install')
    args.work_root.mkdir(parents=True,exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-unicode-file-identity-',dir=args.work_root)).resolve()
    print('Evidence retained at',work,flush=True)
    root = work/'install'; write_bank(root,(1,2,3,4))
    upper = work/'UpperSource'; lower = work/'LowerSource'
    aliases = [(Path('data/Misc/Älias.bin'),Path('DATA/MISC/äLIAS.BIN')),
               (Path('data/Misc/Жfile.bin'),Path('data/Misc/жfile.bin'))]
    distinct = [(Path('data/Misc')/(chr(0x10400)+'.bin'),Path('data/Misc')/(chr(0x10428)+'.bin')),
                (Path('data/Misc/Σ.bin'),Path('data/Misc/ς.bin'))]
    def put(folder,relative,payload):
        path=folder/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(payload)
    for left,right in aliases:
        put(root,left,b'BASE');put(upper,left,b'UPPER');put(lower,right,b'LOWER')
        assert (root/left).samefile(root/right)
    for left,right in distinct:
        put(root,left,b'BASE LEFT');assert not (root/right).exists()
        put(root,right,b'BASE RIGHT');assert not (root/left).samefile(root/right)
        put(upper,left,b'DISTINCT UPPER');put(lower,right,b'DISTINCT LOWER')
    counter=0;checks=[]
    def run(*arguments,good=True):
        nonlocal counter
        counter+=1
        result=subprocess.run([str(args.tool.resolve()),*map(str,arguments)],capture_output=True,
                              text=True,encoding='utf-8',timeout=120)
        (work/f'command-{counter:03d}.json').write_text(json.dumps({'arguments':list(map(str,arguments)),
            'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2),encoding='utf-8')
        assert (result.returncode==0)==good,(arguments,result.stderr)
        return result
    complete=None
    if args.complete_data:
        complete=work/'Complete';write_bank(complete,(1,2,3,4))
        for name in ('script.bin','frontend.bin'):
            (complete/'data/CompiledDefs'/name).write_bytes((complete/'data/CompiledDefs/game.bin').read_bytes())
        run('mods','add',root,complete,'--install-data')
    for source in (upper,lower):run('mods','add',root,source)
    input_sources=[upper,lower]+([complete] if complete else [])
    originals={str(p):snapshot(p) for p in [root,*input_sources]}
    picks=work/'picks.txt';picks.write_text('')
    def command(route,output,reversed_order=False):
        result=['mods',route,root,output]
        if route=='merge':
            sources=[lower,upper] if reversed_order else [upper,lower]
            result+=['--with',*([complete] if complete else []),*sources]
            if complete:result+=['--install-data']
        return result+['--picks',picks,'--json']
    def expected(output,payload):
        for left,right in aliases:
            assert (output/left).read_bytes()==payload and (output/left).samefile(output/right)
        for left,right in distinct:
            assert (output/left).read_bytes()==b'DISTINCT UPPER'
            assert (output/right).read_bytes()==b'DISTINCT LOWER'
            assert not (output/left).samefile(output/right)
        assert len(list((output/'data/Misc').iterdir()))==6
    for route in ('merge','build'):
        output=work/route;picks.write_text('')
        report=json.loads(run(*command(route,output)).stdout);expected(output,b'LOWER')
        rows=report['files']['contested'];assert len(rows)==2
        assert all(row['mods']==['UpperSource','LowerSource'] and row['winner']=='LowerSource' for row in rows)
        (output/'sentinel').write_bytes(b'unowned output')
        checks.append({'route':route,'default_alias_union_and_distinct_files':True})
        if route=='build':run('mods','move',root,'UpperSource',str(2 if complete else 1))
        run(*command(route,output,True));expected(output,b'UPPER')
        if route=='build':run('mods','move',root,'UpperSource',str(1 if complete else 0))
        checks.append({'route':route,'reversed_load_order':True})
        for left,right in aliases:
            for spelling in (left,right):
                key='file:'+str(spelling).replace('/','\\')
                for winner,payload in [('UpperSource',b'UPPER'),('LowerSource',b'LOWER'),('vanilla',b'BASE')]:
                    picks.write_text(key+'\t'+winner+'\n',encoding='utf-8')
                    run(*command(route,output));assert (output/left).read_bytes()==payload
                    assert (output/'sentinel').read_bytes()==b'unowned output'
                    checks.append({'route':route,'alias_choice':key,'winner':winner,'exact_bytes':True})
        # A review written using the lower alias must protect the canonical upper alias.
        picks.write_text('file:'+aliases[0][1].as_posix()+'\tUpperSource\n',encoding='utf-8')
        review=json.loads(run('mods','conflicts',root,'--picks',picks,'--json').stdout)
        row=next(r for r in review['files']['contested'] if 'lias.bin' in r['path'])
        normalize=lambda value:{'present':value['present'],'bytes':value.get('bytes',0),'sha256':value.get('sha256','')}
        stamp={'key':'file:'+aliases[0][1].as_posix(),'winner':'UpperSource',
               'baseline':normalize(row['file_content']['baseline']),
               'versions':{v['mod']:normalize(v) for v in row['file_content']['versions']}}
        picks.write_text('file:'+aliases[0][0].as_posix()+'\tUpperSource\n# forge-file-review-v1 '+json.dumps(stamp)+'\n',encoding='utf-8')
        run(*command(route,output));before=snapshot(output)
        (upper/aliases[0][0]).write_bytes(b'EDIT!')
        failure=run(*command(route,output),good=False)
        assert 'contents changed' in failure.stderr and snapshot(output)==before
        (upper/aliases[0][0]).write_bytes(b'UPPER')
        run(*command(route,output))
        checks.append({'route':route,'mixed_alias_review_stale_refusal_and_recovery':True})
        receipt=json.loads((output/'forge_mods_build.json').read_text(encoding='utf-8'))
        sample=aliases[0][0].as_posix();identity=receipt['files'].get(sample)
        if identity is None:
            sample=next(name for name in receipt['files'] if 'lias.bin' in name);identity=receipt['files'][sample]
        receipt['files'][aliases[0][1].as_posix()]=identity
        before_receipt=(output/'forge_mods_build.json').read_bytes()
        (output/'forge_mods_build.json').write_text(json.dumps(receipt),encoding='utf-8')
        before=snapshot(output);failure=run(*command(route,output),good=False)
        assert 'duplicate owned path' in failure.stderr and snapshot(output)==before
        (output/'forge_mods_build.json').write_bytes(before_receipt)
        checks.append({'route':route,'unicode_duplicate_receipt_refused_preserved':True})
    assert all(originals[str(p)]==snapshot(p) for p in input_sources)
    # Canonical casing must also keep output aliases out of the baseline/source folders.
    unicode_source=work/'Äuthor';put(unicode_source,Path('data/Misc/another.bin'),b'authored')
    for invalid in (work/'äuthor',work/'äuthor'/'output'):
        before=snapshot(unicode_source)
        failure=run('mods','merge',root,invalid,'--with',unicode_source,good=False)
        assert 'Mod output must be outside' in failure.stderr and snapshot(unicode_source)==before
        checks.append({'unicode_source_overlap':str(invalid),'refused_preserved':True})
    unicode_root=work/'Änstall';write_bank(unicode_root,(1,2,3,4))
    for invalid in (work/'änstall',work/'änstall'/'output'):
        before=snapshot(unicode_root)
        failure=run('mods','merge',unicode_root,invalid,'--with',upper,good=False)
        assert 'Mod output must be outside' in failure.stderr and snapshot(unicode_root)==before
        checks.append({'unicode_install_overlap':str(invalid),'refused_preserved':True})
    saved=root/'forge_mods_picks.txt';saved.write_text('file:'+aliases[0][1].as_posix()+'\tUpperSource\n',encoding='utf-8')
    before_stage=snapshot(root);run('mods','deploy',root)
    try:
        assert (root/aliases[0][0]).read_bytes()==b'UPPER'
        assert (root/aliases[1][0]).read_bytes()==b'LOWER'
        report=json.loads(run('mods','conflicts',root,'--json').stdout)
        row=next(r for r in report['files']['contested'] if 'lias.bin' in r['path'])
        assert row['winner']=='UpperSource' and row['file_content']['baseline']['sha256']==hashlib.sha256(b'BASE').hexdigest()
    finally:run('mods','undeploy',root)
    assert snapshot(root)==before_stage
    checks.append({'staged_original_alias_choice_and_exact_restore':True})
    saved.unlink()
    if args.gui:
        for size,scale in [('1440x900',1),('800x600',1.5)]:
            view=work/size;view.mkdir()
            saved.write_text('file:'+str(aliases[0][1])+'\tUpperSource\n',encoding='utf-8')
            before={k:v for k,v in snapshot(root).items() if k!='forge_mods_picks.txt'}
            script=view/'choices.txt'
            lines=['wait_maps','wait_ready',f'set saveroot {root.as_posix()}',f'set uiscale {scale}',
                'mods_tab 1','mods_conflicts','wait_mods','frames 3','assert_state mods_first_winner UpperSource',
                'assert_state mods_picks 1','assert_state mods_unreported_choices 0',
                'mod_pick file:'+str(aliases[0][1])+' LowerSource',
                'mods_conflicts','wait_mods','frames 3','assert_state mods_first_winner LowerSource',
                'assert_state mods_picks 1','assert_state mods_unreported_choices 0',
                'reveal mod_winner_0','frames 3','reveal mod_winner_0','frames 3','clear_toasts',
                f'screenshot {(view/"selected.png").as_posix()}',
                'mods_deploy','wait_mods','assert_log mods deploy: done',
                'mods_undeploy','wait_mods','assert_log mods undeploy: done',
                'mod_pick file:'+str(aliases[0][0])+' -','assert_state mods_picks 0',
                'mods_conflicts','wait_mods','assert_state mods_first_winner LowerSource',
                'assert_state mod_action_failed 0','quit']
            script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            result=subprocess.run([str(args.gui.resolve()),'--install',str(args.install.resolve()),
                '--size',size,'--auto',str(script)],cwd=repo,capture_output=True,text=True,encoding='utf-8',timeout=180,
                env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(view/'appdata'),LOCALAPPDATA=str(view/'localappdata')))
            (view/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
            log=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert result.returncode==0 and 'RESULT PASS' in log,log[-5000:]
            assert {k:v for k,v in snapshot(root).items() if k!='forge_mods_picks.txt'}==before
            assert not saved.exists()
            checks.append({'size':size,'unicode_alias_gui_choice_review_clear_and_exact_restore':True})
    # The shared key must also keep Unicode level paths together without changing
    # case-sensitive thing/quest identities or disjoint edits.
    from test_mod_level_case_identity import put as put_level, values as level_values
    for family in ('tng','qst'):
        case=work/('level-'+family);level_root=case/'install';write_bank(level_root,(1,2,3,4))
        left=Path('Ärea.'+family);right=Path('äREA.'+family.upper())
        baseline=(10,99) if family=='tng' else (False,True)
        authored=(20,100) if family=='tng' else (True,False)
        put_level(level_root,family,left,*baseline)
        a,b=case/'PackA',case/'PackB'
        put_level(a,family,left,authored[0],baseline[1]);put_level(b,family,right,baseline[0],authored[1])
        if complete:run('mods','add',level_root,complete,'--install-data')
        for pack in (a,b):run('mods','add',level_root,pack)
        before={str(p):snapshot(p) for p in (level_root,a,b)}
        key=family+':'+str(right)+'|'+('uid:123456' if family=='tng' else 'Existing')
        original={123456:baseline[0],123457:baseline[1]} if family=='tng' else {'Existing':baseline[0],'Unchanged':baseline[1]}
        expected={123456:authored[0],123457:authored[1]} if family=='tng' else {'Existing':authored[0],'Unchanged':authored[1]}
        vanilla=dict(expected);vanilla[123456 if family=='tng' else 'Existing']=baseline[0]
        for route in ('merge','build'):
            output=case/route;cmd=['mods',route,level_root,output]
            if route=='merge':
                cmd+=['--with',*([complete] if complete else []),a,b]
                if complete:cmd+=['--install-data']
            cmd+=['--picks',picks,'--json'];picks.write_text('')
            report=json.loads(run(*cmd).stdout)
            assert len(report[family]['levels' if family=='tng' else 'files'])==1
            assert level_values(output/'data/Levels'/left,family)==expected
            checks.append({'family':family,'route':route,'unicode_level_disjoint_union':True})
            for winner,wanted in [('PackA',expected),('vanilla',vanilla)]:
                picks.write_text(key+'\t'+winner+'\n',encoding='utf-8');run(*cmd)
                assert level_values(output/'data/Levels'/left,family)==wanted
                checks.append({'family':family,'route':route,'unicode_level_alias_pick':winner})
            picks.write_text(key+'\tPackB\n',encoding='utf-8');output_before=snapshot(output)
            failure=run(*cmd,good=False)
            assert 'selects unavailable contributor PackB' in failure.stderr and snapshot(output)==output_before
            checks.append({'family':family,'route':route,'unicode_unavailable_pick_preserved':True})
        assert before=={str(p):snapshot(p) for p in (level_root,a,b)}
        run('mods','deploy',level_root)
        try:assert level_values(level_root/'data/Levels'/left,family)==expected
        finally:run('mods','undeploy',level_root)
        assert before[str(level_root)]==snapshot(level_root)
        assert level_values(level_root/'data/Levels'/left,family)==original
        checks.append({'family':family,'unicode_level_exact_stage_restore':True})
    (work/'result.json').write_text(json.dumps({'passed':True,'complete_data':args.complete_data,'checks':checks},indent=2),encoding='utf-8')
    print('PASS:',len(checks),'Unicode file identity/choice/review/ownership/restore checks')


if __name__=='__main__':main()
