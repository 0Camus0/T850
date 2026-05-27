# T850 public model downloader.
# Configure either:
#   - T850_MODEL_BASE_URL=https://public.example.com/t850-assets
#   - T850_MODEL_MANIFEST=D:\path\to\model-cloud-manifest.json
#   - T850_MODEL_MANIFEST_URL=https://public.example.com/manifest.json
# or set baseUrl/url entries in Assets\model-cloud-manifest.json.

function Normalize-T850CloudResourcePath {
    param([string]$Path)
    if (-not $Path) { return "" }
    $normalized = $Path.Replace('\', '/').Trim()
    while ($normalized.StartsWith('/')) { $normalized = $normalized.Substring(1) }
    if ($normalized.StartsWith('Assets/', [System.StringComparison]::OrdinalIgnoreCase)) {
        $normalized = $normalized.Substring(7)
    }
    return $normalized
}

function Join-T850CloudUrl {
    param(
        [Parameter(Mandatory = $true)] [string]$BaseUrl,
        [Parameter(Mandatory = $true)] [string]$ResourcePath
    )
    $base = $BaseUrl.TrimEnd('/')
    $parts = (Normalize-T850CloudResourcePath $ResourcePath).Split('/') | Where-Object { $_ -ne '' }
    $escaped = ($parts | ForEach-Object { [System.Uri]::EscapeDataString($_) }) -join '/'
    return "$base/$escaped"
}

function Resolve-T850ModelManifestPath {
    param([string]$RootDir, [string]$ManifestPath)
    $candidates = New-Object System.Collections.Generic.List[string]
    if ($ManifestPath) { $candidates.Add($ManifestPath) }
    if ($env:T850_MODEL_MANIFEST) { $candidates.Add($env:T850_MODEL_MANIFEST) }
    if ($RootDir) {
        $candidates.Add((Join-Path $RootDir 'Assets\model-cloud-manifest.json'))
        $candidates.Add((Join-Path $RootDir 'model-cloud-manifest.json'))
    }
    if ($PSScriptRoot) { $candidates.Add((Join-Path $PSScriptRoot 'model-cloud-manifest.json')) }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) { return (Resolve-Path $candidate).Path }
    }
    return $null
}

function Test-T850CloudModelFile {
    param($Entry, [string]$TargetPath)
    if (-not (Test-Path $TargetPath)) { return $false }
    $actual = (Get-Item -LiteralPath $TargetPath).Length
    if ($Entry.PSObject.Properties['size'] -and $Entry.size -gt 0) {
        if ($actual -ne [int64]$Entry.size) { return $false }
    } elseif ($actual -le 0) {
        return $false
    }
    if ($Entry.PSObject.Properties['sha256'] -and $Entry.sha256) {
        $hash = (Get-FileHash -LiteralPath $TargetPath -Algorithm SHA256).Hash
        if ($hash -ine $Entry.sha256) { return $false }
    }
    return $true
}

function Invoke-T850CloudStatus {
    param([scriptblock]$StatusCallback, [string]$Message)
    if ($StatusCallback) { & $StatusCallback $Message }
}

function Convert-T850CloudManifestModels {
    param($Manifest)
    $entries = New-Object System.Collections.Generic.List[object]

    if ($Manifest.PSObject.Properties['models'] -and $Manifest.models) {
        foreach ($entry in $Manifest.models) { $entries.Add($entry) }
        return $entries.ToArray()
    }

    if ($Manifest.PSObject.Properties['assets'] -and $Manifest.assets) {
        foreach ($asset in $Manifest.assets) {
            $resourcePath = ""
            if ($asset.PSObject.Properties['key'] -and $asset.key) {
                $resourcePath = [string]$asset.key
            } elseif ($asset.PSObject.Properties['localPath'] -and $asset.localPath) {
                $local = Normalize-T850CloudResourcePath ([string]$asset.localPath)
                $marker = 'Assets/Models/'
                $idx = $local.IndexOf($marker, [System.StringComparison]::OrdinalIgnoreCase)
                if ($idx -ge 0) { $resourcePath = $local.Substring($idx + 'Assets/'.Length) }
                else { $resourcePath = $local }
            }

            $resourcePath = Normalize-T850CloudResourcePath $resourcePath
            if ($resourcePath -and -not $resourcePath.StartsWith('Models/', [System.StringComparison]::OrdinalIgnoreCase)) {
                $resourcePath = 'Models/' + $resourcePath.TrimStart('/')
            }
            if (-not $resourcePath) { continue }

            $entries.Add([pscustomobject]@{
                path = $resourcePath
                url = if ($asset.PSObject.Properties['url']) { [string]$asset.url } else { $null }
                size = if ($asset.PSObject.Properties['size']) { [int64]$asset.size } else { 0 }
                sha256 = if ($asset.PSObject.Properties['sha256']) { [string]$asset.sha256 } else { $null }
            })
        }
    }

    return $entries.ToArray()
}

function Read-T850CloudManifest {
    param(
        [string]$RootDir,
        [string]$ManifestPath,
        [string]$ManifestUrl
    )

    if (-not $ManifestUrl -and $env:T850_MODEL_MANIFEST_URL) { $ManifestUrl = $env:T850_MODEL_MANIFEST_URL }
    if ($ManifestUrl) {
        $json = (Invoke-WebRequest -Uri $ManifestUrl -UseBasicParsing).Content
        return [pscustomobject]@{ Manifest = ($json | ConvertFrom-Json); Source = $ManifestUrl }
    }

    $resolvedManifest = Resolve-T850ModelManifestPath -RootDir $RootDir -ManifestPath $ManifestPath
    if (-not $resolvedManifest) { return [pscustomobject]@{ Manifest = $null; Source = $null } }
    return [pscustomobject]@{ Manifest = (Get-Content -LiteralPath $resolvedManifest -Raw | ConvertFrom-Json); Source = $resolvedManifest }
}

function New-T850CloudDownloadTasks {
    param(
        [object[]]$Entries,
        [string]$AssetRoot,
        [string]$BaseUrl
    )

    $tasks = New-Object System.Collections.Generic.List[object]
    foreach ($entry in $Entries) {
        $resourcePath = Normalize-T850CloudResourcePath ([string]$entry.path)
        if (-not $resourcePath) { continue }
        if (-not $resourcePath.StartsWith('Models/', [System.StringComparison]::OrdinalIgnoreCase)) {
            $resourcePath = 'Models/' + $resourcePath.TrimStart('/')
        }
        $targetPath = Join-Path $AssetRoot ($resourcePath.Replace('/', '\'))

        $url = $null
        if ($entry.PSObject.Properties['url'] -and $entry.url) { $url = [string]$entry.url }
        elseif ($BaseUrl) { $url = Join-T850CloudUrl -BaseUrl $BaseUrl -ResourcePath $resourcePath }

        $tasks.Add([pscustomobject]@{
            ResourcePath = $resourcePath
            TargetPath = $targetPath
            Url = $url
            Size = if ($entry.PSObject.Properties['size']) { [int64]$entry.size } else { 0 }
            Sha256 = if ($entry.PSObject.Properties['sha256']) { [string]$entry.sha256 } else { $null }
            Entry = $entry
        })
    }
    return $tasks.ToArray()
}

function Invoke-T850ParallelDownloads {
    param(
        [object[]]$Tasks,
        [int]$Skipped,
        [int]$Total,
        [int]$MaxThreads,
        [scriptblock]$StatusCallback
    )

    if ($Tasks.Count -eq 0) {
        return [pscustomobject]@{ Downloaded = 0; Errors = @() }
    }

    $max = [Math]::Max(1, [Math]::Min($MaxThreads, $Tasks.Count))
    $pool = [runspacefactory]::CreateRunspacePool(1, $max)
    $pool.ApartmentState = 'MTA'
    $pool.Open()

    $downloadScript = {
        param($Task)
        try {
            if (-not $Task.Url) { throw "No URL configured for $($Task.ResourcePath)" }
            $parent = Split-Path -Parent $Task.TargetPath
            if ($parent -and -not (Test-Path $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
            $tmp = "$($Task.TargetPath).download"
            if (Test-Path $tmp) { Remove-Item -LiteralPath $tmp -Force }
            Invoke-WebRequest -Uri $Task.Url -OutFile $tmp -UseBasicParsing
            if ($Task.Size -gt 0) {
                $actual = (Get-Item -LiteralPath $tmp).Length
                if ($actual -ne [int64]$Task.Size) { throw "Downloaded size mismatch for $($Task.ResourcePath): expected $($Task.Size), got $actual" }
            }
            if ($Task.Sha256) {
                $hash = (Get-FileHash -LiteralPath $tmp -Algorithm SHA256).Hash
                if ($hash -ine $Task.Sha256) { throw "Downloaded hash mismatch for $($Task.ResourcePath)" }
            }
            Move-Item -LiteralPath $tmp -Destination $Task.TargetPath -Force
            return [pscustomobject]@{ Ok = $true; ResourcePath = $Task.ResourcePath; TargetPath = $Task.TargetPath; Error = $null }
        } catch {
            try { if (Test-Path "$($Task.TargetPath).download") { Remove-Item -LiteralPath "$($Task.TargetPath).download" -Force -ErrorAction SilentlyContinue } } catch {}
            return [pscustomobject]@{ Ok = $false; ResourcePath = $Task.ResourcePath; TargetPath = $Task.TargetPath; Error = $_.Exception.Message }
        }
    }

    $running = New-Object System.Collections.Generic.List[object]
    foreach ($task in $Tasks) {
        $ps = [powershell]::Create()
        $ps.RunspacePool = $pool
        [void]$ps.AddScript($downloadScript).AddArgument($task)
        $running.Add([pscustomobject]@{ PowerShell = $ps; Handle = $ps.BeginInvoke(); ResourcePath = $task.ResourcePath })
    }

    $downloaded = 0
    $failed = 0
    $errors = New-Object System.Collections.Generic.List[string]
    $lastPercent = -1

    try {
        while ($running.Count -gt 0) {
            for ($i = $running.Count - 1; $i -ge 0; --$i) {
                $job = $running[$i]
                if (-not $job.Handle.IsCompleted) { continue }
                $result = $job.PowerShell.EndInvoke($job.Handle) | Select-Object -First 1
                $job.PowerShell.Dispose()
                $running.RemoveAt($i)

                if ($result.Ok) { $downloaded++ }
                else {
                    $failed++
                    $errors.Add("$($result.ResourcePath): $($result.Error)")
                }

                $completed = $Skipped + $downloaded + $failed
                $percent = if ($Total -gt 0) { [int][Math]::Floor(($completed * 100.0) / $Total) } else { 100 }
                $message = "Model download progress: $completed/$Total ($percent%) - downloaded=$downloaded skipped=$Skipped failed=$failed"
                if ($percent -ne $lastPercent -or $result.Ok -eq $false) {
                    if (-not $StatusCallback) { Write-Host ("[T850] " + $message) }
                    Invoke-T850CloudStatus -StatusCallback $StatusCallback -Message $message
                    $lastPercent = $percent
                }
            }
            Start-Sleep -Milliseconds 250
        }
    } finally {
        foreach ($job in $running) {
            try { $job.PowerShell.Stop() } catch {}
            try { $job.PowerShell.Dispose() } catch {}
        }
        $pool.Close()
        $pool.Dispose()
    }

    return [pscustomobject]@{ Downloaded = $downloaded; Errors = @($errors) }
}

function Ensure-T850CloudModels {
    param(
        [string]$RootDir = (Get-Location).Path,
        [string]$AssetRoot,
        [string]$ManifestPath,
        [string]$ManifestUrl,
        [string]$BaseUrl = $env:T850_MODEL_BASE_URL,
        [int]$MaxThreads = 7,
        [scriptblock]$StatusCallback
    )

    if (-not $AssetRoot) { $AssetRoot = Join-Path $RootDir 'Assets' }
    $manifestInfo = Read-T850CloudManifest -RootDir $RootDir -ManifestPath $ManifestPath -ManifestUrl $ManifestUrl
    $manifest = $manifestInfo.Manifest
    if (-not $manifest) {
        return [pscustomobject]@{ Ok = $true; Configured = $false; Downloaded = 0; Skipped = 0; Manifest = $null; Message = 'No model cloud manifest configured.' }
    }

    if (-not $BaseUrl -and $manifest.PSObject.Properties['baseUrl']) { $BaseUrl = [string]$manifest.baseUrl }
    if (-not $BaseUrl -and $manifest.PSObject.Properties['publicBaseUrl']) { $BaseUrl = [string]$manifest.publicBaseUrl }

    $entries = @(Convert-T850CloudManifestModels -Manifest $manifest)
    if ($entries.Count -eq 0) {
        return [pscustomobject]@{ Ok = $true; Configured = $false; Downloaded = 0; Skipped = 0; Manifest = $manifestInfo.Source; Message = 'Model cloud manifest has no models/assets.' }
    }

    $hasPerEntryUrls = @($entries | Where-Object { $_.PSObject.Properties['url'] -and $_.url }).Count -gt 0
    if (-not $BaseUrl -and -not $hasPerEntryUrls) {
        return [pscustomobject]@{ Ok = $true; Configured = $false; Downloaded = 0; Skipped = 0; Manifest = $manifestInfo.Source; Message = 'Model cloud manifest has no baseUrl; set T850_MODEL_BASE_URL or per-entry urls after public upload.' }
    }

    $allTasks = @(New-T850CloudDownloadTasks -Entries $entries -AssetRoot $AssetRoot -BaseUrl $BaseUrl)
    $downloadTasks = New-Object System.Collections.Generic.List[object]
    $skipped = 0
    $errors = New-Object System.Collections.Generic.List[string]

    foreach ($task in $allTasks) {
        if (Test-T850CloudModelFile -Entry $task.Entry -TargetPath $task.TargetPath) {
            $skipped++
            continue
        }
        if (-not $task.Url) {
            $errors.Add("No URL/baseUrl configured for $($task.ResourcePath)")
            continue
        }
        $downloadTasks.Add($task)
    }

    $total = $allTasks.Count
    $initialPercent = if ($total -gt 0) { [int][Math]::Floor(($skipped * 100.0) / $total) } else { 100 }
    $startMessage = "Model download progress: $skipped/$total ($initialPercent%) - downloaded=0 skipped=$skipped failed=0"
    if (-not $StatusCallback) { Write-Host ("[T850] " + $startMessage) }
    Invoke-T850CloudStatus -StatusCallback $StatusCallback -Message $startMessage

    if ($downloadTasks.Count -gt 0) {
        if (-not $StatusCallback) { Write-Host ("[T850] Starting $($downloadTasks.Count) download(s) with $([Math]::Max(1, [Math]::Min($MaxThreads, $downloadTasks.Count))) worker(s).") }
        $parallel = Invoke-T850ParallelDownloads -Tasks $downloadTasks.ToArray() -Skipped $skipped -Total $total -MaxThreads $MaxThreads -StatusCallback $StatusCallback
        foreach ($err in $parallel.Errors) { $errors.Add($err) }
        $downloaded = $parallel.Downloaded
    } else {
        $downloaded = 0
    }

    return [pscustomobject]@{
        Ok = ($errors.Count -eq 0)
        Configured = $true
        Downloaded = $downloaded
        Skipped = $skipped
        Manifest = $manifestInfo.Source
        Errors = @($errors)
        Message = if ($errors.Count -eq 0) { "Models ready ($downloaded downloaded, $skipped already present)." } else { ($errors -join [System.Environment]::NewLine) }
    }
}