[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Config = 'Debug',
    [string[]]$Apis = @('d3d11', 'd3d12', 'vulkan', 'gl'),
    [int]$Width = 1440,
    [int]$Height = 900,
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$directory = Join-Path $sourceRoot "bin\x64\$Config"
$executable = Join-Path $directory 'T8ditor.exe'
if (-not (Test-Path $executable)) { throw "Build T8ditor $Config x64 first." }
Add-Type -AssemblyName System.Drawing

function Convert-Capture {
    param([string]$Path, [int]$ExpectedWidth, [int]$ExpectedHeight)
    $bytes = [IO.File]::ReadAllBytes($Path)
    $offset = 0
    $tokens = @()
    while ($tokens.Count -lt 4) {
        while ($offset -lt $bytes.Length -and $bytes[$offset] -le 32) { ++$offset }
        $start = $offset
        while ($offset -lt $bytes.Length -and $bytes[$offset] -gt 32) { ++$offset }
        if ($start -eq $offset) { throw 'Truncated capture header' }
        $tokens += [Text.Encoding]::ASCII.GetString($bytes, $start, $offset - $start)
    }
    ++$offset
    if ($tokens[0] -ne 'P6' -or $tokens[3] -ne '255' -or
        [int]$tokens[1] -ne $ExpectedWidth -or [int]$tokens[2] -ne $ExpectedHeight) {
        throw "Unexpected capture format/dimensions: $($tokens -join ' ')"
    }
    $pixelCount = $ExpectedWidth * $ExpectedHeight
    if ($bytes.Length - $offset -ne $pixelCount * 3) { throw 'Truncated capture pixels' }
    $samples = for ($index = $offset; $index -lt $bytes.Length; $index += 300) { [double]$bytes[$index] }
    $average = ($samples | Measure-Object -Average).Average
    $variance = (($samples | ForEach-Object { ($_ - $average) * ($_ - $average) }) | Measure-Object -Average).Average
    if ([Math]::Sqrt($variance) -lt 1) { throw 'Blank or uniform terrain capture' }
    $bitmap = New-Object Drawing.Bitmap $ExpectedWidth, $ExpectedHeight
    try {
        $locked = $bitmap.LockBits([Drawing.Rectangle]::new(0, 0, $ExpectedWidth, $ExpectedHeight),
            [Drawing.Imaging.ImageLockMode]::WriteOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $pixels = New-Object byte[] ($pixelCount * 4)
            for ($pixel = 0; $pixel -lt $pixelCount; ++$pixel) {
                $pixels[$pixel * 4] = $bytes[$offset + $pixel * 3 + 2]
                $pixels[$pixel * 4 + 1] = $bytes[$offset + $pixel * 3 + 1]
                $pixels[$pixel * 4 + 2] = $bytes[$offset + $pixel * 3]
                $pixels[$pixel * 4 + 3] = 255
            }
            [Runtime.InteropServices.Marshal]::Copy($pixels, 0, $locked.Scan0, $pixels.Length)
        } finally {
            $bitmap.UnlockBits($locked)
        }
        $output = [IO.Path]::ChangeExtension($Path, '.png')
        $bitmap.Save($output, [Drawing.Imaging.ImageFormat]::Png)
        return $output
    } finally {
        $bitmap.Dispose()
    }
}

foreach ($api in $Apis) {
    if ($api -notin @('d3d11', 'd3d12', 'vulkan', 'gl')) { throw "Unknown API: $api" }
    $runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $log = "logs/terrain-editor-$api-${Width}x${Height}-$runId.log"
    $arguments = "--api $api --sceneFile Scenes/HeightmapExample.t8scene --terrain-editor-selftest --width $Width --height $Height --dump-frame 30 --logLevel info --logFile $log"
    $process = Start-Process $executable -WorkingDirectory $directory -ArgumentList $arguments -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id
        throw "Terrain editor test timed out: $api"
    }
    $text = Get-Content (Join-Path $directory $log) -Raw
    if ($process.ExitCode -ne 0 -or $text -match '\[ERROR\]|device lost|fatal error') {
        throw "Terrain editor test failed: $api exit=$($process.ExitCode). Log: $directory\$log"
    }
    if ($text -notmatch '\[TerrainEditorTest\] PASS hosted Play shutdown and editor restoration') {
        throw "Terrain editor regression did not finish: $api"
    }
    if ($text -notmatch '\[TerrainEditorTest\] PASS stable preview camera and forwarded movement input') {
        throw "Terrain Play camera/input regression did not finish: $api"
    }
    if ($text -notmatch '\[TerrainEditorTest\] PASS placement flatness, occupancy, undo/redo, removal, navigation exclusion') {
        throw "Terrain placement regression did not finish: $api"
    }
    $dump = [regex]::Match($text, 'RT dump complete -> ([^/\r\n]+)')
    if (-not $dump.Success) { throw "Missing capture: $api" }
    $capture = Join-Path $directory ($dump.Groups[1].Value + '/RT_Dump_BackBuffer.ppm')
    $png = Convert-Capture $capture $Width $Height
    Write-Host "PASS $api terrain brush/paint/collision/undo/clone/reload/Play/camera/input/LOD ${Width}x${Height}"
    Write-Host "Capture: $png"
}