# Stage cooked Content\FleetUI (FleetUI mod WBPs) and pack it with retoc. Copy of pack.ps1 for the FleetUI pak.
# Never touches build\out or build\stage (mining pak98 output). Refuses anything outside Content\FleetUI.
# Output: build\out-fleet\pakchunk103-FleetUI_P.{pak,ucas,utoc}
# Usage: .\pack_fleet.ps1 [-Retoc <path>] [-Name pakchunk103-FleetUI_P]
param(
    [string]$Retoc = $(if ($env:MS_MODS_ROOT) { "$env:MS_MODS_ROOT\tools\retoc\retoc.exe" } else { 'F:\MSDemoMods\tools\retoc\retoc.exe' }),
    [string]$Name = 'pakchunk103-FleetUI_P',
    [string]$Uproject = (Join-Path $PSScriptRoot '..\MiningUnitUI.uproject'),
    [string]$Version = 'UE5_7'
)
$ErrorActionPreference = 'Stop'

$proj = Split-Path (Resolve-Path $Uproject).Path
$cooked = Join-Path $proj 'Saved\Cooked\Windows\MiningUnitUI\Content'
if (-not (Test-Path (Join-Path $cooked 'FleetUI'))) { throw "run cook.ps1 first: $cooked\FleetUI missing" }
if (-not (Test-Path $Retoc)) { throw "retoc not found: $Retoc" }

$build = Join-Path $proj 'build'
$stage = Join-Path $build 'stage-fleet'
$out   = Join-Path $build 'out-fleet'
Remove-Item $stage, $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\MainSequence\Content", $out | Out-Null

# Game mounts packages as /Game under MainSequence\Content, so stage there to match /Game/FleetUI/... paths.
# Only our package: the cook also emits Global/project shader archives that must not shadow the game's.
New-Item -ItemType Directory -Force "$stage\MainSequence\Content\FleetUI" | Out-Null
Copy-Item "$cooked\FleetUI\*" "$stage\MainSequence\Content\FleetUI" -Recurse -Force   # -Recurse: Art\ subfolder if any

$pkgDir = "$stage\MainSequence\Content\FleetUI"
$bad = Get-ChildItem $stage -Recurse -File | Where-Object { $_.FullName -notlike "$pkgDir\*" }
if ($bad) { throw "files staged outside Content\FleetUI: $($bad.FullName -join ', ')" }

# FleetUI may reference only /Game/FleetUI/* and the mining textures /Game/MiningUnitUI/* (tests\test_asset_refs.py --cooked).
$refTest = Join-Path $PSScriptRoot '..\..\..\tests\test_asset_refs.py'
python -I $refTest --cooked $pkgDir --allow /Game/MiningUnitUI/
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
