function New-AethraBenchmarkDirectory {
    param(
        [Parameter(Mandatory)][string]$ProjectDirectory,
        [Parameter(Mandatory)][ValidatePattern('\A[A-Za-z0-9][A-Za-z0-9_-]{0,63}\z')][string]$Name
    )

    if ($Name -match '\A(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])\z') {
        throw "The benchmark name is a reserved device name."
    }

    $root = (Get-Item -LiteralPath $ProjectDirectory -ErrorAction Stop).FullName
    foreach ($component in 'Saved', 'Bench') {
        $root = Join-Path $root $component
        if (-not (Test-Path -LiteralPath $root)) {
            [System.IO.Directory]::CreateDirectory($root) | Out-Null
        }
        $folder = Get-Item -LiteralPath $root -Force -ErrorAction Stop
        if (-not $folder.PSIsContainer -or ($folder.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
            throw "Benchmark output requires physical Saved and Bench directories."
        }
    }

    $timestamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $run = Join-Path $root "$Name-$timestamp-$([Guid]::NewGuid().ToString('N'))"
    (New-Item -ItemType Directory -Path $run -ErrorAction Stop).FullName
}
