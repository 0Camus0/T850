[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Config = 'Debug',
    [ValidateSet('d3d11', 'd3d12', 'vulkan', 'gl')][string]$Api = 'd3d12',
    [string[]]$Steps = @('file-menu', 'save-menu', 'empty-scene', 'view-menu', 'grid', 'flat-import', 'image-import', 'flat-terrain',
        'heightmap-terrain', 'transform', 'raise', 'flatten', 'paint-material', 'footprint', 'placement-placed',
        'placement-overlap', 'placement-bounds', 'placement-slope', 'placement-list', 'placement-removed',
        'unit-marker', 'layout', 'physics', 'navigation-settings', 'navigation-links', 'navigation-result', 'reloaded', 'play', 'stopped'),
    [string]$OutputRoot = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$directory = Join-Path $sourceRoot "bin\x64\$Config"
if (-not $OutputRoot) { $OutputRoot = Join-Path (Split-Path $sourceRoot -Parent) 'documentation\tutorials\images' }
[IO.Directory]::CreateDirectory($OutputRoot) | Out-Null
$executable = Join-Path $directory 'T8ditor.exe'
if (-not (Test-Path $executable)) { throw "Build T8ditor $Config x64 first." }

Add-Type -AssemblyName System.Drawing

foreach ($step in $Steps) {
    if ($step -notmatch '^[a-z-]+$') { throw "Invalid tutorial step name: $step" }
    $output = Join-Path $OutputRoot "$step.png"
    if ((Test-Path $output) -and -not $Force) { throw "Capture already exists: $output. Use -Force to replace it." }
    $id = [Guid]::NewGuid().ToString('N')
    $log = "logs/tutorial-$step-$id.log"
    $arguments = "--api $Api --sceneFile Scenes/RtsBlockout.t8scene --width 1440 --height 900 --tutorial-step $step --dump-frame 45 --logLevel info --logFile $log"
    $process = $null
    try {
        $process = Start-Process $executable -WorkingDirectory $directory -ArgumentList $arguments -PassThru
        if (-not $process.WaitForExit(120000)) { throw "Tutorial capture timed out: $step" }
        $text = Get-Content (Join-Path $directory $log) -Raw
        if ($process.ExitCode -ne 0 -or $text -match '\[ERROR\]' -or $text -notmatch "\[TutorialCapture\] Ready $step") { throw "Tutorial capture failed: $log" }
        $dump = [regex]::Match($text, 'RT dump complete -> ([^/\r\n]+)')
        if (-not $dump.Success) { throw "Missing framebuffer: $step" }
        $path = Join-Path $directory ($dump.Groups[1].Value + '/RT_Dump_BackBuffer.ppm')
        $bytes = [IO.File]::ReadAllBytes($path)
        $offset = 0
        $tokens = @()
        while ($tokens.Count -lt 4) {
            while ($bytes[$offset] -le 32) { ++$offset }
            $start = $offset
            while ($bytes[$offset] -gt 32) { ++$offset }
            $tokens += [Text.Encoding]::ASCII.GetString($bytes, $start, $offset - $start)
        }
        ++$offset
        if ($tokens[0] -ne 'P6' -or $tokens[3] -ne '255') { throw 'Unsupported capture format' }
        $width = [int]$tokens[1]
        $height = [int]$tokens[2]
        if ($bytes.Length - $offset -ne $width * $height * 3) { throw 'Incomplete framebuffer' }
        $bitmap = [Drawing.Bitmap]::new($width, $height)
        try {
            $locked = $bitmap.LockBits([Drawing.Rectangle]::new(0, 0, $width, $height), [Drawing.Imaging.ImageLockMode]::WriteOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try {
                $pixels = New-Object byte[] ($width * $height * 4)
                for ($pixel = 0; $pixel -lt $width * $height; ++$pixel) {
                    $pixels[$pixel * 4] = $bytes[$offset + $pixel * 3 + 2]
                    $pixels[$pixel * 4 + 1] = $bytes[$offset + $pixel * 3 + 1]
                    $pixels[$pixel * 4 + 2] = $bytes[$offset + $pixel * 3]
                    $pixels[$pixel * 4 + 3] = 255
                }
                [Runtime.InteropServices.Marshal]::Copy($pixels, 0, $locked.Scan0, $pixels.Length)
            } finally { $bitmap.UnlockBits($locked) }
            $sum = 0.0
            $square = 0.0
            $count = 0
            for ($row = 0; $row -lt $height; $row += 10) {
                for ($column = 0; $column -lt $width; $column += 10) {
                    $color = $bitmap.GetPixel($column, $row)
                    $value = ($color.R + $color.G + $color.B) / 3.0
                    $sum += $value
                    $square += $value * $value
                    ++$count
                }
            }
            if ($square / $count - ($sum / $count) * ($sum / $count) -lt 1) { throw "Uniform tutorial screenshot: $step" }
            $bitmap.Save($output, [Drawing.Imaging.ImageFormat]::Png)
        } finally { $bitmap.Dispose() }
        $dimensions = "${width}x${height}"
        Write-Host "PASS $step $dimensions $output"
    } finally {
        if ($process -and -not $process.HasExited) {
            $process.CloseMainWindow() | Out-Null
            if (-not $process.WaitForExit(15000)) { Stop-Process -Id $process.Id }
        }
    }
}