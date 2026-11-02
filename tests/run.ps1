# Run every unit/static check (no game needed). Usage: .\tests\run.ps1
# Steps: test_logic.exe, run_compat.ps1, test_gen_paste.py, test_asset_refs.py, Lua syntax (luac -p).
# Exit code 0 = no FAIL (SKIP is allowed), non-zero = at least one step FAILED.
$ErrorActionPreference = 'Continue'
Set-Location (Split-Path -Parent $PSScriptRoot)
$bt = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'  # same MSVC as build.ps1
$luaRoot = if ($env:MS_MODS_ROOT) { $env:MS_MODS_ROOT } else { 'F:\MSDemoMods' }
$luaSrc = "$luaRoot\build\RE-UE4SS\deps\first\LuaRaw\src"         # Lua sources for luac fallback

New-Item -ItemType Directory -Force tests\out | Out-Null
$rows = @()

function Add-Row($name, $status, $detail) {
    $script:rows += [pscustomobject]@{ Step = $name; Status = $status; Detail = $detail }
}

# ---- 1. test_logic.exe (mb_logic.hpp, mb_math.hpp) ----
Write-Host "== test_logic" -ForegroundColor Cyan
cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
cl /nologo /EHsc /std:c++17 /W3 /I mods\MiningBenchMod tests\test_logic.cpp /Fe:tests\out\test_logic.exe /Fo:tests\out\
if ($LASTEXITCODE -ne 0) {
    Add-Row 'test_logic' 'FAIL' 'compile failed'
} else {
    $out = & .\tests\out\test_logic.exe 2>&1
    $out | ForEach-Object { Write-Host $_ }
    $line = ($out | Where-Object { $_ -match '^test_logic: ' } | Select-Object -Last 1)
    if ($LASTEXITCODE -eq 0) { Add-Row 'test_logic' 'PASS' $line } else { Add-Row 'test_logic' 'FAIL' $line }
}

# ---- 2. compat checks (pattern + compat gate) ----
Write-Host "== run_compat" -ForegroundColor Cyan
& .\tests\run_compat.ps1
if ($LASTEXITCODE -eq 0) { Add-Row 'run_compat' 'PASS' 'ALL PASS' } else { Add-Row 'run_compat' 'FAIL' "exit $LASTEXITCODE" }

# ---- 3. python tests ----
$py = (Get-Command python -ErrorAction SilentlyContinue)
function Run-Py($step, $script) {
    Write-Host "== $step" -ForegroundColor Cyan
    if (-not $py) { Add-Row $step 'SKIP' 'python not on PATH'; return }
    $out = & python -I $script 2>&1
    $out | ForEach-Object { Write-Host $_ }
    $ok = @($out | Where-Object { $_ -match '^ok ' }).Count
    $bad = @($out | Where-Object { $_ -match '^FAIL ' }).Count
    $detail = "$ok passed, $bad failed"
    if ($LASTEXITCODE -eq 0) { Add-Row $step 'PASS' $detail } else { Add-Row $step 'FAIL' $detail }
}
Run-Py 'test_gen_paste' 'tests\test_gen_paste.py'
Run-Py 'test_asset_refs' 'tests\test_asset_refs.py'

# ---- 4. Lua syntax (luac -p) ----
Write-Host "== lua syntax" -ForegroundColor Cyan
$luac = $null
$luaExe = $null
$cmdLuac = Get-Command luac -ErrorAction SilentlyContinue
$cmdLua = Get-Command lua -ErrorAction SilentlyContinue
if ($cmdLuac) { $luac = $cmdLuac.Source }
elseif ($cmdLua) { $luaExe = $cmdLua.Source }
elseif (Test-Path "$luaSrc\lvm.c") {
    # build luac.exe from the RE-UE4SS Lua sources (every .c except lua.c)
    New-Item -ItemType Directory -Force tests\out\luac | Out-Null
    $srcs = Get-ChildItem "$luaSrc\*.c" | Where-Object { $_.Name -ne 'lua.c' } | ForEach-Object { $_.FullName }
    cl /nologo /O2 /MD /W0 /I $luaSrc /I "$luaSrc\..\include" $srcs /Fe:tests\out\luac.exe /Fo:tests\out\luac\ | Out-Null
    if ($LASTEXITCODE -eq 0 -and (Test-Path tests\out\luac.exe)) { $luac = (Resolve-Path tests\out\luac.exe).Path }
    else { Write-Host "luac build failed (see cl output: run cl by hand with /I $luaSrc)" }
}

$luaFiles = @(Get-ChildItem -Recurse -Filter *.lua mods, ue | Where-Object { $_.FullName -notmatch '\\(build|out)\\' } | ForEach-Object { $_.FullName })
if ($luaFiles.Count -eq 0) {
    Add-Row 'lua_syntax' 'SKIP' 'no .lua files found'
} elseif (-not ($luac -or $luaExe)) {
    Add-Row 'lua_syntax' 'SKIP' 'no luac/lua on PATH and LuaRaw build failed or sources missing'
} else {
    $bad = 0
    foreach ($f in $luaFiles) {
        if ($luac) { & $luac -p $f 2>&1 | ForEach-Object { Write-Host $_ }; $rc = $LASTEXITCODE }
        else { & $luaExe -e "local f,e=loadfile(arg[1]) if not f then print(e) os.exit(1) end" $f 2>&1 | ForEach-Object { Write-Host $_ }; $rc = $LASTEXITCODE }
        if ($rc -ne 0) { $bad++; Write-Host "  syntax error: $f" }
    }
    $tool = if ($luac) { $luac } else { $luaExe }
    $detail = "$($luaFiles.Count - $bad) passed, $bad failed ($(Split-Path -Leaf $tool))"
    if ($bad -eq 0) { Add-Row 'lua_syntax' 'PASS' $detail } else { Add-Row 'lua_syntax' 'FAIL' $detail }
}

# ---- summary ----
Write-Host ""
Write-Host "== SUMMARY" -ForegroundColor Cyan
$rows | Format-Table -AutoSize | Out-String | Write-Host
$fails = @($rows | Where-Object { $_.Status -eq 'FAIL' }).Count
if ($fails -gt 0) { Write-Host "RESULT: $fails step(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host "RESULT: no failures (SKIP = not run)" -ForegroundColor Green
exit 0
