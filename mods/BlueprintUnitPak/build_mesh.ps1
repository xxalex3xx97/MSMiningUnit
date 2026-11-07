$ErrorActionPreference = 'Stop'
$d = $PSScriptRoot
$root = if ($env:MS_MODS_ROOT) { $env:MS_MODS_ROOT } else { 'F:\MSDemoMods' }
$retoc = "$root\tools\retoc\retoc.exe"
$name = 'pakchunk97-BPUnitMesh_P'
Remove-Item "$d\stage_mesh" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$d\stage_mesh\MainSequence\Content\Meshes\Machines" | Out-Null
New-Item -ItemType Directory -Force "$d\out" | Out-Null
Copy-Item "$d\mesh\SM2_BPUnitTbl.*" "$d\stage_mesh\MainSequence\Content\Meshes\Machines\"
& $retoc to-zen --version UE5_7 "$d\stage_mesh" "$d\out\$name.utoc"; if ($LASTEXITCODE) { throw 'to-zen failed' }
$v = & $retoc verify "$d\out\$name.utoc" 2>&1 | Out-String
if ($v -notmatch 'verified') { throw "verify: $v" }
Write-Output $v
& $retoc list --path --size "$d\out\$name.utoc"
Get-ChildItem "$d\out" -Filter "$name.*" | ForEach-Object { "{0}  {1}  {2}" -f $_.Name, $_.Length, (Get-FileHash $_.FullName).Hash }
