# Package a mod release zip + manifest.txt (local only: no gh, no git, no upload).
# Files are explicit "<local file>=<zip path>" pairs (zip path = Mods/... or Paks/...). Sources and user data are refused.
# Output: <OutDir>\<Name>-<Version>.zip + manifest.txt (BOM-less, LF).
# Example: powershell -File <path>\scripts\release-mod.ps1 -Name MiningBenchMod -Repo <github-user>/<repo> -Version 0.1.0 -Files @('<path>\main.dll=Mods/MiningBenchMod/dlls/main.dll') -Empty @('Mods/MiningBenchMod/enabled.txt')
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_.-]+$')][string]$Name,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')][string]$Repo,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory)][string[]]$Files,
    [string[]]$Empty = @(),
    [string]$MinNative = '0.5.0',
    [string]$OutDir
)
$ErrorActionPreference = 'Stop'
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'   # Git's tar cannot write zip
$utf8 = New-Object System.Text.UTF8Encoding($false)
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot "..\build\release\$Name\$Version" }
$OutDir = [IO.Path]::GetFullPath($OutDir)
$work = Join-Path $env:TEMP ('mod_release_' + [guid]::NewGuid().ToString('N'))
$zipName = "$Name-$Version.zip"

# zip path -> must be Mods/... or Paks/..., no traversal, no sources, no user data
function Check-ZipPath([string]$p) {
    if ($p -match '(^|/)\.\.(/|$)|^/|:' -or $p -notmatch '^(Mods|Paks)/[^/]+') { throw "bad zip path: $p" }
    if ($p -match '\.(cpp|hpp|h|c|cc|py|ps1|js|md|ini|log|bat|sln|vcxproj)$') { throw "refused (source/user data): $p" }
    if ($p -match '^Mods/[^/]+/dlls/[^/]+\.txt$') { throw "refused (dump/data txt in dlls): $p" }
}

$pairs = @()
foreach ($f in $Files) {
    $i = $f.LastIndexOf('=')
    if ($i -lt 1) { throw "Files entry must be <local>=<zip path>: $f" }
    $src = $f.Substring(0, $i); $dst = $f.Substring($i + 1).Replace('\', '/')
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { throw "missing file: $src" }
    Check-ZipPath $dst
    $pairs += [pscustomobject]@{ Src = $src; Dst = $dst }
}
foreach ($e in $Empty) { $d = $e.Replace('\', '/'); Check-ZipPath $d; $pairs += [pscustomobject]@{ Src = $null; Dst = $d } }
$dups = $pairs | Group-Object Dst | Where-Object Count -gt 1
if ($dups) { throw "duplicate zip paths: $($dups.Name -join ', ')" }
if ($pairs.Dst -contains "Mods/$Name/mscore.mod") { throw "do not list Mods/$Name/mscore.mod; the script writes it" }

try {
    $stg = Join-Path $work 'stg'
    New-Item -ItemType Directory -Force $stg | Out-Null
    foreach ($p in $pairs) {
        $to = Join-Path $stg ($p.Dst -replace '/', '\')
        New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
        if ($p.Src) { Copy-Item -LiteralPath $p.Src -Destination $to } else { [IO.File]::WriteAllBytes($to, [byte[]]@()) }
    }
    $modDir = Join-Path $stg "Mods\$Name"
    New-Item -ItemType Directory -Force $modDir | Out-Null
    [IO.File]::WriteAllText((Join-Path $modDir 'mscore.mod'), "name=$Name`nrepo=$Repo`nversion=$Version`n", $utf8)

    $tops = @(Get-ChildItem -LiteralPath $stg -Directory | ForEach-Object Name)
    New-Item -ItemType Directory -Force $OutDir | Out-Null
    $zip = Join-Path $OutDir $zipName
    if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
    & $tar -a -cf $zip -C $stg @tops
    if ($LASTEXITCODE -ne 0) { throw 'tar failed' }

    # listing re-check: only files we put in, nothing else
    $listed = @(& $tar -tf $zip | ForEach-Object { $_.Trim() -replace '\\', '/' } | Where-Object { $_ -and $_ -notmatch '/$' })
    $want = @($pairs.Dst) + "Mods/$Name/mscore.mod"
    $extra = @($listed | Where-Object { $_ -notin $want })
    $missing = @($want | Where-Object { $_ -notin $listed })
    if ($extra -or $missing) { throw "zip listing mismatch. extra: $($extra -join ', ') missing: $($missing -join ', ')" }
    foreach ($l in $listed) { Check-ZipPath $l }

    $sha = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
    $size = (Get-Item -LiteralPath $zip).Length
    $m = "name=$Name`nversion=$Version`nzip=$zipName`nsha256=$sha`nsize=$size`n"
    if ($MinNative) { $m += "min_mscore_native=$MinNative`n" }
    [IO.File]::WriteAllText((Join-Path $OutDir 'manifest.txt'), $m, $utf8)
    "wrote $zip ($size B, sha256 $sha)"
    $listed | ForEach-Object { "  $_" }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
