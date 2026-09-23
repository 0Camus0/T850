[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$AnalysisPath,
    [Parameter(Mandatory = $true)][string]$ReportPath,
    [Parameter(Mandatory = $true)][string]$ChartDirectory
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$analysis = Get-Content $AnalysisPath -Raw | ConvertFrom-Json
$ReportPath = [IO.Path]::GetFullPath($ReportPath)
$ChartDirectory = [IO.Path]::GetFullPath($ChartDirectory)
[void][IO.Directory]::CreateDirectory($ChartDirectory)

$labels = [ordered]@{
    'd3d12-raster'='D3D12 Raster'; 'd3d12-compute'='D3D12 Compute'
    'wgsl-raster'='WGSL Raster'; 'wgsl-compute'='WGSL Compute'
    'spirv-raster'='SPIR-V Raster'; 'spirv-compute'='SPIR-V Compute'
}
$colors = [ordered]@{
    'd3d12-raster'='#2F6B8A'; 'd3d12-compute'='#D45D3B'
    'wgsl-raster'='#3A8F67'; 'wgsl-compute'='#B7791F'
    'spirv-raster'='#5C5AA7'; 'spirv-compute'='#B54773'
}

function Html([object]$Value) { return [Net.WebUtility]::HtmlEncode([string]$Value) }
function F([double]$Value, [int]$Digits=2) { return $Value.ToString("F$Digits", [Globalization.CultureInfo]::InvariantCulture) }
function Find-Cell([string]$Name) { return @($analysis.cells | Where-Object cell -EQ $Name)[0] }
function Find-Trace([string]$Name) { return @($analysis.traces | Where-Object cell -EQ $Name)[0] }
function Find-Startup([string]$Name, [string]$State) { return @($analysis.startup | Where-Object { $_.cell -eq $Name -and $_.state -eq $State })[0] }
function Scope-Value($Startup, [string]$Name) {
    $property = $Startup.shaderScopes.PSObject.Properties[$Name]
    if ($property) { return [double]$property.Value }
    return 0.0
}

function New-Canvas([string]$Path, [string]$Title, [int]$Width=1920, [int]$Height=1080) {
    $bitmap = [Drawing.Bitmap]::new($Width, $Height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([Drawing.ColorTranslator]::FromHtml('#F5F7F8'))
    $titleFont = [Drawing.Font]::new('Segoe UI', 30, [Drawing.FontStyle]::Bold)
    $textFont = [Drawing.Font]::new('Segoe UI', 18)
    $smallFont = [Drawing.Font]::new('Segoe UI', 14)
    $graphics.DrawString($Title, $titleFont, [Drawing.Brushes]::Black, 90, 42)
    return [pscustomobject]@{Bitmap=$bitmap;Graphics=$graphics;TitleFont=$titleFont;TextFont=$textFont;SmallFont=$smallFont;Path=$Path;Width=$Width;Height=$Height}
}
function Save-Canvas($Canvas) {
    $Canvas.Bitmap.Save($Canvas.Path, [Drawing.Imaging.ImageFormat]::Png)
    $Canvas.Graphics.Dispose(); $Canvas.Bitmap.Dispose(); $Canvas.TitleFont.Dispose(); $Canvas.TextFont.Dispose(); $Canvas.SmallFont.Dispose()
}

function Draw-Axes($Canvas, [double]$Min, [double]$Max, [string]$Unit, [int]$Left=180, [int]$Top=140, [int]$Right=1820, [int]$Bottom=940) {
    $g=$Canvas.Graphics; $axisPen=[Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml('#68757D'),2)
    $gridPen=[Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml('#D5DDE1'),1)
    $g.DrawLine($axisPen,$Left,$Top,$Left,$Bottom);$g.DrawLine($axisPen,$Left,$Bottom,$Right,$Bottom)
    for($i=0;$i-le 5;$i++){$value=$Min+($Max-$Min)*$i/5;$y=$Bottom-($Bottom-$Top)*$i/5;$g.DrawLine($gridPen,$Left,$y,$Right,$y);$g.DrawString("$(F $value 2) $Unit",$Canvas.SmallFont,[Drawing.Brushes]::DimGray,55,$y-12)}
    $axisPen.Dispose();$gridPen.Dispose()
    return [pscustomobject]@{Left=$Left;Top=$Top;Right=$Right;Bottom=$Bottom;Min=$Min;Max=$Max}
}

# Chart 1: five-repeat narrow windows.
$chart1=New-Canvas (Join-Path $ChartDirectory 'gpu-busy-one-second.png') 'Held frame 3000: five independent 1.000-second GPU-busy windows'
$axis=Draw-Axes $chart1 14.5 18.0 'ms'
$order=@('d3d12-raster','d3d12-compute','wgsl-raster','wgsl-compute','spirv-raster','spirv-compute')
$groupWidth=($axis.Right-$axis.Left)/$order.Count
for($index=0;$index-lt$order.Count;$index++){$name=$order[$index];$cell=Find-Cell $name;$x=$axis.Left+$index*$groupWidth+$groupWidth*0.22;$barWidth=$groupWidth*0.56;$value=[double]$cell.gpuBusyRunMeans.median;$min=[double]$cell.gpuBusyRunMeans.min;$max=[double]$cell.gpuBusyRunMeans.max;$height=($value-$axis.Min)/($axis.Max-$axis.Min)*($axis.Bottom-$axis.Top);$y=$axis.Bottom-$height;$brush=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($colors[$name]));$chart1.Graphics.FillRectangle($brush,$x,$y,$barWidth,$height);$brush.Dispose();$center=$x+$barWidth/2;$minY=$axis.Bottom-($min-$axis.Min)/($axis.Max-$axis.Min)*($axis.Bottom-$axis.Top);$maxY=$axis.Bottom-($max-$axis.Min)/($axis.Max-$axis.Min)*($axis.Bottom-$axis.Top);$pen=[Drawing.Pen]::new([Drawing.Color]::Black,3);$chart1.Graphics.DrawLine($pen,$center,$minY,$center,$maxY);$chart1.Graphics.DrawLine($pen,$center-10,$minY,$center+10,$minY);$chart1.Graphics.DrawLine($pen,$center-10,$maxY,$center+10,$maxY);$pen.Dispose();$chart1.Graphics.DrawString((F $value 3),$chart1.TextFont,[Drawing.Brushes]::Black,$x,$y-35);$chart1.Graphics.DrawString($labels[$name],$chart1.SmallFont,[Drawing.Brushes]::Black,$x-25,$axis.Bottom+18)}
$chart1.Graphics.DrawString('Bars: median of run means. Whiskers: minimum to maximum across five launches. Each accepted QPC window contains 55-60 populated presents.',$chart1.SmallFont,[Drawing.Brushes]::DimGray,180,1000)
Save-Canvas $chart1

# Chart 2: sustained traces paired by source flow.
$chart2=New-Canvas (Join-Path $ChartDirectory 'gpu-busy-sustained.png') 'Held frame 3000: sustained 60-second GPU busy'
$axis=Draw-Axes $chart2 14.5 18.0 'ms'
$order=@('d3d12-raster','d3d12-compute','wgsl-raster','wgsl-compute','spirv-raster','spirv-compute')
for($index=0;$index-lt$order.Count;$index++){$name=$order[$index];$trace=Find-Trace $name;$x=$axis.Left+$index*$groupWidth+$groupWidth*0.22;$barWidth=$groupWidth*0.56;$value=[double]$trace.present.gpuBusy.mean;$height=($value-$axis.Min)/($axis.Max-$axis.Min)*($axis.Bottom-$axis.Top);$y=$axis.Bottom-$height;$brush=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($colors[$name]));$chart2.Graphics.FillRectangle($brush,$x,$y,$barWidth,$height);$brush.Dispose();$chart2.Graphics.DrawString((F $value 3),$chart2.TextFont,[Drawing.Brushes]::Black,$x,$y-35);$chart2.Graphics.DrawString($labels[$name],$chart2.SmallFont,[Drawing.Brushes]::Black,$x-25,$axis.Bottom+18)}
$chart2.Graphics.DrawString('Mean PresentMon GPUBusy across 3,319-3,600 presents per cell. Raw GPU ETLs and per-process engine counters are retained.',$chart2.SmallFont,[Drawing.Brushes]::DimGray,180,1000)
Save-Canvas $chart2

# Chart 3: compile all cold/warm.
$chart3=New-Canvas (Join-Path $ChartDirectory 'compile-all.png') 'Compile-all corpus: cold versus warm shader cache' 1920 1160
$axis=Draw-Axes $chart3 0 45 's' 180 140 1820 900
$compileNames=@('d3d12','wgsl','spirv');$groupWidth=($axis.Right-$axis.Left)/$compileNames.Count
for($index=0;$index-lt$compileNames.Count;$index++){$name=$compileNames[$index];$entry=@($analysis.compileAll|Where-Object case -EQ $name)[0];foreach($stateIndex in 0,1){$value=if($stateIndex-eq0){[double]$entry.coldMs/1000}else{[double]$entry.warmMs/1000};$x=$axis.Left+$index*$groupWidth+$groupWidth*(0.18+$stateIndex*0.34);$barWidth=$groupWidth*0.26;$height=$value/($axis.Max-$axis.Min)*($axis.Bottom-$axis.Top);$y=$axis.Bottom-$height;$color=if($stateIndex-eq0){'#C54E3D'}else{'#3A7D72'};$stateLabel=if($stateIndex-eq0){'Cold'}else{'Warm'};$brush=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($color));$chart3.Graphics.FillRectangle($brush,$x,$y,$barWidth,$height);$brush.Dispose();$chart3.Graphics.DrawString((F $value 2),$chart3.TextFont,[Drawing.Brushes]::Black,$x,$y-35);$chart3.Graphics.DrawString($stateLabel,$chart3.SmallFont,[Drawing.Brushes]::Black,$x,$axis.Bottom+18)};$chart3.Graphics.DrawString($name.ToUpperInvariant(),$chart3.TextFont,[Drawing.Brushes]::Black,$axis.Left+$index*$groupWidth+$groupWidth*0.4,$axis.Bottom+58)}
$chart3.Graphics.DrawString('291 recorded graphics and compute permutations. Cold removes the selected API cache; warm immediately repeats without clearing it.',$chart3.SmallFont,[Drawing.Brushes]::DimGray,180,1080)
Save-Canvas $chart3

# Chart 4: scene startup cold/warm.
$chart4=New-Canvas (Join-Path $ChartDirectory 'scene-startup.png') 'DayScene first runtime frame: cold versus warm shader cache'
$axis=Draw-Axes $chart4 0 12 's'
$groupWidth=($axis.Right-$axis.Left)/$order.Count
for($index=0;$index-lt$order.Count;$index++){$name=$order[$index];$cold=Find-Startup $name 'cold';$warm=Find-Startup $name 'warm';foreach($stateIndex in 0,1){$value=if($stateIndex-eq0){[double]$cold.firstRuntimeFrameCompleteMs/1000}else{[double]$warm.firstRuntimeFrameCompleteMs/1000};$x=$axis.Left+$index*$groupWidth+$groupWidth*(0.14+$stateIndex*0.36);$barWidth=$groupWidth*0.28;$height=$value/($axis.Max-$axis.Min)*($axis.Bottom-$axis.Top);$y=$axis.Bottom-$height;$color=if($stateIndex-eq0){'#C54E3D'}else{'#3A7D72'};$brush=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($color));$chart4.Graphics.FillRectangle($brush,$x,$y,$barWidth,$height);$brush.Dispose();$chart4.Graphics.DrawString((F $value 1),$chart4.SmallFont,[Drawing.Brushes]::Black,$x,$y-27)};$chart4.Graphics.DrawString($labels[$name],$chart4.SmallFont,[Drawing.Brushes]::Black,$axis.Left+$index*$groupWidth+5,$axis.Bottom+18)}
$chart4.Graphics.DrawString('First-runtime-frame completion includes asset load, shader preparation/module creation, and graphics/compute pipeline creation.',$chart4.SmallFont,[Drawing.Brushes]::DimGray,180,1000)
Save-Canvas $chart4

$cellRows = foreach($name in $order){$cell=Find-Cell $name;$trace=Find-Trace $name;$engine=@($trace.gpuEngines|Where-Object engineType -EQ '3d')[0];"<tr><td>$(Html $labels[$name])</td><td>$(F $cell.gpuBusyRunMeans.median 3)</td><td>$(F $cell.gpuBusyRunMeans.min 3)-$(F $cell.gpuBusyRunMeans.max 3)</td><td>$(F $cell.gpuBusyRunMeans.coefficientOfVariationPercent 2)%</td><td>$(F $trace.present.gpuBusy.mean 3)</td><td>$(F $trace.present.gpuBusy.p95 3)</td><td>$(F $trace.present.presentRateHz 2)</td><td>$(F $engine.utilization.mean 2)%</td></tr>"}
$deltaRows=foreach($delta in $analysis.deltas){$class=if($delta.deltaMs-gt0){'slower'}else{'faster'};$msSign=if($delta.deltaMs-ge0){'+'}else{''};$percentSign=if($delta.deltaPercent-ge0){'+'}else{''};"<tr><td>$(Html $delta.label)</td><td>$(F $delta.referenceMs 3)</td><td>$(F $delta.candidateMs 3)</td><td class='$class'>$msSign$(F $delta.deltaMs 3) ms ($percentSign$(F $delta.deltaPercent 2)%)</td></tr>"}
$compileRows=foreach($entry in $analysis.compileAll){"<tr><td>$(Html $entry.case.ToUpperInvariant())</td><td>$($entry.permutations)</td><td>$(F ($entry.coldMs/1000) 3) s</td><td>$(F ($entry.warmMs/1000) 3) s</td><td>$(F $entry.speedup 2)x</td></tr>"}
$startupRows=foreach($name in $order){$cold=Find-Startup $name 'cold';$warm=Find-Startup $name 'warm';$prepare=if($name-like'd3d12*'){Scope-Value $cold 'shader.compile'}elseif($name-like'spirv*'){Scope-Value $cold 'shader.translate.hlsl_spirv_wgsl'}else{Scope-Value $cold 'shader.load'};"<tr><td>$(Html $labels[$name])</td><td>$(F ($cold.firstRuntimeFrameCompleteMs/1000) 3) s</td><td>$(F ($warm.firstRuntimeFrameCompleteMs/1000) 3) s</td><td>$(F ($prepare/1000) 3) s</td><td>$(F (Scope-Value $cold 'pipeline.create.graphics') 1) ms</td><td>$(F (Scope-Value $cold 'pipeline.create.compute') 1) ms</td></tr>"}

$reportDirectory=Split-Path $ReportPath -Parent
$relativeCharts=[IO.Path]::GetRelativePath($reportDirectory,$ChartDirectory).Replace('\\','/')
$html=@"
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>DayScene ARM64 GPU Performance Report</title><style>
:root{--ink:#172126;--muted:#56656d;--line:#cad4d9;--paper:#f5f7f8;--panel:#fff;--accent:#246b78;--hot:#b7442e;--good:#246b52}*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.55 "Segoe UI",sans-serif;letter-spacing:0}header{background:#16343d;color:#fff;padding:42px max(24px,calc((100% - 1180px)/2)) 36px;border-bottom:5px solid #d48b3d}header h1{font-size:38px;margin:0 0 8px}header p{margin:0;color:#d9e6e9;font-size:18px}.wrap{max-width:1180px;margin:auto;padding:30px 24px 60px}section{margin:0 0 36px}h2{font-size:25px;margin:0 0 14px;border-bottom:2px solid var(--line);padding-bottom:7px}h3{font-size:19px;margin:24px 0 8px}.lede{font-size:19px;max-width:900px}.callouts{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px}.callout{background:var(--panel);border-left:5px solid var(--accent);padding:18px;box-shadow:0 1px 4px #0001}.callout strong{display:block;font-size:24px}.callout span{color:var(--muted)}table{width:100%;border-collapse:collapse;background:var(--panel);font-variant-numeric:tabular-nums}th,td{text-align:right;padding:9px 11px;border-bottom:1px solid #dce3e6}th:first-child,td:first-child{text-align:left}th{background:#e8eef0;color:#26343a}.slower{color:var(--hot);font-weight:650}.faster{color:var(--good);font-weight:650}.chart{display:block;width:100%;height:auto;background:#fff;border:1px solid var(--line);margin:12px 0 26px}.note{background:#e8eef0;border-left:4px solid #627b86;padding:14px 18px}.warn{background:#fff4e8;border-left-color:#c8772c}code{background:#e8eef0;padding:2px 5px;overflow-wrap:anywhere;word-break:break-word}ul{padding-left:22px}@media(max-width:760px){header h1{font-size:30px}.callouts{grid-template-columns:1fr}.wrap{padding:22px 14px}table{font-size:13px}th,td{padding:7px 5px}.scroll{overflow-x:auto}}
</style></head><body><header><h1>DayScene ARM64 GPU Performance</h1><p>Native D3D12 vs Dawn strict WGSL vs HLSL/SPIR-V, raster vs compute post-processing</p></header><main class="wrap">
<section><p class="lede">At a deterministic 1920x1080 frame held after fixed-step runtime frame 3000, shader source flow has no material steady-state GPU effect after compilation. Moving five authored fullscreen post-process passes from pixel shaders to compute is slower on this Qualcomm Adreno X1-85: the sustained penalty is 0.91-1.06 ms of GPU busy time (5.7-6.7%).</p><div class="callouts"><div class="callout"><strong>15.93-16.09 ms</strong><span>Sustained raster GPU busy across all source flows</span></div><div class="callout"><strong>16.99-17.07 ms</strong><span>Sustained compute GPU busy across all source flows</span></div><div class="callout"><strong>5/5 runs per cell</strong><span>Foreground 1920x1080, error-free, populated GPU events</span></div></div></section>
<section><h2>GPU Results</h2><div class="scroll"><table><thead><tr><th>Cell</th><th>1 s median</th><th>Run range</th><th>CV</th><th>60 s mean</th><th>60 s p95</th><th>Present Hz</th><th>3D engine</th></tr></thead><tbody>$($cellRows -join '')</tbody></table></div><img class="chart" src="$relativeCharts/gpu-busy-one-second.png" alt="Five-repeat GPU busy bar chart"><img class="chart" src="$relativeCharts/gpu-busy-sustained.png" alt="Sustained GPU busy bar chart"><div class="scroll"><table><thead><tr><th>Comparison</th><th>Reference</th><th>Candidate</th><th>Delta</th></tr></thead><tbody>$($deltaRows -join '')</tbody></table></div><p class="note">The five-repeat one-second windows are the narrow primary measurement requested. The 60-second runs repeatedly render the same held state and corroborate the stable ordering. Differences between strict WGSL and strict SPIR-V are smaller than run-to-run spread and reverse sign between raster and compute; they are not evidence of a persistent runtime source-language advantage.</p></section>
<section><h2>Raster vs Compute</h2><p>The DayScene graph switches exactly five <code>compute_if_supported</code> passes: God Rays, both God Rays blurs, Bright, and HDR Composition. Geometry and pass count match. Raster records 248 draws, 642,171 indices, and 24 passes; compute records 243 draws, 642,141 indices, and 24 passes because five fullscreen draws become dispatches.</p><p>The process GPU Engine counters attribute work to the unified 3D engine. The separate compute engine reports 0% for every cell, so the driver schedules these D3D12 compute workloads on the 3D engine. Values slightly above 100% are performance-counter sampling artifacts.</p></section>
<section><h2>Shader Compilation</h2><div class="scroll"><table><thead><tr><th>Compiler path</th><th>Permutations</th><th>Cold</th><th>Warm</th><th>Speedup</th></tr></thead><tbody>$($compileRows -join '')</tbody></table></div><img class="chart" src="$relativeCharts/compile-all.png" alt="Compile-all cold and warm chart"><h3>Real Scene Startup</h3><div class="scroll"><table><thead><tr><th>Cell</th><th>Cold first frame</th><th>Warm first frame</th><th>Cold source work</th><th>Graphics PSO</th><th>Compute PSO</th></tr></thead><tbody>$($startupRows -join '')</tbody></table></div><img class="chart" src="$relativeCharts/scene-startup.png" alt="Scene startup cold and warm chart"><p>Native D3D12 has the highest cold compile cost: 41.38 s for all 291 permutations and about 7.2-7.6 s of real-scene shader compilation. Direct WGSL avoids translation but still pays source loading, shader-module creation, and pipeline creation. Strict SPIR-V spends about 0.97-0.98 s translating HLSL through SPIR-V/Tint during cold scene startup; that translation disappears on a warm cache.</p></section>
<section><h2>Method</h2><ul><li>Windows ARM64 Release, DayScene scene 1, 1920x1080 client area, full culling metadata.</li><li><code>--regressionFixedDt 0.0166666667</code> advances deterministically to runtime frame 3000.</li><li><code>--benchmarkHoldFrame 3000</code> sets simulation/physics delta to zero while rendering and presenting continue.</li><li>Each light run settles five seconds after the QPC-stamped hold marker; the accepted half-open window is exactly the following 1.000 s.</li><li>Five independent launches per cell, alternating cell order. Each run contains 55-60 positive PresentMon GPU-busy rows.</li><li>One additional 60-second held run per cell retains PresentMon, WPR GPU ETL, and per-process GPU Engine counters. All six ETLs report zero lost buffers and zero lost events.</li><li>Cold cache removes <code>Shaders/.t8shadercache</code> for the selected API; warm immediately repeats without clearing it.</li></ul></section>
<section><h2>Machine and Tooling</h2><p>Device <strong>WSSICCLROM04</strong>, Qualcomm Adreno X1-85, Windows 11 ARM64, native ARM64 executable SHA-256 <code>$(Html $analysis.capture.executableSha256)</code>. PresentMon uses an unsigned native ARM64 developer build from upstream PR #666, commit <code>503fc8ec5e867f07b94e12a667bc5e5430e79ffc</code>, SHA-256 <code>$(Html $analysis.capture.collectorSha256)</code>. The signed x64 collector previously produced no native ARM64 events.</p></section>
<section><h2>Limits</h2><p class="note warn">PresentMon GPU busy is frame-level attribution, not per-pass timestamp data. WPR GPU traces and process counters identify engine scheduling but do not isolate individual passes. The held frame removes animation and simulation variance; it does not represent every camera position. The result is specific to this Adreno driver and 1080p workload. One-second windows are intentionally narrow, so conclusions use five-run spread and the separate 60-second corroboration.</p><p>Raw accepted evidence: <code>%LOCALAPPDATA%\T850Profiles\arm64-gpu-matrix-20260921</code>. Archive SHA-256: <code>7B3BED6813D4F795BD29B1F1E9D4F8D52A666D09104940A146657E979E4E0EA9</code>.</p></section>
</main></body></html>
"@
[IO.File]::WriteAllText($ReportPath,$html,[Text.UTF8Encoding]::new($false))
Write-Output "ARM64 GPU report written: $ReportPath"
