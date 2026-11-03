# One-off repair: remove duplicate entries from the shared union manifest.json.
#
# PushCloudTextures.ps1 keyed its merge lookup on the textures bucket unconditionally, so a model
# file (bucket t850-models) never matched its existing entry and every run appended another copy.
# The script now refuses to run on a manifest that already has duplicates ("Manifest has duplicate
# entry"), so this repair has to happen once, out of band.
#
#   .\scripts\RepairCloudManifestDuplicates.ps1                 # dry run
#   .\scripts\RepairCloudManifestDuplicates.ps1 -Apply          # publish the repaired manifest
#
# It only removes exact duplicates of (bucket, key), keeping the FIRST occurrence, and recomputes
# assetCount/modelCount/textureCount/totalSize. Nothing else in the manifest is touched.
[CmdletBinding()]
param(
    [string]$TokenFile = 'C:\Users\camus\Documents\cloud.txt',
    [string]$Token,
    [string]$AccountId = '66cd75c80eda50d1a2ae018f7f7136b6',
    [string]$TexturesBucket = 't850-textures',
    [string]$ModelsBucket = 't850-models',
    [string]$ManifestKey = 'manifest.json',
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

function Invoke-R2Request {
    param([string]$Method, [string]$Uri, [string]$ContentType, [string]$CacheControl, [string]$InFile)
    $headers = @{ Authorization = "Bearer $Token" }
    if ($CacheControl) { $headers['Cache-Control'] = $CacheControl }
    $request = @{ Uri = $Uri; Method = $Method; Headers = $headers; TimeoutSec = $TimeoutSec
                  UseBasicParsing = $true; ErrorAction = 'Stop' }
    if ($InFile) { $request['InFile'] = $InFile }
    if ($ContentType) { $request['ContentType'] = $ContentType }
    return Invoke-WebRequest @request
}

function New-R2ObjectUri {
    param([string]$Bucket, [string]$Key)
    $segments = $Key.Split('/') | ForEach-Object { [System.Uri]::EscapeDataString($_) }
    return "$ApiRoot/accounts/$AccountId/r2/buckets/$Bucket/objects/$($segments -join '/')"
}

function Get-ManifestText {
    param([string]$Bucket)
    $response = Invoke-R2Request -Method Get -Uri (New-R2ObjectUri -Bucket $Bucket -Key $ManifestKey)
    return [System.Text.Encoding]::UTF8.GetString($response.RawContentStream.ToArray())
}

# The two buckets must agree before anything is rewritten.
$texturesText = Get-ManifestText $TexturesBucket
$modelsText = Get-ManifestText $ModelsBucket
if ($texturesText -ne $modelsText) {
    throw "manifest.json differs between '$TexturesBucket' and '$ModelsBucket'; resolve the drift first"
}

$manifest = $texturesText | ConvertFrom-Json
$entries = @($manifest.assets)
$seen = @{}
$keep = New-Object System.Collections.Generic.List[object]
$removed = 0
foreach ($entry in $entries) {
    $bucket = if ($entry.PSObject.Properties['bucket']) { [string]$entry.bucket } else { '' }
    $compound = "$bucket|$([string]$entry.key)"
    if ($seen.ContainsKey($compound)) {
        $removed++
        Write-Host "  duplicate: $compound"
        continue
    }
    $seen[$compound] = $true
    $keep.Add($entry)
}

if ($removed -eq 0) {
    Write-Host "No duplicate entries in $ManifestKey ($($entries.Count) assets). Nothing to do."
    exit 0
}

$newTop = [ordered]@{}
foreach ($property in $manifest.PSObject.Properties) {
    switch ($property.Name) {
        'assetCount'   { $newTop['assetCount'] = $keep.Count }
        'generatedAtUtc' { $newTop['generatedAtUtc'] = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') }
        'assets'       { $newTop['assets'] = $keep.ToArray() }
        default        { $newTop[$property.Name] = $property.Value }
    }
}
$modelCount = 0; $textureCount = 0; $totalSize = [int64]0
foreach ($entry in $keep) {
    if ([string]$entry.kind -eq 'model') { $modelCount++ } else { $textureCount++ }
    $totalSize += [int64]$entry.size
}
if ($newTop.Contains('modelCount')) { $newTop['modelCount'] = $modelCount }
if ($newTop.Contains('textureCount')) { $newTop['textureCount'] = $textureCount }
if ($newTop.Contains('totalSize')) { $newTop['totalSize'] = $totalSize }
if ($newTop.Contains('totalSizeMB')) { $newTop['totalSizeMB'] = [Math]::Round($totalSize / 1MB, 2) }
$newTop['manifestVersion'] = [int]$manifest.manifestVersion + 1

$newText = ($newTop | ConvertTo-Json -Depth 10)
if (-not $newText.EndsWith("`n")) { $newText += "`n" }

Write-Host ''
Write-Host "  entries: $($entries.Count) -> $($keep.Count) ($removed duplicate(s) removed)"
Write-Host "  version: $($manifest.manifestVersion) -> $($newTop['manifestVersion'])"
Write-Host "  models=$modelCount textures=$textureCount totalSizeMB=$($newTop['totalSizeMB'])"

if (-not $Apply) {
    Write-Host ''
    Write-Host 'Dry run: nothing published. Pass -Apply to rewrite the manifest in both buckets.' -ForegroundColor Yellow
    exit 0
}

$temporary = [System.IO.Path]::GetTempFileName()
try {
    [System.IO.File]::WriteAllText($temporary, $newText, [System.Text.UTF8Encoding]::new($false))
    foreach ($bucket in @($TexturesBucket, $ModelsBucket)) {
        $null = Invoke-R2Request -Method Put -Uri (New-R2ObjectUri -Bucket $bucket -Key $ManifestKey) `
            -InFile $temporary -ContentType 'application/json' -CacheControl 'public, max-age=300'
        $stored = Get-ManifestText $bucket
        if ($stored -ne $newText) { throw "$bucket read back a different manifest" }
        Write-Host "  published $ManifestKey to $bucket and verified byte-for-byte"
    }
} finally {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
}

$finalTextures = Get-ManifestText $TexturesBucket
$finalModels = Get-ManifestText $ModelsBucket
if ($finalTextures -ne $finalModels) { throw 'The two buckets no longer hold the same manifest' }
Write-Host ''
Write-Host "PASS: $ManifestKey repaired, $($keep.Count) assets, both buckets identical" -ForegroundColor Green
exit 0
