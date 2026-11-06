param(
  [Parameter(Mandatory)][string]$Commit,
  [string[]]$Apis = @('vulkan'),
  [switch]$Restore,
  [string]$MSBuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
)
# Moved out of the untracked backyard-diag\ scratch folder (plan item F). The repository root and the
# perf script both come from $PSScriptRoot, so this works from any checkout.
#
# Builds the tree with every file that differs between $Commit and HEAD taken from $Commit
# (T850/config.json is never touched), measures fps, then optionally restores HEAD.
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$repo = Split-Path -Parent $sourceRoot
$perf = Join-Path $PSScriptRoot 'perf.ps1'
Push-Location $repo
try {
  $changed = @(git diff --name-only --diff-filter=M $Commit HEAD -- T850/ | Where-Object { $_ -ne 'T850/config.json' })
  $added = @(git diff --name-only --diff-filter=A $Commit HEAD -- T850/)
  $deleted = @(git diff --name-only --diff-filter=D $Commit HEAD -- T850/)
  "commit $Commit : modified $($changed.Count), added-since $($added.Count), deleted-since $($deleted.Count)"
  if ($changed.Count) { git checkout $Commit -- $changed }
  if ($deleted.Count) { git checkout $Commit -- $deleted }
  Remove-Item "$sourceRoot\Lib\VStuff\Framework\Release\x64\*.pch" -ErrorAction SilentlyContinue
  Push-Location $sourceRoot
  $build = & $MSBuild T850.sln '/t:Framework;DayScene' /p:Configuration=Release /p:Platform=x64 /m:2 /nologo /v:m 2>&1
  Pop-Location
  $errors = @($build | Select-String -Pattern ' error ')
  if ($errors.Count) { $errors | Select-Object -First 5 | ForEach-Object { $_.Line }; throw "build failed at $Commit" }
  foreach ($api in $Apis) { & $perf -Api $api -Tag "at-$Commit" }
} finally {
  if ($Restore) {
    if ($changed.Count) { git checkout HEAD -- $changed }
    foreach ($file in $deleted) { git rm -q --cached --ignore-unmatch -- $file; Remove-Item -LiteralPath $file -ErrorAction SilentlyContinue }
    "restored HEAD; status:"; git status --short
  }
  Pop-Location
}
