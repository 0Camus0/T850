[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [ValidateRange(1, 10)][int]$Repetitions = 5,
    [ValidateRange(30, 10000)][int]$MeasuredFrames = 600,
    [ValidateRange(300, 20000)][int]$TraceFrames = 6000,
    [string]$CollectorPath,
    [string]$ExpectedCollectorSha256,
    [string]$EvidenceDirectory,
    [switch]$SkipWpr,
    [switch]$DeferEtlValidation,
    [switch]$TraceOnly,
    [switch]$ZeroPresentOnly,
    [switch]$Quick
)

$ErrorActionPreference = 'Stop'
$runtime = Join-Path $Root 'runtime'
$exe = Join-Path $runtime 'DayScene.exe'
$collector = if ($CollectorPath) { $CollectorPath } else { Join-Path $Root 'tools\PresentMon-2.6.0-ARM64-pr666.exe' }
$evidence = if ($EvidenceDirectory) { $EvidenceDirectory } else { Join-Path $Root 'offline-evidence' }
$holdFrame = if ($Quick) { 60 } else { 3000 }
$measured = if ($Quick) { 60 } else { $MeasuredFrames }
$traceMeasured = if ($Quick) { 180 } else { $TraceFrames }
$repeatCount = if ($Quick) { 1 } else { $Repetitions }
$defaultArm64CollectorHash = 'F6A44EEBB392F32BF827E0578C67434CD1D9C3D59BE898F60E549F78BEC09093'

if (!(Test-Path $exe)) { throw "DayScene is missing: $exe" }
if ($TraceOnly -and $ZeroPresentOnly) { throw 'TraceOnly and ZeroPresentOnly are mutually exclusive.' }
if (!(Test-Path $collector)) { throw "PresentMon is missing: $collector" }
$collector = (Resolve-Path $collector).Path
$collectorHash = (Get-FileHash $collector -Algorithm SHA256).Hash
$requiredCollectorHash = if ($ExpectedCollectorSha256) { $ExpectedCollectorSha256 } elseif (!$CollectorPath) { $defaultArm64CollectorHash } else { throw 'CollectorPath requires ExpectedCollectorSha256.' }
if ($requiredCollectorHash -and $collectorHash -ne $requiredCollectorHash) { throw 'PresentMon hash mismatch.' }
if (Test-Path $evidence) { throw "Use a fresh evidence root: $evidence" }
if (Get-Process DayScene, PresentMon* -ErrorAction SilentlyContinue) { throw 'Renderer or collector already active.' }
if (@(logman query -ets 2>&1 | Where-Object { $_ -match '^T850-' }).Count) { throw 'Owned ETW session already active.' }
if (!$SkipWpr -and (wpr.exe -status 2>&1 | Out-String) -notmatch 'WPR is not recording') { throw 'WPR is already recording.' }
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
function Read-SharedText([string]$Path) {
    if (!(Test-Path $Path)) { return '' }
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $reader = [IO.StreamReader]::new($stream)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } catch { $stream.Dispose(); throw }
}
function Stop-OwnedProcess([Diagnostics.Process]$Process) {
    if (!$Process -or $Process.HasExited) { return }
    [void]$Process.CloseMainWindow()
    if (!$Process.WaitForExit(10000)) { $Process.Kill(); $Process.WaitForExit() }
}
function Stop-PresentMon([Diagnostics.Process]$Monitor, [string]$Session, [string]$Directory) {
    if (!$Monitor) { throw 'PresentMon process is unavailable.' }
    if ($Monitor.HasExited) { throw "PresentMon exited before explicit stop: $($Monitor.ExitCode)" }
    $stop = Start-Process $collector -ArgumentList (Quote-Arguments @('--session_name',$Session,'--terminate_existing_session')) `
        -RedirectStandardOutput (Join-Path $Directory 'presentmon-stop.log') `
        -RedirectStandardError (Join-Path $Directory 'presentmon-stop.stderr.log') -PassThru
    $null = $stop.Handle
    try {
        if (!$stop.WaitForExit(15000)) { $stop.Kill(); $stop.WaitForExit() }
        if ($stop.ExitCode -ne 0) { throw "PresentMon stop failed: $($stop.ExitCode)" }
    } finally { $stop.Dispose() }
    if (!$Monitor.WaitForExit(15000)) { $Monitor.Kill(); $Monitor.WaitForExit() }
}
function Assert-EtlLossless([string]$EtlPath, [string]$Directory) {
    $xperf = Get-Command xperf.exe -ErrorAction Stop
    $statsPath = Join-Path $Directory 'xperf-tracestats.txt'
    & $xperf.Source -i $EtlPath -a tracestats *> $statsPath
    if ($LASTEXITCODE -ne 0) { throw "xperf tracestats failed: $EtlPath" }
    $stats = Get-Content $statsPath -Raw
    $lostBuffers = [regex]::Match($stats, 'Total\s+#\s+Lost\s+Buffers\s*:\s*(\d+)', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    $lostEvents = [regex]::Match($stats, 'Total\s+#\s+Lost\s+Events\s*:\s*(\d+)', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (!$lostBuffers.Success -or !$lostEvents.Success) { throw "Unable to parse ETL loss counters: $EtlPath" }
    $result = [pscustomobject]@{lostBuffers=[int64]$lostBuffers.Groups[1].Value;lostEvents=[int64]$lostEvents.Groups[1].Value}
    if ($result.lostBuffers -ne 0 -or $result.lostEvents -ne 0) { throw "ETL contains lost data: $EtlPath" }
    return $result
}
function New-Arguments($Cell, [string]$Directory, [int]$Frames) {
    $arguments = @('--api',$Cell.Api,'--scene','1','--width','1920','--height','1080',
        '--postProcessMode',$Cell.Mode,'--culling','full','--benchmarkNoPresent',
        '--benchmarkHoldFrame',"$holdFrame",'--benchmarkFrames',"$Frames",
        '--benchmarkFixedDt','0.0166666667','--benchmarkOutput',(Join-Path $Directory 'engine-benchmark.json'),
        '--logLevel','info','--logFile',(Join-Path $Directory 'engine.log'))
    if ($Cell.Api -eq 'webgpu') { $arguments += @('--shaderFlow',$Cell.Flow) }
    return $arguments
}
function Wait-OfflineMarker([Diagnostics.Process]$Process, [string]$LogPath, [int]$Frames, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $text = Read-SharedText $LogPath
        $start = [regex]::Match($text, "\[BenchmarkOffline\] start simulationFrame=$holdFrame qpcMs=(\d+\.\d+)")
        $complete = [regex]::Match($text, "\[BenchmarkOffline\] completed frames=$Frames elapsedMs=(\d+\.\d+) completedFps=(\d+\.\d+) qpcMs=(\d+\.\d+) presents=0")
        if ($complete.Success) { return [pscustomobject]@{Text=$text;Start=$start;Complete=$complete} }
        if ($Process.HasExited) { throw "DayScene exited before offline completion: $($Process.ExitCode)" }
        [void]$Process.WaitForExit(100)
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Timed out waiting for offline benchmark completion.'
}
function Assert-EngineLog([string]$Text, $Cell) {
    if ($Text -match '\[ERROR\s*\]|Device lost|Queue submission failed') { throw "Renderer error: $($Cell.Name)" }
    if ($Cell.Api -eq 'webgpu' -and $Text -notmatch 'provider=Dawn backend=D3D12') { throw "Not Dawn/D3D12: $($Cell.Name)" }
    if ($Text -match 'D3D12_Present_Call|presentation failed') { throw "Presentation path reached: $($Cell.Name)" }
}
function Invoke-OfflineRun($Cell, [string]$Name, [int]$Frames, [switch]$Trace) {
    $directory = Join-Path $evidence $Name
    [void][IO.Directory]::CreateDirectory($directory)
    $arguments = New-Arguments $Cell $directory $Frames
    $process = $null
    $wprStarted = $false
    $counterRows = [Collections.Generic.List[object]]::new()
    try {
        $process = Start-Process $exe -WorkingDirectory $runtime -ArgumentList (Quote-Arguments $arguments) `
            -RedirectStandardOutput (Join-Path $directory 'stdout.log') `
            -RedirectStandardError (Join-Path $directory 'stderr.log') -PassThru
        $null = $process.Handle
        $logPath = Join-Path $directory 'engine.log'
        $deadline = [DateTime]::UtcNow.AddMinutes(10)
        $traceStarted = $false
        do {
            $text = Read-SharedText $logPath
            $start = [regex]::Match($text, "\[BenchmarkOffline\] start simulationFrame=$holdFrame qpcMs=(\d+\.\d+)")
            if ($Trace -and $start.Success -and !$traceStarted) {
                $traceStarted = $true
                if (!$SkipWpr) {
                    wpr.exe -start GPU -filemode *> (Join-Path $directory 'wpr-start.log')
                    if ($LASTEXITCODE -ne 0) { throw 'WPR GPU start failed.' }
                    $wprStarted = $true
                }
            }
            $complete = [regex]::Match($text, "\[BenchmarkOffline\] completed frames=$Frames elapsedMs=(\d+\.\d+) completedFps=(\d+\.\d+) qpcMs=(\d+\.\d+) presents=0")
            if ($complete.Success) { break }
            if ($process.HasExited) { throw "DayScene exited before completion: $($process.ExitCode)" }
            if ($Trace -and $traceStarted) {
                $samples = Get-Counter '\GPU Engine(*)\Utilization Percentage' -MaxSamples 1 -ErrorAction SilentlyContinue
                foreach ($sample in @($samples.CounterSamples | Where-Object { $_.Path -match "pid_$($process.Id)_" })) {
                    $counterRows.Add([pscustomobject]@{timestamp=$sample.Timestamp.ToUniversalTime().ToString('o');path=$sample.Path;value=$sample.CookedValue})
                }
            }
            [void]$process.WaitForExit($(if ($Trace) { 900 } else { 100 }))
        } while ([DateTime]::UtcNow -lt $deadline)
        if (!$complete.Success) { throw 'Offline benchmark timeout.' }
        if ($wprStarted) {
            wpr.exe -stop (Join-Path $directory 'gpu.etl') "T850 offline GPU trace $($Cell.Name)" *> (Join-Path $directory 'wpr-stop.log')
            if ($LASTEXITCODE -ne 0) { throw 'WPR GPU stop failed.' }
            $wprStarted = $false
        }
        $traceStats = if ($Trace -and !$SkipWpr -and !$DeferEtlValidation) { Assert-EtlLossless (Join-Path $directory 'gpu.etl') $directory } else { $null }
        Stop-OwnedProcess $process
        Assert-EngineLog $text $Cell
        $elapsedMs = [double]$complete.Groups[1].Value
        $fps = [double]$complete.Groups[2].Value
        if ($elapsedMs -le 0.0 -or $fps -le 0.0) { throw 'Invalid completed throughput.' }
        $counterRows | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $directory 'gpu-engine-counters.json') -Encoding UTF8
        $metadata = [pscustomobject]@{cell=$Cell.Name;frames=$Frames;elapsedMs=$elapsedMs;completedFps=$fps;presents=0;
            startQpcMs=if($start.Success){[double]$start.Groups[1].Value}else{$null};completeQpcMs=[double]$complete.Groups[3].Value;
            trace=[bool]$Trace;wprCaptured=[bool]($Trace -and !$SkipWpr);etlValidationDeferred=[bool]($Trace -and !$SkipWpr -and $DeferEtlValidation);traceStats=$traceStats;counterRows=$counterRows.Count;arguments=$arguments;directory=$directory}
        $metadata | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $directory 'capture.json') -Encoding UTF8
        return $metadata
    } finally {
        if ($wprStarted) { wpr.exe -cancel *> (Join-Path $directory 'wpr-cancel.log') }
        if ($process) { Stop-OwnedProcess $process; $process.Dispose() }
    }
}
function Assert-ZeroPresents($Cell) {
    $directory = Join-Path $evidence "zero-present\$($Cell.Name)"
    [void][IO.Directory]::CreateDirectory($directory)
    $arguments = New-Arguments $Cell $directory 60
    $process = Start-Process $exe -WorkingDirectory $runtime -ArgumentList (Quote-Arguments $arguments) `
        -RedirectStandardOutput (Join-Path $directory 'stdout.log') -RedirectStandardError (Join-Path $directory 'stderr.log') -PassThru
    $null = $process.Handle
    $session = "T850-Offline-Zero-$($Cell.Api)"
    $csv = Join-Path $directory 'presentmon.csv'
    $monitor = Start-Process $collector -ArgumentList (Quote-Arguments @('--session_name',$session,'--no_console_stats','--v2_metrics','--qpc_time_ms',
        '--timed','120','--terminate_after_timed','--output_file',$csv,'--process_id',"$($process.Id)")) -PassThru
    $null = $monitor.Handle
    try {
        if ($monitor.WaitForExit(1000)) { throw "PresentMon exited during startup: $($monitor.ExitCode)" }
        $marker = Wait-OfflineMarker $process (Join-Path $directory 'engine.log') 60 300
        Stop-PresentMon $monitor $session $directory
        Stop-OwnedProcess $process
        Assert-EngineLog $marker.Text $Cell
        $rows = if (Test-Path $csv) { @(Import-Csv $csv | Where-Object { [int]$_.ProcessID -eq $process.Id }) } else { @() }
        if ($rows.Count -ne 0) { throw "Offline run presented $($rows.Count) frames: $($Cell.Name)" }
        return [pscustomobject]@{cell=$Cell.Name;presentRows=0;passed=$true}
    } finally {
        if (!$monitor.HasExited) { try { Stop-PresentMon $monitor $session $directory } catch {} }
        Stop-OwnedProcess $process
        $monitor.Dispose(); $process.Dispose()
    }
}

$result = [ordered]@{schema=1;startedUtc=[DateTime]::UtcNow.ToString('o');passed=$false;quick=[bool]$Quick;skipWpr=[bool]$SkipWpr;deferEtlValidation=[bool]$DeferEtlValidation;traceOnly=[bool]$TraceOnly;zeroPresentOnly=[bool]$ZeroPresentOnly;machine=$env:COMPUTERNAME;
    architecture=$env:PROCESSOR_ARCHITECTURE;executableSha256=(Get-FileHash $exe).Hash;collectorPath=$collector;collectorSha256=$collectorHash;
    scene=1;width=1920;height=1080;holdFrame=$holdFrame;measuredFrames=$measured;traceFrames=$traceMeasured;
    zeroPresent=@();runs=@();traces=@()}
try {
    if (!$TraceOnly) {
        $result.zeroPresent += Assert-ZeroPresents $cells[0]
        $result.zeroPresent += Assert-ZeroPresents $cells[2]
    }
    if (!$TraceOnly -and !$ZeroPresentOnly) {
        for ($repeat=1; $repeat -le $repeatCount; ++$repeat) {
            $order=@($cells); if ($repeat % 2 -eq 0) { [array]::Reverse($order) }
            foreach ($cell in $order) { $result.runs += Invoke-OfflineRun $cell "runs\$($cell.Name)-r$repeat" $measured }
        }
    }
    if (!$ZeroPresentOnly) {
        foreach ($cell in $cells) { $result.traces += Invoke-OfflineRun $cell "traces\$($cell.Name)" $traceMeasured -Trace }
    }
    $result.passed=$true
} catch { $result.failure=$_.Exception.ToString() }
finally { $result.completedUtc=[DateTime]::UtcNow.ToString('o');$result|ConvertTo-Json -Depth 10|Set-Content (Join-Path $evidence 'result.json') -Encoding UTF8 }
if(!$result.passed){throw $result.failure}
Write-Output "PASS: offline GPU matrix at $evidence"
