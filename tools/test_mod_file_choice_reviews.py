#!/usr/bin/env python3
"""Refuse changed reviewed file choices while preserving outputs and legacy picks."""
import argparse
import json
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
    parser.add_argument('--complete-data', action='store_true')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-file-choice-reviews-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4))
    complete = None
    relative = Path('data/Misc/shared.bin'); key = 'file:data/misc/shared.bin'
    (root/relative).parent.mkdir(parents=True); (root/relative).write_bytes(b'BASE')
    sources = []
    def run(*arguments, good=True):
        result = subprocess.run([str(args.tool), *map(str,arguments)], capture_output=True,
                                text=True, timeout=120)
        assert (result.returncode == 0) == good, (arguments, result.stderr)
        return result
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        defs = complete/'data/CompiledDefs'
        for name in ('script.bin','frontend.bin'): (defs/name).write_bytes((defs/'game.bin').read_bytes())
        run('mods','add',root,complete,'--install-data')
    for name, payload in (('PackA', b'AAAA'), ('PackB', b'BBBB'), ('PackC', None)):
        source = work/name; (source/relative).parent.mkdir(parents=True)
        if payload: (source/relative).write_bytes(payload)
        else: (source/'data/Misc/other.bin').write_bytes(b'other')
        sources.append(source); run('mods', 'add', root, source)
    input_roots = [root,*sources] + ([complete] if complete else [])
    before_inputs = {str(p): snapshot(p) for p in input_roots}
    picks = work/'picks.txt'
    def review(winner='PackA', choice_key=key):
        picks.write_text(key+'\t'+winner+'\n')
        report = json.loads(run('mods','conflicts',root,'--picks',picks,'--json').stdout)
        row = next(r for r in report['files']['contested'] if r['path']=='data/misc/shared.bin')
        normalize = lambda v: {'present':v['present'],'bytes':v.get('bytes',0),'sha256':v.get('sha256','')}
        return {'key':choice_key,'winner':winner,'baseline':normalize(row['file_content']['baseline']),
                'versions':{v['mod']:normalize(v) for v in row['file_content']['versions']}}
    def save(stamp, winner=None):
        picks.write_text(stamp['key']+'\t'+(winner or stamp['winner'])+'\n'+
                         '# forge-file-review-v1 '+json.dumps(stamp,separators=(',',':'))+'\n')
    checks = []
    for route in ('merge','build'):
        output = work/route
        command = ['mods',route,root,output]
        if route=='merge':
            command += ['--with',*([complete] if complete else []),*sources]
            if complete: command += ['--install-data']
        command += ['--picks',picks]
        stamp = review(); save(stamp); run(*command)
        (output/'sentinel').write_bytes(b'prior output')
        assert (output/relative).read_bytes()==b'AAAA'
        changes = [('selected source',sources[0]/relative,b'aaaa'),
                   ('other source',sources[1]/relative,b'bbbb'),
                   ('baseline',root/relative,b'base'),
                   ('baseline removed',root/relative,None),
                   ('new contributor',sources[2]/relative,b'CCCC')]
        for label,path,payload in changes:
            prior = path.read_bytes() if path.exists() else None
            if payload is None: path.unlink()
            else: path.write_bytes(payload)
            before = snapshot(output); choice_before = picks.read_bytes()
            failed = run(*command,good=False)
            assert 'contents changed' in failed.stderr and snapshot(output)==before
            report = json.loads(run('mods','conflicts',root,'--picks',picks,'--json').stdout)
            row = next(r for r in report['files']['contested'] if r['path']=='data/misc/shared.bin')
            assert row['choice_stale'] and row['choice_reviewed'] and not row['overridden']
            assert picks.read_bytes()==choice_before and snapshot(output)==before
            (work/(route+'-'+label.replace(' ','-')+'.log')).write_text(failed.stderr)
            if prior is None: path.unlink()
            else: path.write_bytes(prior)
            checks.append({'route':route,'case':label,'refused_preserved_reviewable':True})
        save(stamp,'PackB'); before=snapshot(output)
        run(*command,good=False); assert snapshot(output)==before
        checks.append({'route':route,'case':'winner edited without review','refused':True})
        mixed_case = dict(stamp, key='file:Data/Misc/Shared.bin')
        save(mixed_case)
        run(*command)
        assert (output/relative).read_bytes()==b'AAAA'
        (sources[0]/relative).write_bytes(b'aaaa'); before=snapshot(output)
        failed=run(*command,good=False)
        assert 'contents changed' in failed.stderr and snapshot(output)==before
        report=json.loads(run('mods','conflicts',root,'--picks',picks,'--json').stdout)
        row=next(r for r in report['files']['contested'] if r['path']=='data/misc/shared.bin')
        assert row['choice_stale'] and row['choice_reviewed']
        (sources[0]/relative).write_bytes(b'AAAA')
        checks.append({'route':route,'case':'mixed-case review path','refused_preserved_reviewable':True})
        save(stamp)
        if route=='merge':
            reversed_command=['mods','merge',root,output,'--with',*([complete] if complete else []),sources[1],sources[0],sources[2],'--picks',picks]
            if complete: reversed_command += ['--install-data']
            run(*reversed_command)
        else:
            run('mods','move',root,'PackA',str(3 if complete else 2)); run(*command)
            run('mods','move',root,'PackA',str(1 if complete else 0))
        assert (output/relative).read_bytes()==b'AAAA'
        checks.append({'route':route,'case':'reorder unchanged versions','accepted':True})
        (sources[0]/'data/Misc/unrelated.bin').write_bytes(b'new unrelated file')
        run(*command); (sources[0]/'data/Misc/unrelated.bin').unlink()
        checks.append({'route':route,'case':'unrelated content','accepted':True})
        (sources[0]/relative).write_bytes(b'aaaa')
        refreshed=review(); save(refreshed); run(*command)
        assert (output/relative).read_bytes()==b'aaaa'
        checks.append({'route':route,'case':'review again','accepted_exact_bytes':True})
        picks.write_text(key+'\tPackA\n'); run(*command)
        assert (output/relative).read_bytes()==b'aaaa'
        checks.append({'route':route,'case':'legacy unstamped choice','accepted':True})
        (sources[0]/relative).write_bytes(b'AAAA')
        inactive=dict(stamp,key='file:data/misc/inactive.bin'); save(inactive); run(*command)
        assert (output/relative).read_bytes()==b'BBBB'
        checks.append({'route':route,'case':'inactive review','retained':True})
        picks.write_text(''); run(*command)
        assert (output/relative).read_bytes()==b'BBBB' and (output/'sentinel').read_bytes()==b'prior output'
        checks.append({'route':route,'case':'clear','accepted':True})
    stamp=review(); save(stamp)
    run('mods','deploy',root,'--picks',picks)
    try:
        installed=snapshot(root)
        report=json.loads(run('mods','conflicts',root,'--picks',picks,'--json').stdout)
        (work/'staged-review.json').write_text(json.dumps(report,indent=2))
        row=next(r for r in report['files']['contested'] if r['path']=='data/misc/shared.bin')
        assert not row['choice_stale'] and not row['choice_unavailable'] and row['winner']=='PackA'
        assert snapshot(root)==installed
        for route in ('merge','build'):
            output=work/('staged-'+route)
            command=['mods',route,root,output]
            if route=='merge':
                command+=['--with',*([complete] if complete else []),*sources]
                if complete: command+=['--install-data']
            run(*command,'--picks',picks)
            assert (output/relative).read_bytes()==b'AAAA' and snapshot(root)==installed
        valid_choice=picks.read_bytes()
        picks.write_bytes(valid_choice+b'# forge-file-review-v1 {}\n')
        failed=run('mods','deploy',root,'--picks',picks,good=False)
        assert 'cannot read file choice review' in failed.stderr and snapshot(root)==installed
        picks.write_bytes(valid_choice)
        checks.append({'case':'malformed review before redeploy','staged_install_preserved':True})
        run('mods','deploy',root,'--picks',picks)
        assert (root/relative).read_bytes()==b'AAAA'
        checks.append({'case':'staged baseline review/build/redeploy','read_only_review':True})
    finally:
        run('mods','undeploy',root)
    assert before_inputs=={str(p):snapshot(p) for p in input_roots}
    (work/'result.json').write_text(json.dumps({'checks':checks,'inputs_unchanged':True,
                                               'complete_data':args.complete_data},indent=2))
    print('PASS:',len(checks),'file review identity, refusal and recovery checks')


if __name__=='__main__': main()
