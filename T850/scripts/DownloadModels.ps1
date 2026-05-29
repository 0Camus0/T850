param(
    [string]$RootDir,
    [string]$ManifestUrl = "https://pub-2fa5c50bbfbc4b829da0d6c6300815b0.r2.dev/runtime_assets.json",
    [string]$ManifestPath,
    [string]$AssetRoot,
    [int]$MaxThreads = 7
)

$ErrorActionPreference = "Stop"

if (-not $RootDir) {
    $RootDir = Split-Path -Parent $PSScriptRoot
}
if (-not $AssetRoot) {
    $AssetRoot = Join-Path $RootDir "Assets"
}

. (Join-Path $PSScriptRoot "ModelCloud.ps1")

$result = Ensure-T850CloudModels `
    -RootDir $RootDir `
    -AssetRoot $AssetRoot `
    -ManifestPath $ManifestPath `
    -ManifestUrl $ManifestUrl `
    -MaxThreads $MaxThreads `
    -StatusCallback { param([string]$Message) Write-Host ("[T850] " + $Message) }

if (-not $result.Ok) {
    Write-Error $result.Message
    exit 1
}

Write-Host ("[T850] " + $result.Message)
exit 0