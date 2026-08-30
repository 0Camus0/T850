# Verification and Release Gates

Status: verified against scripts, local matrix runs, deterministic captures, and PR CI on 2026-08-30.

Use the narrowest gate that can falsify the change, then broaden according to blast radius. Do not report success from compilation alone when the change has a runtime or visual contract.

## Gate Selection

| Change | Minimum gate |
|---|---|
| one local implementation detail | owning project/file compile or focused runtime check |
| Framework API/source | x64 Debug full solution |
| gameplay/schema/physics/navigation | x64 Debug + ARM64 Debug + `--game-selftest` |
| renderer/shared scene behavior | x64 Release + focused visual capture/compare |
| new Framework source | MSBuild/CMake/filter registration audit |
| final milestone/release | x64/ARM64 Debug+Release, Release self-test, full visual matrix, platform builds |
| documentation | relative-link audit + `git diff --check` |

## Windows Build Matrix

For individual clean rebuilds from the source root:

```powershell
.\scripts\build.ps1 -Config Debug   -Platform x64
.\scripts\build.ps1 -Config Release -Platform x64
.\scripts\build.ps1 -Config Debug   -Platform ARM64
.\scripts\build.ps1 -Config Release -Platform ARM64
```

Run x86 when the change affects Win32 support:

```powershell
.\scripts\build.ps1 -Config Debug -Platform x86
```

Expected result: `BUILD SUCCEEDED` and exit code 0. `Rebuild` is the default action.

GitHub Actions uses the same script with `-Action Build`. Run the exact local Windows PR/CI matrix with:

```powershell
.\scripts\RunWindowsBuildMatrix.ps1
```

This runs registration validation, full-solution Win32/x64/ARM64 Debug+Release builds, verifies `DayScene.exe` and `T8ditor.exe` in every cell, and runs the 41 self-tests for Win32/x64 Debug and Release. ARM64 is compile/link-only on the x64 runner.

## Gameplay Self-Tests

Build x64, then run the matching executable:

```powershell
& .\bin\x64\Debug\DayScene.exe --game-selftest
& .\bin\x64\Release\DayScene.exe --game-selftest
```

Expected result: every line begins with `PASS` and process exit code is 0. Any `FAIL` or nonzero exit blocks the next milestone.

The suite currently has 41 checks covering schema/migration/IDs, validation, groups, stable registry ownership, fixed tick/pause, controllers, components, events, state machines, physics handle reuse, generated triangle-mesh body creation, mutable mesh validation, stable render handles, chunks, greedy meshing, negative coordinates, DDA, streaming budgets, atomic voxel persistence, atlas UV/bounds behavior, immutable material variants, and unavailable navigation.

For graphics-backend strategy changes, also exercise ImGui and GPU profiling on every desktop API:

```powershell
& .\bin\x64\Debug\DayScene.exe --api d3d11 --scene 4 --profile --profileFrames 8
& .\bin\x64\Debug\DayScene.exe --api d3d12 --scene 4 --profile --profileFrames 8
& .\bin\x64\Debug\DayScene.exe --api vulkan --scene 4 --profile --profileFrames 8
& .\bin\x64\Debug\DayScene.exe --api gl --scene 4 --profile --profileFrames 8
```

Require an `ImGuiSystem initialized` line, a matching `Profiler initialized (API=..., GPU=...)` line, a profiler report, exit 0, and no validation/device errors.

Focused voxel visual gate:

```powershell
.\scripts\CaptureVisualBaselines.ps1 -RunSet candidate -Cases voxel-streaming -Apis d3d11,d3d12,gl,vulkan -Force -ContinueOnError
```

Expected: four captured entries, zero engine errors, and nonuniform 1280x720 backbuffers.

## Offline glTF Validation

This does not create a graphics device:

```powershell
& .\bin\x64\Release\DayScene.exe --validateGltf Models/DamagedHelmet.glb
```

Use it after parser/accessor/material changes. Exit code 0 means the document loaded and structural/accessor checks completed.

## Runtime Smoke Test

Run from the output directory with a log and deterministic dump:

```powershell
Set-Location .\bin\x64\Release
.\DayScene.exe `
  --api d3d11 `
  --scene 4 `
  --sceneFile Scenes/DayScene.t8scene `
  --width 1280 --height 720 `
  --regressionFixedDt 0.0166666667 `
  --dumpSnapshot-seconds 1 `
  --logLevel info `
  --logFile logs/runtime-smoke.log
```

Expected:

- exit code 0;
- one new `dumps_d3d11_*` directory;
- `RT_Dump_BackBuffer.ppm` and `snapshot.json` exist;
- log has no `[ERROR]`, `device lost`, or failed submit lines.

The manifest-backed visual script is preferred for accepted regression evidence because it checks all of these conditions automatically.

## T8ditor Smoke Test

T8ditor is a GUI subsystem process on Windows; use `Start-Process -PassThru` and wait explicitly:

```powershell
Set-Location .\bin\x64\Release
$log = Join-Path $PWD 'logs/editor-smoke.log'
$args = @(
  '--api','d3d11',
  '--sceneFile','Scenes/Q3/q3dm6_mod_3_jolt.t8scene',
  '--width','1280','--height','720',
  '--dump-frame','30',
  '--logLevel','info','--logFile',$log
)
$process = Start-Process .\T8ditor.exe -ArgumentList $args -WorkingDirectory $PWD -PassThru
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "T8ditor failed: $($process.ExitCode)" }
Select-String $log -Pattern '\[GameValidation\]|RT dump complete|\[ERROR\]'
```

Expected: scene validation runs, frame 30 produces ten render-target files for the current graph, and there are no engine errors. A known warning can be acceptable only when documented and reviewed.

## Visual Regression

See [Visual regression baselines](../debug/visual-regression.md) for full procedures.

Full candidate gate:

```powershell
.\scripts\build.ps1 -Config Release -Platform x64
.\scripts\CaptureVisualBaselines.ps1 -RunSet candidate -Force -ContinueOnError
.\scripts\CompareVisualBaselines.ps1 -Tolerance 2 -OutputPath .\VisualBaselines\final-comparison.json
```

Expected: capture reports `Failed=0`; comparison reports `Comparison failures=0`.

Do not overwrite `reference` unless an intentional visual change has been reviewed. `-Tolerance 2` is the established final-tree D3D11 quantization allowance; exact comparison remains the default.

## Telemetry Gate

Telemetry writes only on normal shutdown. For an automated dump plus telemetry check, launch with `--keepRunning`, wait for the dump, close the window normally, then inspect `logs/*_TIMESTAMP.json`.

Required gameplay counter names are documented in [Diagnostics](../debug/diagnostics.md).

## Build-File Registration Audit

Every maintained gameplay/terrain/mutable-mesh source must appear in MSBuild, filters, desktop/Steam CMake, and Android's separate CMake list. Run:

```powershell
.\scripts\ValidateBuildRegistration.ps1
```

This check is also a required GitHub Actions job before Windows, Android, and Steam Deck builds.

## Documentation Audit

From the repository root:

```powershell
$broken = @()
$docs = Get-ChildItem .\documentation -Recurse -Filter *.md
foreach ($file in $docs) {
  $text = Get-Content $file.FullName -Raw
  foreach ($match in [regex]::Matches($text, '(?<!!)\[[^\]]+\]\(([^)]+)\)')) {
    $target = $match.Groups[1].Value.Trim().Trim('<','>')
    if ($target -match '^(https?://|mailto:|#)') { continue }
    $part = ($target -split '#', 2)[0]
    if (-not $part) { continue }
    $path = [IO.Path]::GetFullPath((Join-Path $file.DirectoryName ([Uri]::UnescapeDataString($part))))
    if (-not (Test-Path -LiteralPath $path)) { $broken += "$($file.FullName) -> $target" }
  }
}
if ($broken) { $broken; exit 1 }
'Documentation links PASS'
```

Then:

```powershell
git diff --check
```

## Android Gate

From the repository root after toolchain setup:

```powershell
.\T850\scripts\android\BuildAndroid.bat Release --abi arm64-v8a
```

A local signed Release requires signing configuration. For compile-only CI-style validation:

```powershell
.\T850\scripts\android\BuildAndroid.bat Release --allow-unsigned-release --abi arm64-v8a
```

See [Android build and deployment](../platform/android.md).

## Steam Deck Gate

From the source root on a host with Podman:

```bash
./steamdeck/BuildSteamRuntime.sh --configuration Release --configure-only
./steamdeck/BuildSteamRuntime.sh --configuration Release
./steamdeck/PackageSteamDeckRelease.sh --configuration Release --skip-build
```

See [Steam Deck build and deployment](../platform/steam-deck.md).

## CI Matrix

`.github/workflows/build.yml` currently runs:

- Windows: Win32, x64, ARM64 crossed with Debug and Release;
- Android: arm64-v8a and x86_64 Release APK builds;
- Steam Deck: SteamRT Release build and tarball package;
- tagged `v*` release: Windows ZIPs, Android APKs, Steam Deck tarball, and compiled launcher.

CI builds and verifies both `DayScene.exe` and `T8ditor.exe` in all six Windows cells. PR #33 run [33325073153](https://github.com/0Camus0/T850/actions/runs/33325073153) passed registration, Win32/x64/ARM64 Debug+Release, Android arm64-v8a/x86_64, and Steam Deck.

## Failure Classification

Report failures as one of:

- source/compile/link failure;
- runtime behavior failure;
- visual mismatch;
- missing required asset;
- hardware limit;
- environment/toolchain prerequisite;
- test infrastructure failure.

Do not call an environment prerequisite a source failure. Do not call a skipped case a passing case.

## Cleanup

Generated outputs are ignored. Remove only artifacts created by the current check:

```powershell
Remove-Item .\bin\x64\Release\dumps_* -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item .\bin\x64\Release\logs -Recurse -Force -ErrorAction SilentlyContinue
```

Preserve `VisualBaselines/reference` and any explicitly retained comparison report.

## Related Documents

- [Windows build and run](../development/windows-build-and-run.md)
- [Visual regression](../debug/visual-regression.md)
- [Diagnostics](../debug/diagnostics.md)
- [Android deployment](../platform/android.md)
- [Steam Deck deployment](../platform/steam-deck.md)
