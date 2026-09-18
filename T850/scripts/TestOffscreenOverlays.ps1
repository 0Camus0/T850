[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Config = 'Debug',
    [ValidateSet('d3d11', 'd3d12', 'vulkan', 'gl', 'webgpu')]
    [string[]]$Apis = @('vulkan', 'webgpu'),
    [ValidateRange(0, 6)][int[]]$Scenes = @(1, 6),
    [ValidateSet('auto', 'wgsl', 'spirv')][string]$ShaderFlow = 'auto',
    [ValidateRange(1, 100000)][int]$ProfileFrames = 120,
    [ValidateRange(1, 3600)][int]$TimeoutSeconds = 180,
    [string]$OutputDirectory,
    [switch]$Capture
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$workingDirectory = Join-Path $sourceRoot "bin/x64/$Config"
$executable = Join-Path $workingDirectory 'DayScene.exe'
if (!(Test-Path $executable)) { throw "Build $Config x64 first: $executable" }
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $env:LOCALAPPDATA "T850Profiles/offscreen-overlays-$(Get-Date -Format yyyyMMdd-HHmmss)"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($OutputDirectory)
if ($Capture) { Add-Type -AssemblyName PresentationCore }

function Convert-Capture([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes, 0, [Math]::Min(128, $bytes.Length)), '^P6\s+640\s+360\s+255\s')
    if (!$header.Success -or $bytes.Length -ne $header.Length + 640 * 360 * 3) {
        throw "Unexpected or truncated offscreen capture: $Path"
    }
    $pixels = [byte[]]::new(640 * 360 * 3)
    [Array]::Copy($bytes, $header.Length, $pixels, 0, $pixels.Length)
    $colors = [Collections.Generic.HashSet[int]]::new()
    for ($offset = 0; $offset -lt $pixels.Length; $offset += 93) {
        [void]$colors.Add(([int]$pixels[$offset] -shl 16) -bor ([int]$pixels[$offset + 1] -shl 8) -bor [int]$pixels[$offset + 2])
    }
    if ($colors.Count -lt 2) { throw "Uniform offscreen capture: $Path" }
    $image = [Windows.Media.Imaging.BitmapSource]::Create(640, 360, 96, 96, [Windows.Media.PixelFormats]::Rgb24, $null, $pixels, 640 * 3)
    $encoder = [Windows.Media.Imaging.PngBitmapEncoder]::new()
    $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($image))
    $stream = [IO.File]::Create([IO.Path]::ChangeExtension($Path, '.png'))
    try { $encoder.Save($stream) } finally { $stream.Dispose() }
}

$results = foreach ($api in $Apis) {
    foreach ($scene in $Scenes) {
    foreach ($mode in @('surface', 'offscreen')) {
                $caseName = "$api-scene$scene-$mode-$ShaderFlow"
        $caseDirectory = Join-Path $OutputDirectory $caseName
        if (Test-Path $caseDirectory) { throw "Use a fresh output directory: $caseDirectory" }
        [void][IO.Directory]::CreateDirectory($caseDirectory)
        $log = Join-Path $caseDirectory 'engine.log'
        $arguments = @('--api', $api, '--scene', "$scene", '--profile', '--profileFrames', "$ProfileFrames",
            '--width', '640', '--height', '360', '--regressionFixedDt', '0.0166666667',
            '--logLevel', 'info', '--logFile', $log)
        if ($api -eq 'webgpu') { $arguments += @('--shaderFlow', $ShaderFlow) }
        if ($api -eq 'd3d12') { $arguments += '--d3d12debug' }
        if ($scene -eq 4) { $arguments += @('--sceneFile', 'Scenes/DayScene.t8scene') }
        if ($mode -eq 'offscreen') {
            $arguments += '--offscreen'
            if ($Capture) { $arguments += '--offscreenDebug' }
        }
        $before = @(Get-ChildItem $workingDirectory -Directory -Filter "dumps_${api}_offscreen_*" | Select-Object -ExpandProperty FullName)
        $quotedArguments = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
        Write-Output "Running $caseName ($ProfileFrames frames)" | Out-Host
        $process = Start-Process $executable -WorkingDirectory $workingDirectory -ArgumentList $quotedArguments -PassThru `
            -RedirectStandardOutput (Join-Path $caseDirectory 'stdout.log') -RedirectStandardError (Join-Path $caseDirectory 'stderr.log')
        $null = $process.Handle
        try {
            if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
                $process.Kill()
                $process.WaitForExit()
                throw "Overlay test timed out: $caseName"
            }
            if ($process.ExitCode -ne 0) { throw "Overlay test exited $($process.ExitCode): $log" }
        } finally {
            if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
        }
        $text = Get-Content $log -Raw
        if ($text -match '\[ERROR\]|\[Profiler\] WARNING|VUID-|DEVICE_REMOVED|DEVICE_LOST') {
            throw "Overlay validation failed: $log"
        }
        if ($text -notmatch "REPORT \($ProfileFrames frames\)" -or $text -notmatch 'CPU_N=[1-9]') {
            throw "Missing completed profiler report: $log"
        }
        $captureCount = 0
        if ($Capture -and $mode -eq 'offscreen') {
            $dumps = [regex]::Matches($text, '\[Offscreen\] Debug dumps -> ([^/\r\n]+)/')
            if ($dumps.Count -eq 0) { throw "No timed capture; increase -ProfileFrames and retry in a fresh directory: $log" }
            foreach ($dumpName in @($dumps | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)) {
                if ([IO.Path]::GetFileName($dumpName) -ne $dumpName) { throw "Unexpected dump path: $dumpName" }
                $dumpDirectory = Join-Path $workingDirectory $dumpName
                if ($before -contains $dumpDirectory) { throw "Refusing to move an existing capture: $dumpDirectory" }
                $destination = Join-Path $caseDirectory $dumpName
                Move-Item $dumpDirectory $destination
                foreach ($frame in Get-ChildItem $destination -Filter 'RT_Dump_Offscreen_*.ppm') {
                    Convert-Capture $frame.FullName
                    ++$captureCount
                }
            }
            if ($captureCount -eq 0) { throw "No offscreen pixels captured: $caseDirectory" }
        }
        [pscustomobject]@{api=$api; scene=$scene; mode=$mode; shaderFlow=$ShaderFlow; frames=$ProfileFrames; captures=$captureCount; result='PASS'; log=$log}
    }
  }
}
$results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
$results | Format-Table api, scene, mode, shaderFlow, frames, captures, result
Write-Output "Offscreen overlay checks PASS. Evidence: $OutputDirectory"