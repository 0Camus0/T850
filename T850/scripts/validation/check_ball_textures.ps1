# Read-only check that the ball material palette is complete on disk and in the cloud manifest.
#
# W-25 (a different PBR material on every ball) needs 32 ambientCG sets, each with the three maps
# the renderer binds. The textures are NOT committed: they come from `DownloadTextures.ps1` or the
# launcher's Download Assets button, both through `scripts/ModelCloud.ps1` and the R2 texture
# manifest. Gate 10 fails at run time when a set is missing, which is too late for CI.
#
# This script never writes to R2 and never downloads: it reads the manifest (live by default, or a
# saved copy) plus the local asset root and reports what is absent.
#
#   pwsh scripts/validation/check_ball_textures.ps1
#   pwsh scripts/validation/check_ball_textures.ps1 -ManifestPath C:\tmp\manifest.json
#   pwsh scripts/validation/check_ball_textures.ps1 -SkipCloudManifest   # offline, disk only
#
# Layout (Assets/Scenes/BackyardBallMaterials.json):
#   materials[] = { id: "Metal009", label: "...", metal: 1.0 }
#   maps        = { color: "Color.jpg", normal: "Normal.png", orm: "ORM.png" }
# Files: Assets/Textures/Ball/<id>/<map file>, plus a `.complete` marker written by the downloader.
#
# Exit code 0 only when every palette set and every palette map is present.

[CmdletBinding()]
param(
    [string]$RootDir,
    [string]$PalettePath,
    [string]$ManifestPath = '',
    [switch]$SkipCloudManifest,
    [int]$MinimumSets = 32
)

$ErrorActionPreference = 'Stop'
if (-not $RootDir) { $RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }
if (-not (Test-Path (Join-Path $RootDir 'Assets'))) { throw "No Assets directory under $RootDir" }
if (-not $PalettePath) { $PalettePath = Join-Path $RootDir 'Assets\Scenes\BackyardBallMaterials.json' }

$problems = New-Object System.Collections.Generic.List[string]
$palette = Get-Content -LiteralPath $PalettePath -Raw | ConvertFrom-Json
$materials = @($palette.materials)
$mapFiles = @($palette.maps.PSObject.Properties | ForEach-Object { [string]$_.Value })
if ($materials.Count -lt $MinimumSets) {
    $problems.Add("palette has $($materials.Count) sets, gate 10 needs $MinimumSets")
}
if ($mapFiles.Count -eq 0) { $problems.Add('palette names no maps') }

# 1. Local asset root.
$missing = New-Object System.Collections.Generic.List[string]
foreach ($material in $materials) {
    $dir = Join-Path $RootDir ('Assets\Textures\Ball\' + $material.id)
    foreach ($map in $mapFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $dir $map))) {
            $missing.Add("$($material.id)/$map")
        }
    }
}
foreach ($file in ($missing | Select-Object -First 10)) { $problems.Add("missing $file") }
if ($missing.Count -gt 10) { $problems.Add("... and $($missing.Count - 10) more missing file(s)") }

# 2. The cloud manifest must carry the same sets, or a fresh checkout cannot download them.
$ballEntries = 0
$manifestSource = 'skipped'
if (-not $SkipCloudManifest) {
    if ($ManifestPath) {
        $json = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
        $manifestSource = $ManifestPath
    } else {
        $manifestUrl = if ($env:T850_TEXTURE_MANIFEST_URL) { $env:T850_TEXTURE_MANIFEST_URL }
                       else { 'https://pub-ef5de729f9044220aa32f0601d99faa8.r2.dev/manifest.json' }
        $json = (Invoke-WebRequest -Uri $manifestUrl -UseBasicParsing).Content | ConvertFrom-Json
        $manifestSource = $manifestUrl
    }
    $keys = @($json.assets | ForEach-Object {
        if ($_.PSObject.Properties['key'] -and $_.key) { ([string]$_.key).TrimStart('/') }
        elseif ($_.PSObject.Properties['localRelativePath']) { ([string]$_.localRelativePath).TrimStart('/') }
        else { '' }
    })
    $ballKeys = @($keys | Where-Object { $_ -like 'Textures/Ball/*' })
    $ballEntries = $ballKeys.Count
    $cloudIds = @($ballKeys | ForEach-Object {
        ($_ -split '/')[2]
    } | Sort-Object -Unique)
    foreach ($material in $materials) {
        if ($cloudIds -notcontains $material.id) {
            $problems.Add("cloud manifest has no Textures/Ball/$($material.id)/ entries")
        }
    }
    $expected = $materials.Count * $mapFiles.Count
    if ($ballEntries -lt $expected) {
        $problems.Add("cloud manifest lists $ballEntries Textures/Ball entries, the palette needs $expected")
    }
}

if ($problems.Count -gt 0) {
    Write-Output "[ball-textures] FAIL: $($problems.Count) problem(s)"
    $problems | ForEach-Object { Write-Output "  - $_" }
    exit 1
}
Write-Output ("[ball-textures] PASS: {0} sets x {1} maps on disk, {2} Textures/Ball entries in {3}" -f `
    $materials.Count, $mapFiles.Count, $ballEntries, $manifestSource)
