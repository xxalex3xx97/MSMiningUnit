# Stage cooked Content\MiningUnitUI (incl. Art\ = our game-texture copies) and pack it with retoc (same method as mods\MiningUnitBlockPak README step 3).
# Refuses to pack anything outside Content\MiningUnitUI or any /Game/ path outside /Game/MiningUnitUI/.
# Output: build\out\pakchunk98-MiningUnitUI_P.{pak,ucas,utoc}
# Usage: .\pack.ps1 [-Retoc <path>] [-Name pakchunk98-MiningUnitUI_P]
param(
    [string]$Retoc = $(if ($env:MS_MODS_ROOT) { "$env:MS_MODS_ROOT\tools\retoc\retoc.exe" } else { 'F:\MSDemoMods\tools\retoc\retoc.exe' }),
    [string]$Name = 'pakchunk98-MiningUnitUI_P',
    [string]$Uproject = (Join-Path $PSScriptRoot '..\MiningUnitUI.uproject'),
    [string]$Version = 'UE5_7'
)
$ErrorActionPreference = 'Stop'

$proj = Split-Path (Resolve-Path $Uproject).Path
$cooked = Join-Path $proj 'Saved\Cooked\Windows\MiningUnitUI\Content'
if (-not (Test-Path $cooked)) { throw "run cook.ps1 first: $cooked missing" }
if (-not (Test-Path $Retoc)) { throw "retoc not found: $Retoc" }

$build = Join-Path $proj 'build'
$stage = Join-Path $build 'stage'
$out   = Join-Path $build 'out'
Remove-Item $stage, $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\MainSequence\Content", $out | Out-Null

# Project folder is MiningUnitUI in the cook, but the game mounts its packages as /Game under
# MainSequence\Content. Stage under MainSequence\Content so the package paths match the game (/Game/MiningUnitUI/...).
# Only our package: the cook also emits Global/project shader archives that must not shadow the game's.
New-Item -ItemType Directory -Force "$stage\MainSequence\Content\MiningUnitUI" | Out-Null
Copy-Item "$cooked\MiningUnitUI\*" "$stage\MainSequence\Content\MiningUnitUI" -Recurse -Force   # -Recurse: Art\ subfolder

$pkgDir = "$stage\MainSequence\Content\MiningUnitUI"
$bad = Get-ChildItem $stage -Recurse -File | Where-Object { $_.FullName -notlike "$pkgDir\*" }
if ($bad) { throw "files staged outside Content\MiningUnitUI: $($bad.FullName -join ', ')" }

# Our package may reference only /Game/MiningUnitUI/* (tests\test_asset_refs.py --cooked).
$refTest = Join-Path $PSScriptRoot '..\..\..\tests\test_asset_refs.py'
python -I $refTest --cooked $pkgDir
if ($LASTEXITCODE -ne 0) { throw "asset reference scan failed (exit $LASTEXITCODE): game paths in cooked package" }

$utoc = Join-Path $out "$Name.utoc"
& $Retoc to-zen --version $Version $stage $utoc
if ($LASTEXITCODE -ne 0) { throw "retoc to-zen failed (exit $LASTEXITCODE)" }

$v = & $Retoc verify $utoc 2>&1 | Out-String
if ($v -notmatch 'verified') { throw "retoc verify did not print 'verified': $v" }
& $Retoc list --path --size $utoc

Get-ChildItem $out | ForEach-Object {
    "{0}  {1:N0} B  {2}" -f $_.Name, $_.Length, (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
}
