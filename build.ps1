# Build MiningBenchMod, optionally install into the game. Usage: .\build.ps1 [-Install]
param([switch]$Install)
$ErrorActionPreference = 'Continue'  # native tools write warnings to stderr; failures are checked via exit codes below
$src  = $PSScriptRoot                  # source = this checkout (worktree or master)
$deps = if ($env:MS_MODS_ROOT) { $env:MS_MODS_ROOT } else { 'F:\MSDemoMods' }   # RE-UE4SS tree, build output and backups (shared with user builds)
$re   = "$deps\build\RE-UE4SS"
$bt   = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$gameDir = if ($env:MS_GAME_DIR) { $env:MS_GAME_DIR } else { 'F:\SteamLibrary\steamapps\common\Main Sequence Demo\Windows\MainSequence' }
$game = "$gameDir\Binaries\Win64\ue4ss\Mods\MiningBenchMod\dlls"

# build compiles from the mirror, so sync the source first
Copy-Item "$src\mods\MiningBenchMod\*.cpp","$src\mods\MiningBenchMod\*.hpp" "$re\cppmods\MiningBenchMod\" -Force

# load the VS environment into this session
cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
$env:PATH = "$bt\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$bt\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"

Set-Location $re
cmake --build build --target MiningBenchMod
if ($LASTEXITCODE -ne 0) { throw "BUILD FAILED" }

$dll = "$re\build\Game__Shipping__Win64\bin\MiningBenchMod.dll"
$f = Get-Item $dll
"{0}  {1:N0} B  {2}" -f $f.Name, $f.Length, $f.LastWriteTime
if ((Get-Date) - $f.LastWriteTime -gt [TimeSpan]::FromMinutes(5)) { Write-Warning "DLL is older than 5 min - build may be stale" }

if ($Install) {
    if (Get-Process -Name 'MainSequence*' -ErrorAction SilentlyContinue) { throw "Game is running - close it first" }
    $bk = "$deps\build\backups\main.$(Get-Date -f yyyyMMdd-HHmmss).dll"
    Copy-Item "$game\main.dll" $bk
    Copy-Item $dll "$game\main.dll" -Force
    "Installed. Backup: $bk"
}
