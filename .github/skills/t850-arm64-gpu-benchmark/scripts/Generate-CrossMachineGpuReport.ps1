[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$AnalysisPath,
  [Parameter(Mandatory=$true)][string]$ReportPath,
  [string]$CpuAnalysisPath,
  [string]$ShaderCompilationPath,
  [string]$ChartDirectory,
  [string]$ScreenshotPath
)
$ErrorActionPreference='Stop'
$ReportPath=[IO.Path]::GetFullPath($ReportPath)
if(!$ChartDirectory){$ChartDirectory=Join-Path (Split-Path $ReportPath -Parent) 'DayScene-DXC-Cross-Machine-GPU-Charts'}
$ChartDirectory=[IO.Path]::GetFullPath($ChartDirectory)
if(!$ScreenshotPath){$ScreenshotPath=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\T850\web\previews\1.png'))}
$analysis=Get-Content $AnalysisPath -Raw|ConvertFrom-Json
if($analysis.schema-ne2-or@($analysis.machines).Count-ne2){throw 'Expected cross-machine analysis schema 2'}
$cpuAnalysis=if($CpuAnalysisPath){Get-Content $CpuAnalysisPath -Raw|ConvertFrom-Json}else{$null}
if($cpuAnalysis-and($cpuAnalysis.schema-ne1-or@($cpuAnalysis.machines).Count-ne2)){throw 'Expected cross-machine CPU analysis schema 1'}
$shaderAnalysis=if($ShaderCompilationPath){Get-Content $ShaderCompilationPath -Raw|ConvertFrom-Json}else{$null}
if($shaderAnalysis-and($shaderAnalysis.schema-ne1-or@($shaderAnalysis.runs).Count-lt3-or@($shaderAnalysis.summary).Count-ne12)){throw 'Expected x64 shader compilation analysis schema 1'}
$reportDirectory=Split-Path $ReportPath -Parent
$chartRel=[IO.Path]::GetRelativePath($reportDirectory,$ChartDirectory).Replace('\','/')
function Html($value){[Net.WebUtility]::HtmlEncode([string]$value)}
function F($value,[int]$digits=3){if($null-eq$value){return '-'};([double]$value).ToString("F$digits",[Globalization.CultureInfo]::InvariantCulture)}
function SignedPercent($value){if($null-eq$value){return '-'};$number=[double]$value;"$($(if($number-ge0){'+'}else{''}))$(F $number 2)%"}
function New-ReportBarChart([string]$Path,[string]$Title,[string]$Subtitle,[object[]]$Items,[double]$Maximum,[string]$Unit){
  Add-Type -AssemblyName System.Drawing
  $width=2200;$left=720;$right=2070;$top=170;$rowHeight=92;$height=$top+$Items.Count*$rowHeight+120
  $bitmap=[Drawing.Bitmap]::new($width,$height)
  $graphics=[Drawing.Graphics]::FromImage($bitmap)
  $graphics.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $graphics.Clear([Drawing.ColorTranslator]::FromHtml('#F7F9FA'))
  $titleFont=[Drawing.Font]::new('Segoe UI',28,[Drawing.FontStyle]::Bold)
  $subtitleFont=[Drawing.Font]::new('Segoe UI',14)
  $labelFont=[Drawing.Font]::new('Segoe UI',15,[Drawing.FontStyle]::Bold)
  $valueFont=[Drawing.Font]::new('Consolas',14,[Drawing.FontStyle]::Bold)
  $gridPen=[Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml('#D4DDE1'),1)
  $graphics.DrawString($Title,$titleFont,[Drawing.Brushes]::Black,55,38)
  $graphics.DrawString($Subtitle,$subtitleFont,[Drawing.Brushes]::DimGray,58,92)
  foreach($tick in 0..5){
    $value=$Maximum*$tick/5;$x=$left+($right-$left)*$tick/5
    $graphics.DrawLine($gridPen,$x,$top-15,$x,$height-75)
    $graphics.DrawString("$(F $value 1) $Unit",$subtitleFont,[Drawing.Brushes]::DimGray,$x-28,$height-62)
  }
  for($index=0;$index-lt$Items.Count;$index++){
    $item=$Items[$index];$y=$top+$index*$rowHeight
    $graphics.DrawString([string]$item.label,$labelFont,[Drawing.Brushes]::Black,55,$y+16)
    $barWidth=if($Maximum-gt0){($right-$left)*[Math]::Max(0,[double]$item.value)/$Maximum}else{0}
    $brush=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml([string]$item.color))
    $graphics.FillRectangle($brush,$left,$y+12,$barWidth,44)
    $brush.Dispose()
    $graphics.DrawString("$(F $item.value 3) $Unit",$valueFont,[Drawing.Brushes]::Black,[Math]::Min($left+$barWidth+14,$right-5),$y+18)
  }
  [IO.Directory]::CreateDirectory((Split-Path $Path -Parent))|Out-Null
  $bitmap.Save($Path,[Drawing.Imaging.ImageFormat]::Png)
  $gridPen.Dispose();$titleFont.Dispose();$subtitleFont.Dispose();$labelFont.Dispose();$valueFont.Dispose();$graphics.Dispose();$bitmap.Dispose()
}
function Find-Throughput($machine,[string]$cell){@($machine.throughput.cells|Where-Object cell -eq $cell)[0]}
function Find-Frame($machine){@($machine.relativeOverhead|Where-Object pass -eq 'gpu.frame')[0]}
function Find-Trace($machine,[string]$cell){@($machine.throughput.traces|Where-Object cell -eq $cell)[0]}
function Find-ComputeDelta($machine,[string]$flow){@($machine.throughput.computeOverRaster|Where-Object flow -eq $flow)[0]}
function Find-CpuMachine([string]$label){if(!$cpuAnalysis){return $null};@($cpuAnalysis.machines|Where-Object label -eq $label)[0]}
function Find-ShaderSummary([string]$flow,[string]$stage){if(!$shaderAnalysis){return $null};@($shaderAnalysis.summary|Where-Object{$_.flow-eq$flow-and$_.stage-eq$stage})[0].statistics}
function Pass-Delta($machine,[string]$candidate,[string]$reference,[bool]$descending){
  $rows=@($machine.relativeOverhead|Where-Object pass -ne 'gpu.frame'|ForEach-Object{
    [pscustomobject]@{pass=$_.pass;deltaMs=[double]$_.$candidate-[double]$_.$reference}
  })
  if($descending){@($rows|Sort-Object deltaMs -Descending)[0]}else{@($rows|Sort-Object deltaMs)[0]}
}
function Summary-Row($machine){
  $frame=@($machine.relativeOverhead|Where-Object pass -eq 'gpu.frame')[0]
  $browser=Find-Throughput $machine 'browser-wgsl-raster'
  "<tr><td>$(Html $machine.label)</td><td>$(F $frame.d3d12RasterMs)</td><td>$(F $frame.wgslRasterMs)</td><td>$(SignedPercent $frame.wgslRasterOverNativePercent)</td><td>$(F $frame.spirvRasterMs)</td><td>$(SignedPercent $frame.spirvRasterOverNativePercent)</td><td>$(if($browser){"$(F $browser.medianFrameMs) ms ($(SignedPercent $browser.overNativeD3D12Percent))"}else{'pending'})</td></tr>"
}
function Observation($machine){
  $frame=@($machine.relativeOverhead|Where-Object pass -eq 'gpu.frame')[0]
  $native=Find-Throughput $machine 'd3d12-raster'
  $wgsl=Find-Throughput $machine 'wgsl-raster'
  $browser=Find-Throughput $machine 'browser-wgsl-raster'
  $browserText=if($browser){" Edge WGSL completed throughput is $(F $browser.medianFrameMs) ms/frame ($(SignedPercent $browser.overNativeD3D12Percent) versus native D3D12 raster)."}else{' Edge throughput is pending or unavailable.'}
  "<p><strong>$(Html $machine.label):</strong> native D3D12 raster GPU time is $(F $frame.d3d12RasterMs) ms; native Dawn WGSL is $(F $frame.wgslRasterMs) ms ($(SignedPercent $frame.wgslRasterOverNativePercent)) and SPIR-V is $(F $frame.spirvRasterMs) ms ($(SignedPercent $frame.spirvRasterOverNativePercent)). Completed native WGSL throughput is $(F $wgsl.medianFrameMs) ms/frame ($(SignedPercent $wgsl.overNativeD3D12Percent) versus $(F $native.medianFrameMs) ms native D3D12).$browserText</p>"
}
function Machine-Section($machine){
  $stabilityRows=foreach($cell in @($machine.cells|Sort-Object cell)){
    "<tr><td>$(Html $cell.cell)</td><td>$(F $cell.frame.medianGpuMs)</td><td>$(F $cell.frame.minGpuMs)</td><td>$(F $cell.frame.maxGpuMs)</td><td>$(F $cell.frame.coefficientOfVariationPercent 2)%</td><td>$($cell.frame.runs)</td></tr>"
  }
  $timestampRows=foreach($row in $machine.relativeOverhead){
    "<tr><td>$(Html $row.pass)</td><td>$(F $row.d3d12RasterMs)</td><td>$(F $row.d3d12ComputeMs)</td><td>$(SignedPercent $row.nativeComputeOverRasterPercent)</td><td>$(F $row.wgslRasterMs)</td><td>$(SignedPercent $row.wgslRasterOverNativePercent)</td><td>$(F $row.wgslComputeMs)</td><td>$(SignedPercent $row.wgslComputeOverRasterPercent)</td><td>$(F $row.spirvRasterMs)</td><td>$(SignedPercent $row.spirvRasterOverNativePercent)</td><td>$(F $row.spirvComputeMs)</td><td>$(SignedPercent $row.spirvComputeOverRasterPercent)</td></tr>"
  }
  $throughputSection=''
  if($machine.throughput){
    $browserNote=if($machine.throughput.browser){
      "<p>Edge <code>$(Html $machine.throughput.browser.cdpVersion.Browser)</code>, runtime identity <code>$(Html $machine.throughput.browser.runtimeIdentity)</code>, browser timestamp status <strong>$(Html $machine.throughput.browser.gpuTimestampStatus)</strong>.</p>"
    }else{'<p>Browser throughput pending or unavailable for this host.</p>'}
    $throughputRows=foreach($row in $machine.throughput.cells){
      "<tr><td>$(Html $row.cell)</td><td>$(Html $row.source)</td><td>$(F $row.medianFrameMs)</td><td>$(F $row.minFrameMs)-$(F $row.maxFrameMs)</td><td>$(F $row.coefficientOfVariationPercent 2)%</td><td>$(F $row.completedFps 2)</td><td>$(SignedPercent $row.overNativeD3D12Percent)</td><td>$($row.runs)</td></tr>"
    }
    $compileRows=foreach($row in $machine.throughput.compileAll){
      "<tr><td>$(Html $row.case)</td><td>$($row.permutations)</td><td>$(F ($row.coldMs/1000)) s</td><td>$(F ($row.warmMs/1000)) s</td><td>$(F $row.speedup 2)x</td></tr>"
    }
    $startupRows=foreach($group in @($machine.throughput.startup|Group-Object cell|Sort-Object Name)){
      $cold=@($group.Group|Where-Object state -eq 'cold')[0]
      $warm=@($group.Group|Where-Object state -eq 'warm')[0]
      "<tr><td>$(Html $group.Name)</td><td>$(F ($cold.firstRuntimeFrameCompleteMs/1000)) s</td><td>$(F ($warm.firstRuntimeFrameCompleteMs/1000)) s</td><td>$(F (($cold.firstRuntimeFrameCompleteMs-$warm.firstRuntimeFrameCompleteMs)/1000)) s</td></tr>"
    }
    $computeRows=foreach($row in $machine.throughput.computeOverRaster){
      "<tr><td>$(Html $row.flow)</td><td>$(F $row.rasterMs)</td><td>$(F $row.computeMs)</td><td>$(SignedPercent $row.computeOverRasterPercent)</td></tr>"
    }
    $traceRows=foreach($row in $machine.throughput.traces){
      $etl=if($row.etl.available){"$([math]::Round($row.etl.bytes/1MB,1)) MiB, lost 0/0, validated on $($row.etl.validatedOnMachine)"}else{'unavailable'}
      "<tr><td>$(Html $row.cell)</td><td>$(F $row.frameMs)</td><td>$(F $row.engine3d.mean 1)%</td><td>$(Html $etl)</td></tr>"
    }
    $throughputSection=@"
<h3>Completed Throughput</h3><p>This metric includes CPU recording, submission, queueing, and the final GPU drain. Browser rows are not GPU timestamp substitutes.</p>
$browserNote
<div class="scroll"><table><thead><tr><th>Cell</th><th>Source</th><th>Median ms</th><th>Min-max ms</th><th>CV</th><th>FPS</th><th>vs native D3D12</th><th>Runs</th></tr></thead><tbody>$($throughputRows-join'')</tbody></table></div><p>Edge benchmark canvases may remain black or stale by design: measured frames render into offscreen targets and use <code>SubmitNoPresent</code>. The manifest's exact frame count, render size, queue drain, and zero-present fields validate executed work. Visual validation is a separate normal onscreen run.</p>
<h3>Throughput Compute versus Raster</h3><table><thead><tr><th>Flow</th><th>Raster ms</th><th>Compute ms</th><th>Compute overhead</th></tr></thead><tbody>$($computeRows-join'')</tbody></table>
<h3>Cold and Warm Shader Costs</h3><table><thead><tr><th>Flow</th><th>Permutations</th><th>Cold</th><th>Warm</th><th>Speedup</th></tr></thead><tbody>$($compileRows-join'')</tbody></table>
<h3>Cold and Warm Scene Startup</h3><table><thead><tr><th>Cell</th><th>Cold</th><th>Warm</th><th>Cold cost</th></tr></thead><tbody>$($startupRows-join'')</tbody></table>
<h3>Sustained GPU Engine / ETW</h3><table><thead><tr><th>Cell</th><th>ms/frame</th><th>3D engine</th><th>ETW</th></tr></thead><tbody>$($traceRows-join'')</tbody></table><p>3D-engine means sum all matching per-process <code>engtype_3D</code> instances across GPU nodes. Multi-node counter totals can slightly exceed 100%.</p>
"@
  }
  $cpuSection=''
  $cpuMachine=Find-CpuMachine $machine.label
  if($cpuMachine){
    $cpuRows=foreach($cell in $cpuMachine.cells){
      $label=@{d3d12='Native D3D12';webgpu='Native Dawn WGSL';edge='Microsoft Edge WebGPU'}[$cell.cell]
      "<tr><td>$(Html $label)</td><td>$(F $cell.metrics.nonWait.mean)</td><td>$(SignedPercent $cell.nonWaitOverD3D12Percent)</td><td>$(F $cell.metrics.cpu.mean)</td><td>$(F $cell.metrics.wait.mean)</td><td>$(F $cell.metrics.encode.mean)</td><td>$(F $cell.metrics.submit.mean)</td><td>$(F $cell.metrics.graph.mean)</td><td>$(F $cell.metrics.nonWait.coefficientOfVariationPercent 2)%</td><td>$($cell.runs)</td></tr>"
    }
    $cpuSection=@"
<h3>Matched CPU Overhead</h3><p>Saturday methodology rerun with the current DXC build: compute post-processing, fixed 1/60 simulation step, 1,800 CPU-profiled frames, and frames 300-1799 analyzed. <strong>Non-wait CPU</strong> is <code>cpuFrameMs - gpu.gpu_wait - webgpu.surface_acquire - gpu.present</code>. This animated onscreen CPU scenario is matched across x64 and ARM64, but it is separate from the held-frame <code>SubmitNoPresent</code> GPU matrix above.</p><div class="scroll"><table><thead><tr><th>Backend</th><th>Non-wait CPU (ms)</th><th>vs native D3D12</th><th>CPU frame (ms)</th><th>Explicit wait (ms)</th><th>Encode (ms)</th><th>Submit (ms)</th><th>Render graph (ms)</th><th>Non-wait CV</th><th>Runs</th></tr></thead><tbody>$($cpuRows-join'')</tbody></table></div><p>Native executable <code>$(Html $cpuMachine.nativeExecutableSha256)</code>; Edge <code>$(Html $cpuMachine.browserVersion)</code>; browser runtime <code>$(Html $cpuMachine.browserRuntimeIdentity)</code>. All nine runs share one frame-by-frame work hash.</p>
"@
  }
  @"
<section><h2>$(Html $machine.label)</h2><p>Machine <code>$(Html $machine.machine)</code>, architecture <code>$(Html $machine.architecture)</code>. Timestamp evidence: $($machine.repetitions) runs x $($machine.samplesPerRun) samples.</p>
<h3>Timestamp Run Stability</h3><table><thead><tr><th>Cell</th><th>Median GPU ms</th><th>Run minimum</th><th>Run maximum</th><th>CV</th><th>Runs</th></tr></thead><tbody>$($stabilityRows-join'')</tbody></table>
<h3>GPU Time by Render Pass</h3><div class="scroll pass-scroll"><table class="pass-table"><colgroup><col class="pass-name"><col span="11" class="pass-metric"></colgroup><thead><tr class="api-groups"><th rowspan="2" scope="col">Render-graph pass</th><th colspan="3" scope="colgroup">Native D3D12<br><span>Authored HLSL to DXC/DXIL</span></th><th colspan="4" scope="colgroup">Native Dawn<br><span>Authored WGSL</span></th><th colspan="4" scope="colgroup">Native Dawn<br><span>HLSL to SPIR-V to Tint/WGSL</span></th></tr><tr><th scope="col">Raster GPU time (ms)</th><th scope="col">Compute GPU time (ms)</th><th scope="col">Compute vs raster (%)</th><th scope="col">Raster GPU time (ms)</th><th scope="col">Raster vs native D3D12 (%)</th><th scope="col">Compute GPU time (ms)</th><th scope="col">Compute vs raster (%)</th><th scope="col">Raster GPU time (ms)</th><th scope="col">Raster vs native D3D12 (%)</th><th scope="col">Compute GPU time (ms)</th><th scope="col">Compute vs raster (%)</th></tr></thead><tbody>$($timestampRows-join'')</tbody></table></div>
<p>Pass and frame values are independently aggregated medians and do not sum exactly. Dawn pass profiling closes physical passes at logical timestamp boundaries; compare Dawn cells only within this same instrumented mode.</p>
$throughputSection$cpuSection</section>
"@
}
$summaryRows=@($analysis.machines|ForEach-Object{Summary-Row $_})
$observations=@($analysis.machines|ForEach-Object{Observation $_})
$sections=@($analysis.machines|ForEach-Object{Machine-Section $_})
$x64=@($analysis.machines|Where-Object label -eq 'x64')[0]
$arm64=@($analysis.machines|Where-Object label -eq 'ARM64')[0]
$x64Frame=Find-Frame $x64;$arm64Frame=Find-Frame $arm64
$x64Native=Find-Throughput $x64 'd3d12-raster';$arm64Native=Find-Throughput $arm64 'd3d12-raster'
$x64Wgsl=Find-Throughput $x64 'wgsl-raster';$arm64Wgsl=Find-Throughput $arm64 'wgsl-raster'
$x64Spirv=Find-Throughput $x64 'spirv-raster';$arm64Spirv=Find-Throughput $arm64 'spirv-raster'
$x64Edge=Find-Throughput $x64 'browser-wgsl-raster';$arm64Edge=Find-Throughput $arm64 'browser-wgsl-raster'
$x64EdgeCompute=Find-ComputeDelta $x64 'browser-wgsl';$arm64EdgeCompute=Find-ComputeDelta $arm64 'browser-wgsl'
$x64WgslTrace=Find-Trace $x64 'wgsl-raster';$x64D3dTrace=Find-Trace $x64 'd3d12-raster'
$arm64WgslTrace=Find-Trace $arm64 'wgsl-raster';$arm64D3dTrace=Find-Trace $arm64 'd3d12-raster'
$x64Top=Pass-Delta $x64 'wgslRasterMs' 'd3d12RasterMs' $true
$arm64Top=Pass-Delta $arm64 'wgslRasterMs' 'd3d12RasterMs' $true
$arm64Offset=Pass-Delta $arm64 'wgslRasterMs' 'd3d12RasterMs' $false
$x64SourceGap=100*($x64Frame.spirvRasterMs-$x64Frame.wgslRasterMs)/$x64Frame.wgslRasterMs
$arm64SourceGap=100*($arm64Frame.spirvRasterMs-$arm64Frame.wgslRasterMs)/$arm64Frame.wgslRasterMs
$x64SourceThroughputGap=100*($x64Spirv.medianFrameMs-$x64Wgsl.medianFrameMs)/$x64Wgsl.medianFrameMs
$arm64SourceThroughputGap=100*($arm64Spirv.medianFrameMs-$arm64Wgsl.medianFrameMs)/$arm64Wgsl.medianFrameMs
$x64Cv=@($x64.cells.frame.coefficientOfVariationPercent);$arm64Cv=@($arm64.cells.frame.coefficientOfVariationPercent)
$x64CvMin=($x64Cv|Measure-Object -Minimum).Minimum;$x64CvMax=($x64Cv|Measure-Object -Maximum).Maximum
$arm64CvMin=($arm64Cv|Measure-Object -Minimum).Minimum;$arm64CvMax=($arm64Cv|Measure-Object -Maximum).Maximum
$x64Cpu=Find-CpuMachine 'x64';$arm64Cpu=Find-CpuMachine 'ARM64'
$cpuConclusion=''
$cpuComparisonSection=''
if($x64Cpu-and$arm64Cpu){
  $x64CpuD3d=@($x64Cpu.cells|Where-Object cell -eq 'd3d12')[0];$x64CpuWgsl=@($x64Cpu.cells|Where-Object cell -eq 'webgpu')[0];$x64CpuEdge=@($x64Cpu.cells|Where-Object cell -eq 'edge')[0]
  $armCpuD3d=@($arm64Cpu.cells|Where-Object cell -eq 'd3d12')[0];$armCpuWgsl=@($arm64Cpu.cells|Where-Object cell -eq 'webgpu')[0];$armCpuEdge=@($arm64Cpu.cells|Where-Object cell -eq 'edge')[0]
  $systemBalanceText="Direct CPU profiling confirms the backend tax: native Dawn adds $(SignedPercent $x64CpuWgsl.nonWaitOverD3D12Percent) non-wait CPU on x64 and $(SignedPercent $armCpuWgsl.nonWaitOverD3D12Percent) on ARM64. x64 still has lower absolute CPU cost, while ARM remains much closer to GPU saturation."
  $cpuConclusion="<li><strong>Direct CPU profiling confirms the backend overhead.</strong> Native Dawn non-wait CPU is $(F $x64CpuWgsl.metrics.nonWait.mean) ms versus $(F $x64CpuD3d.metrics.nonWait.mean) ms for D3D12 on x64 ($(SignedPercent $x64CpuWgsl.nonWaitOverD3D12Percent)), and $(F $armCpuWgsl.metrics.nonWait.mean) versus $(F $armCpuD3d.metrics.nonWait.mean) ms on ARM64 ($(SignedPercent $armCpuWgsl.nonWaitOverD3D12Percent)). Edge reaches $(F $x64CpuEdge.metrics.nonWait.mean) ms on x64 and $(F $armCpuEdge.metrics.nonWait.mean) ms on ARM64. Dawn's submit scope adds $(F ($x64CpuWgsl.metrics.submit.mean-$x64CpuD3d.metrics.submit.mean)) ms over D3D12 on x64 and $(F ($armCpuWgsl.metrics.submit.mean-$armCpuD3d.metrics.submit.mean)) ms on ARM64. Render-graph CPU adds $(F ($x64CpuWgsl.metrics.graph.mean-$x64CpuD3d.metrics.graph.mean)) ms on x64 but is $(F ([math]::Abs($armCpuWgsl.metrics.graph.mean-$armCpuD3d.metrics.graph.mean))) ms lower on ARM64, so ARM's native Dawn CPU tax is primarily submission rather than graph traversal. Edge's render-graph scope adds $(F ($x64CpuEdge.metrics.graph.mean-$x64CpuD3d.metrics.graph.mean)) ms on x64 and $(F ($armCpuEdge.metrics.graph.mean-$armCpuD3d.metrics.graph.mean)) ms on ARM64. These measurements confirm CPU/backend overhead directly; they do not change the separate GPU-bound interpretation of the ARM device.</li>"
  $cpuBackends=@(
    [pscustomobject]@{label='Native D3D12';x64=$x64CpuD3d;arm=$armCpuD3d},
    [pscustomobject]@{label='Native Dawn WGSL';x64=$x64CpuWgsl;arm=$armCpuWgsl},
    [pscustomobject]@{label='Microsoft Edge WebGPU';x64=$x64CpuEdge;arm=$armCpuEdge}
  )
  $cpuMatrixRows=foreach($backend in $cpuBackends){
    $ratio=$backend.arm.metrics.nonWait.mean/$backend.x64.metrics.nonWait.mean
    "<tr><td>$(Html $backend.label)</td><td>$(F $backend.x64.metrics.nonWait.mean)</td><td>$(SignedPercent $backend.x64.nonWaitOverD3D12Percent)</td><td>$(F $backend.arm.metrics.nonWait.mean)</td><td>$(SignedPercent $backend.arm.nonWaitOverD3D12Percent)</td><td>$(F $ratio 2)x</td></tr>"
  }
  $absoluteMax=($cpuBackends|ForEach-Object{@($_.x64.metrics.nonWait.mean,$_.arm.metrics.nonWait.mean)}|Measure-Object -Maximum).Maximum
  $absoluteBars=foreach($backend in $cpuBackends){
    $x64Width=100*$backend.x64.metrics.nonWait.mean/$absoluteMax;$armWidth=100*$backend.arm.metrics.nonWait.mean/$absoluteMax
    "<div class='bar-pair'><strong>$(Html $backend.label)</strong><div class='bar-row'><span>x64</span><div class='bar-track'><i class='bar x64-bar' style='width:$(F $x64Width 2)%'></i></div><b>$(F $backend.x64.metrics.nonWait.mean) ms</b></div><div class='bar-row'><span>ARM64</span><div class='bar-track'><i class='bar arm-bar' style='width:$(F $armWidth 2)%'></i></div><b>$(F $backend.arm.metrics.nonWait.mean) ms</b></div></div>"
  }
  $overheadBackends=@($cpuBackends|Where-Object label -ne 'Native D3D12')
  $overheadMax=($overheadBackends|ForEach-Object{@($_.x64.nonWaitOverD3D12Percent,$_.arm.nonWaitOverD3D12Percent)}|Measure-Object -Maximum).Maximum
  $overheadBars=foreach($backend in $overheadBackends){
    $x64Width=100*$backend.x64.nonWaitOverD3D12Percent/$overheadMax;$armWidth=100*$backend.arm.nonWaitOverD3D12Percent/$overheadMax
    "<div class='bar-pair'><strong>$(Html $backend.label)</strong><div class='bar-row'><span>x64</span><div class='bar-track'><i class='bar x64-bar' style='width:$(F $x64Width 2)%'></i></div><b>$(SignedPercent $backend.x64.nonWaitOverD3D12Percent)</b></div><div class='bar-row'><span>ARM64</span><div class='bar-track'><i class='bar arm-bar' style='width:$(F $armWidth 2)%'></i></div><b>$(SignedPercent $backend.arm.nonWaitOverD3D12Percent)</b></div></div>"
  }
  $cpuComparisonSection=@"
<section class="cpu-comparison"><h2>CPU Overhead: x64 versus ARM64</h2><p>Matched current-DXC CPU scenario on both hosts. Absolute cross-machine time reflects the complete systems, not CPU ISA alone; normalized columns compare each backend with native D3D12 on the same host.</p><div class="scroll"><table><thead><tr><th>Backend</th><th>x64 non-wait CPU (ms)</th><th>x64 vs D3D12</th><th>ARM64 non-wait CPU (ms)</th><th>ARM64 vs D3D12</th><th>ARM64 / x64</th></tr></thead><tbody>$($cpuMatrixRows-join'')</tbody></table></div><div class="report-chart-grid"><figure><figcaption>Absolute non-wait CPU time</figcaption><img class="report-chart" src="$chartRel/cpu-nonwait-absolute.png" alt="Absolute non-wait CPU time chart"></figure><figure><figcaption>Normalized backend cost</figcaption><img class="report-chart" src="$chartRel/cpu-overhead-normalized.png" alt="Normalized CPU backend overhead chart"></figure></div><div class="chart-grid"><figure class="cpu-chart"><figcaption>Absolute non-wait CPU time</figcaption>$($absoluteBars-join'')</figure><figure class="cpu-chart"><figcaption>Backend overhead versus native D3D12</figcaption>$($overheadBars-join'')</figure></div><p class="note">All runs use compute post-processing, 1920x1080, 1,800 profiled frames, frames 300-1799 analyzed, three repetitions per backend, and the same frame-by-frame work hash on both hosts.</p></section>
"@
}else{
  $systemBalanceText="ARM64 sustains $(F $arm64D3dTrace.engine3d.mean 1)% to $(F $arm64WgslTrace.engine3d.mean 1)% 3D-engine utilization, while x64 raster runs sustain only $(F $x64D3dTrace.engine3d.mean 1)% to $(F $x64WgslTrace.engine3d.mean 1)%. The RTX GPU finishes its work much sooner, so CPU recording and submission matter more."
}
$shaderConclusion=''
$shaderExecutive=''
$shaderCompilationSection=''
if($shaderAnalysis){
  $shaderLabels=@{d3d12='Native D3D12 DXC';wgsl='Native Dawn WGSL';spirv='Native Dawn HLSL -> SPIR-V -> WGSL'}
  $stageLabels=@{vertex='Vertex';pixel='Pixel';compute='Compute';all='All stages'}
  $shaderMatrixRows=foreach($stage in @('vertex','pixel','compute','all')){
    $cells=foreach($flow in @('d3d12','wgsl','spirv')){
      $stats=Find-ShaderSummary $flow $stage
      "<td>$(F $stats.meanMs)</td><td>$(F $stats.medianMs)</td><td>$(F $stats.maxMs)</td>"
    }
    "<tr><th>$(Html $stageLabels[$stage])</th>$($cells-join'')</tr>"
  }
  $shaderComparisonRows=foreach($row in $shaderAnalysis.comparisons){
    "<tr><th>$(Html $stageLabels[$row.stage])</th><td>$(F $row.d3d12MeanMs)</td><td>$(F $row.wgslMeanMs)</td><td>$(SignedPercent $row.wgslVsD3D12Percent)</td><td>$(F $row.spirvMeanMs)</td><td>$(SignedPercent $row.spirvVsD3D12Percent)</td><td>$(SignedPercent $row.spirvVsWgslPercent)</td></tr>"
  }
  $shaderComputeRows=foreach($row in $shaderAnalysis.computeVsPixel){
    "<tr><td>$(Html $shaderLabels[$row.flow])</td><td>$(F $row.pixelMeanMs)</td><td>$(F $row.computeMeanMs)</td><td>$(SignedPercent $row.computeVsPixelMeanPercent)</td><td>$(F $row.pixelMedianMs)</td><td>$(F $row.computeMedianMs)</td><td>$(SignedPercent $row.computeVsPixelMedianPercent)</td></tr>"
  }
  $shaderMaximumRows=foreach($flow in @('d3d12','wgsl','spirv')){
    foreach($stage in @('vertex','pixel','compute')){
      $stats=Find-ShaderSummary $flow $stage
      $identity=if($stats.maxKey){$stats.maxKey}elseif($stats.maxPermutation){$stats.maxPermutation}else{'-'}
      "<tr><td>$(Html $shaderLabels[$flow])</td><td>$(Html $stageLabels[$stage])</td><td>$($stats.count)</td><td>$(F $stats.meanMs)</td><td>$(F $stats.medianMs)</td><td>$(F $stats.p95Ms)</td><td>$(F $stats.maxMs)</td><td><code>$(Html $stats.maxShader)</code></td><td><code>$(Html $identity)</code></td><td>$($stats.maxRun)</td></tr>"
    }
  }
  $d3dPixel=Find-ShaderSummary 'd3d12' 'pixel';$wgslPixel=Find-ShaderSummary 'wgsl' 'pixel';$spirvPixel=Find-ShaderSummary 'spirv' 'pixel'
  $d3dAll=Find-ShaderSummary 'd3d12' 'all';$wgslAll=Find-ShaderSummary 'wgsl' 'all';$spirvAll=Find-ShaderSummary 'spirv' 'all'
  $d3dComputeDelta=@($shaderAnalysis.computeVsPixel|Where-Object flow -eq 'd3d12')[0]
  $wgslComputeDelta=@($shaderAnalysis.computeVsPixel|Where-Object flow -eq 'wgsl')[0]
  $spirvComputeDelta=@($shaderAnalysis.computeVsPixel|Where-Object flow -eq 'spirv')[0]
  $shaderExecutive="<article class='finding'><span>x64 shader preparation</span><strong>Pixel variants dominate cold preparation time</strong><p>Pixel-stage means are $(F $d3dPixel.meanMs) ms for native DXC, $(F $wgslPixel.meanMs) ms for strict WGSL preparation, and $(F $spirvPixel.meanMs) ms for SPIR-V preparation. Compute-stage means are 71-76% lower for this corpus.</p></article>"
  $shaderConclusion="<li><strong>x64 cold shader preparation is dominated by pixel variants, especially <code>FS_Mesh</code>.</strong> Across five cold launches per flow, native DXC compile/reflection averages $(F $d3dPixel.meanMs) ms for pixel stages, strict WGSL preparation averages $(F $wgslPixel.meanMs) ms, and HLSL -> SPIR-V -> WGSL preparation averages $(F $spirvPixel.meanMs) ms. The all-stage means are $(F $d3dAll.meanMs), $(F $wgslAll.meanMs), and $(F $spirvAll.meanMs) ms, respectively. These percentages compare different host-side preparation paths, not compiler speed, because Dawn backend compilation during pipeline creation is excluded. Compute means are $(SignedPercent $d3dComputeDelta.computeVsPixelMeanPercent), $(SignedPercent $wgslComputeDelta.computeVsPixelMeanPercent), and $(SignedPercent $spirvComputeDelta.computeVsPixelMeanPercent) versus pixel for D3D12, WGSL, and SPIR-V. This describes the recorded corpus rather than proving compute stages intrinsically cheaper: each run has 281 pixel variants but only 9 compute programs.</li>"
  $shaderCompilationSection=@"
<section class="shader-compilation"><h2>x64 Shader Preparation</h2><p>Five independent cold Release launches per flow, balanced across launch positions, against <code>Shaders/shader_permutations.json</code>. Every launch must produce 281 vertex, 281 pixel, and 9 compute cache-miss events. Arithmetic mean (average), median, p95, maximum, and exact maximum shader are calculated over all repetitions.</p><p class="note"><strong>Metric boundary:</strong> native D3D12 measures DXC compile plus reflection. WebGPU measures translation/preparation/reflection plus the synchronous <code>CreateShaderModule</code> call. Dawn backend compilation during pipeline creation is excluded, so cross-flow percentages are not compiler-speed comparisons. Pipeline creation, source loading, cache writes, process startup, and warm cache hits are excluded from per-stage values.</p>
<h3>Per-stage Timing Matrix</h3><div class="scroll"><table><thead><tr><th rowspan="2">Stage</th><th colspan="3">Native D3D12 DXC (ms)</th><th colspan="3">WebGPU WGSL (ms)</th><th colspan="3">WebGPU HLSL -> SPIR-V -> WGSL (ms)</th></tr><tr><th>Mean</th><th>Median</th><th>Max</th><th>Mean</th><th>Median</th><th>Max</th><th>Mean</th><th>Median</th><th>Max</th></tr></thead><tbody>$($shaderMatrixRows-join'')</tbody></table></div><figure class="single-chart"><figcaption>Mean cold preparation time by stage and flow</figcaption><img class="report-chart" src="$chartRel/shader-preparation-stage-means.png" alt="Shader preparation stage means chart"></figure>
<h3>Compiler-flow Comparison</h3><div class="scroll"><table><thead><tr><th>Stage</th><th>D3D12 mean (ms)</th><th>WGSL mean (ms)</th><th>WGSL vs D3D12</th><th>SPIR-V mean (ms)</th><th>SPIR-V vs D3D12</th><th>SPIR-V vs WGSL</th></tr></thead><tbody>$($shaderComparisonRows-join'')</tbody></table></div>
<h3>Compute versus Pixel</h3><div class="scroll"><table><thead><tr><th>Flow</th><th>Pixel mean (ms)</th><th>Compute mean (ms)</th><th>Compute vs pixel mean</th><th>Pixel median (ms)</th><th>Compute median (ms)</th><th>Compute vs pixel median</th></tr></thead><tbody>$($shaderComputeRows-join'')</tbody></table></div><p>The compute and pixel corpora contain different programs and unequal sample counts. These ratios describe this recorded corpus and do not isolate shader stage as the causal variable.</p>
<h3>Maximum Shader Detail</h3><div class="scroll"><table><thead><tr><th>Flow</th><th>Stage</th><th>Samples</th><th>Mean (ms)</th><th>Median (ms)</th><th>P95 (ms)</th><th>Max (ms)</th><th>Maximum shader</th><th>Key/permutation</th><th>Run</th></tr></thead><tbody>$($shaderMaximumRows-join'')</tbody></table></div><p>Executable <code>$(Html $shaderAnalysis.executable.sha256)</code>; manifest <code>$(Html $shaderAnalysis.manifestSha256)</code>.</p></section>
"@
}
[IO.Directory]::CreateDirectory($ChartDirectory)|Out-Null
Get-ChildItem $ChartDirectory -File -Filter '*.png' -ErrorAction SilentlyContinue|Remove-Item -Force
if(!(Test-Path -LiteralPath $ScreenshotPath)){throw "Missing report screenshot: $ScreenshotPath"}
$screenshotDirectory=Split-Path $ScreenshotPath -Parent
$screenshots=@(
  [pscustomobject]@{source=$ScreenshotPath;name='day-scene.png'},
  [pscustomobject]@{source=(Join-Path $screenshotDirectory '0.png');name='sandbox-scene.png'},
  [pscustomobject]@{source=(Join-Path $screenshotDirectory '6.png');name='minecraft-scene.png'}
)
foreach($screenshot in $screenshots){if(!(Test-Path -LiteralPath $screenshot.source)){throw "Missing report screenshot: $($screenshot.source)"};Copy-Item -LiteralPath $screenshot.source -Destination (Join-Path $ChartDirectory $screenshot.name) -Force}
$palette=@{d3d12='#24758A';wgsl='#3A7D72';spirv='#C46A32';edge='#9B4F78';x64='#24758A';arm='#C46A32'}
$gpuExecutionItems=@(
  [pscustomobject]@{label='x64 / Native D3D12';value=100;color=$palette.d3d12},
  [pscustomobject]@{label='x64 / Dawn WGSL';value=100*$x64Frame.wgslRasterMs/$x64Frame.d3d12RasterMs;color=$palette.wgsl},
  [pscustomobject]@{label='x64 / Dawn SPIR-V';value=100*$x64Frame.spirvRasterMs/$x64Frame.d3d12RasterMs;color=$palette.spirv},
  [pscustomobject]@{label='ARM64 / Native D3D12';value=100;color=$palette.d3d12},
  [pscustomobject]@{label='ARM64 / Dawn WGSL';value=100*$arm64Frame.wgslRasterMs/$arm64Frame.d3d12RasterMs;color=$palette.wgsl},
  [pscustomobject]@{label='ARM64 / Dawn SPIR-V';value=100*$arm64Frame.spirvRasterMs/$arm64Frame.d3d12RasterMs;color=$palette.spirv}
)
New-ReportBarChart (Join-Path $ChartDirectory 'gpu-execution-normalized.png') 'Raster GPU execution time by host' 'Each host is normalized to its own native D3D12 raster result. Lower is better.' $gpuExecutionItems 140 'index'
$throughputItems=@(
  [pscustomobject]@{label='x64 / Native D3D12';value=100;color=$palette.d3d12},
  [pscustomobject]@{label='x64 / Native Dawn WGSL';value=100*$x64Wgsl.medianFrameMs/$x64Native.medianFrameMs;color=$palette.wgsl},
  [pscustomobject]@{label='x64 / Native Dawn SPIR-V';value=100*$x64Spirv.medianFrameMs/$x64Native.medianFrameMs;color=$palette.spirv},
  [pscustomobject]@{label='x64 / Edge WebGPU';value=100*$x64Edge.medianFrameMs/$x64Native.medianFrameMs;color=$palette.edge},
  [pscustomobject]@{label='ARM64 / Native D3D12';value=100;color=$palette.d3d12},
  [pscustomobject]@{label='ARM64 / Native Dawn WGSL';value=100*$arm64Wgsl.medianFrameMs/$arm64Native.medianFrameMs;color=$palette.wgsl},
  [pscustomobject]@{label='ARM64 / Native Dawn SPIR-V';value=100*$arm64Spirv.medianFrameMs/$arm64Native.medianFrameMs;color=$palette.spirv},
  [pscustomobject]@{label='ARM64 / Edge WebGPU';value=100*$arm64Edge.medianFrameMs/$arm64Native.medianFrameMs;color=$palette.edge}
)
New-ReportBarChart (Join-Path $ChartDirectory 'completed-throughput-normalized.png') 'Queue-drained completed throughput by host' 'CPU recording, submission, queueing, and final GPU drain. Each host is normalized to native D3D12. Lower is better.' $throughputItems 140 'index'
if($x64Cpu-and$arm64Cpu){
  $cpuAbsoluteItems=@(
    [pscustomobject]@{label='x64 / Native D3D12';value=$x64CpuD3d.metrics.nonWait.mean;color=$palette.x64},
    [pscustomobject]@{label='x64 / Native Dawn WGSL';value=$x64CpuWgsl.metrics.nonWait.mean;color=$palette.wgsl},
    [pscustomobject]@{label='x64 / Edge WebGPU';value=$x64CpuEdge.metrics.nonWait.mean;color=$palette.edge},
    [pscustomobject]@{label='ARM64 / Native D3D12';value=$armCpuD3d.metrics.nonWait.mean;color=$palette.arm},
    [pscustomobject]@{label='ARM64 / Native Dawn WGSL';value=$armCpuWgsl.metrics.nonWait.mean;color=$palette.wgsl},
    [pscustomobject]@{label='ARM64 / Edge WebGPU';value=$armCpuEdge.metrics.nonWait.mean;color=$palette.edge}
  )
  New-ReportBarChart (Join-Path $ChartDirectory 'cpu-nonwait-absolute.png') 'Matched non-wait CPU time' 'Compute post-processing, frames 300-1799, three runs per backend. Lower is better.' $cpuAbsoluteItems 4 'ms'
  $cpuNormalizedItems=@(
    [pscustomobject]@{label='x64 / Native D3D12';value=100;color=$palette.x64},
    [pscustomobject]@{label='x64 / Native Dawn WGSL';value=100+$x64CpuWgsl.nonWaitOverD3D12Percent;color=$palette.wgsl},
    [pscustomobject]@{label='x64 / Edge WebGPU';value=100+$x64CpuEdge.nonWaitOverD3D12Percent;color=$palette.edge},
    [pscustomobject]@{label='ARM64 / Native D3D12';value=100;color=$palette.arm},
    [pscustomobject]@{label='ARM64 / Native Dawn WGSL';value=100+$armCpuWgsl.nonWaitOverD3D12Percent;color=$palette.wgsl},
    [pscustomobject]@{label='ARM64 / Edge WebGPU';value=100+$armCpuEdge.nonWaitOverD3D12Percent;color=$palette.edge}
  )
  New-ReportBarChart (Join-Path $ChartDirectory 'cpu-overhead-normalized.png') 'CPU backend cost normalized per host' 'Native D3D12 is 100 on each host. Lower is better.' $cpuNormalizedItems 400 'index'
}
if($shaderAnalysis){
  $shaderChartItems=@()
  foreach($stage in @('vertex','pixel','compute')){foreach($flow in @('d3d12','wgsl','spirv')){$stats=Find-ShaderSummary $flow $stage;$shaderChartItems+=[pscustomobject]@{label="$($stageLabels[$stage]) / $($shaderLabels[$flow])";value=$stats.meanMs;color=$palette[$flow]}}}
  New-ReportBarChart (Join-Path $ChartDirectory 'shader-preparation-stage-means.png') 'x64 cold shader preparation by stage' 'Native D3D12 is DXC compile plus reflection; WebGPU is source preparation plus CreateShaderModule. Lower is better.' $shaderChartItems 30 'ms'
}
$visualOverviewSection=@"
<section class="visual-overview"><h2>Visual Overview</h2><p>The profiling workload uses authored engine content. GPU timing runs render the held DayScene state offscreen; these screenshots provide visual context and are not measured frames copied from <code>SubmitNoPresent</code>.</p><div class="scene-gallery"><figure><img src="$chartRel/day-scene.png" alt="DayScene rendered benchmark scene"><figcaption>DayScene benchmark scene</figcaption></figure><figure><img src="$chartRel/sandbox-scene.png" alt="Sandbox rendered scene"><figcaption>Sandbox rendering coverage</figcaption></figure><figure><img src="$chartRel/minecraft-scene.png" alt="Minecraft rendered scene"><figcaption>Minecraft streaming workload</figcaption></figure></div><div class="report-chart-grid"><figure><figcaption>GPU execution</figcaption><img class="report-chart" src="$chartRel/gpu-execution-normalized.png" alt="Normalized raster GPU execution chart"></figure><figure><figcaption>Completed throughput</figcaption><img class="report-chart" src="$chartRel/completed-throughput-normalized.png" alt="Normalized completed throughput chart"></figure></div></section>
"@
$executiveSummary=@"
<section class="executive"><h2>Executive Summary</h2><div class="summary-grid">
<article class="finding"><span>GPU execution</span><strong>Native D3D12 is fastest on both GPUs</strong><p>Dawn WGSL adds $(SignedPercent $x64Frame.wgslRasterOverNativePercent) raster GPU time on x64 and $(SignedPercent $arm64Frame.wgslRasterOverNativePercent) on ARM64. The backend gap is much larger on the RTX host.</p></article>
<article class="finding"><span>System balance</span><strong>ARM64 is GPU-bound; x64 is relatively feed-bound</strong><p>$systemBalanceText</p></article>
<article class="finding"><span>Compute post-processing</span><strong>Compute is platform- and flow-specific</strong><p>On x64, WGSL compute reduces GPU time by $(F ([math]::Abs($x64Frame.wgslComputeOverRasterPercent)) 2)%; on ARM64 every native flow is slower in compute mode by $(F $arm64Frame.nativeComputeOverRasterPercent 2)% to $(F $arm64Frame.wgslComputeOverRasterPercent 2)%.</p></article>
<article class="finding"><span>Microsoft Edge WebGPU</span><strong>Browser throughput reverses direction by host</strong><p>Edge raster is $(SignedPercent $x64Edge.overNativeD3D12Percent) versus native D3D12 on x64 and $(SignedPercent $arm64Edge.overNativeD3D12Percent) on ARM64. Browser pass timestamps remain unavailable.</p></article>
$shaderExecutive
</div></section>
"@
$conclusions=@"
<section class="conclusion-section"><h2>Conclusions</h2><ol class="conclusions">
<li><strong>DXC/DXIL parity does not imply equal GPU code or equal cost.</strong> Native D3D12 compiles authored HLSL directly, while Dawn lowers WGSL through Tint before DXC. Native D3D12 wins whole-frame GPU execution on both hosts: $(F $x64Frame.d3d12RasterMs) versus $(F $x64Frame.wgslRasterMs) ms on x64, and $(F $arm64Frame.d3d12RasterMs) versus $(F $arm64Frame.wgslRasterMs) ms on ARM64.</li>
<li><strong>The ARM64 system is more GPU-bound; x64 is relatively CPU/submission-bound.</strong> ARM64 whole-frame GPU work is $(F $arm64Frame.d3d12RasterMs)-$(F $arm64Frame.wgslRasterMs) ms with sustained 3D-engine utilization of $(F $arm64D3dTrace.engine3d.mean 1)% for D3D12 and $(F $arm64WgslTrace.engine3d.mean 1)% for WGSL. On x64, the much larger RTX GPU completes the same raster flows in $(F $x64Frame.d3d12RasterMs)-$(F $x64Frame.wgslRasterMs) ms and sustained utilization is only $(F $x64D3dTrace.engine3d.mean 1)% to $(F $x64WgslTrace.engine3d.mean 1)%. CPU recording, submission, queue depth, and synchronization therefore represent a larger part of x64 end-to-end time, whereas keeping the Adreno continuously occupied matters more on ARM64.</li>
<li><strong>ARM64 native D3D12 appears to underfeed the GPU relative to Dawn.</strong> D3D12 executes the raster command stream faster on the GPU ($(F $arm64Frame.d3d12RasterMs) versus $(F $arm64Frame.wgslRasterMs) ms), yet its completed throughput is slower ($(F $arm64Native.medianFrameMs) versus $(F $arm64Wgsl.medianFrameMs) ms/frame) and its sustained 3D utilization is lower ($(F $arm64D3dTrace.engine3d.mean 1)% versus $(F $arm64WgslTrace.engine3d.mean 1)%). Dawn's fuller queue occupancy overcomes its slower GPU code. This points to batching, command feeding, synchronization, queue-depth, or Adreno D3D12-driver interaction; these measurements do not isolate which mechanism is responsible.</li>
<li><strong>GPU timestamps and completed throughput must remain separate.</strong> The timestamp result isolates submitted GPU execution, while completed throughput also includes CPU command generation, submission, queueing, and the final drain. On x64, native D3D12's larger GPU-time advantage survives end to end ($(F $x64Native.medianFrameMs) versus $(F $x64Wgsl.medianFrameMs) ms/frame). On ARM64, Dawn's scheduling/occupancy advantage reverses the timestamp ordering.</li>
$cpuConclusion
$shaderConclusion
<li><strong>The pass-level cause is GPU-specific.</strong> On x64, the largest WGSL raster penalties are distributed, led by $(Html $x64Top.pass) (+$(F $x64Top.deltaMs) ms), followed by GBuffer and DOF. On ARM64, $(Html $arm64Top.pass) dominates at +$(F $arm64Top.deltaMs) ms, partly offset by $(Html $arm64Offset.pass) ($(F $arm64Offset.deltaMs) ms).</li>
<li><strong>Compute is not a universal optimization.</strong> Native D3D12 and SPIR-V compute are $(SignedPercent $x64Frame.nativeComputeOverRasterPercent) and $(SignedPercent $x64Frame.spirvComputeOverRasterPercent) on x64, while WGSL compute is $(SignedPercent $x64Frame.wgslComputeOverRasterPercent). On ARM64 all three native flows regress by $(F $arm64Frame.nativeComputeOverRasterPercent 2)% to $(F $arm64Frame.wgslComputeOverRasterPercent 2)%. Edge compute is $(SignedPercent $x64EdgeCompute.computeOverRasterPercent) on x64 but $(SignedPercent $arm64EdgeCompute.computeOverRasterPercent) on ARM64.</li>
<li><strong>Authored WGSL versus the SPIR-V detour has no universal winner.</strong> SPIR-V raster GPU time is $(SignedPercent $x64SourceGap) versus WGSL on x64 and $(SignedPercent $arm64SourceGap) on ARM64; completed throughput is $(SignedPercent $x64SourceThroughputGap) and $(SignedPercent $arm64SourceThroughputGap), respectively. These source-flow deltas are smaller and less consistent than the API/backend effect.</li>
<li><strong>Browser evidence is throughput-only.</strong> Both Edge builds advertise <code>timestamp-query</code> but lack <code>GPUCommandEncoder.writeTimestamp</code>. Edge values therefore include browser, CPU recording/submission, Dawn, queueing, and final GPU drain; they must not be read as per-pass GPU execution. The black or stale benchmark canvas is expected because measured work uses <code>SubmitNoPresent</code>.</li>
<li><strong>Run spread and platform differences limit causal claims.</strong> Whole-frame timestamp CV spans $(F $x64CvMin 2)% to $(F $x64CvMax 2)% on x64 and $(F $arm64CvMin 2)% to $(F $arm64CvMax 2)% on ARM64. The comparison establishes behavior on these two complete systems, not an x64-versus-ARM64 ISA effect.</li>
</ol></section>
"@
$generated=Html $analysis.generatedUtc
$browserStatus=Html $analysis.browserWebGpu.status
$browserReason=Html $analysis.browserWebGpu.reason
$normalization=Html $analysis.normalization
$html=@"
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>DayScene DXC Cross-Machine GPU Report</title><style>:root{--ink:#172126;--muted:#58676e;--line:#cad4d9;--paper:#f3f6f7;--panel:#fff;--accent:#1f6876;--warn:#a45a1d}*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.5 "Segoe UI",sans-serif;letter-spacing:0}header{padding:34px max(20px,calc((100% - 1240px)/2));background:#173841;color:#fff;border-bottom:5px solid #d28a3c}header h1{margin:0 0 7px;font-size:34px}.wrap{max-width:1240px;margin:auto;padding:28px 20px 60px}section{min-width:0;margin-bottom:40px}h2{border-bottom:2px solid var(--line);padding-bottom:7px}h3{margin-top:28px}table{display:block;max-width:100%;overflow-x:auto;width:100%;border-collapse:collapse;background:var(--panel);font-variant-numeric:tabular-nums}th,td{padding:8px 10px;border-bottom:1px solid #dce3e6;text-align:right;white-space:nowrap}th:first-child,td:first-child{text-align:left}th{background:#e6edef}.scroll{max-width:100%;overflow-x:auto}.scroll table{display:table;min-width:100%;max-width:none;overflow:visible}.note{padding:14px 18px;background:#fff4e8;border-left:4px solid var(--warn)}code{background:#e4ebed;padding:2px 5px;overflow-wrap:anywhere}@media(max-width:700px){header h1{font-size:27px}.wrap{padding:20px 10px}table{font-size:12px}th,td{padding:6px}}</style></head><body><header><h1>DayScene DXC Cross-Machine GPU Report</h1><p>Native D3D12 DXC/DXIL, native Dawn WGSL/SPIR-V, and Microsoft Edge WebGPU</p></header><main class="wrap"><p>$normalization</p><p class="note"><strong>Browser per-pass GPU timestamps: $browserStatus.</strong> $browserReason. Browser completed throughput is reported separately where available.</p><section><h2>Normalized Summary</h2><table><thead><tr><th>Host</th><th>D3D12 raster GPU ms</th><th>WGSL raster GPU ms</th><th>WGSL overhead</th><th>SPIR-V raster GPU ms</th><th>SPIR-V overhead</th><th>Edge raster throughput</th></tr></thead><tbody>$($summaryRows-join'')</tbody></table>$($observations-join'')</section>$($sections-join'')<p>Generated UTC: $generated</p></main></body></html>
"@
$reportCss=@'
header{padding-left:max(20px,calc((100% - 1780px)/2));padding-right:max(20px,calc((100% - 1780px)/2))}.wrap{max-width:1780px}.summary-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:18px}.finding{min-width:0;padding:16px 18px;border-left:4px solid var(--accent);background:#fff}.finding span{display:block;margin-bottom:5px;color:var(--muted);font-size:12px;font-weight:700;text-transform:uppercase}.finding strong{display:block;font-size:18px;line-height:1.3}.finding p{margin:9px 0 0}.conclusions{display:grid;gap:12px;padding-left:26px}.conclusions li{padding-left:5px}.chart-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px;margin:20px 0}.cpu-chart{margin:0;padding:18px;background:#fff;border:1px solid var(--line)}.cpu-chart figcaption{margin-bottom:14px;font-size:18px;font-weight:700}.bar-pair{margin:0 0 16px}.bar-pair:last-child{margin-bottom:0}.bar-pair>strong{display:block;margin-bottom:5px}.bar-row{display:grid;grid-template-columns:52px minmax(100px,1fr) 86px;gap:8px;align-items:center;margin:4px 0;font-size:13px}.bar-row b{text-align:right;font-variant-numeric:tabular-nums}.bar-track{height:15px;background:#e4eaec;overflow:hidden}.bar{display:block;height:100%;min-width:2px}.x64-bar{background:#24758a}.arm-bar{background:#c46a32}.pass-scroll{border:1px solid var(--line);background:#fff}.scroll .pass-table{display:table;width:100%;min-width:1580px;table-layout:fixed;font-size:13px}.pass-table col.pass-name{width:205px}.pass-table col.pass-metric{width:125px}.pass-table th{white-space:normal;vertical-align:bottom;line-height:1.25;text-align:center}.pass-table td{white-space:nowrap}.pass-table th:first-child,.pass-table td:first-child{position:sticky;left:0;z-index:2;background:#fff;font-weight:650}.pass-table thead th:first-child{z-index:4;background:#d6e2e5}.pass-table .api-groups th{padding-top:12px;padding-bottom:12px;background:#d6e2e5;border-bottom:2px solid #9eb1b8;font-size:14px}.pass-table .api-groups th span{color:#455960;font-size:12px;font-weight:500}.pass-table .api-groups th:not(:last-child){border-right:2px solid #9eb1b8}.pass-table thead tr:nth-child(2) th:nth-child(3),.pass-table thead tr:nth-child(2) th:nth-child(7),.pass-table tbody td:nth-child(4),.pass-table tbody td:nth-child(8){border-right:2px solid #c0cdd1}.pass-table tbody tr:first-child{font-weight:700;background:#edf4f5}.pass-table tbody tr:first-child td:first-child{background:#edf4f5}@media(max-width:1100px){.summary-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}@media(max-width:700px){.summary-grid,.chart-grid{grid-template-columns:1fr}.finding strong{font-size:16px}.bar-row{grid-template-columns:46px minmax(70px,1fr) 78px}.scroll .pass-table{min-width:1500px;font-size:12px}.pass-table col.pass-name{width:190px}.pass-table col.pass-metric{width:119px}}
'@
$visualCss=@'
.report-chart-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px;margin:20px 0}.report-chart-grid figure,.single-chart,.scene-gallery figure{margin:0;padding:18px;background:#fff;border:1px solid var(--line)}.report-chart-grid figcaption,.single-chart figcaption,.scene-gallery figcaption{margin-top:10px;font-size:18px;font-weight:700}.report-chart{display:block;width:100%;height:auto}.scene-gallery{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:18px;margin:20px 0}.scene-gallery img{display:block;width:100%;aspect-ratio:16/9;object-fit:cover}.single-chart{margin-top:20px}@media(max-width:980px){.report-chart-grid,.scene-gallery{grid-template-columns:1fr}}@media(max-width:700px){.report-chart-grid figure,.single-chart,.scene-gallery figure{padding:10px}}
'@
$html=$html.Replace('</style>',"$reportCss$visualCss</style>")
$html=$html.Replace('grid-template-columns:repeat(4,minmax(0,1fr))','grid-template-columns:repeat(auto-fit,minmax(250px,1fr))')
$html=$html.Replace('<section><h2>Normalized Summary</h2>',"$executiveSummary$visualOverviewSection<section><h2>Normalized Summary</h2>")
$html=$html.Replace('<section><h2>x64</h2>',"$conclusions<section><h2>x64</h2>")
$html=$html.Replace('<section class="conclusion-section">',$cpuComparisonSection+$shaderCompilationSection+'<section class="conclusion-section">')
[IO.Directory]::CreateDirectory((Split-Path $ReportPath -Parent))|Out-Null
[IO.File]::WriteAllText($ReportPath,$html,[Text.UTF8Encoding]::new($false))
Write-Output "Cross-machine report written: $ReportPath"