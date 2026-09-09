[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ModelPath,
    [string]$Animation = '',
    [uint32[]]$HiddenGeometry = @(),
    [string]$OutputPath = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$model = Get-Item -LiteralPath $ModelPath
if ($model.Extension -ine '.glb') { throw 'This local example requires a self-contained GLB.' }
$destination = Join-Path $sourceRoot 'Assets\Models\PlacementBuilding.glb'
if (Test-Path -LiteralPath $destination) {
    if ((Get-FileHash -LiteralPath $destination).Hash -ne (Get-FileHash -LiteralPath $model.FullName).Hash) {
        throw 'PlacementBuilding.glb already contains another model; it will not be overwritten.'
    }
} else {
    [IO.Directory]::CreateDirectory((Split-Path $destination -Parent)) | Out-Null
    Copy-Item -LiteralPath $model.FullName -Destination $destination
}
if (-not $OutputPath) { $OutputPath = Join-Path $sourceRoot 'bin\x64\Debug\PlacementModel.t8scene' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if ((Test-Path -LiteralPath $OutputPath) -and -not $Force) { throw 'Example exists. Choose another OutputPath or use Force.' }
$scene = Get-Content (Join-Path $sourceRoot 'Assets\Scenes\RtsBlockout.t8scene') -Raw | ConvertFrom-Json
$scene.objects[0].heightmap.placements = @(
    foreach ($index in 0, 1) {
        [pscustomobject]@{
            id = "model-$index"
            name = $(if ($index -eq 0) { 'Square Building' } else { 'Rectangular Building' })
            kind = 'building'
            cell_x = $(if ($index -eq 0) { 4 } else { 7 })
            cell_z = 4
            width = 2
            depth = $(if ($index -eq 0) { 2 } else { 1 })
            height = 3
            color = @{ x = 0.2; y = 0.6; z = 0.9 }
            visual = @{
                mesh = 'Models/PlacementBuilding.glb'
                hidden_geometry = @($HiddenGeometry)
                yaw_degrees = 0
                animation = $Animation
                animate = $true
                loop = $true
                animation_speed = 1
            }
        }
    }
)
$scene.cameras[0].position = [pscustomobject]@{ x = -7; y = 17; z = -38 }
$scene.cameras[0].target = [pscustomobject]@{ x = -19; y = 1; z = -22 }
$scene.lights[0].intensity = 12
$scene.lights[0] | Add-Member -NotePropertyName visible -NotePropertyValue $false -Force
$scene | Add-Member -NotePropertyName editor -NotePropertyValue @{
    camera_target = @{ x = -23; y = 1; z = -22 }
    camera_yaw = 0.5; camera_pitch = 0.7; camera_distance = 20
    show_wireframe = $false
} -Force
[IO.Directory]::CreateDirectory((Split-Path $OutputPath -Parent)) | Out-Null
[IO.File]::WriteAllText($OutputPath, ($scene | ConvertTo-Json -Depth 64), [Text.UTF8Encoding]::new($false))
Write-Host "Local model: $destination"
Write-Host "Local scene: $OutputPath"
