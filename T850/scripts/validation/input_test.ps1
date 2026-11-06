param(
  [string[]]$Apis = @('d3d12'),
  [ValidateSet('pick', 'pad', 'both')][string]$Mode = 'both',
  [int]$Balls = 6,
  [string]$ExeDir,
  [string]$OutRoot,
  # SKIP is legitimate only on an API without 3D compute storage; on the five solver APIs the
  # scene logs FAIL instead, so a SKIP there means the run did not test anything.
  [switch]$AllowSkip
)
# Moved out of the untracked backyard-diag\ scratch folder (plan item F). Every path comes from
# $PSScriptRoot, so this works from any checkout: the source root is two levels up (T850), the
# default executable is bin\x64\Release, and artefacts land under build\diag\.
#
# In-process input tests: the scene feeds a synthetic InputManager to its own input path, so this
# script only starts the app and reads its log; it never touches the real mouse, keyboard, gamepad
# or window focus (--unattended).
#   pick : Ctrl + left click grabs the ball under the cursor, drag, push, release.
#   pad  : RT grabs along the crosshair, left stick pans, left trigger lifts, and the pad quick
#          actions (X +10, Y reset, shoulders speed, d-pad heading/arrow) all fire.
# The two run as separate processes because each drives the same scene input path.
$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ExeDir) { $ExeDir = Join-Path $sourceRoot 'bin\x64\Release' }
if (-not $OutRoot) { $OutRoot = Join-Path $sourceRoot 'build\diag' }
$logDir = Join-Path $OutRoot 'input'
New-Item -ItemType Directory -Force $logDir | Out-Null

$modes = if ($Mode -eq 'both') { @('pick', 'pad') } else { @($Mode) }
$failureCount = 0

foreach ($testMode in $modes) {
  $flag = if ($testMode -eq 'pick') { '--backyard-pick-test' } else { '--backyard-pad-test' }
  $tag = if ($testMode -eq 'pick') { '[BackyardPickTest]' } else { '[BackyardPadTest]' }
  foreach ($api in $Apis) {
    $log = Join-Path $logDir "${testMode}_$api.log"
    Remove-Item -ErrorAction SilentlyContinue $log
    $p = Start-Process (Join-Path $ExeDir 'DayScene.exe') -WorkingDirectory $ExeDir -PassThru -ArgumentList @(
      '--scene', '7', '--api', $api, '--unattended', '--regressionFixedDt', '0.0033',
      '--backyard-spawn', "$Balls", $flag, '--logLevel', 'info', '--logFile', $log)
    $null = $p.Handle
    $timedOut = -not $p.WaitForExit(240000)
    if ($timedOut) { Stop-Process -Id $p.Id -Force }
    $errors = @(Select-String -LiteralPath $log -Pattern '\[ERROR')
    $testErrors = @($errors | Where-Object { $_.Line -notmatch 'BackyardPickTest|BackyardPadTest' })
    $result = Select-String -LiteralPath $log -Pattern "$([regex]::Escape($tag)) (PASS|FAIL|SKIP)" |
      Select-Object -Last 1
    $verdict = if (-not $result) { 'NORESULT' } else { $result.Matches[0].Groups[1].Value }
    $passed = ($verdict -eq 'PASS' -or ($verdict -eq 'SKIP' -and $AllowSkip)) -and
              -not $timedOut -and $p.ExitCode -eq 0 -and $testErrors.Count -eq 0
    if (-not $passed) { $failureCount++ }
    '{0,-5} {1,-8} exit={2} timeout={3} errors={4} verdict={5}' -f $testMode, $api, $p.ExitCode, $timedOut, $testErrors.Count, $verdict
    Select-String -LiteralPath $log -Pattern ([regex]::Escape($tag)) | ForEach-Object {
      '    ' + $_.Line.Substring([Math]::Max(0, $_.Line.IndexOf($tag)))
    }
    $testErrors | Select-Object -First 3 | ForEach-Object { '    ' + $_.Line }
    $dump = (Select-String -LiteralPath $log -Pattern 'Saved (dumps_[^/]+)/RT_Dump_BackBuffer').Matches
    if ($dump) { "    dump $($dump[0].Groups[1].Value)" }
  }
}

if ($failureCount -gt 0) {
  Write-Error "$failureCount input test run(s) did not pass."
  exit 1
}
