[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$GpuEvidence,
  [Parameter(Mandatory=$true)][string]$ShaderEvidence,
  [string]$TraceEvidence,
  [Parameter(Mandatory=$true)][string]$OutputPath
)
$ErrorActionPreference='Stop'
$gpu=Get-Content (Join-Path $GpuEvidence 'result.json') -Raw|ConvertFrom-Json
$shader=Get-Content (Join-Path $ShaderEvidence 'result.json') -Raw|ConvertFrom-Json
$traceRoot=if($TraceEvidence){$TraceEvidence}else{$GpuEvidence}
$traceGpu=if($TraceEvidence){Get-Content (Join-Path $TraceEvidence 'result.json') -Raw|ConvertFrom-Json}else{$gpu}
if(!$gpu.passed-or!$shader.passed-or!$traceGpu.passed){throw 'Input evidence did not pass'}
foreach($input in @($shader,$traceGpu)){
  if($input.machine-ne$gpu.machine-or$input.architecture-ne$gpu.architecture-or$input.executableSha256-ne$gpu.executableSha256){throw 'Input evidence machine, architecture, or executable identity differs'}
}
if($traceGpu.collectorSha256-ne$gpu.collectorSha256){throw 'Trace and throughput collector identity differs'}
function Percentile([double[]]$v,[double]$f){$s=@($v|Sort-Object);if(!$s.Count){return $null};$p=($s.Count-1)*$f;$a=[math]::Floor($p);$b=[math]::Ceiling($p);if($a-eq$b){return [double]$s[$a]};[double]$s[$a]+($p-$a)*([double]$s[$b]-[double]$s[$a])}
function Stats([double[]]$v){if(!$v.Count){return $null};$mean=($v|Measure-Object -Average).Average;$variance=if($v.Count-gt1){($v|ForEach-Object{($_-$mean)*($_-$mean)}|Measure-Object -Average).Average}else{0};[pscustomobject]@{count=$v.Count;mean=$mean;median=Percentile $v .5;p95=Percentile $v .95;min=($v|Measure-Object -Minimum).Minimum;max=($v|Measure-Object -Maximum).Maximum;standardDeviation=[math]::Sqrt($variance);coefficientOfVariationPercent=if($mean){100*[math]::Sqrt($variance)/$mean}else{$null}}}
function Validate-Etl([string]$path){$xperf=Get-Command xperf.exe -ErrorAction Stop;$statsPath=Join-Path (Split-Path $path -Parent) 'xperf-tracestats.txt';& $xperf.Source -i $path -a tracestats *> $statsPath;if($LASTEXITCODE-ne0){throw "xperf tracestats failed: $path"};$stats=Get-Content $statsPath -Raw;$buffers=[regex]::Match($stats,'Total\s+#\s+Lost\s+Buffers\s*:\s*(\d+)',[Text.RegularExpressions.RegexOptions]::IgnoreCase);$events=[regex]::Match($stats,'Total\s+#\s+Lost\s+Events\s*:\s*(\d+)',[Text.RegularExpressions.RegexOptions]::IgnoreCase);if(!$buffers.Success-or!$events.Success){throw "Unable to parse ETL loss counters: $path"};$result=[pscustomobject]@{lostBuffers=[int64]$buffers.Groups[1].Value;lostEvents=[int64]$events.Groups[1].Value};if($result.lostBuffers-ne0-or$result.lostEvents-ne0){throw "ETL contains lost data: $path"};$result}
$cells=foreach($g in @($gpu.runs|Group-Object cell|Sort-Object Name)){$runs=@($g.Group);$frameMs=@($runs|ForEach-Object{[double]$_.elapsedMs/[double]$_.frames});$fps=@($runs|ForEach-Object{[double]$_.completedFps});[pscustomobject]@{cell=$g.Name;repetitions=$runs.Count;completedFrameMs=Stats $frameMs;completedFps=Stats $fps;runs=@($runs|ForEach-Object{[pscustomobject]@{frames=$_.frames;elapsedMs=$_.elapsedMs;frameMs=[double]$_.elapsedMs/[double]$_.frames;completedFps=$_.completedFps;presents=$_.presents}})}}
function Cell([string]$name){@($cells|Where-Object cell -EQ $name)[0]}
function Delta([string]$label,[string]$reference,[string]$candidate){$r=Cell $reference;$c=Cell $candidate;$rv=[double]$r.completedFrameMs.median;$cv=[double]$c.completedFrameMs.median;$d=$cv-$rv;[pscustomobject]@{label=$label;reference=$reference;candidate=$candidate;referenceMs=$rv;candidateMs=$cv;deltaMs=$d;deltaPercent=if($rv){100*$d/$rv}else{$null}}}
$deltas=@(
 Delta 'D3D12 compute minus raster' 'd3d12-raster' 'd3d12-compute'
 Delta 'WGSL compute minus raster' 'wgsl-raster' 'wgsl-compute'
 Delta 'SPIR-V compute minus raster' 'spirv-raster' 'spirv-compute'
 Delta 'WGSL minus D3D12 raster' 'd3d12-raster' 'wgsl-raster'
 Delta 'SPIR-V minus D3D12 raster' 'd3d12-raster' 'spirv-raster'
 Delta 'WGSL minus D3D12 compute' 'd3d12-compute' 'wgsl-compute'
 Delta 'SPIR-V minus D3D12 compute' 'd3d12-compute' 'spirv-compute'
 Delta 'SPIR-V minus WGSL raster' 'wgsl-raster' 'spirv-raster'
 Delta 'SPIR-V minus WGSL compute' 'wgsl-compute' 'spirv-compute'
)
$traces=foreach($t in $traceGpu.traces){$counterPath=Join-Path $t.directory 'gpu-engine-counters.json';if(!(Test-Path $counterPath)){$counterPath=Join-Path (Join-Path $traceRoot "traces\$($t.cell)") 'gpu-engine-counters.json'};$data=if(Test-Path $counterPath){@(Get-Content $counterPath -Raw|ConvertFrom-Json)}else{@()};$three=@($data|Where-Object{$_.path-match'engtype_3d'});$perTime=@($three|Group-Object timestamp|ForEach-Object{($_.Group.value|Measure-Object -Sum).Sum});$etl=Join-Path (Join-Path $traceRoot "traces\$($t.cell)") 'gpu.etl';$traceStats=if($t.traceStats){$t.traceStats}elseif((Test-Path $etl)-and$t.etlValidationDeferred){Validate-Etl $etl}else{$null};$etlResult=if(Test-Path $etl){if(!$traceStats){throw "ETL was not validated: $etl"};[pscustomobject]@{available=$true;bytes=(Get-Item $etl).Length;sha256=(Get-FileHash $etl).Hash;lostBuffers=[int64]$traceStats.lostBuffers;lostEvents=[int64]$traceStats.lostEvents;validatedOnMachine=$env:COMPUTERNAME}}else{[pscustomobject]@{available=$false;reason='WPR capture skipped or unavailable'}};[pscustomobject]@{cell=$t.cell;frames=$t.frames;elapsedMs=$t.elapsedMs;frameMs=[double]$t.elapsedMs/[double]$t.frames;completedFps=$t.completedFps;presents=$t.presents;engine3d=Stats @($perTime);etl=$etlResult}}
$compile=foreach($g in @($shader.compileAll|Group-Object case|Sort-Object Name)){$cold=@($g.Group|Where-Object state -EQ cold)[0];$warm=@($g.Group|Where-Object state -EQ warm)[0];[pscustomobject]@{case=$g.Name;permutations=$cold.succeeded;coldMs=$cold.wallMs;warmMs=$warm.wallMs;savedMs=$cold.wallMs-$warm.wallMs;speedup=$cold.wallMs/$warm.wallMs}}
$startup=foreach($entry in $shader.startup){$scopes=@{};foreach($s in $entry.shaderScopes){$scopes[$s.name]=$s.totalMs};$counters=@{};foreach($c in $entry.shaderCounters){$counters[$c.name]=$c.value};$dir=Join-Path $ShaderEvidence "startup\$($entry.cell)-$($entry.state)";$report=Get-ChildItem $dir -Filter 'telemetry_*.json'|Select-Object -First 1;$work=@{};if($report){$telemetry=Get-Content $report.FullName -Raw|ConvertFrom-Json;$frame=@($telemetry.frames|Where-Object{-not$_.startup})[0];foreach($c in @($frame.counters)){$work[$c.name]=$c.value}};[pscustomobject]@{cell=$entry.cell;state=$entry.state;wallMs=$entry.wallMs;firstRuntimeFrameCompleteMs=$entry.firstRuntimeFrameCompleteMs;shaderScopes=$scopes;shaderCounters=$counters;work=$work}}
$analysis=[pscustomobject]@{schema=2;generatedUtc=[DateTime]::UtcNow.ToString('o');method='GPU-drained offscreen SubmitNoPresent; no DWM or swapchain presents';capture=[pscustomobject]@{machine=$gpu.machine;architecture=$gpu.architecture;executableSha256=$gpu.executableSha256;collectorPath=$gpu.collectorPath;collectorSha256=$gpu.collectorSha256;skipWpr=[bool]$gpu.skipWpr;scene=$gpu.scene;width=$gpu.width;height=$gpu.height;holdFrame=$gpu.holdFrame;measuredFrames=$gpu.measuredFrames;traceFrames=$gpu.traceFrames;zeroPresent=$gpu.zeroPresent};cells=@($cells);deltas=$deltas;traces=@($traces);compileAll=@($compile);startup=@($startup)}
$analysis|ConvertTo-Json -Depth 15|Set-Content $OutputPath -Encoding UTF8
Write-Output "Offline analysis written: $OutputPath"
