param(
  [int[]]$Scenes = @(0, 1, 2, 3, 4, 5, 6, 7),
  [string[]]$Apis = @('d3d11', 'd3d12', 'vulkan', 'gl', 'webgpu'),
  [int]$Frames = 240,
  [string]$ExeDir,
  [string]$OutRoot
)
# Scene x API smoke matrix (plan item G.4): every scene on every backend, headless, unattended, and
# the log must contain zero [ERROR] lines. Logs land under build\diag\scene-matrix.
#
#   pwsh scripts\validation\run_scene_matrix.ps1
#   pwsh scripts\validation\run_scene_matrix.ps1 -Scenes 7 -Apis gl,webgpu -Frames 600
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ExeDir) { $ExeDir = Join-Path $sourceRoot 'bin\x64\Release' }
if (-not $OutRoot) { $OutRoot = Join-Path $sourceRoot 'build\diag' }
$out = Join-Path $OutRoot 'scene-matrix'
New-Item -ItemType Directory -Force $out | Out-Null

# Scene 4 (SceneTemplate) refuses to start without a document by design, so the cell gives it one:
# without this the matrix reports a "failure" that is really a usage error.
$sceneArguments = @{ 4 = @('--sceneFile', 'Scenes/Nexus.t8scene') }

$failures = 0
Push-Location $ExeDir
try {
  foreach ($scene in $Scenes) {
    foreach ($api in $Apis) {
      $name = "scene$scene-$api"
      $log = Join-Path $out "$name.log"
      Remove-Item $log -ErrorAction SilentlyContinue
      $arguments = @('--scene', "$scene", '--api', $api, '--width', '1280', '--height', '720',
                     '--dump-frame', "$Frames", '--unattended', '--logLevel', 'info', '--logFile', $log)
      if ($sceneArguments.ContainsKey($scene)) { $arguments += $sceneArguments[$scene] }
      $p = Start-Process -FilePath (Join-Path $ExeDir 'DayScene.exe') -ArgumentList $arguments -PassThru -WindowStyle Hidden
      $null = $p.Handle
      $timedOut = -not $p.WaitForExit(300000)
      if ($timedOut) { Stop-Process -Id $p.Id -Force }
      $errors = @()
      if (Test-Path $log) { $errors = @(Select-String -LiteralPath $log -Pattern '\[ERROR') }
      $ok = (-not $timedOut) -and ($errors.Count -eq 0) -and (Test-Path $log)
      if (-not $ok) { $failures++ }
      $line = '{0,-16} exit={1,-3} timeout={2,-5} errors={3,-3} {4}' -f $name, $p.ExitCode, $timedOut, $errors.Count, $(if ($ok) { 'PASS' } else { 'FAIL' })
      Write-Output $line
      foreach ($e in ($errors | Select-Object -First 4)) {
        Write-Output ('    ' + $e.Line.Substring(0, [Math]::Min(240, $e.Line.Length)))
      }
    }
  }
} finally { Pop-Location }

Write-Output ("{0} cell(s) failed of {1}" -f $failures, ($Scenes.Count * $Apis.Count))
exit $(if ($failures) { 1 } else { 0 })
