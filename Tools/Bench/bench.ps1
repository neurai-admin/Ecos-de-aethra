# Repeatable 1080p benchmark: fixed-timestep route (-AethraBench), CSV capture, stats.
# Usage (from any folder):
#   powershell -ExecutionPolicy Bypass -File Tools\Bench\bench.ps1 -Name minha-medicao [-Seconds 60] [-Extra '-dpcvars=aethra.Horizon=0']
#   -Exe <packaged EcosDeAethra.exe> runs a packaged build instead of the editor in -game mode.
# Run once with -Seconds 10 first after shader/material changes: the first run compiles shaders.
param(
    [Parameter(Mandatory)][string]$Name,
    [ValidateRange(1, 3600)][int]$Seconds = 60,
    [ValidateSet('', '-dpcvars=aethra.Horizon=0', '-dpcvars=aethra.Horizon=1')][string]$Extra = '',
    [string]$Exe = 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe',
    [string]$Project = '',
    [switch]$RealTime,
    # The Visual World prototype instead of the voxel world (its own flight route over the river plain).
    [switch]$VisualWorld
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'bench-output.ps1')

# PowerShell 5.1 evaluates param defaults before $PSScriptRoot is set.
$root = (Get-Item -LiteralPath (Join-Path $PSScriptRoot '..\..')).FullName
if (-not $Project) { $Project = Join-Path $root 'EcosDeAethra.uproject' }
$projectFile = Get-Item -LiteralPath $Project
if ($projectFile.PSIsContainer -or $projectFile.Extension -ne '.uproject') {
    throw "Project must identify an Unreal .uproject file."
}
$exeFile = Get-Item -LiteralPath $Exe
if ($exeFile.PSIsContainer -or $exeFile.Extension -ne '.exe') {
    throw "Exe must identify a Windows executable."
}
$out = New-AethraBenchmarkDirectory -ProjectDirectory $root -Name $Name
Write-Output "Benchmark directory: $out"

$arguments = @()
if ($exeFile.BaseName -like 'UnrealEditor*') { $arguments += "`"$($projectFile.FullName)`"", '-game' }
$arguments += '-windowed', '-ResX=1920', '-ResY=1080', '-ForceRes', '-nosound',
    "-userdir=`"$out`"", "-abslog=`"$out\run.log`"", "-AethraBench=$Seconds", '"-ExecCmds=r.VSync 0, t.MaxFPS 0"'
if (-not $RealTime) { $arguments += '-benchmark', '-fps=60' }
if ($Extra) { $arguments += $Extra }
if ($VisualWorld) { $arguments += '-AethraVisualWorld' }

$process = Start-Process -FilePath $exeFile.FullName -ArgumentList $arguments -PassThru
if (-not $process.WaitForExit(($Seconds + 180) * 1000)) {
    $process.Kill()
    throw "Benchmark timed out. See $out\run.log."
}
if ($process.ExitCode -ne 0) {
    throw "Benchmark exited with code $($process.ExitCode). See $out\run.log."
}
$csvDirectory = Join-Path $out 'Saved\Profiling\CSV'
$csv = @(Get-ChildItem -LiteralPath $csvDirectory -File -Filter '*.csv' -ErrorAction SilentlyContinue)
if ($csv.Count -ne 1) { throw "Expected one benchmark CSV. See $out\run.log." }
Write-Output "CSV: $($csv[0].FullName)"
python -I "$PSScriptRoot\csvstats.py" $csv[0].FullName
if ($LASTEXITCODE -ne 0) { throw "Could not summarize benchmark CSV." }
Write-Output "Screenshots: $out\Saved\Screenshots (route start, horizon vista)"
