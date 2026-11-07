param([switch]$WithContainer)
$ErrorActionPreference = 'Stop'
# Hidden container default machine (BP_BPUnitStoreObj_01). OFF: crashes game on block creation, cause under investigation.
$withContainer = $false
if ($WithContainer) { $withContainer = $true }
Set-Location $PSScriptRoot
$d = $PSScriptRoot
$root = if ($env:MS_MODS_ROOT) { $env:MS_MODS_ROOT } else { 'F:\MSDemoMods' }
$retoc = "$root\tools\retoc\retoc.exe"
$name = 'pakchunk100-BlueprintUnit_P'
Remove-Item "$d\stage" -Recurse -Force -ErrorAction SilentlyContinue
if ($withContainer) { $env:BPU_CONTAINER = '1' } else { Remove-Item Env:BPU_CONTAINER -ErrorAction SilentlyContinue }
node clone.js; if ($LASTEXITCODE) { throw 'clone failed' }
python -I patch_registry.py 3 B "$d\stage\MainSequence\Content\Data"; if ($LASTEXITCODE) { throw 'registry failed' }
python -I patch_research.py "$d\stage\MainSequence\Content\Data\Research"; if ($LASTEXITCODE) { throw 'research failed' }
if ($withContainer) { python -I patch_bpo_machine.py "$d\stage\MainSequence\Content\Blueprints\BuildingBlocks"; if ($LASTEXITCODE) { throw 'bpo machine failed' } }
else { Write-Host 'container disabled: skip patch_bpo_machine.py (use -WithContainer)' }
New-Item -ItemType Directory -Force "$d\out" | Out-Null
& $retoc to-zen --version UE5_6 --no-parallel "$d\stage" "$d\out\$name.utoc"; if ($LASTEXITCODE) { throw 'to-zen failed' }
$v = & $retoc verify "$d\out\$name.utoc" 2>&1 | Out-String
if ($v -notmatch 'verified') { throw "verify: $v" }
& $retoc list --path --size "$d\out\$name.utoc"
Get-ChildItem "$d\out" -Filter "$name.*" | ForEach-Object { "{0}  {1}  {2}" -f $_.Name, $_.Length, (Get-FileHash $_.FullName).Hash }
