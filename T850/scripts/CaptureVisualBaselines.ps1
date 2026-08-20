param(
    [ValidateSet("reference", "candidate")]
    [string]$RunSet = "reference",

    [ValidateSet("d3d11", "d3d12", "gl", "vulkan")]
    [string[]]$Apis = @("d3d11", "d3d12", "gl", "vulkan"),

    [string[]]$Cases = @(),
    [int]$Width = 1280,
    [int]$Height = 720,
    [double]$DumpSeconds = 5.0,
    [double]$FixedDeltaSeconds = (1.0 / 60.0),
    [int]$TimeoutSeconds = 240,
    [string]$ExePath,
    [string]$OutputRoot,
    [string]$ReplayFromRunSet,
    [switch]$Force,
    [switch]$KeepRawDumps,
    [switch]$ContinueOnError
)

$ErrorActionPreference = "Stop"

$sourceRoot = Split-Path -Parent $PSScriptRoot
if (-not $ExePath) {
    $ExePath = Join-Path $sourceRoot "bin\x64\Release\DayScene.exe"
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $sourceRoot "VisualBaselines"
}

$ExePath = [IO.Path]::GetFullPath($ExePath)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$exeDir = Split-Path -Parent $ExePath
$assetsRoot = Join-Path $sourceRoot "Assets"
$runRoot = Join-Path $OutputRoot $RunSet

if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "DayScene executable not found: $ExePath. Build x64 Release first."
}
if ($Width -le 0 -or $Height -le 0) {
    throw "Width and Height must be positive."
}
if ($DumpSeconds -lt 0.0) {
    throw "DumpSeconds must be non-negative."
}
if ($FixedDeltaSeconds -le 0.0 -or $FixedDeltaSeconds -gt 1.0) {
    throw "FixedDeltaSeconds must be greater than 0 and no more than 1 second."
}

$modelDamagedHelmet = "Models/DamagedHelmet.glb"
$modelAnimated = "Models/Tyrant.glb"
$modelDoomSlayer = "Models/doomslayer_cine_all_animations_no-damage_double-sided-opaque.glb"

$caseDefinitions = @(
    [pscustomobject]@{
        Id = "sandbox"
        Scene = 0
        ExtraArgs = @("--model", $modelDamagedHelmet, "--orbitYaw", "0.75")
        RequiredAssets = @("Models/DamagedHelmet.glb")
        Note = "SandboxScene with a fixed model and orbit yaw."
    },
    [pscustomobject]@{
        Id = "day"
        Scene = 1
        ExtraArgs = @()
        RequiredAssets = @("Models/SponzaEsc.glb", "Models/SkyBox.glb")
        Note = "DayScene runtime demo."
    },
    [pscustomobject]@{
        Id = "quake3"
        Scene = 2
        ExtraArgs = @("--model", $modelDamagedHelmet)
        RequiredAssets = @($modelDamagedHelmet)
        Note = "Quake3Mock with the static DamagedHelmet; animated coverage is provided by RagdollEditor."
    },
    [pscustomobject]@{
        Id = "ragdoll-editor"
        Scene = 3
        ExtraArgs = @("--model", $modelAnimated)
        RequiredAssets = @($modelAnimated)
        Note = "RagdollEditor with the animated Tyrant model."
    },
    [pscustomobject]@{
        Id = "scene-template-q3-jolt"
        Scene = 4
        ExtraArgs = @("--sceneFile", "Scenes/Q3/q3dm6_mod_3_jolt.t8scene")
        RequiredAssets = @("Scenes/Q3/q3dm6_mod_3_jolt.t8scene", "Models/Q3/q3dm6.glb", $modelDoomSlayer)
        MinimumVulkanAdapterRam = 4GB
        Note = "SceneTemplate with the Jolt Q3 authored scene."
    },
    [pscustomobject]@{
        Id = "scene-template-q3"
        Scene = 4
        ExtraArgs = @("--sceneFile", "Scenes/Q3/q3dm6_mod_3.t8scene")
        RequiredAssets = @("Scenes/Q3/q3dm6_mod_3.t8scene", "Models/Q3/q3dm6.glb", $modelDoomSlayer)
        MinimumVulkanAdapterRam = 4GB
        Note = "SceneTemplate with the non-Jolt Q3 authored scene."
    },
    [pscustomobject]@{
        Id = "scene-template-day"
        Scene = 4
        ExtraArgs = @("--sceneFile", "Scenes/DayScene.t8scene")
        RequiredAssets = @("Scenes/DayScene.t8scene", "Models/SponzaEsc.glb", "Models/SkyBox.glb")
        Note = "SceneTemplate with DayScene.t8scene."
    },
    [pscustomobject]@{
        Id = "scene-template-nexus"
        Scene = 4
        ExtraArgs = @("--sceneFile", "Scenes/Nexus.t8scene")
        RequiredAssets = @("Scenes/Nexus.t8scene", "Models/nexus_wars_terrain.glb", "Models/marine.glb")
        Note = "Nexus.t8scene; source models are not present in the public runtime manifest."
    },
    [pscustomobject]@{
        Id = "voxel-streaming"
        Scene = 5
        ExtraArgs = @()
        RequiredAssets = @()
        Note = "VoxelScene with generated, asynchronously streamed mutable terrain."
    }
)

if ($Cases.Count -gt 0) {
    $unknownCases = @($Cases | Where-Object { $_ -notin $caseDefinitions.Id })
    if ($unknownCases.Count -gt 0) {
        throw "Unknown case(s): $($unknownCases -join ', '). Available: $($caseDefinitions.Id -join ', ')"
    }
    $caseDefinitions = @($caseDefinitions | Where-Object { $_.Id -in $Cases })
}

function Get-PpmDimensions {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [IO.File]::OpenRead($Path)
    try {
        $buffer = New-Object byte[] 256
        $count = $stream.Read($buffer, 0, $buffer.Length)
        $header = [Text.Encoding]::ASCII.GetString($buffer, 0, $count)
        $match = [regex]::Match($header, '^P6\s+(\d+)\s+(\d+)\s+255\s')
        if (-not $match.Success) {
            throw "Unsupported PPM header in $Path"
        }
        return [pscustomobject]@{
            Width = [int]$match.Groups[1].Value
            Height = [int]$match.Groups[2].Value
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Get-PpmStats {
    param([Parameter(Mandatory = $true)][string]$Path)

    $bytes = [IO.File]::ReadAllBytes($Path)
    $header = [Text.Encoding]::ASCII.GetString($bytes, 0, [Math]::Min(256, $bytes.Length))
    $match = [regex]::Match($header, '^P6\s+(\d+)\s+(\d+)\s+255\s')
    if (-not $match.Success) {
        throw "Unsupported PPM header in $Path"
    }

    $offset = $match.Length
    $count = $bytes.Length - $offset
    [double]$sum = 0.0
    [double]$sumSquares = 0.0
    [int]$minimum = 255
    [int]$maximum = 0
    for ($index = $offset; $index -lt $bytes.Length; ++$index) {
        $value = [int]$bytes[$index]
        $sum += $value
        $sumSquares += $value * $value
        if ($value -lt $minimum) { $minimum = $value }
        if ($value -gt $maximum) { $maximum = $value }
    }
    $mean = $sum / $count
    $variance = [Math]::Max(0.0, ($sumSquares / $count) - ($mean * $mean))
    return [pscustomobject]@{
        MeanChannel = [Math]::Round($mean, 4)
        StandardDeviation = [Math]::Round([Math]::Sqrt($variance), 4)
        MinimumChannel = $minimum
        MaximumChannel = $maximum
    }
}

function Get-NewDumpDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Api,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Before
    )

    $after = @(Get-ChildItem -LiteralPath $exeDir -Directory -Filter "dumps_${Api}_*" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending)
    return $after | Where-Object { $_.FullName -notin $Before } | Select-Object -First 1
}

New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

$gitCommit = (& git -C $sourceRoot rev-parse HEAD 2>$null)
$gitStatus = @(& git -C $sourceRoot status --short 2>$null)
$gpuInfo = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue |
    Select-Object Name, DriverVersion, AdapterRAM)
$maximumAdapterRam = ($gpuInfo | Measure-Object AdapterRAM -Maximum).Maximum
$exeHash = (Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash
$records = New-Object System.Collections.Generic.List[object]
$failureCount = 0

$manifestPath = Join-Path $runRoot "manifest.json"
if ($Cases.Count -gt 0 -and (Test-Path -LiteralPath $manifestPath)) {
    $existingManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    foreach ($existingRecord in @($existingManifest.Captures)) {
        if ($existingRecord.Case -notin $Cases -or $existingRecord.Api -notin $Apis) {
            $records.Add($existingRecord)
        }
    }
}

foreach ($case in $caseDefinitions) {
    $missingAssets = @($case.RequiredAssets | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $assetsRoot $_))
    })

    foreach ($api in $Apis) {
        $caseDir = Join-Path (Join-Path $runRoot $case.Id) $api
        $minimumVulkanAdapterRam = if ($case.PSObject.Properties['MinimumVulkanAdapterRam']) {
            [uint64]$case.MinimumVulkanAdapterRam
        } else { 0 }
        if ($api -eq "vulkan" -and $minimumVulkanAdapterRam -gt 0 -and
            [uint64]$maximumAdapterRam -lt $minimumVulkanAdapterRam) {
            if (Test-Path -LiteralPath $caseDir) {
                Remove-Item -LiteralPath $caseDir -Recurse -Force
            }
            $requiredGiB = [Math]::Round($minimumVulkanAdapterRam / 1GB, 1)
            $detectedGiB = [Math]::Round([uint64]$maximumAdapterRam / 1GB, 1)
            Write-Warning "Skipping $($case.Id)/$api; requires ${requiredGiB} GiB VRAM, detected ${detectedGiB} GiB."
            $records.Add([pscustomobject]@{
                Case = $case.Id
                Api = $api
                Status = "skipped_hardware_limit"
                RequiredAdapterRamBytes = $minimumVulkanAdapterRam
                DetectedAdapterRamBytes = [uint64]$maximumAdapterRam
                Note = $case.Note
            })
            continue
        }
        if ($missingAssets.Count -gt 0) {
            if (Test-Path -LiteralPath $caseDir) {
                Remove-Item -LiteralPath $caseDir -Recurse -Force
            }
            Write-Warning "Skipping $($case.Id)/$api; missing: $($missingAssets -join ', ')"
            $records.Add([pscustomobject]@{
                Case = $case.Id
                Api = $api
                Status = "skipped_missing_assets"
                MissingAssets = $missingAssets
                Note = $case.Note
            })
            continue
        }

        if ((Test-Path -LiteralPath $caseDir) -and -not $Force) {
            throw "Capture already exists: $caseDir. Use -Force to replace it."
        }
        if (Test-Path -LiteralPath $caseDir) {
            Remove-Item -LiteralPath $caseDir -Recurse -Force
        }
        New-Item -ItemType Directory -Path $caseDir -Force | Out-Null

        $engineLog = Join-Path $caseDir "engine.log"
        $stdoutLog = Join-Path $caseDir "stdout.log"
        $stderrLog = Join-Path $caseDir "stderr.log"
        $before = @(Get-ChildItem -LiteralPath $exeDir -Directory -Filter "dumps_${api}_*" -ErrorAction SilentlyContinue |
            ForEach-Object FullName)

        $arguments = @(
            "--api", $api,
            "--scene", [string]$case.Scene,
            "--width", [string]$Width,
            "--height", [string]$Height,
            "--regressionFixedDt", $FixedDeltaSeconds.ToString([Globalization.CultureInfo]::InvariantCulture),
            "--logLevel", "info",
            "--logFile", $engineLog
        ) + $case.ExtraArgs

        $captureMode = "timed"
        $replaySnapshot = $null
        if ($ReplayFromRunSet) {
            $replaySnapshot = Join-Path (Join-Path (Join-Path (Join-Path $OutputRoot $ReplayFromRunSet) $case.Id) $api) "snapshot.json"
            if (-not (Test-Path -LiteralPath $replaySnapshot)) {
                throw "Replay snapshot not found: $replaySnapshot"
            }
            $arguments += @("--replaySnapshot", $replaySnapshot)
            $captureMode = "snapshot_replay"
        }
        else {
            $arguments += @("--dumpSnapshot-seconds", $DumpSeconds.ToString([Globalization.CultureInfo]::InvariantCulture))
        }

        Write-Host "[baseline] $RunSet $($case.Id)/$api (${Width}x${Height}, ${DumpSeconds}s)"
        $started = Get-Date
        $process = Start-Process -FilePath $ExePath -ArgumentList $arguments -WorkingDirectory $exeDir `
            -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog -PassThru
        $completed = $process.WaitForExit($TimeoutSeconds * 1000)
        if (-not $completed) {
            $process.Kill($true)
            $process.WaitForExit()
            $message = "Timed out after $TimeoutSeconds seconds."
            $failureCount++
            $records.Add([pscustomobject]@{
                Case = $case.Id; Api = $api; Status = "timeout"; Error = $message; Note = $case.Note
            })
            if (-not $ContinueOnError) { throw "$($case.Id)/${api}: $message" }
            continue
        }
        $process.WaitForExit()
        $exitCode = $process.ExitCode
        $dumpDir = Get-NewDumpDirectory -Api $api -Before $before
        if ($exitCode -ne 0 -or -not $dumpDir) {
            $message = "ExitCode=$exitCode; new dump directory found=$([bool]$dumpDir)."
            $failureCount++
            $records.Add([pscustomobject]@{
                Case = $case.Id; Api = $api; Status = "failed"; Error = $message; Note = $case.Note
            })
            if (-not $ContinueOnError) { throw "$($case.Id)/${api}: $message See $caseDir" }
            continue
        }

        $backBuffer = Join-Path $dumpDir.FullName "RT_Dump_BackBuffer.ppm"
        if (-not (Test-Path -LiteralPath $backBuffer)) {
            throw "$($case.Id)/$api did not produce RT_Dump_BackBuffer.ppm in $($dumpDir.FullName)"
        }
        $dimensions = Get-PpmDimensions -Path $backBuffer
        if ($dimensions.Width -ne $Width -or $dimensions.Height -ne $Height) {
            throw "$($case.Id)/$api produced $($dimensions.Width)x$($dimensions.Height), expected ${Width}x${Height}."
        }

        $imageStats = Get-PpmStats -Path $backBuffer
        $errorLines = @()
        foreach ($logPath in @($engineLog, $stdoutLog, $stderrLog)) {
            if (Test-Path -LiteralPath $logPath) {
                $errorLines += @(Select-String -LiteralPath $logPath -Pattern '\[ERROR\]|fatal error|device lost|vkQueueSubmit failed' |
                    ForEach-Object Line | Select-Object -Unique)
            }
        }

        $savedBackBuffer = Join-Path $caseDir "RT_Dump_BackBuffer.ppm"
        Copy-Item -LiteralPath $backBuffer -Destination $savedBackBuffer -Force
        $snapshotPath = Join-Path $dumpDir.FullName "snapshot.json"
        if (Test-Path -LiteralPath $snapshotPath) {
            Copy-Item -LiteralPath $snapshotPath -Destination (Join-Path $caseDir "snapshot.json") -Force
        }

        $ended = Get-Date
        $record = [pscustomobject]@{
            Case = $case.Id
            Api = $api
            Status = "captured"
            Scene = $case.Scene
            ExtraArgs = $case.ExtraArgs
            Width = $dimensions.Width
            Height = $dimensions.Height
            DumpSeconds = $DumpSeconds
            FixedDeltaSeconds = $FixedDeltaSeconds
            CaptureMode = $captureMode
            ReplaySnapshot = $replaySnapshot
            DurationSeconds = [math]::Round(($ended - $started).TotalSeconds, 3)
            ExitCode = $exitCode
            EngineErrorCount = $errorLines.Count
            EngineErrors = $errorLines | Select-Object -First 20
            ImageStats = $imageStats
            Screenshot = "$($case.Id)/$api/RT_Dump_BackBuffer.ppm"
            Sha256 = (Get-FileHash -LiteralPath $savedBackBuffer -Algorithm SHA256).Hash
            SourceDump = $dumpDir.Name
            Note = $case.Note
        }

        if ($errorLines.Count -gt 0 -or $imageStats.StandardDeviation -lt 1.0) {
            $record.Status = "invalid_capture"
            $record | Add-Member -NotePropertyName Error -NotePropertyValue (
                "EngineErrors=$($errorLines.Count); ImageStdDev=$($imageStats.StandardDeviation)")
            $failureCount++
        }
        $records.Add($record)
        $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $caseDir "capture.json") -Encoding utf8

        if (-not $KeepRawDumps) {
            Remove-Item -LiteralPath $dumpDir.FullName -Recurse -Force
        }
        if ($record.Status -ne "captured" -and -not $ContinueOnError) {
            throw "$($case.Id)/${api}: $($record.Error). See $caseDir"
        }
    }
}

$manifest = [ordered]@{
    SchemaVersion = 1
    RunSet = $RunSet
    CapturedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    GitCommit = $gitCommit
    GitDirty = ($gitStatus.Count -gt 0)
    GitStatus = $gitStatus
    Executable = $ExePath
    ExecutableSha256 = $exeHash
    Width = $Width
    Height = $Height
    DumpSeconds = $DumpSeconds
    FixedDeltaSeconds = $FixedDeltaSeconds
    ReplayFromRunSet = $ReplayFromRunSet
    Apis = $Apis
    Gpu = $gpuInfo
    Captures = $records
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8

$capturedCount = @($records | Where-Object Status -eq "captured").Count
$skippedCount = @($records | Where-Object Status -like "skipped*").Count
Write-Host "[baseline] Captured=$capturedCount Skipped=$skippedCount Failed=$failureCount"
Write-Host "[baseline] Manifest: $manifestPath"
if ($failureCount -gt 0) { exit 1 }
exit 0