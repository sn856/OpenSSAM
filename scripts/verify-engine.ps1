# Compares engine outputs with the baseline produced by the pre-change build.
#   -MakeBaseline : create .local/baseline/baseline.json (refused if it exists)
#   -Quick        : every combination except the large file
#   (no switch)   : every combination; the large-file row is reported NOT RUN if the file is missing
# Exit code 0 = every comparison matched. Windows PowerShell 5.1 compatible. ASCII only.
[CmdletBinding()]
param(
    [switch]$Quick,
    [switch]$MakeBaseline
)

$ErrorActionPreference = 'Stop'

# Thread count used for the "default" column. '' = omit nthreads (use the program default).
$DefaultThreads = ''

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exe = Join-Path $repoRoot 'modern\SSAMDLL\bin\SSAM_MT.exe'
$dataDir = Join-Path $repoRoot '.local\testdata'
$baseDir = Join-Path $repoRoot '.local\baseline'
$outDir = Join-Path $repoRoot '.local\verify-out'
$baselineFile = Join-Path $baseDir 'baseline.json'
$maxThreads = [string][Environment]::ProcessorCount

if (-not (Test-Path -LiteralPath $exe)) { throw "Console program not found: $exe" }
New-Item -ItemType Directory -Force -Path $baseDir, $outDir | Out-Null

# Local test inputs (not in the repository). .local/testdata/test-files.json (UTF-8):
#   { "small": "<~50MB TRJ>", "medium": "<~150MB TRJ>", "large": "<multi-GB TRJ, optional>" }
# Keep the same absolute paths over time: output files contain the TRJ path.
$configFile = Join-Path $dataDir 'test-files.json'
if (-not (Test-Path -LiteralPath $configFile)) { throw "Test file list not found: $configFile" }
$testFiles = Get-Content -LiteralPath $configFile -Raw -Encoding UTF8 | ConvertFrom-Json
$hot = $testFiles.small
$mid = $testFiles.medium
$bigFile = if ($testFiles.large) { $testFiles.large } else { '' }
$old = Join-Path $dataDir 'gen_oldformat.trj'
$trunc = Join-Path $dataDir 'gen_truncated.trj'
foreach ($f in @($hot, $mid, $old, $trunc)) {
    if (-not $f -or -not (Test-Path -LiteralPath $f)) { throw "Test input missing: $f (run scripts/make-engine-testdata.ps1)" }
}

function Get-Sha([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return 'MISSING' }
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}

function Get-TextSha([string]$text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
    return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
}

# Runs the console program once. Returns a hashtable: kind -> sha256.
function Invoke-Case([string[]]$trjs, [string]$opt, [string]$threads, [bool]$reader, [string[]]$kinds) {
    $csv = Join-Path $outDir 'run.csv'
    $dump = Join-Path $outDir 'run.dump'
    $dat = $trjs[0].Substring(0, $trjs[0].Length - 4) + '_dat.csv'
    Remove-Item -LiteralPath $csv, $dump, $dat -ErrorAction SilentlyContinue

    $argList = @("trjfiles=$($trjs -join ',')", "csvfile=$csv")
    # Two digits on purpose: the pre-change console program ignored single-character values.
    if ($threads -ne '') { $argList += ('nthreads={0:D2}' -f [int]$threads) }
    if ($opt -eq 'dat') { $argList += '-dat' }
    if ($opt -eq 'puea') { $argList += '-puea' }

    $env:SSAM_VERIFY_DUMP = $dump
    $env:SSAM_PROFILE = $null
    if ($reader) { $env:SSAM_READER_THREAD = $null } else { $env:SSAM_READER_THREAD = '0' }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $stdout = & $exe @argList
    $sw.Stop()
    $env:SSAM_VERIFY_DUMP = $null
    $env:SSAM_READER_THREAD = $null

    $result = @{ seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1) }
    foreach ($k in $kinds) {
        switch ($k) {
            'csv' { $result[$k] = Get-Sha $csv }
            'dump' { $result[$k] = Get-Sha $dump }
            'dat' { $result[$k] = Get-Sha $dat }
            'stdout' {
                $lines = @($stdout)
                $idx = -1
                for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -like 'Analysis error*') { $idx = $i; break } }
                if ($idx -lt 0) { $result[$k] = 'NO-ERROR-TEXT' }
                else { $result[$k] = Get-TextSha (($lines[$idx..($lines.Count - 1)]) -join "`n") }
            }
        }
    }
    Remove-Item -LiteralPath $dat -ErrorAction SilentlyContinue
    return $result
}

# name, inputs, option, kinds, runs (threads, reader)
$dflt = $DefaultThreads
$cases = @(
    @{ name = 'hot.default';   trjs = @($hot);        opt = 'default'; kinds = @('csv');          runs = @(@('1', $true), @($dflt, $true), @($maxThreads, $true), @('1', $false)) },
    @{ name = 'mid.default';   trjs = @($mid);        opt = 'default'; kinds = @('csv');          runs = @(@('1', $true), @($dflt, $true), @($maxThreads, $true), @('1', $false)) },
    @{ name = 'hot.dat';       trjs = @($hot);        opt = 'dat';     kinds = @('csv', 'dat');   runs = @(@('1', $true), @($dflt, $true)) },
    @{ name = 'mid.dat';       trjs = @($mid);        opt = 'dat';     kinds = @('csv', 'dat');   runs = @(@('1', $true), @($dflt, $true)) },
    @{ name = 'hot.puea';      trjs = @($hot);        opt = 'puea';    kinds = @('csv', 'dump');  runs = @(@('1', $true), @($dflt, $true)) },
    @{ name = 'mid.puea';      trjs = @($mid);        opt = 'puea';    kinds = @('csv', 'dump');  runs = @(@('1', $true), @($dflt, $true)) },
    @{ name = 'old.default';   trjs = @($old);        opt = 'default'; kinds = @('csv');          runs = @(@('1', $true), @($dflt, $true)) },
    @{ name = 'two.default';   trjs = @($hot, $mid);  opt = 'default'; kinds = @('csv');          runs = @(@('1', $true), @($dflt, $true)) },
    @{ name = 'trunc.dat';     trjs = @($trunc);      opt = 'dat';     kinds = @('stdout', 'dat'); runs = @(@('1', $true), @('1', $false)) }
)
$bigCase = @{ name = 'big.default'; trjs = @($bigFile); opt = 'default'; kinds = @('csv'); runs = @(@('1', $true), @($dflt, $true)) }
$bigAvailable = ($bigFile -ne '') -and (Test-Path -LiteralPath $bigFile)
if (-not $Quick) { if ($bigAvailable) { $cases += $bigCase } }

if ($MakeBaseline) {
    if (Test-Path -LiteralPath $baselineFile) { throw "Baseline already exists: $baselineFile. It is created once and never regenerated." }
    $baseline = [ordered]@{}
    foreach ($c in $cases) {
        $r = Invoke-Case $c.trjs $c.opt '1' $true $c.kinds
        foreach ($k in $c.kinds) {
            $baseline["$($c.name).$k"] = $r[$k]
            Write-Host ("BASELINE  {0,-22} {1}  ({2}s)" -f "$($c.name).$k", $r[$k].Substring(0, [math]::Min(12, $r[$k].Length)), $r.seconds)
        }
    }
    ($baseline | ConvertTo-Json) | Out-File -LiteralPath $baselineFile -Encoding ascii
    Write-Host "Baseline written: $baselineFile"
    exit 0
}

if (-not (Test-Path -LiteralPath $baselineFile)) { throw "Baseline not found: $baselineFile (run with -MakeBaseline on the pre-change build)" }
$baseline = Get-Content -LiteralPath $baselineFile -Raw | ConvertFrom-Json

$failed = 0
$total = 0
foreach ($c in $cases) {
    foreach ($run in $c.runs) {
        $threads = [string]$run[0]
        $reader = [bool]$run[1]
        $r = Invoke-Case $c.trjs $c.opt $threads $reader $c.kinds
        foreach ($k in $c.kinds) {
            $key = "$($c.name).$k"
            $expected = $baseline.$key
            $total++
            $ok = ($expected -and $r[$k] -eq $expected)
            if (-not $ok) { $failed++ }
            $tLabel = if ($threads -eq '') { 'dflt' } else { $threads }
            $rLabel = if ($reader) { 'on' } else { 'off' }
            Write-Host ("{0}  {1,-22} threads={2,-4} reader={3,-3} {4}s" -f $(if ($ok) { 'MATCH   ' } else { 'MISMATCH' }), $key, $tLabel, $rLabel, $r.seconds)
        }
    }
}
if (-not $Quick -and -not $bigAvailable) { Write-Host "NOT RUN   big.default.csv (file not found: $bigFile)" }

# Console argument checks (48MB file). stdout and stderr go to files so a stderr line is not a PowerShell error.
function Invoke-ArgCheck([string[]]$extraArgs) {
    $csv = Join-Path $outDir 'argcheck.csv'
    $outFile = Join-Path $outDir 'argcheck.stdout.txt'
    $errFile = Join-Path $outDir 'argcheck.stderr.txt'
    Remove-Item -LiteralPath $csv, $outFile, $errFile -ErrorAction SilentlyContinue
    $argLine = ('"trjfiles={0}" "csvfile={1}" {2}' -f $hot, $csv, ($extraArgs -join ' '))
    $env:SSAM_VERIFY_DUMP = $null
    $env:SSAM_PROFILE = $null
    $env:SSAM_READER_THREAD = $null
    $proc = Start-Process -FilePath $exe -ArgumentList $argLine -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    $stdoutText = ''
    $stderrText = ''
    if (Test-Path -LiteralPath $outFile) { $stdoutText = [string](Get-Content -LiteralPath $outFile -Raw) }
    if (Test-Path -LiteralPath $errFile) { $stderrText = [string](Get-Content -LiteralPath $errFile -Raw) }
    return @{ stdout = $stdoutText; stderr = $stderrText; lines = @($stdoutText -split "`r?`n") }
}

function Write-ArgCheck([string]$name, [bool]$ok) {
    $script:total++
    if (-not $ok) { $script:failed++ }
    Write-Host ("{0}  {1}" -f $(if ($ok) { 'MATCH   ' } else { 'MISMATCH' }), $name)
}

$a = Invoke-ArgCheck @('ttc=1', 'pet=5', 'nthreads=8')
$noIgnored = (($a.stdout + $a.stderr) -notmatch '(ttc|pet|nthreads) argument with no value ignored')
Write-ArgCheck 'args.short-values.no-ignored-warning' $noIgnored
Write-ArgCheck 'args.short-values.nthreads-8' ($a.lines -contains 'nThreads: 8')
Write-ArgCheck 'args.short-values.analysis-complete' ($a.lines -contains 'Analysis complete.')

$a = Invoke-ArgCheck @('pet=0')
Write-ArgCheck 'args.pet-0.range-error' ($a.stdout.Contains('not in acceptable range, (0.0, 10.0]'))
Write-ArgCheck 'args.pet-0.no-analysis' (-not ($a.lines -contains 'Analysis complete.'))

$a = Invoke-ArgCheck @('nthreads=0')
Write-ArgCheck 'args.nthreads-0.error' ($a.stdout.Contains('value 0 must be 1 or greater'))
Write-ArgCheck 'args.nthreads-0.no-analysis' (-not ($a.lines -contains 'Analysis complete.'))

Write-Host ''
Write-Host "Comparisons: $total, mismatches: $failed"
if ($failed -ne 0) { exit 1 }
exit 0
