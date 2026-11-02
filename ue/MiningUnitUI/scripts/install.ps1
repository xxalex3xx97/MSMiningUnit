# Install pakchunk98-MiningUnitUI_P into the game Paks dir. Refuses if the game is running.
# Backs up any existing pakchunk98 files first. Does not touch saves.
# Usage: .\install.ps1 [-PaksDir <path>] [-Name pakchunk98-MiningUnitUI_P]
# Default Paks dir = $env:MS_GAME_DIR\Content\Paks (see README "Configuration").
param(
    [string]$PaksDir = $(if ($env:MS_GAME_DIR) { "$env:MS_GAME_DIR\Content\Paks" } else { 'F:\SteamLibrary\steamapps\common\Main Sequence Demo\Windows\MainSequence\Content\Paks' }),
    [string]$Name = 'pakchunk98-MiningUnitUI_P'
)
$ErrorActionPreference = 'Stop'

if (Get-Process -Name 'MainSequence*' -ErrorAction SilentlyContinue) { throw 'Game is running - close it first' }
if (-not (Test-Path $PaksDir)) { throw "Paks dir not found: $PaksDir" }

$proj = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out = Join-Path $proj 'build\out'
$exts = '.pak', '.ucas', '.utoc'
$files = $exts | ForEach-Object { Join-Path $out "$Name$_" }
foreach ($f in $files) { if (-not (Test-Path $f)) { throw "missing build output: $f (run pack.ps1)" } }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$bk = Join-Path $proj "build\backup\pak98-$stamp"
$existing = $exts | ForEach-Object { Join-Path $PaksDir "$Name$_" } | Where-Object { Test-Path $_ }
if ($existing) {
    New-Item -ItemType Directory -Force $bk | Out-Null
    $existing | ForEach-Object { Copy-Item $_ $bk }
    "backup: $bk ($($existing.Count) files)"
} else {
    "no previous $Name in Paks, nothing to back up"
}

$files | ForEach-Object { Copy-Item $_ $PaksDir -Force }
"installed to $PaksDir"
$exts | ForEach-Object {
    $p = Join-Path $PaksDir "$Name$_"
    "{0}  {1}" -f (Get-FileHash $p -Algorithm SHA256).Hash.ToLower(), $p
}
