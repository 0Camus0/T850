<#
.SYNOPSIS
    Uploads model assets to the t850-models R2 bucket and keeps runtime_assets.json in step.

.DESCRIPTION
    The T850 cloud asset model has two buckets and two manifests:

      * t850-textures + t850-models each hold an identical copy of the *union* manifest.json.
        That one is maintained by PushCloudTextures.ps1 and every entry names its bucket.
      * t850-models additionally holds runtime_assets.json, the *model* manifest read by every
        model downloader: scripts\DownloadModels.ps1 and the launcher, steamdeck\DownloadCloudAssets.py,
        android\...\AssetDownloadActivity.java and web\cloud-assets.mjs.

    runtime_assets.json has no union: its entries are

        { "key": "Backyard/Backyard_House.glb",
          "url": "https://pub-…r2.dev/Backyard/Backyard_House.glb",
          "size": 5297556, "sha256": "…" }

    where `key` is the path relative to Assets/Models/ — that is the contract all four downloaders
    implement. `size` and `sha256` are optional to them but written here, because both
    Test-T850CloudModelFile (PowerShell) and the Deck downloader verify them, and a model that
    silently truncates is otherwise undetectable.

    The pipeline is the same "objects before manifest" rule as PushCloudTextures.ps1:

      1. scan the local files under -Prefix and hash them
      2. download runtime_assets.json from t850-models (the cloud copy is the source of truth;
         the committed Assets\model-cloud-manifest.json is a mirror, rewritten at the end)
      3. merge the entries for the scanned files, preserving the order of the ones already there
      4. (a dry run stops here and prints what it would do)
      5. upload changed objects and verify each by reading it back and comparing sha256
      6. publish runtime_assets.json, verify the stored bytes, then confirm the public URL of every
         uploaded object serves the right length
      7. mirror the manifest into Assets\model-cloud-manifest.json so a repo checkout and a
         launcher-installed copy report the same inventory

    Without -Apply nothing is written: no PUT, no local file change. The token is read from a file
    outside the repository (or $env:CLOUDFLARE_API_TOKEN) and is never printed.

.PARAMETER Prefix
    Asset-relative prefixes to sync, e.g. Models\Backyard. Default: Models/Backyard.

.PARAMETER Prune
    Also delete the *manifest entries* under the scanned prefixes whose local file is gone, and the
    objects. Off by default: a model that a released build still references must not vanish because
    a developer cleaned their working copy. A dry run always reports what pruning would do.

.PARAMETER Force
    Re-upload every scanned file even when the manifest already matches it.

.PARAMETER Apply
    Perform the uploads. Without it the script is a dry run.

.EXAMPLE
    .\scripts\PushCloudModels.ps1
    Dry run for Models/Backyard: prints what would be uploaded and how the manifest changes.

.EXAMPLE
    .\scripts\PushCloudModels.ps1 -Apply
    Uploads the changed meshes and republishes runtime_assets.json.

.EXAMPLE
    .\scripts\PushCloudModels.ps1 -Check
    Verifies the cloud manifest against the local files without touching anything, and exits
    non-zero when a local model is missing from the cloud.
#>
[CmdletBinding()]
param(
    [string]$RootDir,
    [string]$AssetRoot,
    [string[]]$Prefix = @('Models/Backyard'),
    [string]$TokenFile = 'C:\Users\camus\Documents\cloud.txt',
    [string]$Token,
    [string]$AccountId = '66cd75c80eda50d1a2ae018f7f7136b6',
    [string]$ModelsBucket = 't850-models',
    [string]$ManifestKey = 'runtime_assets.json',
    # The committed mirror. Empty disables writing it.
    [string]$LocalManifestPath,
    [switch]$Check,
    [switch]$Prune,
    [switch]$Force,
    [switch]$Apply,
    [string]$CandidatePath,
    [int]$Retries = 3,
    [int]$TimeoutSec = 600
)

$ErrorActionPreference = 'Stop'
$ApiRoot = 'https://api.cloudflare.com/client/v4'
# Meshes are regenerated under the same names as the Blender scene evolves, so unlike the ball
# textures these must NOT be marked immutable: a 30-day cached copy of a replaced mesh would fail
# the sha256 check in every downloader with no way to recover but a purge.
$ModelCacheControl = 'public, max-age=86400'
$ManifestCacheControl = 'public, max-age=300'
$ManifestContentType = 'application/json'
$ContentTypes = @{
    '.glb' = 'model/gltf-binary'
    '.gltf' = 'model/gltf+json'
    '.bin' = 'application/octet-stream'
    '.json' = 'application/json'
    '.png' = 'image/png'
    '.jpg' = 'image/jpeg'
    '.jpeg' = 'image/jpeg'
}
$ModelExtensions = @('.glb', '.gltf', '.bin')

if (-not $RootDir) { $RootDir = Split-Path -Parent $PSScriptRoot }
if (-not $AssetRoot) { $AssetRoot = Join-Path $RootDir 'Assets' }
if (-not (Test-Path -LiteralPath $AssetRoot)) { throw "No Assets directory under $RootDir" }
if (-not $LocalManifestPath) { $LocalManifestPath = Join-Path $AssetRoot 'model-cloud-manifest.json' }

if (-not $Token) {
    if ($env:CLOUDFLARE_API_TOKEN) {
        $Token = $env:CLOUDFLARE_API_TOKEN.Trim()
    } elseif ($TokenFile -and (Test-Path -LiteralPath $TokenFile)) {
        # ReadAllText keeps any BOM; Trim() removes it together with whitespace.
        $Token = ([System.IO.File]::ReadAllText($TokenFile)).Trim()
    } else {
        throw "No Cloudflare API token: set CLOUDFLARE_API_TOKEN or create $TokenFile"
    }
}
if (-not $Token) { throw 'Cloudflare API token is empty' }
if ($Token -match '\s') { throw 'Cloudflare API token contains whitespace' }
if ($Check -and $Apply) { throw '-Check and -Apply are mutually exclusive' }

$Prefixes = @($Prefix | ForEach-Object { $_.Replace('\', '/').Trim('/') } | Where-Object { $_ })
if ($Prefixes.Count -eq 0) { throw 'No sync prefix given' }

function Write-Step { param([string]$Message) Write-Host "  $Message" }
function Write-Warn { param([string]$Message) Write-Host "  WARN $Message" -ForegroundColor Yellow }
function Write-Fail { param([string]$Message) Write-Host "  FAIL $Message" -ForegroundColor Red }

# ---------------------------------------------------------------- helpers

function ConvertTo-R2Key { param([string]$RelativePath) return ($RelativePath.Replace('\', '/').TrimStart('/')) }

function ConvertTo-R2UrlPath {
    param([string]$Key)
    # Keys keep their slashes; every other unsafe character is percent-encoded, which is how the
    # existing manifest encodes brackets ("%281%29").
    return (($Key.Split('/') | ForEach-Object { [System.Uri]::EscapeDataString($_) }) -join '/')
}

function New-R2ObjectUri {
    param([string]$Key)
    return "$ApiRoot/accounts/$AccountId/r2/buckets/$ModelsBucket/objects/$(ConvertTo-R2UrlPath $Key)"
}

function Invoke-R2Request {
    param(
        [Parameter(Mandatory)][string]$Method,
        [Parameter(Mandatory)][string]$Uri,
        [string]$ContentType,
        [string]$CacheControl,
        [string]$InFile
    )
    $headers = @{ Authorization = "Bearer $Token" }
    if ($CacheControl) { $headers['Cache-Control'] = $CacheControl }
    $attempt = 0
    while ($true) {
        $attempt++
        try {
            $request = @{
                Uri             = $Uri
                Method          = $Method
                Headers         = $headers
                TimeoutSec      = $TimeoutSec
                UseBasicParsing = $true
                ErrorAction     = 'Stop'
            }
            if ($InFile) { $request['InFile'] = $InFile }
            if ($ContentType) { $request['ContentType'] = $ContentType }
            return Invoke-WebRequest @request
        } catch {
            $status = 0
            if ($_.Exception.Response -and $_.Exception.Response.StatusCode) {
                $status = [int]$_.Exception.Response.StatusCode
            }
            if ($status -eq 401 -or $status -eq 403) {
                throw "Cloudflare rejected the token (HTTP $status). Check the token's R2 permissions."
            }
            if ($attempt -gt $Retries -or ($status -ge 400 -and $status -lt 500 -and $status -ne 429)) {
                throw "HTTP $status on $Method $Uri after $attempt attempt(s): $($_.Exception.Message)"
            }
            Start-Sleep -Seconds ([Math]::Min(10, [Math]::Pow(2, $attempt)))
        }
    }
}

function Get-R2ObjectBytes { param([string]$Key) return (Invoke-R2Request -Method Get -Uri (New-R2ObjectUri -Key $Key)).RawContentStream.ToArray() }

function Get-Sha256Hex { param([string]$Path) return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }

function Get-ContentTypeFor {
    param([string]$Path)
    $extension = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
    if ($ContentTypes.ContainsKey($extension)) { return $ContentTypes[$extension] }
    throw "No R2 content type mapped for '$Path' (extension '$extension')"
}

function Test-UnderPrefixes {
    param([string]$RelativePath)
    foreach ($prefix in $Prefixes) {
        if ($RelativePath.Equals($prefix, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
        if ($RelativePath.StartsWith("$prefix/", [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

function Get-HeaderLength {
    param($HeaderValue)
    # On PowerShell 7.6 `$response.Headers['Content-Length']` is a String[] of one element, and
    # casting that array to [int64] throws ("Cannot convert the System.String[] value…"). A
    # swallowed throw here previously reported every object as unreadable, so parse explicitly.
    $text = if ($HeaderValue -is [array]) { [string]($HeaderValue | Select-Object -First 1) } else { [string]$HeaderValue }
    $parsed = [int64]0
    if ([int64]::TryParse($text, [ref]$parsed)) { return $parsed }
    return [int64]-1
}

function Test-PublicObject {
    param([string]$Url, [int64]$Size, [int]$Attempts = 12)
    # A new object is readable through the authenticated API immediately but the public hostname
    # lags by tens of seconds, so the budget is deliberately generous — up to about two minutes.
    for ($i = 0; $i -lt $Attempts; $i++) {
        try {
            $head = Invoke-WebRequest -Uri $Url -Method Head -UseBasicParsing -TimeoutSec 30 -ErrorAction Stop
            if ((Get-HeaderLength $head.Headers['Content-Length']) -eq $Size) { return $true }
        } catch { }
        Start-Sleep -Seconds ([Math]::Min(3 * ($i + 1), 15))
    }
    return $false
}

# ---------------------------------------------------------------- scan local files

Write-Host ''
Write-Host "T850 model cloud push — $ManifestKey in '$ModelsBucket'" -ForegroundColor Cyan
Write-Host "  root:     $RootDir"
Write-Host "  prefixes: $($Prefixes -join ', ')"
Write-Host "  mode:     $(if ($Check) { 'check only' } elseif ($Apply) { 'APPLY' } else { 'dry run' })"

$modelsRoot = Join-Path $AssetRoot 'Models'
if (-not (Test-Path -LiteralPath $modelsRoot)) { throw "No Models directory under $AssetRoot" }

$files = New-Object System.Collections.Generic.List[object]
foreach ($prefix in $Prefixes) {
    $directory = Join-Path $AssetRoot $prefix
    if (-not (Test-Path -LiteralPath $directory)) { throw "Prefix '$prefix' does not exist under $AssetRoot" }
    foreach ($item in Get-ChildItem -LiteralPath $directory -Recurse -File) {
        $extension = $item.Extension.ToLowerInvariant()
        if ($ModelExtensions -notcontains $extension) { continue }
        $relativeToModels = [System.IO.Path]::GetRelativePath($modelsRoot, $item.FullName).Replace('\', '/')
        $files.Add([pscustomobject]@{
            Key         = $relativeToModels
            FullName    = $item.FullName
            Size        = $item.Length
            Sha256      = Get-Sha256Hex $item.FullName
            ContentType = Get-ContentTypeFor $item.FullName
        })
    }
}
if ($files.Count -eq 0) { throw "No model files (.glb/.gltf/.bin) under $($Prefixes -join ', ')" }
$files = @($files | Sort-Object Key)
Write-Step ("scanned {0} model file(s), {1:N2} MB" -f $files.Count, (($files | Measure-Object Size -Sum).Sum / 1MB))

# ---------------------------------------------------------------- read the cloud manifest

$manifestBytes = Get-R2ObjectBytes -Key $ManifestKey
$manifest = [System.Text.Encoding]::UTF8.GetString($manifestBytes) | ConvertFrom-Json
if (-not $manifest.PSObject.Properties['assets']) { throw "$ManifestKey has no 'assets' array" }
$publicBase = [string]$manifest.publicBaseUrl
if (-not $publicBase) { throw "$ManifestKey has no publicBaseUrl; cannot build entry urls" }
Write-Step "cloud manifest: $($manifest.assets.Count) asset(s), base $publicBase"

$entries = @($manifest.assets)
$index = @{}
for ($i = 0; $i -lt $entries.Count; $i++) {
    $key = [string]$entries[$i].key
    if ($key) { $index[$key] = $i }
}

# ---------------------------------------------------------------- classify

$toUpload = New-Object System.Collections.Generic.List[object]
$unchanged = 0
foreach ($file in $files) {
    $existing = if ($index.ContainsKey($file.Key)) { $entries[$index[$file.Key]] } else { $null }
    $isCurrent = $false
    if ($existing) {
        $existingSize = if ($existing.PSObject.Properties['size']) { [int64]$existing.size } else { -1 }
        $existingHash = if ($existing.PSObject.Properties['sha256']) { [string]$existing.sha256 } else { '' }
        # A pre-existing entry from before sha256 was written only has its size to compare, so any
        # file that needs certainty is re-uploaded; the cost is a few MB once.
        $isCurrent = ($existingSize -eq $file.Size) -and ($existingHash -eq $file.Sha256)
        $hasUrl = $existing.PSObject.Properties['url'] -and $existing.url
        if ($isCurrent -and -not $hasUrl) { $isCurrent = $false }
    }
    if ($Force -or -not $isCurrent) { $toUpload.Add($file) } else { $unchanged++ }
}

# ---------------------------------------------------------------- backfill unscanned entries
#
# Entries outside the scanned prefixes are kept as they are, but an older copy of this script
# published some as `key` + `url` only. The downloaders then fall back to "exists and is non-empty",
# so a truncated or replaced file cannot be detected. Fill in what can be determined without
# uploading: `sha256` from the local file when it is on disk, `size` from the local file or from a
# HEAD of the public URL. An entry that cannot be resolved keeps its current shape.
$backfill = @{}
$backfilledHash = 0
$backfilledSize = 0
foreach ($entry in $entries) {
    $key = [string]$entry.key
    if (-not $key) { continue }
    if ($files | Where-Object { $_.Key -eq $key }) { continue }        # scanned below, already exact
    $hasSize = $entry.PSObject.Properties['size'] -and [int64]$entry.size -gt 0
    $hasHash = $entry.PSObject.Properties['sha256'] -and [string]$entry.sha256
    if ($hasSize -and $hasHash) { continue }

    $size = if ($hasSize) { [int64]$entry.size } else { [int64]0 }
    $hash = if ($hasHash) { ([string]$entry.sha256).ToLowerInvariant() } else { '' }
    $localPath = Join-Path $modelsRoot ($key.Replace('/', [System.IO.Path]::DirectorySeparatorChar))
    if (Test-Path -LiteralPath $localPath -PathType Leaf) {
        $item = Get-Item -LiteralPath $localPath
        $size = [int64]$item.Length
        if (-not $hash) { $hash = (Get-Sha256Hex $localPath).ToLowerInvariant(); $backfilledHash++ }
    } elseif (-not $size) {
        $url = if ($entry.PSObject.Properties['url'] -and $entry.url) { [string]$entry.url } else { "$publicBase/$(ConvertTo-R2UrlPath $key)" }
        for ($attempt = 0; $attempt -lt 3 -and -not $size; $attempt++) {
            try {
                $head = Invoke-WebRequest -Uri $url -Method Head -UseBasicParsing -TimeoutSec 30 -ErrorAction Stop
                $value = $head.Headers['Content-Length']
                $text = if ($value -is [array]) { [string]($value | Select-Object -First 1) } else { [string]$value }
                $parsed = [int64]0
                if ([int64]::TryParse($text, [ref]$parsed) -and $parsed -gt 0) { $size = $parsed }
            } catch { Start-Sleep -Seconds 1 }
        }
        if ($size -gt 0) { $backfilledSize++ }
    }
    if ($size -gt 0 -or $hash) {
        $backfill[$key] = [pscustomobject]@{ Size = $size; Sha256 = $hash }
    }
}
if ($backfill.Count -gt 0) {
    Write-Step ("backfill: {0} unscanned entry(ies), {1} gained a size, {2} gained a sha256" -f `
        $backfill.Count, $backfilledSize, $backfilledHash)
}

$seen = @{}
foreach ($file in $files) { $seen[$file.Key] = $true }
$stale = @($entries | Where-Object {
    $key = [string]$_.key
    if (-not $key) { return $false }
    if (-not (Test-UnderPrefixes $key)) { return $false }
    return -not $seen.ContainsKey($key)
})

# ---------------------------------------------------------------- build the new manifest

$newEntries = New-Object System.Collections.Generic.List[object]
foreach ($entry in $entries) {
    $key = [string]$entry.key
    if ($stale.Count -gt 0 -and $Prune) {
        $isStale = $false
        foreach ($candidate in $stale) { if ([string]$candidate.key -eq $key) { $isStale = $true; break } }
        if ($isStale) { continue }
    }
    $replacement = $files | Where-Object { $_.Key -eq $key } | Select-Object -First 1
    if ($replacement) {
        $newEntries.Add([ordered]@{
            key    = $replacement.Key
            url    = "$publicBase/$(ConvertTo-R2UrlPath $replacement.Key)"
            size   = $replacement.Size
            sha256 = $replacement.Sha256.ToLowerInvariant()
        })
    } elseif ($backfill.ContainsKey($key)) {
        # Not scanned this run, but the size (and possibly the hash) could be determined without
        # uploading. Rebuild the entry so it carries them, keeping every other field.
        $facts = $backfill[$key]
        $newEntries.Add([ordered]@{
            key    = $key
            url    = if ($entry.PSObject.Properties['url'] -and $entry.url) { [string]$entry.url } else { "$publicBase/$(ConvertTo-R2UrlPath $key)" }
            size   = $facts.Size
            sha256 = $facts.Sha256
        })
    } else {
        # Untouched entry: keep it exactly as it was, field order included.
        $newEntries.Add($entry)
    }
}
foreach ($file in $files) {
    if ($index.ContainsKey($file.Key)) { continue }
    $newEntries.Add([ordered]@{
        key    = $file.Key
        url    = "$publicBase/$(ConvertTo-R2UrlPath $file.Key)"
        size   = $file.Size
        sha256 = $file.Sha256.ToLowerInvariant()
    })
}

$newTop = [ordered]@{}
foreach ($property in $manifest.PSObject.Properties) {
    if ($property.Name -eq 'assets' -or $property.Name -eq 'assetCount') { continue }
    $newTop[$property.Name] = $property.Value
}
$newTop['assetCount'] = $newEntries.Count
# `@($list)` throws "Argument types do not match" when assigning a List[object] into an
# OrderedDictionary (pwsh 7.6), so materialise the array first.
$newTop['assets'] = $newEntries.ToArray()
$newManifestText = ($newTop | ConvertTo-Json -Depth 10)
if (-not $newManifestText.EndsWith("`n")) { $newManifestText += "`n" }

# ---------------------------------------------------------------- report

Write-Host ''
Write-Host '  Changes' -ForegroundColor Cyan
if ($toUpload.Count -eq 0) { Write-Step 'objects: nothing to upload' }
foreach ($file in $toUpload) {
    $state = if ($index.ContainsKey($file.Key)) { 'replace' } else { 'new    ' }
    Write-Step ("{0} {1} ({2:N2} MB, sha256 {3}…)" -f $state, $file.Key, ($file.Size / 1MB), $file.Sha256.Substring(0, 16))
}
Write-Step "objects: $($toUpload.Count) to upload, $unchanged already current"
if ($stale.Count -gt 0) {
    foreach ($entry in $stale) { Write-Warn "stale manifest entry, local file gone: $($entry.key)$(if (-not $Prune) { ' (kept; pass -Prune to remove)' })" }
}
Write-Step "manifest: $($entries.Count) -> $($newEntries.Count) asset(s)"

$existingHashes = @{}
foreach ($file in $files) { $existingHashes[$file.Key] = $true }

if ($Check) {
    $problems = @($files | Where-Object { -not $index.ContainsKey($_.Key) })
    if ($problems.Count -gt 0) {
        foreach ($problem in $problems) { Write-Fail "not in the cloud manifest: $($problem.Key)" }
        Write-Host ''
        Write-Host "FAIL: $($problems.Count) local model(s) missing from $ManifestKey" -ForegroundColor Red
        exit 1
    }
    Write-Host ''
    Write-Host "PASS: every local model under $($Prefixes -join ', ') is in $ManifestKey" -ForegroundColor Green
    exit 0
}

if ($CandidatePath) {
    Set-Content -LiteralPath $CandidatePath -Value $newManifestText -NoNewline -Encoding utf8
    Write-Step "candidate manifest written to $CandidatePath"
}

if (-not $Apply) {
    Write-Host ''
    Write-Host 'Dry run: nothing uploaded. Pass -Apply to publish.' -ForegroundColor Yellow
    exit 0
}

# ---------------------------------------------------------------- upload objects

$uploaded = 0
foreach ($file in $toUpload) {
    $null = Invoke-R2Request -Method Put -Uri (New-R2ObjectUri -Key $file.Key) `
        -InFile $file.FullName -ContentType $file.ContentType -CacheControl $ModelCacheControl
    # Verify the stored object, not the HTTP status (same reasoning as PushCloudTextures.ps1).
    $readBack = Get-R2ObjectBytes -Key $file.Key
    $readBackHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new($readBack)) -Algorithm SHA256).Hash
    if ($readBackHash -ine $file.Sha256) {
        throw "Uploaded '$($file.Key)' but the stored object does not match the local file ($readBackHash vs $($file.Sha256))"
    }
    $uploaded++
    Write-Step ("uploaded {0}/{1}: {2}" -f $uploaded, $toUpload.Count, $file.Key)
}

# ---------------------------------------------------------------- publish the manifest

$temporary = [System.IO.Path]::GetTempFileName()
try {
    [System.IO.File]::WriteAllText($temporary, $newManifestText, [System.Text.UTF8Encoding]::new($false))
    $null = Invoke-R2Request -Method Put -Uri (New-R2ObjectUri -Key $ManifestKey) `
        -InFile $temporary -ContentType $ManifestContentType -CacheControl $ManifestCacheControl
} finally {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
}
$publishedText = [System.Text.Encoding]::UTF8.GetString((Get-R2ObjectBytes -Key $ManifestKey))
if ($publishedText -ne $newManifestText) {
    throw "$ManifestKey read back different from what was published"
}
Write-Step "published $ManifestKey ($($newEntries.Count) assets) and verified byte-for-byte"

# ---------------------------------------------------------------- verify the public edge

$publicFailures = @()
foreach ($file in $files) {
    $url = "$publicBase/$(ConvertTo-R2UrlPath $file.Key)"
    if (-not (Test-PublicObject -Url $url -Size $file.Size)) { $publicFailures += $url }
}
if ($publicFailures.Count -gt 0) {
    foreach ($failure in $publicFailures) { Write-Fail "public URL did not serve the expected length: $failure" }
    Write-Host ''
    Write-Host "FAIL: $($publicFailures.Count) object(s) are not publicly readable yet. The objects and the" -ForegroundColor Red
    Write-Host "manifest are already published; public propagation can take a minute. Re-run the same" -ForegroundColor Red
    Write-Host "command to finish verification and the local mirror." -ForegroundColor Red
    exit 1
}
Write-Step "public URLs verified for $($files.Count) object(s)"

# ---------------------------------------------------------------- mirror locally

if ($LocalManifestPath) {
    $localExisting = if (Test-Path -LiteralPath $LocalManifestPath) { [System.IO.File]::ReadAllText($LocalManifestPath) } else { '' }
    if ($localExisting -ne $newManifestText) {
        [System.IO.File]::WriteAllText($LocalManifestPath, $newManifestText, [System.Text.UTF8Encoding]::new($false))
        Write-Step "mirrored the manifest into $LocalManifestPath — commit it"
    } else {
        Write-Step "$LocalManifestPath was already current"
    }
}

Write-Host ''
Write-Host "PASS: $uploaded object(s) uploaded, $($newEntries.Count) assets in $ManifestKey" -ForegroundColor Green
exit 0
