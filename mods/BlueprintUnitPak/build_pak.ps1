param([ValidateSet('none', 'vanilla', 'clone')][string]$Container = 'none')
$ErrorActionPreference = 'Stop'
# Hidden container default machine on BPO_BlueprintUnit (DefaultMachines, unversioned header idx 24). Variants:
#   none    = no container, 5 packages                    -> out\none
#   vanilla = container = vanilla BP_ContainerObject_2 (32 slots, bonus 10), 5 packages -> out\vanilla
#   clone   = container = clone BP_BPUnitStoreObj_01 (120 slots, bonus 200), 6 packages -> out\clone
# Each variant writes out\<variant>\pakchunk100-BlueprintUnit_P.{pak,ucas,utoc}; the last built is also copied to out\.
Set-Location $PSScriptRoot
$d = $PSScriptRoot
$root = if ($env:MS_MODS_ROOT) { $env:MS_MODS_ROOT } else { 'F:\MSDemoMods' }
$retoc = "$root\tools\retoc\retoc.exe"
$name = 'pakchunk100-BlueprintUnit_P'
$bpo = "$d\stage\MainSequence\Content\Blueprints\BuildingBlocks"
Remove-Item "$d\stage" -Recurse -Force -ErrorAction SilentlyContinue
if ($Container -eq 'clone') { $env:BPU_CONTAINER = '1' } else { Remove-Item Env:BPU_CONTAINER -ErrorAction SilentlyContinue }
node clone.js; if ($LASTEXITCODE) { throw 'clone failed' }
python -I patch_registry.py 3 B "$d\stage\MainSequence\Content\Data"; if ($LASTEXITCODE) { throw 'registry failed' }
python -I patch_research.py "$d\stage\MainSequence\Content\Data\Research"; if ($LASTEXITCODE) { throw 'research failed' }
switch ($Container) {
    'none' { Write-Host 'container none: skip patch_bpo_machine.py' }
    'vanilla' {
        python -I patch_bpo_machine.py $bpo '/Game/Blueprints/Machines/BP_ContainerObject_2' 'BP_ContainerObject_2_C'
        if ($LASTEXITCODE) { throw 'bpo machine (vanilla) failed' }
    }
    'clone' {
        python -I patch_bpo_machine.py $bpo '/Game/Blueprints/Machines/BP_BPUnitStoreObj_01' 'BP_BPUnitStoreObj_01_C'
        if ($LASTEXITCODE) { throw 'bpo machine (clone) failed' }
    }
}
$outDir = "$d\out\$Container"
New-Item -ItemType Directory -Force $outDir | Out-Null
& $retoc to-zen --version UE5_6 --no-parallel "$d\stage" "$outDir\$name.utoc"; if ($LASTEXITCODE) { throw 'to-zen failed' }
$v = & $retoc verify "$outDir\$name.utoc" 2>&1 | Out-String
if ($v -notmatch 'verified') { throw "verify: $v" }
& $retoc list --path --size "$outDir\$name.utoc"
Copy-Item "$outDir\$name.*" "$d\out" -Force
Get-ChildItem $outDir -Filter "$name.*" | ForEach-Object { "{0}  {1}  {2}" -f $_.Name, $_.Length, (Get-FileHash $_.FullName).Hash }
