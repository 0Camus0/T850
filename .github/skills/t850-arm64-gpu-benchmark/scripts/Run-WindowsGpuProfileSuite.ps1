[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$CollectorPath,
    [Parameter(Mandatory = $true)][string]$ExpectedCollectorSha256,
    [ValidateRange(1, 5)][int]$TimestampRepetitions = 1,
    [ValidateRange(20, 5000)][int]$TimestampSamples = 120,
    [ValidateRange(1, 10)][int]$ThroughputRepetitions = 5,
    [ValidateRange(30, 10000)][int]$MeasuredFrames = 600,
    [ValidateRange(300, 20000)][int]$TraceFrames = 6000,
    [ValidateRange(60, 20000)][int]$HoldFrame = 3000,
    [ValidateRange(320, 7680)][int]$Width = 1920,
    [ValidateRange(180, 4320)][int]$Height = 1080,
    [switch]$SkipWpr
)

$ErrorActionPreference = 'Stop'
$runtime = Join-Path $Root 'runtime'
$exe = Join-Path $runtime 'DayScene.exe'
$resultPath = Join-Path $Root 'suite-result.json'

if (!(Test-Path $exe)) { throw "DayScene is missing: $exe" }
foreach ($directory in 'gpu-timestamp-evidence', 'offline-evidence', 'shader-cost-evidence') {
    if (Test-Path (Join-Path $Root $directory)) {
        throw "Use a fresh suite root; output already exists: $directory"
    }
}
if (Test-Path $resultPath) { throw "Use a fresh suite root: $resultPath" }

$timestampScript = Join-Path $PSScriptRoot 'Capture-Arm64GpuTimestampMatrix.ps1'
$offlineScript = Join-Path $PSScriptRoot 'Capture-Arm64GpuOfflineMatrix.ps1'
$shaderScript = Join-Path $PSScriptRoot 'Capture-Arm64ShaderCosts.ps1'

$manifest = [ordered]@{
    schema = 1
    startedUtc = [DateTime]::UtcNow.ToString('o')
    passed = $false
    machine = $env:COMPUTERNAME
    architecture = $env:PROCESSOR_ARCHITECTURE
    executableSha256 = (Get-FileHash $exe -Algorithm SHA256).Hash
    collectorPath = (Resolve-Path $CollectorPath).Path
    collectorSha256 = (Get-FileHash $CollectorPath -Algorithm SHA256).Hash
    skipWpr = [bool]$SkipWpr
    phases = @()
}

try {
    & $timestampScript -Root $Root -Repetitions $TimestampRepetitions `
        -Samples $TimestampSamples -HoldFrame $HoldFrame -Width $Width -Height $Height
    $timestampResult = Join-Path $Root 'gpu-timestamp-evidence\result.json'
    if (!(Test-Path $timestampResult) -or !(Get-Content $timestampResult -Raw | ConvertFrom-Json).passed) {
        throw 'GPU timestamp phase failed'
    }
    $manifest.phases += [pscustomobject]@{
        name = 'gpu-timestamps'
        result = $timestampResult
        sha256 = (Get-FileHash $timestampResult -Algorithm SHA256).Hash
    }

    $offlineArguments = @{
        Root = $Root
        Repetitions = $ThroughputRepetitions
        MeasuredFrames = $MeasuredFrames
        TraceFrames = $TraceFrames
        CollectorPath = $CollectorPath
        ExpectedCollectorSha256 = $ExpectedCollectorSha256
    }
    if ($SkipWpr) { $offlineArguments.SkipWpr = $true }
    & $offlineScript @offlineArguments
    $offlineResult = Join-Path $Root 'offline-evidence\result.json'
    if (!(Test-Path $offlineResult) -or !(Get-Content $offlineResult -Raw | ConvertFrom-Json).passed) {
        throw 'Offline throughput/trace phase failed'
    }
    $manifest.phases += [pscustomobject]@{
        name = 'offline-throughput-trace'
        result = $offlineResult
        sha256 = (Get-FileHash $offlineResult -Algorithm SHA256).Hash
    }

    & $shaderScript -Root $Root
    $shaderResult = Join-Path $Root 'shader-cost-evidence\result.json'
    if (!(Test-Path $shaderResult) -or !(Get-Content $shaderResult -Raw | ConvertFrom-Json).passed) {
        throw 'Shader/startup phase failed'
    }
    $manifest.phases += [pscustomobject]@{
        name = 'shader-costs'
        result = $shaderResult
        sha256 = (Get-FileHash $shaderResult -Algorithm SHA256).Hash
    }

    $manifest.passed = $true
} catch {
    $manifest.failure = $_.Exception.ToString()
} finally {
    $manifest.completedUtc = [DateTime]::UtcNow.ToString('o')
    $manifest | ConvertTo-Json -Depth 8 | Set-Content $resultPath -Encoding UTF8
}

if (!$manifest.passed) { throw $manifest.failure }
Write-Output "PASS: Windows GPU profile suite at $Root"
