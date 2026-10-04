[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Root,
    [Parameter(Mandatory=$true)][string]$ExpectedExecutableSha256,
    [ValidateRange(1,10)][int]$Repetitions=3,
    [ValidateRange(600,10000)][int]$Frames=1800,
    [ValidateRange(0,5000)][int]$WarmupFrames=300,
    [ValidateRange(320,7680)][int]$Width=1920,
    [ValidateRange(240,4320)][int]$Height=1080
)
$ErrorActionPreference='Stop'
$runtime=Join-Path $Root 'runtime'
$exe=Join-Path $runtime 'DayScene.exe'
$evidence=Join-Path $Root 'cpu-overhead-native-evidence'
if(!(Test-Path $exe)){throw "DayScene is missing: $exe"}
if((Get-FileHash $exe).Hash-ne$ExpectedExecutableSha256){throw 'Executable hash mismatch'}
if($WarmupFrames-ge$Frames){throw 'WarmupFrames must be less than Frames'}
if(Test-Path $evidence){throw "Use a fresh evidence directory: $evidence"}
if(Get-Process DayScene -ErrorAction SilentlyContinue){throw 'DayScene is already active'}
[void][IO.Directory]::CreateDirectory($evidence)
$cells=@(
    [pscustomobject]@{name='d3d12';api='d3d12';flow=$null},
    [pscustomobject]@{name='webgpu';api='webgpu';flow='wgsl'}
)
function Quote-Arguments([string[]]$Arguments){(($Arguments|ForEach-Object{'"'+$_.Replace('"','\"')+'"'})-join' ')}
function Invoke-Run($cell,[int]$repeat){
    $name="$($cell.name)-r$repeat";$directory=Join-Path $evidence "runs\$name";[void][IO.Directory]::CreateDirectory($directory)
    $arguments=@('--api',$cell.api,'--scene','1','--width',"$Width",'--height',"$Height",'--postProcessMode','compute','--culling','full',
        '--benchmark','--benchmarkFrames',"$($Frames+1)",'--benchmarkFixedDt','0.0166666667','--profileCpuOnly','--profileFrames',"$Frames",
        '--telemetry','--telemetryFrequencyFrames','0','--telemetryOutput',(Join-Path $directory 'telemetry.json'),'--logLevel','info','--logFile',(Join-Path $directory 'engine.log'))
    if($cell.flow){$arguments+=@('--shaderFlow',$cell.flow)}
    $process=Start-Process $exe -WorkingDirectory $runtime -ArgumentList (Quote-Arguments $arguments) -RedirectStandardOutput (Join-Path $directory 'stdout.log') -RedirectStandardError (Join-Path $directory 'stderr.log') -PassThru
    $null=$process.Handle
    try{
        if(!$process.WaitForExit(900000)){$process.Kill();$process.WaitForExit();throw "CPU capture timed out: $name"}
        if($process.ExitCode-ne0){throw "CPU capture failed ($($process.ExitCode)): $name"}
    }finally{if(!$process.HasExited){$process.Kill();$process.WaitForExit()};$process.Dispose()}
    $reports=@(Get-ChildItem $directory -Filter 'telemetry_*.json')
    if($reports.Count-ne1){throw "Telemetry report count is $($reports.Count): $name"}
    Copy-Item $reports[0].FullName (Join-Path $directory 'telemetry.json')
    $telemetry=Get-Content $reports[0].FullName -Raw|ConvertFrom-Json
    $measured=@($telemetry.frames|Where-Object{-not$_.startup-and$_.detailed-ne$false})
    if($telemetry.version-ne2-or$telemetry.droppedRecords-ne0-or$telemetry.unfinishedWriters-ne0-or$measured.Count-ne$Frames){throw "Incomplete CPU telemetry: $name"}
    $steady=@($measured|Where-Object{$_.frame-ge$WarmupFrames-and$_.frame-lt$Frames})
    if($steady.Count-ne($Frames-$WarmupFrames)){throw "Steady-state frame mismatch: $name"}
    $log=Get-Content (Join-Path $directory 'engine.log') -Raw
    if($log-match'\[ERROR\s*\]|Device lost|Queue submission failed'){throw "Renderer error: $name"}
    if($cell.api-eq'd3d12'-and$log-notmatch'profile=cs_6_[0-9] flow=dxc'){throw 'Native D3D12 did not use DXC/DXIL'}
    if($cell.api-eq'webgpu'-and$log-notmatch'provider=Dawn backend=D3D12'){throw 'Native WebGPU did not use Dawn/D3D12'}
    [pscustomobject]@{cell=$cell.name;repeat=$repeat;telemetry="runs/$name/telemetry.json";engineLog="runs/$name/engine.log";frames=$Frames;steadyFrames=$steady.Count;adapterId=$telemetry.adapterId;arguments=$arguments}
}
$result=[ordered]@{schema=1;startedUtc=[DateTime]::UtcNow.ToString('o');passed=$false;machine=$env:COMPUTERNAME;architecture=$env:PROCESSOR_ARCHITECTURE;
    executableSha256=(Get-FileHash $exe).Hash;repetitions=$Repetitions;frames=$Frames;warmupFrames=$WarmupFrames;width=$Width;height=$Height;postProcessMode='compute';shaderFlow='wgsl';runs=@()}
try{
    for($repeat=1;$repeat-le$Repetitions;$repeat++){$order=@($cells);if($repeat%2-eq0){[array]::Reverse($order)};foreach($cell in $order){$result.runs+=Invoke-Run $cell $repeat}}
    $result.passed=$true
}catch{$result.failure=$_.Exception.ToString()}
finally{$result.completedUtc=[DateTime]::UtcNow.ToString('o');$result|ConvertTo-Json -Depth 8|Set-Content (Join-Path $evidence 'result.json') -Encoding UTF8}
if(!$result.passed){throw $result.failure}
Write-Output "PASS: native CPU overhead matrix at $evidence"