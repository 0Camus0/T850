[CmdletBinding()]
param(
    [string]$EmSdkRoot = (Join-Path $env:LOCALAPPDATA 'T850\emsdk'),
    [string]$CMake = 'cmake',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [int[]]$ExportScenes = @(6, 0, 1, 2, 3, 4, 5),
    [switch]$Clean,
    [switch]$SkipShaderExport
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $sourceRoot 'build\web'
$sdkVersion = '6.0.9'
$portVersion = 'v20260219.200501'
$portHash = 'C355E19619AA23E8630A526793353230918635B79CC8B0B0149FBC48747405AB'
if ($Clean -and $SkipShaderExport) { throw '-Clean requires shader export; omit -SkipShaderExport.' }
if ($Clean -and (Test-Path -LiteralPath $buildRoot)) {
    if ((Get-Item -LiteralPath $buildRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing to clean a linked browser build directory.' }
    Write-Host "Removing generated browser build: $buildRoot"
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

function Assert-CommandSucceeded([string]$Operation) {
    if ($LASTEXITCODE -ne 0) { throw "$Operation failed with exit code $LASTEXITCODE" }
}

function Invoke-LoggedNativeCommand {
    param([string]$FilePath, [string[]]$Arguments, [string]$LogPath, [string]$Operation)
    Write-Host "$Operation..."
    $quoted = @(@($FilePath) + $Arguments | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' })
    $command = ($quoted -join ' ') + ' > "' + $LogPath + '" 2>&1'
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $env:ComSpec
    $startInfo.Arguments = '/d /s /c "' + $command + '"'
    $startInfo.WorkingDirectory = $PWD.Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    try {
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            if (Test-Path -LiteralPath $LogPath) { Get-Content -LiteralPath $LogPath -Tail 30 }
            throw "$Operation failed with exit code $($process.ExitCode)"
        }
    } finally { $process.Dispose() }
}

if (-not (Test-Path (Join-Path $EmSdkRoot 'emsdk.bat'))) {
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git $EmSdkRoot
    Assert-CommandSucceeded 'SDK manager clone'
}
Invoke-LoggedNativeCommand -FilePath (Join-Path $EmSdkRoot 'emsdk.bat') -Arguments @('install', $sdkVersion) -LogPath (Join-Path $buildRoot 'sdk-install.log') -Operation 'SDK installation'
Invoke-LoggedNativeCommand -FilePath (Join-Path $EmSdkRoot 'emsdk.bat') -Arguments @('activate', $sdkVersion) -LogPath (Join-Path $buildRoot 'sdk-activate.log') -Operation 'SDK activation'
$python = Get-ChildItem (Join-Path $EmSdkRoot 'python\*\python.exe') | Select-Object -Last 1
if (-not $python) { throw 'Emscripten Python was not installed' }
$env:EMSCRIPTEN = Join-Path $EmSdkRoot 'upstream\emscripten'
$port = Join-Path $buildRoot "emdawnwebgpu-$portVersion.remoteport.py"
if (-not (Test-Path $port)) {
    Invoke-WebRequest "https://github.com/google/dawn/releases/download/$portVersion/emdawnwebgpu-$portVersion.remoteport.py" -OutFile $port
}
if ((Get-FileHash $port -Algorithm SHA256).Hash -ne $portHash) {
    throw 'Pinned Emdawnwebgpu port checksum mismatch'
}

if (-not $SkipShaderExport) {
    & (Join-Path $PSScriptRoot 'SetupDawn.ps1')
    Assert-CommandSucceeded 'Native Dawn audit setup'
    & (Join-Path $PSScriptRoot 'build.ps1') -Config Release -Platform x64 -Action Build
    Assert-CommandSucceeded 'Native shader exporter build'
    Push-Location (Join-Path $sourceRoot 'bin\x64\Release')
    try {
        $shaderOutput = Join-Path $buildRoot 'WebShaders'
        Invoke-LoggedNativeCommand -FilePath '.\DayScene.exe' -Arguments @('--compileShaders', '--api', 'webgpu', '--webShaderOutput', $shaderOutput) -LogPath (Join-Path $buildRoot 'shader-export.log') -Operation 'Recorded shader export'
        foreach ($scene in $ExportScenes) {
            $catalog = Get-Content (Join-Path $sourceRoot 'web\scenes.json') -Raw | ConvertFrom-Json
            $launch = $catalog.scenes | Where-Object id -EQ $scene
            if (-not $launch) { throw "Unknown runtime scene: $scene" }
            $sceneArguments = @($launch.arguments | Where-Object { $_ })
            if ($launch.sceneFile) { $sceneArguments += @('--sceneFile', $launch.sceneFile) }
            $arguments = @('--api', 'webgpu', '--scene', [string]$scene, '--width', '640', '--height', '360',
                '--regressionFixedDt', '0.0166666667', '--dumpSnapshot-seconds', '1', '--webShaderOutput', $shaderOutput) + $sceneArguments
            Invoke-LoggedNativeCommand -FilePath '.\DayScene.exe' -Arguments $arguments -LogPath (Join-Path $buildRoot "scene-$scene-export.log") -Operation "Scene $scene shader export"
            if (Select-String -Path (Join-Path $buildRoot "scene-$scene-export.log") -Pattern '\[ERROR\s*\]' -Quiet) {
                throw "Scene $scene reported an engine error during shader export"
            }
        }
    } finally {
        Pop-Location
    }
}
if (-not (Test-Path (Join-Path $buildRoot 'WebShaders\*.json'))) {
    throw 'Prepared shaders are missing. Run without -SkipShaderExport first.'
}

& $python.FullName (Join-Path $env:EMSCRIPTEN 'emcmake.py') $CMake `
    -S $sourceRoot -B $buildRoot -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" `
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
Assert-CommandSucceeded 'Browser configure'
& $CMake --build $buildRoot --target DayScene T850WebSelfTests --parallel 4
Assert-CommandSucceeded 'Browser build'
$cmakePath = (Get-Command $CMake).Source
$ctest = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
& $ctest --test-dir $buildRoot -R '^T850WebSelfTests$' --output-on-failure
Assert-CommandSucceeded 'Wasm gameplay and terrain tests'
Write-Host "Browser build ready: $buildRoot\site"
Write-Host "Serve: node `"$sourceRoot\web\server.mjs`""