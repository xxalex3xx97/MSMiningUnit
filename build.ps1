# Build a mod DLL, optionally install into the game. Usage: .\build.ps1 [-Mod MiningBenchMod] [-Install]
param(
    [ValidateSet('MiningBenchMod')][string]$Mod = 'MiningBenchMod',
    [switch]$Install
)
$ErrorActionPreference = 'Continue'  # native tools write warnings to stderr; failures are checked via exit codes below
$src  = $PSScriptRoot                  # source = this checkout (worktree or master)
$deps = if ($env:MS_MODS_ROOT) { $env:MS_MODS_ROOT } else { 'F:\MSDemoMods' }   # RE-UE4SS tree, build output and backups (shared with user builds)
$re   = if ($env:MS_UE4SS_ROOT) { $env:MS_UE4SS_ROOT } else { 'F:\mscore-build\RE-UE4SS-v4' }   # must be a UE4SS v4 tree
# UE4SS v4 gate: mods built here must come from a v4 tree (3.0.1 headers give DLLs v4 cannot load)
$ueTag = (git -C $re describe --tags --exact-match 2>$null)
if ("$ueTag" -notmatch '^v[4-9]\.') { throw "UE4SS tree '$re' is not a v4 tag (got '$ueTag'). Run scripts\setup-ue4ss-v4.ps1 or set MS_UE4SS_ROOT to a v4 tree." }
$bt   = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$gameDir = if ($env:MS_GAME_DIR) { $env:MS_GAME_DIR } else { 'F:\SteamLibrary\steamapps\common\Main Sequence Demo\Windows\MainSequence' }
$game = "$gameDir\Binaries\Win64\ue4ss\Mods\$Mod\dlls"
$utf8 = New-Object System.Text.UTF8Encoding $false

# build compiles from the mirror, so sync the source first (sources + CMakeLists when the repo has one)
$mirror = "$re\cppmods\$Mod"
New-Item -ItemType Directory -Force $mirror | Out-Null
Get-ChildItem "$src\mods\$Mod" -File | Where-Object { $_.Extension -in '.cpp', '.hpp', '.h', '.txt' } | Copy-Item -Destination $mirror -Force
# MiningBenchMod calls ShipControl through its C ABI header (mb_shipctl.hpp includes sc_api.h).
# ShipControl is the pinned submodule external\ShipControl (tag in deps.lock); its headers are copied here.
if ($Mod -eq 'MiningBenchMod') {
    foreach ($f in 'sc_api.h', 'sc_logic_path.hpp', 'sc_logic_steer.hpp', 'sc_logic_bubble.hpp', 'sc_flight_abi.h') {
        if (-not (Test-Path "$src\external\ShipControl\$f")) { throw "external\ShipControl is not initialised: run git submodule update --init" }
    }
    Copy-Item "$src\external\ShipControl\sc_api.h" -Destination $mirror -Force
    Copy-Item "$src\external\ShipControl\sc_logic_path.hpp" -Destination $mirror -Force   # mb_logic.hpp includes the path/warp geometry
    Copy-Item "$src\external\ShipControl\sc_logic_steer.hpp" -Destination $mirror -Force  # mb_logic.hpp includes the approach/low-power rules
    Copy-Item "$src\external\ShipControl\sc_logic_bubble.hpp" -Destination $mirror -Force  # mb_logic.hpp includes the bubble ladder rules (SC3-C)
    Copy-Item "$src\external\ShipControl\sc_flight_abi.h" -Destination $mirror -Force     # MsFlight ABI copy (SC6-D3a), mining reads flights through it
}

# load the VS environment into this session
cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
$env:PATH = "$bt\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$bt\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"

Set-Location $re
cmake --build build --target $Mod
if ($LASTEXITCODE -ne 0) { throw "BUILD FAILED" }

$dll = "$re\build\Game__Shipping__Win64\bin\$Mod.dll"
$f = Get-Item $dll
"{0}  {1:N0} B  {2}" -f $f.Name, $f.Length, $f.LastWriteTime
if ((Get-Date) - $f.LastWriteTime -gt [TimeSpan]::FromMinutes(5)) { Write-Warning "DLL is older than 5 min - build may be stale" }

if ($Install) {
    if (Get-Process -Name 'MainSequence*' -ErrorAction SilentlyContinue) { throw "Game is running - close it first" }
    $bkTag = if ($Mod -eq 'MiningBenchMod') { 'main' } else { "$Mod-main" }
    $bk = "$deps\build\backups\$bkTag.$(Get-Date -f yyyyMMdd-HHmmss).dll"
    New-Item -ItemType Directory -Force $game | Out-Null
    if (Test-Path "$game\main.dll") { Copy-Item "$game\main.dll" $bk }
    Copy-Item $dll "$game\main.dll" -Force
    # mscore.mod (manager Installed column) + MSCore install list (lets the manager Remove this mod)
    $verFile = Get-ChildItem "$src\mods\$Mod\*version*.hpp" | Select-Object -First 1
    $vh = Get-Content $verFile.FullName -Raw
    if ($vh -notmatch 'kVersion\s*=\s*"([^"]+)"') { throw "kVersion not found in $($verFile.Name)" }
    $ver = $Matches[1]
    $modDir = Split-Path $game -Parent
    [IO.File]::WriteAllText("$modDir\mscore.mod", "name=$Mod`nrepo=xxalex3xx97/$Mod`nversion=$ver`n", $utf8)
    $files = @("Mods/$Mod/dlls/main.dll", "Mods/$Mod/mscore.mod")
    if (Test-Path "$modDir\enabled.txt") { $files += "Mods/$Mod/enabled.txt" }
    $ilDir = "$gameDir\Binaries\Win64\ue4ss\Mods\MSCore\installed"
    New-Item -ItemType Directory -Force $ilDir | Out-Null
    [IO.File]::WriteAllText("$ilDir\$Mod.txt", "$ver`n" + (($files | Sort-Object) -join "`n") + "`n", $utf8)
    "Installed. Backup: $bk"
}
