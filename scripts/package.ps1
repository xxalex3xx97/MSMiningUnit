# Build the private-preview release zip (mining only) and optionally the dev menu zip.
# Usage: .\scripts\package.ps1 [-Version 0.0.1] [-DevMenu]
# WHAT: stages the mining mod (DLL, Lua, 2 paks, docs) into dist\stage and zips it to dist\MSMiningUnit-<ver>-preview-for-developer.zip.
# WHY: one reproducible zip for the developer; the game install is only READ (paks are copied, never written).
# DEPENDS ON: the built MiningBenchMod.dll (build.ps1 output), the installed game Paks, mods\MiningUnitUI, mods\MSDevMenu, README.md, docs\.
# USED BY: manual release step (see README "Private preview"). Output dist\ is git-ignored; never commit zips.
param(
    [string]$Version = '0.0.1',
    [string]$Game = $(if ($env:MS_GAME_DIR) { $env:MS_GAME_DIR } else { 'F:\SteamLibrary\steamapps\common\Main Sequence Demo\Windows\MainSequence' }),
    [string]$Dll = $(if ($env:MS_MODS_ROOT) { "$env:MS_MODS_ROOT\build\RE-UE4SS\build\Game__Shipping__Win64\bin\MiningBenchMod.dll" } else { 'F:\MSDemoMods\build\RE-UE4SS\build\Game__Shipping__Win64\bin\MiningBenchMod.dll' }),
    [string]$UiPakDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'ue\MiningUnitUI\build\out'),
    [switch]$DevMenu
)
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$dist  = Join-Path $root 'dist'
$stage = Join-Path $dist 'stage'
$paks  = Join-Path $Game 'Content\Paks'
$ue4ss = 'Binaries\Win64\ue4ss\Mods'

function Require($p) { if (-not (Test-Path -LiteralPath $p)) { throw "missing source: $p" } }

# ---- sources must exist ----
Require $Dll
Require (Join-Path $Game 'Binaries\Win64')
foreach ($f in 'pakchunk98-MiningUnitUI_P', 'pakchunk99-ClonedBlock_P') {
    foreach ($ext in 'pak', 'ucas', 'utoc') { Require (Join-Path $paks "$f.$ext") }
}
Require (Join-Path $root 'mods\MiningUnitUI\enabled.txt')
Require (Join-Path $root 'mods\MiningUnitUI\Scripts')
Require (Join-Path $root 'README.md')
Require (Join-Path $root 'docs\ASSETS.md')

# ---- DLL checks: warn when stale, must carry the version text ----
$dllItem = Get-Item -LiteralPath $Dll
if ((Get-Date) - $dllItem.LastWriteTime -gt [TimeSpan]::FromMinutes(5)) { Write-Warning "DLL is older than 5 min - rebuild with build.ps1 first" }
$bytes = [IO.File]::ReadAllBytes($dllItem.FullName)
$ascii = [Text.Encoding]::ASCII.GetString($bytes)
if ($ascii -notmatch [regex]::Escape($Version)) { throw "DLL does not contain version text '$Version' (kVersion in mb_version.hpp)" }

# ---- fresh stage ----
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

function Put($src, $rel) {
    $dst = Join-Path $stage $rel
    New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
    Copy-Item -LiteralPath $src -Destination $dst -Force
}

# mining mod: DLL + Lua window + the 2 paks (copied from the installed game)
Put $dllItem.FullName "$ue4ss\MiningBenchMod\dlls\main.dll"
Put (Join-Path $root 'mods\MiningUnitUI\enabled.txt') "$ue4ss\MiningUnitUI\enabled.txt"
Get-ChildItem -LiteralPath (Join-Path $root 'mods\MiningUnitUI\Scripts') -File | ForEach-Object { Put $_.FullName "$ue4ss\MiningUnitUI\Scripts\$($_.Name)" }
foreach ($f in 'pakchunk98-MiningUnitUI_P', 'pakchunk99-ClonedBlock_P') {
    # the UI pak comes from the fresh pack.ps1 output (own art) when present, else from the game install
    $dir = if ($f -like 'pakchunk98*' -and (Test-Path (Join-Path $UiPakDir "$f.pak"))) { $UiPakDir } else { $paks }
    foreach ($ext in 'pak', 'ucas', 'utoc') { Put (Join-Path $dir "$f.$ext") "Content\Paks\$f.$ext" }
}

# docs
Put (Join-Path $root 'README.md') 'README.md'
Put (Join-Path $root 'docs\ASSETS.md') 'docs\ASSETS.md'

# DevReference.html is deliberately NOT staged (developer's own reference doc)
$install = @"
Main Sequence - Mining Unit, private preview $Version
======================================================

Unofficial fan mod. All game assets, names and code belong to the game's developer/publisher.
Private preview for the developer only. Do not redistribute.

Requires RE-UE4SS for the game (Binaries\Win64\ue4ss). Close the game before installing.

INSTALL
1. Copy the Binaries and Content folders from this zip into:
   <game>\Windows\MainSequence\   (merge; keep existing files)
   Steam default: <SteamLibrary>\steamapps\common\Main Sequence Demo\Windows\MainSequence
2. Open Binaries\Win64\ue4ss\Mods\mods.txt and add these two lines (or set them to 1):
       MiningBenchMod : 1
       MiningUnitUI : 1
3. Start the game. F4 opens the Mining Unit window.

UNINSTALL
1. Set both lines in mods.txt to 0 (or delete them).
2. Delete Binaries\Win64\ue4ss\Mods\MiningBenchMod and Binaries\Win64\ue4ss\Mods\MiningUnitUI.
3. Delete Content\Paks\pakchunk98-MiningUnitUI_P.* and Content\Paks\pakchunk99-ClonedBlock_P.*.

DISABLE WITHOUT UNINSTALLING
Set kill_switch=1 in Binaries\Win64\ue4ss\Mods\MiningBenchMod\dlls\MiningBench.ini.
The mod then does nothing (hot reload). 0 turns it back on.
"@
Set-Content -LiteralPath (Join-Path $stage 'INSTALL.txt') -Value $install -Encoding ASCII

# ---- zip ----
$zip = Join-Path $dist "MSMiningUnit-$Version-preview-for-developer.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
# Compress-Archive (PS 5.1) writes backslash entry names; write the entries ourselves with '/'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$za = [IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    $base = (Resolve-Path $stage).Path.TrimEnd('\') + '\'
    Get-ChildItem $stage -Recurse -File | ForEach-Object {
        $entry = $_.FullName.Substring($base.Length).Replace('\', '/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $_.FullName, $entry, 'Optimal')
    }
} finally { $za.Dispose() }
Write-Host "wrote $zip"
Get-FileHash -Algorithm SHA256 -LiteralPath $zip | ForEach-Object { Write-Host "SHA256 $($_.Hash)  $($_.Path)" }

# ---- optional dev menu zip (separate mod, not in the release) ----
if ($DevMenu) {
    $dstage = Join-Path $dist 'stage-devmenu'
    if (Test-Path -LiteralPath $dstage) { Remove-Item -LiteralPath $dstage -Recurse -Force }
    Copy-Item -LiteralPath (Join-Path $root 'mods\MSDevMenu') -Destination (Join-Path $dstage "$ue4ss\MSDevMenu") -Recurse -Force
    $dzip = Join-Path $dist "MSDevMenu-$Version.zip"
    if (Test-Path -LiteralPath $dzip) { Remove-Item -LiteralPath $dzip -Force }
    Compress-Archive -Path (Join-Path $dstage '*') -DestinationPath $dzip -CompressionLevel Optimal
    Write-Host "wrote $dzip"
    Get-FileHash -Algorithm SHA256 -LiteralPath $dzip | ForEach-Object { Write-Host "SHA256 $($_.Hash)  $($_.Path)" }
}
