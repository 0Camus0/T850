# T850 Build Script
# Usage: .\build.ps1 -Config <Release|Debug> -Platform <x64|x86|ARM64>
# Example: .\build.ps1 -Config Release -Platform x64

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Release", "Debug", IgnoreCase = $true)]
    [string]$Config,

    [Parameter(Mandatory = $true)]
    [ValidateSet("x64", "x86", "ARM64", IgnoreCase = $true)]
    [string]$Platform
)

$ErrorActionPreference = "Stop"

# Resolve paths
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir   = Split-Path -Parent $scriptDir
$slnPath   = Join-Path $rootDir "T850.sln"

if (-not (Test-Path $slnPath)) {
    Write-Host "ERROR: Solution not found at $slnPath" -ForegroundColor Red
    exit 1
}

# Locate MSBuild
$msbuildPaths = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
)

$msbuild = $null
foreach ($p in $msbuildPaths) {
    if (Test-Path $p) {
        $msbuild = $p
        break
    }
}

if (-not $msbuild) {
    # Try vswhere as fallback
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($vsPath) {
            $candidate = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $candidate) { $msbuild = $candidate }
        }
    }
}

if (-not $msbuild) {
    Write-Host "ERROR: MSBuild not found. Install Visual Studio Build Tools." -ForegroundColor Red
    exit 1
}

# Normalize config casing
$Config = $Config.Substring(0,1).ToUpper() + $Config.Substring(1).ToLower()

# Map platform to MSBuild platform name (x86 -> Win32 in .sln)
$msbuildPlatform = switch ($Platform.ToLower()) {
    "x86"   { "x86" }
    "x64"   { "x64" }
    "arm64" { "ARM64" }
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " T850 Rebuild" -ForegroundColor Cyan
Write-Host " Config:   $Config" -ForegroundColor Cyan
Write-Host " Platform: $Platform ($msbuildPlatform)" -ForegroundColor Cyan
Write-Host " Solution: $slnPath" -ForegroundColor Cyan
Write-Host " MSBuild:  $msbuild" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

# Run MSBuild
& $msbuild $slnPath /p:Configuration=$Config /p:Platform=$msbuildPlatform /t:Rebuild /v:minimal 2>&1 | ForEach-Object {
    $line = $_.ToString()
    # Color errors red, warnings yellow
    if ($line -match ": error ") {
        Write-Host $line -ForegroundColor Red
    } elseif ($line -match ": warning ") {
        Write-Host $line -ForegroundColor Yellow
    } else {
        Write-Host $line
    }
}

$exitCode = $LASTEXITCODE
$stopwatch.Stop()
$elapsed = $stopwatch.Elapsed.ToString("mm\:ss\.fff")

Write-Host ""
if ($exitCode -eq 0) {
    Write-Host "========================================" -ForegroundColor Green
    Write-Host " BUILD SUCCEEDED  ($elapsed)" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
} else {
    Write-Host "========================================" -ForegroundColor Red
    Write-Host " BUILD FAILED  (exit code $exitCode, $elapsed)" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
}

exit $exitCode
