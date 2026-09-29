# Build the opt-in profiler with clang-cl's MSVC ABI. No GUI is launched.
param([int]$Jobs = 2)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Install Visual Studio C++ build tools before profiling.' }
$clang = "$env:ProgramFiles/LLVM/bin/clang-cl.exe"
if (-not (Test-Path -LiteralPath $clang)) { throw 'Install LLVM clang-cl before profiling.' }
$vsEnv = & cmd.exe /d /s /c "`"$vs/Common7/Tools/VsDevCmd.bat`" -arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio environment setup failed.' }
foreach ($entry in $vsEnv) {
    if ($entry -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$previousPriority = (Get-Process -Id $PID).PriorityClass
try {
    (Get-Process -Id $PID).PriorityClass = 'BelowNormal'
    cmake -S $root -B "$root/build-profile-clang" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFABLEFORGE_PROFILE=ON "-DCMAKE_CXX_COMPILER=$clang" "-DCMAKE_C_COMPILER=$clang" '-DCMAKE_CXX_FLAGS=/utf-8 /EHsc /DNOMINMAX' '-DCMAKE_C_FLAGS=/DNOMINMAX' -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
    if ($LASTEXITCODE -ne 0) { throw 'Profile configuration failed.' }
    cmake --build "$root/build-profile-clang" --target FableForge -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Profile build failed.' }
} finally {
    (Get-Process -Id $PID).PriorityClass = $previousPriority
}
