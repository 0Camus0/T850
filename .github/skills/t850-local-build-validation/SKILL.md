---
name: t850-local-build-validation
description: "Use when asked to prove all T850 builds pass locally, run the Windows Debug/Release architecture matrix, validate source registration, run gameplay self-tests, compile both Android ABIs, check Steam Deck/SteamRT locally, or produce a local pre-PR build health report."
argument-hint: "State changed subsystem, required platforms/configurations, whether clean builds are required, and available Android/Podman toolchains."
---

# T850 Complete Local Build Validation

Use this workflow before declaring a broad engine change healthy. Execute the available matrix; classify unavailable toolchains as blocked, never as passed.

## 1. Establish Roots and Protect Local State

```powershell
$RepoRoot = git rev-parse --show-toplevel
$SourceRoot = Join-Path $RepoRoot 'T850'
Set-Location $RepoRoot
```

Require `T850/T850.sln` and `T850/scripts/ValidateBuildRegistration.ps1`.

Record but do not clean the worktree:

```powershell
git status --short
git diff --check
git diff --stat
```

This repository commonly has machine-local changes such as:

```text
T850/config.json
T850/Librerias/vcpkg
```

Do not revert, stage, or commit them unless explicitly requested. Do not delete user processes merely to make a relink pass; check locks first and use focused targets when appropriate.

## 2. Validate Build Registration First

Every Framework source/header must be registered in Visual Studio, desktop/Steam CMake, Android CMake, and filters where applicable.

```powershell
& .\T850\scripts\ValidateBuildRegistration.ps1
if ($LASTEXITCODE -ne 0) { throw 'Build registration failed' }
```

Fix registration before spending time on platform builds.

## 3. Run the Authoritative Windows Matrix

The repository script runs six cells and verifies outputs:

```powershell
& .\T850\scripts\RunWindowsBuildMatrix.ps1 -Action Build
```

Cells:

```text
Debug  | Win32
Release| Win32
Debug  | x64
Release| x64
Debug  | ARM64
Release| ARM64
```

It also runs gameplay/terrain self-tests on Win32 and x64. Expected final line:

```text
Windows CI build matrix PASS (6 cells)
```

Use `-Action Rebuild` when validating compiler/header/build-system changes or when stale outputs are plausible.

If memory is constrained:

```powershell
$env:T850_BUILD_WORKERS = '4'
```

### Toolchain selection

Use the repository scripts rather than manually selecting MSBuild. If direct ARM64 diagnosis is required, locate a Visual Studio 2022 installation that actually has Host x64 to ARM64 tools:

```powershell
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
& $VsWhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 `
  -property installationPath
```

Do not interpret an x64-only Build Tools installation as a source failure. On machines with multiple VS installs, ARM64 may be available only in Community/Professional.

### Focused build during iteration

Use only before the full gate:

```powershell
$MsBuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe'
& $MsBuild "$SourceRoot\T850.sln" `
  /p:Configuration=Release /p:Platform=x64 `
  '/t:Framework;DayScene' /nologo /v:minimal
```

Quote `'/t:Framework;DayScene'` as one PowerShell argument. A focused pass does not replace the six-cell matrix.

## 4. Require Engine Self-Tests

Run from a built output or use the matrix result:

```powershell
& "$SourceRoot\bin\x64\Release\DayScene.exe" --game-selftest
if ($LASTEXITCODE -ne 0) { throw 'Game self-tests failed' }
```

Expected current result: 39 `PASS` lines and exit 0. Any failure blocks completion.

## 5. Build Android ABIs Sequentially

Do not run Android/vcpkg installs concurrently against the same vcpkg root; its install lock will fail one build.

```powershell
$Sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'

& .\T850\scripts\android\BuildAndroid.bat Release `
  --allow-unsigned-release --sdk $Sdk --abi arm64-v8a
if ($LASTEXITCODE -ne 0) { throw 'Android arm64-v8a failed' }

& .\T850\scripts\android\BuildAndroid.bat Release `
  --allow-unsigned-release --sdk $Sdk --abi x86_64
if ($LASTEXITCODE -ne 0) { throw 'Android x86_64 failed' }
```

Use `--clean` when headers, shared Vulkan code, CMake lists, ABI-facing types, or generated build state changed:

```powershell
& .\T850\scripts\android\BuildAndroid.bat Release `
  --allow-unsigned-release --sdk $Sdk --abi arm64-v8a --clean
```

A clean build is required to prove a low-level file compiled; an incremental Gradle success may mark native objects up to date.

Success requires exit 0 and an APK under:

```text
T850/android/app/build/outputs/apk/development/release/
T850/android/app/build/outputs/apk/production/release/
```

Unsigned Release is a compile gate, not release-signing proof. Preserve existing local signing properties/keystores and never print secrets.

## 6. Validate Steam Deck / SteamRT

On Linux or a Podman-capable host:

```bash
cd T850
bash -n steamdeck/BuildSteamRuntime.sh
bash -n steamdeck/PackageSteamDeckRelease.sh
bash -n steamdeck/T850.sh
bash -n steamdeck/T850DeckLauncher.sh
python3 -m py_compile steamdeck/T850DeckLauncher.py
./steamdeck/BuildSteamRuntime.sh --configuration Release
```

Require:

```text
bin/SteamDeck/Release/DayScene
bin/SteamDeck/Release/libc++.so.1
bin/SteamDeck/Release/libc++abi.so.1
bin/SteamDeck/Release/libunwind.so.1
```

On Windows without Podman/WSL, do not install WSL implicitly and do not claim SteamRT passed. Report:

```text
SteamRT local build: BLOCKED - no Podman-capable Linux environment
```

Use the GitHub Steam Deck job as the independent platform gate via `t850-github-ci-validation`.

## 7. Runtime and Rendering Smoke Selection

Scale runtime checks to the changed subsystem:

- shared renderer/backend: fixed-frame D3D11, D3D12, Vulkan, GL captures;
- D3D12/Vulkan resource lifetime: explicit API stress plus error-log scan;
- voxel streaming: movement across multiple recenter thresholds;
- block edits: repeated place/remove and background navmesh completion;
- scene serialization: parse changed JSON and save/reload round trip;
- native crash/assert: use `t850-crash-debugging` before changing code.

For cross-API image checks use `t850-api-frame-comparison`.

## 8. Common Failure Classification

| Failure | Classification / next action |
|---|---|
| source registration validator | project metadata defect |
| compile/link error in one cell | source/platform defect |
| T8ditor/DayScene locked | running user process; use focused target or coordinate shutdown |
| mixed Community/BuildTools includes | shell environment contamination; use clean toolchain shell/script |
| Android vcpkg lock | builds ran concurrently; rerun sequentially |
| missing SDK/NDK/JDK | environment blocked; run setup, do not patch source |
| Steam download timeout | external infrastructure; retry/mirror only after confirming repeated endpoint failure |
| self-test failure | product defect; do not call matrix healthy |
| warnings in unrelated generated/legacy code | report separately; do not silently label errors |

## 9. Final Local Gate

Before reporting:

```powershell
Set-Location $RepoRoot
git diff --check
git status --short
Get-Process DayScene,T8ditor -ErrorAction SilentlyContinue
```

Require:

- registration passed;
- all six Windows cells passed;
- 41/41 tests passed on supported hosts;
- Android arm64-v8a and x86_64 passed sequentially;
- SteamRT passed locally or is explicitly blocked;
- requested runtime/visual checks passed;
- no test process remains;
- no accidental generated files are staged;
- no unrelated worktree changes were reverted.

Report every platform/configuration separately, including blocked/skipped cells and exact reasons. Do not compress "all healthy" over an unavailable target.

Related skills:

- `t850-build-run`
- `t850-platform-deploy`
- `t850-api-frame-comparison`
- `t850-crash-debugging`

Related documentation:

- `documentation/development/windows-build-and-run.md`
- `documentation/platform/android.md`
- `documentation/platform/steam-deck.md`
- `documentation/testing/verification.md`
