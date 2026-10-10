"""Read-only effect selection with case variants and duplicate-name refusal."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from test_big_entry_markers import archive
from test_mod_field_details import write_bank


def main():
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='effect-names-',dir=repo/'build'))
    root=work/'install'
    write_bank(root,(1,2,3,4))
    # Only the map list is needed; this test never opens or exports a level.
    levels=root/'data/Levels/FinalAlbion'
    levels.mkdir(parents=True)
    (levels/'unused.lev').write_bytes(b'')
    effects=root/'data/Misc/pc/effects.big'
    payload=struct.pack('<I',100)+b'Names fixture\0'+bytes(7+5*4+4+5)+struct.pack('<I',0)
    rows=[(100,0,'Mixed',payload),(101,0,'MIXED',payload),(42,0,'SAME_EFFECT',payload)]
    archive(effects,b'BIGB',[('PARTICLE_MAIN_PC',111,rows),
                            ('PARTICLE_FRONTEND_PC',111,[(999,0,'Mixed',payload)])])
    before=hashlib.sha256(effects.read_bytes()).hexdigest()
    script=work/'names.txt'
    script.write_text('\n'.join([
        'wait_maps','wait_ready','assets_tab 3','frames 3',
        'assert_state effects_count 3','effect_select Mixed','assert_state effect_id 100',
        'assert_state effect_selected Mixed','effect_select MIXED','assert_state effect_id 101',
        'assert_state effect_selected MIXED','effect_select 100','assert_state effect_selected Mixed',
        'effect_select same_effect','assert_state effect_id 42','assert_state effect_parsed_fully 1',
        'dump_state','quit'])+'\n',encoding='utf-8')
    profile=work/'profile';profile.mkdir()
    env=dict(os.environ,APPDATA=str(profile),FABLEFORGE_AUTOMATION_HIDDEN='1')
    run=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(root),'--auto',str(script)],
                       cwd=repo,capture_output=True,text=True,timeout=120,env=env)
    (work/'gui-output.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    trace=Path(str(script)+'.log').read_text(encoding='utf-8')
    assert run.returncode==0 and 'RESULT PASS' in trace,trace[-3000:]
    assert hashlib.sha256(effects.read_bytes()).hexdigest()==before
    ambiguous=work/'ambiguous.txt'
    ambiguous.write_text('\n'.join(['wait_maps','wait_ready','assets_tab 3','frames 3',
        'effect_select Mixed','effect_select mixed','assert_state effect_id 100',
        'assert_state effect_selected Mixed','assert_log effects: ambiguous name or ID mixed',
        'quit'])+'\n',encoding='utf-8')
    run=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(root),'--auto',str(ambiguous)],
                       cwd=repo,capture_output=True,text=True,timeout=120,env=env)
    (work/'ambiguous-output.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    trace=Path(str(ambiguous)+'.log').read_text(encoding='utf-8')
    assert run.returncode!=0 and 'RESULT FAIL' in trace,trace[-3000:]
    failures=[line for line in trace.splitlines() if 'FAIL ' in line]
    assert len(failures)==1 and 'effect_select: no effect mixed' in failures[0],failures
    assert 'ok   assert_state effect_selected Mixed' in trace
    assert hashlib.sha256(effects.read_bytes()).hexdigest()==before
    cli=repo/'build/forge.exe'
    run=subprocess.run([str(cli),'effects','--install',str(root)],capture_output=True,text=True,timeout=30)
    assert run.returncode==0 and '3 parsed fully, 0 partially' in run.stdout,(run.stdout,run.stderr)
    archive(effects,b'BIGB',[('PARTICLE_MAIN_PC',111,[*rows,(102,0,'Mixed',payload)])])
    duplicated=effects.read_bytes()
    run=subprocess.run([str(cli),'effects','verbose','--install',str(root)],capture_output=True,text=True,timeout=30)
    assert run.returncode==1 and '2 parsed fully, 2 partially' in run.stdout,(run.stdout,run.stderr)
    assert 'unresolved: Mixed (no unique entry)' in run.stdout
    assert effects.read_bytes()==duplicated
    archive(effects,b'BIGB',[('PARTICLE_MAIN_PC',111,rows),
                            ('PARTICLE_FRONTEND_PC',111,[(999,0,'Mixed',payload)])])
    before=effects.read_bytes()
    run=subprocess.run([str(cli),'effects','--install',str(root)],capture_output=True,text=True,timeout=30)
    assert run.returncode==0 and '3 parsed fully, 0 partially' in run.stdout,(run.stdout,run.stderr)
    assert effects.read_bytes()==before
    for banks in ([('PARTICLE_FRONTEND_PC',111,rows)],
                  [('PARTICLE_MAIN_PC',111,rows),('PARTICLE_MAIN_PC',111,rows)]):
        archive(effects,b'BIGB',banks)
        before=effects.read_bytes()
        run=subprocess.run([str(cli),'effects','--install',str(root)],capture_output=True,text=True,timeout=30)
        assert run.returncode==1 and 'PARTICLE_MAIN_PC' in run.stderr,(run.stdout,run.stderr)
        assert effects.read_bytes()==before
    result={'gui':'PASS','gui_ambiguous_preserves_selection':'PASS','cli_case_variants':'PASS','cli_main_bank_selection':'PASS','cli_duplicate_refusal':'PASS','scratch':str(work)}
    (work/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Effect name selection passed; evidence:',work)


if __name__=='__main__':
    main()
