[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RuntimeRoot,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [ValidateRange(1, 10)][int]$Repetitions = 5,
    [ValidateRange(30, 900)][int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
$RuntimeRoot = [IO.Path]::GetFullPath($RuntimeRoot)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$exe = Join-Path $RuntimeRoot 'DayScene.exe'
$shaderRoot = Join-Path $RuntimeRoot 'Shaders'
$cacheRoot = Join-Path $shaderRoot '.t8shadercache'
$evidenceRoot = Join-Path (Split-Path -Parent $OutputPath) 'pipeline-ready-logs'

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "DayScene.exe is missing: $exe" }
if (-not (Test-Path -LiteralPath (Join-Path $shaderRoot 'shader_permutations.json') -PathType Leaf)) {
    throw "Shader permutation manifest is missing: $shaderRoot"
}
if (Test-Path -LiteralPath $evidenceRoot) { Remove-Item -LiteralPath $evidenceRoot -Recurse -Force }
[void][IO.Directory]::CreateDirectory($evidenceRoot)

function Read-SharedText([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $reader = [IO.StreamReader]::new($stream)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } catch { $stream.Dispose(); throw }
}

function Get-Statistics([double[]]$Values) {
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    $median = if (($sorted.Count % 2) -eq 0) { ($sorted[$middle - 1] + $sorted[$middle]) / 2.0 } else { $sorted[$middle] }
    return [ordered]@{
        count = $sorted.Count
        sumMs = ($sorted | Measure-Object -Sum).Sum
        meanMs = ($sorted | Measure-Object -Average).Average
        medianMs = $median
        minMs = $sorted[0]
        maxMs = $sorted[-1]
    }
}

function Save-Result($Result) {
    $directory = Split-Path -Parent $OutputPath
    if ($directory) { [void][IO.Directory]::CreateDirectory($directory) }
    $json = $Result | ConvertTo-Json -Depth 10
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json)
    $stream = [IO.File]::Open($OutputPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::Write,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $stream.SetLength(0)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    } finally {
        $stream.Dispose()
    }
}

function Invoke-Cell {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][ValidateSet('d3d12', 'webgpu')][string]$Api,
        [string]$Flow,
        [ValidateSet('off', 'reset', 'on', 'none')][string]$SessionMode = 'none',
        [ValidateSet('off', 'reset', 'on', 'none')][string]$LibraryMode = 'none',
        [ValidateSet('off', 'reset', 'on', 'none')][string]$DawnCacheMode = 'none',
        [switch]$ClearShaderCache
    )

    if ($ClearShaderCache -and (Test-Path -LiteralPath $cacheRoot)) {
        Remove-Item -LiteralPath $cacheRoot -Recurse -Force
    }
    if ($SessionMode -eq 'none') { Remove-Item Env:T850_D3D12_SHADER_CACHE_SESSION -ErrorAction SilentlyContinue }
    else { $env:T850_D3D12_SHADER_CACHE_SESSION = $SessionMode }
    if ($LibraryMode -eq 'none') { Remove-Item Env:T850_D3D12_PIPELINE_LIBRARY -ErrorAction SilentlyContinue }
    else { $env:T850_D3D12_PIPELINE_LIBRARY = $LibraryMode }
    if ($DawnCacheMode -eq 'none') { Remove-Item Env:T850_WEBGPU_DAWN_CACHE -ErrorAction SilentlyContinue }
    else { $env:T850_WEBGPU_DAWN_CACHE = $DawnCacheMode }

    $stdout = Join-Path $evidenceRoot "$Name.stdout.log"
    $stderr = Join-Path $evidenceRoot "$Name.stderr.log"
    $arguments = @('--compileShaders', '--api', $Api, '--logLevel', 'error')
    if ($Api -eq 'webgpu') { $arguments += @('--shaderFlow', $Flow) }
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -WorkingDirectory $RuntimeRoot -ArgumentList $arguments `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    $null = $process.Handle
    $completed = $process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $completed) { $process.Kill(); $process.WaitForExit() }
    $stopwatch.Stop()
    $exitCode = if ($completed) { $process.ExitCode } else { -1 }
    $process.Dispose()
    $text = (Read-SharedText $stdout) + "`n" + (Read-SharedText $stderr)

    $completion = [regex]::Match($text, '\[ShaderPrecompile\] complete: (\d+) succeeded, (\d+) failed')
    $events = @([regex]::Matches($text,
        '\[PipelineReadyProfile\] backend=(\S+) flow=(\S+) kind=compute identity="([^"]+)" elapsedMs=(\d+\.\d+)') |
        ForEach-Object { [pscustomobject]@{ backend=$_.Groups[1].Value; flow=$_.Groups[2].Value; identity=$_.Groups[3].Value; elapsedMs=[double]$_.Groups[4].Value } })
    $session = [regex]::Matches($text,
        '(?:Shader cache session:|D3D12ShaderCacheSessionProfile\]) hits=(\d+) misses=(\d+) stores=(\d+) rejected=(\d+) lookupMs=(\d+(?:\.\d+)?) storeMs=(\d+(?:\.\d+)?)') |
        Select-Object -Last 1
    $library = [regex]::Matches($text,
        'D3D12PipelineLibraryProfile\] hits=(\d+) misses=(\d+) stores=(\d+) rejected=(\d+) bytes=(\d+)') |
        Select-Object -Last 1
    $dawnCache = [regex]::Matches($text,
        'DawnPersistentCacheProfile\] hits=(\d+) misses=(\d+) stores=(\d+) loadBytes=(\d+) storeBytes=(\d+) lookupMs=(\d+(?:\.\d+)?) storeMs=(\d+(?:\.\d+)?)') |
        Select-Object -Last 1
    $record = [ordered]@{
        name = $Name
        api = $Api
        flow = if ($Api -eq 'd3d12') { 'dxc' } else { $Flow }
        sessionMode = $SessionMode
        libraryMode = $LibraryMode
        dawnCacheMode = $DawnCacheMode
        shaderCacheCleared = [bool]$ClearShaderCache
        exitCode = $exitCode
        wallMs = $stopwatch.Elapsed.TotalMilliseconds
        succeeded = if ($completion.Success) { [int]$completion.Groups[1].Value } else { 0 }
        failed = if ($completion.Success) { [int]$completion.Groups[2].Value } else { -1 }
        pipelineReady = Get-Statistics @($events.elapsedMs)
        events = $events
        session = if ($session) { [ordered]@{
            hits=[int]$session.Groups[1].Value; misses=[int]$session.Groups[2].Value
            stores=[int]$session.Groups[3].Value; rejected=[int]$session.Groups[4].Value
            lookupMs=[double]$session.Groups[5].Value; storeMs=[double]$session.Groups[6].Value
        }} else { $null }
        library = if ($library) { [ordered]@{
            hits=[int]$library.Groups[1].Value; misses=[int]$library.Groups[2].Value
            stores=[int]$library.Groups[3].Value; rejected=[int]$library.Groups[4].Value
            bytes=[int64]$library.Groups[5].Value
        }} else { $null }
        dawnCache = if ($dawnCache) { [ordered]@{
            hits=[int]$dawnCache.Groups[1].Value; misses=[int]$dawnCache.Groups[2].Value
            stores=[int]$dawnCache.Groups[3].Value; loadBytes=[int64]$dawnCache.Groups[4].Value
            storeBytes=[int64]$dawnCache.Groups[5].Value; lookupMs=[double]$dawnCache.Groups[6].Value
            storeMs=[double]$dawnCache.Groups[7].Value
        }} else { $null }
        errors = @([regex]::Matches($text, '\[ERROR[^\]]*\]') | ForEach-Object Value).Count
        timedOut = -not $completed
    }
    if ($exitCode -ne 0 -or -not $completion.Success -or $record.failed -ne 0 -or $events.Count -ne 10) {
        throw "Pipeline-ready cell failed: $Name (exit=$exitCode events=$($events.Count) succeeded=$($record.succeeded) failed=$($record.failed))"
    }
    return [pscustomobject]$record
}

$result = [ordered]@{
    schema = 3
    startedUtc = [DateTime]::UtcNow.ToString('o')
    machine = $env:COMPUTERNAME
    architecture = $env:PROCESSOR_ARCHITECTURE
    executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    repetitions = $Repetitions
    boundary = 'manifest entry validation and source load through successful usable compute pipeline creation'
    passed = $false
    primes = @()
    runs = @()
}

try {
    $coldCells = @(
        [pscustomobject]@{Name='d3d12';Api='d3d12';Flow='';Session='off';DawnCache='none'},
        [pscustomobject]@{Name='wgsl';Api='webgpu';Flow='wgsl';Session='none';DawnCache='off'},
        [pscustomobject]@{Name='spirv';Api='webgpu';Flow='spirv';Session='none';DawnCache='off'}
    )
    for ($repetition = 1; $repetition -le $Repetitions; ++$repetition) {
        $order = @($coldCells)
        if (($repetition % 2) -eq 0) { [array]::Reverse($order) }
        foreach ($cell in $order) {
            $result.runs += Invoke-Cell -Name "cold-$($cell.Name)-r$repetition" -Api $cell.Api `
                -Flow $cell.Flow -SessionMode $cell.Session -DawnCacheMode $cell.DawnCache -ClearShaderCache
            Save-Result $result
        }
    }

    # Prime each engine artifact cache and the native PSO session outside measured warm repetitions.
    if (Test-Path -LiteralPath $cacheRoot) { Remove-Item -LiteralPath $cacheRoot -Recurse -Force }
    $result.primes += Invoke-Cell -Name 'prime-d3d12-session' -Api d3d12 -SessionMode reset
    $result.primes += Invoke-Cell -Name 'prime-d3d12-library' -Api d3d12 -SessionMode on -LibraryMode reset
    $result.primes += Invoke-Cell -Name 'prime-wgsl' -Api webgpu -Flow wgsl -SessionMode none -DawnCacheMode reset
    $result.primes += Invoke-Cell -Name 'prime-spirv' -Api webgpu -Flow spirv -SessionMode none -DawnCacheMode on
    Save-Result $result

    for ($repetition = 1; $repetition -le $Repetitions; ++$repetition) {
        $cacheCells = @(
            [pscustomobject]@{Name='d3d12-library';Api='d3d12';Flow='';Session='on';Library='on';DawnCache='none'},
            [pscustomobject]@{Name='d3d12-session';Api='d3d12';Flow='';Session='on';Library='off';DawnCache='none'},
            [pscustomobject]@{Name='d3d12-no-session';Api='d3d12';Flow='';Session='off';Library='off';DawnCache='none'},
            [pscustomobject]@{Name='wgsl-dawn-cache';Api='webgpu';Flow='wgsl';Session='none';Library='none';DawnCache='on'},
            [pscustomobject]@{Name='wgsl-no-dawn-cache';Api='webgpu';Flow='wgsl';Session='none';Library='none';DawnCache='off'},
            [pscustomobject]@{Name='spirv-dawn-cache';Api='webgpu';Flow='spirv';Session='none';Library='none';DawnCache='on'},
            [pscustomobject]@{Name='spirv-no-dawn-cache';Api='webgpu';Flow='spirv';Session='none';Library='none';DawnCache='off'}
        )
        if (($repetition % 2) -eq 0) { [array]::Reverse($cacheCells) }
        foreach ($cell in $cacheCells) {
            $result.runs += Invoke-Cell -Name "warm-$($cell.Name)-r$repetition" -Api $cell.Api `
                -Flow $cell.Flow -SessionMode $cell.Session -LibraryMode $cell.Library -DawnCacheMode $cell.DawnCache
            Save-Result $result
        }
    }
    $result.passed = $true
    $result.completedUtc = [DateTime]::UtcNow.ToString('o')
    Save-Result $result
} finally {
    Remove-Item Env:T850_D3D12_SHADER_CACHE_SESSION -ErrorAction SilentlyContinue
    Remove-Item Env:T850_D3D12_PIPELINE_LIBRARY -ErrorAction SilentlyContinue
    Remove-Item Env:T850_WEBGPU_DAWN_CACHE -ErrorAction SilentlyContinue
}

Write-Host "PASS: pipeline-ready matrix -> $OutputPath"