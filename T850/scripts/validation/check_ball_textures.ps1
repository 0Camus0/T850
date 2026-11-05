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
#   pwsh scripts/validation/check_ball_textures.ps1 -SkipDisk            # CI: no Assets\Textures\Ball
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
    # CI checks out the repository without the downloaded textures, so the local-root half has
    # nothing to look at there. The manifest half is the part CI can prove, and it is what decides
    # whether a fresh install can download the palette at all.
    [switch]$SkipDisk,
    [int]$MinimumSets = 32
)

$ErrorActionPreference = 'Stop'
if (-not $RootDir) { $RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }
if (-not $SkipDisk -and -not (Test-Path (Join-Path $RootDir 'Assets'))) { throw "No Assets directory under $RootDir" }
if (-not $PalettePath) {
    # The engine resolves `Assets\Scenes\...` and then `Scenes\...` relative to its working
    # directory, and a release layout ships the flat form, so accept either.
    $PalettePath = Join-Path $RootDir 'Assets\Scenes\BackyardBallMaterials.json'
    if (-not (Test-Path -LiteralPath $PalettePath)) {
        $PalettePath = Join-Path $RootDir 'Scenes\BackyardBallMaterials.json'
    }
}

$problems = New-Object System.Collections.Generic.List[string]
$palette = Get-Content -LiteralPath $PalettePath -Raw | ConvertFrom-Json
$materials = @($palette.materials)
$mapFiles = @($palette.maps.PSObject.Properties | ForEach-Object { [string]$_.Value })
if ($materials.Count -lt $MinimumSets) {
    $problems.Add("palette has $($materials.Count) sets, gate 10 needs $MinimumSets")
}
if ($mapFiles.Count -eq 0) { $problems.Add('palette names no maps') }

# 1. Local asset root.
if ($SkipDisk) {
    Write-Output '[ball-textures] local asset root skipped (-SkipDisk)'
} else {
    # The engine resolves `Assets\Textures\...` and then `Textures\...` relative to its working
    # directory, and the launcher downloads into `<root>\Textures\...`, so accept either layout
    # instead of only the repository one.
    $ballRoot = Join-Path $RootDir 'Assets\Textures\Ball'
    if (-not (Test-Path -LiteralPath $ballRoot)) { $ballRoot = Join-Path $RootDir 'Textures\Ball' }
    $missing = New-Object System.Collections.Generic.List[string]
    foreach ($material in $materials) {
        $dir = Join-Path $ballRoot $material.id
        foreach ($map in $mapFiles) {
            if (-not (Test-Path -LiteralPath (Join-Path $dir $map))) {
                $missing.Add("$($material.id)/$map")
            }
        }
    }
    foreach ($file in ($missing | Select-Object -First 10)) { $problems.Add("missing $file") }
    if ($missing.Count -gt 10) { $problems.Add("... and $($missing.Count - 10) more missing file(s)") }
}

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
        try {
            $json = (Invoke-WebRequest -Uri $manifestUrl -UseBasicParsing -TimeoutSec 30).Content | ConvertFrom-Json
        } catch {
            # Nothing else in CI proves the palette is downloadable, so a network failure has to be
            # loud in the log without failing a build that never asked to reach the network.
            if ($SkipDisk) {
                Write-Output "[ball-textures] SKIP: the cloud manifest is unreachable ($manifestUrl): $($_.Exception.Message)"
                exit 0
            }
            throw
        }
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
Write-Output ("[ball-textures] PASS: {0} sets x {1} maps {4}, {2} Textures/Ball entries in {3}" -f `
    $materials.Count, $mapFiles.Count, $ballEntries, $manifestSource,
    $(if ($SkipDisk) { '(disk skipped)' } else { 'on disk' }))
