[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe not found: $vswhere"
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) {
    throw 'MSBuild.exe not found. Install Visual Studio Build Tools with C++ support first.'
}

$dllSolution = Join-Path $repoRoot 'modern\SSAMDLL\SSAMDLL.sln'
$appSolution = Join-Path $repoRoot 'modern\SSAM3\SSAMAPP.sln'
$dllOutput = Join-Path $repoRoot 'modern\SSAMDLL\bin\SSAMDLL_MT.dll'
$workingDir = Join-Path $repoRoot 'modern\SSAM3\WorkingDir'
$workingDll = Join-Path $workingDir 'SSAMDLL_MT.dll'

Write-Host "Using MSBuild: $msbuild"
Write-Host 'Building SSAMDLL (Release_MT | x64)...'
& $msbuild $dllSolution /t:SSAMDLL /p:Configuration=Release_MT /p:Platform=x64 /m /nologo
if ($LASTEXITCODE -ne 0) {
    throw "SSAMDLL build failed with exit code $LASTEXITCODE"
}

Write-Host 'Building SSAMAPP (Release_MT | x64)...'
& $msbuild $appSolution /t:SSAMAPP /p:Configuration=Release_MT /p:Platform=x64 /m /nologo
if ($LASTEXITCODE -ne 0) {
    throw "SSAMAPP build failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $dllOutput)) {
    throw "Built DLL not found: $dllOutput"
}

if (-not (Test-Path -LiteralPath $workingDir)) {
    throw "WorkingDir not found: $workingDir"
}

Copy-Item -LiteralPath $dllOutput -Destination $workingDll -Force

Write-Host ''
Write-Host 'Build complete.'
Write-Host "Updated runtime DLL: $workingDll"
Write-Host "Executable: $(Join-Path $workingDir 'SSAM3M.exe')"
