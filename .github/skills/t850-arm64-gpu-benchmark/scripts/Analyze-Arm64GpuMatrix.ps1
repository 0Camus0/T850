[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceRoot,
    [string]$OutputPath = (Join-Path $EvidenceRoot 'analysis.json')
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$resultPath = Join-Path $EvidenceRoot 'result.json'
if (!(Test-Path $resultPath)) { throw "Missing capture result: $resultPath" }
$capture = Get-Content $resultPath -Raw | ConvertFrom-Json
if (!$capture.passed) { throw "Capture did not pass: $($capture.failure)" }

function Get-Percentile([double[]]$Values, [double]$Fraction) {
    if (!$Values.Count) { return $null }
    $sorted = @($Values | Sort-Object)
    $position = ($sorted.Count - 1) * $Fraction
    $lower = [math]::Floor($position)
    $upper = [math]::Ceiling($position)
    if ($lower -eq $upper) { return [double]$sorted[$lower] }
    return [double]$sorted[$lower] + ($position - $lower) * ([double]$sorted[$upper] - [double]$sorted[$lower])
}

function Get-Stats([double[]]$Values) {
    if (!$Values.Count) { return $null }
    $mean = ($Values | Measure-Object -Average).Average
    $variance = if ($Values.Count -gt 1) {
        ($Values | ForEach-Object { ($_ - $mean) * ($_ - $mean) } | Measure-Object -Average).Average
    } else { 0.0 }
    return [pscustomobject]@{
        count = $Values.Count
        mean = $mean
        median = Get-Percentile $Values 0.5
        p95 = Get-Percentile $Values 0.95
        min = ($Values | Measure-Object -Minimum).Minimum
        max = ($Values | Measure-Object -Maximum).Maximum
        standardDeviation = [math]::Sqrt($variance)
        coefficientOfVariationPercent = if ($mean) { 100.0 * [math]::Sqrt($variance) / $mean } else { $null }
    }
}

function Read-PresentMetrics([string]$Path) {
    $rows = @(Import-Csv $Path)
    if (!$rows.Count) { throw "Empty PresentMon window: $Path" }
    $positiveGpu = @($rows | Where-Object { $_.GPUBusy -notin @('', 'NA') -and [double]$_.GPUBusy -gt 0 })
    if (!$positiveGpu.Count) { throw "No GPU-busy samples: $Path" }
    $durationMs = ([double]$rows[-1].CPUStartQPCTime - [double]$rows[0].CPUStartQPCTime)
    return [pscustomobject]@{
        rows = $rows.Count
        qpcSpanMs = $durationMs
        presentRateHz = if ($durationMs -gt 0) { 1000.0 * ($rows.Count - 1) / $durationMs } else { $null }
        gpuBusy = Get-Stats @($positiveGpu | ForEach-Object { [double]$_.GPUBusy })
        gpuTime = Get-Stats @($rows | Where-Object { $_.GPUTime -notin @('', 'NA') } | ForEach-Object { [double]$_.GPUTime })
        frameTime = Get-Stats @($rows | Where-Object { $_.FrameTime -notin @('', 'NA') } | ForEach-Object { [double]$_.FrameTime })
        displayedTime = Get-Stats @($rows | Where-Object { $_.DisplayedTime -notin @('', 'NA') } | ForEach-Object { [double]$_.DisplayedTime })
    }
}

$runRows = foreach ($directory in @(Get-ChildItem (Join-Path $EvidenceRoot 'light') -Directory | Sort-Object Name)) {
    $meta = Get-Content (Join-Path $directory.FullName 'capture.json') -Raw | ConvertFrom-Json
    $metrics = Read-PresentMetrics (Join-Path $directory.FullName 'accepted-window.csv')
    [pscustomobject]@{cell=$meta.cell;repeat=$meta.repeat;metrics=$metrics;directory=$directory.FullName}
}

$cellRows = foreach ($group in @($runRows | Group-Object cell | Sort-Object Name)) {
    $runs = @($group.Group)
    [pscustomobject]@{
        cell = $group.Name
        repetitions = $runs.Count
        gpuBusyRunMeans = Get-Stats @($runs | ForEach-Object { [double]$_.metrics.gpuBusy.mean })
        gpuTimeRunMeans = Get-Stats @($runs | ForEach-Object { [double]$_.metrics.gpuTime.mean })
        frameTimeRunMeans = Get-Stats @($runs | ForEach-Object { [double]$_.metrics.frameTime.mean })
        displayedTimeRunMeans = Get-Stats @($runs | ForEach-Object { [double]$_.metrics.displayedTime.mean })
        presentRateRunMeans = Get-Stats @($runs | ForEach-Object { [double]$_.metrics.presentRateHz })
    }
}

function Find-Cell([string]$Name) { return @($cellRows | Where-Object cell -EQ $Name)[0] }
function New-Delta([string]$Label, [string]$Reference, [string]$Candidate) {
    $referenceCell = Find-Cell $Reference
    $candidateCell = Find-Cell $Candidate
    $referenceMs = [double]$referenceCell.gpuBusyRunMeans.median
    $candidateMs = [double]$candidateCell.gpuBusyRunMeans.median
    $deltaMs = $candidateMs - $referenceMs
    return [pscustomobject]@{label=$Label;reference=$Reference;candidate=$Candidate;referenceMs=$referenceMs;candidateMs=$candidateMs;
        deltaMs=$deltaMs;deltaPercent=if($referenceMs){100.0*$deltaMs/$referenceMs}else{$null}}
}

$deltas = @(
    New-Delta 'D3D12 compute minus raster' 'd3d12-raster' 'd3d12-compute'
    New-Delta 'WGSL compute minus raster' 'wgsl-raster' 'wgsl-compute'
    New-Delta 'SPIR-V compute minus raster' 'spirv-raster' 'spirv-compute'
    New-Delta 'WGSL minus D3D12 raster' 'd3d12-raster' 'wgsl-raster'
    New-Delta 'SPIR-V minus D3D12 raster' 'd3d12-raster' 'spirv-raster'
    New-Delta 'WGSL minus D3D12 compute' 'd3d12-compute' 'wgsl-compute'
    New-Delta 'SPIR-V minus D3D12 compute' 'd3d12-compute' 'spirv-compute'
    New-Delta 'SPIR-V minus WGSL raster' 'wgsl-raster' 'spirv-raster'
    New-Delta 'SPIR-V minus WGSL compute' 'wgsl-compute' 'spirv-compute'
)

$traceRows = foreach ($directory in @(Get-ChildItem (Join-Path $EvidenceRoot 'trace') -Directory | Sort-Object Name)) {
    $meta = Get-Content (Join-Path $directory.FullName 'capture.json') -Raw | ConvertFrom-Json
    $metrics = Read-PresentMetrics (Join-Path $directory.FullName 'accepted-window.csv')
    $counterPath = Join-Path $directory.FullName 'gpu-engine-counters.json'
    $counters = if (Test-Path $counterPath) { @(Get-Content $counterPath -Raw | ConvertFrom-Json) } else { @() }
    $engineRows = foreach ($group in @($counters | Group-Object {
        $match = [regex]::Match($_.path, 'engtype_([^\)]+)\)')
        if ($match.Success) { $match.Groups[1].Value } else { 'unknown' }
    })) {
        $samples = @($group.Group | Group-Object timestamp | ForEach-Object {
            ($_.Group | Measure-Object value -Sum).Sum
        })
        [pscustomobject]@{engineType=$group.Name;utilization=Get-Stats @($samples)}
    }
    [pscustomobject]@{cell=$meta.cell;seconds=$meta.traceSeconds;present=$metrics;gpuEngines=@($engineRows);
        etl=(Join-Path $directory.FullName 'gpu.etl');directory=$directory.FullName}
}

$compileRows = foreach ($group in @($capture.compileAll | Group-Object case | Sort-Object Name)) {
    $cold = @($group.Group | Where-Object state -EQ 'cold')[0]
    $warm = @($group.Group | Where-Object state -EQ 'warm')[0]
    [pscustomobject]@{case=$group.Name;coldMs=[double]$cold.wallMs;warmMs=[double]$warm.wallMs;
        savedMs=[double]$cold.wallMs-[double]$warm.wallMs;speedup=if($warm.wallMs){[double]$cold.wallMs/[double]$warm.wallMs}else{$null};permutations=$cold.succeeded}
}

$startupRows = foreach ($entry in @($capture.startup)) {
    $scopeTotals = @{}
    foreach ($scope in @($entry.shaderScopes)) { $scopeTotals[$scope.name] = [double]$scope.totalMs }
    $counterTotals = @{}
    foreach ($counter in @($entry.shaderCounters)) { $counterTotals[$counter.name] = [double]$counter.value }
    [pscustomobject]@{cell=$entry.cell;state=$entry.state;wallMs=[double]$entry.wallMs;
        firstRuntimeFrameCompleteMs=[double]$entry.firstRuntimeFrameCompleteMs;shaderScopes=$scopeTotals;shaderCounters=$counterTotals}
}

$analysis = [pscustomobject]@{
    schema=1;generatedUtc=[DateTime]::UtcNow.ToString('o');capture=[pscustomobject]@{
        machine=$capture.machine;architecture=$capture.architecture;executableSha256=$capture.executableSha256;
        collectorSha256=$capture.collectorSha256;collectorSource=$capture.collectorSource;scene=$capture.scene;width=$capture.width;height=$capture.height;
        fixedDt=$capture.fixedDt;holdFrame=$capture.holdFrame;settleMs=$capture.settleMs;windowMs=$capture.windowMs
    }
    cells=@($cellRows);deltas=$deltas;traces=@($traceRows);compileAll=@($compileRows);startup=@($startupRows);runs=@($runRows)
}
$analysis | ConvertTo-Json -Depth 15 | Set-Content $OutputPath -Encoding UTF8
Write-Output "ARM64 GPU analysis written: $OutputPath"
