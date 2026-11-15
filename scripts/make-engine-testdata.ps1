# Creates derived TRJ test inputs under .local/testdata from the 48MB sample.
#   gen_oldformat.trj  - format version 1.04, no Z option byte, no Z values
#   gen_truncated.trj  - cut in the middle of a vehicle record (error path)
# Windows PowerShell 5.1 compatible. ASCII only.
[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dataDir = Join-Path $repoRoot '.local\testdata'
$configFile = Join-Path $dataDir 'test-files.json'
if (-not (Test-Path -LiteralPath $configFile)) { throw "Test file list not found: $configFile (see scripts/verify-engine.ps1)" }
$source = (Get-Content -LiteralPath $configFile -Raw -Encoding UTF8 | ConvertFrom-Json).small
$oldFormat = Join-Path $dataDir 'gen_oldformat.trj'
$truncated = Join-Path $dataDir 'gen_truncated.trj'

if (-not (Test-Path -LiteralPath $source)) {
    throw "Source TRJ not found: $source"
}
if (-not $Force -and (Test-Path -LiteralPath $oldFormat) -and (Test-Path -LiteralPath $truncated)) {
    Write-Host 'Derived test inputs already exist. Use -Force to recreate.'
    return
}

Add-Type -TypeDefinition @'
using System;
using System.IO;

public static class TrjTestData
{
    // Record layout follows SSAM::ReadInputFormat / ReadDimensions / ReadVehicle.
    public static string Make(string src, string oldPath, string truncPath)
    {
        byte[] d = File.ReadAllBytes(src);
        int p = 0;
        if (d[p] != 0) throw new Exception("FORMAT record expected");
        byte endian = d[p + 1];
        if (endian != (byte)'L') throw new Exception("little-endian source expected");
        float version = BitConverter.ToSingle(d, p + 2);
        bool hasZ = version > 1.04;
        if (!hasZ) throw new Exception("source must be a new-format file");
        p += 6 + 1;
        if (d[p] != 1) throw new Exception("DIMENSIONS record expected");
        int dimStart = p;
        p += 1 + 1 + 4 + 16;
        int bodyStart = p;

        using (var o = new BinaryWriter(File.Create(oldPath)))
        {
            o.Write((byte)0); o.Write(endian); o.Write(1.04f);
            o.Write(d, dimStart, bodyStart - dimStart);
            long vehicles = 0, steps = 0;
            int truncAt = -1;
            while (p < d.Length)
            {
                byte t = d[p];
                if (t == 2)
                {
                    o.Write(d, p, 5); p += 5; steps++;
                }
                else if (t == 3)
                {
                    if (truncAt < 0 && p > d.Length / 2) truncAt = p + 7;
                    o.Write(d, p, 1 + 4 + 4 + 1 + 32);
                    p += 1 + 4 + 4 + 1 + 40; vehicles++;
                }
                else throw new Exception("bad record type " + t + " at " + p);
            }
            if (truncAt < 0) throw new Exception("no truncation point found");
            using (var tr = File.Create(truncPath)) { tr.Write(d, 0, truncAt); }
            return "steps=" + steps + " vehicles=" + vehicles + " truncatedAt=" + truncAt;
        }
    }
}
'@

$result = [TrjTestData]::Make($source, $oldFormat, $truncated)
Write-Host "Created: $oldFormat"
Write-Host "Created: $truncated"
Write-Host $result
