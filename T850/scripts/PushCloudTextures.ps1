<#
.SYNOPSIS
    Uploads texture assets to Cloudflare R2 and keeps the shared manifest.json in step.

.DESCRIPTION
    The T850 cloud asset model is:

      * two public R2 buckets, t850-models and t850-textures, each holding an identical
        copy of one union manifest (manifest.json). Each asset entry names its bucket,
        so every consumer filters the manifest by kind.
      * the manifest lives only in the cloud: there is no committed local copy of it.
        This script therefore treats the downloaded manifest as the source of truth,
        changes only the entries for the files it was pointed at, and never renumbers or
        reorders anything else. A PowerShell JSON round trip of the current manifest is
        byte-identical (verified), so untouched entries do not churn.
      * downloaders (scripts\DownloadTextures.ps1, the Android and Steam Deck clients,
        and the Pages worker) read the manifest and verify an entry's size, plus its
        sha256 when the entry carries one. New entries written by this script carry a
        sha256; pre-existing entries are left exactly as they are.

    The pipeline follows the "objects before manifest, manifest before deletes" rule:

      1. scan the local files under -Prefix and hash them
      2. download manifest.json from both buckets and abort if the copies have drifted
      3. merge the entries for the scanned files only, recompute the aggregates
      4. (dry run stops here and prints what it would do)
      5. upload changed objects, verify each against the ETag returned by R2
      6. publish the refreshed manifest to both buckets and verify both copies
      7. (with -Prune) delete objects that no longer exist locally, then publish the
         manifest that no longer references them

    Without -Apply nothing is written: no PUT, no DELETE. The token is read from a file
    outside the repository (or $env:CLOUDFLARE_API_TOKEN) and is never printed.

.PARAMETER Prefix
    Asset-relative prefixes to sync, e.g. Textures\Ball. Default: Textures/Ball.

.PARAMETER Prune
    Delete objects and manifest entries under the scanned prefixes that no longer exist
    locally. Off by default; the dry run always reports what pruning would remove.

.PARAMETER Force
    Re-upload every scanned file even when the manifest says it is already current.

.PARAMETER Apply
    Perform the uploads. Without it the script is a dry run.

.PARAMETER CandidatePath
    Write the manifest that would be published to this path (useful with a dry run:
    the full candidate can be reviewed and diffed before anything is uploaded).

.EXAMPLE
    .\scripts\PushCloudTextures.ps1
    Dry run for Textures/Ball: prints the uploads, entry changes and prune candidates.

.EXAMPLE
    .\scripts\PushCloudTextures.ps1 -Prefix Textures\Ball -Apply
    Uploads the changed files and republishes manifest.json to both buckets.
#>
[CmdletBinding()]
param(
    [string]$RootDir,
    [string]$AssetRoot,
    [string[]]$Prefix = @('Textures/Ball'),
    [string]$TokenFile = 'C:\Users\camus\Documents\cloud.txt',
    [string]$Token,
    [string]$AccountId = '66cd75c80eda50d1a2ae018f7f7136b6',
    [string]$TexturesBucket = 't850-textures',
    [string]$ModelsBucket = 't850-models',
    [string]$ManifestKey = 'manifest.json',
    [switch]$Prune,
    [switch]$Force,
    [switch]$Apply,
    [string]$CandidatePath,
    [int]$Retries = 3,
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ApiRoot = 'https://api.cloudflare.com/client/v4'

# Immutable caching is only safe when a key never changes content. Texture keys are
# content-addressed in practice (a changed texture is usually a new file), and the
# manifest is deliberately short-lived so consumers pick up a publish quickly.
$TextureCacheControl = 'public, max-age=31536000, immutable'
$ManifestCacheControl = 'public, max-age=300'
$ManifestContentType = 'application/json'

$ContentTypes = @{
    '.png'   = 'image/png'
    '.jpg'   = 'image/jpeg'
    '.jpeg'  = 'image/jpeg'
    '.dds'   = 'image/vnd-ms.dds'
    '.t8ibl' = 'application/octet-stream'
    '.ktx'   = 'application/octet-stream'
    '.json'  = 'application/json'
    '.bin'   = 'application/octet-stream'
}

function Write-Step { param([string]$Message) Write-Host "[T850] $Message" }
function Write-Warn { param([string]$Message) Write-Host "[T850] WARNING: $Message" -ForegroundColor Yellow }

# ---------------------------------------------------------------- token and paths

if (-not $RootDir) { $RootDir = Split-Path -Parent $PSScriptRoot }
if (-not $AssetRoot) { $AssetRoot = Join-Path $RootDir 'Assets' }
if (-not (Test-Path -LiteralPath $AssetRoot)) {
    throw "Asset root not found: $AssetRoot (pass -AssetRoot)"
}

if (-not $Token) {
    if ($env:CLOUDFLARE_API_TOKEN) {
        $Token = $env:CLOUDFLARE_API_TOKEN.Trim()
    } elseif (Test-Path -LiteralPath $TokenFile) {
        # Get-Content -Raw keeps any BOM; Trim() removes it together with whitespace.
        $Token = ([System.IO.File]::ReadAllText($TokenFile)).Trim()
    } else {
        throw "No Cloudflare API token: set CLOUDFLARE_API_TOKEN or create $TokenFile"
    }
}
if (-not $Token) { throw 'Cloudflare API token is empty' }
if ($Token -match '\s') { throw 'Cloudflare API token contains whitespace' }

$Headers = @{ Authorization = "Bearer $Token" }
$Prefixes = @($Prefix | ForEach-Object { $_.Replace('\', '/').Trim('/') } | Where-Object { $_ })
if ($Prefixes.Count -eq 0) { throw 'No sync prefix given' }

$publicBaseTextures = $null
$publicBaseModels = $null

# ---------------------------------------------------------------- helpers

function ConvertTo-R2Key {
    param([string]$RelativePath)
    return ($RelativePath.Replace('\', '/').TrimStart('/'))
}

function ConvertTo-R2UrlPath {
    param([string]$Key)
    # Keys keep their slashes; every other unsafe character is percent-encoded. The
    # existing manifest encodes brackets the same way ("%281%29").
    $segments = $Key.Split('/') | ForEach-Object { [System.Uri]::EscapeDataString($_) }
    return ($segments -join '/')
}

function New-R2ObjectUri {
    param([string]$Bucket, [string]$Key)
    return "$ApiRoot/accounts/$AccountId/r2/buckets/$Bucket/objects/$(ConvertTo-R2UrlPath $Key)"
}

function Invoke-R2Request {
    param(
        [Parameter(Mandatory)][string]$Method,
        [Parameter(Mandatory)][string]$Uri,
        [string]$ContentType,
        [string]$CacheControl,
        [string]$InFile,
        [byte[]]$Body,
        [string]$OutFile
    )
    $headers = @{ Authorization = "Bearer $Token" }
    if ($CacheControl) { $headers['Cache-Control'] = $CacheControl }
    $attempt = 0
    while ($true) {
        $attempt++
        try {
            $request = @{
                Uri = $Uri
                Method = $Method
                Headers = $headers
                TimeoutSec = $TimeoutSec
                UseBasicParsing = $true
                ErrorAction = 'Stop'
            }
            if ($Body) { $request['Body'] = $Body }
            if ($InFile) { $request['InFile'] = $InFile }
            if ($ContentType) { $request['ContentType'] = $ContentType }
            if ($OutFile) { $request['OutFile'] = $OutFile }
            return Invoke-WebRequest @request
        } catch {
            $status = 0
            if ($_.Exception.Response -and $_.Exception.Response.StatusCode) {
                $status = [int]$_.Exception.Response.StatusCode
            }
            if ($status -eq 401 -or $status -eq 403) {
                throw "Cloudflare rejected the token (HTTP $status): $($_.Exception.Message)"
            }
            if ($attempt -gt $Retries -or ($status -ge 400 -and $status -lt 500 -and $status -ne 429)) {
                throw "HTTP $status on $Method $Uri after $attempt attempt(s): $($_.Exception.Message)"
            }
            Start-Sleep -Seconds ([Math]::Min(10, [Math]::Pow(2, $attempt)))
        }
    }
}

function Get-R2ObjectText {
    param([string]$Bucket, [string]$Key)
    $response = Invoke-R2Request -Method Get -Uri (New-R2ObjectUri -Bucket $Bucket -Key $Key)
    return [System.Text.Encoding]::UTF8.GetString($response.RawContentStream.ToArray())
}

function Get-R2ObjectBytes {
    param([string]$Bucket, [string]$Key)
    $response = Invoke-R2Request -Method Get -Uri (New-R2ObjectUri -Bucket $Bucket -Key $Key)
    return $response.RawContentStream.ToArray()
}

function Get-Sha256Hex {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-ContentTypeFor {
    param([string]$Path)
    $extension = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
    if ($ContentTypes.ContainsKey($extension)) { return $ContentTypes[$extension] }
    throw "No R2 content type mapped for '$Path' (extension '$extension')"
}

function Format-SizeMB {
    param([int64]$Bytes)
    return [Math]::Round($Bytes / 1MB, 2)
}

function New-ManifestEntry {
    param(
        [string]$AssetRoot,
        [string]$Bucket,
        [string]$ContentType,
        [string]$Key,
        [string]$Kind,
        [string]$LocalRelativePath,
        [int64]$Size,
        [string]$Sha256,
        [string]$PublicBaseUrl
    )
    # Field order matches the existing manifest: alphabetical, with sha256 between
    # localRelativePath and size.
    $entry = [ordered]@{
        assetRoot         = $AssetRoot
        bucket            = $Bucket
        contentType       = $ContentType
        key               = $Key
        kind              = $Kind
        localPath         = "$AssetRoot/$LocalRelativePath"
        localRelativePath = $LocalRelativePath
    }
    if ($Sha256) { $entry['sha256'] = $Sha256.ToLowerInvariant() }
    $entry['size'] = $Size
    $entry['sizeMB'] = (Format-SizeMB $Size)
    $entry['url'] = "$PublicBaseUrl/$(ConvertTo-R2UrlPath $Key)"
    return [pscustomobject]$entry
}

function Get-EntryIndex {
    param([object[]]$Entries)
    # Assets are unique by (bucket, key): the two buckets may legitimately hold the same
    # relative key (a model and a texture of the same name).
    $index = @{}
    for ($i = 0; $i -lt $Entries.Count; $i++) {
        $key = [string]$Entries[$i].key
        if (-not $key) { continue }
        $bucket = if ($Entries[$i].PSObject.Properties['bucket']) { [string]$Entries[$i].bucket } else { '' }
        $compound = "$bucket|$key"
        if ($index.ContainsKey($compound)) { throw "Manifest has duplicate entry '$compound'" }
        $index[$compound] = $i
    }
    return $index
}

function Test-KeyUnderPrefixes {
    param([string]$RelativePath)
    foreach ($prefix in $Prefixes) {
        if ($RelativePath -like "$prefix/*") { return $true }
    }
    return $false
}

function Test-ManifestShape {
    param($Manifest)
    if (-not $Manifest) { throw 'Manifest is empty' }
    if (-not $Manifest.PSObject.Properties['assets']) { throw 'Manifest has no assets array' }
    if ([int]$Manifest.schemaVersion -ne 1) { throw "Unsupported manifest schemaVersion '$($Manifest.schemaVersion)'" }
    foreach ($entry in $Manifest.assets) {
        foreach ($field in @('bucket', 'contentType', 'key', 'kind', 'localRelativePath', 'size', 'url')) {
            if (-not $entry.PSObject.Properties[$field]) { throw "Manifest entry '$($entry.key)' is missing '$field'" }
        }
    }
}

# ---------------------------------------------------------------- scan local files

Write-Step "Scanning $(Join-Path $RootDir 'Assets') under prefix(es): $($Prefixes -join ', ')"

$files = New-Object System.Collections.Generic.List[object]
$seenKeys = @{}
foreach ($prefix in $Prefixes) {
    $localDir = Join-Path $AssetRoot ($prefix -replace '/', '\')
    if (-not (Test-Path -LiteralPath $localDir)) {
        Write-Warn "Prefix '$prefix' has no local directory: $localDir"
        continue
    }
    $found = Get-ChildItem -LiteralPath $localDir -Recurse -File -Force |
        Where-Object { $_.Name -notlike '.*' -and $_.Length -gt 0 }
    foreach ($file in $found) {
        $relative = $file.FullName.Substring($AssetRoot.Length).TrimStart('\', '/').Replace('\', '/')
        $key = ConvertTo-R2Key $relative
        if ($seenKeys.ContainsKey($key)) { throw "Two local files map to the same R2 key '$key'" }
        $seenKeys[$key] = $true
        $files.Add([pscustomobject]@{
            Key               = $key
            LocalRelativePath = $relative
            FullName          = $file.FullName
            Size              = [int64]$file.Length
            Sha256            = (Get-Sha256Hex $file.FullName)
            ContentType       = (Get-ContentTypeFor $file.FullName)
        })
    }
}
if ($files.Count -eq 0) {
    throw "No files found under the requested prefixes; refusing to touch the manifest"
}
$totalBytes = ($files | Measure-Object -Property Size -Sum).Sum
Write-Step ("Found {0} file(s), {1} MB" -f $files.Count, [Math]::Round($totalBytes / 1MB, 2))

# ---------------------------------------------------------------- read the cloud manifest

Write-Step 'Downloading manifest.json from both buckets'
$texturesManifestBytes = Get-R2ObjectBytes -Bucket $TexturesBucket -Key $ManifestKey
$modelsManifestBytes = Get-R2ObjectBytes -Bucket $ModelsBucket -Key $ManifestKey
$texturesHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($texturesManifestBytes)) -Algorithm SHA256).Hash
$modelsHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($modelsManifestBytes)) -Algorithm SHA256).Hash
if ($texturesHash -ne $modelsHash) {
    throw "manifest.json differs between '$TexturesBucket' ($texturesHash) and '$ModelsBucket' ($modelsHash). Resolve the drift before pushing."
}
Write-Step "Both buckets hold the same manifest (sha256 $($texturesHash.Substring(0, 16))…)"

$manifestText = [System.Text.Encoding]::UTF8.GetString($texturesManifestBytes)
$manifest = $manifestText | ConvertFrom-Json
Test-ManifestShape $manifest

$manifestAssetRoot = [string]$manifest.assetRoot
if (-not $manifestAssetRoot) { $manifestAssetRoot = 'T850/T850/Assets' }
$publicBaseTextures = [string]$manifest.buckets.$TexturesBucket.publicBaseUrl
$publicBaseModels = [string]$manifest.buckets.$ModelsBucket.publicBaseUrl
if (-not $publicBaseTextures) { throw "Manifest has no publicBaseUrl for '$TexturesBucket'" }

$entries = @($manifest.assets)
$entryIndex = Get-EntryIndex $entries

# ---------------------------------------------------------------- merge

$toUpload = New-Object System.Collections.Generic.List[object]
$newEntries = 0
$updatedEntries = 0
$unchanged = 0
$sha256Backfilled = 0

foreach ($file in $files) {
    $compound = "$TexturesBucket|$($file.Key)"
    $existingIndex = if ($entryIndex.ContainsKey($compound)) { $entryIndex[$compound] } else { -1 }
    if ($existingIndex -ge 0) {
        $existing = $entries[$existingIndex]
        $existingSha = if ($existing.PSObject.Properties['sha256']) { [string]$existing.sha256 } else { '' }
        $sizeMatches = ([int64]$existing.size -eq $file.Size)
        $contentMatches = if ($existingSha) { $existingSha -ieq $file.Sha256 } else { $sizeMatches }
        # Minimal-touch update: a published entry keeps its contentType, url and path
        # fields exactly as they are; only the facts that describe the bytes change.
        $newEntry = [ordered]@{}
        foreach ($property in $existing.PSObject.Properties) { $newEntry[$property.Name] = $property.Value }
        $newEntry['size'] = $file.Size
        $newEntry['sizeMB'] = (Format-SizeMB $file.Size)
        if ($existingSha) { $newEntry['sha256'] = $file.Sha256.ToLowerInvariant() }
        else { $sha256Backfilled++ }                    # stays size-only, as before
        $newEntry = [pscustomobject]$newEntry
        $entryJsonBefore = $existing | ConvertTo-Json -Depth 10 -Compress
        $entryJsonAfter = $newEntry | ConvertTo-Json -Depth 10 -Compress
        if ($entryJsonBefore -ne $entryJsonAfter) {
            $entries[$existingIndex] = $newEntry
            $updatedEntries++
        }
        if ($Force -or -not $contentMatches) {
            $toUpload.Add($file)
        } else {
            $unchanged++
        }
    } else {
        $newEntry = New-ManifestEntry -AssetRoot $manifestAssetRoot -Bucket $TexturesBucket `
            -ContentType $file.ContentType -Key $file.Key -Kind 'texture' `
            -LocalRelativePath $file.LocalRelativePath -Size $file.Size -Sha256 $file.Sha256 `
            -PublicBaseUrl $publicBaseTextures
        $entries += $newEntry
        $entryIndex[$compound] = $entries.Count - 1
        $newEntries++
        $toUpload.Add($file)
    }
}

# Entries under a synced prefix whose file disappeared locally.
$staleEntries = New-Object System.Collections.Generic.List[object]
foreach ($entry in $entries) {
    $relative = [string]$entry.localRelativePath
    if (-not (Test-KeyUnderPrefixes $relative)) { continue }
    if ($seenKeys.ContainsKey([string]$entry.key)) { continue }
    $staleEntries.Add($entry)
}
if ($staleEntries.Count -gt 0 -and -not $Prune) {
    Write-Warn ("{0} manifest entr(ies) under the synced prefix have no local file; use -Prune to remove them" -f $staleEntries.Count)
}

# ---------------------------------------------------------------- aggregates

$modelCount = 0
$textureCount = 0
$totalSize = [int64]0
foreach ($entry in $entries) {
    if ([string]$entry.kind -eq 'model') { $modelCount++ } else { $textureCount++ }
    $totalSize += [int64]$entry.size
}

$newTop = [ordered]@{}
foreach ($property in $manifest.PSObject.Properties) {
    switch ($property.Name) {
        'assetCount'   { $newTop['assetCount'] = $entries.Count }
        'modelCount'   { $newTop['modelCount'] = $modelCount }
        'textureCount' { $newTop['textureCount'] = $textureCount }
        'totalSize'    { $newTop['totalSize'] = $totalSize }
        'totalSizeMB'  { $newTop['totalSizeMB'] = (Format-SizeMB $totalSize) }
        'generatedAtUtc' { $newTop['generatedAtUtc'] = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') }
        'assets'       { $newTop['assets'] = $entries }
        default        { $newTop[$property.Name] = $property.Value }
    }
    if ($property.Name -eq 'manifestUrls' -and -not $manifest.PSObject.Properties['manifestVersion']) {
        $newTop['manifestVersion'] = [int]1
    }
}
if ($newTop.Contains('manifestVersion')) {
    $previousVersion = if ($manifest.PSObject.Properties['manifestVersion']) { [int]$manifest.manifestVersion } else { 0 }
    $newTop['manifestVersion'] = $previousVersion + 1
}

$newManifest = [pscustomobject]$newTop
$newJson = ($newManifest | ConvertTo-Json -Depth 20)
$newText = $newJson + "`n"
$tempPath = Join-Path ([System.IO.Path]::GetTempPath()) ("t850-manifest-{0}.json" -f [guid]::NewGuid().ToString('N'))
[System.IO.File]::WriteAllText($tempPath, $newText, [System.Text.UTF8Encoding]::new($false))

# Re-read what we are about to publish: a truncated or malformed manifest must never ship.
$check = (Get-Content -LiteralPath $tempPath -Raw) | ConvertFrom-Json
Test-ManifestShape $check
if ([int]$check.assetCount -ne $entries.Count) { throw "Manifest assetCount $($check.assetCount) does not match $($entries.Count) entries" }
if (@($check.assets).Count -ne $entries.Count) { throw 'Manifest assets array does not match the entry list' }

Write-Step ("Entries: {0} total ({1} models, {2} textures), {3} new, {4} updated, {5} unchanged" -f `
    $entries.Count, $modelCount, $textureCount, $newEntries, $updatedEntries, $unchanged)
$uploadBytes = [int64]0
foreach ($file in $toUpload) { $uploadBytes += $file.Size }
Write-Step ("Objects to upload: {0} ({1} MB)" -f $toUpload.Count, [Math]::Round($uploadBytes / 1MB, 2))
Write-Step ("Publishing manifestVersion {0}, {1} bytes (was {2})" -f $newTop['manifestVersion'], [System.Text.Encoding]::UTF8.GetByteCount($newText), [System.Text.Encoding]::UTF8.GetByteCount($manifestText))

foreach ($file in $toUpload) {
    Write-Host ("  upload: {0} ({1} bytes)" -f $file.Key, $file.Size)
}
foreach ($entry in $staleEntries) {
    Write-Host ("  stale : {0}{1}" -f $entry.key, $(if ($Prune) { ' (will prune)' } else { '' }))
}
if ($sha256Backfilled -gt 0) {
    Write-Step ("{0} existing entr(ies) stay size-only by design (sha256 is added only to new entries)" -f $sha256Backfilled)
}

$pendingChanges = $newEntries + $updatedEntries
if ($Prune) { $pendingChanges += $staleEntries.Count }

if ($pendingChanges -eq 0 -and -not $Force) {
    $currentVersion = if ($manifest.PSObject.Properties['manifestVersion']) { $manifest.manifestVersion } else { 'none' }
    Write-Step "The manifest is already current (version $currentVersion): no new entries, no changed files, nothing to prune."
    Write-Step 'Nothing was uploaded, deleted or published.'
    Remove-Item -LiteralPath $tempPath -ErrorAction SilentlyContinue
    return
}

if ($CandidatePath) {
    [System.IO.File]::WriteAllText($CandidatePath, $newText, [System.Text.UTF8Encoding]::new($false))
    Write-Step "Candidate manifest written to $CandidatePath"
}

if (-not $Apply) {
    Write-Step 'Dry run: nothing was uploaded, deleted or published. Re-run with -Apply to push.'
    Remove-Item -LiteralPath $tempPath -ErrorAction SilentlyContinue
    return
}

# ---------------------------------------------------------------- upload objects

function Test-PublicObject {
    param([string]$Url, [int64]$Size, [int]$Attempts = 2)
    for ($i = 0; $i -lt $Attempts; $i++) {
        try {
            $head = Invoke-WebRequest -Uri $Url -Method Head -UseBasicParsing -TimeoutSec 30 -ErrorAction Stop
            if ([int64]$head.Headers['Content-Length'] -eq $Size) { return $true }
            return $false
        } catch {
            Start-Sleep -Seconds 1
        }
    }
    return $false
}

$uploaded = 0
$uploadedBytes = [int64]0
foreach ($file in $toUpload) {
    $uri = New-R2ObjectUri -Bucket $TexturesBucket -Key $file.Key
    $null = Invoke-R2Request -Method Put -Uri $uri -InFile $file.FullName -ContentType $file.ContentType -CacheControl $TextureCacheControl

    # Verify the object, not the HTTP status: read it back through the authenticated API
    # (no edge cache involved) and compare hashes. The ETag returned by a PUT is not
    # surfaced reliably by Invoke-WebRequest.
    $readBack = Get-R2ObjectBytes -Bucket $TexturesBucket -Key $file.Key
    $readBackHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($readBack)) -Algorithm SHA256).Hash
    if ($readBackHash -ine $file.Sha256) {
        throw "Uploaded '$($file.Key)' but the stored object does not match the local file ($readBackHash vs $($file.Sha256))"
    }
    $uploaded++
    $uploadedBytes += $file.Size
    Write-Step ("uploaded {0}/{1}: {2} (sha256 {3}…)" -f $uploaded, $toUpload.Count, $file.Key, $file.Sha256.Substring(0, 16))
}

# ---------------------------------------------------------------- prune objects

$pruned = 0
if ($Prune -and $staleEntries.Count -gt 0) {
    foreach ($entry in $staleEntries) {
        try {
            $null = Invoke-R2Request -Method Delete -Uri (New-R2ObjectUri -Bucket ([string]$entry.bucket) -Key ([string]$entry.key))
            $pruned++
            Write-Step "pruned $($entry.key)"
        } catch {
            Write-Warn "could not delete '$($entry.key)': $($_.Exception.Message)"
        }
    }
    # Drop the entries too, then republish below.
    $keep = @($entries | Where-Object {
        if ([string]$_.kind -ne 'texture') { return $true }
        if ($seenKeys.ContainsKey([string]$_.key)) { return $true }
        return -not (Test-KeyUnderPrefixes ([string]$_.localRelativePath))
    })
    $newTop['assets'] = $keep
    $newTop['assetCount'] = $keep.Count
    $textureCount = 0; $modelCount = 0; $totalSize = [int64]0
    foreach ($entry in $keep) {
        if ([string]$entry.kind -eq 'model') { $modelCount++ } else { $textureCount++ }
        $totalSize += [int64]$entry.size
    }
    $newTop['modelCount'] = $modelCount
    $newTop['textureCount'] = $textureCount
    $newTop['totalSize'] = $totalSize
    $newTop['totalSizeMB'] = (Format-SizeMB $totalSize)
    $newManifest = [pscustomobject]$newTop
    $newText = (($newManifest | ConvertTo-Json -Depth 20) + "`n")
    [System.IO.File]::WriteAllText($tempPath, $newText, [System.Text.UTF8Encoding]::new($false))
}

# ---------------------------------------------------------------- publish manifest

$manifestBytes = [System.IO.File]::ReadAllBytes($tempPath)
foreach ($bucket in @($TexturesBucket, $ModelsBucket)) {
    $uri = New-R2ObjectUri -Bucket $bucket -Key $ManifestKey
    $null = Invoke-R2Request -Method Put -Uri $uri -Body $manifestBytes -ContentType $ManifestContentType -CacheControl $ManifestCacheControl
    $readBack = Get-R2ObjectBytes -Bucket $bucket -Key $ManifestKey
    $readBackHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($readBack)) -Algorithm SHA256).Hash
    $localHash = (Get-FileHash -LiteralPath $tempPath -Algorithm SHA256).Hash
    if ($readBackHash -ne $localHash) { throw "manifest.json in '$bucket' did not match what was uploaded ($readBackHash vs $localHash)" }
    Write-Step "published $ManifestKey to $bucket and verified byte-for-byte"
}

$finalTextures = Get-R2ObjectBytes -Bucket $TexturesBucket -Key $ManifestKey
$finalModels = Get-R2ObjectBytes -Bucket $ModelsBucket -Key $ManifestKey
$finalTexturesHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($finalTextures)) -Algorithm SHA256).Hash
$finalModelsHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($finalModels)) -Algorithm SHA256).Hash
if ($finalTexturesHash -ne $finalModelsHash) {
    throw "manifest.json drifted between buckets after publishing ($finalTexturesHash vs $finalModelsHash)"
}
Write-Step "Both buckets still hold the same manifest (sha256 $($finalTexturesHash.Substring(0, 16))…)"

# r2.dev serves through Cloudflare's edge cache, so the public copy can lag the bucket.
foreach ($base in @($publicBaseTextures, $publicBaseModels)) {
    $url = "$base/$ManifestKey"
    $fresh = $false
    for ($i = 0; $i -lt 15; $i++) {
        try {
            $publicCopy = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 30 -ErrorAction Stop
            $publicHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($publicCopy.RawContentStream.ToArray())) -Algorithm SHA256).Hash
            if ($publicHash -eq $finalTexturesHash) { $fresh = $true; break }
        } catch { }
        Start-Sleep -Seconds 2
    }
    if ($fresh) { Write-Step "public $url serves the new manifest" }
    else { Write-Warn "public $url is still serving the previous manifest (max-age=$ManifestCacheControl); it will catch up" }
}

# A sample of the objects through the public domain, the way a client would fetch them.
# r2.dev caches at the edge and a freshly stored key can take a couple of minutes to
# appear; the objects themselves were verified through the API above, so this is a
# propagation report, not a failure.
if ($toUpload.Count -gt 0) {
    $sample = @($toUpload[0], $toUpload[[int][Math]::Floor($toUpload.Count / 2)], $toUpload[$toUpload.Count - 1]) |
        Sort-Object Key -Unique
    $notYetServed = 0
    foreach ($file in $sample) {
        $publicUrl = "$publicBaseTextures/$(ConvertTo-R2UrlPath $file.Key)"
        if (Test-PublicObject -Url $publicUrl -Size $file.Size -Attempts 45) {
            Write-Step "public sample ok: $($file.Key)"
        } else {
            $notYetServed++
            Write-Warn "public sample not served yet: $($file.Key)"
        }
    }
    if ($notYetServed -gt 0) {
        Write-Warn "r2.dev is still propagating for $notYetServed sampled object(s); they are stored and listed in the manifest"
    }
}

Remove-Item -LiteralPath $tempPath -ErrorAction SilentlyContinue
Write-Step ("Done: {0} object(s) uploaded ({1} MB), {2} pruned, manifestVersion {3}" -f `
    $uploaded, [Math]::Round($uploadedBytes / 1MB, 2), $pruned, $newTop['manifestVersion'])
