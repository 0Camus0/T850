---
name: t850-platform-deploy
description: "Use when asked to build, sign, install, deploy, launch, package, or publish T850 for Android, Steam Deck, Linux/SteamRT, or Windows release artifacts."
argument-hint: "State platform, configuration, ABI/device/Deck host, and whether to build, install, run, package, or publish."
---

# T850 Platform Deployment Workflow

Classify prerequisite failures separately from source failures. Never claim a platform passed when its toolchain was unavailable.

## Roots

```powershell
$RepoRoot = git rev-parse --show-toplevel
$SourceRoot = Join-Path $RepoRoot 'T850'
```

## Android

### Prerequisite check/setup

```powershell
Set-Location $RepoRoot
.\SetupAndroidToolchain.bat
```

Required versions: JDK 17+, SDK/target 35, min 28, build tools 35.0.0, NDK 27.2.12479018, CMake 3.22.1, Vulkan SDK.

Stop as environment-blocked when setup/tool paths are absent. Do not modify source to bypass a missing SDK.

### Build

```powershell
.\T850\scripts\android\BuildAndroid.bat Debug --abi arm64-v8a
.\T850\scripts\android\BuildAndroid.bat Release --abi arm64-v8a
```

Release requires signing unless compile-only:

```powershell
.\T850\scripts\android\BuildAndroid.bat Release --allow-unsigned-release --abi arm64-v8a
```

Success: exit 0 and APK under `T850/android/app/build/outputs/apk/development/<config>/`.

### Install and launch

```powershell
.\T850\scripts\android\BuildAndroid.bat Debug --install --launch
```

`--launch` implies install and starts `com.t850.engine/.LauncherActivity`.

### Fast repack

Only after a full template APK exists:

```powershell
.\BuildAndroidFastApk.ps1 Debug --install --launch
```

This replaces the native `.so`, zipaligns, and debug-keystore signs. It is not a production release path, even with `Release` configuration.

### Signing

Local properties/env must provide store file/password, alias, and key password. `ConfigureAndroidReleaseSigning.ps1` generates/uploads GitHub Actions secrets and requires authenticated `gh`.

Never print passwords/secrets. If an interactive process asks for secrets, tell the user to enter them directly.

### Android finish report

Report:

- SDK/NDK/JDK/Vulkan versions;
- ABI/config/asset profile;
- signed or unsigned;
- APK path/hash;
- install/launch result and device serial;
- logcat/runtime result;
- source versus environment failures.

## Steam Deck / SteamRT

### Official local build

Run on a Podman-capable Linux/Deck host:

```bash
cd T850
./steamdeck/BuildSteamRuntime.sh --configuration Release --configure-only
./steamdeck/BuildSteamRuntime.sh --configuration Release
```

Success output:

```text
bin/SteamDeck/Release/DayScene
bin/SteamDeck/Release/libc++.so.1
bin/SteamDeck/Release/libc++abi.so.1
bin/SteamDeck/Release/libunwind.so.1
```

If the script says Podman is required, report environment-blocked before CMake.

### Run

```bash
./steamdeck/T850.sh --game-mode --scene 1
./steamdeck/T850.sh --desktop --scene 4 --scene-file Scenes/DayScene.t8scene
```

The wrapper forces Vulkan, configures runtime libraries, assets/symlinks, SDL, and cloud download.

### Package

```bash
./steamdeck/PackageSteamDeckRelease.sh --configuration Release --skip-build
```

Expected tarball: `steamdeck/package/T850-SteamDeck-Release.tar.gz`.

### Windows SSH orchestration

Requires key-based SSH and `deckConfig.json` or explicit host/user:

```powershell
Set-Location $RepoRoot
.\LaunchSteamDeckSolution.bat --deck-host HOST --deck-user deck --build --setup-only
.\LaunchSteamDeckSolution.bat --deck-host HOST --deck-user deck --deploy-assets --skip-host --skip-deck --setup-only
.\LaunchSteamDeckSolution.bat --deck-host HOST --deck-user deck --install-launcher --run-deck --skip-host --skip-deck --setup-only
```

Review remote package installation: it can disable SteamOS read-only mode and invoke sudo/pacman.

### Steam finish report

Report container image, config/editor option, output/hash, package path/hash, remote host/run result, and any prerequisite/CMake/link/runtime failure.

## Windows Release Artifacts

Local build outputs are runnable directories, not complete standalone packages:

```powershell
Set-Location $SourceRoot
.\scripts\build.ps1 -Config Release -Platform x64
```

The authoritative distributable packaging is the `v*` GitHub Actions release job. It compiles `T850Launcher.exe`, stages executables/DLLs/tracked lightweight assets/cloud downloader/config, emits Windows ZIPs, and includes Android/Steam artifacts.

To build the Release launcher locally:

```powershell
.\scripts\build_launcher_release.ps1
```

This may install the `ps2exe` module.

Do not package only the executable. Include copied DLLs, launcher/downloader support, config, and assets/manifest.

## Cross-Platform Source Rule

Every new Framework source must be in Visual Studio, desktop/Steam CMake, and Android's separate CMake source list. Before platform deployment, run:

```powershell
.\scripts\ValidateBuildRegistration.ps1
```

## Detailed Guides

- `documentation/platform/android.md`
- `documentation/platform/steam-deck.md`
- `documentation/development/windows-build-and-run.md`
- `documentation/testing/verification.md`
