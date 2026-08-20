param(
    [string]$ReferenceRoot,
    [string]$CandidateRoot,
    [int]$Tolerance = 0,
    [double]$MaximumDiffPercent = 0.0,
    [double]$MaximumAverageChannelDelta = 0.0,
    [string[]]$Cases = @(),
    [ValidateSet("d3d11", "d3d12", "gl", "vulkan")]
    [string[]]$Apis = @(),
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent $PSScriptRoot
$baselineRoot = Join-Path $sourceRoot "VisualBaselines"
if (-not $ReferenceRoot) { $ReferenceRoot = Join-Path $baselineRoot "reference" }
if (-not $CandidateRoot) { $CandidateRoot = Join-Path $baselineRoot "candidate" }
if (-not $OutputPath) { $OutputPath = Join-Path $baselineRoot "comparison.json" }

$ReferenceRoot = [IO.Path]::GetFullPath($ReferenceRoot)
$CandidateRoot = [IO.Path]::GetFullPath($CandidateRoot)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$compareScript = Join-Path $PSScriptRoot "compare_dumps.py"

foreach ($requiredPath in @(
    (Join-Path $ReferenceRoot "manifest.json"),
    (Join-Path $CandidateRoot "manifest.json"),
    $compareScript)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required baseline input not found: $requiredPath"
    }
}

$referenceManifest = Get-Content -LiteralPath (Join-Path $ReferenceRoot "manifest.json") -Raw | ConvertFrom-Json
$candidateManifest = Get-Content -LiteralPath (Join-Path $CandidateRoot "manifest.json") -Raw | ConvertFrom-Json
$results = New-Object System.Collections.Generic.List[object]
$failureCount = 0

foreach ($reference in @($referenceManifest.Captures)) {
    if (($Cases.Count -gt 0 -and $reference.Case -notin $Cases) -or
        ($Apis.Count -gt 0 -and $reference.Api -notin $Apis)) {
        continue
    }
    if ($reference.Status -ne "captured") {
        $results.Add([pscustomobject]@{
            Case = $reference.Case
            Api = $reference.Api
            Status = "not_comparable"
            ReferenceStatus = $reference.Status
        })
        continue
    }

    $candidate = @($candidateManifest.Captures | Where-Object {
        $_.Case -eq $reference.Case -and $_.Api -eq $reference.Api
    }) | Select-Object -First 1
    if (-not $candidate -or $candidate.Status -ne "captured") {
        $failureCount++
        $results.Add([pscustomobject]@{
            Case = $reference.Case
            Api = $reference.Api
            Status = "missing_candidate"
            CandidateStatus = if ($candidate) { $candidate.Status } else { "missing" }
        })
        continue
    }

    $referenceDir = Join-Path (Join-Path $ReferenceRoot $reference.Case) $reference.Api
    $candidateDir = Join-Path (Join-Path $CandidateRoot $candidate.Case) $candidate.Api
    $json = & python $compareScript $referenceDir $candidateDir --tolerance $Tolerance --json
    if ($LASTEXITCODE -ne 0) {
        throw "compare_dumps.py failed for $($reference.Case)/$($reference.Api)"
    }
    $comparison = $json | ConvertFrom-Json
    $backBuffer = @($comparison.comparisons | Where-Object target -eq "RT_Dump_BackBuffer.ppm") |
        Select-Object -First 1
    if (-not $backBuffer) {
        throw "Backbuffer comparison missing for $($reference.Case)/$($reference.Api)"
    }

    $passed = $backBuffer.status -ne "size_mismatch" -and
        $backBuffer.diff_percent -le $MaximumDiffPercent -and
        ($backBuffer.diff_pixels -eq 0 -or
            $backBuffer.avg_channel_delta -le $MaximumAverageChannelDelta)
    if (-not $passed) { $failureCount++ }
    $results.Add([pscustomobject]@{
        Case = $reference.Case
        Api = $reference.Api
        Status = if ($passed) { "passed" } else { "different" }
        DiffPixels = $backBuffer.diff_pixels
        DiffPercent = $backBuffer.diff_percent
        MaximumChannelDelta = $backBuffer.max_channel_delta
        AverageChangedPixelDelta = $backBuffer.avg_changed_pixel_delta
        AverageChannelDelta = $backBuffer.avg_channel_delta
        AverageLuminanceDelta = $backBuffer.avg_luminance_delta
    })
}

$report = [ordered]@{
    SchemaVersion = 1
    ComparedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    ReferenceRoot = $ReferenceRoot
    CandidateRoot = $CandidateRoot
    Tolerance = $Tolerance
    MaximumDiffPercent = $MaximumDiffPercent
    MaximumAverageChannelDelta = $MaximumAverageChannelDelta
    ReferenceCommit = $referenceManifest.GitCommit
    CandidateCommit = $candidateManifest.GitCommit
    Failures = $failureCount
    Results = $results
}
New-Item -ItemType Directory -Path (Split-Path -Parent $OutputPath) -Force | Out-Null
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8

$results | Format-Table Case, Api, Status, DiffPercent, MaximumChannelDelta, AverageChannelDelta -AutoSize
Write-Host "[baseline] Comparison failures=$failureCount"
Write-Host "[baseline] Report: $OutputPath"
if ($failureCount -gt 0) { exit 1 }
exit 0