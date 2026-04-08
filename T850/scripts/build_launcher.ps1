# build_launcher.ps1
# Compiles scripts/Launcher.ps1 into T850Launcher.exe in the repo root.
# Requires: Install-Module ps2exe -Scope CurrentUser

param(
    [switch]$NoConsole = $true
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir   = Split-Path -Parent $scriptDir
$source    = Join-Path $scriptDir "Launcher.ps1"
$output    = Join-Path $rootDir   "T850Launcher.exe"

if (-not (Get-Module ps2exe -ListAvailable)) {
    Write-Host "Installing ps2exe module..." -ForegroundColor Yellow
    Install-Module ps2exe -Scope CurrentUser -Force
}

Write-Host "Compiling $source -> $output" -ForegroundColor Cyan

$params = @{
    inputFile  = $source
    outputFile = $output
    noConsole  = $NoConsole
    iconFile   = (Join-Path $rootDir "Resources\T850.ico")
    title      = "T850 Engine Launcher"
    company    = "T850"
    product    = "T850 Engine"
    version    = "1.0.0.0"
}

Invoke-ps2exe @params

if (Test-Path $output) {
    Write-Host "Success: $output" -ForegroundColor Green
} else {
    Write-Host "Build failed." -ForegroundColor Red
    exit 1
}
