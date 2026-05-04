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

# Normalize config casing
$Config = $Config.Substring(0,1).ToUpper() + $Config.Substring(1).ToLower()

# Map platform to MSBuild platform name (x86 -> Win32 in .sln)
$msbuildPlatform = switch ($Platform.ToLower()) {
    "x86"   { "x86" }
    "x64"   { "x64" }
    "arm64" { "ARM64" }
}

function Test-MSBuildSupportsPlatform {
    param(
        [Parameter(Mandatory = $true)] [string]$MSBuildPath,
        [Parameter(Mandatory = $true)] [string]$TargetPlatform
    )

    if (-not (Test-Path $MSBuildPath)) { return $false }
    if ($TargetPlatform -ne "ARM64") { return $true }

    $installRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MSBuildPath)))
    $vcRoot = Join-Path $installRoot "VC\Tools\MSVC"
    if (-not (Test-Path $vcRoot)) { return $false }

    $arm64Compiler = Get-ChildItem -Path $vcRoot -Recurse -Filter cl.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\bin\\Hostx64\\arm64\\cl\.exe$' } |
        Select-Object -First 1
    return [bool]$arm64Compiler
}

function Find-MSBuildForPlatform {
    param([Parameter(Mandatory = $true)] [string]$TargetPlatform)

    $progX86 = [System.Environment]::GetFolderPath("ProgramFilesX86")
    $progFiles = [System.Environment]::GetFolderPath("ProgramFiles")

    $preferred = if ($TargetPlatform -eq "ARM64") {
        @(
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progX86  "Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe")
        )
    } else {
        @(
            (Join-Path $progX86  "Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $progFiles "Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe")
        )
    }

    $candidates = $preferred + @(
        (Join-Path $progX86 "Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"),
        (Join-Path $progX86 "Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-MSBuildSupportsPlatform -MSBuildPath $candidate -TargetPlatform $TargetPlatform) {
            return $candidate
        }
    }

    $vswhere = Join-Path $progX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPaths = & $vswhere -all -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        foreach ($vsPath in $vsPaths) {
            $candidate = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-MSBuildSupportsPlatform -MSBuildPath $candidate -TargetPlatform $TargetPlatform) {
                return $candidate
            }
        }
    }

    return $null
}

$msbuild = Find-MSBuildForPlatform -TargetPlatform $msbuildPlatform
if (-not $msbuild) {
    Write-Host "ERROR: MSBuild not found for platform $msbuildPlatform. Install the matching Visual Studio v143 build tools." -ForegroundColor Red
    exit 1
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
