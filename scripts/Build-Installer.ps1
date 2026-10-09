param(
    [string]$FabricJar,
    [switch]$TestBuild,
    [string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskCompiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if(!(Test-Path -LiteralPath $taskCompiler)){throw '.NET Framework C# compiler not found.'}
if(!$FabricJar){$FabricJar=Join-Path $taskRoot 'build\dependencies\fabric-api-0.161.0+26.3.jar'}
$taskResources=@{
    'native.dll' = Join-Path $taskRoot 'build\bridge\dinput8.dll'
    'guest.jar' = Join-Path $taskRoot 'build\guest\skycraft-0.1.2-re4.1.jar'
    'fabric.jar' = $FabricJar
    'settings.ini' = Join-Path $taskRoot 'tools\re4_tweaks\settings\settings.ini'
    'trainer.ini' = Join-Path $taskRoot 'tools\re4_tweaks\settings\trainer_settings.ini'
    'mmc-pack.json' = Join-Path $taskRoot 'installer\mmc-pack.json'
    'GUIDE_RU.md' = Join-Path $taskRoot 'GUIDE_RU.md'
    'theme.mp3' = Join-Path $taskRoot 'installer\assets\theme.mp3'
}
$taskOut=Join-Path $taskRoot 'dist\RE4Craft-0.3.2-installer3'
if($TestBuild){$taskOut=Join-Path $taskRoot 'build\installer-tests'}
if($OutputDirectory){$taskOut=[IO.Path]::GetFullPath($OutputDirectory)}
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
$taskArgs=@('/nologo','/target:winexe','/platform:anycpu','/optimize+','/codepage:65001',('/out:'+(Join-Path $taskOut 'RE4Craft-Setup.exe')),
    '/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.Runtime.Serialization.dll','/reference:System.Core.dll')
if(!$TestBuild){$taskArgs+=('/win32manifest:'+(Join-Path $taskRoot 'installer\app.manifest'))}
else{$taskArgs+=('/define:VISUAL_PREVIEW'),('/win32manifest:'+(Join-Path $taskRoot 'installer\preview.manifest'))}
foreach($taskName in $taskResources.Keys){
    $taskFile=$taskResources[$taskName]
    if(!(Test-Path -LiteralPath $taskFile)){throw "Missing resource: $taskFile"}
    $taskArgs+=('/resource:'+$taskFile+',RE4Craft.'+$taskName)
}
$taskArgs+=(Join-Path $taskRoot 'installer\InstallerEngine.cs'),(Join-Path $taskRoot 'installer\Program.cs'),(Join-Path $taskRoot 'installer\SetupForm.cs'),(Join-Path $taskRoot 'installer\SetupMusic.cs')
& $taskCompiler @taskArgs
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed.'}
Copy-Item -LiteralPath (Join-Path $taskRoot 'installer\app.config') -Destination (Join-Path $taskOut 'RE4Craft-Setup.exe.config') -Force
Get-FileHash -LiteralPath (Join-Path $taskOut 'RE4Craft-Setup.exe')
