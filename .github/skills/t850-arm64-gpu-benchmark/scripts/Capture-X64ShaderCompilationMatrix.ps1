[CmdletBinding()]
param(
    [string]$RuntimeRoot = "$PSScriptRoot\..\..\..\..\T850\bin\x64\Release",
    [string]$OutputRoot = "$env:LOCALAPPDATA\T850Profiles\shader-compilation-x64-$(Get-Date -Format yyyyMMdd-HHmmss)",
    [ValidateRange(1, 20)][int]$Repetitions = 5,
    [string]$ExpectedExecutableSha256,
    [switch]$AllowDirtySource
)

$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
$RuntimeRoot = [IO.Path]::GetFullPath($RuntimeRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$exe = Join-Path $RuntimeRoot 'DayScene.exe'
$cache = Join-Path $RuntimeRoot 'Shaders\.t8shadercache'
$manifest = Join-Path $RuntimeRoot 'Shaders\shader_permutations.json'

if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'This capture is x64-only.' }
if (!(Test-Path -LiteralPath $exe)) { throw "Missing x64 DayScene executable: $exe" }
if (!(Test-Path -LiteralPath $manifest)) { throw "Missing shader permutation manifest: $manifest" }
if (Test-Path -LiteralPath $OutputRoot) { throw "Use a fresh output root: $OutputRoot" }
if (Get-Process DayScene -ErrorAction SilentlyContinue) { throw 'DayScene is already running.' }
$executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
if ($ExpectedExecutableSha256 -and $executableSha256 -ne $ExpectedExecutableSha256) {
    throw "Executable hash mismatch: expected $ExpectedExecutableSha256, found $executableSha256"
}
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$sourceRevision = $null
$sourceDirty = $null
if (Test-Path -LiteralPath (Join-Path $repositoryRoot '.git')) {
    $sourceRevision = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Could not read source revision.' }
    $sourceDirty = @(& git -C $repositoryRoot status --porcelain --untracked-files=no -- . ':(exclude)T850/Librerias/vcpkg').Count -gt 0
    if ($sourceDirty -and !$AllowDirtySource) {
        throw 'Source tree is dirty. Use a clean worktree for accepted evidence or pass -AllowDirtySource for an explicitly diagnostic run.'
    }
}
[void][IO.Directory]::CreateDirectory($OutputRoot)

$cases = @(
    [pscustomobject]@{ Name = 'd3d12'; Api = 'd3d12'; Flow = $null },
    [pscustomobject]@{ Name = 'wgsl'; Api = 'webgpu'; Flow = 'wgsl' },
    [pscustomobject]@{ Name = 'spirv'; Api = 'webgpu'; Flow = 'spirv' }
)

function Quote-Arguments([string[]]$Arguments) {
    return (($Arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' ')
}

function Get-Percentile([double[]]$Values, [double]$Percentile) {
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $rank = ($sorted.Count - 1) * $Percentile
    $lower = [Math]::Floor($rank)
    $upper = [Math]::Ceiling($rank)
    if ($lower -eq $upper) { return [double]$sorted[$lower] }
    return [double]$sorted[$lower] + ($rank - $lower) * ([double]$sorted[$upper] - [double]$sorted[$lower])
}

function Get-Statistics([object[]]$Events) {
    $values = @($Events | ForEach-Object { [double]$_.elapsedMs })
    if ($values.Count -eq 0) { return $null }
    $mean = ($values | Measure-Object -Average).Average
    $sumSquares = 0.0
    foreach ($value in $values) { $sumSquares += ($value - $mean) * ($value - $mean) }
    $maximum = @($Events | Sort-Object elapsedMs -Descending)[0]
    return [pscustomobject]@{
        count = $values.Count
        meanMs = [double]$mean
        medianMs = [double](Get-Percentile $values 0.5)
        p95Ms = [double](Get-Percentile $values 0.95)
        minMs = [double](($values | Measure-Object -Minimum).Minimum)
        maxMs = [double]$maximum.elapsedMs
        standardDeviationMs = [Math]::Sqrt($sumSquares / $values.Count)
        maxShader = $maximum.shader
        maxEntry = $maximum.entry
        maxKey = $maximum.key
        maxPermutation = $maximum.permutation
        maxRun = $maximum.repetition
        maxSequence = $maximum.sequence
        maxWasFirstCompile = $maximum.sequence -eq 1
    }
}

$eventPattern = [regex]::new(
    '\[ShaderCompileProfile\]\s+backend=(?<backend>\S+)\s+flow=(?<flow>\S+)\s+stage=(?<stage>\S+)\s+shader="(?<shader>[^"]*)"\s+entry=(?<entry>\S+)\s+(?<identity>.*?)cache=(?<cache>\S+)\s+(?:(?:prepareMs=(?<prepare>[0-9.]+)\s+moduleMs=(?<module>[0-9.]+)\s+)?)elapsedMs=(?<elapsed>[0-9.]+)')
$events = [Collections.Generic.List[object]]::new()
$runs = [Collections.Generic.List[object]]::new()
$expectedCounts = @{}

$orderPatterns = @(
    @(0, 1, 2), @(2, 1, 0), @(1, 2, 0),
    @(0, 2, 1), @(1, 0, 2), @(2, 0, 1)
)
for ($repetition = 1; $repetition -le $Repetitions; ++$repetition) {
    $pattern = $orderPatterns[($repetition - 1) % $orderPatterns.Count]
    $orderedCases = @($pattern | ForEach-Object { $cases[$_] })
    foreach ($case in $orderedCases) {
        if (Test-Path -LiteralPath $cache) { Remove-Item -LiteralPath $cache -Recurse -Force }
        if (Test-Path -LiteralPath $cache) { throw "Shader cache deletion failed: $cache" }
        $runDirectory = Join-Path $OutputRoot ("raw\{0}-run-{1:D2}" -f $case.Name, $repetition)
        [void][IO.Directory]::CreateDirectory($runDirectory)
        $stdout = Join-Path $runDirectory 'stdout.log'
        $stderr = Join-Path $runDirectory 'stderr.log'
        $arguments = @('--compileShaders', '--api', $case.Api, '--logLevel', 'info')
        if ($case.Flow) { $arguments += @('--shaderFlow', $case.Flow) }

        $stopwatch = [Diagnostics.Stopwatch]::StartNew()
        $process = Start-Process $exe -WorkingDirectory $RuntimeRoot -ArgumentList (Quote-Arguments $arguments) `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
        $null = $process.Handle
        try {
            if (!$process.WaitForExit(900000)) {
                $process.Kill()
                $process.WaitForExit()
                throw "Shader compilation timed out: $($case.Name) repetition $repetition"
            }
            $exitCode = $process.ExitCode
        } finally {
            if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
            $stopwatch.Stop()
        }
        if ($exitCode -ne 0) {
            $excerpt = @(
                Get-Content -LiteralPath $stdout -Tail 20 -ErrorAction SilentlyContinue
                Get-Content -LiteralPath $stderr -Tail 20 -ErrorAction SilentlyContinue
            ) -join "`n"
            throw "Shader compilation failed with exit ${exitCode}: $runDirectory`n$excerpt"
        }

        $text = [IO.File]::ReadAllText($stdout) + "`n" + [IO.File]::ReadAllText($stderr)
        $complete = [regex]::Match($text, '\[ShaderPrecompile\] complete: (\d+) succeeded, (\d+) failed')
        if (!$complete.Success -or [int]$complete.Groups[2].Value -ne 0) {
            throw "Compile-all completion marker failed: $($case.Name) repetition $repetition"
        }
        $matches = @($eventPattern.Matches($text))
        if ($matches.Count -eq 0) { throw "No shader timing events: $($case.Name) repetition $repetition" }

        $sequence = 0
        $parsedEvents = foreach ($match in $matches) {
            ++$sequence
            $identity = $match.Groups['identity'].Value
            $permutation = if ($identity -match 'permutation="([^"]*)"') { $Matches[1] } else { '' }
            $key = if ($identity -match 'key=(\S+)') { $Matches[1] } else { '' }
            [pscustomobject]@{
                flow = $case.Name
                backend = $match.Groups['backend'].Value
                stage = $match.Groups['stage'].Value
                shader = $match.Groups['shader'].Value
                entry = $match.Groups['entry'].Value
                key = $key
                permutation = $permutation
                cache = $match.Groups['cache'].Value
                prepareMs = if ($match.Groups['prepare'].Success) { [double]::Parse($match.Groups['prepare'].Value, $culture) } else { $null }
                moduleMs = if ($match.Groups['module'].Success) { [double]::Parse($match.Groups['module'].Value, $culture) } else { $null }
                elapsedMs = [double]::Parse($match.Groups['elapsed'].Value, $culture)
                repetition = $repetition
                sequence = $sequence
            }
        }
        $cacheHitCount = @($parsedEvents | Where-Object cache -EQ 'hit').Count
        $runEvents = @($parsedEvents | Where-Object cache -EQ 'miss')
        if ($runEvents.Count -eq 0) { throw "Cold run has no cache-miss compiler events: $($case.Name) repetition $repetition" }
        $stageCounts = [ordered]@{}
        foreach ($stage in @('vertex', 'pixel', 'compute')) {
            $stageCounts[$stage] = @($runEvents | Where-Object stage -EQ $stage).Count
        }
        $countSignature = ($stageCounts.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ';'
        if ($expectedCounts.ContainsKey($case.Name) -and $expectedCounts[$case.Name] -ne $countSignature) {
            throw "Stage count changed for $($case.Name): expected $($expectedCounts[$case.Name]), got $countSignature"
        }
        $expectedCounts[$case.Name] = $countSignature
        foreach ($event in $runEvents) { $events.Add($event) }
        $runs.Add([pscustomobject]@{
            flow = $case.Name
            repetition = $repetition
            wallMs = $stopwatch.Elapsed.TotalMilliseconds
            succeeded = [int]$complete.Groups[1].Value
            eventCount = $runEvents.Count
            cacheHitEventCount = $cacheHitCount
            stageCounts = [pscustomobject]$stageCounts
            statistics = Get-Statistics $runEvents
            arguments = $arguments
        })
        Write-Host ("PASS {0} run {1}/{2}: {3} compile events, {4} intra-run hits, {5:N1} ms wall" -f
            $case.Name, $repetition, $Repetitions, $runEvents.Count, $cacheHitCount, $stopwatch.Elapsed.TotalMilliseconds)
    }
}

$summary = foreach ($flow in @('d3d12', 'wgsl', 'spirv')) {
    foreach ($stage in @('vertex', 'pixel', 'compute', 'all')) {
        $selection = @($events | Where-Object { $_.flow -eq $flow -and ($stage -eq 'all' -or $_.stage -eq $stage) })
        [pscustomobject]@{ flow = $flow; stage = $stage; statistics = Get-Statistics $selection }
    }
}

$comparisons = foreach ($stage in @('vertex', 'pixel', 'compute', 'all')) {
    $row = @{}
    foreach ($flow in @('d3d12', 'wgsl', 'spirv')) {
        $row[$flow] = ($summary | Where-Object { $_.flow -eq $flow -and $_.stage -eq $stage }).statistics
    }
    [pscustomobject]@{
        stage = $stage
        d3d12MeanMs = $row.d3d12.meanMs
        wgslMeanMs = $row.wgsl.meanMs
        spirvMeanMs = $row.spirv.meanMs
        wgslVsD3d12Percent = 100.0 * ($row.wgsl.meanMs / $row.d3d12.meanMs - 1.0)
        spirvVsD3d12Percent = 100.0 * ($row.spirv.meanMs / $row.d3d12.meanMs - 1.0)
        spirvVsWgslPercent = 100.0 * ($row.spirv.meanMs / $row.wgsl.meanMs - 1.0)
    }
}

$computeVsPixel = foreach ($flow in @('d3d12', 'wgsl', 'spirv')) {
    $pixel = ($summary | Where-Object { $_.flow -eq $flow -and $_.stage -eq 'pixel' }).statistics
    $compute = ($summary | Where-Object { $_.flow -eq $flow -and $_.stage -eq 'compute' }).statistics
    [pscustomobject]@{
        flow = $flow
        pixelMeanMs = $pixel.meanMs
        computeMeanMs = $compute.meanMs
        computeVsPixelMeanPercent = 100.0 * ($compute.meanMs / $pixel.meanMs - 1.0)
        pixelMedianMs = $pixel.medianMs
        computeMedianMs = $compute.medianMs
        computeVsPixelMedianPercent = 100.0 * ($compute.medianMs / $pixel.medianMs - 1.0)
    }
}

$result = [ordered]@{
    schema = 1
    completedUtc = [DateTime]::UtcNow.ToString('o')
    passed = $true
    methodology = [ordered]@{
        architecture = 'x64'
        configuration = 'Release'
        repetitions = $Repetitions
        cacheState = 'cold before every launch'
        corpus = 'Shaders/shader_permutations.json'
        metric = 'per-stage synchronous CPU duration'
        d3d12Metric = 'DXC compile plus reflection'
        webgpuMetric = 'source translation/preparation/reflection plus CreateShaderModule call'
        excluded = @('graphics pipeline creation', 'compute pipeline creation', 'disk cache writes', 'whole-process startup')
    }
    machine = [ordered]@{
        name = $env:COMPUTERNAME
        processor = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)
        gpu = @((Get-CimInstance Win32_VideoController | ForEach-Object { [pscustomobject]@{ name = $_.Name; driverVersion = $_.DriverVersion } }))
        os = (Get-CimInstance Win32_OperatingSystem).Caption
    }
    source = [ordered]@{ repositoryRoot = $repositoryRoot; revision = $sourceRevision; dirty = $sourceDirty }
    executable = [ordered]@{ path = $exe; sha256 = $executableSha256 }
    manifestSha256 = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash
    runs = $runs
    summary = $summary
    comparisons = $comparisons
    computeVsPixel = $computeVsPixel
    events = $events
}

$jsonPath = Join-Path $OutputRoot 'shader-compilation-x64.json'
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

function ConvertTo-HtmlText([object]$Value) { return [Net.WebUtility]::HtmlEncode([string]$Value) }
function Format-Milliseconds([double]$Value) { return $Value.ToString('0.000', $culture) }
function Format-Percent([double]$Value) { return $Value.ToString('+0.0;-0.0;0.0', $culture) + '%' }
$labels = @{ d3d12 = 'Native D3D12 DXC'; wgsl = 'WebGPU WGSL'; spirv = 'WebGPU HLSL -> SPIR-V -> WGSL' }
$stageLabels = @{ vertex = 'Vertex'; pixel = 'Pixel'; compute = 'Compute'; all = 'All stages' }

$matrixRows = foreach ($stage in @('vertex', 'pixel', 'compute', 'all')) {
    $cells = foreach ($flow in @('d3d12', 'wgsl', 'spirv')) {
        $stats = ($summary | Where-Object { $_.flow -eq $flow -and $_.stage -eq $stage }).statistics
        "<td>$(Format-Milliseconds $stats.meanMs)</td><td>$(Format-Milliseconds $stats.medianMs)</td><td>$(Format-Milliseconds $stats.maxMs)</td>"
    }
    "<tr><th>$(ConvertTo-HtmlText $stageLabels[$stage])</th>$($cells -join '')</tr>"
}
$maximumRows = foreach ($flow in @('d3d12', 'wgsl', 'spirv')) {
    foreach ($stage in @('vertex', 'pixel', 'compute')) {
        $stats = ($summary | Where-Object { $_.flow -eq $flow -and $_.stage -eq $stage }).statistics
        $identity = if ($stats.maxKey) { $stats.maxKey } elseif ($stats.maxPermutation) { $stats.maxPermutation } else { '-' }
        "<tr><td>$(ConvertTo-HtmlText $labels[$flow])</td><td>$(ConvertTo-HtmlText $stageLabels[$stage])</td><td>$($stats.count)</td><td>$(Format-Milliseconds $stats.meanMs)</td><td>$(Format-Milliseconds $stats.medianMs)</td><td>$(Format-Milliseconds $stats.p95Ms)</td><td>$(Format-Milliseconds $stats.maxMs)</td><td><code>$(ConvertTo-HtmlText $stats.maxShader)</code></td><td>$(ConvertTo-HtmlText $stats.maxEntry)</td><td><code>$(ConvertTo-HtmlText $identity)</code></td><td>$($stats.maxRun)</td><td>$(if($stats.maxWasFirstCompile){'yes'}else{'no'})</td></tr>"
    }
}
$comparisonRows = foreach ($row in $comparisons) {
    "<tr><th>$(ConvertTo-HtmlText $stageLabels[$row.stage])</th><td>$(Format-Milliseconds $row.d3d12MeanMs)</td><td>$(Format-Milliseconds $row.wgslMeanMs)</td><td>$(Format-Percent $row.wgslVsD3d12Percent)</td><td>$(Format-Milliseconds $row.spirvMeanMs)</td><td>$(Format-Percent $row.spirvVsD3d12Percent)</td><td>$(Format-Percent $row.spirvVsWgslPercent)</td></tr>"
}
$computeRows = foreach ($row in $computeVsPixel) {
    "<tr><td>$(ConvertTo-HtmlText $labels[$row.flow])</td><td>$(Format-Milliseconds $row.pixelMeanMs)</td><td>$(Format-Milliseconds $row.computeMeanMs)</td><td>$(Format-Percent $row.computeVsPixelMeanPercent)</td><td>$(Format-Milliseconds $row.pixelMedianMs)</td><td>$(Format-Milliseconds $row.computeMedianMs)</td><td>$(Format-Percent $row.computeVsPixelMedianPercent)</td></tr>"
}

$html = @"
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>DayScene x64 Shader Preparation Matrix</title><style>
:root{color-scheme:light;--ink:#1b252d;--muted:#58656e;--line:#cad2d8;--paper:#f4f6f7;--accent:#006c67;--warm:#9b4d16}*{box-sizing:border-box}body{margin:0;background:linear-gradient(135deg,#eef2f3,#fff 45%,#f5eee7);color:var(--ink);font-family:Georgia,'Times New Roman',serif;line-height:1.45}main{max-width:1500px;margin:auto;padding:32px 24px 64px}h1,h2{letter-spacing:0}h1{font-size:clamp(2rem,4vw,4rem);margin:0 0 8px}h2{border-bottom:2px solid var(--accent);padding-bottom:6px;margin-top:36px}.meta{color:var(--muted);max-width:1000px}.band{background:#17333a;color:#fff;padding:18px 22px;margin:24px 0}.scroll{overflow-x:auto;border:1px solid var(--line);background:#fff}table{border-collapse:collapse;width:100%;min-width:900px;font-family:'Segoe UI',sans-serif;font-size:14px}th,td{padding:9px 11px;border-bottom:1px solid var(--line);text-align:right;vertical-align:top}th:first-child,td:first-child,td:nth-last-child(n+4){text-align:left}thead th{background:#e4ecec;color:#183238;position:sticky;top:0}tbody tr:nth-child(even){background:#f8fafb}code{font-family:Consolas,monospace;font-size:12px;overflow-wrap:anywhere;word-break:break-word}.note{border-left:4px solid var(--warm);padding:10px 14px;background:#fff8f1}.small{font-size:13px;color:var(--muted)}@media(max-width:700px){main{padding:20px 12px 48px}h1{font-size:2rem}}
</style></head><body><main><h1>DayScene x64 Shader Preparation Matrix</h1><p class="meta">Cold Release preparation of the recorded shader corpus on $(ConvertTo-HtmlText $env:COMPUTERNAME). $Repetitions independent launches per flow; arithmetic mean, median and maximum are calculated from individual stage events.</p><div class="band"><strong>Metric boundary:</strong> native D3D12 measures DXC compile plus reflection. WebGPU measures source translation/preparation/reflection plus the synchronous <code>CreateShaderModule</code> call. Dawn backend compilation performed during pipeline creation is excluded, so cross-flow percentages compare host-side preparation paths, not compiler speed. Pipeline creation and disk-cache writes are excluded.</div>
<h2>Timing Matrix</h2><div class="scroll"><table><thead><tr><th rowspan="2">Stage</th><th colspan="3">Native D3D12 DXC (ms)</th><th colspan="3">WebGPU WGSL (ms)</th><th colspan="3">WebGPU SPIR-V flow (ms)</th></tr><tr><th>Mean</th><th>Median</th><th>Max</th><th>Mean</th><th>Median</th><th>Max</th><th>Mean</th><th>Median</th><th>Max</th></tr></thead><tbody>$($matrixRows -join '')</tbody></table></div>
<h2>Backend Comparison</h2><div class="scroll"><table><thead><tr><th>Stage</th><th>D3D12 mean</th><th>WGSL mean</th><th>WGSL vs D3D12</th><th>SPIR-V mean</th><th>SPIR-V vs D3D12</th><th>SPIR-V vs WGSL</th></tr></thead><tbody>$($comparisonRows -join '')</tbody></table></div>
<h2>Compute Versus Pixel</h2><div class="scroll"><table><thead><tr><th>Flow</th><th>Pixel mean</th><th>Compute mean</th><th>Compute vs pixel mean</th><th>Pixel median</th><th>Compute median</th><th>Compute vs pixel median</th></tr></thead><tbody>$($computeRows -join '')</tbody></table></div><p class="note">The compute and pixel corpora contain different source programs and different sample counts. These ratios answer whether this recorded corpus took the same time; they do not isolate shader stage as the cause.</p>
<h2>Maximum Shader Detail</h2><div class="scroll"><table><thead><tr><th>Flow</th><th>Stage</th><th>Samples</th><th>Mean</th><th>Median</th><th>P95</th><th>Max</th><th>Maximum shader</th><th>Entry</th><th>Key/permutation</th><th>Run</th><th>First compile</th></tr></thead><tbody>$($maximumRows -join '')</tbody></table></div>
<h2>Reproduction</h2><p class="small">Executable SHA-256: <code>$(ConvertTo-HtmlText $result.executable.sha256)</code><br>Manifest SHA-256: <code>$(ConvertTo-HtmlText $result.manifestSha256)</code><br>Raw events and run metadata: <code>shader-compilation-x64.json</code></p></main></body></html>
"@
$htmlPath = Join-Path $OutputRoot 'DayScene-x64-Shader-Compilation-Report.html'
[IO.File]::WriteAllText($htmlPath, $html, [Text.UTF8Encoding]::new($false))

$markdown = [Collections.Generic.List[string]]::new()
$markdown.Add('# DayScene x64 Shader Preparation Matrix')
$markdown.Add('')
$markdown.Add('| Stage | D3D12 mean | WGSL mean | WGSL vs D3D12 | SPIR-V mean | SPIR-V vs D3D12 | SPIR-V vs WGSL |')
$markdown.Add('|---|---:|---:|---:|---:|---:|---:|')
foreach ($row in $comparisons) {
    $markdown.Add("| $($stageLabels[$row.stage]) | $(Format-Milliseconds $row.d3d12MeanMs) ms | $(Format-Milliseconds $row.wgslMeanMs) ms | $(Format-Percent $row.wgslVsD3d12Percent) | $(Format-Milliseconds $row.spirvMeanMs) ms | $(Format-Percent $row.spirvVsD3d12Percent) | $(Format-Percent $row.spirvVsWgslPercent) |")
}
$markdown.Add('')
$markdown.Add('| Flow | Pixel mean | Compute mean | Compute vs pixel |')
$markdown.Add('|---|---:|---:|---:|')
foreach ($row in $computeVsPixel) {
    $markdown.Add("| $($labels[$row.flow]) | $(Format-Milliseconds $row.pixelMeanMs) ms | $(Format-Milliseconds $row.computeMeanMs) ms | $(Format-Percent $row.computeVsPixelMeanPercent) |")
}
$markdown | Set-Content -LiteralPath (Join-Path $OutputRoot 'summary.md') -Encoding UTF8

Write-Output "PASS: x64 shader compilation matrix at $OutputRoot"
Write-Output $jsonPath
Write-Output $htmlPath