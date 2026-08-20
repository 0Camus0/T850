[CmdletBinding()]
param(
    [ValidateSet("Win32", "x64", "ARM64", IgnoreCase = $true)]
    [string[]]$Platforms = @("Win32", "x64", "ARM64"),

    [ValidateSet("Debug", "Release", IgnoreCase = $true)]
    [string[]]$Configurations = @("Debug", "Release"),

    [ValidateSet("Build", "Rebuild", IgnoreCase = $true)]
    [string]$Action = "Build"
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $PSScriptRoot "build.ps1"
$registrationScript = Join-Path $PSScriptRoot "ValidateBuildRegistration.ps1"
$powerShellExe = (Get-Process -Id $PID).Path

& $registrationScript -SourceRoot $sourceRoot

foreach ($platform in $Platforms) {
    foreach ($configuration in $Configurations) {
        Write-Host ""
        Write-Host "=== CI build cell: $configuration|$platform ($Action) ===" -ForegroundColor Cyan
        & $powerShellExe -NoProfile -ExecutionPolicy Bypass -File $buildScript `
            -Config $configuration -Platform $platform -Action $Action
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        $output = Join-Path $sourceRoot "bin\$platform\$configuration"
        foreach ($executable in @("DayScene.exe", "T8ditor.exe")) {
            $path = Join-Path $output $executable
            if (-not (Test-Path $path)) {
                Write-Error "Expected build output was not found: $path"
                exit 1
            }
        }

        if ($platform -ne "ARM64") {
            & (Join-Path $output "DayScene.exe") --game-selftest
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
    }
}

Write-Host ""
Write-Host "Windows CI build matrix PASS ($($Platforms.Count * $Configurations.Count) cells)" -ForegroundColor Green