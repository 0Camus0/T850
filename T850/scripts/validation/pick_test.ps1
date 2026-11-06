param(
  [string[]]$Apis = @('d3d12'),
  [int]$Balls = 6,
  [string]$ExeDir,
  [string]$OutRoot
)
# Moved out of the untracked backyard-diag\ scratch folder (plan item F). Every path comes from
# $PSScriptRoot, so this works from any checkout: the source root is two levels up (T850), the
# default executable is bin\x64\Release, and artefacts land under build\diag\.
#
# In-process pick/drag test: the scene feeds a synthetic InputManager to its own input path
# (--backyard-pick-test). This script only starts the app and reads its log; it never touches the
# real mouse, keyboard or window focus (--unattended).
$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ExeDir) { $ExeDir = Join-Path $sourceRoot 'bin\x64\Release' }
if (-not $OutRoot) { $OutRoot = Join-Path $sourceRoot 'build\diag' }
$logDir = Join-Path $OutRoot 'pick'
New-Item -ItemType Directory -Force $logDir | Out-Null

foreach ($api in $Apis) {
  $log = Join-Path $logDir "pick_$api.log"
  Remove-Item -ErrorAction SilentlyContinue $log
  $p = Start-Process (Join-Path $ExeDir 'DayScene.exe') -WorkingDirectory $ExeDir -PassThru -ArgumentList @(
    '--scene', '7', '--api', $api, '--unattended', '--regressionFixedDt', '0.0033',
    '--backyard-spawn', "$Balls", '--backyard-pick-test', '--logLevel', 'info', '--logFile', $log)
  $null = $p.Handle
  $timedOut = -not $p.WaitForExit(240000)
  if ($timedOut) { Stop-Process -Id $p.Id -Force }
  $errors = @(Select-String -LiteralPath $log -Pattern '\[ERROR')
  '{0,-7} exit={1} timeout={2} errors={3}' -f $api, $p.ExitCode, $timedOut, $errors.Count
  Select-String -LiteralPath $log -Pattern '\[BackyardPickTest\]' | ForEach-Object {
    '    ' + $_.Line.Substring([Math]::Max(0, $_.Line.IndexOf('[BackyardPickTest]')))
  }
  $errors | Where-Object { $_.Line -notmatch 'BackyardPickTest' } | Select-Object -First 3 | ForEach-Object { '    ' + $_.Line }
  $dump = (Select-String -LiteralPath $log -Pattern 'Saved (dumps_[^/]+)/RT_Dump_BackBuffer').Matches
  if ($dump) { "    dump $($dump[0].Groups[1].Value)" }
}
