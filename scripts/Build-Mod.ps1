param([ValidateSet('native','bridge')][string]$BuildFolder='bridge')
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskPython = (Get-Command python.exe -ErrorAction Stop).Source
$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $taskVsWhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'Visual Studio 2022 C++ build tools were not found.' }
Push-Location $taskRoot
try {
    & $taskPython scripts/prepare_fork.py
    if ($LASTEXITCODE -ne 0) { throw 'Source preparation failed.' }
    & $msbuild tools/re4_tweaks/dllmain/dllmain.vcxproj /m /p:Configuration=Release /p:Platform=Win32 /p:WindowsTargetPlatformVersion=10.0.26100.0 "/p:OutDir=$taskRoot\build\$BuildFolder\" "/p:IntDir=$taskRoot\build\$BuildFolder-obj\" /verbosity:quiet /nologo /fl /flp:logfile=build-mashup.log
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed; see build-mashup.log.' }
    Get-FileHash -LiteralPath (Join-Path $taskRoot "build\$BuildFolder\dinput8.dll") -Algorithm SHA256
} finally { Pop-Location }
