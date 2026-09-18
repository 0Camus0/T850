[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Config = 'Release',
    [ValidateSet('d3d11','d3d12','vulkan','gl','webgpu')][string[]]$Apis = @('d3d12','webgpu'),
    [ValidateRange(0,6)][int]$Scene = 1,
    [ValidateRange(10,8000)][int]$Frames = 600,
    [ValidateRange(0,7999)][int]$Warmup = 120,
    [ValidateRange(1,20)][int]$Repetitions = 3,
    [ValidateSet('wgsl','spirv','auto')][string]$ShaderFlow = 'wgsl',
    [ValidateRange(64,7680)][int]$Width = 1280,
    [ValidateRange(64,4320)][int]$Height = 720,
    [ValidateRange(10,1800)][int]$TimeoutSeconds = 180,
    [switch]$Presented,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
if ($Warmup -ge $Frames) { throw 'Warmup must be smaller than Frames.' }
$source = Split-Path $PSScriptRoot -Parent
$working = Join-Path $source "bin/x64/$Config"
$executable = Join-Path $working 'DayScene.exe'
if (!(Test-Path $executable)) { throw "Build the measurement executable first: $executable" }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $env:LOCALAPPDATA "T850Profiles/cpu-phases-$(Get-Date -Format yyyyMMdd-HHmmss)" }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path $OutputDirectory) { throw 'Use a fresh evidence directory.' }
[void][IO.Directory]::CreateDirectory($OutputDirectory)

function Get-Percentile([double[]]$Values, [double]$Fraction) {
    if (!$Values.Count) { return $null }
    $sorted = @($Values | Sort-Object)
    return $sorted[[Math]::Min($sorted.Count - 1, [int][Math]::Floor(($sorted.Count - 1) * $Fraction))]
}

$results = [Collections.Generic.List[object]]::new()
for ($repeat = 0; $repeat -lt $Repetitions; ++$repeat) {
    $order = @($Apis)
    if ($repeat % 2) { [Array]::Reverse($order) }
    foreach ($api in $order) {
        $directory = Join-Path $OutputDirectory "$api-$repeat"
        [void][IO.Directory]::CreateDirectory($directory)
        $log = Join-Path $directory 'engine.log'
        $arguments = @('--api',$api,'--scene',"$Scene",'--width',"$Width",'--height',"$Height",
            '--profileCpuOnly','--profileFrames',"$Frames",'--telemetry','--telemetryFrequencyFrames','0',
            '--telemetryOutput',(Join-Path $directory 'telemetry.json'),'--regressionFixedDt','0.0166666667',
            '--logLevel','info','--logFile',$log)
        if (!$Presented) { $arguments += '--offscreen' }
        if ($api -eq 'webgpu') { $arguments += @('--shaderFlow',$ShaderFlow) }
        if ($Scene -eq 4) { $arguments += @('--sceneFile','Scenes/DayScene.t8scene') }
        Write-Host "Profiling $api repetition $($repeat + 1)/$Repetitions ($Frames frames, CPU only)"
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $process = Start-Process $executable -WorkingDirectory $working -ArgumentList (($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' ') `
            -RedirectStandardOutput (Join-Path $directory 'stdout.log') -RedirectStandardError (Join-Path $directory 'stderr.log') -PassThru
        $null = $process.Handle
        try {
            if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw "Profiling timeout: $api" }
            if ($process.ExitCode -ne 0) { throw "Profiling failed: $api exit=$($process.ExitCode)" }
        } finally {
            if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
        }
        $timer.Stop()
        $text = Get-Content $log -Raw
        if ($text -match '\[ERROR\]|\[Profiler\] WARNING|VUID-' -or $text -notmatch 'GPU=disabled') { throw "Invalid measurement log: $log" }
        $reports = @(Get-ChildItem $directory -Filter 'telemetry_*.json')
        if ($reports.Count -ne 1) { throw "Expected one telemetry report: $directory" }
        $report = Get-Content $reports[0].FullName -Raw | ConvertFrom-Json
        if ($report.version -ne 2 -or $report.droppedRecords -ne 0 -or $report.unfinishedWriters -ne 0) { throw 'Telemetry dropped or unfinished work; not a valid comparison.' }
        $samples = @($report.frames | Where-Object { !$_.startup -and $_.detailed -ne $false -and $_.frame -ge $Warmup })
        if (!$samples.Count) { throw 'No steady-state samples after warmup.' }
        $phases = @{}
        $work = @{}
        foreach ($sample in $samples) {
            foreach ($scope in $sample.scopes) {
                if (!$phases.ContainsKey($scope.name)) { $phases[$scope.name] = [Collections.Generic.List[double]]::new() }
                $phases[$scope.name].Add($scope.totalMs)
            }
            foreach ($counter in $sample.counters) {
                if ($counter.name -in @('gpu.draws','gpu.indices','render.pass.count')) {
                    if (!$work.ContainsKey($counter.name)) { $work[$counter.name] = 0.0 }
                    $work[$counter.name] += $counter.value
                }
            }
        }
        $phaseRows = foreach ($name in ($phases.Keys | Sort-Object)) {
            [pscustomobject]@{name=$name; samples=$phases[$name].Count; p50=Get-Percentile $phases[$name].ToArray() 0.5; p95=Get-Percentile $phases[$name].ToArray() 0.95}
        }
        $startupFrame = @($report.frames | Where-Object startup)[0]
        if (!$startupFrame -or $null -eq $report.startupTiming.firstRuntimeFrameCompleteMs) { throw 'Missing startup/first-runtime-frame attribution.' }
        $startup = [pscustomobject]@{milestones=$report.startupTiming;
            operations=@($startupFrame.scopes | Where-Object name -Match '^(shader\.|pipeline\.)');
            cache=@($startupFrame.counters | Where-Object name -Match '^shader\.');uploads=$startupFrame.uploads}
        $results.Add([pscustomobject]@{api=$api;provider=$report.provider;backend=$report.backend;adapterId=$report.adapterId;shaderFlow=$report.shaderFlow;
            repetition=$repeat;samples=$samples.Count;p50=Get-Percentile @($samples.cpuFrameMs) 0.5;p95=Get-Percentile @($samples.cpuFrameMs) 0.95;
            processSeconds=$timer.Elapsed.TotalSeconds;startup=$startup;phases=@($phaseRows);work=$work;gpuTiming=$null;report=$reports[0].FullName})
    }
}
$adapters = @($results | Where-Object { $_.api -in @('d3d12','webgpu') } | Select-Object -ExpandProperty adapterId -Unique)
$matchedAdapter = $adapters.Count -eq 1 -and $adapters[0] -ne '0'
$matchedWork = $true
$reference = $results[0]
foreach ($result in $results) {
    foreach ($name in @('gpu.draws','gpu.indices','render.pass.count')) {
        if (!$result.work.ContainsKey($name) -or !$reference.work.ContainsKey($name) -or
            $result.work[$name] / $result.samples -ne $reference.work[$name] / $reference.samples) { $matchedWork = $false }
    }
}
$summary = foreach ($api in $Apis) {
    $runs = @($results | Where-Object api -EQ $api)
    $values = @($runs.p50)
    [pscustomobject]@{api=$api;repetitions=$runs.Count;p50Median=Get-Percentile $values 0.5;
        p50Min=($values | Measure-Object -Minimum).Minimum;p50Max=($values | Measure-Object -Maximum).Maximum;
        p95Median=Get-Percentile @($runs.p95) 0.5}
}
$reportObject = [pscustomobject]@{schema=1;configuration=$Config;executableSha256=(Get-FileHash $executable).Hash;scene=$Scene;width=$Width;height=$Height;
    presented=[bool]$Presented;warmup=$Warmup;matchedAdapter=$matchedAdapter;matchedUsefulWork=$matchedWork;gpuTiming=$null;
    timingInstrument='Built-in CPU wall-clock aggregates';startup='Per-run telemetry-epoch milestones and shader/pipeline CPU operations are separate from steady state; cache contents are not cleared. Frame completion is CPU submission/present return, not GPU/display completion.';
    summary=@($summary);runs=$results.ToArray()}
$reportObject | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $OutputDirectory 'comparison.json') -Encoding UTF8
$lines = @('# CPU Phase Comparison','',"Configuration: $Config. GPU timing: unavailable. CPU timings are inclusive and must not be summed.",
    "Matched adapter: $matchedAdapter. Matched useful work: $matchedWork.",'','| API | Repeats | p50 median ms | p50 range ms | p95 median ms |','|---|---:|---:|---|---:|')
foreach ($row in $summary) { $lines += "| $($row.api) | $($row.repetitions) | $($row.p50Median) | $($row.p50Min) .. $($row.p50Max) | $($row.p95Median) |" }
$native = $summary | Where-Object api -EQ 'd3d12'
$dawn = $summary | Where-Object api -EQ 'webgpu'
if ($native -and $dawn) {
    $delta = $dawn.p50Median - $native.p50Median
    $spread = [Math]::Max($native.p50Max - $native.p50Min, $dawn.p50Max - $dawn.p50Min)
    $conclusive = $Config -eq 'Release' -and $Repetitions -ge 3 -and $matchedAdapter -and $matchedWork -and [Math]::Abs($delta) -gt [Math]::Max(0.01,$spread)
    $percent = if ($native.p50Median -gt 0) { 100 * $delta / $native.p50Median } else { $null }
    $lines += '',"Dawn - native p50: $delta ms ($percent percent). Interpretable above observed run spread: $conclusive."
}
$lines += '', 'Phase p50/p95, sample counts, upload matrices and raw reports are in comparison.json and each telemetry file.',
    'Startup milestones, shader preparation/translation, pipeline creation and cache provenance are recorded separately per run. Cache state is observed, not forced cold.',
    'This run does not measure instrumentation overhead against a compiled-out executable. PresentMon requires presented runs; ETW/PIX captures are separate evidence, not zero-overhead baselines.'
$lines | Set-Content (Join-Path $OutputDirectory 'Report.md') -Encoding UTF8
Write-Output "Profiling comparison recorded: $OutputDirectory"