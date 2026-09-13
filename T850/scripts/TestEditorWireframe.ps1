[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Config = 'Debug',
    [ValidateSet('d3d11','d3d12','vulkan')][string[]]$Apis = @('d3d11','d3d12','vulkan'),
    [string]$Executable, [string]$WorkingDirectory,
    [switch]$Far, [switch]$Thin)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
if (!$Executable) { $Executable = Join-Path $sourceRoot "bin/x64/$Config/T8ditor.exe" }
if (!$WorkingDirectory) { $WorkingDirectory = Split-Path $Executable -Parent }
Add-Type -AssemblyName PresentationCore

function Read-Capture([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes, 0, 128), '^P6\s+(\d+)\s+(\d+)\s+255\s')
    if (!$header.Success -or $header.Groups[1].Value -ne '800' -or $header.Groups[2].Value -ne '600') { throw 'Unexpected framebuffer format' }
    $pixels = [byte[]]::new(800 * 600 * 3)
    if ($bytes.Length -ne $header.Length + $pixels.Length) { throw 'Truncated framebuffer' }
    [Array]::Copy($bytes, $header.Length, $pixels, 0, $pixels.Length)
    $image = [Windows.Media.Imaging.BitmapSource]::Create(800,600,96,96,[Windows.Media.PixelFormats]::Rgb24,$null,$pixels,2400)
    $encoder = [Windows.Media.Imaging.PngBitmapEncoder]::new()
    $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($image))
    $stream = [IO.File]::Create([IO.Path]::ChangeExtension($Path, '.png'))
    try { $encoder.Save($stream) } finally { $stream.Dispose() }
    return ,$pixels
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ("t850-wire-depth-$([Guid]::NewGuid().ToString('N'))")
$fixture = Join-Path $temporary 'occlusion.t8scene'
$scene = @{
    version = 2
    render_graph = 'Scenes/SceneTemplate_RenderGraph.json'
    control_descriptor = 'Scenes/SceneTemplate.json'
    objects = @(
        @{ name = 'Rear selected plane'; position = @{x=-6;y=0;z=-6}; heightmap = @{size_x=12;size_z=12;samples_x=17;samples_z=17;base_color=@{x=0.15;y=0.25;z=0.15}}; show_wire=$false },
        @{ name = 'Opaque occluder'; position = @{x=-2;y=2;z=-2}; heightmap = @{size_x=4;size_z=4;samples_x=2;samples_z=2;base_color=@{x=0.05;y=0.15;z=0.55}}; show_wire=$false }
    )
    lights = @(@{id='sun';name='Sun';type=0;position=@{x=0;y=10;z=0};direction=@{x=0;y=-1;z=0};intensity=2})
}
try {
    [void][IO.Directory]::CreateDirectory($temporary)
    if ($Thin) { $scene.objects[1].position.y = 0.025 }
    if ($Far) {
        foreach ($object in $scene.objects) {
            $object.position.x *= 125
            $object.position.y *= 125
            $object.position.z *= 125
            $object.heightmap.size_x *= 125
            $object.heightmap.size_z *= 125
        }
    }
    $scene | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $fixture -Encoding UTF8
    foreach ($api in $Apis) {
        if ($api -notin @('d3d11','d3d12','vulkan','gl')) { throw "Unknown API: $api" }
        $captures = @{}
        foreach ($mode in 'off','on') {
            $step = if ($Far) { "wireframe-depth-far-$mode" } else { "wireframe-depth-$mode" }
            $log = "logs/wire-depth-$api-$mode-$([Guid]::NewGuid().ToString('N')).log"
            $process = Start-Process $Executable -WorkingDirectory $WorkingDirectory -ArgumentList "--api $api --sceneFile `"$fixture`" --tutorial-step $step --width 800 --height 600 --dump-frame 30 --logLevel info --logFile $log" -PassThru
            if (!$process.WaitForExit(60000)) { Stop-Process -Id $process.Id; throw 'Wireframe capture timed out' }
            $text = Get-Content (Join-Path $WorkingDirectory $log) -Raw
            if ($process.ExitCode -ne 0 -or $text -match '\[ERROR\]|device lost' -or $text -notmatch '\[WireframeDepthTest\]') { throw "Wireframe capture failed: $log" }
            $dump = [regex]::Match($text, 'RT dump complete -> ([^/\r\n]+)')
            if (!$dump.Success) { throw 'No wireframe capture produced' }
            $path = Join-Path $WorkingDirectory "$($dump.Groups[1].Value)/RT_Dump_BackBuffer.ppm"
            $captures[$mode] = Read-Capture $path
            Write-Output "Capture $mode : $path"
        }
        $hidden = 0
        $visible = 0
        for ($vertical = 120; $vertical -lt 540; ++$vertical) {
            for ($horizontal = 120; $horizontal -lt 680; ++$horizontal) {
                $offset = ($vertical * 800 + $horizontal) * 3
                $wire = $true
                for ($channel = 0; $channel -lt 3; ++$channel) {
                    $wire = $wire -and $captures.on[$offset+$channel] -gt 220 -and
                        ([int]$captures.on[$offset+$channel] - [int]$captures.off[$offset+$channel]) -gt 30
                }
                if (!$wire) { continue }
                if ($horizontal -ge 360 -and $horizontal -lt 440 -and $vertical -ge 240 -and $vertical -lt 290) { ++$hidden }
                else { ++$visible }
            }
        }
        Write-Output "$api hidden wire pixels=$hidden; visible wire pixels=$visible"
        if ($hidden -gt 2) { throw "$api selected wireframe bleeds through the opaque foreground plane" }
        if ($visible -lt 50) { throw "$api visible selected wireframe disappeared" }
        Write-Output "PASS $api selected wireframe occlusion"
    }
} finally { if (Test-Path $temporary) { Remove-Item $temporary -Recurse -Force } }