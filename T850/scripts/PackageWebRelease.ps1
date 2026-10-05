[CmdletBinding()]
param(
    [string]$SourceRoot,
    [string]$BundleDirectory,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $SourceRoot) { $SourceRoot = Split-Path -Parent $PSScriptRoot }
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
if (-not $BundleDirectory) { $BundleDirectory = Join-Path $SourceRoot 'build\web' }
if (-not $OutputPath) { $OutputPath = Join-Path $SourceRoot 'artifacts\web\T850-Web-Release.zip' }
$BundleDirectory = [IO.Path]::GetFullPath($BundleDirectory)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required browser release input is missing: $Path" }
    if ((Get-Item -LiteralPath $Path).Length -le 0) { throw "Required browser release input is empty: $Path" }
}

$siteRoot = Join-Path $BundleDirectory 'site'
$shaderRoot = Join-Path $BundleDirectory 'WebShaders'
$cloudCatalogPath = Join-Path $BundleDirectory 'CloudAssets\routes.json'
foreach ($name in @('DayScene.html', 'DayScene.js', 'DayScene.wasm', 'scenes.json', 'touch-controls.js')) {
    Require-File (Join-Path $siteRoot $name)
}
Require-File $cloudCatalogPath
if (-not (Get-ChildItem -LiteralPath $shaderRoot -File -Filter '*.json' | Select-Object -First 1)) {
    throw "Prepared browser shaders are missing: $shaderRoot"
}

$cloudCatalog = Get-Content -LiteralPath $cloudCatalogPath -Raw | ConvertFrom-Json
$cloudRoutes = @($cloudCatalog.routes.PSObject.Properties)
if ($cloudCatalog.version -ne 1 -or $cloudRoutes.Count -eq 0) { throw 'Cloud asset catalog is invalid or empty.' }
if (-not $cloudCatalog.routes.PSObject.Properties['Textures/sky/CubeMap_SkyWater.dds']) {
    throw 'Cloud asset catalog is missing the required browser sky resource.'
}

$webRoot = Join-Path $SourceRoot 'web'
foreach ($name in @('server.mjs', 'cloud-assets.mjs', 'cloudflare-config.mjs', 'launcher.html', 'launcher.mjs')) {
    Require-File (Join-Path $webRoot $name)
}
$previewRoot = Join-Path $webRoot 'previews'
if (-not (Test-Path -LiteralPath $previewRoot -PathType Container) -or
    -not (Get-ChildItem -LiteralPath $previewRoot -File -Filter '*.png' | Select-Object -First 1)) {
    throw "Browser launcher previews are missing: $previewRoot"
}

$stageRoot = Join-Path ([IO.Path]::GetTempPath()) ("t850-web-release-{0}" -f [Guid]::NewGuid().ToString('N'))
$packageRoot = Join-Path $stageRoot 'T850-Web-Release'
try {
    $packageWebRoot = Join-Path $packageRoot 'web'
    $packageSiteRoot = Join-Path $packageWebRoot 'site'
    New-Item -ItemType Directory -Path $packageSiteRoot -Force | Out-Null
    Copy-Item (Join-Path $siteRoot '*') -Destination $packageSiteRoot -Recurse -Force
    Copy-Item (Join-Path $webRoot 'launcher.html') -Destination $packageSiteRoot -Force
    Copy-Item (Join-Path $webRoot 'launcher.mjs') -Destination $packageSiteRoot -Force
    Copy-Item $previewRoot -Destination (Join-Path $packageSiteRoot 'previews') -Recurse -Force

    Copy-Item $shaderRoot -Destination (Join-Path $packageWebRoot 'WebShaders') -Recurse -Force
    Copy-Item (Split-Path -Parent $cloudCatalogPath) -Destination (Join-Path $packageWebRoot 'CloudAssets') -Recurse -Force
    foreach ($name in @('server.mjs', 'cloud-assets.mjs', 'cloudflare-config.mjs')) {
        Copy-Item (Join-Path $webRoot $name) -Destination $packageWebRoot -Force
    }

    $assetOutput = Join-Path $packageWebRoot 'assets'
    New-Item -ItemType Directory -Path $assetOutput -Force | Out-Null
    $gitRoot = (& git -C $SourceRoot rev-parse --show-toplevel).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $gitRoot) { throw 'Cannot locate the Git root for deterministic browser asset packaging.' }
    $sourcePrefix = [IO.Path]::GetRelativePath($gitRoot, $SourceRoot).Replace('\', '/')
    $assetPrefix = "$sourcePrefix/Assets/"
    $assetSpecs = @('Shaders', 'Models', 'Fonts', 'Textures', 'Scenes', 'Layouts') |
        ForEach-Object { "$sourcePrefix/Assets/$_" }
    $trackedAssets = @(& git -C $gitRoot -c core.quotePath=false ls-files -- @assetSpecs)
    if ($LASTEXITCODE -ne 0 -or $trackedAssets.Count -eq 0) { throw 'No tracked browser runtime metadata was found.' }
    $cloudOnlyExtensions = @('.bin', '.dds', '.glb', '.gltf', '.t8ibl', '.zip')
    foreach ($trackedPath in $trackedAssets) {
        if (-not $trackedPath.StartsWith($assetPrefix, [StringComparison]::Ordinal)) { throw "Unexpected tracked asset path: $trackedPath" }
        $resource = $trackedPath.Substring($assetPrefix.Length)
        $source = Join-Path $gitRoot $trackedPath
        $extension = [IO.Path]::GetExtension($resource).ToLowerInvariant()
        if ($extension -in $cloudOnlyExtensions) {
            if (-not $cloudCatalog.routes.PSObject.Properties[$resource]) {
                throw "Tracked heavyweight browser asset has no cloud route: $resource"
            }
            continue
        }
        $destination = Join-Path $assetOutput $resource
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }

    $readme = @'
T850 Browser WebGPU Release

Requirements:
- A WebGPU-capable browser.
- Node.js 20 or newer on PATH.
- Internet access for cloud-hosted models, textures, and IBL data.

Run on Windows:
  Start-T850-Web.cmd

Run on macOS or Linux:
    sh Start-T850-Web.sh

Manual command:
  node web/server.mjs --open --launcher

The local server binds only to 127.0.0.1 and supplies the cross-origin isolation
headers required by the threaded WebAssembly runtime. Opening launcher.html with
file:// is not supported. Heavy runtime assets are downloaded through the
allowlisted public cloud routes in web/CloudAssets/routes.json.
'@
    [IO.File]::WriteAllText((Join-Path $packageRoot 'README.txt'), $readme)
    [IO.File]::WriteAllText((Join-Path $packageRoot 'Start-T850-Web.cmd'), @'
@echo off
where node >nul 2>&1 || (echo Node.js 20 or newer is required.& exit /b 1)
node "%~dp0web\server.mjs" --open --launcher %*
'@)
    [IO.File]::WriteAllText((Join-Path $packageRoot 'Start-T850-Web.sh'), @'
#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec node "$ROOT/web/server.mjs" --open --launcher "$@"
'@)

    New-Item -ItemType Directory -Path (Split-Path -Parent $OutputPath) -Force | Out-Null
    Remove-Item -LiteralPath $OutputPath -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $OutputPath -CompressionLevel Optimal
    Require-File $OutputPath
    Write-Host "Browser release package: $OutputPath ($((Get-Item $OutputPath).Length) bytes)"
} finally {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
}