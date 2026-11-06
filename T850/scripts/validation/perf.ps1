param(
  [string]$Api = 'd3d12',
  [int]$Width = 1280,
  [int]$Height = 720,
  [int]$Frames = 3600,
  [string]$Tag = 'base',
  [string[]]$Extra = @(),
  [long]$Affinity = 0,
  [string]$ExeDir,
  [string]$OutRoot
)
# Moved out of the untracked backyard-diag\ scratch folder (plan item F). Every path comes from
# $PSScriptRoot, so this works from any checkout: the source root is two levels up (T850), the
# default executable is bin\x64\Release, and artefacts land under build\diag\.
#
# Runs scene 7 and reports frames/s between consecutive [Backyard][timing] lines (each covers 600
# frames), skipping the first window (startup, drop, splash).
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ExeDir) { $ExeDir = Join-Path $sourceRoot 'bin\x64\Release' }
if (-not $OutRoot) { $OutRoot = Join-Path $sourceRoot 'build\diag' }
$out = Join-Path $OutRoot 'perf'
New-Item -ItemType Directory -Force $out | Out-Null
$log = Join-Path $out "$Tag-$Api-$Width.log"
Remove-Item $log -ErrorAction SilentlyContinue
$args2 = @('--api', $Api, '--scene', '7', '--width', "$Width", '--height', "$Height",
           '--dump-frame', "$Frames", '--unattended', '--logLevel', 'info', '--logFile', $log) + $Extra
$p = Start-Process (Join-Path $ExeDir 'DayScene.exe') -ArgumentList $args2 -WorkingDirectory $ExeDir -PassThru -WindowStyle Normal
$null = $p.Handle
if ($Affinity -ne 0) { $p.ProcessorAffinity = [IntPtr]$Affinity }
if (-not $p.WaitForExit(600000)) { $p.Kill(); throw 'timeout' }
$stamps = @(Select-String -LiteralPath $log -Pattern '^\[(\d+):(\d+):(\d+)\.(\d+)\].*\[Backyard\]\[timing\]' | ForEach-Object {
  $m = $_.Matches[0].Groups
  [int]$m[1].Value * 3600 + [int]$m[2].Value * 60 + [int]$m[3].Value + [double]("0." + $m[4].Value)
})
$fps = @()
for ($i = 2; $i -lt $stamps.Count; $i++) { $fps += 600.0 / ($stamps[$i] - $stamps[$i - 1]) }
if ($fps.Count -eq 0) {
  # No solver step means no [timing] lines; the 60-frame probe still logs [sphere].
  $probe = @(Select-String -LiteralPath $log -Pattern '^\[(\d+):(\d+):(\d+)\.(\d+)\].*\[Backyard\]\[sphere\]' | ForEach-Object {
    $m = $_.Matches[0].Groups
    [int]$m[1].Value * 3600 + [int]$m[2].Value * 60 + [int]$m[3].Value + [double]("0." + $m[4].Value)
  })
  for ($i = 10; $i + 10 -lt $probe.Count; $i += 10) { $fps += 600.0 / ($probe[$i + 10] - $probe[$i]) }
}
$errors = @(Select-String -LiteralPath $log -Pattern '\[ERROR').Count
$sorted = $fps | Sort-Object
$median = if ($sorted.Count) { $sorted[[int][math]::Floor($sorted.Count / 2)] } else { 0 }
"{0,-28} api={1,-6} {2}x{3} exit={4} errors={5} windows={6} fps median={7:N1} all=[{8}]" -f $Tag, $Api, $Width, $Height, $p.ExitCode, $errors, $fps.Count, $median, (($fps | ForEach-Object { '{0:N0}' -f $_ }) -join ' ')
