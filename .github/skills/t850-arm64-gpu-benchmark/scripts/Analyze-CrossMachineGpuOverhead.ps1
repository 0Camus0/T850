[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$X64TimestampPath,
    [Parameter(Mandatory=$true)][string]$Arm64TimestampPath,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [string]$X64OfflineAnalysisPath,
    [string]$Arm64OfflineAnalysisPath,
    [string]$X64BrowserPath,
    [string]$Arm64BrowserPath,
    [string]$BrowserStatus = 'capability-blocked',
    [string]$BrowserReason = 'GPUCommandEncoder.writeTimestamp is unavailable'
)
$ErrorActionPreference='Stop'
function Percentile([double[]]$Values,[double]$Fraction){$sorted=@($Values|Sort-Object);if(!$sorted.Count){return $null};$position=($sorted.Count-1)*$Fraction;$lower=[math]::Floor($position);$upper=[math]::Ceiling($position);if($lower-eq$upper){return [double]$sorted[$lower]};[double]$sorted[$lower]+($position-$lower)*([double]$sorted[$upper]-[double]$sorted[$lower])}
function Aggregate($result,[string]$cell){
  $runs=@($result.runs|Where-Object cell -eq $cell)
  if(!$runs.Count){throw "Missing cell: $cell"}
  $frame=@($runs|ForEach-Object{[double]$_.medianGpuMs})
  $names=@($runs[0].passes.name)
  foreach($run in $runs){
    $runNames=@($run.passes.name)
    if($runNames.Count-ne$names.Count-or@(Compare-Object $names $runNames).Count){throw "Pass structure mismatch: $cell"}
  }
  $passes=@(foreach($name in $names){
    $values=@(foreach($run in $runs){
      $pass=@($run.passes|Where-Object name -eq $name)[0]
      [double]$pass.medianGpuMs
    })
    [pscustomobject]@{name=$name;medianGpuMs=Percentile $values .5;minGpuMs=($values|Measure-Object -Minimum).Minimum;maxGpuMs=($values|Measure-Object -Maximum).Maximum;runs=$values.Count}
  })
  $frameMean=($frame|Measure-Object -Average).Average
  $frameVariance=($frame|ForEach-Object{($_-$frameMean)*($_-$frameMean)}|Measure-Object -Average).Average
  [pscustomobject]@{cell=$cell;frame=[pscustomobject]@{medianGpuMs=Percentile $frame .5;minGpuMs=($frame|Measure-Object -Minimum).Minimum;maxGpuMs=($frame|Measure-Object -Maximum).Maximum;coefficientOfVariationPercent=if($frameMean){100*[math]::Sqrt($frameVariance)/$frameMean}else{$null};runs=$frame.Count};passes=$passes}
}
function Machine($result,[string]$label){
  $cells=@{};foreach($name in @('d3d12-raster','d3d12-compute','wgsl-raster','wgsl-compute','spirv-raster','spirv-compute')){$cells[$name]=Aggregate $result $name}
  $passNames=@($cells['d3d12-raster'].passes.name)
  $rows=@(foreach($pass in @('gpu.frame')+$passNames){
    $value={param($cell)if($pass-eq'gpu.frame'){[double]$cells[$cell].frame.medianGpuMs}else{$match=@($cells[$cell].passes|Where-Object name -eq $pass);if($match.Count-ne1){throw "Missing or duplicate pass '$pass' in $cell"};[double]$match[0].medianGpuMs}}
    $dr=&$value 'd3d12-raster';$dc=&$value 'd3d12-compute';$wr=&$value 'wgsl-raster';$wc=&$value 'wgsl-compute';$sr=&$value 'spirv-raster';$sc=&$value 'spirv-compute'
    [pscustomobject]@{pass=$pass;d3d12RasterMs=$dr;d3d12ComputeMs=$dc;wgslRasterMs=$wr;wgslComputeMs=$wc;spirvRasterMs=$sr;spirvComputeMs=$sc;wgslRasterOverNativePercent=if($dr){100*($wr-$dr)/$dr}else{$null};spirvRasterOverNativePercent=if($dr){100*($sr-$dr)/$dr}else{$null};wgslComputeOverNativePercent=if($dc){100*($wc-$dc)/$dc}else{$null};spirvComputeOverNativePercent=if($dc){100*($sc-$dc)/$dc}else{$null};nativeComputeOverRasterPercent=if($dr){100*($dc-$dr)/$dr}else{$null};wgslComputeOverRasterPercent=if($wr){100*($wc-$wr)/$wr}else{$null};spirvComputeOverRasterPercent=if($sr){100*($sc-$sr)/$sr}else{$null}}
  })
  [pscustomobject]@{label=$label;machine=$result.machine;architecture=$result.architecture;executableSha256=$result.executableSha256;repetitions=$result.repetitions;samplesPerRun=$result.samplesPerRun;cells=@($cells.Values);relativeOverhead=$rows}
}
function Throughput($timestamp,$offline,$browser){
  if($offline.schema-notin@(1,2)-or@($offline.cells).Count-ne6){throw 'Offline analysis input is incompatible'}
  if([string]$offline.capture.executableSha256-ne[string]$timestamp.executableSha256){throw 'Timestamp and offline executable hashes differ'}
  $rows=@()
  foreach($cell in $offline.cells){
    $mode=if($cell.cell-like'*-compute'){'compute'}else{'raster'}
    $reference=@($offline.cells|Where-Object cell -eq "d3d12-$mode")[0]
    $frameMs=[double]$cell.completedFrameMs.median
    $referenceMs=[double]$reference.completedFrameMs.median
    if($frameMs-le0-or$referenceMs-le0){throw "Invalid throughput duration: $($cell.cell)"}
    $rows+=[pscustomobject]@{cell=$cell.cell;source='native';mode=$mode;medianFrameMs=$frameMs;minFrameMs=$cell.completedFrameMs.min;maxFrameMs=$cell.completedFrameMs.max;coefficientOfVariationPercent=$cell.completedFrameMs.coefficientOfVariationPercent;completedFps=1000/$frameMs;overNativeD3D12Percent=if($referenceMs){100*($frameMs-$referenceMs)/$referenceMs}else{$null};runs=$cell.repetitions}
  }
  if($browser){
    if(!$browser.passed){throw 'Browser throughput input did not pass'}
    if($browser.machine-ne$timestamp.machine-or$browser.architecture-ne$timestamp.architecture){throw 'Browser and native timestamp host identity differs'}
    if(@($browser.runs).Count-ne(2*[int]$browser.repetitions)){throw 'Browser throughput matrix is incomplete'}
    foreach($group in @($browser.runs|Group-Object cell|Sort-Object Name)){
      if($group.Count-ne[int]$browser.repetitions){throw "Browser cell repetition mismatch: $($group.Name)"}
      if(@($group.Group|Where-Object{$_.frames-ne$browser.frames-or$_.presents-ne0-or$_.renderSize[0]-ne$browser.width-or$_.renderSize[1]-ne$browser.height}).Count){throw "Browser cell contract mismatch: $($group.Name)"}
      $mode=if($group.Name-like'*-compute'){'compute'}else{'raster'}
      $reference=@($offline.cells|Where-Object cell -eq "d3d12-$mode")[0]
      $values=@($group.Group|ForEach-Object{[double]$_.frameMs})
      $frameMs=Percentile $values .5
      $referenceMs=[double]$reference.completedFrameMs.median
      if($frameMs-le0-or$referenceMs-le0){throw "Invalid browser throughput duration: $($group.Name)"}
      $mean=($values|Measure-Object -Average).Average
      $variance=($values|ForEach-Object{($_-$mean)*($_-$mean)}|Measure-Object -Average).Average
      $rows+=[pscustomobject]@{cell=$group.Name;source='browser-edge';mode=$mode;medianFrameMs=$frameMs;minFrameMs=($values|Measure-Object -Minimum).Minimum;maxFrameMs=($values|Measure-Object -Maximum).Maximum;coefficientOfVariationPercent=if($mean){100*[math]::Sqrt($variance)/$mean}else{$null};completedFps=1000/$frameMs;overNativeD3D12Percent=if($referenceMs){100*($frameMs-$referenceMs)/$referenceMs}else{$null};runs=$values.Count}
    }
  }
  $computeDeltas=@(foreach($prefix in @('d3d12','wgsl','spirv','browser-wgsl')){
    $source=if($prefix-eq'browser-wgsl'){'browser-edge'}else{'native'}
    $raster=@($rows|Where-Object{$_.source-eq$source-and$_.cell-eq"$prefix-raster"})[0]
    $compute=@($rows|Where-Object{$_.source-eq$source-and$_.cell-eq"$prefix-compute"})[0]
    if($raster-and$compute){[pscustomobject]@{flow=$prefix;rasterMs=$raster.medianFrameMs;computeMs=$compute.medianFrameMs;computeOverRasterPercent=if($raster.medianFrameMs){100*($compute.medianFrameMs-$raster.medianFrameMs)/$raster.medianFrameMs}else{$null}}}
  })
  [pscustomobject]@{executableSha256MatchesTimestamp=$true;cells=$rows;computeOverRaster=$computeDeltas;traces=$offline.traces;compileAll=$offline.compileAll;startup=$offline.startup;browser=if($browser){[pscustomobject]@{machine=$browser.machine;architecture=$browser.architecture;runtimeIdentity=$browser.runtimeIdentity;cdpVersion=$browser.cdpVersion;gpuTimestampStatus=$browser.gpuTimestampStatus}}else{$null}}
}
function Load-Optional([string]$path){if(!$path){return $null};Get-Content $path -Raw|ConvertFrom-Json}
$x64=Get-Content $X64TimestampPath -Raw|ConvertFrom-Json;$arm64=Get-Content $Arm64TimestampPath -Raw|ConvertFrom-Json
if(!$x64.passed-or!$arm64.passed){throw 'Timestamp input did not pass'}
$x64Machine=Machine $x64 'x64';$arm64Machine=Machine $arm64 'ARM64'
$x64Offline=Load-Optional $X64OfflineAnalysisPath;$arm64Offline=Load-Optional $Arm64OfflineAnalysisPath
$x64Browser=Load-Optional $X64BrowserPath;$arm64Browser=Load-Optional $Arm64BrowserPath
if($x64Browser-and$arm64Browser-and$x64Browser.runtimeIdentity-ne$arm64Browser.runtimeIdentity){throw 'x64 and ARM64 browser runtime identities differ'}
if($x64Offline){$x64Machine|Add-Member throughput (Throughput $x64 $x64Offline $x64Browser)}
if($arm64Offline){$arm64Machine|Add-Member throughput (Throughput $arm64 $arm64Offline $arm64Browser)}
$result=[pscustomobject]@{schema=2;generatedUtc=[DateTime]::UtcNow.ToString('o');normalization='Each machine is normalized to its own native D3D12 or raster baseline; absolute x64/ARM64 time is not compared as architecture causality.';browserWebGpu=[pscustomobject]@{status=$BrowserStatus;reason=$BrowserReason};machines=@($x64Machine,$arm64Machine)}
$result|ConvertTo-Json -Depth 12|Set-Content $OutputPath -Encoding UTF8
Write-Output "Cross-machine relative overhead written: $OutputPath"
