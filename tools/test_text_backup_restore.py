#!/usr/bin/env python3
"""Discover localization backups and retire restored text-browser content."""
import argparse,json,os
from pathlib import Path
import subprocess,tempfile
from test_mod_field_details import write_bank
from test_mod_asset_choice_contributors import bank
from test_text_browser_ui import string

def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--english-only',action='store_true')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args();work=Path(tempfile.mkdtemp(prefix='text-backup-restore-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    languages=[('English','.forge-orig','Original English Ω')]
    if not args.english_only:languages += [('French','.atlas-orig','Bonjour original Ω'),('语言','.forge-orig','Original 漢字')]
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir();root=case/'install';write_bank(root,(1,2,3,4))
        levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
        originals={};backups={}
        for language,suffix,original in languages:
            target=root/'data/lang'/language/'text.big'
            records={'TXT_HELLO':{'id':10,'type':0,'header':b'','payload':string(original,'TXT_HELLO')}}
            bank(target,'TEXT_MAIN',records);originals[target]=target.read_bytes()
            backup=target.with_name(target.name+suffix);backup.write_bytes(target.read_bytes());backups[backup]=backup.read_bytes()
            records['TXT_HELLO']['payload']=string('Changed '+language,'TXT_HELLO');bank(target,'TEXT_MAIN',records)
        ignored=root/'data/lang/index.txt.forge-orig';ignored.write_bytes(b'not a language backup')
        nested=root/'data/lang/English/nested/ignore.big.forge-orig'
        nested.parent.mkdir();nested.write_bytes(b'not a direct language backup')
        listing=subprocess.run([str(args.forge.resolve()),'backups','--install',str(root)],cwd=args.cwd.resolve(),
            capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)
        (case/'listing.log').write_text(listing.stdout+listing.stderr,encoding='utf-8')
        assert listing.returncode==0 and f'{len(languages)} backed-up file(s), {len(languages)} differ' in listing.stdout,listing.stdout+listing.stderr
        selected=languages[-1][0];expected=languages[-1][2]
        lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 5','text_language '+selected,
            'wait_texts','frames 3','text_select TXT_HELLO','assert_text_content Changed '+selected,
            'restore_during_text_load_refused','wait_texts','frames 3',
            'text_select TXT_HELLO','assert_text_content Changed '+selected,
            'setup 1','frames 3','assert_state backups_differ '+str(len(languages)),
            'reveal btn_restore_all','frames 3','click btn_restore_all','frames 3',
            'reveal btn_restore_confirm','frames 3','click btn_restore_confirm','frames 8',
            'wait_ready','assert_state backups_differ 0','setup 0','assets_tab 5','wait_texts','frames 3',
            'text_select TXT_HELLO','assert_text_content '+expected,'quit']
        script=case/'restore.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
        result=subprocess.run([str(args.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
            cwd=args.cwd.resolve(),env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-4000:]
        assert all(path.read_bytes()==value for path,value in originals.items())
        assert all(path.read_bytes()==value for path,value in backups.items())
        assert ignored.read_bytes()==b'not a language backup'
        assert nested.read_bytes()==b'not a direct language backup'
        print(size,'language discovery/original restoration/fresh browser checks: PASS',flush=True)
    (work/'result.json').write_text(json.dumps({'passed':True,'languages':[v[0] for v in languages],'sizes':['1440x900','800x600']},indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
