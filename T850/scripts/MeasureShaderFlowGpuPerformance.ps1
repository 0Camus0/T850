[CmdletBinding()]
param(
    [ValidateSet(
        'native-d3d12-hlsl',
        'native-webgpu-wgsl',
        'native-webgpu-spirv',
        'edge-webgpu-wgsl',
        'edge-webgpu-spirv'
    )]
    [string[]]$Cases = @(
        'native-d3d12-hlsl',
        'native-webgpu-wgsl',
        'native-webgpu-spirv',
        'edge-webgpu-wgsl',
        'edge-webgpu-spirv'
    ),

    [ValidateRange(320, 16384)]
    [int]$Width = 1280,

    [ValidateRange(200, 16384)]
    [int]$Height = 720,

    [ValidateRange(0, 6)]
    [int]$Scene = 1,

    [ValidateSet('full', 'lazy', 'disabled')]
    [string]$Culling = 'full',

    [ValidateRange(1, 600)]
    [int]$WarmupSeconds = 15,

    [ValidateRange(1, 3600)]
    [int]$CaptureSeconds = 20,

    [ValidateRange(1, 20)]
    [int]$RunsPerCase = 3,

    [ValidateRange(0, 2147483647)]
    [int]$RandomSeed = 850,

    [string]$OutputDirectory,

    [string]$PresentMonPath,
    [string]$ExecutablePath,
    [string]$EdgePath,
    [string]$EdgeUrl = 'http://127.0.0.1:8765/',

    [ValidateRange(5, 120)]
    [int]$EdgeProcessTimeoutSeconds = 30,

    [switch]$GenerateOnly,
    [switch]$SelfTest
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -lt [Version]'5.1') {
    throw 'MeasureShaderFlowGpuPerformance.ps1 requires PowerShell 5.1 or newer.'
}

$invariantCulture = [Globalization.CultureInfo]::InvariantCulture

function Get-CaseDefinitions {
    @(
        [pscustomobject]@{
            Name = 'native-d3d12-hlsl'
            Host = 'native'
            Api = 'd3d12'
            ShaderPath = 'Native HLSL (D3D12)'
            ShaderFlow = 'native-hlsl'
        },
        [pscustomobject]@{
            Name = 'native-webgpu-wgsl'
            Host = 'native'
            Api = 'webgpu'
            ShaderPath = 'WGSL'
            ShaderFlow = 'wgsl'
        },
        [pscustomobject]@{
            Name = 'native-webgpu-spirv'
            Host = 'native'
            Api = 'webgpu'
            ShaderPath = 'HLSL -> SPIR-V -> WGSL'
            ShaderFlow = 'spirv'
        },
        [pscustomobject]@{
            Name = 'edge-webgpu-wgsl'
            Host = 'edge'
            Api = 'webgpu'
            ShaderPath = 'WGSL'
            ShaderFlow = 'wgsl'
        },
        [pscustomobject]@{
            Name = 'edge-webgpu-spirv'
            Host = 'edge'
            Api = 'webgpu'
            ShaderPath = 'HLSL -> SPIR-V -> WGSL'
            ShaderFlow = 'spirv'
        }
    )
}

function Get-Mean {
    param([double[]]$Values)

    if ($null -eq $Values -or $Values.Count -eq 0) {
        return $null
    }
    return [double](($Values | Measure-Object -Average).Average)
}

function Get-Percentile {
    param(
        [double[]]$Values,
        [ValidateRange(0.0, 1.0)][double]$Percentile
    )

    if ($null -eq $Values -or $Values.Count -eq 0) {
        return $null
    }

    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 1) {
        return [double]$sorted[0]
    }

    $position = ($sorted.Count - 1) * $Percentile
    $lower = [int][Math]::Floor($position)
    $upper = [int][Math]::Ceiling($position)
    if ($lower -eq $upper) {
        return [double]$sorted[$lower]
    }

    $fraction = $position - $lower
    return [double]$sorted[$lower] + (([double]$sorted[$upper] - [double]$sorted[$lower]) * $fraction)
}

function Get-MetricSummary {
    param([double[]]$Values)

    if ($null -eq $Values -or $Values.Count -eq 0) {
        return $null
    }

    [pscustomobject]@{
        Count = $Values.Count
        MeanMs = Get-Mean $Values
        P50Ms = Get-Percentile $Values 0.50
        P95Ms = Get-Percentile $Values 0.95
        P99Ms = Get-Percentile $Values 0.99
        MinMs = ($Values | Measure-Object -Minimum).Minimum
        MaxMs = ($Values | Measure-Object -Maximum).Maximum
    }
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )

    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}

function ConvertTo-DoubleOrNull {
    param($Value)

    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) {
        return $null
    }

    $parsed = 0.0
    if ([double]::TryParse(
        [string]$Value,
        [Globalization.NumberStyles]::Float,
        $invariantCulture,
        [ref]$parsed
    )) {
        return $parsed
    }
    return $null
}

function Get-RowValue {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)][string[]]$Names
    )

    foreach ($name in $Names) {
        $property = $Row.PSObject.Properties[$name]
        if ($null -ne $property) {
            return $property.Value
        }
    }
    return $null
}

function Get-MetricValues {
    param(
        [Parameter(Mandatory = $true)][object[]]$Rows,
        [Parameter(Mandatory = $true)][string[]]$Names
    )

    $values = [Collections.Generic.List[double]]::new()
    foreach ($row in $Rows) {
        $value = ConvertTo-DoubleOrNull (Get-RowValue -Row $row -Names $Names)
        if ($null -ne $value) {
            $values.Add([double]$value)
        }
    }
    return $values.ToArray()
}

function Resolve-PresentMon {
    if (-not [string]::IsNullOrWhiteSpace($PresentMonPath)) {
        if (-not (Test-Path -LiteralPath $PresentMonPath -PathType Leaf)) {
            throw "PresentMon was not found at '$PresentMonPath'."
        }
        return [IO.Path]::GetFullPath($PresentMonPath)
    }

    $command = Get-Command PresentMon.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $toolRoot = Join-Path $env:LOCALAPPDATA 'T850Tools\PresentMon'
    if (Test-Path -LiteralPath $toolRoot -PathType Container) {
        $candidate = Get-ChildItem -LiteralPath $toolRoot -Filter 'PresentMon*.exe' -File -Recurse |
            Sort-Object -Property LastWriteTimeUtc -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    throw "PresentMon was not found. Pass -PresentMonPath or install the signed portable release under '$toolRoot'."
}

function Test-PresentMon {
    param([Parameter(Mandatory = $true)][string]$Path)

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid) {
        throw "PresentMon must have a valid Authenticode signature; '$Path' reports $($signature.Status)."
    }

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Path
    $startInfo.Arguments = '--help'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    try {
        $standardOutputTask = $process.StandardOutput.ReadToEndAsync()
        $standardErrorTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $standardOutput = $standardOutputTask.GetAwaiter().GetResult()
        $standardError = $standardErrorTask.GetAwaiter().GetResult()
        $helpExitCode = $process.ExitCode
    } finally {
        $process.Dispose()
    }
    $helpOutput = $standardOutput + "`n" + $standardError
    if ($helpExitCode -notin @(0, 1) -or $helpOutput -notmatch '(?m)^PresentMon\s+\d') {
        throw "PresentMon --help failed with exit code $helpExitCode."
    }
    foreach ($requiredOption in @(
        '--process_id',
        '--delay',
        '--timed',
        '--terminate_after_timed',
        '--stop_existing_session',
        '--no_console_stats',
        '--v2_metrics',
        '--output_file'
    )) {
        if ($helpOutput -notmatch [regex]::Escape($requiredOption)) {
            throw "PresentMon does not advertise required option '$requiredOption'."
        }
    }
}

function Resolve-Edge {
    if (-not [string]::IsNullOrWhiteSpace($EdgePath)) {
        if (-not (Test-Path -LiteralPath $EdgePath -PathType Leaf)) {
            throw "Microsoft Edge was not found at '$EdgePath'."
        }
        return [IO.Path]::GetFullPath($EdgePath)
    }

    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\Edge\Application\msedge.exe')
    )
    $resolved = $candidates | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_ -PathType Leaf)
    } | Select-Object -First 1
    if (-not $resolved) {
        throw 'Microsoft Edge was not found. Pass -EdgePath explicitly.'
    }
    return [IO.Path]::GetFullPath($resolved)
}

function Measure-PresentMonCsv {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$ExpectedProcessId,
        [Parameter(Mandatory = $true)][int]$ExpectedSeconds,
        [switch]$AllowMultipleSwapchains
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "PresentMon did not create '$Path'."
    }

    $allRows = @(Import-Csv -LiteralPath $Path)
    if ($allRows.Count -lt 2) {
        throw "PresentMon capture '$Path' contains fewer than two rows."
    }

    $pidRows = @($allRows | Where-Object {
        $processId = Get-RowValue -Row $_ -Names @('ProcessID', 'ProcessId', 'process_id')
        $parsedProcessId = 0
        $null -ne $processId -and [int]::TryParse([string]$processId, [ref]$parsedProcessId) -and
            $parsedProcessId -eq $ExpectedProcessId
    })
    if ($pidRows.Count -eq 0) {
        throw "PresentMon capture '$Path' contains no rows for PID $ExpectedProcessId."
    }
    if ($pidRows.Count -ne $allRows.Count) {
        throw "PresentMon capture '$Path' contains rows from processes other than PID $ExpectedProcessId."
    }

    $applicationRows = @($pidRows | Where-Object {
        $frameType = Get-RowValue -Row $_ -Names @('FrameType', 'frame_type')
        $null -eq $frameType -or [string]::IsNullOrWhiteSpace([string]$frameType) -or $frameType -eq 'Application'
    })
    if ($applicationRows.Count -ge 2) {
        $pidRows = $applicationRows
    }

    $swapchainGroups = @($pidRows | Group-Object -Property {
        [string](Get-RowValue -Row $_ -Names @('SwapChainAddress', 'SwapChain', 'swap_chain_address'))
    } | Sort-Object -Property Count -Descending)
    if ($swapchainGroups.Count -eq 0 -or [string]::IsNullOrWhiteSpace([string]$swapchainGroups[0].Name)) {
        throw "PresentMon capture '$Path' has no usable swapchain identity."
    }
    $ignoredSwapchainRows = 0
    if ($swapchainGroups.Count -gt 1) {
        if (-not $AllowMultipleSwapchains) {
            throw "PresentMon capture '$Path' contains $($swapchainGroups.Count) swapchains; expected one."
        }
        $ignoredSwapchainRows = $pidRows.Count - $swapchainGroups[0].Count
        $pidRows = @($swapchainGroups[0].Group)
    }

    $cpuStart = Get-MetricValues -Rows $pidRows -Names @('CPUStartTime', 'cpu_start_time')
    if ($cpuStart.Count -lt 2) {
        throw "PresentMon capture '$Path' has no usable CPUStartTime column."
    }
    $measuredSeconds = (($cpuStart | Measure-Object -Maximum).Maximum -
        ($cpuStart | Measure-Object -Minimum).Minimum) / 1000.0
    if ($measuredSeconds -lt ($ExpectedSeconds * 0.8)) {
        throw "PresentMon capture '$Path' spans only $([Math]::Round($measuredSeconds, 2)) seconds; expected about $ExpectedSeconds."
    }

    $frameTime = Get-MetricValues -Rows $pidRows -Names @('FrameTime', 'MsBetweenPresents', 'frame_time')
    $gpuTime = Get-MetricValues -Rows $pidRows -Names @('GPUTime', 'MsGPUTime', 'gpu_time')
    $gpuBusy = Get-MetricValues -Rows $pidRows -Names @('GPUBusy', 'MsGPUBusy', 'gpu_busy')
    $gpuWait = Get-MetricValues -Rows $pidRows -Names @('GPUWait', 'MsGPUWait', 'gpu_wait')
    if ($frameTime.Count -lt 2) {
        throw "PresentMon capture '$Path' has no usable FrameTime samples."
    }
    if ($gpuTime.Count -lt [Math]::Max(2, [int][Math]::Floor($pidRows.Count * 0.8))) {
        throw "PresentMon capture '$Path' has GPUTime for only $($gpuTime.Count)/$($pidRows.Count) rows; GPU overhead cannot be reported."
    }

    [pscustomobject]@{
        FrameCount = $pidRows.Count
        MeasuredSeconds = $measuredSeconds
        EffectiveFps = ($pidRows.Count - 1) / $measuredSeconds
        FrameTime = $frameTime
        GpuTime = $gpuTime
        GpuBusy = $gpuBusy
        GpuWait = $gpuWait
        GpuTimeCoverage = $gpuTime.Count / [double]$pidRows.Count
        GpuBusyCoverage = $gpuBusy.Count / [double]$pidRows.Count
        SwapchainCount = $swapchainGroups.Count
        IgnoredSwapchainRows = $ignoredSwapchainRows
        PresentRuntime = (@($pidRows | ForEach-Object {
            Get-RowValue -Row $_ -Names @('PresentRuntime', 'present_runtime')
        } | Sort-Object -Unique) -join ';')
        PresentMode = (@($pidRows | ForEach-Object {
            Get-RowValue -Row $_ -Names @('PresentMode', 'present_mode')
        } | Sort-Object -Unique) -join ';')
        GpuIdentifier = (@($pidRows | ForEach-Object {
            Get-RowValue -Row $_ -Names @('GPUName', 'GPUAdapter', 'GPU', 'gpu')
        } | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) } | Sort-Object -Unique) -join ';')
    }
}

function Invoke-PresentMonCapture {
    param(
        [Parameter(Mandatory = $true)][string]$PresentMon,
        [Parameter(Mandatory = $true)][int]$ProcessId,
        [Parameter(Mandatory = $true)][string]$CsvPath,
        [Parameter(Mandatory = $true)][string]$ConsolePath
    )

    $arguments = @(
        '--process_id', [string]$ProcessId,
        '--delay', [string]$WarmupSeconds,
        '--timed', [string]$CaptureSeconds,
        '--terminate_after_timed',
        '--stop_existing_session',
        '--no_console_stats',
        '--v2_metrics',
        '--output_file', $CsvPath
    )

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $console = (& $PresentMon @arguments 2>&1 | Out-String)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    Write-Utf8NoBom -Path $ConsolePath -Content $console
    if ($exitCode -ne 0) {
        if ($console -match '(?i)failed to start trace session:\s*access denied|Performance Log Users') {
            throw "PresentMon cannot start its ETW session. Run the profiling shell with the required permission or add the user to Performance Log Users, then retry. See '$ConsolePath'."
        }
        throw "PresentMon failed with exit code $exitCode. See '$ConsolePath'."
    }
}

function Test-EngineOutput {
    param(
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath
    )

    $output = ((Get-Content -LiteralPath $StdoutPath -Raw -ErrorAction SilentlyContinue) + "`n" +
        (Get-Content -LiteralPath $StderrPath -Raw -ErrorAction SilentlyContinue))
    if ($output -match '(?i)\[(error|fatal)\]|device[ -]?lost|validation error|out of memory') {
        throw "DayScene reported an engine error. See '$StdoutPath' and '$StderrPath'."
    }
}

function Get-NativeAdapterIdentity {
    param(
        [Parameter(Mandatory = $true)]$Case,
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$EvidenceDirectory
    )

    [void][IO.Directory]::CreateDirectory($EvidenceDirectory)
    $stdoutPath = Join-Path $EvidenceDirectory 'stdout.txt'
    $stderrPath = Join-Path $EvidenceDirectory 'stderr.txt'
    $arguments = @('--compute-selftest', '--api', $Case.Api, '--logLevel', 'info')
    if ($Case.Api -eq 'webgpu') {
        $arguments += @('--shaderFlow', $Case.ShaderFlow)
    }
    $process = Start-Process -FilePath $Executable -WorkingDirectory (Split-Path -Parent $Executable) -PassThru `
        -ArgumentList $arguments -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(120000)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
            throw "Native adapter preflight timed out for '$($Case.Name)'."
        }
        if ($process.ExitCode -ne 0) {
            throw "Native adapter preflight failed for '$($Case.Name)' with exit code $($process.ExitCode)."
        }
    } finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
        }
        $process.Dispose()
    }

    $output = ((Get-Content -LiteralPath $stdoutPath -Raw -ErrorAction SilentlyContinue) + "`n" +
        (Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue))
    $match = [regex]::Match($output, '(?m)\[(?:D3D12|WebGPU)\] adapter LUID=(\d+)')
    if (-not $match.Success -or $match.Groups[1].Value -eq '0') {
        throw "Native adapter preflight did not report a valid LUID for '$($Case.Name)'."
    }
    if ($output -notmatch '\[ComputeSelfTest\] PASS: arithmetic and odd-sized image kernels') {
        throw "Native adapter preflight did not complete the compute oracle for '$($Case.Name)'."
    }
    if ($Case.Api -eq 'webgpu' -and $output -notmatch "startup shaderFlow=$([regex]::Escape($Case.ShaderFlow))") {
        throw "Native adapter preflight did not select shader flow '$($Case.ShaderFlow)'."
    }

    $identity = "dxgi-luid:$($match.Groups[1].Value)"
    [pscustomobject]@{
        Case = $Case.Name
        Api = $Case.Api
        ShaderFlow = $Case.ShaderFlow
        Identity = $identity
        Luid = [uint64]$match.Groups[1].Value
        EvidenceDirectory = $EvidenceDirectory
    }
}

function Get-BrowserAdapterIdentity {
    param($Adapter)

    if ($null -eq $Adapter) {
        return $null
    }
    $parts = @(
        [string]$Adapter.vendor,
        [string]$Adapter.architecture,
        [string]$Adapter.device,
        [string]$Adapter.description,
        [string]$Adapter.isFallbackAdapter
    ) | ForEach-Object { $_.Trim().ToLowerInvariant() }
    if (@($parts | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count -eq 0) {
        return $null
    }
    return 'browser:' + ($parts -join '|')
}

function Get-EdgeRunUrl {
    param([Parameter(Mandatory = $true)][string]$ShaderFlow)

    $builder = [UriBuilder]::new($EdgeUrl)
    $parameters = [ordered]@{
        scene = [string]$Scene
        width = [string]$Width
        height = [string]$Height
        culling = $Culling
        logLevel = 'error'
        postProcessMode = 'compute'
        shaderFlow = $ShaderFlow
    }
    $queryParts = [Collections.Generic.List[string]]::new()
    $existingQuery = $builder.Query.TrimStart('?')
    if (-not [string]::IsNullOrWhiteSpace($existingQuery)) {
        $queryParts.Add($existingQuery)
    }
    foreach ($entry in $parameters.GetEnumerator()) {
        $queryParts.Add(('{0}={1}' -f
            [Uri]::EscapeDataString([string]$entry.Key),
            [Uri]::EscapeDataString([string]$entry.Value)))
    }
    $builder.Query = $queryParts -join '&'
    return $builder.Uri.AbsoluteUri
}

function Invoke-DevToolsExpression {
    param(
        [Parameter(Mandatory = $true)][string]$WebSocketUrl,
        [Parameter(Mandatory = $true)][string]$Expression,
        [switch]$AwaitPromise
    )

    $socket = [Net.WebSockets.ClientWebSocket]::new()
    $cancellation = [Threading.CancellationTokenSource]::new()
    $stream = $null
    $cancellation.CancelAfter(5000)
    try {
        $null = $socket.ConnectAsync([Uri]$WebSocketUrl, $cancellation.Token).GetAwaiter().GetResult()
        $request = @{
            id = 1
            method = 'Runtime.evaluate'
            params = @{
                expression = $Expression
                returnByValue = $true
                awaitPromise = [bool]$AwaitPromise
            }
        } | ConvertTo-Json -Depth 4 -Compress
        $requestBytes = [Text.Encoding]::UTF8.GetBytes($request)
        $null = $socket.SendAsync(
            [ArraySegment[byte]]::new($requestBytes),
            [Net.WebSockets.WebSocketMessageType]::Text,
            $true,
            $cancellation.Token
        ).GetAwaiter().GetResult()

        $buffer = New-Object byte[] 65536
        $stream = [IO.MemoryStream]::new()
        do {
            $received = $socket.ReceiveAsync(
                [ArraySegment[byte]]::new($buffer),
                $cancellation.Token
            ).GetAwaiter().GetResult()
            if ($received.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) {
                throw 'Edge closed the DevTools connection before returning runtime state.'
            }
            $stream.Write($buffer, 0, $received.Count)
        } while (-not $received.EndOfMessage)

        $response = [Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
        if ($response.PSObject.Properties['error']) {
            throw "DevTools evaluation failed: $($response.error.message)"
        }
        if (-not $response.result -or -not $response.result.result -or
            -not $response.result.result.PSObject.Properties['value']) {
            throw 'DevTools evaluation returned no value.'
        }
        return $response.result.result.value
    } finally {
        if ($stream) {
            $stream.Dispose()
        }
        $cancellation.Dispose()
        $socket.Dispose()
    }
}

function Set-EdgeContentSize {
        param([Parameter(Mandatory = $true)][string]$WebSocketUrl)

        $lastState = $null
        for ($attempt = 0; $attempt -lt 8; ++$attempt) {
                $lastState = Invoke-DevToolsExpression -WebSocketUrl $WebSocketUrl -Expression @"
(() => {
    const targetWidth = $Width;
    const targetHeight = $Height;
    const canvas = document.getElementById('canvas');
    const canvasWidth = canvas?.clientWidth ?? 0;
    const canvasHeight = canvas?.clientHeight ?? 0;
    if (canvasWidth !== targetWidth || canvasHeight !== targetHeight) {
        window.resizeBy(targetWidth - canvasWidth, targetHeight - canvasHeight);
    }
    return { innerWidth, innerHeight, outerWidth, outerHeight, canvasWidth, canvasHeight, devicePixelRatio };
})()
"@
                if ([int]$lastState.canvasWidth -eq $Width -and [int]$lastState.canvasHeight -eq $Height) {
                        return $lastState
                }
                [Threading.Thread]::Sleep(150)
        }

        throw "Edge content area could not be resized to ${Width}x${Height}. Last state: $($lastState | ConvertTo-Json -Compress)"
}

function Get-EdgeAdapterInfo {
        param([Parameter(Mandatory = $true)][string]$WebSocketUrl)

        Invoke-DevToolsExpression -WebSocketUrl $WebSocketUrl -AwaitPromise -Expression @'
(async () => {
    const adapter = await navigator.gpu?.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) return null;
    const info = adapter.info ?? {};
    return {
        vendor: info.vendor ?? null,
        architecture: info.architecture ?? null,
        device: info.device ?? null,
        description: info.description ?? null,
        isFallbackAdapter: adapter.isFallbackAdapter ?? info.isFallbackAdapter ?? null
    };
})()
'@
}

function Wait-EdgeDevToolsTarget {
    param(
        [Parameter(Mandatory = $true)][string]$ProfileDirectory,
        [Parameter(Mandatory = $true)][string]$ExpectedUrl
    )

    $portFile = Join-Path $ProfileDirectory 'DevToolsActivePort'
    $deadline = [DateTime]::UtcNow.AddSeconds($EdgeProcessTimeoutSeconds)
    $lastError = $null
    do {
        try {
            if (Test-Path -LiteralPath $portFile -PathType Leaf) {
                $portLines = @(Get-Content -LiteralPath $portFile)
                $port = 0
                if ($portLines.Count -ge 1 -and [int]::TryParse([string]$portLines[0], [ref]$port) -and $port -gt 0) {
                    $targets = @(Invoke-RestMethod -Uri "http://127.0.0.1:$port/json/list" -TimeoutSec 2)
                    $target = $targets | Where-Object {
                        $_.type -eq 'page' -and $_.url -eq $ExpectedUrl -and
                        -not [string]::IsNullOrWhiteSpace([string]$_.webSocketDebuggerUrl)
                    } | Select-Object -First 1
                    if ($target) {
                        return $target
                    }
                }
            }
        } catch {
            $lastError = $_.Exception.Message
        }
        [Threading.Thread]::Sleep(100)
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "Edge DevTools did not expose the benchmark page within $EdgeProcessTimeoutSeconds seconds. Last error: $lastError"
}

function Get-EdgeRuntimeState {
    param([Parameter(Mandatory = $true)][string]$WebSocketUrl)

    Invoke-DevToolsExpression -WebSocketUrl $WebSocketUrl -Expression @'
(() => {
  const runtime = window.t850;
    const args = globalThis.Module?.arguments ?? [];
    const shaderFlowIndex = args.lastIndexOf('--shaderFlow');
    const postProcessIndex = args.lastIndexOf('--postProcessMode');
  return {
    state: runtime?.state ?? null,
    frames: runtime?.frames ?? 0,
    errors: runtime?.errors ?? [],
        renderSize: runtime?.renderSize ?? null,
        shaderFlow: shaderFlowIndex >= 0 ? args[shaderFlowIndex + 1] : null,
        postProcessMode: postProcessIndex >= 0 ? args[postProcessIndex + 1] : null
  };
})()
'@
}

function Wait-EdgeRuntimeReady {
        param(
                [Parameter(Mandatory = $true)][string]$WebSocketUrl,
                [Parameter(Mandatory = $true)][string]$ExpectedShaderFlow
        )

    $deadline = [DateTime]::UtcNow.AddSeconds($EdgeProcessTimeoutSeconds)
    $lastState = $null
    do {
        try {
            $lastState = Get-EdgeRuntimeState -WebSocketUrl $WebSocketUrl
            if ($lastState.state -eq 'failed') {
                throw "Edge runtime failed: $(@($lastState.errors) -join '; ')"
            }
            $renderSize = @($lastState.renderSize)
            if ($lastState.state -eq 'running' -and [int64]$lastState.frames -ge 30 -and
                $renderSize.Count -eq 2 -and [int]$renderSize[0] -eq $Width -and [int]$renderSize[1] -eq $Height -and
                $lastState.shaderFlow -eq $ExpectedShaderFlow -and $lastState.postProcessMode -eq 'compute') {
                return $lastState
            }
        } catch {
            if ($_.Exception.Message -like 'Edge runtime failed:*') {
                throw
            }
        }
        [Threading.Thread]::Sleep(100)
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "Edge runtime did not reach the requested $ExpectedShaderFlow compute state at ${Width}x${Height} within $EdgeProcessTimeoutSeconds seconds. Last state: $($lastState | ConvertTo-Json -Compress)"
}

function Get-OwnedEdgeProcesses {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][int[]]$BaselineProcessIds,
        [Parameter(Mandatory = $true)][int]$RootProcessId
    )

    $newProcesses = @(Get-CimInstance Win32_Process -Filter "Name = 'msedge.exe'" -ErrorAction SilentlyContinue |
        Where-Object { [int]$_.ProcessId -notin $BaselineProcessIds })
    $ownedIds = [Collections.Generic.HashSet[int]]::new()
    [void]$ownedIds.Add($RootProcessId)
    do {
        $added = $false
        foreach ($process in $newProcesses) {
            if (-not $ownedIds.Contains([int]$process.ProcessId) -and
                $ownedIds.Contains([int]$process.ParentProcessId)) {
                [void]$ownedIds.Add([int]$process.ProcessId)
                $added = $true
            }
        }
    } while ($added)

    @($newProcesses | Where-Object { $ownedIds.Contains([int]$_.ProcessId) })
}

function Wait-EdgeGpuProcess {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][int[]]$BaselineProcessIds,
        [Parameter(Mandatory = $true)][int]$RootProcessId
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($EdgeProcessTimeoutSeconds)
    do {
        $gpuProcesses = @(Get-OwnedEdgeProcesses -BaselineProcessIds $BaselineProcessIds `
            -RootProcessId $RootProcessId | Where-Object {
            $_.CommandLine -match '(?:^|\s)--type=gpu-process(?:\s|$)'
        } | Sort-Object -Property CreationDate -Descending)
        if ($gpuProcesses.Count -gt 0) {
            return $gpuProcesses[0]
        }
        [Threading.Thread]::Sleep(100)
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "Edge did not create a GPU process within $EdgeProcessTimeoutSeconds seconds."
}

function Stop-NewEdgeProcesses {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][int[]]$BaselineProcessIds,
        [Parameter(Mandatory = $true)][int]$RootProcessId
    )

    $owned = @(Get-OwnedEdgeProcesses -BaselineProcessIds $BaselineProcessIds `
        -RootProcessId $RootProcessId | Sort-Object -Property CreationDate -Descending)
    foreach ($process in $owned) {
        Stop-Process -Id ([int]$process.ProcessId) -Force -ErrorAction SilentlyContinue
    }
}

function New-RunResult {
    param(
        [Parameter(Mandatory = $true)]$Case,
        [Parameter(Mandatory = $true)]$PlanEntry,
        [Parameter(Mandatory = $true)]$Capture,
        [Parameter(Mandatory = $true)][int]$ProcessId,
        [Parameter(Mandatory = $true)][string]$RunDirectory,
        [string]$Target
    )

    [pscustomobject]@{
        Case = $Case.Name
        Host = $Case.Host
        Api = $Case.Api
        ShaderPath = $Case.ShaderPath
        ShaderFlow = $Case.ShaderFlow
        Sequence = $PlanEntry.Sequence
        Repetition = $PlanEntry.Repetition
        ProcessId = $ProcessId
        Target = $Target
        FrameCount = $Capture.FrameCount
        MeasuredSeconds = $Capture.MeasuredSeconds
        EffectiveFps = $Capture.EffectiveFps
        FrameTime = $Capture.FrameTime
        GpuTime = $Capture.GpuTime
        GpuBusy = $Capture.GpuBusy
        GpuWait = $Capture.GpuWait
        GpuTimeCoverage = $Capture.GpuTimeCoverage
        GpuBusyCoverage = $Capture.GpuBusyCoverage
        SwapchainCount = $Capture.SwapchainCount
        IgnoredSwapchainRows = $Capture.IgnoredSwapchainRows
        PresentRuntime = $Capture.PresentRuntime
        PresentMode = $Capture.PresentMode
        GpuIdentifier = $Capture.GpuIdentifier
        RunDirectory = $RunDirectory
    }
}

function Invoke-NativeRun {
    param(
        [Parameter(Mandatory = $true)]$Case,
        [Parameter(Mandatory = $true)]$PlanEntry,
        [Parameter(Mandatory = $true)][string]$PresentMon,
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$RunDirectory,
        [Parameter(Mandatory = $true)][string]$AdapterIdentity
    )

    $processName = [IO.Path]::GetFileNameWithoutExtension($Executable)
    $existing = @(Get-Process -Name $processName -ErrorAction SilentlyContinue)
    if ($existing.Count -gt 0) {
        throw "Refusing to profile while $processName is already running (PID(s): $($existing.Id -join ', '))."
    }

    [void][IO.Directory]::CreateDirectory($RunDirectory)
    $stdoutPath = Join-Path $RunDirectory 'dayscene.stdout.txt'
    $stderrPath = Join-Path $RunDirectory 'dayscene.stderr.txt'
    $csvPath = Join-Path $RunDirectory 'presentmon.csv'
    $consolePath = Join-Path $RunDirectory 'presentmon.console.txt'
    $arguments = @(
        '--api', $Case.Api,
        '--scene', [string]$Scene,
        '--width', [string]$Width,
        '--height', [string]$Height,
        '--culling', $Culling,
        '--postProcessMode', 'compute',
        '--logLevel', 'error'
    )
    if ($Case.Api -eq 'webgpu') {
        $arguments += @('--shaderFlow', $Case.ShaderFlow)
    }

    $app = Start-Process -FilePath $Executable -WorkingDirectory (Split-Path -Parent $Executable) -PassThru `
        -ArgumentList $arguments -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $null = $app.Handle
    try {
        Invoke-PresentMonCapture -PresentMon $PresentMon -ProcessId $app.Id -CsvPath $csvPath -ConsolePath $consolePath
        if ($app.HasExited -and $app.ExitCode -ne 0) {
            throw "DayScene exited with code $($app.ExitCode) during capture. See '$RunDirectory'."
        }
    } finally {
        if (-not $app.HasExited) {
            Stop-Process -Id $app.Id -Force
        }
        $app.WaitForExit()
    }

    $processId = $app.Id
    $app.Dispose()
    Test-EngineOutput -StdoutPath $stdoutPath -StderrPath $stderrPath
    $capture = Measure-PresentMonCsv -Path $csvPath -ExpectedProcessId $processId -ExpectedSeconds $CaptureSeconds
    $result = New-RunResult -Case $Case -PlanEntry $PlanEntry -Capture $capture -ProcessId $processId `
        -RunDirectory $RunDirectory -Target ($arguments -join ' ')
    $result.GpuIdentifier = $AdapterIdentity
    return $result
}

function Invoke-EdgeRun {
    param(
        [Parameter(Mandatory = $true)]$Case,
        [Parameter(Mandatory = $true)]$PlanEntry,
        [Parameter(Mandatory = $true)][string]$PresentMon,
        [Parameter(Mandatory = $true)][string]$Edge,
        [Parameter(Mandatory = $true)][string]$RunDirectory
    )

    [void][IO.Directory]::CreateDirectory($RunDirectory)
    $csvPath = Join-Path $RunDirectory 'presentmon.csv'
    $consolePath = Join-Path $RunDirectory 'presentmon.console.txt'
    $profileDirectory = Join-Path $RunDirectory 'edge-profile'
    [void][IO.Directory]::CreateDirectory($profileDirectory)
    $runUrl = Get-EdgeRunUrl -ShaderFlow $Case.ShaderFlow
    $baselineProcessIds = @(
        Get-Process -Name msedge -ErrorAction SilentlyContinue | ForEach-Object { [int]$_.Id }
    )
    $arguments = @(
        "--user-data-dir=`"$profileDirectory`"",
        '--no-first-run',
        '--no-default-browser-check',
        '--disable-background-timer-throttling',
        '--disable-renderer-backgrounding',
        '--disable-backgrounding-occluded-windows',
        '--remote-debugging-port=0',
        "--window-size=$Width,$Height",
        "--app=`"$runUrl`""
    )

    $edgeRoot = Start-Process -FilePath $Edge -ArgumentList $arguments -PassThru
    $null = $edgeRoot.Handle
    try {
        $devToolsTarget = @(Wait-EdgeDevToolsTarget -ProfileDirectory $profileDirectory -ExpectedUrl $runUrl) |
            Where-Object { $_.PSObject.Properties['webSocketDebuggerUrl'] } |
            Select-Object -Last 1
        if (-not $devToolsTarget -or [string]::IsNullOrWhiteSpace([string]$devToolsTarget.webSocketDebuggerUrl)) {
            throw 'Edge DevTools target did not provide a WebSocket URL.'
        }
        $webSocketUrl = [string]$devToolsTarget.webSocketDebuggerUrl
        $contentSize = Set-EdgeContentSize -WebSocketUrl $webSocketUrl
        $beforeState = Wait-EdgeRuntimeReady -WebSocketUrl $webSocketUrl `
            -ExpectedShaderFlow $Case.ShaderFlow
        $browserAdapter = Get-EdgeAdapterInfo -WebSocketUrl $webSocketUrl
        $preflightEvidence = [pscustomobject]@{
            Runtime = $beforeState
            ContentSize = $contentSize
            BrowserAdapter = $browserAdapter
            DevToolsTargetUrl = $devToolsTarget.url
        }
        $preflightEvidence | ConvertTo-Json -Depth 5 | Set-Content `
            -LiteralPath (Join-Path $RunDirectory 'edge-preflight.json') -Encoding UTF8
        $gpuProcess = Wait-EdgeGpuProcess -BaselineProcessIds $baselineProcessIds -RootProcessId $edgeRoot.Id
        Invoke-PresentMonCapture -PresentMon $PresentMon -ProcessId ([int]$gpuProcess.ProcessId) `
            -CsvPath $csvPath -ConsolePath $consolePath
        $capture = Measure-PresentMonCsv -Path $csvPath -ExpectedProcessId ([int]$gpuProcess.ProcessId) `
            -ExpectedSeconds $CaptureSeconds -AllowMultipleSwapchains
        $afterState = Get-EdgeRuntimeState -WebSocketUrl $webSocketUrl
        if ($afterState.state -ne 'running' -or $afterState.shaderFlow -ne $Case.ShaderFlow -or
            $afterState.postProcessMode -ne 'compute' -or @($afterState.errors).Count -ne 0 -or
            [int64]$afterState.frames -le [int64]$beforeState.frames) {
            throw "Edge runtime did not remain healthy during capture: $($afterState | ConvertTo-Json -Compress)"
        }
        $runtimeEvidence = [pscustomobject]@{
            Before = $beforeState
            After = $afterState
            BrowserAdapter = $browserAdapter
            DevToolsTargetUrl = $devToolsTarget.url
        }
        $runtimeEvidence | ConvertTo-Json -Depth 5 | Set-Content `
            -LiteralPath (Join-Path $RunDirectory 'edge-runtime.json') -Encoding UTF8
        $result = New-RunResult -Case $Case -PlanEntry $PlanEntry -Capture $capture `
            -ProcessId ([int]$gpuProcess.ProcessId) -RunDirectory $RunDirectory -Target $runUrl
        $result | Add-Member -NotePropertyName BrowserFramesBefore -NotePropertyValue ([int64]$beforeState.frames)
        $result | Add-Member -NotePropertyName BrowserFramesAfter -NotePropertyValue ([int64]$afterState.frames)
        $result | Add-Member -NotePropertyName BrowserAdapter -NotePropertyValue $browserAdapter
        $result.GpuIdentifier = Get-BrowserAdapterIdentity -Adapter $browserAdapter
        return $result
    } finally {
        Stop-NewEdgeProcesses -BaselineProcessIds $baselineProcessIds -RootProcessId $edgeRoot.Id
        if (-not $edgeRoot.HasExited) {
            Stop-Process -Id $edgeRoot.Id -Force -ErrorAction SilentlyContinue
        }
        $edgeRoot.Dispose()
    }
}

function Get-RunSummaryRows {
    param([Parameter(Mandatory = $true)][object[]]$Runs)

    foreach ($run in $Runs) {
        $frameTime = Get-MetricSummary $run.FrameTime
        $gpuTime = Get-MetricSummary $run.GpuTime
        $gpuBusy = Get-MetricSummary $run.GpuBusy
        [pscustomobject]@{
            Case = $run.Case
            Sequence = $run.Sequence
            Repetition = $run.Repetition
            Host = $run.Host
            Api = $run.Api
            ShaderFlow = $run.ShaderFlow
            ProcessId = $run.ProcessId
            Frames = $run.FrameCount
            MeasuredSeconds = $run.MeasuredSeconds
            EffectiveFps = $run.EffectiveFps
            FrameTimeMeanMs = $frameTime.MeanMs
            FrameTimeP50Ms = $frameTime.P50Ms
            FrameTimeP95Ms = $frameTime.P95Ms
            FrameTimeP99Ms = $frameTime.P99Ms
            GpuTimeMeanMs = $gpuTime.MeanMs
            GpuTimeP50Ms = $gpuTime.P50Ms
            GpuTimeP95Ms = $gpuTime.P95Ms
            GpuTimeP99Ms = $gpuTime.P99Ms
            GpuBusyMeanMs = if ($gpuBusy) { $gpuBusy.MeanMs } else { $null }
            GpuTimeCoverage = $run.GpuTimeCoverage
            GpuBusyCoverage = $run.GpuBusyCoverage
            SwapchainCount = $run.SwapchainCount
            IgnoredSwapchainRows = $run.IgnoredSwapchainRows
            PresentRuntime = $run.PresentRuntime
            PresentMode = $run.PresentMode
            GpuIdentifier = $run.GpuIdentifier
            BrowserAdapterVendor = if ($run.PSObject.Properties['BrowserAdapter']) { $run.BrowserAdapter.vendor } else { $null }
            BrowserAdapterArchitecture = if ($run.PSObject.Properties['BrowserAdapter']) { $run.BrowserAdapter.architecture } else { $null }
            BrowserAdapterDevice = if ($run.PSObject.Properties['BrowserAdapter']) { $run.BrowserAdapter.device } else { $null }
            Target = $run.Target
            RunDirectory = $run.RunDirectory
        }
    }
}

function Get-OverheadComparisons {
    param([Parameter(Mandatory = $true)][hashtable]$SummaryByCase)

    $comparisons = [Collections.Generic.List[object]]::new()
    $specifications = @(
        @('spirv-vs-wgsl-native', 'native-webgpu-spirv', 'native-webgpu-wgsl',
            'Closest measure of generated shader-code cost; both use native Dawn/WebGPU.'),
        @('spirv-vs-wgsl-edge', 'edge-webgpu-spirv', 'edge-webgpu-wgsl',
            'Closest browser measure of generated shader-code cost; both use Edge WebGPU.'),
        @('wgsl-webgpu-vs-native-hlsl', 'native-webgpu-wgsl', 'native-d3d12-hlsl',
            'Includes Dawn/WebGPU backend overhead as well as native shader differences.'),
        @('spirv-webgpu-vs-native-hlsl', 'native-webgpu-spirv', 'native-d3d12-hlsl',
            'Includes Dawn/WebGPU backend overhead and translated shader differences.'),
        @('edge-wgsl-vs-native-hlsl', 'edge-webgpu-wgsl', 'native-d3d12-hlsl',
            'Includes Edge, compositor, WebGPU backend and shader differences.'),
        @('edge-spirv-vs-native-hlsl', 'edge-webgpu-spirv', 'native-d3d12-hlsl',
            'Includes Edge, compositor, WebGPU backend and translated shader differences.'),
        @('edge-host-overhead-wgsl', 'edge-webgpu-wgsl', 'native-webgpu-wgsl',
            'Host-path delta for direct WGSL; browser presentation remains a confounder.'),
        @('edge-host-overhead-spirv', 'edge-webgpu-spirv', 'native-webgpu-spirv',
            'Host-path delta for translated WGSL; browser presentation remains a confounder.')
    )
    foreach ($specification in $specifications) {
        if ($SummaryByCase.ContainsKey($specification[1]) -and $SummaryByCase.ContainsKey($specification[2])) {
            $comparisons.Add((New-OverheadComparison -Name $specification[0] `
                -Candidate $SummaryByCase[$specification[1]] -Baseline $SummaryByCase[$specification[2]] `
                -Interpretation $specification[3]))
        }
    }
    return $comparisons.ToArray()
}

function Format-Metric {
    param($Value)

    if ($null -eq $Value) {
        return '-'
    }
    return ([double]$Value).ToString('0.0000', $invariantCulture)
}

function Write-ComparisonReport {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object[]]$Summaries,
        [Parameter(Mandatory = $true)][object[]]$Comparisons,
        [Parameter(Mandatory = $true)][bool]$MatchedGpuIdentifier
    )

    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('# Shader Flow GPU Performance')
    $lines.Add('')
    $lines.Add("Scene $Scene at ${Width}x${Height}, culling $Culling; $RunsPerCase runs per case, ${WarmupSeconds}s warmup, ${CaptureSeconds}s capture.")
    $lines.Add('Primary overhead metric: PresentMon GPUTime. FrameTime is the complete presented frame interval and is reported separately.')
    $lines.Add("Runtime-owned adapter identity matched across all runs: $MatchedGpuIdentifier. Native identities are exact DXGI LUIDs; browser identities use privacy-limited WebGPU adapter information.")
    $lines.Add('')
    $lines.Add('| Case | Host | Shader path | Runs | Frames | FrameTime mean ms | FrameTime p95 ms | GPUTime mean ms | GPUTime p95 ms | GPUBusy mean ms | Effective FPS |')
    $lines.Add('|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|')
    foreach ($summary in $Summaries) {
        $gpuBusyMean = if ($summary.GpuBusy) { $summary.GpuBusy.MeanMs } else { $null }
        $lines.Add("| $($summary.Case) | $($summary.Host) | $($summary.ShaderPath) | $($summary.Runs) | $($summary.Frames) | $(Format-Metric $summary.FrameTime.MeanMs) | $(Format-Metric $summary.FrameTime.P95Ms) | $(Format-Metric $summary.GpuTime.MeanMs) | $(Format-Metric $summary.GpuTime.P95Ms) | $(Format-Metric $gpuBusyMean) | $(Format-Metric $summary.EffectiveFps) |")
    }
    $lines.Add('')
    $lines.Add('## GPUTime Overhead')
    $lines.Add('')
    $lines.Add('| Comparison | Candidate - baseline ms | GPU overhead % | FrameTime delta ms | Run spread ms | Same GPU | Interpretable |')
    $lines.Add('|---|---:|---:|---:|---:|---|---|')
    foreach ($comparison in $Comparisons) {
        $lines.Add("| $($comparison.Name) | $(Format-Metric $comparison.GpuTimeDeltaMs) | $(Format-Metric $comparison.GpuTimeOverheadPercent) | $(Format-Metric $comparison.FrameTimeDeltaMs) | $(Format-Metric $comparison.ObservedRunSpreadMs) | $($comparison.MatchedGpuIdentifier) | $($comparison.Interpretable) |")
    }
    $lines.Add('')
    $lines.Add('The native SPIR-V-vs-WGSL row is the primary answer to whether translated shader output costs GPU time. Native-HLSL comparisons also include API/backend behavior and cannot isolate shader language by themselves.')
    $lines.Add('Edge comparisons include browser and compositor work. Use them for shipping-path cost, not as a shader-only attribution.')
    $lines.Add('Translation and pipeline creation are startup CPU costs outside the warmed capture interval. This report does not estimate them from GPUTime.')
    $lines.Add('A comparison is interpretable only when both cases have at least three runs, every run has the same populated runtime-owned adapter identity, and the delta exceeds observed run spread. Cross-host browser/native identity is not treated as physical-GPU proof. Confirm visual/work parity before making a backend decision.')
    $lines.Add('')
    $lines.Add('Raw PresentMon CSV files and console logs are under `raw/`; machine-readable summaries are `run-summary.csv`, `overhead.csv`, and `comparison.json`.')
    Write-Utf8NoBom -Path $Path -Content (($lines -join "`n") + "`n")
}

function ConvertTo-HtmlText {
    param($Value)

    if ($null -eq $Value) {
        return ''
    }
    return [Net.WebUtility]::HtmlEncode([string]$Value)
}

function ConvertTo-InvariantNumber {
    param([double]$Value)

    return $Value.ToString('0.###', $invariantCulture)
}

function New-MetricBarChartSvg {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][object[]]$Summaries,
        [ValidateSet('GpuTime', 'FrameTime')][string]$Metric
    )

    $values = @($Summaries | ForEach-Object { [double]$_.PSObject.Properties[$Metric].Value.MeanMs })
    $maximumValue = [double](($values | Measure-Object -Maximum).Maximum)
    $chartMaximum = if ($maximumValue -gt 0.0) { [Math]::Ceiling($maximumValue * 1.2 * 10.0) / 10.0 } else { 1.0 }
    $left = 70.0
    $top = 24.0
    $plotWidth = 870.0
    $plotHeight = 216.0
    $bottom = $top + $plotHeight
    $slotWidth = $plotWidth / [Math]::Max(1, $Summaries.Count)
    $barWidth = [Math]::Min(126.0, $slotWidth * 0.58)
    $colors = @('#16796f', '#b64236', '#2861ae', '#c58b26', '#71558f')
    $lines = [Collections.Generic.List[string]]::new()
    $encodedTitle = ConvertTo-HtmlText $Title

    $lines.Add("<svg viewBox=`"0 0 960 330`" role=`"img`" aria-label=`"$encodedTitle`">")
    $lines.Add("<title>$encodedTitle</title>")
    for ($tick = 0; $tick -le 4; ++$tick) {
        $y = $top + ($plotHeight * $tick / 4.0)
        $tickValue = $chartMaximum * (4 - $tick) / 4.0
        $lines.Add(('<line x1="70" y1="{0}" x2="940" y2="{0}" stroke="#d9dddc"></line>' -f (ConvertTo-InvariantNumber $y)))
        $lines.Add(('<text x="60" y="{0}" text-anchor="end">{1}</text>' -f
            (ConvertTo-InvariantNumber ($y + 4.0)), (ConvertTo-HtmlText ((Format-Metric $tickValue) + ' ms'))))
    }
    for ($index = 0; $index -lt $Summaries.Count; ++$index) {
        $summary = $Summaries[$index]
        $value = $values[$index]
        $height = if ($chartMaximum -gt 0.0) { $plotHeight * $value / $chartMaximum } else { 0.0 }
        $x = $left + ($slotWidth * $index) + (($slotWidth - $barWidth) / 2.0)
        $y = $bottom - $height
        $center = $x + ($barWidth / 2.0)
        $color = $colors[$index % $colors.Count]
        $lines.Add(('<rect x="{0}" y="{1}" width="{2}" height="{3}" fill="{4}"></rect>' -f
            (ConvertTo-InvariantNumber $x), (ConvertTo-InvariantNumber $y),
            (ConvertTo-InvariantNumber $barWidth), (ConvertTo-InvariantNumber $height), $color))
        $lines.Add(('<text x="{0}" y="{1}" text-anchor="middle" class="value">{2} ms</text>' -f
            (ConvertTo-InvariantNumber $center), (ConvertTo-InvariantNumber ([Math]::Max(18.0, $y - 9.0))),
            (ConvertTo-HtmlText (Format-Metric $value))))
        $lines.Add(('<text x="{0}" y="274" text-anchor="middle" class="case-label">{1}</text>' -f
            (ConvertTo-InvariantNumber $center), (ConvertTo-HtmlText $summary.Case)))
        $lines.Add(('<text x="{0}" y="295" text-anchor="middle" class="host-label">{1}</text>' -f
            (ConvertTo-InvariantNumber $center), (ConvertTo-HtmlText $summary.ShaderPath)))
    }
    $lines.Add('</svg>')
    return $lines -join "`n"
}

function Get-ComparisonFinding {
    param(
        [Parameter(Mandatory = $true)]$Comparison,
        [Parameter(Mandatory = $true)][string]$CandidateLabel,
        [Parameter(Mandatory = $true)][string]$BaselineLabel
    )

    $direction = if ($Comparison.GpuTimeDeltaMs -ge 0.0) { 'more' } else { 'less' }
    $status = if ($Comparison.Interpretable) {
        'The delta exceeds observed run spread on a matched GPU.'
    } elseif (-not $Comparison.MatchedGpuIdentifier) {
        'Physical-adapter parity is not established, so this delta is not interpretable.'
    } else {
        'The delta does not exceed observed run-to-run spread.'
    }
    return ('{0} uses <strong>{1} ms ({2}%) {3} GPU time</strong> than {4}. {5}' -f
        (ConvertTo-HtmlText $CandidateLabel),
        (ConvertTo-HtmlText (Format-Metric ([Math]::Abs($Comparison.GpuTimeDeltaMs)))),
        (ConvertTo-HtmlText (Format-Metric ([Math]::Abs($Comparison.GpuTimeOverheadPercent)))),
        $direction,
        (ConvertTo-HtmlText $BaselineLabel),
        (ConvertTo-HtmlText $status))
}

function Write-HtmlComparisonReport {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object[]]$Summaries,
        [Parameter(Mandatory = $true)][object[]]$Comparisons,
        [Parameter(Mandatory = $true)][object[]]$RunRows,
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][bool]$MatchedGpuIdentifier
    )

    $summaryRows = [Collections.Generic.List[string]]::new()
    foreach ($summary in $Summaries) {
        $gpuBusyMean = if ($summary.GpuBusy) { Format-Metric $summary.GpuBusy.MeanMs } else { '-' }
        $summaryRows.Add(('<tr><td>{0}</td><td>{1}</td><td>{2}</td><td>{3}</td><td>{4}</td><td>{5}</td><td>{6}</td><td>{7}</td><td>{8}</td><td>{9}</td></tr>' -f
            (ConvertTo-HtmlText $summary.Case), (ConvertTo-HtmlText $summary.Host),
            (ConvertTo-HtmlText $summary.ShaderPath), $summary.Runs, $summary.Frames,
            (ConvertTo-HtmlText (Format-Metric $summary.FrameTime.MeanMs)),
            (ConvertTo-HtmlText (Format-Metric $summary.FrameTime.P95Ms)),
            (ConvertTo-HtmlText (Format-Metric $summary.GpuTime.MeanMs)),
            (ConvertTo-HtmlText (Format-Metric $summary.GpuTime.P95Ms)),
            (ConvertTo-HtmlText $gpuBusyMean)))
    }

    $comparisonRows = [Collections.Generic.List[string]]::new()
    foreach ($comparison in $Comparisons) {
        $statusClass = if ($comparison.Interpretable) { 'status-pass' } else { 'status-caution' }
        $statusText = if ($comparison.Interpretable) { 'Interpretable' } else { 'Inconclusive' }
        $comparisonRows.Add(('<tr><td>{0}</td><td>{1}</td><td>{2}</td><td>{3}</td><td>{4}</td><td>{5}</td><td><span class="{6}">{7}</span></td><td>{8}</td></tr>' -f
            (ConvertTo-HtmlText $comparison.Name),
            (ConvertTo-HtmlText (Format-Metric $comparison.GpuTimeDeltaMs)),
            (ConvertTo-HtmlText (Format-Metric $comparison.GpuTimeOverheadPercent)),
            (ConvertTo-HtmlText (Format-Metric $comparison.FrameTimeDeltaMs)),
            (ConvertTo-HtmlText (Format-Metric $comparison.ObservedRunSpreadMs)),
            (ConvertTo-HtmlText $comparison.MatchedGpuIdentifier), $statusClass, $statusText,
            (ConvertTo-HtmlText $comparison.Interpretation)))
    }

    $runRows = [Collections.Generic.List[string]]::new()
    foreach ($run in $RunRows) {
        $runNumber = 'run-{0:D2}' -f [int]$run.Repetition
        $rawRoot = 'raw/{0}/{1}' -f $run.Case, $runNumber
        $browserAdapter = if ($run.PSObject.Properties['BrowserAdapterVendor'] -and
            -not [string]::IsNullOrWhiteSpace([string]$run.BrowserAdapterVendor)) {
            " $($run.BrowserAdapterVendor) $($run.BrowserAdapterArchitecture)".Trim()
        } else {
            '-'
        }
        $runRows.Add(('<tr><td>{0}</td><td>{1}</td><td>{2}</td><td>{3}</td><td>{4}</td><td>{5}</td><td>{6}</td><td>{7}</td><td><a href="{8}/presentmon.csv">CSV</a> / <a href="{8}/presentmon.console.txt">log</a></td></tr>' -f
            $run.Sequence, (ConvertTo-HtmlText $run.Case), $run.Repetition, $run.Frames,
            (ConvertTo-HtmlText (Format-Metric $run.FrameTimeMeanMs)),
            (ConvertTo-HtmlText (Format-Metric $run.GpuTimeMeanMs)),
            (ConvertTo-HtmlText (Format-Metric $run.GpuTimeP95Ms)),
            (ConvertTo-HtmlText $browserAdapter), (ConvertTo-HtmlText $rawRoot)))
    }

    $nativeFlow = @($Comparisons | Where-Object Name -eq 'spirv-vs-wgsl-native') | Select-Object -First 1
    $edgeFlow = @($Comparisons | Where-Object Name -eq 'spirv-vs-wgsl-edge') | Select-Object -First 1
    $wgslVsHlsl = @($Comparisons | Where-Object Name -eq 'wgsl-webgpu-vs-native-hlsl') | Select-Object -First 1
    $spirvVsHlsl = @($Comparisons | Where-Object Name -eq 'spirv-webgpu-vs-native-hlsl') | Select-Object -First 1
    $findings = [Collections.Generic.List[string]]::new()
    if ($nativeFlow) { $findings.Add('<p>' + (Get-ComparisonFinding $nativeFlow 'HLSL to SPIR-V to WGSL in native Dawn' 'direct WGSL in native Dawn') + '</p>') }
    if ($edgeFlow) { $findings.Add('<p>' + (Get-ComparisonFinding $edgeFlow 'HLSL to SPIR-V to WGSL in Edge' 'direct WGSL in Edge') + '</p>') }
    if ($wgslVsHlsl) { $findings.Add('<p>' + (Get-ComparisonFinding $wgslVsHlsl 'native WebGPU with WGSL' 'native D3D12 with HLSL') + '</p>') }
    if ($spirvVsHlsl) { $findings.Add('<p>' + (Get-ComparisonFinding $spirvVsHlsl 'native WebGPU with translated WGSL' 'native D3D12 with HLSL') + '</p>') }
    if ($findings.Count -eq 0) { $findings.Add('<p>No requested comparison pair was present in this partial run.</p>') }

    $primaryComparisons = @($Comparisons | Where-Object { $_.Name -in @('spirv-vs-wgsl-native', 'spirv-vs-wgsl-edge') })
    $primaryReady = $primaryComparisons.Count -gt 0 -and
        @($primaryComparisons | Where-Object { -not $_.Interpretable }).Count -eq 0
    $coverageClass = if ($primaryReady) { 'notice good' } else { 'notice' }
    $coverageTitle = if ($primaryReady) { 'Primary comparison coverage: PASS.' } else { 'Primary comparison coverage: INCONCLUSIVE.' }
    $gpuChart = New-MetricBarChartSvg -Title 'Mean GPU time by shader flow and host' -Summaries $Summaries -Metric GpuTime
    $frameChart = New-MetricBarChartSvg -Title 'Mean complete frame time by shader flow and host' -Summaries $Summaries -Metric FrameTime
    $manifestJson = ConvertTo-HtmlText ($Manifest | ConvertTo-Json -Depth 8)
    $generatedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm:ss ''UTC''', $invariantCulture)
    $title = 'Day Scene: shader-flow GPU performance evidence'

    $html = @"
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>$title</title><style>
:root{color:#202927;background:#f5f7f6;font:16px/1.6 'Segoe UI',sans-serif;letter-spacing:0}*{box-sizing:border-box}body{margin:0}header{background:#163d36;color:white;padding:38px max(24px,calc((100vw - 1280px)/2));border-bottom:7px solid #cca24a}h1{font:600 38px/1.2 Georgia,serif;margin:10px 0}h2{font:600 27px/1.3 Georgia,serif;margin:0 0 16px}h3{font-size:19px}header p{color:#cfdfd8;max-width:1050px}.eyebrow{text-transform:uppercase;font-size:13px;font-weight:700;color:#cca24a;margin:0}main{max-width:1280px;margin:auto;padding:0 24px}section{padding:34px 0;border-bottom:1px solid #d5dcd8}.lead{font-size:19px}.notice{padding:12px 18px;border-left:4px solid #b64236;background:#fff1ec}.good{border-color:#16796f;background:#e8f2ed}.charts{display:grid;grid-template-columns:1fr 1fr;gap:24px}.charts>div{min-width:0}svg{width:100%;height:auto;background:white;border:1px solid #d5dcd8}svg text{font:13px 'Segoe UI',sans-serif;fill:#4c5853}svg .value{font-size:16px;fill:#202927}.case-label{font-weight:600}.host-label{font-size:10px}.caption,small{font-size:13px;color:#5c6864}.scroll{overflow-x:auto}.scroll table{min-width:1120px}table{border-collapse:collapse;width:100%;font-size:13px;background:white;margin:18px 0}th,td{padding:10px 12px;border-bottom:1px solid #d5dcd8;text-align:left;vertical-align:top}th{background:#e6ece8;white-space:nowrap}td{overflow-wrap:normal}a{color:#175e9d;text-underline-offset:3px}.status-pass{color:#12645c;font-weight:700}.status-caution{color:#a33a31;font-weight:700}summary{cursor:pointer;font-weight:600}details{margin:16px 0}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px;background:#fff;padding:16px;border:1px solid #d5dcd8}footer{padding:25px 0;font-size:13px;color:#5c6864}@media(max-width:820px){h1{font-size:31px}.charts{grid-template-columns:1fr}main{padding:0 16px}header{padding:28px 18px}.case-label{font-size:10px}.host-label{display:none}}
</style></head><body><header><p class="eyebrow">T850 controlled GPU evidence</p><h1>Shader-flow GPU performance</h1><p>Direct WGSL versus HLSL -&gt; SPIR-V -&gt; WGSL, across native Dawn/WebGPU and Microsoft Edge, with native D3D12 HLSL as the backend baseline.</p><p>Scene $Scene &middot; ${Width}x${Height} &middot; $Culling culling &middot; $RunsPerCase runs per case &middot; generated $generatedUtc</p></header><main>
<section><h2>Findings</h2><p class="lead">Headline overhead uses PresentMon GPUTime. Complete presented FrameTime is shown separately and CPU busy time is not used as GPU cost.</p>$($findings -join "`n")<p class="$coverageClass"><strong>$coverageTitle</strong> A result requires matched adapter evidence, at least three runs per case, and a delta larger than observed run spread.</p><p class="notice"><strong>Interpretation boundary:</strong> Native SPIR-V-versus-WGSL is the closest generated-shader comparison. D3D12 HLSL comparisons also include Dawn/WebGPU backend behavior. Edge additionally includes browser, GPU-process, compositor, and presentation behavior. Offline translation and pipeline creation are startup CPU costs outside the warmed GPU interval.</p></section>
<section><h2>Frame and GPU evidence</h2><div class="charts"><div><h3>Mean GPU time</h3>$gpuChart</div><div><h3>Mean complete frame time</h3>$frameChart</div></div><p class="caption">Case means give every accepted run equal weight. Percentiles below pool accepted frames. GPUTime is queue-span GPU work; GPUBusy is retained as supporting active-work evidence.</p><div class="scroll"><table><thead><tr><th>Case</th><th>Host</th><th>Shader path</th><th>Runs</th><th>Frames</th><th>Frame mean ms</th><th>Frame p95 ms</th><th>GPU mean ms</th><th>GPU p95 ms</th><th>GPUBusy mean ms</th></tr></thead><tbody>$($summaryRows -join "`n")</tbody></table></div></section>
<section><h2>GPU-time overhead</h2><div class="scroll"><table><thead><tr><th>Comparison</th><th>GPU delta ms</th><th>GPU overhead %</th><th>Frame delta ms</th><th>Run spread ms</th><th>Same GPU</th><th>Status</th><th>Scope</th></tr></thead><tbody>$($comparisonRows -join "`n")</tbody></table></div><p class="caption">Delta is candidate minus baseline. Negative values favor the candidate. Cross-host adapter identity may require separate physical-GPU verification when the browser exposes only privacy-limited adapter information.</p></section>
<section><h2>Nsight pass timing</h2><p><strong>Yes.</strong> NVIDIA Nsight Graphics can measure individual native D3D12 draws and dispatches. Use GPU Trace for queue duration and overlap, then Frame Debugger/API Inspector for individual <code>Draw*</code> and <code>Dispatch</code> actions. T850 names D3D12 compute PSOs, so kernels such as <code>CS_GodRays.hlsl Compute PSO</code> can be identified.</p><p>The engine also records authored RenderGraph passes as native GPU-profiler scopes. The D3D12 command list does not currently emit matching external event ranges, so Nsight may show PSO/action names rather than authored pass names. Nsight can diagnose Edge's translated D3D12 stream, but browser and compositor work remain mixed in; do not use replay or range profiling as the headline total-frame baseline.</p></section>
<section><h2>Every retained run</h2><div class="scroll"><table><thead><tr><th>Sequence</th><th>Case</th><th>Repeat</th><th>Frames</th><th>Frame mean ms</th><th>GPU mean ms</th><th>GPU p95 ms</th><th>Browser adapter</th><th>Evidence</th></tr></thead><tbody>$($runRows -join "`n")</tbody></table></div></section>
<section><h2>Method and raw evidence</h2><p>Fresh processes are captured in randomized order after a ${WarmupSeconds}-second warmup for ${CaptureSeconds} seconds. Native captures target the exact DayScene PID. Edge uses an isolated profile, verifies an advancing ${Width}x${Height} canvas and strict shader flow through DevTools, then targets the owned GPU child process. Captures with insufficient GPUTime coverage or invalid swapchain/PID attribution are rejected.</p><p><a href="manifest.json">Manifest</a> / <a href="run-order.csv">Run order</a> / <a href="run-summary.csv">Run summary</a> / <a href="overhead.csv">Overhead table</a> / <a href="comparison.json">Analysis JSON</a> / <a href="Report.md">Markdown report</a> / <a href="raw/">Raw captures</a></p><details><summary>Hardware, software, and run provenance</summary><pre>$manifestJson</pre></details><p class="caption">PresentMon executable and native binary hashes are recorded in the manifest. Browser adapter information is retained per Edge run. Visual/useful-work parity remains a separate acceptance gate.</p></section>
<footer>Generated from retained PresentMon measurements. No production deployment or system power/display setting changes.</footer></main></body></html>
"@
    Write-Utf8NoBom -Path $Path -Content $html
}

function New-RunPlan {
    param(
        [Parameter(Mandatory = $true)][object[]]$SelectedCases,
        [Parameter(Mandatory = $true)][int]$Repetitions,
        [Parameter(Mandatory = $true)][int]$Seed
    )

    $random = [Random]::new($Seed)
    $plan = [Collections.Generic.List[object]]::new()
    for ($repetition = 1; $repetition -le $Repetitions; ++$repetition) {
        $order = [object[]]$SelectedCases.Clone()
        for ($index = $order.Count - 1; $index -gt 0; --$index) {
            $swapIndex = $random.Next($index + 1)
            $temporary = $order[$index]
            $order[$index] = $order[$swapIndex]
            $order[$swapIndex] = $temporary
        }
        foreach ($case in $order) {
            $plan.Add([pscustomobject]@{
                Sequence = $plan.Count + 1
                Repetition = $repetition
                Case = $case.Name
                Host = $case.Host
                Api = $case.Api
                ShaderPath = $case.ShaderPath
                ShaderFlow = $case.ShaderFlow
            })
        }
    }
    return $plan.ToArray()
}

function New-CaseSummary {
    param(
        [Parameter(Mandatory = $true)]$Case,
        [Parameter(Mandatory = $true)][object[]]$Runs
    )

    $frameTimes = @($Runs | ForEach-Object { $_.FrameTime })
    $gpuTimes = @($Runs | ForEach-Object { $_.GpuTime })
    $gpuBusy = @($Runs | ForEach-Object { $_.GpuBusy })
    $runGpuMeans = @($Runs | ForEach-Object { Get-Mean $_.GpuTime })
    $frameTimeSummary = Get-MetricSummary $frameTimes
    $gpuTimeSummary = Get-MetricSummary $gpuTimes
    $gpuBusySummary = Get-MetricSummary $gpuBusy
    $identifiedRuns = @($Runs | Where-Object {
        $property = $_.PSObject.Properties['GpuIdentifier']
        $null -ne $property -and -not [string]::IsNullOrWhiteSpace([string]$property.Value)
    })
    $gpuIdentifiers = @($identifiedRuns | ForEach-Object {
        $_.PSObject.Properties['GpuIdentifier'].Value
    } | Sort-Object -Unique)

    $frameTimeSummary.MeanMs = Get-Mean @($Runs | ForEach-Object { Get-Mean $_.FrameTime })
    $gpuTimeSummary.MeanMs = Get-Mean $runGpuMeans
    if ($gpuBusySummary) {
        $gpuBusySummary.MeanMs = Get-Mean @($Runs | ForEach-Object { Get-Mean $_.GpuBusy })
    }

    [pscustomobject]@{
        Case = $Case.Name
        Host = $Case.Host
        Api = $Case.Api
        ShaderPath = $Case.ShaderPath
        ShaderFlow = $Case.ShaderFlow
        Runs = $Runs.Count
        Frames = ($Runs | Measure-Object -Property FrameCount -Sum).Sum
        EffectiveFps = Get-Mean @($Runs.EffectiveFps)
        FrameTime = $frameTimeSummary
        GpuTime = $gpuTimeSummary
        GpuBusy = $gpuBusySummary
        GpuTimeRunMeanMinMs = ($runGpuMeans | Measure-Object -Minimum).Minimum
        GpuTimeRunMeanMaxMs = ($runGpuMeans | Measure-Object -Maximum).Maximum
        GpuIdentityComplete = $identifiedRuns.Count -eq $Runs.Count
        GpuIdentifiers = $gpuIdentifiers
    }
}

function New-OverheadComparison {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Candidate,
        [Parameter(Mandatory = $true)]$Baseline,
        [Parameter(Mandatory = $true)][string]$Interpretation
    )

    $gpuDelta = $Candidate.GpuTime.MeanMs - $Baseline.GpuTime.MeanMs
    $gpuPercent = if ($Baseline.GpuTime.MeanMs -gt 0.0) {
        100.0 * $gpuDelta / $Baseline.GpuTime.MeanMs
    } else {
        $null
    }
    $frameDelta = $Candidate.FrameTime.MeanMs - $Baseline.FrameTime.MeanMs
    $observedSpread = [Math]::Max(
        $Candidate.GpuTimeRunMeanMaxMs - $Candidate.GpuTimeRunMeanMinMs,
        $Baseline.GpuTimeRunMeanMaxMs - $Baseline.GpuTimeRunMeanMinMs
    )
    $matchedGpuIdentifier = $Candidate.GpuIdentityComplete -and $Baseline.GpuIdentityComplete -and
        $Candidate.GpuIdentifiers.Count -eq 1 -and $Baseline.GpuIdentifiers.Count -eq 1 -and
        $Candidate.GpuIdentifiers[0] -eq $Baseline.GpuIdentifiers[0]
    $aboveObservedSpread = $Candidate.Runs -ge 3 -and $Baseline.Runs -ge 3 -and
        [Math]::Abs($gpuDelta) -gt [Math]::Max(0.01, $observedSpread)

    [pscustomobject]@{
        Name = $Name
        Candidate = $Candidate.Case
        Baseline = $Baseline.Case
        GpuTimeDeltaMs = $gpuDelta
        GpuTimeOverheadPercent = $gpuPercent
        FrameTimeDeltaMs = $frameDelta
        ObservedRunSpreadMs = $observedSpread
        AboveObservedSpread = $aboveObservedSpread
        MatchedGpuIdentifier = $matchedGpuIdentifier
        Interpretable = $matchedGpuIdentifier -and $aboveObservedSpread
        Interpretation = $Interpretation
    }
}

function Invoke-SelfTest {
    $definitions = Get-CaseDefinitions
    if ($definitions.Count -ne 5) {
        throw "Expected five comparison cases, found $($definitions.Count)."
    }

    $plan = New-RunPlan -SelectedCases $definitions -Repetitions 3 -Seed 850
    if ($plan.Count -ne 15) {
        throw "Expected 15 planned runs, found $($plan.Count)."
    }
    foreach ($definition in $definitions) {
        if (@($plan | Where-Object Case -eq $definition.Name).Count -ne 3) {
            throw "Run plan does not contain three repetitions for '$($definition.Name)'."
        }
    }

    $baselineRuns = @(
        [pscustomobject]@{ FrameCount = 3; EffectiveFps = 100.0; FrameTime = [double[]]@(9.0, 10.0, 11.0); GpuTime = [double[]]@(3.0, 4.0, 5.0); GpuBusy = [double[]]@(2.0, 3.0, 4.0); GpuIdentifier = '0' },
        [pscustomobject]@{ FrameCount = 3; EffectiveFps = 100.0; FrameTime = [double[]]@(9.0, 10.0, 11.0); GpuTime = [double[]]@(3.0, 4.0, 5.0); GpuBusy = [double[]]@(2.0, 3.0, 4.0); GpuIdentifier = '0' },
        [pscustomobject]@{ FrameCount = 3; EffectiveFps = 100.0; FrameTime = [double[]]@(9.0, 10.0, 11.0); GpuTime = [double[]]@(3.0, 4.0, 5.0); GpuBusy = [double[]]@(2.0, 3.0, 4.0); GpuIdentifier = '0' }
    )
    $candidateRuns = @(
        [pscustomobject]@{ FrameCount = 3; EffectiveFps = 80.0; FrameTime = [double[]]@(11.0, 12.0, 13.0); GpuTime = [double[]]@(4.0, 5.0, 6.0); GpuBusy = [double[]]@(3.0, 4.0, 5.0); GpuIdentifier = '0' },
        [pscustomobject]@{ FrameCount = 3; EffectiveFps = 80.0; FrameTime = [double[]]@(11.0, 12.0, 13.0); GpuTime = [double[]]@(4.0, 5.0, 6.0); GpuBusy = [double[]]@(3.0, 4.0, 5.0); GpuIdentifier = '0' },
        [pscustomobject]@{ FrameCount = 3; EffectiveFps = 80.0; FrameTime = [double[]]@(11.0, 12.0, 13.0); GpuTime = [double[]]@(4.0, 5.0, 6.0); GpuBusy = [double[]]@(3.0, 4.0, 5.0); GpuIdentifier = '0' }
    )
    $baseline = New-CaseSummary -Case $definitions[0] -Runs $baselineRuns
    $candidate = New-CaseSummary -Case $definitions[1] -Runs $candidateRuns
    $comparison = New-OverheadComparison -Name 'self-test' -Candidate $candidate -Baseline $baseline -Interpretation 'synthetic'

    if ([Math]::Abs($baseline.GpuTime.MeanMs - 4.0) -gt 0.0001) {
        throw 'GPU mean aggregation is incorrect.'
    }
    if ([Math]::Abs($comparison.GpuTimeDeltaMs - 1.0) -gt 0.0001 -or
        [Math]::Abs($comparison.GpuTimeOverheadPercent - 25.0) -gt 0.0001) {
        throw 'GPU overhead calculation is incorrect.'
    }
    if ([Math]::Abs($comparison.FrameTimeDeltaMs - 2.0) -gt 0.0001) {
        throw 'Total frame-time delta calculation is incorrect.'
    }
    if (-not $comparison.MatchedGpuIdentifier -or -not $comparison.Interpretable) {
        throw 'Matched GPU identifiers should permit an above-spread comparison.'
    }

    $mismatchedRuns = @($candidateRuns | ForEach-Object {
        [pscustomobject]@{ FrameCount = $_.FrameCount; EffectiveFps = $_.EffectiveFps; FrameTime = $_.FrameTime;
            GpuTime = $_.GpuTime; GpuBusy = $_.GpuBusy; GpuIdentifier = '1' }
    })
    $mismatchedSummary = New-CaseSummary -Case $definitions[1] -Runs $mismatchedRuns
    $mismatchedComparison = New-OverheadComparison -Name 'mismatched-adapter' `
        -Candidate $mismatchedSummary -Baseline $baseline -Interpretation 'synthetic'
    if ($mismatchedComparison.MatchedGpuIdentifier -or $mismatchedComparison.Interpretable) {
        throw 'Mismatched GPU identifiers must invalidate an overhead comparison.'
    }

    $unequalRuns = @(
        [pscustomobject]@{ FrameCount = 2; EffectiveFps = 100.0; FrameTime = [double[]]@(10.0, 10.0); GpuTime = [double[]]@(2.0, 2.0); GpuBusy = [double[]]@(1.0, 1.0) },
        [pscustomobject]@{ FrameCount = 4; EffectiveFps = 50.0; FrameTime = [double[]]@(20.0, 20.0, 20.0, 20.0); GpuTime = [double[]]@(6.0, 6.0, 6.0, 6.0); GpuBusy = [double[]]@(5.0, 5.0, 5.0, 5.0) }
    )
    $unequalSummary = New-CaseSummary -Case $definitions[0] -Runs $unequalRuns
    if ([Math]::Abs($unequalSummary.FrameTime.MeanMs - 15.0) -gt 0.0001 -or
        [Math]::Abs($unequalSummary.GpuTime.MeanMs - 4.0) -gt 0.0001 -or
        [Math]::Abs($unequalSummary.GpuBusy.MeanMs - 3.0) -gt 0.0001) {
        throw 'Case means must give each run equal weight.'
    }

    $temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) ("t850-gpu-profile-test-" + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($temporaryDirectory)
    try {
        $csvPath = Join-Path $temporaryDirectory 'presentmon.csv'
        $csv = @'
ProcessID,SwapChainAddress,FrameType,CPUStartTime,FrameTime,GPUTime,GPUBusy,GPUWait,PresentRuntime,PresentMode,GPU
42,0x1,Application,0,10,4,3,1,DXGI,Hardware,0
42,0x2,Application,500,30,20,18,2,DXGI,Composed,0
42,0x1,application,1000,11,5,4,1,DXGI,Hardware,0
42,0x1,Application,2000,12,6,5,1,DXGI,Hardware,0
'@
        Write-Utf8NoBom -Path $csvPath -Content $csv
        $capture = Measure-PresentMonCsv -Path $csvPath -ExpectedProcessId 42 -ExpectedSeconds 2 -AllowMultipleSwapchains
        if ($capture.FrameCount -ne 3 -or $capture.SwapchainCount -ne 2 -or
            $capture.IgnoredSwapchainRows -ne 1 -or [Math]::Abs((Get-Mean $capture.GpuTime) - 5.0) -gt 0.0001) {
            throw 'PresentMon CSV parsing is incorrect.'
        }

        $modernCsvPath = Join-Path $temporaryDirectory 'presentmon-modern.csv'
        $modernCsv = @'
ProcessID,SwapChainAddress,CPUStartTime,MsBetweenPresents,MsGPUTime,MsGPUBusy,MsGPUWait,PresentRuntime,PresentMode,GPU
42,0x1,0,10,4,3,1,DXGI,Hardware,0
42,0x1,1000,11,5,4,1,DXGI,Hardware,0
42,0x1,2000,12,6,5,1,DXGI,Hardware,0
'@
        Write-Utf8NoBom -Path $modernCsvPath -Content $modernCsv
        $modernCapture = Measure-PresentMonCsv -Path $modernCsvPath -ExpectedProcessId 42 -ExpectedSeconds 2
        if ($modernCapture.FrameCount -ne 3 -or
            [Math]::Abs((Get-Mean $modernCapture.FrameTime) - 11.0) -gt 0.0001 -or
            [Math]::Abs((Get-Mean $modernCapture.GpuTime) - 5.0) -gt 0.0001 -or
            [Math]::Abs((Get-Mean $modernCapture.GpuBusy) - 4.0) -gt 0.0001) {
            throw 'Current PresentMon v2 CSV parsing is incorrect.'
        }

        $invalidProcessPath = Join-Path $temporaryDirectory 'invalid-process.csv'
        Write-Utf8NoBom -Path $invalidProcessPath -Content ($csv +
            "`ninvalid,0x1,Application,2500,10,4,3,1,DXGI,Hardware,0`n")
        try {
            $null = Measure-PresentMonCsv -Path $invalidProcessPath -ExpectedProcessId 42 -ExpectedSeconds 2 -AllowMultipleSwapchains
            throw 'PresentMon CSV parsing accepted a malformed process ID.'
        } catch {
            if ($_.Exception.Message -notmatch 'processes other than PID 42') {
                throw
            }
        }

        $missingSwapchainPath = Join-Path $temporaryDirectory 'missing-swapchain.csv'
        $missingSwapchainCsv = @'
ProcessID,SwapChainAddress,FrameType,CPUStartTime,FrameTime,GPUTime,GPUBusy,GPUWait,PresentRuntime,PresentMode,GPU
42,,Application,0,10,4,3,1,DXGI,Hardware,0
42,,Application,1000,10,4,3,1,DXGI,Hardware,0
'@
        Write-Utf8NoBom -Path $missingSwapchainPath -Content $missingSwapchainCsv
        try {
            $null = Measure-PresentMonCsv -Path $missingSwapchainPath -ExpectedProcessId 42 -ExpectedSeconds 1
            throw 'PresentMon CSV parsing accepted rows without a swapchain identity.'
        } catch {
            if ($_.Exception.Message -notmatch 'no usable swapchain identity') {
                throw
            }
        }

        $htmlPath = Join-Path $temporaryDirectory 'Shader-Flow-GPU-Performance-Report.html'
        $htmlRunRows = @([pscustomobject]@{
            Sequence = 1; Case = 'native-d3d12-hlsl'; Repetition = 1; Frames = 3
            FrameTimeMeanMs = 10.0; GpuTimeMeanMs = 4.0; GpuTimeP95Ms = 4.9
        })
        $htmlManifest = [pscustomobject]@{ Schema = 1; Instrument = 'synthetic'; Cases = $definitions }
        Write-HtmlComparisonReport -Path $htmlPath -Summaries @($baseline, $candidate) `
            -Comparisons @($comparison, $mismatchedComparison) -RunRows $htmlRunRows `
            -Manifest $htmlManifest -MatchedGpuIdentifier $false
        $html = Get-Content -LiteralPath $htmlPath -Raw
        if ($html -notmatch '<!doctype html>' -or $html -notmatch '</style></head><body>' -or
            $html -notmatch '</footer></main></body></html>' -or
            @([regex]::Matches($html, '<svg')).Count -ne 2 -or
            @([regex]::Matches($html, '<rect ')).Count -lt 4 -or
            @([regex]::Matches($html, '<table')).Count -lt 3 -or
            $html -notmatch 'Nsight pass timing' -or $html -notmatch 'Hardware, software, and run provenance' -or
            $html -match '<script|<link[^>]+stylesheet') {
            throw 'Self-contained HTML report generation is incorrect.'
        }
    } finally {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }

    $edgeRunUrl = Get-EdgeRunUrl -ShaderFlow 'spirv'
    if ($edgeRunUrl -notmatch 'shaderFlow=spirv' -or $edgeRunUrl -notmatch 'postProcessMode=compute' -or
        $edgeRunUrl -match 'profile|fixedDt|benchmark') {
        throw "Edge measurement URL is invalid: $edgeRunUrl"
    }

    Write-Output 'PASS MeasureShaderFlowGpuPerformance self-test'
}

if ($SelfTest) {
    Invoke-SelfTest
    return
}

$definitionsByName = @{}
foreach ($definition in (Get-CaseDefinitions)) {
    $definitionsByName[$definition.Name] = $definition
}
$selectedCases = @($Cases | ForEach-Object { $definitionsByName[$_] })
if (@($Cases | Sort-Object -Unique).Count -ne $Cases.Count) {
    throw 'Each case may be selected only once.'
}
$runPlan = @(New-RunPlan -SelectedCases $selectedCases -Repetitions $RunsPerCase -Seed $RandomSeed)

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $env:LOCALAPPDATA "T850Profiles\shader-flow-gpu\$(Get-Date -Format yyyyMMdd-HHmmss)"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

if ($GenerateOnly) {
    if (Test-Path -LiteralPath $OutputDirectory) {
        throw "Use a fresh output directory: '$OutputDirectory'."
    }
    [void][IO.Directory]::CreateDirectory($OutputDirectory)
    $manifest = [pscustomobject]@{
        Schema = 1
        Instrument = 'PresentMon v2 GPU metrics'
        Width = $Width
        Height = $Height
        Scene = $Scene
        Culling = $Culling
        WarmupSeconds = $WarmupSeconds
        CaptureSeconds = $CaptureSeconds
        RunsPerCase = $RunsPerCase
        RandomSeed = $RandomSeed
        Cases = $selectedCases
        RunPlan = $runPlan
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding UTF8
    $runPlan | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'run-order.csv') -NoTypeInformation -Encoding UTF8
    Write-Output "Generated shader-flow GPU run plan: $OutputDirectory"
    return
}

$sourceRoot = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $sourceRoot 'bin\x64\Release\DayScene.exe'
}
$ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)

$needsNative = @($selectedCases | Where-Object Host -eq 'native').Count -gt 0
$needsEdge = @($selectedCases | Where-Object Host -eq 'edge').Count -gt 0
if ($needsNative -and -not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
    throw "Build the Release measurement executable first: '$ExecutablePath'."
}

$resolvedPresentMon = Resolve-PresentMon
Test-PresentMon -Path $resolvedPresentMon
$resolvedEdge = $null
if ($needsEdge) {
    $resolvedEdge = Resolve-Edge
    try {
        $response = Invoke-WebRequest -Uri $EdgeUrl -UseBasicParsing -Method Get
        if ($response.StatusCode -ne 200) {
            throw "HTTP status $($response.StatusCode)"
        }
    } catch {
        throw "The Edge runtime URL is unavailable: '$EdgeUrl'. Build the web target and start web/server.mjs. $($_.Exception.Message)"
    }
}

if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Use a fresh output directory: '$OutputDirectory'."
}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$rawDirectory = Join-Path $OutputDirectory 'raw'
[void][IO.Directory]::CreateDirectory($rawDirectory)
$nativeAdapterPreflights = [Collections.Generic.List[object]]::new()
$nativeAdapterIdentityByCase = @{}
if ($needsNative) {
    $preflightRoot = Join-Path $OutputDirectory 'preflight\native-adapters'
    foreach ($case in @($selectedCases | Where-Object Host -eq 'native')) {
        Write-Host "Adapter preflight: $($case.Name)"
        $preflight = Get-NativeAdapterIdentity -Case $case -Executable $ExecutablePath `
            -EvidenceDirectory (Join-Path $preflightRoot $case.Name)
        $nativeAdapterPreflights.Add($preflight)
        $nativeAdapterIdentityByCase[$case.Name] = $preflight.Identity
    }
}

$manifest = [pscustomobject]@{
    Schema = 1
    Instrument = 'PresentMon v2 GPU metrics'
    PresentMonPath = $resolvedPresentMon
    PresentMonVersion = (Get-Item -LiteralPath $resolvedPresentMon).VersionInfo.FileVersion
    PresentMonSha256 = (Get-FileHash -LiteralPath $resolvedPresentMon -Algorithm SHA256).Hash
    ExecutablePath = if ($needsNative) { $ExecutablePath } else { $null }
    ExecutableSha256 = if ($needsNative) { (Get-FileHash -LiteralPath $ExecutablePath -Algorithm SHA256).Hash } else { $null }
    EdgePath = $resolvedEdge
    EdgeVersion = if ($resolvedEdge) { (Get-Item -LiteralPath $resolvedEdge).VersionInfo.FileVersion } else { $null }
    EdgeUrl = if ($needsEdge) { $EdgeUrl } else { $null }
    Width = $Width
    Height = $Height
    Scene = $Scene
    Culling = $Culling
    PostProcessMode = 'compute'
    WarmupSeconds = $WarmupSeconds
    CaptureSeconds = $CaptureSeconds
    RunsPerCase = $RunsPerCase
    RandomSeed = $RandomSeed
    Cases = $selectedCases
    NativeAdapterPreflights = $nativeAdapterPreflights.ToArray()
    RunPlan = $runPlan
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding UTF8
$runPlan | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'run-order.csv') -NoTypeInformation -Encoding UTF8

$results = [Collections.Generic.List[object]]::new()
foreach ($entry in $runPlan) {
    $case = $definitionsByName[$entry.Case]
    $runDirectory = Join-Path $rawDirectory ("{0}\run-{1:D2}" -f $case.Name, $entry.Repetition)
    Write-Host ("[{0}/{1}] {2}, repetition {3}" -f $entry.Sequence, $runPlan.Count, $case.Name, $entry.Repetition)
    if ($case.Host -eq 'native') {
        $result = Invoke-NativeRun -Case $case -PlanEntry $entry -PresentMon $resolvedPresentMon `
            -Executable $ExecutablePath -RunDirectory $runDirectory `
            -AdapterIdentity $nativeAdapterIdentityByCase[$case.Name]
    } else {
        $result = Invoke-EdgeRun -Case $case -PlanEntry $entry -PresentMon $resolvedPresentMon `
            -Edge $resolvedEdge -RunDirectory $runDirectory
    }
    $results.Add($result)
}

$runSummaryRows = @(Get-RunSummaryRows -Runs $results.ToArray())
$runSummaryRows | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'run-summary.csv') -NoTypeInformation -Encoding UTF8

$summaries = [Collections.Generic.List[object]]::new()
$summaryByCase = @{}
foreach ($case in $selectedCases) {
    $caseRuns = @($results | Where-Object Case -eq $case.Name)
    $summary = New-CaseSummary -Case $case -Runs $caseRuns
    $summaries.Add($summary)
    $summaryByCase[$case.Name] = $summary
}
$comparisons = @(Get-OverheadComparisons -SummaryByCase $summaryByCase)
$comparisons | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'overhead.csv') -NoTypeInformation -Encoding UTF8

$identifiedResults = @($results | Where-Object {
    -not [string]::IsNullOrWhiteSpace([string]$_.GpuIdentifier)
})
$gpuIdentifiers = @($identifiedResults | Select-Object -ExpandProperty GpuIdentifier | Where-Object {
    -not [string]::IsNullOrWhiteSpace([string]$_)
} | Sort-Object -Unique)
$matchedGpuIdentifier = $identifiedResults.Count -eq $results.Count -and $gpuIdentifiers.Count -eq 1
$comparisonObject = [pscustomobject]@{
    Schema = 1
    PrimaryOverheadMetric = 'PresentMon GPUTime mean'
    FrameTimeMeaning = 'Complete presented frame interval; not GPU execution time'
    MatchedGpuIdentifier = $matchedGpuIdentifier
    GpuIdentifiers = $gpuIdentifiers
    Summaries = $summaries.ToArray()
    Comparisons = $comparisons
    Runs = $runSummaryRows
    Limitations = @(
        'Native adapter identities are exact DXGI LUIDs; browser adapter keys are privacy-limited and do not prove cross-host physical-GPU parity.',
        'Native D3D12 versus WebGPU includes API/backend behavior, not shader language alone.',
        'Edge rows include browser and compositor work.',
        'Translation and pipeline creation are startup CPU costs outside the warmed GPU capture.',
        'Visual and useful-work parity must be established separately.'
    )
}
$comparisonObject | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'comparison.json') -Encoding UTF8
Write-ComparisonReport -Path (Join-Path $OutputDirectory 'Report.md') -Summaries $summaries.ToArray() `
    -Comparisons $comparisons -MatchedGpuIdentifier $matchedGpuIdentifier
Write-HtmlComparisonReport -Path (Join-Path $OutputDirectory 'Shader-Flow-GPU-Performance-Report.html') `
    -Summaries $summaries.ToArray() -Comparisons $comparisons -RunRows $runSummaryRows `
    -Manifest $manifest -MatchedGpuIdentifier $matchedGpuIdentifier
$readme = @'
Open Shader-Flow-GPU-Performance-Report.html in Edge, Chrome, or Firefox.
No server or internet connection is required.
All charts, tables, findings, and provenance are included in the HTML file.
Raw PresentMon CSV files, logs, and machine-readable summaries remain alongside it.
'@
Write-Utf8NoBom -Path (Join-Path $OutputDirectory 'README.txt') -Content $readme

Write-Output "Shader-flow GPU comparison recorded: $OutputDirectory"