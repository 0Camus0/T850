param([switch]$Force)

$ErrorActionPreference = 'Stop'
$output = Join-Path (Split-Path $PSScriptRoot -Parent) 'Assets\Textures\Terrain\HeightmapExample.bmp'
if ((Test-Path $output) -and -not $Force) { throw "Image already exists: $output. Use -Force to regenerate." }
Add-Type -AssemblyName System.Drawing
[System.IO.Directory]::CreateDirectory((Split-Path $output -Parent)) | Out-Null
$bitmap = New-Object System.Drawing.Bitmap 65,65
try {
    for ($row = 0; $row -lt 65; ++$row) {
        for ($column = 0; $column -lt 65; ++$column) {
            $horizontal = $column / 64.0
            $vertical = $row / 64.0
            $height = 0.15 + 0.65 * [Math]::Pow([Math]::Sin($horizontal * [Math]::PI), 2) * [Math]::Pow([Math]::Sin($vertical * [Math]::PI), 2)
            $gray = [int][Math]::Round($height * 255)
            $bitmap.SetPixel($column, $row, [System.Drawing.Color]::FromArgb($gray, $gray, $gray))
        }
    }
    $bitmap.Save($output, [System.Drawing.Imaging.ImageFormat]::Bmp)
} finally {
    $bitmap.Dispose()
}
Write-Host "Generated $output"