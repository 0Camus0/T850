# Moved out of the untracked backyard-diag\ scratch folder (plan item F). Every path comes from
# $PSScriptRoot, so this works from any checkout: the source root is two levels up (T850), the
# default executable is bin\x64\Release, and artefacts land under build\diag\.
param(
  [string]$ExeDir,
  [string]$OutRoot,

  [string[]]$Apis = @('d3d12', 'vulkan'),
  [int]$Balls = 0,
  [string]$Tag = 'base',
  [int]$Frames = 2400,
  [string[]]$Extra = @(),
  [string[]]$Summary = @()
)
$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ExeDir) { $ExeDir = Join-Path $sourceRoot 'bin\x64\Release' }
if (-not $OutRoot) { $OutRoot = Join-Path $sourceRoot 'build\diag' }
# CPU-phase profile from the engine's runtime telemetry (per-frame inclusive CPU wall time
# per scope). gpu.gpu_wait is the CPU blocked on the GPU, so a large wait means GPU-bound
# and a large scene.update with a small wait means CPU-bound in the solver's recording.
# The app runs --unattended at log level error and exits by itself after --dump-frame.
$ErrorActionPreference = 'Stop'
$out = Join-Path $OutRoot 'telemetry'
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($api in $Apis) {
  $json = Join-Path $out "$Tag-$api-$Balls.json"
  $log = Join-Path $out "$Tag-$api-$Balls.log"
  Remove-Item $json, $log -ErrorAction SilentlyContinue
  $appArgs = @('--scene', '7', '--api', $api, '--width', '1280', '--height', '720', '--unattended',
               '--logLevel', 'error', '--logFile', $log, '--backyard-spawn', "$Balls",
               '--runtimeTelemetry', '--telemetryFrequencyFrames', '1', '--telemetryOutput', $json,
               '--profile', '--profileCpuOnly', '--profileFrames', "$Frames") + $Extra
  $app = Start-Process (Join-Path $ExeDir 'DayScene.exe') -WorkingDirectory $ExeDir -PassThru -ArgumentList $appArgs
  $null = $app.Handle
  if (-not $app.WaitForExit(600000)) { Stop-Process -Id $app.Id -Force; "$api timeout"; continue }
  $errors = @(Select-String -LiteralPath $log -Pattern '\[ERROR' -ErrorAction SilentlyContinue).Count
  # The engine appends a timestamp to the requested output name.
  $written = Get-ChildItem $out -Filter "$Tag-$api-${Balls}_*.json" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $written) { "$api no telemetry output (errors=$errors)"; continue }
  python (Join-Path $PSScriptRoot 'telemetry_summary.py') $written.FullName $api $Tag $Balls $errors @Summary
}
