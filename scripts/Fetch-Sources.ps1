$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
New-Item -ItemType Directory -Path (Join-Path $taskRoot 'tools') -Force | Out-Null
foreach($taskSource in @(
    @{name='re4_tweaks';url='https://github.com/nipkownix/re4_tweaks.git';commit='92c0208bd09c29c9640e13a2493b8e6bd6edd700'},
    @{name='PeakCraft';url='https://github.com/aeironnsarmiento/PeakCraft.git';commit='d07030ee527ee2a2facc989f5acae8a317968c02'}
)){
    $taskPath=Join-Path $taskRoot ('tools\'+$taskSource.name)
    if(!(Test-Path -LiteralPath $taskPath)){
        & git clone $taskSource.url $taskPath
        if($LASTEXITCODE -ne 0){throw 'Source clone failed.'}
        & git -C $taskPath checkout --detach $taskSource.commit
        if($LASTEXITCODE -ne 0){throw 'Pinned checkout failed.'}
    }
    $taskRevision=& git -C $taskPath rev-parse HEAD
    if($taskRevision -ne $taskSource.commit){throw "Existing checkout is not the pinned commit: $taskPath. It has not been reset."}
    & git -C $taskPath submodule update --init --recursive
    if($LASTEXITCODE -ne 0){throw 'Submodule checkout failed.'}
}
$taskDependencies=Join-Path $taskRoot 'build\dependencies'
New-Item -ItemType Directory -Path $taskDependencies -Force | Out-Null
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.7.2/webfonts/fa-solid-900.ttf' -OutFile (Join-Path $taskDependencies 'fa-solid-900.ttf')
if((Get-FileHash -LiteralPath (Join-Path $taskDependencies 'fa-solid-900.ttf')).Hash -ne 'AF19D135D3A935B3EBFBD80320716FFE1202052C5F68DC2C5F1ABC57005AC605'){throw 'Unexpected font checksum.'}
Invoke-WebRequest -Uri 'https://maven.fabricmc.net/net/fabricmc/fabric-api/fabric-api/0.161.0+26.3/fabric-api-0.161.0+26.3.jar' -OutFile (Join-Path $taskDependencies 'fabric-api-0.161.0+26.3.jar')
if((Get-FileHash -LiteralPath (Join-Path $taskDependencies 'fabric-api-0.161.0+26.3.jar')).Hash -ne '86F16178A3CECC887A85A4CFE9A79D92FA7341D8F39B5951A4D6AD800AB657A6'){throw 'Unexpected Fabric API checksum.'}
Write-Output 'Pinned sources and redistributable dependencies are ready. See BUILDING.md.'
