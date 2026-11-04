# One-way sync of this repository to the Linux build/verify machine (10.0.0.80).
#
# The remote copy is a CONSUMER, never a second author: it is fetched from a git bundle and can
# never push back. That keeps "both codebases up to date" a single command per step instead of a
# merge problem. The same technique already ships the Steam Deck build.
#
#   pwsh scripts/SyncRemoteBuild.ps1                 # update the remote to the current HEAD
#   pwsh scripts/SyncRemoteBuild.ps1 -Build          # ...then configure + build DayScene there
#   pwsh scripts/SyncRemoteBuild.ps1 -Check          # report remote state without changing it
#   pwsh scripts/SyncRemoteBuild.ps1 -RemoteUser deck@10.0.0.225 -RemoteDir ~/t850 -Build
#
# What it does, in order:
#   1. bundles `origin/master..HEAD` plus the last synced ref, so a step ships only new commits
#   2. copies the bundle over scp and runs `git fetch <bundle> HEAD:refs/heads/remote-verify`
#      followed by `git reset --hard` in the remote worktree (no merge, no local edits survive)
#   3. optionally runs the CMake configure and build with Ninja
#
# Requirements on the remote: git, cmake >= 3.20, ninja, a C++23 compiler, and space for the vcpkg
# tree. The remote worktree is self-contained; vcpkg lives inside it (`Librerias/vcpkg`) and is
# bootstrapped by CMake on first configure, so the first build is the slow one.
[CmdletBinding()]
param(
    [string]$RemoteUser = 'camus000',
    [string]$RemoteHost = '10.0.0.80',
    [string]$RemoteDir = '/mnt/980pro/t850-verify',
    [string]$RemoteBranch = 'remote-verify',
    [string]$Configuration = 'Release',
    [string]$Targets = 'DayScene',
    [int]$Parallel,
    [string]$RootDir,
    [switch]$Build,
    [switch]$Check,
    [switch]$FullBundle
)

$ErrorActionPreference = 'Stop'
if (-not $RootDir) { $RootDir = Split-Path -Parent $PSScriptRoot }
$sshTarget = "$RemoteUser@$RemoteHost"
$sshArgs = @('-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15')

function Invoke-Remote {
    param([string]$Command, [switch]$Quiet)
    $output = & ssh @sshArgs $sshTarget $Command 2>&1
    if ($LASTEXITCODE -ne 0) {
        if (-not $Quiet) { $output | ForEach-Object { Write-Host "  remote: $_" -ForegroundColor DarkGray } }
        throw "remote command failed (exit $LASTEXITCODE): $Command"
    }
    return $output
}

Write-Host ''
Write-Host "T850 remote verify — ${sshTarget}:${RemoteDir}" -ForegroundColor Cyan

# ---------------------------------------------------------------- preflight

$remoteInfo = @(Invoke-Remote -Command 'nproc; cmake --version | head -1; git --version; df -Pm /mnt/980pro | tail -1 | tr -s " " | cut -d" " -f4')
$cores = ($remoteInfo[0] | Out-String).Trim()
$cmakeVersion = (($remoteInfo[1] | Out-String).Trim() -replace '^cmake version ', '')
$gitVersion = (($remoteInfo[2] | Out-String).Trim() -replace '^git version ', '')
$mntFreeMB = ($remoteInfo[3] | Out-String).Trim()
Write-Host ("  remote: {0} cores, cmake {1}, git {2}, /mnt free {3} GB" -f $cores, $cmakeVersion, $gitVersion, [Math]::Round([double]$mntFreeMB / 1024, 0))

$exists = (Invoke-Remote -Command "test -d $RemoteDir/.git && echo yes || echo no").Trim()
if ($Check) {
    if ($exists -ne 'yes') {
        Write-Host "  remote worktree: absent ($RemoteDir)" -ForegroundColor Yellow
        exit 0
    }
    $remoteHead = (Invoke-Remote -Command "cd $RemoteDir && git log -1 --format='%h %s'").Trim()
    Write-Host "  remote worktree: $RemoteDir"
    Write-Host "  remote HEAD: $remoteHead"
    exit 0
}

# ---------------------------------------------------------------- bundle

$localHead = (git -C $RootDir rev-parse HEAD).Trim()
$shortHead = $localHead.Substring(0, 8)
$bundle = Join-Path $env:TEMP "t850-remote-$shortHead.bundle"
Remove-Item -LiteralPath $bundle -Force -ErrorAction SilentlyContinue

# An incremental bundle only applies if the remote already has the prerequisite commit, so ask the
# remote what it has. A fresh worktree (or one that somehow diverged) gets the full branch.
$remoteHeadCommit = ''
if ($exists -eq 'yes') {
    $remoteHeadCommit = ((Invoke-Remote -Command "cd $RemoteDir && git rev-parse --verify --quiet HEAD 2>/dev/null || true" -Quiet) | Out-String).Trim()
}
$base = ''
# `git rev-parse HEAD` on an empty repository prints the literal string "HEAD" and fails, and "HEAD"
# resolves locally to our own HEAD — which would make the range empty and git would refuse to write
# the bundle. Only accept a real object id that exists here.
if (-not $FullBundle -and $remoteHeadCommit -match '^[0-9a-f]{40}$') {
    & git -C $RootDir cat-file -e "$remoteHeadCommit^{commit}" 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) {
        & git -C $RootDir merge-base --is-ancestor $remoteHeadCommit $localHead 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) { $base = $remoteHeadCommit }
    }
}

# Already current: nothing to transfer. Only the build step (if asked for) still runs.
$alreadyCurrent = ($base -and $base -eq $localHead)
if ($alreadyCurrent) {
    Write-Host "  remote is already at $shortHead — nothing to sync"
}

if (-not $alreadyCurrent) {
if ($base) {
    $bundleArgs = @('bundle', 'create', $bundle, "^$base", 'backyard_scene')
} else {
    $bundleArgs = @('bundle', 'create', $bundle, 'backyard_scene')
}
$bundleOutput = & git -C $RootDir @bundleArgs 2>&1
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $bundle)) {
    throw "git bundle failed: $($bundleOutput -join ' | ')"
}
$bundleMB = [Math]::Round((Get-Item -LiteralPath $bundle).Length / 1MB, 1)
$mode = if ($base) { "incremental from $($base.Substring(0,8))" } else { 'full branch' }
Write-Host "  bundle: $bundleMB MB ($mode)"

$remoteBundle = "/tmp/t850-$shortHead.bundle"
& scp @sshArgs -q $bundle "${sshTarget}:$remoteBundle"
if ($LASTEXITCODE -ne 0) { throw "scp failed" }

# ---------------------------------------------------------------- fetch into the remote worktree

$initScript = @'
set -e
dir="__DIR__"
bundle="__BUNDLE__"
branch="__BRANCH__"
if [ ! -d "$dir/.git" ]; then
  echo "  creating $dir"
  mkdir -p "$dir"
  cd "$dir"
  git init -q
  git config advice.detachedHead false
  git config core.autocrlf false
else
  cd "$dir"
fi
git fetch -q --force "$bundle" "refs/heads/backyard_scene:refs/heads/$branch"
git checkout -q -f "$branch" 2>/dev/null || git checkout -q -f -B "$branch" "refs/heads/$branch"
git reset -q --hard "refs/heads/$branch"
echo "  remote HEAD: $(git log -1 --format='%h %s')"
rm -f "$bundle"
'@
$initScript = $initScript.Replace('__DIR__', $RemoteDir).Replace('__BUNDLE__', $remoteBundle).Replace('__BRANCH__', $RemoteBranch)
$initScript = $initScript -replace "`r`n", "`n"
$tmpScript = Join-Path $env:TEMP "t850-remote-init.sh"
[System.IO.File]::WriteAllText($tmpScript, $initScript, [System.Text.UTF8Encoding]::new($false))
& scp @sshArgs -q $tmpScript "${sshTarget}:/tmp/t850-remote-init.sh"
if ($LASTEXITCODE -ne 0) { throw "scp of the init script failed" }

Write-Host '  syncing...'
Invoke-Remote -Command 'bash /tmp/t850-remote-init.sh; rm -f /tmp/t850-remote-init.sh' | ForEach-Object { Write-Host "  $_" }
Remove-Item -LiteralPath $bundle, $tmpScript -Force -ErrorAction SilentlyContinue
}   # if (-not $alreadyCurrent)

if (-not $Build) {
    Write-Host ''
    if ($alreadyCurrent) { Write-Host 'Already current; nothing to do (add -Build to rebuild).' -ForegroundColor Green }
    else { Write-Host 'Synced. Add -Build to configure and build there.' -ForegroundColor Green }
    exit 0
}

# ---------------------------------------------------------------- build

if (-not $Parallel) {
    $Parallel = [Math]::Max(1, [int]$cores - 2)
}
$buildDir = "$RemoteDir/build/linux-verify"
$buildScript = @'
set -e
dir="__DIR__"
build="$dir/build/linux-verify"
cfg="__CONFIG__"
targets="__TARGETS__"
jobs="__JOBS__"
start=$(date +%s)
cmake -S "$dir/T850" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE="$cfg" 2>&1 | tail -25
mid=$(date +%s)
echo "  configure: $((mid - start))s"
set +e
cmake --build "$build" --target $targets --parallel "$jobs" 2>&1 | tail -60
status=${PIPESTATUS[0]}
set -e
end=$(date +%s)
echo "  build: $((end - mid))s  total: $((end - start))s"
if [ "$status" -ne 0 ]; then
  echo "  BUILD FAILED (exit $status)"
  exit "$status"
fi
ls -l "$build/bin/$cfg/DayScene" 2>/dev/null || ls -l "$dir/bin/SteamDeck/$cfg/DayScene" 2>/dev/null || true
'@
$buildScript = $buildScript.Replace('__DIR__', $RemoteDir).Replace('__CONFIG__', $Configuration).Replace('__TARGETS__', $Targets).Replace('__JOBS__', "$Parallel")
$buildScript = $buildScript -replace "`r`n", "`n"
$tmpBuild = Join-Path $env:TEMP "t850-remote-build.sh"
[System.IO.File]::WriteAllText($tmpBuild, $buildScript, [System.Text.UTF8Encoding]::new($false))
& scp @sshArgs -q $tmpBuild "${sshTarget}:/tmp/t850-remote-build.sh"
if ($LASTEXITCODE -ne 0) { throw "scp of the build script failed" }

Write-Host "  building ($Configuration, $Parallel jobs)..."
Invoke-Remote -Command 'bash /tmp/t850-remote-build.sh; rm -f /tmp/t850-remote-build.sh' | ForEach-Object { Write-Host "  $_" }
Remove-Item -LiteralPath $tmpBuild -Force -ErrorAction SilentlyContinue

Write-Host ''
Write-Host 'PASS: remote worktree synced and built' -ForegroundColor Green
