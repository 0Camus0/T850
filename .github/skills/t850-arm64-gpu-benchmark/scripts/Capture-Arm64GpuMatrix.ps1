[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [ValidateRange(1, 10)][int]$Repetitions = 5,
    [ValidateRange(1, 300)][int]$TraceSeconds = 60,
    [switch]$Quick
)

$ErrorActionPreference = 'Stop'
$runtime = Join-Path $Root 'runtime'
$exe = Join-Path $runtime 'DayScene.exe'
$collector = Join-Path $Root 'tools\PresentMon-2.6.0-ARM64-pr666.exe'
$evidence = Join-Path $Root 'evidence'
$expectedCollectorHash = 'F6A44EEBB392F32BF827E0578C67434CD1D9C3D59BE898F60E549F78BEC09093'
$holdFrame = if ($Quick) { 60 } else { 3000 }
$settleMs = if ($Quick) { 500 } else { 5000 }
$windowMs = 1000
$repetitionsToRun = if ($Quick) { 1 } else { $Repetitions }
$traceSecondsToRun = if ($Quick) { 3 } else { $TraceSeconds }

if (!(Test-Path $exe)) { throw "DayScene is missing: $exe" }
if (!(Test-Path $collector)) { throw "PresentMon is missing: $collector" }
if ((Get-FileHash $collector -Algorithm SHA256).Hash -ne $expectedCollectorHash) { throw 'PresentMon hash mismatch.' }
if (Test-Path $evidence) { throw "Use a fresh evidence root: $evidence" }
if (Get-Process DayScene, PresentMon* -ErrorAction SilentlyContinue) { throw 'Renderer or collector already active.' }
if (@(logman query -ets 2>&1 | Where-Object { $_ -match '^T850-' }).Count) { throw 'Owned ETW session already active.' }
if ((wpr.exe -status 2>&1 | Out-String) -notmatch 'WPR is not recording') { throw 'WPR is already recording.' }
[void][IO.Directory]::CreateDirectory($evidence)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class T850WindowProbe {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
'@

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
    } catch {
        $stream.Dispose()
        throw
    }
}

function Wait-HoldMarker([Diagnostics.Process]$Process, [string]$LogPath, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $text = Read-SharedText $LogPath
        $match = [regex]::Match($text, "\[BenchmarkHold\] runtimeFrame=$holdFrame simulationDt=0 rendering continues uncapped=1 qpcMs=(\d+\.\d+)")
        if ($match.Success) { return [double]$match.Groups[1].Value }
        if ($Process.HasExited) { throw "DayScene exited before hold marker: $($Process.ExitCode)" }
        [void]$Process.WaitForExit(100)
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Timed out waiting for deterministic hold marker.'
}

function Stop-OwnedProcess([Diagnostics.Process]$Process) {
    if (!$Process -or $Process.HasExited) { return }
    [void]$Process.CloseMainWindow()
    if (!$Process.WaitForExit(30000)) { $Process.Kill(); $Process.WaitForExit() }
}

function Stop-PresentMon([Diagnostics.Process]$Monitor, [string]$Session, [string]$Directory) {
    if (!$Monitor -or $Monitor.HasExited) { return }
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

function Get-WindowState([Diagnostics.Process]$Process) {
    $Process.Refresh()
    $handle = $Process.MainWindowHandle
    $rect = [T850WindowProbe+RECT]::new()
    $ok = $handle -ne [IntPtr]::Zero -and [T850WindowProbe]::GetClientRect($handle, [ref]$rect)
    return [pscustomobject]@{
        handle = $handle.ToInt64()
        width = if ($ok) { $rect.Right - $rect.Left } else { 0 }
        height = if ($ok) { $rect.Bottom - $rect.Top } else { 0 }
        visible = $ok -and [T850WindowProbe]::IsWindowVisible($handle)
        foreground = $ok -and [T850WindowProbe]::GetForegroundWindow() -eq $handle
    }
}

function New-SceneArguments($Cell, [string]$Directory) {
    $args = @('--api',$Cell.Api,'--scene','1','--width','1920','--height','1080',
        '--postProcessMode',$Cell.Mode,'--culling','full','--regressionFixedDt','0.0166666667',
        '--benchmarkHoldFrame',"$holdFrame",'--logLevel','info','--logFile',(Join-Path $Directory 'engine.log'))
    if ($Cell.Api -eq 'webgpu') { $args += @('--shaderFlow',$Cell.Flow) }
    return $args
}

function Remove-ShaderCache {
    $cache = Join-Path $runtime 'Shaders\.t8shadercache'
    if (Test-Path $cache) { Remove-Item $cache -Recurse -Force }
}

function Invoke-Bounded([string[]]$Arguments, [string]$Directory, [int]$TimeoutSeconds) {
    [void][IO.Directory]::CreateDirectory($Directory)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process $exe -WorkingDirectory $runtime -ArgumentList (Quote-Arguments $Arguments) `
        -RedirectStandardOutput (Join-Path $Directory 'stdout.log') `
        -RedirectStandardError (Join-Path $Directory 'stderr.log') -PassThru
    $null = $process.Handle
    try {
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw 'Process timeout.' }
        $exit = $process.ExitCode
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
        $timer.Stop()
    }
    if ($exit -ne 0) { throw "Process failed: $exit" }
    return [pscustomobject]@{ exitCode=$exit; wallMs=$timer.Elapsed.TotalMilliseconds; arguments=$Arguments }
}

function Measure-CompileAll($Case, [string]$State) {
    if ($State -eq 'cold') { Remove-ShaderCache }
    $directory = Join-Path $evidence "compile-all\$($Case.Name)-$State"
    $args = @('--compileShaders','--api',$Case.Api,'--logLevel','info')
    if ($Case.Api -eq 'webgpu') { $args += @('--shaderFlow',$Case.Flow) }
    $result = Invoke-Bounded $args $directory 900
    $text = (Get-Content (Join-Path $directory 'stdout.log') -Raw) + (Get-Content (Join-Path $directory 'stderr.log') -Raw)
    $match = [regex]::Match($text, '\[ShaderPrecompile\] complete: (\d+) succeeded, (\d+) failed')
    if (!$match.Success -or [int]$match.Groups[2].Value -ne 0) { throw "Compile-all failed: $($Case.Name) $State" }
    return [pscustomobject]@{ case=$Case.Name; state=$State; wallMs=$result.wallMs; succeeded=[int]$match.Groups[1].Value; failed=0; directory=$directory }
}

function Measure-Startup($Cell, [string]$State) {
    if ($State -eq 'cold') { Remove-ShaderCache }
    $directory = Join-Path $evidence "startup\$($Cell.Name)-$State"
    [void][IO.Directory]::CreateDirectory($directory)
    $args = @('--api',$Cell.Api,'--scene','1','--width','1920','--height','1080','--postProcessMode',$Cell.Mode,
        '--culling','full','--regressionFixedDt','0.0166666667','--profileCpuOnly','--profileFrames','1',
        '--telemetry','--telemetryFrequencyFrames','0','--telemetryOutput',(Join-Path $directory 'telemetry.json'),
        '--logLevel','info','--logFile',(Join-Path $directory 'engine.log'))
    if ($Cell.Api -eq 'webgpu') { $args += @('--shaderFlow',$Cell.Flow) }
    $run = Invoke-Bounded $args $directory 300
    $engine = Read-SharedText (Join-Path $directory 'engine.log')
    if ($engine -match '\[ERROR\s*\]|Device lost|Queue submission failed') {
        throw "Renderer or asset error in startup measurement: $($Cell.Name) $State"
    }
    if ($Cell.Api -eq 'webgpu' -and $engine -notmatch 'provider=Dawn backend=D3D12') {
        throw "Startup measurement did not use Dawn/D3D12: $($Cell.Name) $State"
    }
    $report = @(Get-ChildItem $directory -Filter 'telemetry_*.json')
    if ($report.Count -ne 1) { throw "Startup telemetry missing: $($Cell.Name) $State" }
    $telemetry = Get-Content $report[0].FullName -Raw | ConvertFrom-Json
    $startup = @($telemetry.frames | Where-Object startup)[0]
    if (!$startup -or $null -eq $telemetry.startupTiming.firstRuntimeFrameCompleteMs) { throw 'Startup attribution missing.' }
    return [pscustomobject]@{
        cell=$Cell.Name; state=$State; wallMs=$run.wallMs
        firstRuntimeFrameCompleteMs=$telemetry.startupTiming.firstRuntimeFrameCompleteMs
        shaderScopes=@($startup.scopes | Where-Object name -Match '^(shader\.|pipeline\.)')
        shaderCounters=@($startup.counters | Where-Object name -Match '^shader\.')
        report=$report[0].FullName
    }
}

function Invoke-LightCapture($Cell, [int]$Repeat) {
    $name = "$($Cell.Name)-r$Repeat"
    $directory = Join-Path $evidence "light\$name"
    [void][IO.Directory]::CreateDirectory($directory)
    $session = "T850-GPU-$($Cell.Name)-$Repeat"
    $process = $null
    $monitor = $null
    try {
        $arguments = New-SceneArguments $Cell $directory
        $process = Start-Process $exe -WorkingDirectory $runtime -ArgumentList (Quote-Arguments $arguments) `
            -RedirectStandardOutput (Join-Path $directory 'stdout.log') `
            -RedirectStandardError (Join-Path $directory 'stderr.log') -PassThru
        $null = $process.Handle
        $monitorArgs = @('--session_name',$session,'--no_console_stats','--v2_metrics','--qpc_time_ms','--timed','180',
            '--terminate_after_timed','--output_file',(Join-Path $directory 'presentmon.csv'),'--process_id',"$($process.Id)")
        $monitor = Start-Process $collector -ArgumentList (Quote-Arguments $monitorArgs) `
            -RedirectStandardOutput (Join-Path $directory 'presentmon.log') `
            -RedirectStandardError (Join-Path $directory 'presentmon.stderr.log') -PassThru
        $null = $monitor.Handle
        $holdQpcMs = Wait-HoldMarker $process (Join-Path $directory 'engine.log') 180
        $window = Get-WindowState $process
        [void]$process.WaitForExit($settleMs + $windowMs + 500)
        Stop-PresentMon $monitor $session $directory
        Stop-OwnedProcess $process
        $engine = Read-SharedText (Join-Path $directory 'engine.log')
        if ($engine -match '\[ERROR\s*\]|Device lost|Queue submission failed') { throw 'Renderer error in light capture.' }
        if ($Cell.Api -eq 'webgpu' -and $engine -notmatch 'provider=Dawn backend=D3D12') { throw 'WebGPU did not use Dawn/D3D12.' }
        $rows = @(Import-Csv (Join-Path $directory 'presentmon.csv') | Where-Object {
            [int]$_.ProcessID -eq $process.Id -and [double]$_.CPUStartQPCTime -ge ($holdQpcMs + $settleMs) -and
            [double]$_.CPUStartQPCTime -lt ($holdQpcMs + $settleMs + $windowMs)
        })
        $gpu = @($rows | Where-Object { $_.GPUBusy -notin @('','NA') -and [double]$_.GPUBusy -gt 0 })
        if ($rows.Count -lt 20 -or $gpu.Count -lt 20) { throw "Insufficient accepted PresentMon rows: rows=$($rows.Count) gpu=$($gpu.Count)" }
        $qpcSpanMs = [double]$rows[-1].CPUStartQPCTime - [double]$rows[0].CPUStartQPCTime
        $presentRateHz = if ($qpcSpanMs -gt 0.0) { 1000.0 * ($rows.Count - 1) / $qpcSpanMs } else { 0.0 }
        $invalidPresentationRows = @($rows | Where-Object {
            $_.SyncInterval -ne '0' -or $_.AllowsTearing -ne '1' -or
            (([int]$_.PresentFlags -band 512) -eq 0)
        })
        if ($invalidPresentationRows.Count -or $presentRateHz -le 75.0) {
            throw "Capture is paced or lacks tearing: rate=$presentRateHz invalidRows=$($invalidPresentationRows.Count)"
        }
        if (!$window.visible -or $window.width -ne 1920 -or $window.height -ne 1080) { throw "Window mismatch: $($window.width)x$($window.height) visible=$($window.visible)" }
        $accepted = $rows | ConvertTo-Csv -NoTypeInformation
        Set-Content (Join-Path $directory 'accepted-window.csv') $accepted -Encoding UTF8
        $meta = [pscustomobject]@{cell=$Cell.Name;repeat=$Repeat;pid=$process.Id;holdQpcMs=$holdQpcMs;settleMs=$settleMs;windowMs=$windowMs;presentRateHz=$presentRateHz;
            rows=$rows.Count;gpuBusyRows=$gpu.Count;window=$window;arguments=$arguments;directory=$directory}
        $meta | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $directory 'capture.json') -Encoding UTF8
        return $meta
    } finally {
        if ($monitor) { try { Stop-PresentMon $monitor $session $directory } catch {} ; $monitor.Dispose() }
        if ($process) { Stop-OwnedProcess $process; $process.Dispose() }
    }
}

function Invoke-TraceCapture($Cell) {
    $directory = Join-Path $evidence "trace\$($Cell.Name)"
    [void][IO.Directory]::CreateDirectory($directory)
    $session = "T850-GPU-Trace-$($Cell.Name)"
    $process = $null
    $monitor = $null
    $wprStarted = $false
    try {
        $arguments = New-SceneArguments $Cell $directory
        $process = Start-Process $exe -WorkingDirectory $runtime -ArgumentList (Quote-Arguments $arguments) `
            -RedirectStandardOutput (Join-Path $directory 'stdout.log') `
            -RedirectStandardError (Join-Path $directory 'stderr.log') -PassThru
        $null = $process.Handle
        $monitorArgs = @('--session_name',$session,'--no_console_stats','--v2_metrics','--qpc_time_ms','--timed','180',
            '--terminate_after_timed','--output_file',(Join-Path $directory 'presentmon.csv'),'--process_id',"$($process.Id)")
        $monitor = Start-Process $collector -ArgumentList (Quote-Arguments $monitorArgs) `
            -RedirectStandardOutput (Join-Path $directory 'presentmon.log') `
            -RedirectStandardError (Join-Path $directory 'presentmon.stderr.log') -PassThru
        $null = $monitor.Handle
        $holdQpcMs = Wait-HoldMarker $process (Join-Path $directory 'engine.log') 180
        $window = Get-WindowState $process
        wpr.exe -start GPU -filemode *> (Join-Path $directory 'wpr-start.log')
        if ($LASTEXITCODE -ne 0) { throw 'WPR GPU start failed.' }
        $wprStarted = $true
        $counterSet = Get-Counter '\GPU Engine(*)\Utilization Percentage' -SampleInterval 1 -MaxSamples $traceSecondsToRun
        $counterRows = foreach ($sampleSet in $counterSet.CounterSamples) {
            if ($sampleSet.Path -match "pid_$($process.Id)_") {
                [pscustomobject]@{timestamp=$sampleSet.Timestamp.ToUniversalTime().ToString('o');path=$sampleSet.Path;value=$sampleSet.CookedValue}
            }
        }
        $counterRows | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $directory 'gpu-engine-counters.json') -Encoding UTF8
        wpr.exe -stop (Join-Path $directory 'gpu.etl') "T850 held-frame GPU trace $($Cell.Name)" *> (Join-Path $directory 'wpr-stop.log')
        if ($LASTEXITCODE -ne 0) { throw 'WPR GPU stop failed.' }
        $wprStarted = $false
        Stop-PresentMon $monitor $session $directory
        Stop-OwnedProcess $process
        $engine = Read-SharedText (Join-Path $directory 'engine.log')
        if ($engine -match '\[ERROR\s*\]|Device lost|Queue submission failed') { throw 'Renderer error in trace capture.' }
        $rows = @(Import-Csv (Join-Path $directory 'presentmon.csv') | Where-Object {
            [int]$_.ProcessID -eq $process.Id -and [double]$_.CPUStartQPCTime -ge $holdQpcMs -and
            [double]$_.CPUStartQPCTime -lt ($holdQpcMs + $traceSecondsToRun * 1000)
        })
        if (@($rows | Where-Object { $_.GPUBusy -notin @('','NA') -and [double]$_.GPUBusy -gt 0 }).Count -lt ($traceSecondsToRun * 20)) {
            throw 'Insufficient GPU-busy trace rows.'
        }
        $qpcSpanMs = [double]$rows[-1].CPUStartQPCTime - [double]$rows[0].CPUStartQPCTime
        $presentRateHz = if ($qpcSpanMs -gt 0.0) { 1000.0 * ($rows.Count - 1) / $qpcSpanMs } else { 0.0 }
        $invalidPresentationRows = @($rows | Where-Object {
            $_.SyncInterval -ne '0' -or $_.AllowsTearing -ne '1' -or
            (([int]$_.PresentFlags -band 512) -eq 0)
        })
        if ($invalidPresentationRows.Count -or $presentRateHz -le 75.0) {
            throw "Trace is paced or lacks tearing: rate=$presentRateHz invalidRows=$($invalidPresentationRows.Count)"
        }
        $rows | ConvertTo-Csv -NoTypeInformation | Set-Content (Join-Path $directory 'accepted-window.csv') -Encoding UTF8
        $meta = [pscustomobject]@{cell=$Cell.Name;pid=$process.Id;holdQpcMs=$holdQpcMs;traceSeconds=$traceSecondsToRun;presentRateHz=$presentRateHz;
            rows=$rows.Count;window=$window;counterRows=@($counterRows).Count;arguments=$arguments;directory=$directory}
        $meta | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $directory 'capture.json') -Encoding UTF8
        return $meta
    } finally {
        if ($wprStarted) { wpr.exe -cancel *> (Join-Path $directory 'wpr-cancel.log') }
        if ($monitor) { try { Stop-PresentMon $monitor $session $directory } catch {} ; $monitor.Dispose() }
        if ($process) { Stop-OwnedProcess $process; $process.Dispose() }
    }
}

$result = [ordered]@{
    schema=1;startedUtc=[DateTime]::UtcNow.ToString('o');passed=$false;quick=[bool]$Quick
    machine=$env:COMPUTERNAME;architecture=$env:PROCESSOR_ARCHITECTURE
    executableSha256=(Get-FileHash $exe -Algorithm SHA256).Hash
    collectorSha256=$expectedCollectorHash;collectorSource='PresentMon PR #666 commit 503fc8ec5e867f07b94e12a667bc5e5430e79ffc'
    scene=1;width=1920;height=1080;fixedDt=0.0166666667;holdFrame=$holdFrame;settleMs=$settleMs;windowMs=$windowMs
    compileAll=@();startup=@();light=@();traces=@()
}
try {
    $compileCases = @(
        [pscustomobject]@{Name='d3d12';Api='d3d12';Flow='native'},
        [pscustomobject]@{Name='wgsl';Api='webgpu';Flow='wgsl'},
        [pscustomobject]@{Name='spirv';Api='webgpu';Flow='spirv'}
    )
    foreach ($case in $compileCases) {
        $result.compileAll += Measure-CompileAll $case 'cold'
        $result.compileAll += Measure-CompileAll $case 'warm'
    }
    foreach ($cell in $cells) {
        $result.startup += Measure-Startup $cell 'cold'
        $result.startup += Measure-Startup $cell 'warm'
    }
    foreach ($case in $compileCases) { [void](Measure-CompileAll $case 'warm') }
    for ($repeat = 1; $repeat -le $repetitionsToRun; ++$repeat) {
        $order = @($cells)
        if ($repeat % 2 -eq 0) { [array]::Reverse($order) }
        foreach ($cell in $order) { $result.light += Invoke-LightCapture $cell $repeat }
    }
    foreach ($cell in $cells) { $result.traces += Invoke-TraceCapture $cell }
    $result.passed = $true
} catch {
    $result.failure = $_.Exception.ToString()
} finally {
    $result.completedUtc = [DateTime]::UtcNow.ToString('o')
    $result | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $evidence 'result.json') -Encoding UTF8
}
if (!$result.passed) { throw $result.failure }
Write-Output "PASS: ARM64 GPU matrix evidence at $evidence"
