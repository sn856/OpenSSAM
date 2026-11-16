[CmdletBinding()]
param(
    [string]$OutputPath = 'dist\SSAM3EXE'
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$templateDir = Join-Path $repoRoot 'archive\SSAM3EXE'
$workingDir = Join-Path $repoRoot 'modern\SSAM3\WorkingDir'
$builtExe = Join-Path $workingDir 'SSAM3M.exe'
$builtDll = Join-Path $repoRoot 'modern\SSAMDLL\bin\SSAMDLL_MT.dll'
$packageDir = if ([System.IO.Path]::IsPathRooted($OutputPath)) { $OutputPath } else { Join-Path $repoRoot $OutputPath }

if (-not (Test-Path -LiteralPath $templateDir)) {
    throw "Template folder not found: $templateDir"
}

if (-not (Test-Path -LiteralPath $builtExe)) {
    throw "Built executable not found: $builtExe"
}

if (-not (Test-Path -LiteralPath $builtDll)) {
    throw "Built DLL not found: $builtDll"
}

if (Test-Path -LiteralPath $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}

New-Item -ItemType Directory -Path $packageDir | Out-Null
Copy-Item -Path (Join-Path $templateDir '*') -Destination $packageDir -Recurse -Force
if (Test-Path -LiteralPath (Join-Path $packageDir 'SSAM3EXE.zip')) {
    Remove-Item -LiteralPath (Join-Path $packageDir 'SSAM3EXE.zip') -Force
}
Copy-Item -LiteralPath $builtExe -Destination (Join-Path $packageDir 'SSAM3M.exe') -Force
Copy-Item -LiteralPath $builtExe -Destination (Join-Path $packageDir 'SSAM3.exe') -Force
Copy-Item -LiteralPath $builtDll -Destination (Join-Path $packageDir 'SSAMDLL_MT.dll') -Force

# Console program (same engine DLL as the GUI)
$builtConsole = Join-Path $repoRoot 'modern\SSAMDLL\bin\SSAM_MT.exe'
if (-not (Test-Path -LiteralPath $builtConsole)) {
    throw "Built console program not found: $builtConsole"
}
Copy-Item -LiteralPath $builtConsole -Destination (Join-Path $packageDir 'SSAM_MT.exe') -Force

Write-Host "Package ready: $packageDir"
