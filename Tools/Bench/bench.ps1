# Repeatable 1080p benchmark: fixed-timestep route (-AethraBench), CSV capture, stats.
# Usage (from any folder):
#   powershell -ExecutionPolicy Bypass -File Tools\Bench\bench.ps1 -Name minha-medicao [-Seconds 60] [-Extra '-dpcvars=aethra.Horizon=0']
#   -Exe <packaged EcosDeAethra.exe> runs a packaged build instead of the editor in -game mode.
# Run once with -Seconds 10 first after shader/material changes: the first run compiles shaders.
param(
    [Parameter(Mandatory)][string]$Name,
    [int]$Seconds = 60,
    [string]$Extra = '',
    [string]$Exe = 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe',
    [string]$Project = ''
)
# PowerShell 5.1 evaluates param defaults before $PSScriptRoot is set.
if (-not $Project) { $Project = (Resolve-Path (Join-Path $PSScriptRoot '..\..\EcosDeAethra.uproject')).Path }
$out = Join-Path (Split-Path $Project) "Saved\Bench\$Name"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory $out | Out-Null

$arguments = @()
if ($Exe -like '*UnrealEditor*') { $arguments += "`"$Project`"", '-game' }
$arguments += '-windowed', '-ResX=1920', '-ResY=1080', '-nosound', '-benchmark', '-fps=60',
    "-userdir=`"$out`"", "-abslog=`"$out\run.log`"", "-AethraBench=$Seconds", '"-ExecCmds=r.VSync 0, t.MaxFPS 0"'
if ($Extra) { $arguments += $Extra }

$process = Start-Process $Exe -ArgumentList $arguments -PassThru
if (-not $process.WaitForExit(($Seconds + 180) * 1000)) { $process.Kill(); Write-Output 'TIMEOUT: process killed' }
$csv = Get-ChildItem "$out\Saved\Profiling\CSV\*.csv" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $csv) { Write-Output "NO CSV - see $out\run.log"; exit 1 }
python -I "$PSScriptRoot\csvstats.py" $csv.FullName
Write-Output "Screenshots: $out\Saved\Screenshots (route start, horizon vista)"
