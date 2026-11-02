# Cook MiningUnitUI loose for Windows. Does not open the editor GUI (-run=cook is a commandlet).
# Usage: .\cook.ps1 [-EngineRoot <UE 5.7 dir>] [-Versioned]   (default: $env:UE_ROOT, see README "Configuration")
param(
    [string]$EngineRoot = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'F:\UE_5.7' }),
    [string]$Uproject = (Join-Path $PSScriptRoot '..\MiningUnitUI.uproject'),
    [switch]$Versioned  # default -unversioned (per plan); pass -Versioned if retoc rejects unversioned cook
)
$ErrorActionPreference = 'Stop'

$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path $editor)) { throw "UnrealEditor-Cmd.exe not found: $editor" }
$Uproject = (Resolve-Path $Uproject).Path

$cookArgs = @('-run=cook', '-targetplatform=Windows', '-iterate', '-stdout', '-unattended', '-NoLogTimes')
if (-not $Versioned) { $cookArgs += '-unversioned' }

& $editor $Uproject @cookArgs
if ($LASTEXITCODE -ne 0) { throw "cook failed (exit $LASTEXITCODE)" }

$cooked = Join-Path (Split-Path $Uproject) 'Saved\Cooked\Windows\MiningUnitUI'
if (-not (Test-Path (Join-Path $cooked 'Content'))) { throw "no cooked Content under $cooked" }
"cooked: $cooked"
