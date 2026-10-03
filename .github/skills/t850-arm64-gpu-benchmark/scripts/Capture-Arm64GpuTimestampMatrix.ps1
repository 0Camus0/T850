[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [ValidateRange(1, 5)][int]$Repetitions = 1,
    [ValidateRange(20, 5000)][int]$Samples = 120,
    [ValidateRange(60, 20000)][int]$HoldFrame = 3000,
    [ValidateRange(320, 7680)][int]$Width = 1920,
    [ValidateRange(180, 4320)][int]$Height = 1080
)

$ErrorActionPreference = 'Stop'
$runtime = Join-Path $Root 'runtime'
$exe = Join-Path $runtime 'DayScene.exe'
$evidence = Join-Path $Root 'gpu-timestamp-evidence'

if (!(Test-Path $exe)) { throw "DayScene is missing: $exe" }
if (Test-Path $evidence) { throw "Use a fresh evidence root: $evidence" }
if (Get-Process DayScene -ErrorAction SilentlyContinue) { throw 'DayScene is already running.' }
[void][IO.Directory]::CreateDirectory($evidence)

$cells = @(
    [pscustomobject]@{ Name='d3d12-raster'; Api='d3d12'; Flow='native'; Mode='raster' },
    [pscustomobject]@{ Name='d3d12-compute'; Api='d3d12'; Flow='native'; Mode='compute' },
    [pscustomobject]@{ Name='wgsl-raster'; Api='webgpu'; Flow='wgsl'; Mode='raster' },
    [pscustomobject]@{ Name='wgsl-compute'; Api='webgpu'; Flow='wgsl'; Mode='compute' },
    [pscustomobject]@{ Name='spirv-raster'; Api='webgpu'; Flow='spirv'; Mode='raster' },
    [pscustomobject]@{ Name='spirv-compute'; Api='webgpu'; Flow='spirv'; Mode='compute' }
)

function Quote-Arguments([string[]]$Arguments) {
    return (($Arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' ')
}

function Get-Percentile([double[]]$Values, [double]$Percentile) {
    $sorted = @($Values | Sort-Object)
    if (!$sorted.Count) { return $null }
    $index = [math]::Floor(($sorted.Count - 1) * $Percentile)
    return [double]$sorted[$index]
}

function Invoke-TimestampRun($Cell, [int]$Repetition) {
    $name = "$($Cell.Name)-r$Repetition"
    $directory = Join-Path $evidence $name
    [void][IO.Directory]::CreateDirectory($directory)
    $reportPath = Join-Path $directory 'gpu-profile.json'
    $logPath = Join-Path $directory 'engine.log'
    $arguments = @(
        '--api', $Cell.Api,
        '--scene', '1',
        '--width', "$Width",
        '--height', "$Height",
        '--postProcessMode', $Cell.Mode,
        '--culling', 'full',
        '--benchmarkNoPresent',
        '--benchmarkHoldFrame', "$HoldFrame",
        '--benchmarkFrames', "$($Samples + 60)",
        '--benchmarkFixedDt', '0.0166666667',
        '--profileGpu',
        '--profileGpuFrames', "$Samples",
        '--profileGpuPasses', 'render-graph',
        '--profileGpuOutput', $reportPath,
        '--logLevel', 'info',
        '--logFile', $logPath
    )
    if ($Cell.Api -eq 'webgpu') { $arguments += @('--shaderFlow', $Cell.Flow) }

    $process = Start-Process $exe -WorkingDirectory $runtime `
        -ArgumentList (Quote-Arguments $arguments) `
        -RedirectStandardOutput (Join-Path $directory 'stdout.log') `
        -RedirectStandardError (Join-Path $directory 'stderr.log') -PassThru
    $null = $process.Handle
    try {
        if (!$process.WaitForExit(600000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "GPU timestamp run timed out: $name"
        }
        if ($process.ExitCode -ne 0) { throw "GPU timestamp run failed ($($process.ExitCode)): $name" }
    } finally {
        $process.Dispose()
    }

    if (!(Test-Path $reportPath)) { throw "GPU timestamp report is missing: $name" }
    if (!(Test-Path $logPath)) { throw "Engine log is missing: $name" }
    $log = [IO.File]::ReadAllText($logPath)
    if ($log -match '\[ERROR\s*\]|Device lost|Queue submission failed') {
        throw "Renderer error in GPU timestamp run: $name"
    }
    if ($log -notmatch "\[GpuTimestamp\] armed workloadFrame=$HoldFrame samples=$Samples") {
        throw "GPU timestamps did not arm at held frame $HoldFrame`: $name"
    }
    if ($Cell.Api -eq 'webgpu') {
        if ($log -notmatch 'provider=Dawn backend=D3D12') { throw "Unexpected WebGPU backend: $name" }
        if ($log -notmatch 'timestamp-query requested=1 active=1') { throw "TimestampQuery is inactive: $name" }
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $allSamples = @($report.samples)
    $frameSamples = @($allSamples | Where-Object { $_.region -eq 'gpu.frame' })
    $values = @($frameSamples | ForEach-Object { [double]$_.gpuMs })
    $invalid = @($allSamples | Where-Object {
        !$_.valid -or [double]$_.gpuMs -lt 0.0 -or
        [double]::IsNaN([double]$_.gpuMs) -or [double]::IsInfinity([double]$_.gpuMs)
    })
    $regions = @($allSamples.region | Sort-Object -Unique)
    $regionSets = @($allSamples | Group-Object frame | ForEach-Object {
        @($_.Group.region | Sort-Object) -join '|'
    } | Sort-Object -Unique)
    if ([int]$report.framesRequested -ne $Samples -or
        [int]$report.framesSubmitted -ne $Samples -or
        [int]$report.framesValid -ne $Samples -or
        [int]$report.framesDropped -ne 0 -or
        [int]$report.framesFailed -ne 0 -or
        [string]$report.granularity -ne 'render-graph' -or
        $values.Count -ne $Samples -or $invalid.Count -ne 0 -or
        $regions.Count -le 1 -or $regionSets.Count -ne 1) {
        throw "Invalid GPU timestamp report: $name"
    }

    $measure = $values | Measure-Object -Average -Minimum -Maximum
    $passes = @($allSamples | Where-Object { $_.region -ne 'gpu.frame' } |
        Group-Object region | ForEach-Object {
            $passValues = @($_.Group | ForEach-Object { [double]$_.gpuMs })
            $passMeasure = $passValues | Measure-Object -Average -Minimum -Maximum
            [pscustomobject]@{
                name = $_.Name
                samples = $passValues.Count
                meanGpuMs = [double]$passMeasure.Average
                medianGpuMs = Get-Percentile $passValues 0.5
                p95GpuMs = Get-Percentile $passValues 0.95
                minGpuMs = [double]$passMeasure.Minimum
                maxGpuMs = [double]$passMeasure.Maximum
            }
        })
    return [pscustomobject]@{
        cell = $Cell.Name
        repetition = $Repetition
        api = [string]$report.api
        provider = [string]$report.provider
        backend = [string]$report.backend
        shaderFlow = $Cell.Flow
        postProcessMode = $Cell.Mode
        holdFrame = $HoldFrame
        samples = $values.Count
        meanGpuMs = [double]$measure.Average
        medianGpuMs = Get-Percentile $values 0.5
        p95GpuMs = Get-Percentile $values 0.95
        minGpuMs = [double]$measure.Minimum
        maxGpuMs = [double]$measure.Maximum
        regionCount = $regions.Count
        passes = $passes
        reportPath = $reportPath
        logPath = $logPath
    }
}

$result = [ordered]@{
    schema = 1
    mode = 'render-graph-gpu-timestamps'
    startedUtc = [DateTime]::UtcNow.ToString('o')
    passed = $false
    machine = $env:COMPUTERNAME
    architecture = $env:PROCESSOR_ARCHITECTURE
    executableSha256 = (Get-FileHash $exe -Algorithm SHA256).Hash
    scene = 1
    width = $Width
    height = $Height
    holdFrame = $HoldFrame
    samplesPerRun = $Samples
    repetitions = $Repetitions
    runs = @()
}
try {
    for ($repetition = 1; $repetition -le $Repetitions; ++$repetition) {
        $order = @($cells)
        if ($repetition % 2 -eq 0) { [array]::Reverse($order) }
        foreach ($cell in $order) {
            $result.runs += Invoke-TimestampRun $cell $repetition
        }
    }
    $result.passed = $true
} catch {
    $result.failure = $_.Exception.ToString()
} finally {
    $result.completedUtc = [DateTime]::UtcNow.ToString('o')
    $result | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $evidence 'result.json') -Encoding UTF8
}
if (!$result.passed) { throw $result.failure }
Write-Output "PASS: GPU timestamp matrix at $evidence"