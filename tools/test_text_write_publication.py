#!/usr/bin/env python3
"""Failed text set/import publication preserves original files and permits retry."""
import argparse,contextlib,ctypes,hashlib,json,os
from ctypes import wintypes
from pathlib import Path
import re,shutil,subprocess,tempfile
import _winapi
from test_big_entry_markers import archive
from test_fmp_links import package_records

@contextlib.contextmanager
def deny_reads_and_renames(path):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,
                                wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    kernel.CreateFileW.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    handle=kernel.CreateFileW(str(path),0x80000000,2,None,3,0x80,None)
    assert handle!=wintypes.HANDLE(-1).value,ctypes.get_last_error()
    try:yield
    finally:kernel.CloseHandle(handle)

def main():
    assert os.name=='nt','Real Windows file-sharing semantics required'
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    args=parser.parse_args();work=Path(tempfile.mkdtemp(prefix='text-write-publication-',dir=repo/'build')).resolve()
    print('Evidence retained at',work,flush=True)
    payload=lambda value:value.encode('utf-16-le')+bytes(18)
    checks=0
    for mode in ('set','import'):
        case=work/mode;case.mkdir();source=case/'source Ω.big';destination=case/'output 漢字.big'
        archive(source,b'BIGB',[('TEXT_A',106,[(1,0,'TXT_TEST',payload('Original')),
                                                     (2,0,'TXT_OTHER',payload('Untouched'))])])
        archive(destination,b'BIGB',[('TEXT_A',106,[(1,0,'TXT_TEST',payload('Previous output'))])])
        source_bytes=source.read_bytes();previous=destination.read_bytes()
        value='Changed Ω'
        manifest=case/'import Ω.json';manifest.write_text(json.dumps({'entries':[{'name':'TXT_TEST','content':value}]}),encoding='utf-8')
        def run(accepted,in_place=False):
            nonlocal checks
            command=[str(args.tool.resolve()),'text',mode,str(source)]
            command+=['TXT_TEST',value] if mode=='set' else [str(manifest)]
            command+=['--in-place'] if in_place else ['--out',str(destination)]
            result=subprocess.run(command,cwd=repo,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)
            (case/f'run-{checks}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
            assert (result.returncode==0)==accepted,(mode,result.stdout,result.stderr)
            checks+=1
            assert not list(case.glob('.forge-text-write-*')),'publication leaked staging files'
        with deny_reads_and_renames(destination):run(False)
        assert destination.read_bytes()==previous,'failed reopen verification changed the output'
        assert source.read_bytes()==source_bytes
        run(True)
        records=package_records(destination,by_id=True)['TEXT_A']
        assert records[1][0].startswith('Changed Ω'.encode('utf-16-le')+bytes(2))
        assert records[2][0]==payload('Untouched') and source.read_bytes()==source_bytes
        destination.unlink();destination.mkdir();(destination/'sentinel').write_bytes(b'keep')
        run(False)
        assert (destination/'sentinel').read_bytes()==b'keep' and source.read_bytes()==source_bytes
        with deny_reads_and_renames(source):run(False,True)
        assert source.read_bytes()==source_bytes
        backup=source.with_name(source.name+'.forge-orig');backup.mkdir();(backup/'sentinel').write_bytes(b'keep backup directory')
        run(False,True)
        assert source.read_bytes()==source_bytes and (backup/'sentinel').read_bytes()==b'keep backup directory'
        (backup/'sentinel').unlink();backup.rmdir()
        run(True,True)
        assert source.with_name(source.name+'.forge-orig').read_bytes()==source_bytes
        records=package_records(source,by_id=True)['TEXT_A']
        assert records[2][0]==payload('Untouched')
        value='Second change Ω';manifest.write_text(json.dumps({'entries':[{'name':'TXT_TEST','content':value}]}),encoding='utf-8')
        run(True,True)
        assert backup.read_bytes()==source_bytes
        assert package_records(source,by_id=True)['TEXT_A'][1][0].startswith(value.encode('utf-16-le')+bytes(2))
    helper_source=work/'hold.cpp'
    helper_source.write_text('#include <windows.h>\n#include <cstdio>\nint main(){puts("ready");fflush(stdout);Sleep(60000);}\n',encoding='utf-8')
    compiler=re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$',
        (repo/'build/CMakeCache.txt').read_text(encoding='utf-8'),re.MULTILINE).group(1)
    helper=work/'hold.exe'
    subprocess.run([compiler,str(helper_source),'-o',str(helper)],capture_output=True,check=True,timeout=60)
    for mode in ('set','import'):
        install=work/(mode+'-install');target=install/'data/lang/English/text.big'
        archive(target,b'BIGB',[('TEXT_A',106,[(1,0,'TXT_TEST',payload('Original'))])])
        source=work/(mode+'-source.big');shutil.copyfile(target,source)
        manifest=work/(mode+'-running.json');manifest.write_text(json.dumps({'entries':[{'name':'TXT_TEST','content':'Changed'}]}),encoding='utf-8')
        game=install/'Fable.exe';shutil.copyfile(helper,game)
        def command(in_place):
            return [str(args.tool.resolve()),'text',mode,str(target if in_place else source),
                *(['TXT_TEST','Changed'] if mode=='set' else [str(manifest)]),
                *(['--in-place'] if in_place else ['--out',str(target)])]
        before={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in install.rglob('*') if p.is_file()}
        process=subprocess.Popen([str(game)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            assert process.stdout.readline().strip()=='ready'
            for in_place in (False,True):
                result=subprocess.run(command(in_place),cwd=repo,capture_output=True,timeout=30)
                (work/f'{mode}-running-{in_place}.log').write_bytes(result.stdout+result.stderr)
                assert result.returncode!=0 and b'Fable.exe is running' in result.stderr,result.stderr
                assert all(hashlib.sha256(p.read_bytes()).hexdigest()==value for p,value in before.items())
                assert not list(target.parent.glob('.forge-text-write-*'))
                checks+=1
            unrelated=work/(mode+'-unrelated')/'data/lang/English/text.big'
            separate=command(False);separate[-1]=str(unrelated)
            result=subprocess.run(separate,cwd=repo,capture_output=True,timeout=30)
            assert result.returncode==0,result.stderr
            assert all(hashlib.sha256(p.read_bytes()).hexdigest()==value for p,value in before.items())
            assert package_records(unrelated,by_id=True)['TEXT_A'][1][0].startswith('Changed'.encode('utf-16-le')+bytes(2))
            checks+=1
            external=work/(mode+'-external-language');external.mkdir()
            external_bank=external/'text.big'
            archive(external_bank,b'BIGB',[('TEXT_A',106,[(1,0,'TXT_TEST',payload('External original'))])])
            external_bytes=external_bank.read_bytes()
            language_link=install/'data/lang/French'
            _winapi.CreateJunction(str(external),str(language_link))
            for in_place in (False,True):
                aliased=command(in_place)
                aliased[3 if in_place else -1]=str(language_link/'text.big')
                result=subprocess.run(aliased,cwd=repo,capture_output=True,timeout=30)
                (work/f'{mode}-aliased-language-{in_place}.log').write_bytes(result.stdout+result.stderr)
                assert result.returncode!=0 and b'Fable.exe is running' in result.stderr,result.stderr
                assert external_bank.read_bytes()==external_bytes
                assert not list(external.glob('.forge-text-write-*'))
                checks+=1
            asset_alias=work/(mode+'-asset-alias')
            _winapi.CreateJunction(str(target.parent),str(asset_alias))
            for in_place in (False,True):
                reverse=command(in_place);reverse[3 if in_place else -1]=str(asset_alias/'text.big')
                result=subprocess.run(reverse,cwd=repo,capture_output=True,timeout=30)
                (work/f'{mode}-asset-alias-{in_place}.log').write_bytes(result.stdout+result.stderr)
                assert result.returncode!=0 and b'Fable.exe is running' in result.stderr,result.stderr
                assert all(hashlib.sha256(p.read_bytes()).hexdigest()==value for p,value in before.items())
                checks+=1
        finally:
            process.terminate();process.communicate(timeout=10)
        for in_place in (False,True):
            result=subprocess.run(command(in_place),cwd=repo,capture_output=True,timeout=30)
            assert result.returncode==0,result.stderr
            assert package_records(target,by_id=True)['TEXT_A'][1][0].startswith('Changed'.encode('utf-16-le')+bytes(2))
            checks+=1
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8')
    print('Text write publication:',checks,'preservation/retry/backup checks passed.')

if __name__=='__main__':main()
