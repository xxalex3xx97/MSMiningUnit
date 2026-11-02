# Build and run the pattern/compat unit checks (no game needed). Usage: .\tests\run_compat.ps1
# Exit code 0 = all PASS, non-zero = compile or check failure.
$ErrorActionPreference = 'Continue'
Set-Location (Split-Path -Parent $PSScriptRoot)
$bt = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'  # same MSVC as build.ps1

# load the VS environment into this session
cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}

New-Item -ItemType Directory -Force tests\out | Out-Null
cl /nologo /EHsc /std:c++17 /I mods\MiningBenchMod tests\test_compat.cpp /Fe:tests\out\test_compat.exe /Fo:tests\out\
if ($LASTEXITCODE -ne 0) { Write-Host "COMPILE FAILED"; exit 1 }

& .\tests\out\test_compat.exe
if ($LASTEXITCODE -ne 0) { Write-Host "TESTS FAILED (exit $LASTEXITCODE)"; exit $LASTEXITCODE }
Write-Host "ALL PASS"
exit 0
