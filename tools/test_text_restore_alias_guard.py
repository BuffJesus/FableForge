#!/usr/bin/env python3
"""Restore protects running owners through language-folder and install-root junctions."""
import argparse,json,os,re,shutil,subprocess,tempfile
from pathlib import Path
import _winapi
from test_big_entry_markers import archive

def main():
    assert os.name=='nt'
    repo=Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    ap.add_argument('--text-only',action='store_true',help='Tracked folders without game.bin')
    ap.add_argument('--cwd',type=Path,default=repo)
    ap.add_argument('--work-root',type=Path,default=repo/'build')
    a=ap.parse_args();work=Path(tempfile.mkdtemp(prefix='text-restore-alias-',dir=a.work_root.resolve())).resolve()
    print('Evidence retained at',work,flush=True)
    native=work/'own-running-install';outside=work/'outside-install';unrelated=work/'unrelated-install'
    def setup(root):
        if a.text_only:
            (root/'data').mkdir(parents=True);return
        defs=root/'data/CompiledDefs';defs.mkdir(parents=True);(defs/'game.bin').write_bytes(b'x')
    for root in (native,outside,unrelated):setup(root)
    payload=lambda text:text.encode('utf-16-le')+bytes(18)
    def tracked(root):
        target=root/'data/lang/English/text.big';backup=Path(str(target)+'.forge-orig')
        archive(target,b'BIGB',[('TEXT_A',106,[(1,0,'TXT_TEST',payload('Original'))])])
        original=target.read_bytes();backup.write_bytes(original)
        archive(target,b'BIGB',[('TEXT_A',106,[(1,0,'TXT_TEST',payload('Edited'))])])
        return target,backup,original,target.read_bytes()
    target,backup,original,current=tracked(native)
    other,other_backup,other_original,_=tracked(unrelated)
    link=outside/'data/lang/English';link.parent.mkdir(parents=True)
    _winapi.CreateJunction(str(target.parent),str(link))
    helper_source=work/'hold.cpp';helper=work/'hold.exe'
    helper_source.write_text('#include <windows.h>\n#include <cstdio>\nint main(){puts("ready");fflush(stdout);Sleep(60000);}\n',encoding='utf-8')
    compiler=re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$',
        (repo/'build/CMakeCache.txt').read_text(encoding='utf-8'),re.MULTILINE).group(1)
    subprocess.run([compiler,str(helper_source),'-o',str(helper)],capture_output=True,check=True,timeout=60)
    game=native/'Fable.exe';shutil.copyfile(helper,game)
    process=subprocess.Popen([str(game)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        assert process.stdout.readline().strip()=='ready'
        result=subprocess.run([str(a.forge.resolve()),'restore','--install',str(unrelated)],cwd=a.cwd.resolve(),
            capture_output=True,text=True,encoding='utf-8',timeout=30)
        assert result.returncode==0 and other.read_bytes()==other_original,result.stdout+result.stderr
        assert target.read_bytes()==current and backup.read_bytes()==original
        result=subprocess.run([str(a.forge.resolve()),'restore','--install',str(outside)],cwd=a.cwd.resolve(),
            capture_output=True,text=True,encoding='utf-8',timeout=30)
        (work/'aliased-restore.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        (work/'observed.json').write_text(json.dumps({'returncode':result.returncode,
            'target_changed':target.read_bytes()!=current,'backup_changed':backup.read_bytes()!=original},indent=2)+'\n',encoding='utf-8')
        assert result.returncode!=0 and 'Fable.exe is running' in result.stdout+result.stderr,result.stdout+result.stderr
        assert target.read_bytes()==current and backup.read_bytes()==original
    finally:
        process.terminate();process.communicate(timeout=10)
    result=subprocess.run([str(a.forge.resolve()),'restore','--install',str(outside)],cwd=a.cwd.resolve(),
        capture_output=True,text=True,encoding='utf-8',timeout=30)
    assert result.returncode==0 and target.read_bytes()==original and backup.read_bytes()==original,result.stdout+result.stderr
    # A root junction must protect other bank types too, including a game
    # launched through that junction and checked through its physical path.
    native_other=work/'own-running-no-text';setup(native_other)
    level=native_other/'data/Levels/Quest.qst';level.parent.mkdir();level.write_bytes(b'edited quest')
    level_backup=Path(str(level)+'.forge-orig');level_backup.write_bytes(b'original quest')
    entire_alias=work/'whole-install-alias';_winapi.CreateJunction(str(native_other),str(entire_alias))
    shutil.copyfile(helper,native_other/'Fable.exe')
    process=subprocess.Popen([str(entire_alias/'Fable.exe')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        assert process.stdout.readline().strip()=='ready'
        for index,root in enumerate((entire_alias,native_other)):
            result=subprocess.run([str(a.forge.resolve()),'restore','--install',str(root)],cwd=a.cwd.resolve(),
                capture_output=True,text=True,encoding='utf-8',timeout=30)
            (work/f'root-alias-{index}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
            assert result.returncode!=0 and 'Fable.exe is running' in result.stdout+result.stderr,result.stdout+result.stderr
            assert level.read_bytes()==b'edited quest' and level_backup.read_bytes()==b'original quest'
    finally:
        process.terminate();process.communicate(timeout=10)
    result=subprocess.run([str(a.forge.resolve()),'restore','--install',str(entire_alias)],cwd=a.cwd.resolve(),
        capture_output=True,text=True,encoding='utf-8',timeout=30)
    assert result.returncode==0 and level.read_bytes()==b'original quest',result.stdout+result.stderr
    # A stage-created bank has no original backup for scan() to discover.
    backup.unlink();target.unlink()
    archive(target,b'BIGB',[('TEXT_A',106,[(1,0,'TXT_TEST',payload('Stage-created'))])])
    manifest=outside/'forge_stage_manifest.json'
    manifest.write_text(json.dumps({'files':[{'path':'data/lang/English/text.big','had_original':False}]}),encoding='utf-8')
    staged=target.read_bytes();manifest_bytes=manifest.read_bytes()
    process=subprocess.Popen([str(game)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        assert process.stdout.readline().strip()=='ready'
        result=subprocess.run([str(a.forge.resolve()),'restore','--install',str(outside),'--forget'],
            cwd=a.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',timeout=30)
        (work/'stage-created-alias.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'Fable.exe is running' in result.stdout+result.stderr,result.stdout+result.stderr
        assert target.read_bytes()==staged and manifest.read_bytes()==manifest_bytes
    finally:
        process.terminate();process.communicate(timeout=10)
    result=subprocess.run([str(a.forge.resolve()),'restore','--install',str(outside),'--forget'],
        cwd=a.cwd.resolve(),capture_output=True,text=True,encoding='utf-8',timeout=30)
    assert result.returncode==0 and not target.exists() and not manifest.exists(),result.stdout+result.stderr
    print('Unrelated root allowed; language/root junction owners refused; physical/aliased launch, stage creation and offline retries: PASS',flush=True)

if __name__=='__main__':main()
