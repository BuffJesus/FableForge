"""Package a selected Fable Textures build without replacing earlier artifacts."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

ALLOWED={'advapi32.dll','dwmapi.dll','gdi32.dll','kernel32.dll','msvcrt.dll','ole32.dll',
    'shell32.dll','user32.dll','uuid.dll','bcrypt.dll','shlwapi.dll','comdlg32.dll','comctl32.dll','ucrtbase.dll'}


def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    repo=Path(__file__).resolve().parents[1]
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build',type=Path,default=repo/'build')
    ap.add_argument('--runtime-dll',type=Path,default=repo/'build/texture-display-native/Release/TextureSizeFix.dll')
    ap.add_argument('--archive-runtime-dll',type=Path,default=repo/'build/archive4gb-validation-20261018/Release/Archive4GB.dll')
    ap.add_argument('--output-dir',type=Path,default=repo/'dist')
    ap.add_argument('--name',default='FableTextures-0.2.0-win64')
    ap.add_argument('--exe-sha256',help='optional verified input executable pin')
    args=ap.parse_args();build=args.build.resolve();output=args.output_dir.resolve();name=args.name
    if not re.fullmatch(r'FableTextures-[A-Za-z0-9][A-Za-z0-9._-]*',name):raise ValueError('invalid package name')
    folder=output/name;archive=output/(name+'.zip');report_path=output/(name+'.json')
    if any(p.exists() for p in (folder,archive,report_path)):raise FileExistsError('package already exists; choose a fresh name or output directory')
    cache=(build/'CMakeCache.txt').read_text()
    source=Path(re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$',cache,re.M)[1]).resolve()
    if source!=repo:raise ValueError('selected build belongs to another checkout')
    compiler=Path(re.search(r'^CMAKE_C_COMPILER:(?:FILEPATH|STRING)=(.+)$',cache,re.M)[1]);bindir=compiler.parent
    original=build/'FableTextureExport.exe';input_sha=digest(original)
    if args.exe_sha256 and input_sha!=args.exe_sha256:raise ValueError('input executable differs from its validation pin')
    runtime_sha=digest(args.runtime_dll)
    archive_runtime_sha=digest(args.archive_runtime_dll)
    if archive_runtime_sha!='6d94484d9303d63a0423353f858e9787d099487de9b3928e5619b752f9832b52':
        raise ValueError('Archive4GB.dll differs from the validated runtime pin')
    def imports(path):
        text=subprocess.check_output([str(bindir/'objdump.exe'),'-p',str(path)],text=True)
        return re.findall(r'DLL Name: ([^\r\n]+)',text)
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.textures-package-',dir=output) as scratch:
        stage=Path(scratch);payload=stage/'payload';(payload/'runtime').mkdir(parents=True)
        exe=payload/'FableTextures.exe';shutil.copy2(original,exe)
        subprocess.run([str(bindir/'strip.exe'),'--strip-unneeded',str(exe)],check=True)
        dlls=imports(exe)
        if not all(n.lower() in ALLOWED or n.lower().startswith('api-ms-win-crt-') for n in dlls):
            raise ValueError('non-system executable dependencies: '+repr(dlls))
        runtime=payload/'runtime/TextureSizeFix.dll';shutil.copy2(args.runtime_dll,runtime)
        runtime_dlls=imports(runtime)
        if [n.upper() for n in runtime_dlls]!=['KERNEL32.DLL']:raise ValueError('unexpected game-side runtime dependencies')
        archive_runtime=payload/'runtime/Archive4GB.dll';shutil.copy2(args.archive_runtime_dll,archive_runtime)
        archive_runtime_dlls=imports(archive_runtime)
        if [n.upper() for n in archive_runtime_dlls]!=['KERNEL32.DLL']:raise ValueError('unexpected Archive4GB dependencies')
        for source,target in ((repo/'tools/texture-export/README.txt','README.txt'),(repo/'LICENSE','LICENSE.txt'),
            (repo/'dist/FableTextureExport-0.1.6-win64/THIRD-PARTY-NOTICES.txt','THIRD-PARTY-NOTICES.txt')):
            shutil.copy2(source,payload/target)
        files={p.relative_to(payload).as_posix():dict(bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(payload.rglob('*')) if p.is_file()}
        info=dict(name=name,buildDirectory=str(build),inputExeSha256=input_sha,runtimeInputSha256=runtime_sha,
            files=files,systemDlls=dlls,runtimeDlls=runtime_dlls,archive4GBIncluded=True,archiveRuntimeSha256=archive_runtime_sha,
            sourceHashesAtPackaging={p:digest(repo/p) for p in ('tools/texture-export/main.cpp','tools/texture-export/import.cpp','tools/texture-export/import.hpp','tools/texture-export/collection.cpp','src/textureproject.cpp','src/textureproject.hpp','src/texturefileio.hpp')})
        (payload/'PACKAGE_INFO.json').write_text(json.dumps(info,indent=2)+'\n')
        packed=stage/'candidate.zip'
        with zipfile.ZipFile(packed,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for p in sorted(payload.rglob('*')):
                if p.is_file():z.write(p,p.relative_to(payload).as_posix())
        report=dict(name=name,exeBytes=exe.stat().st_size,zipBytes=packed.stat().st_size,exeSha256=digest(exe),
            runtimeSha256=digest(runtime),zipSha256=digest(packed),inputExeSha256=input_sha,
            archiveRuntimeSha256=archive_runtime_sha,archiveRuntimeDlls=archive_runtime_dlls,
            systemDlls=dlls,runtimeDlls=runtime_dlls,files=files)
        # Neither rename replaces an existing package on Windows.
        payload.rename(folder);packed.rename(archive)
        with report_path.open('x',encoding='utf-8') as stream:json.dump(report,stream,indent=2);stream.write('\n')
    print(archive);print(json.dumps(report,indent=2))


if __name__=='__main__':main()
