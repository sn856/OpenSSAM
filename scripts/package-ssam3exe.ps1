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

# Visual C++ runtime DLLs (app-local), so the package runs without installing the redistributables.
# 2015-2022 (x64): from the Build Tools redist folder. 2012 (x64, used by the OSG DLLs): from .deps\vcredist\2012-x64,
# extracted from Microsoft's vcredist_x64.exe 2012 (11.0.50727).
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -property installationPath | Select-Object -First 1
$redistRoot = Get-ChildItem -LiteralPath (Join-Path $vsPath 'VC\Redist\MSVC') -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $redistRoot) { throw "VC redist folder not found under $vsPath" }
$runtimeDlls = @(
    'x64\Microsoft.VC143.CRT\msvcp140.dll',
    'x64\Microsoft.VC143.CRT\vcruntime140.dll',
    'x64\Microsoft.VC143.CRT\vcruntime140_1.dll',
    'x64\Microsoft.VC143.OpenMP\vcomp140.dll',
    'x64\Microsoft.VC143.MFC\mfc140.dll'
) | ForEach-Object { Join-Path $redistRoot.FullName $_ }
$runtimeDlls += @('msvcp110.dll', 'msvcr110.dll') | ForEach-Object { Join-Path $repoRoot ".deps\vcredist\2012-x64\$_" }
foreach ($dll in $runtimeDlls) {
    if (-not (Test-Path -LiteralPath $dll)) { throw "Runtime DLL not found: $dll" }
    Copy-Item -LiteralPath $dll -Destination $packageDir -Force
}

Write-Host "Package ready: $packageDir"
