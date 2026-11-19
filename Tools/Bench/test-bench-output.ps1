# Run from either Windows PowerShell 5.1 or PowerShell 7.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'bench-output.ps1')

function Assert-Rejected {
    param([scriptblock]$Action)
    try { & $Action | Out-Null }
    catch { return }
    throw 'Expected the benchmark output to reject this input.'
}

$fixture = Join-Path ([System.IO.Path]::GetTempPath()) "aethra-bench-tests-$([Guid]::NewGuid().ToString('N'))"
$project = Join-Path $fixture 'project [test]'
$outside = Join-Path $fixture 'outside'
[System.IO.Directory]::CreateDirectory($project) | Out-Null
[System.IO.Directory]::CreateDirectory($outside) | Out-Null
$sentinel = Join-Path $outside 'keep.txt'
[System.IO.File]::WriteAllText($sentinel, 'retain')

$first = New-AethraBenchmarkDirectory -ProjectDirectory $project -Name 'valid_run-01'
$firstResult = Join-Path $first 'keep.csv'
[System.IO.File]::WriteAllText($firstResult, 'first run')
$second = New-AethraBenchmarkDirectory -ProjectDirectory $project -Name 'valid_run-01'
$expectedRoot = Join-Path $project 'Saved\Bench'
if ($first -eq $second -or (Split-Path -Parent $first) -ne $expectedRoot -or (Split-Path -Parent $second) -ne $expectedRoot) {
    throw 'Expected two unique runs under Saved\Bench.'
}
if ([System.IO.File]::ReadAllText($firstResult) -ne 'first run') {
    throw 'The previous result changed.'
}

$invalid = '.', '..', '..\outside', $outside, 'C:outside', '\\server\share', '/absolute', 'slash/name',
    'wild*', 'space name', 'quote"name', ('name' + [char]10), 'NUL', ('a' * 65)
foreach ($name in $invalid) {
    Assert-Rejected { New-AethraBenchmarkDirectory -ProjectDirectory $project -Name $name }
}
if (@(Get-ChildItem -LiteralPath $expectedRoot).Count -ne 2) {
    throw 'An invalid name created an output directory.'
}

$junctionCount = 0
foreach ($component in 'Saved', 'Bench') {
    $junctionProject = Join-Path $fixture "junction-$component"
    $junctionParent = $junctionProject
    if ($component -eq 'Bench') {
        $junctionParent = Join-Path $junctionProject 'Saved'
    }
    [System.IO.Directory]::CreateDirectory($junctionParent) | Out-Null
    $entry = Join-Path $junctionParent $component
    [System.IO.File]::WriteAllText($entry, 'not a directory')
    Assert-Rejected { New-AethraBenchmarkDirectory -ProjectDirectory $junctionProject -Name 'valid' }
    [System.IO.File]::Delete($entry)
    try { New-Item -ItemType Junction -Path $entry -Target $outside | Out-Null }
    catch {
        $denied = $_.Exception -is [System.UnauthorizedAccessException] -or
            ($_.Exception -is [System.ComponentModel.Win32Exception] -and $_.Exception.NativeErrorCode -in 5, 1314)
        if (-not $denied) { throw }
        Write-Output "SKIP junction $component (creation denied by the host)."
        continue
    }
    Assert-Rejected { New-AethraBenchmarkDirectory -ProjectDirectory $junctionProject -Name 'valid' }
    $junctionCount++
}
if (@(Get-ChildItem -LiteralPath $outside).Count -ne 1 -or [System.IO.File]::ReadAllText($sentinel) -ne 'retain') {
    throw 'A run touched the directory outside Saved\Bench.'
}
Write-Output "Benchmark output tests passed ($($invalid.Count) invalid names, two file collisions, $junctionCount junctions, unique retained runs)."
