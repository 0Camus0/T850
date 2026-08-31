[CmdletBinding()]
param(
    [string]$DeckHost,
    [string]$DeckUser,
    [string]$DeckRoot,
    [string]$BaseDeckRoot,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$SkipBuild,
    [switch]$SkipLauncherInstall,
    [switch]$Run,
    [switch]$Desktop
)

$ErrorActionPreference = "Stop"

$repoRoot = $PSScriptRoot
$sourceRoot = Join-Path $repoRoot "T850"
$deckConfigPath = Join-Path $repoRoot "deckConfig.json"

if (Test-Path -LiteralPath $deckConfigPath) {
    $deckConfig = Get-Content -Raw -LiteralPath $deckConfigPath | ConvertFrom-Json
    if (-not $DeckHost -and $deckConfig.deckHost) { $DeckHost = [string]$deckConfig.deckHost }
    if (-not $DeckUser -and $deckConfig.deckUser) { $DeckUser = [string]$deckConfig.deckUser }
    if (-not $DeckRoot -and $deckConfig.deckRoot) { $DeckRoot = [string]$deckConfig.deckRoot }
    if (-not $BaseDeckRoot -and $deckConfig.baseDeckRoot) { $BaseDeckRoot = [string]$deckConfig.baseDeckRoot }
}

if (-not $DeckHost) { throw "DeckHost is required. Pass -DeckHost or configure deckConfig.json." }
if (-not $DeckUser) { $DeckUser = "deck" }
if (-not $DeckRoot) { $DeckRoot = "/home/$DeckUser/Code/T850-minecraft-atlas-test" }
if (-not $BaseDeckRoot) { $BaseDeckRoot = "/home/$DeckUser/Code/T850" }

foreach ($value in @($DeckHost, $DeckUser, $DeckRoot, $BaseDeckRoot, $Configuration)) {
    if ($value -match "['`"`r`n]") { throw "Unsafe SSH argument: $value" }
}
if ($DeckRoot -eq $BaseDeckRoot) {
    throw "DeckRoot must be a managed update directory, not the base Deck checkout."
}
if (-not (Test-Path -LiteralPath $sourceRoot)) { throw "T850 source directory not found: $sourceRoot" }

foreach ($command in @("git", "tar", "ssh", "scp")) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Required command is not available: $command"
    }
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("t850-deck-update-" + [guid]::NewGuid().ToString("N"))
$stageRoot = Join-Path $tempRoot "stage"
$baseArchive = Join-Path $tempRoot "base.tar"
$uploadArchive = Join-Path $tempRoot "t850-working-tree.tar.gz"
$remoteScript = Join-Path $tempRoot "update-remote.sh"
$remoteToken = [guid]::NewGuid().ToString("N")
$remoteArchive = "/tmp/t850-working-tree-$remoteToken.tar.gz"
$remoteScriptPath = "/tmp/t850-update-$remoteToken.sh"
$target = "$DeckUser@$DeckHost"

try {
    New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null

    Push-Location $repoRoot
    try {
        & git archive --format=tar --output=$baseArchive HEAD
        if ($LASTEXITCODE -ne 0) { throw "git archive failed with exit code $LASTEXITCODE" }
        & tar -xf $baseArchive -C $stageRoot
        if ($LASTEXITCODE -ne 0) { throw "tar extraction failed with exit code $LASTEXITCODE" }

        $overlayPaths = @(
            & git diff --name-only --diff-filter=ACMRTUXB HEAD --
            & git ls-files --others --exclude-standard --
        ) | Where-Object {
            $_ -and -not $_.StartsWith("T850/Librerias/vcpkg", [StringComparison]::OrdinalIgnoreCase)
        } | Sort-Object -Unique

        foreach ($relativePath in $overlayPaths) {
            $sourcePath = Join-Path $repoRoot $relativePath
            if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { continue }
            $destinationPath = Join-Path $stageRoot $relativePath
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationPath) | Out-Null
            Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
        }

        $deletedPaths = @(& git diff --name-only --diff-filter=D HEAD --)
        foreach ($relativePath in $deletedPaths) {
            $stagedPath = Join-Path $stageRoot $relativePath
            if (Test-Path -LiteralPath $stagedPath) { Remove-Item -LiteralPath $stagedPath -Force }
        }
    } finally {
        Pop-Location
    }

    & tar -czf $uploadArchive -C $stageRoot .
    if ($LASTEXITCODE -ne 0) { throw "update archive creation failed with exit code $LASTEXITCODE" }

    $buildFlag = if ($SkipBuild) { "0" } else { "1" }
    $installFlag = if ($SkipLauncherInstall) { "0" } else { "1" }
    $runFlag = if ($Run) { "1" } else { "0" }
    $runMode = if ($Desktop) { "--desktop" } else { "--game-mode" }

    $remoteScriptText = @'
#!/usr/bin/env bash
set -euo pipefail

root="$1"
base_root="$2"
configuration="$3"
archive="$4"
build="$5"
install_launcher="$6"
run_game="$7"
run_mode="$8"

mkdir -p "$root"
tar -xzf "$archive" -C "$root"
rm -f "$archive"

vcpkg="$root/T850/Librerias/vcpkg"
if [[ ! -x "$vcpkg/vcpkg" ]]; then
  source_vcpkg="$base_root/T850/Librerias/vcpkg"
  if [[ ! -x "$source_vcpkg/vcpkg" ]]; then
    echo "[T850] Missing reusable Deck vcpkg tree: $source_vcpkg" >&2
    exit 2
  fi
  rm -rf "$vcpkg"
  mkdir -p "$vcpkg"
  cp -a "$source_vcpkg/." "$vcpkg/"
fi

chmod +x "$root/T850/steamdeck/"*.sh
cd "$root/T850"

if [[ "$build" == "1" ]]; then
    find "$root/build/steamdeck-steamrt-libcpp" -type f -name '*.pch' -delete 2>/dev/null || true
  ./steamdeck/BuildSteamRuntime.sh --configuration "$configuration"
fi

if [[ "$install_launcher" == "1" ]]; then
  ./steamdeck/InstallSteamDeckLauncher.sh
fi

if [[ "$run_game" == "1" ]]; then
  systemctl --user stop t850-minecraft.service 2>/dev/null || true
  systemctl --user reset-failed t850-minecraft.service 2>/dev/null || true
  systemd-run --user --unit=t850-minecraft --collect \
    --property="WorkingDirectory=$root/T850" \
    --setenv=XDG_RUNTIME_DIR=/run/user/1000 \
    --setenv=WAYLAND_DISPLAY=wayland-0 \
    --setenv=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
    --setenv=SDL_VIDEODRIVER=wayland \
    "$root/T850/steamdeck/T850.sh" "$run_mode" --scene 6 --log-level info
  systemctl --user show t850-minecraft.service -p ActiveState -p SubState -p MainPID
fi

echo "[T850] Steam Deck update complete: $root"
'@
    $remoteScriptText = $remoteScriptText -replace "`r`n", "`n"
    [IO.File]::WriteAllText($remoteScript, $remoteScriptText, [Text.UTF8Encoding]::new($false))

    Write-Host "[T850] Uploading current working tree to ${target}:$DeckRoot"
    & scp -q $uploadArchive "${target}:$remoteArchive"
    if ($LASTEXITCODE -ne 0) { throw "source upload failed with exit code $LASTEXITCODE" }
    & scp -q $remoteScript "${target}:$remoteScriptPath"
    if ($LASTEXITCODE -ne 0) { throw "remote updater upload failed with exit code $LASTEXITCODE" }

    & ssh -o BatchMode=yes $target "bash '$remoteScriptPath' '$DeckRoot' '$BaseDeckRoot' '$Configuration' '$remoteArchive' '$buildFlag' '$installFlag' '$runFlag' '$runMode'; status=`$?; rm -f '$remoteScriptPath' '$remoteArchive'; exit `$status"
    if ($LASTEXITCODE -ne 0) { throw "Steam Deck update failed with exit code $LASTEXITCODE" }

    if (-not $SkipLauncherInstall) {
        Write-Host "[T850] Steam shortcut updated. Restart Steam or return to Game Mode to refresh the library entry."
    }
} finally {
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}