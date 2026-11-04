# Backfill the sha256 of manifest entries whose bytes are not on this machine.
#
# PushCloudTextures.ps1 and PushCloudModels.ps1 can only hash a file they can read, so an entry whose
# object exists in R2 but whose local copy is gone (the sky cubemaps, the LUTs, the model library)
# stays size-only. That leaves the browser catalog with routes it cannot verify, because the catalog
# copies `sha256` straight from the manifest.
#
# This script reads the object back through the authenticated R2 API, hashes it, and writes the hash
# into the manifest. Nothing is re-uploaded: the object is the source of truth, the manifest learns
# what it already is.
#
#   pwsh scripts/BackfillCloudHashes.ps1                        # dry run, Textures/ only
#   pwsh scripts/BackfillCloudHashes.ps1 -Apply
#   pwsh scripts/BackfillCloudHashes.ps1 -Prefix Textures/ -Only Textures/sky
#   pwsh scripts/BackfillCloudHashes.ps1 -Manifest runtime_assets.json -Apply
#
# `runtime_assets.json` (the model manifest) holds only model entries and is published by
# PushCloudModels.ps1; the union `manifest.json` holds both kinds. Pick one with -Manifest.
[CmdletBinding()]
param(
    [string]$TokenFile = 'C:\Users\camus\Documents\cloud.txt',
    [string]$Token,
    [string]$AccountId = '66cd75c80eda50d1a2ae018f7f7136b6',
    [string]$TexturesBucket = 't850-textures',
    [string]$ModelsBucket = 't850-models',
    [string]$Manifest = 'manifest.json',
    [string[]]$Prefix = @('Textures/'),
    # Only hash entries under these key prefixes; empty means every prefix given above.
    [string[]]$Only = @(),
    [switch]$Apply,
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
$ApiRoot = 'https://api.cloudflare.com/client/v4'

if (-not $Token) {
    if ($env:CLOUDFLARE_API_TOKEN) { $Token = $env:CLOUDFLARE_API_TOKEN.Trim() }
    elseif (Test-Path -LiteralPath $TokenFile) { $Token = ([System.IO.File]::ReadAllText($TokenFile)).Trim() }
    else { throw "No Cloudflare API token: set CLOUDFLARE_API_TOKEN or create $TokenFile" }
}

function New-R2ObjectUri {
    param([string]$Bucket, [string]$Key)
    $segments = $Key.Split('/') | ForEach-Object { [System.Uri]::EscapeDataString($_) }
    return "$ApiRoot/accounts/$AccountId/r2/buckets/$Bucket/objects/$($segments -join '/')"
}

function Invoke-R2Request {
    param([string]$Method, [string]$Uri, [string]$ContentType, [string]$CacheControl, [string]$InFile, [string]$OutFile)
    $headers = @{ Authorization = "Bearer $Token" }
    if ($CacheControl) { $headers['Cache-Control'] = $CacheControl }
    $request = @{ Uri = $Uri; Method = $Method; Headers = $headers; TimeoutSec = $TimeoutSec
                  UseBasicParsing = $true; ErrorAction = 'Stop' }
    if ($InFile) { $request['InFile'] = $InFile }
    if ($OutFile) { $request['OutFile'] = $OutFile }
    if ($ContentType) { $request['ContentType'] = $ContentType }
    return Invoke-WebRequest @request
}

function Get-ManifestText {
    param([string]$Bucket)
    $response = Invoke-R2Request -Method Get -Uri (New-R2ObjectUri -Bucket $Bucket -Key $Manifest)
    return [System.Text.Encoding]::UTF8.GetString($response.RawContentStream.ToArray())
}

function Test-UnderPrefix {
    param([string]$Key, [string[]]$List)
    foreach ($prefix in $List) {
        if ($Key.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

# The union manifest is published to both buckets and must stay identical; runtime_assets.json only
# lives in the models bucket.
$buckets = if ($Manifest -eq 'runtime_assets.json') { @($ModelsBucket) } else { @($TexturesBucket, $ModelsBucket) }
$current = Get-ManifestText $buckets[0]
foreach ($bucket in $buckets | Select-Object -Skip 1) {
    if ((Get-ManifestText $bucket) -ne $current) { throw "$Manifest differs between the buckets; resolve the drift first" }
}

$manifestData = $current | ConvertFrom-Json
$entries = @($manifestData.assets)
$scope = if ($Only.Count -gt 0) { $Only } else { $Prefix }

$targets = @($entries | Where-Object {
    $key = [string]$_.key
    $key -and -not ($_.PSObject.Properties['sha256'] -and $_.sha256) -and (Test-UnderPrefix $key $scope)
})

Write-Host ''
Write-Host "T850 hash backfill — $Manifest" -ForegroundColor Cyan
Write-Host "  entries: $($entries.Count), without a hash under $($scope -join ', '): $($targets.Count)"
if ($targets.Count -eq 0) {
    Write-Host '  nothing to do'
    exit 0
}

$hashed = 0
$failed = 0
$updated = 0
foreach ($entry in $entries) {
    $key = [string]$entry.key
    if (-not ($targets | Where-Object { $_.key -eq $key })) { continue }
    $bucket = if ($entry.PSObject.Properties['bucket'] -and $entry.bucket) { [string]$entry.bucket } else { $TexturesBucket }
    $temporary = [System.IO.Path]::GetTempFileName()
    try {
        $null = Invoke-R2Request -Method Get -Uri (New-R2ObjectUri -Bucket $bucket -Key $key) -OutFile $temporary
        $hash = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant()
        $size = (Get-Item -LiteralPath $temporary).Length
        $entry | Add-Member -NotePropertyName sha256 -NotePropertyValue $hash -Force
        if (-not ($entry.PSObject.Properties['size'] -and [int64]$entry.size -gt 0)) {
            $entry | Add-Member -NotePropertyName size -NotePropertyValue $size -Force
        }
        $hashed++
        $updated++
        Write-Host "  $key -> $($hash.Substring(0, 16))… ($size bytes)"
    } catch {
        $failed++
        Write-Host "  FAIL $key : $($_.Exception.Message)" -ForegroundColor Red
    } finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "  hashed $hashed, failed $failed"
if (-not $Apply) {
    Write-Host ''
    Write-Host 'Dry run: nothing published. Pass -Apply to rewrite the manifest.' -ForegroundColor Yellow
    exit 0
}
if ($updated -eq 0) { Write-Host 'Nothing changed.'; exit 0 }

$newText = ($manifestData | ConvertTo-Json -Depth 20)
if (-not $newText.EndsWith("`n")) { $newText += "`n" }
$temporaryManifest = [System.IO.Path]::GetTempFileName()
try {
    [System.IO.File]::WriteAllText($temporaryManifest, $newText, [System.Text.UTF8Encoding]::new($false))
    foreach ($bucket in $buckets) {
        $null = Invoke-R2Request -Method Put -Uri (New-R2ObjectUri -Bucket $bucket -Key $Manifest) `
            -InFile $temporaryManifest -ContentType 'application/json' -CacheControl 'public, max-age=300'
        if ((Get-ManifestText $bucket) -ne $newText) { throw "$bucket read back a different $Manifest" }
        Write-Host "  published $Manifest to $bucket and verified byte-for-byte"
    }
} finally {
    Remove-Item -LiteralPath $temporaryManifest -Force -ErrorAction SilentlyContinue
}

if ($buckets.Count -gt 1) {
    $a = Get-ManifestText $buckets[0]
    $b = Get-ManifestText $buckets[1]
    if ($a -ne $b) { throw 'The buckets no longer hold the same manifest' }
}

Write-Host ''
Write-Host "PASS: $hashed entr(ies) gained a sha256 in $Manifest" -ForegroundColor Green
exit 0
