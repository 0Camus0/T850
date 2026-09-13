[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Config = 'Debug',
    [ValidateSet('d3d11','d3d12','vulkan')][string[]]$Apis = @('d3d11','d3d12','vulkan'),
    [string]$Executable, [string]$WorkingDirectory,
    [switch]$Far, [switch]$Thin, [switch]$PlacementGrid)
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
    if ($PlacementGrid) {
        if ($Thin -or $Far) { throw 'PlacementGrid cannot be combined with Thin/Far' }
        $scene.objects = @(@{
            name = 'Selected blockout terrain'; position = @{x=-32;y=0;z=-32}; show_wire=$false
            heightmap = @{
                size_x=64;size_z=64;samples_x=65;samples_z=65;base_color=@{x=0.22;y=0.38;z=0.26}
                placement_grid=@{enabled=$true;cell_size=2}
                placements=@(
                    @{id='blue-base';name='Blue Blockout';cell_x=5;cell_z=6;width=4;depth=4;height=5;color=@{x=0.15;y=0.45;z=0.85}},
                    @{id='red-base';name='Red Blockout';cell_x=23;cell_z=22;width=4;depth=4;height=5;color=@{x=0.8;y=0.15;z=0.12}}
                )
            }
        })
    }
    $scene | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $fixture -Encoding UTF8
    foreach ($api in $Apis) {
        if ($api -notin @('d3d11','d3d12','vulkan','gl')) { throw "Unknown API: $api" }
        $captures = @{}
        foreach ($mode in 'off','on') {
            $step = if ($Far) { "wireframe-depth-far-$mode" } else { "wireframe-depth-$mode" }
            if ($PlacementGrid) { $step = "placement-depth-$mode" }
            $log = "logs/wire-depth-$api-$mode-$([Guid]::NewGuid().ToString('N')).log"
            $process = Start-Process $Executable -WorkingDirectory $WorkingDirectory -ArgumentList "--api $api --sceneFile `"$fixture`" --tutorial-step $step --width 800 --height 600 --dump-frame 30 --logLevel info --logFile $log" -PassThru
            if (!$process.WaitForExit(60000)) { Stop-Process -Id $process.Id; throw 'Wireframe capture timed out' }
            $text = Get-Content (Join-Path $WorkingDirectory $log) -Raw
            if ($process.ExitCode -ne 0 -or $text -match '\[ERROR\]|device lost' -or $text -notmatch '\[WireframeDepthTest\]') { throw "Wireframe capture failed: $log" }
            $dump = [regex]::Match($text, 'RT dump complete -> ([^/\r\n]+)')
            if (!$dump.Success) { throw 'No wireframe capture produced' }
            $path = Join-Path $WorkingDirectory "$($dump.Groups[1].Value)/RT_Dump_BackBuffer.ppm"
            $captures[$mode] = Read-Capture $path
            if ($PlacementGrid -and $mode -eq 'off') {
                $captures.albedo = Read-Capture (Join-Path $WorkingDirectory "$($dump.Groups[1].Value)/RT_Dump_GBuffer_Albedo.ppm")
            }
            Write-Output "Capture $mode : $path"
        }
        if ($PlacementGrid) {
            $occluded = @{red=0;blue=0}
            $covered = @{red=0;blue=0}
            $ground = 0
            for ($vertical = 110; $vertical -lt 570; ++$vertical) {
                for ($horizontal = 60; $horizontal -lt 740; ++$horizontal) {
                    $offset = ($vertical * 800 + $horizontal) * 3
                    $red = [int]$captures.off[$offset]
                    $green = [int]$captures.off[$offset+1]
                    $blue = [int]$captures.off[$offset+2]
                    $changed = [Math]::Abs($red-[int]$captures.on[$offset]) + [Math]::Abs($green-[int]$captures.on[$offset+1]) + [Math]::Abs($blue-[int]$captures.on[$offset+2]) -gt 24
                    if ($changed -and $green -gt $red*1.2 -and $green -gt $blue*1.1) { ++$ground }
                    $materialRed = $captures.albedo[$offset]
                    $materialGreen = $captures.albedo[$offset+1]
                    $materialBlue = $captures.albedo[$offset+2]
                    $color = if ($materialRed -gt 80 -and $materialRed -gt $materialGreen*1.6 -and $materialRed -gt $materialBlue*1.6) { 'red' }
                        elseif ($materialBlue -gt 80 -and $materialBlue -gt $materialRed*1.6 -and $materialBlue -gt $materialGreen*1.2) { 'blue' } else { '' }
                    if (!$color) { continue }
                    $interior = $true
                    foreach ($rowOffset in -3,0,3) {
                        foreach ($columnOffset in -3,0,3) {
                            $sample = $offset + ($rowOffset*800 + $columnOffset)*3
                            if ($color -eq 'red') { $interior = $interior -and $captures.albedo[$sample] -gt $captures.albedo[$sample+1]*1.6 -and $captures.albedo[$sample] -gt $captures.albedo[$sample+2]*1.6 }
                            else { $interior = $interior -and $captures.albedo[$sample+2] -gt $captures.albedo[$sample]*1.6 -and $captures.albedo[$sample+2] -gt $captures.albedo[$sample+1]*1.2 }
                        }
                    }
                    if (!$interior) { continue }
                    ++$covered[$color]
                    if ($changed) { ++$occluded[$color] }
                }
            }
            Write-Output "$api placement grid: red=$($occluded.red)/$($covered.red), blue=$($occluded.blue)/$($covered.blue) occluded pixels changed; visible ground=$ground"
            if ($covered.red -lt 200 -or $covered.blue -lt 200) { throw 'Blockout occlusion masks are empty or too small' }
            if ($occluded.red -gt 2 -or $occluded.blue -gt 2) { throw "$api placement grid draws on top of blockouts" }
            if ($ground -lt 18000) { throw "$api exposed placement grid coverage regressed" }
            Write-Output "PASS $api terrain placement grid occlusion"
            continue
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
        if (!$Thin -and !$Far -and $visible -lt 9000) { throw "$api coplanar wireframe coverage regressed" }
        Write-Output "PASS $api selected wireframe occlusion"
    }
} finally { if (Test-Path $temporary) { Remove-Item $temporary -Recurse -Force } }