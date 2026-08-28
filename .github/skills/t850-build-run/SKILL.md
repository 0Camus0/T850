---
name: t850-build-run
description: "Use when asked to set up, build, rebuild, run, test, configure, launch, download assets, validate glTF, smoke-test T8ditor, or diagnose Windows build/runtime failures in T850."
argument-hint: "State target platform/configuration, scene/model, and desired test or run."
---

# T850 Build and Run Workflow

This is a procedural skill. Execute commands; do not stop at a plan unless blocked by a missing toolchain.

## 1. Establish Roots

```powershell
$RepoRoot = git rev-parse --show-toplevel
$SourceRoot = Join-Path $RepoRoot 'T850'
Set-Location $SourceRoot
```

If `T850.sln` is absent, stop: the wrong root is selected.

## 2. Choose the Operation

### First-time Windows setup

From repository root:

```powershell
Set-Location $RepoRoot
.\LaunchSolution.bat --setup-only
```

For ARM64 provisioning:

```powershell
.\LaunchSolution.bat --arm64 --setup-only
```

Success: vcpkg setup and cloud downloads complete with exit 0. Stop on missing VS 2022/v143; do not use a newer ABI.

### Build

From source root:

```powershell
.\scripts\build.ps1 -Config Debug -Platform x64
```

Use Release or ARM64 only when required:

```powershell
.\scripts\build.ps1 -Config Release -Platform x64
.\scripts\build.ps1 -Config Debug -Platform ARM64
.\scripts\build.ps1 -Config Release -Platform ARM64
```

Expected: `BUILD SUCCEEDED`, exit 0. The default action rebuilds the full solution.

To use the same incremental action as GitHub Actions:

```powershell
.\scripts\build.ps1 -Config Debug -Platform x64 -Action Build
```

To run the exact six-cell Windows PR/CI matrix plus Win32/x64 self-tests:

```powershell
.\scripts\RunWindowsBuildMatrix.ps1
```

If memory is constrained:

```powershell
$env:T850_BUILD_WORKERS = '4'
```

### Download assets

From repository root:

```powershell
.\LaunchSolution.bat --assets-only
```

Or source root:

```powershell
.\scripts\DownloadModels.ps1 -RootDir . -MaxThreads 7
.\scripts\DownloadTextures.ps1 -RootDir . -MaxThreads 7
```

Success: `Models ready` / `Textures ready`. Do not treat unavailable required assets as test passes.

## 3. Run from Output Directory

Always set output as working directory:

```powershell
Set-Location (Join-Path $SourceRoot 'bin\x64\Release')
```

### DayScene examples

```powershell
.\DayScene.exe --api d3d11 --scene 0 --model Models/DamagedHelmet.glb
.\DayScene.exe --api d3d12 --scene 1 --width 1920 --height 1080
.\DayScene.exe --api d3d11 --scene 4 --sceneFile Scenes/DayScene.t8scene
```

Scene indices: 0 Sandbox, 1 DayScene, 2 Quake3Mock, 3 RagdollEditor, 4 SceneTemplate, 5 VoxelScene, 6 Minecraft.

Use `--help` instead of guessing a flag:

```powershell
.\DayScene.exe --help
```

### T8ditor examples

```powershell
.\T8ditor.exe --api d3d12 --width 1920 --height 1080
.\T8ditor.exe --api d3d11 --sceneFile Scenes/DayScene.t8scene
```

The launcher maps editor runs to D3D12/Vulkan on x64 and ARM64, and D3D11/Vulkan on Win32; direct CLI accepts D3D11/D3D12/GL/Vulkan.

### WPF launcher

```powershell
Set-Location $SourceRoot
.\scripts\Launcher.ps1
```

Use it for interactive build/run/config/device selection. For automated evidence, prefer direct scripts.

## 4. Tests

### Gameplay suite

```powershell
& (Join-Path $SourceRoot 'bin\x64\Debug\DayScene.exe') --game-selftest
```

Expected: all 39 lines pass, exit 0. Any fail blocks completion.

### Offline glTF

Run from output directory so dependencies are available:

```powershell
.\DayScene.exe --validateGltf Models/DamagedHelmet.glb
```

Expected: structural summary and exit 0.

### Runtime smoke

```powershell
.\DayScene.exe `
  --api d3d11 --scene 4 --sceneFile Scenes/DayScene.t8scene `
  --width 1280 --height 720 `
  --regressionFixedDt 0.0166666667 `
  --dumpSnapshot-seconds 1 `
  --logLevel info --logFile logs/runtime-smoke.log
```

Expected: new `dumps_d3d11_*`, backbuffer/snapshot, exit 0, no engine errors.

### T8ditor GUI smoke

Use explicit process wait:

```powershell
$log = Join-Path $PWD 'logs/editor-smoke.log'
$args = @('--api','d3d11','--sceneFile','Scenes/DayScene.t8scene','--dump-frame','30','--logFile',$log)
$p = Start-Process .\T8ditor.exe -ArgumentList $args -WorkingDirectory $PWD -PassThru
$p.WaitForExit()
if ($p.ExitCode -ne 0) { throw "Editor failed: $($p.ExitCode)" }
Select-String $log -Pattern '\[GameValidation\]|RT dump complete|\[ERROR\]'
```

## 5. Configuration Rule

DayScene reads JSON only with `--config PATH`:

```powershell
.\DayScene.exe --config config.json --api vulkan
```

Order: defaults, JSON root fields, nested JSON fields, CLI, validation. The launcher stores `config.json` state but launches with explicit CLI arguments.

## 6. Diagnose Failures

| Symptom | Next check |
|---|---|
| MSBuild missing | install VS 2022 C++ workload/v143 |
| ARM64 compiler missing | install Host x64 to ARM64 tools |
| unresolved external after new file | `.vcxproj` and CMake registration |
| newer MSVC ABI in vcpkg library | rerun `LaunchSolution.bat`, verify VS 2022 pin |
| missing DLL | run from output dir and inspect post-build copies |
| missing asset | cloud status/dependency list; use resource-relative path |
| device lost/black frame | log + visual capture; do not accept image |
| native exception, assert dialog, unexplained exit | use `t850-crash-debugging`; capture CDB stack before editing |
| config seems ignored | was `--config` supplied? check CLI override and `[config]` warnings |

## 7. Finish

For shared/game Framework changes, run at least x64 Debug and ARM64 Debug. For final work, run Debug+Release on both and Release self-tests.

Remove only generated smoke dumps/logs you created. Preserve accepted visual references.

Detailed docs:

- `documentation/development/windows-build-and-run.md`
- `documentation/development/runtime-configuration.md`
- `documentation/development/cloud-assets.md`
- `documentation/testing/verification.md`
