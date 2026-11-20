# rebuild-after-update.ps1: post-game-update rebuild of the mod paks (99/100/101).
# Default = rebuild only (no install). -Check = rebuild to temp + report STALE (exit 1 if any). -Install = install verified paks (user step).
param(
  [switch]$Check,
  [switch]$Install,
  [string]$OutDir,
  [string]$GameRoot = 'F:\SteamLibrary\steamapps\common\Main Sequence',
  [string]$Retoc = 'F:\MSDemoMods\tools\retoc\retoc.exe',
  [string]$BpuDir = 'F:\ms-bpunit\mods\BlueprintUnitPak',
  [string]$MsmDir = 'F:\MSDemoMods\mods\MSMiningModel',
  [string]$Work,
  [switch]$KeepWork
)
$ErrorActionPreference = 'Stop'
$stamp = Get-Date -f yyyyMMdd-HHmmss
$paks = "$GameRoot\Windows\MainSequence\Content\Paks"
$repo = Split-Path $PSScriptRoot
$names = 'pakchunk99-ClonedBlock_P', 'pakchunk100-BlueprintUnit_P', 'pakchunk101-MSMiningModel_P'
$exitCode = 0
if ($Check -and $Install) { throw '-Check and -Install are mutually exclusive' }
$outGiven = [bool]$OutDir
if (-not $Work) { $Work = "$([IO.Path]::GetPathRoot($GameRoot))ms-rebuild-tmp\$stamp" }
if (-not $OutDir) { $OutDir = "$env:TEMP\ms-rebuilt-paks-$stamp" }

# Preflight
foreach ($p in @($Retoc, $paks, "$BpuDir\build_pak.ps1", "$MsmDir\clone_bp.js")) {
  if (-not (Test-Path -LiteralPath $p)) { throw "missing: $p" }
}
Get-Command node -ErrorAction Stop | Out-Null
Get-Command python -ErrorAction Stop | Out-Null

# Game must be closed (Paks are read-locked / mid-load otherwise)
function Assert-GameClosed {
  if (Get-Process | Where-Object { $_.ProcessName -match '^Main ?Sequence' }) { throw 'game running, close it' }
}
function Get-Sha([string]$p) {
  if (Test-Path -LiteralPath $p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash } else { $null }
}
function Compare-Paks {
  foreach ($n in $names) {
    $same = $true
    foreach ($ext in '.ucas', '.utoc') {
      $a = Get-Sha "$paks\$n$ext"
      $b = Get-Sha "$OutDir\$n$ext"
      if (-not $a -or $a -ne $b) { $same = $false }
    }
    [pscustomobject]@{
      Name           = $n
      Verify         = if ($res.ContainsKey($n)) { 'OK' } else { 'FAIL' }
      'Live==Rebuilt' = $same
      State          = if ($same) { 'FRESH' } else { 'STALE' }
    }
  }
}
Assert-GameClosed

New-Item -ItemType Directory -Force -Path $Work | Out-Null
try {
  function Step($name, [scriptblock]$b) { Write-Host "== $name"; & $b }

  Step 'extract vanilla' {
    New-Item -ItemType Directory -Force -Path "$Work\vanilla-in" | Out-Null
    foreach ($f in 'global.utoc', 'global.ucas', 'pakchunk0-Windows.pak', 'pakchunk0-Windows.utoc', 'pakchunk0-Windows.ucas') {
      try { New-Item -ItemType HardLink -Path "$Work\vanilla-in\$f" -Target "$paks\$f" | Out-Null }
      catch { Copy-Item -LiteralPath "$paks\$f" -Destination "$Work\vanilla-in\$f" }
    }
    & $Retoc to-legacy "$Work\vanilla-in" "$Work\legacy_full"
    if ($LASTEXITCODE -ne 0) { throw "retoc to-legacy failed ($LASTEXITCODE)" }
  }

  Step 'donor copy' {
    $donors = @(
      'Blueprints\BuildingBlocks\BPO_WorkstationBlock',
      'Blueprints\BuildingBlocks\BPO_CommandModule',
      'Blueprints\BuildingBlocks\BP_CommandModule',
      'Blueprints\Machines\BPO_Workbench',
      'Blueprints\Machines\BP_Workbench',
      'Blueprints\Machines\BPO_WorkbenchMachine',
      'Blueprints\Machines\BP_WorkbenchMachine',
      'Blueprints\Machines\BP_Container_2',
      'Blueprints\Machines\BP_ContainerObject_2',
      'Blueprints\Widgets\Windows\WBP_Workbench',
      'Data\Localization\STB_Interaction',
      'Data\DAT_BuildingBlockRegistry',
      'Data\Registries-Demo\DAT_BuildingBlockRegistry_Demo',
      'Data\Research\RSC_MiningLaser',
      'Data\Research\RSC_ComputationScience'
    )
    foreach ($rel in $donors) {
      foreach ($ext in '.uasset', '.uexp') {
        $src = "$Work\legacy_full\MainSequence\Content\$rel$ext"
        if (-not (Test-Path -LiteralPath $src)) { throw "donor missing: $src" }
        $dst = "$Work\legacy\MainSequence\Content\$rel$ext"
        New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
        Copy-Item -LiteralPath $src -Destination $dst
      }
    }
    if (Test-Path -LiteralPath "$Work\legacy_full\scriptobjects.bin") {
      Copy-Item -LiteralPath "$Work\legacy_full\scriptobjects.bin" -Destination "$Work\legacy\"
    }
    Remove-Item -LiteralPath "$Work\legacy_full" -Recurse -Force
  }

  Step 'scratch buildroot' {
    New-Item -ItemType Directory -Force -Path "$Work\tools\retoc", "$Work\mods\MiningUnitBlockPak", "$Work\mods\BlueprintUnitPak", "$Work\mods\MSMiningModel" | Out-Null
    Copy-Item "$(Split-Path $Retoc)\*" "$Work\tools\retoc" -Recurse -Force
    foreach ($f in 'clone.js', 'patch_registry.py', 'patch_research.py') {
      Copy-Item -LiteralPath "$repo\mods\MiningUnitBlockPak\$f" -Destination "$Work\mods\MiningUnitBlockPak\"
    }
    Copy-Item -LiteralPath "$repo\mods\MiningUnitBlockPak\src" -Destination "$Work\mods\MiningUnitBlockPak\" -Recurse -Force
    foreach ($f in 'build_pak.ps1', 'clone.js', 'patch_registry.py', 'patch_research.py', 'patch_bpo_machine.py') {
      Copy-Item -LiteralPath "$BpuDir\$f" -Destination "$Work\mods\BlueprintUnitPak\"
    }
    $js = [IO.File]::ReadAllText("$MsmDir\clone_bp.js")
    $workSlash = $Work -replace '\\', '/'
    $js = $js.Replace("'F:/MSDemoMods'", "'$workSlash'")
    [IO.File]::WriteAllText("$Work\mods\MSMiningModel\clone_bp.js", $js, (New-Object Text.UTF8Encoding $false))
    $env:MS_MODS_ROOT = $Work
  }

  $res = @{}
  $retocExe = "$Work\tools\retoc\retoc.exe"
  $mu = "$Work\mods\MiningUnitBlockPak"
  New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

  Step 'pak99 ClonedBlock' {
    Set-Location $mu
    & node clone.js
    if ($LASTEXITCODE -ne 0) { throw 'clone99' }
    robocopy "$mu\src" "$mu\stageB" /E /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy src->stageB ($LASTEXITCODE)" }
    New-Item -ItemType Directory -Force -Path "$mu\stageB\MainSequence\Content\Data" | Out-Null
    & python -I patch_registry.py 2 B "$mu\stageB\MainSequence\Content\Data"
    if ($LASTEXITCODE -ne 0) { throw 'reg99' }
    robocopy "$mu\stageB" "$mu\stageC" /E /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy stageB->stageC ($LASTEXITCODE)" }
    New-Item -ItemType Directory -Force -Path "$mu\stageC\MainSequence\Content\Data\Research" | Out-Null
    & python -I patch_research.py "$mu\stageC\MainSequence\Content\Data\Research"
    if ($LASTEXITCODE -ne 0) { throw 'res99' }
    & $retocExe to-zen --version UE5_6 --no-parallel "$mu\stageC" "$OutDir\pakchunk99-ClonedBlock_P.utoc"
    if ($LASTEXITCODE -ne 0) { throw 'zen99' }
  }

  Step 'pak100 BlueprintUnit' {
    Set-Location "$Work\mods\BlueprintUnitPak"
    & powershell -NoProfile -File .\build_pak.ps1 -Container clone
    if ($LASTEXITCODE -ne 0) { throw 'bpu100' }
    Copy-Item -Path "$Work\mods\BlueprintUnitPak\out\clone\pakchunk100-BlueprintUnit_P.*" -Destination $OutDir -Force
  }

  Step 'pak101 MSMiningModel' {
    Set-Location "$Work\mods\MSMiningModel"
    & node clone_bp.js
    if ($LASTEXITCODE -ne 0) { throw 'clone101' }
    & $retocExe to-zen --version UE5_6 --no-parallel "$Work\mods\MSMiningModel\stageBP" "$OutDir\pakchunk101-MSMiningModel_P.utoc"
    if ($LASTEXITCODE -ne 0) { throw 'zen101' }
  }

  Step 'verify' {
    $ErrorActionPreference = 'Continue'
    foreach ($n in $names) {
      $v = & $retocExe verify "$OutDir\$n.utoc" 2>&1 | Out-String
      if ($v -notmatch 'verified') { throw $v }
      $res[$n] = $v.Trim()
      Write-Host "$n : $($v.Trim())"
    }
  }

  Step 'compare' {
    $rows = @(Compare-Paks)
    Write-Host "Live paks vs rebuilt ($OutDir):"
    $rows | Format-Table -AutoSize | Out-String | Write-Host
    if ($Check) {
      if ($rows | Where-Object { $_.State -eq 'STALE' }) { $script:exitCode = 1 }
    }
    elseif ($Install) {
      if ($rows | Where-Object { $_.Verify -ne 'OK' }) { throw 'verify not OK for all 3, install aborted' }
      Assert-GameClosed
      $bk = "$env:USERPROFILE\.claude\jobs\rollback-$(Get-Date -f MMdd)\paks-pre-install-$(Get-Date -f HHmm)"
      New-Item -ItemType Directory -Force -Path $bk | Out-Null
      foreach ($n in $names) { Get-ChildItem -LiteralPath $paks -Filter "$n.*" | Copy-Item -Destination $bk -Force }
      foreach ($n in $names) {
        Get-ChildItem -LiteralPath $OutDir -Filter "$n.*" | Where-Object { $_.Extension -in '.pak', '.utoc', '.ucas' } |
          Copy-Item -Destination $paks -Force
      }
      $post = @(Compare-Paks) | Select-Object Name, Verify, 'Live==Rebuilt', State, @{ n = 'Installed'; e = { if ($_.State -eq 'FRESH') { 'PASS' } else { 'FAIL' } } }
      Write-Host 'After install:'
      $post | Format-Table -AutoSize | Out-String | Write-Host
      Write-Host "backup of previous live paks: $bk"
      if ($post | Where-Object { $_.Installed -eq 'FAIL' }) { $script:exitCode = 1 }
    }
  }
}
finally {
  Set-Location $env:TEMP
  if (-not $KeepWork -and (Test-Path -LiteralPath $Work)) { Remove-Item -LiteralPath $Work -Recurse -Force }
  if ($Check -and -not $outGiven -and (Test-Path -LiteralPath $OutDir)) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
}
exit $exitCode
