#!/usr/bin/env python3
"""EgoCore invokes its compiler literally through Unicode module/env/temp paths."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def snapshot(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=repo/'build/forge-tools.exe')
    args = parser.parse_args()
    assert os.name == 'nt', 'this fixture exercises Windows process paths'
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    work = Path(tempfile.mkdtemp(prefix='egocore-unicode-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    source = work/'fail-compiler.cpp'
    source.write_text(r'''#include <windows.h>
#include <string>
int wmain(int argc, wchar_t** argv) {
    std::string output = "FORGE_TEST_COMPILER_FAILURE\n";
    for (int i = 0; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string value(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, value.data(), n, nullptr, nullptr);
        value.resize(n-1);
        output += "arg" + std::to_string(i) + "=" + value + "\n";
    }
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), output.data(), DWORD(output.size()), &written, nullptr);
    // Nonzero status must refuse even if a failed compiler left both bank files.
    if (argc == 5) for (const wchar_t* name : {L"game.bin", L"names.bin"}) {
        std::wstring path = std::wstring(argv[4]) + L"\\" + name;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            WriteFile(file, "failed compiler output", 22, &written, nullptr);
            CloseHandle(file);
        }
    }
    return 7;
}
''', encoding='utf-8')
    cache = (repo/'build/CMakeCache.txt').read_text(encoding='utf-8')
    compiler = re.search(r'^CMAKE_CXX_COMPILER:FILEPATH=(.+)$', cache, re.M).group(1)
    helper = work/'fail-compiler.exe'
    subprocess.run([compiler, '-std=c++20', '-static', '-municode', str(source), '-o', str(helper)],
        cwd=work, check=True, capture_output=True, timeout=60)
    marker = '\u00e9 \u03a9 \u6f22\u5b57 %FORGE_DEFC_TOKEN% & files'
    root = work/'install'
    defs = root/'data/CompiledDefs'
    defs.mkdir(parents=True)
    for name in ('game.bin', 'names.bin'):
        shutil.copyfile(args.install/'data/CompiledDefs'/name, defs/name)
    root_before = snapshot(root)
    mod = work/'CompilerProbe'
    (mod/'Data/Defs').mkdir(parents=True)
    (mod/'CompilerProbe.dll').write_bytes(b'synthetic DLL; never executed')
    (mod/'Data/Defs/probe.def').write_text('#definition OBJECT PROBE\n Value 2;\n#end_definition\n', encoding='utf-8')
    text = work/'text defs'
    text.mkdir()
    (text/'probe.def').write_text('#definition OBJECT PROBE\n Value 1;\n#end_definition\n', encoding='utf-8')
    unicode_text = work/('text defs '+marker)
    shutil.copytree(text, unicode_text)
    cwd = work/'unrelated working directory'
    cwd.mkdir()
    apps = {}
    for label, suffix in [('ascii', 'plain files'), ('unicode', marker)]:
        app = work/('application '+suffix)
        app.mkdir()
        shutil.copyfile(args.exe.resolve(), app/'forge-tools.exe')
        shutil.copyfile(helper, app/'defc.exe')
        apps[label] = app
    apps['missing'] = work/'application missing compiler'
    apps['missing'].mkdir()
    shutil.copyfile(args.exe.resolve(), apps['missing']/'forge-tools.exe')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    results = []
    for case, app_key, explicit, unicode_temp, unicode_defs in [
            ('ascii-control', 'ascii', False, False, False),
            ('unicode-module', 'unicode', False, False, False),
            ('unicode-compiler-env', 'ascii', True, False, False),
            ('unicode-temp', 'ascii', False, True, False),
            ('unicode-defs-env', 'ascii', False, False, True),
            ('locked-compiler', 'ascii', False, False, False),
            ('missing-compiler', 'missing', False, False, False)]:
        temp = work/('temp '+marker if unicode_temp else case+'-temp')
        temp.mkdir()
        sentinel = temp/'forge_egocore/unowned/keep.txt'
        sentinel.parent.mkdir(parents=True)
        sentinel.write_bytes(b'unrelated compiler workspace')
        temp_before = snapshot(temp)
        output = work/(case+'-output')
        output.mkdir()
        (output/'keep.txt').write_bytes(b'previous valid output')
        output_before = snapshot(output)
        env = dict(os.environ, TEMP=str(temp), TMP=str(temp),
            FORGE_DEFS_TEXT=str(unicode_text if unicode_defs else text), FORGE_DEFC_TOKEN='expanded')
        env.pop('FORGE_DEFC', None)
        if explicit:
            env['FORGE_DEFC'] = str(apps['unicode']/'defc.exe')
        if app_key == 'missing':
            env['PATH'] = str(cwd)
        command = [str(apps[app_key]/'forge-tools.exe'), 'mods', 'merge', str(root), str(output), str(mod)]
        locked = None
        if case == 'locked-compiler':
            locked = kernel.CreateFileW(str(apps[app_key]/'defc.exe'), 0x80000000, 0, None, 3, 0x80, None)
            assert locked != ctypes.c_void_p(-1).value, ctypes.get_last_error()
        try:
            result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, timeout=60)
        finally:
            if locked is not None:
                kernel.CloseHandle(locked)
        log = (result.stdout+result.stderr).decode('utf-8', errors='replace')
        (work/(case+'.log')).write_text(log, encoding='utf-8')
        expected = apps['unicode' if explicit else app_key]/'defc.exe'
        passed = result.returncode != 0 and 'FORGE_TEST_COMPILER_FAILURE' in log and f'arg0={expected}' in log
        if case in ('locked-compiler', 'missing-compiler'):
            passed = result.returncode != 0 and 'cannot start definition compiler (Windows error ' in log
        if unicode_temp:
            passed = passed and str(temp) in log and '%FORGE_DEFC_TOKEN%' in log
        assert snapshot(root) == root_before and snapshot(output) == output_before
        assert snapshot(temp) == temp_before, 'failed normalization changed an unowned workspace or leaked files'
        results.append({'case': case, 'returncode': result.returncode, 'passed': passed})
        print(results[-1], flush=True)
        if case == 'locked-compiler':
            result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, timeout=60)
            log = (result.stdout+result.stderr).decode('utf-8', errors='replace')
            (work/(case+'-retry.log')).write_text(log, encoding='utf-8')
            assert snapshot(root) == root_before and snapshot(output) == output_before and snapshot(temp) == temp_before
            results.append({'case': case+'-retry', 'returncode': result.returncode,
                'passed': result.returncode != 0 and 'FORGE_TEST_COMPILER_FAILURE' in log and f'arg0={expected}' in log})
            print(results[-1], flush=True)
    (work/'result.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    assert all(result['passed'] for result in results), results
    print('Literal Unicode compiler invocation and failed-build preservation: PASS')


if __name__ == '__main__':
    main()
