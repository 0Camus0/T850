[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$ThroughputResultPath,
  [Parameter(Mandatory=$true)][string]$TraceResultPath,
  [Parameter(Mandatory=$true)][string]$OutputPath
)
$ErrorActionPreference='Stop'
$throughput=Get-Content $ThroughputResultPath -Raw|ConvertFrom-Json
$trace=Get-Content $TraceResultPath -Raw|ConvertFrom-Json
$cells=@('d3d12-raster','d3d12-compute','wgsl-raster','wgsl-compute','spirv-raster','spirv-compute')
if($throughput.passed){throw 'Throughput source unexpectedly passed; use it directly.'}
if([string]$throughput.failure-notmatch'xperf\.exe'){throw 'Throughput source did not fail solely at remote xperf validation.'}
if(!$trace.passed-or!$trace.traceOnly-or!$trace.deferEtlValidation){throw 'Trace source is not a passed deferred-validation trace-only capture.'}
if($throughput.machine-ne$trace.machine-or$throughput.architecture-ne$trace.architecture){throw 'Evidence machine or architecture differs.'}
if($throughput.executableSha256-ne$trace.executableSha256){throw 'Evidence executable hashes differ.'}
if($throughput.collectorSha256-ne$trace.collectorSha256){throw 'Evidence collector hashes differ.'}
if(@($throughput.runs).Count -ne 30 -or @($throughput.zeroPresent).Count -ne 2){throw 'Throughput source is incomplete.'}
foreach($cell in $cells){
  $runs=@($throughput.runs|Where-Object cell -eq $cell)
  if($runs.Count -ne 5 -or ($runs.frames|Measure-Object -Sum).Sum -ne 3000 -or @($runs|Where-Object presents -ne 0).Count){throw "Invalid throughput cell: $cell"}
}
if(@($trace.runs).Count -or @($trace.zeroPresent).Count -or @($trace.traces).Count -ne 6){throw 'Trace source contains an unexpected phase shape.'}
foreach($cell in $cells){
  $entry=@($trace.traces|Where-Object cell -eq $cell)
  if($entry.Count -ne 1 -or !$entry[0].wprCaptured -or !$entry[0].etlValidationDeferred -or $entry[0].frames -ne 6000 -or $entry[0].presents -ne 0){throw "Invalid trace cell: $cell"}
}
$throughput.passed=$true
$throughput.failure=$null
$throughput.traces=$trace.traces
$throughput.skipWpr=$false
$throughput|Add-Member deferEtlValidation $true -Force
$throughput.completedUtc=$trace.completedUtc
$throughput|Add-Member mergedEvidence ([pscustomobject]@{
  throughputResultSha256=(Get-FileHash $ThroughputResultPath).Hash
  traceResultSha256=(Get-FileHash $TraceResultPath).Hash
  reason='Throughput completed before remote xperf absence stopped the original trace phase; traces were recaptured with local xperf validation deferred.'
}) -Force
[IO.Directory]::CreateDirectory((Split-Path $OutputPath -Parent))|Out-Null
$throughput|ConvertTo-Json -Depth 12|Set-Content $OutputPath -Encoding UTF8
Write-Output "Merged offline evidence written: $OutputPath"