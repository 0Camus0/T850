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

This runs registration validation, full-solution Win32/x64/ARM64 Debug+Release builds, verifies `DayScene.exe` and `T8ditor.exe` in every cell, and runs the 43 self-tests for Win32/x64 Debug and Release. ARM64 is compile/link-only on the x64 runner.

## Gameplay Self-Tests

Build x64, then run the matching executable:

```powershell
& .\bin\x64\Debug\DayScene.exe --game-selftest
& .\bin\x64\Release\DayScene.exe --game-selftest
```

Expected result: every line begins with `PASS` and process exit code is 0. Any `FAIL` or nonzero exit blocks the next milestone.

## Cross-Backend Compute Self-Test

Run the standalone arithmetic dispatch after an x64 build:

```powershell
& .\bin\x64\Debug\DayScene.exe --compute-selftest --d3d12debug
```

The command creates a minimal application without scene assets. It first compiles
`Shaders/CS_Arithmetic.hlsl`, dispatches over 96 integers, and validates structured-buffer
readback. It then runs paired image-write/image-read kernels at `1x1`, `7x5`, and
`257x129`; each pair writes an RGBA8 storage texture, samples it in a second dispatch,
writes packed pixels to a structured buffer, and checks every pixel on the CPU. Require three
`[ComputeImage] PASS` lines, final `PASS: arithmetic and odd-sized image kernels`, and exit
code 0. D3D12 remains the default; add `--api d3d11`, `--api vulkan`, `--api webgpu`, or
`--api gl` for the other implementations. Desktop GL requires an OpenGL 4.3+ context;
the 3.3 fallback is raster-only and correctly rejects this compute-only self-test.

To verify compute permutation recording and D3D12 artifact caching, add `--dumpShaderPermutations --shaderPermutationOutput <temporary-json> --logLevel debug`. Require a version-2 `compute_permutations` entry named `CS_Arithmetic.hlsl:CS:base` with a matching `key`, `kind=compute`, entry point `CS`, permutation `base`, and no defines. The graphics `permutations` section must contain only hexadecimal keys. Backend profiles are intentionally artifact metadata rather than source-permutation identity. On a cold D3D12 cache, require `CS stored`; on the next identical run, require `CS hit`.

The arithmetic workload is standalone-only; normal DayScene no longer records its diagnostic `Dispatch(2, 1, 1)`. Use `--compute-selftest` when validating structured-buffer compute and use the real God Rays dispatch for live PIX inspection.

DayScene's God Rays calculation uses compute on D3D11, D3D12, Vulkan, WebGPU, and desktop OpenGL 4.3+ when `--postProcessMode compute` is selected. At 1280x720, require `Pass 'God Rays' dispatched compute 160 x 90 x 1`. D3D12 PIX must show `CS_GodRays.hlsl Compute PSO`, two sampled depth textures, two samplers, and the `GodRaysCalc` storage UAV. `--postProcessMode raster` must suppress every graph compute pipeline on each supported API. Desktop GL below 4.3 and OpenGL ES must use the raster fallback.

Post-processing uses `--postProcessMode compute|raster`. `compute` selects every declared alternative for A/B testing, and `raster` suppresses all graph compute pipelines, including Minecraft TorchParticles. At 1280x720, forced compute requires 160x90 God Rays/blur/HDR dispatches and a 64x64 Bright dispatch. Shadow and bloom blur remain raster. On desktop GL 4.3+, forcing compute must create and dispatch every declared GL compute pipeline; older desktop GL and OpenGL ES must log the raster fallback. Matched D3D11, D3D12, Vulkan, WebGPU, and desktop GL replay comparisons should retain every intermediate target within tolerance 2; record and review any backend-specific final backbuffer variance.

All maintained render graphs must create and dispatch `CS_Bright` and `CS_HDRComposite` when compute is forced. Validate DayScene scenes 0-3 and 5-6 directly; SceneTemplate scene 4 requires an explicit `--sceneFile`. Also run T8ditor separately because it has its own CLI/parser and graph. D3D11, D3D12, Vulkan, WebGPU, and desktop GL 4.3+ shared-graph plus Minecraft smoke tests must dispatch both kernels; older desktop GL and OpenGL ES must log raster fallbacks, zero compute dispatches, and complete a nonuniform frame dump.

For visual parity, capture matched fixed-time compute and raster frames on D3D11, D3D12, and Vulkan and compare all targets with tolerance 2. The first three-backend comparison matched all 18 targets exactly on each API, including `RT_Dump_GodRays.ppm`. Also run an odd output size such as 1023x577 and require a `128 x 73 x 1` dispatch to exercise bounds checking.

### Manual God Rays compute/raster validation

Run from `bin/x64/Debug` with `--logLevel info` and a unique log per API/mode. Test `compute` and `raster` on D3D11, D3D12, Vulkan, WebGPU, and desktop GL 4.3+. For older desktop GL or OpenGL ES, request `compute` and require the logged raster fallback.

In every interactive run, press and release `G` once to open runtime controls, select `GodRays` in the Debug RT selector, and inspect the isolated target. Press and release `G` again to hide the controls while leaving the selected target visible. Compare the compute and raster images using the same camera and settings.

Expected compute log evidence on each supported API is pipeline creation followed by `Pass 'God Rays' dispatched compute 320 x 180 x 1 for 2560x1440 output`. `--postProcessMode raster` must report the graphics implementation and must not mention `CS_GodRays` or `dispatched compute`. Older desktop GL and OpenGL ES compute mode must report `using raster fallback on API=gl`.

For a PIX GPU capture, use D3D12 compute mode, hide the runtime controls after selecting the God Rays debug target, then capture a frame. Search Events for `Dispatch`; normal DayScene should contain the God Rays `Dispatch(320,180,1)`, not the standalone arithmetic `Dispatch(2,1,1)`. Select it and require Pipeline/State to reference `Shaders/CS_GodRays.hlsl Compute PSO`, root constants at `b0`, sampled scene/shadow depth at `t0`/`t1`, samplers at `s0`/`s1`, and the `GodRaysCalc` storage UAV at `u0`. A following UAV/resource barrier is expected. Missing standalone `Set*` rows in PIX Events is not a failure because Pipeline/State reconstructs the bound state at the selected dispatch.

For an external capture tool, add `--compute-selftest-wait 10`. The process waits ten seconds before and after the dispatch so a PIX timing capture can start and stop around the GPU work without adding the compute operation to a scene.

The suite currently has 60 checks. In addition to gameplay, scene, terrain, physics,
navigation, material, and lifecycle contracts, `T-COMPUTE-GRAPH-01` loads every maintained
render graph under strict parsing and rejects unknown keys, missing storage usage, read/write
feedback, invalid permutations, and incomplete typed binding layouts.

Validate the authored Minecraft block-to-atlas contract without creating a graphics device:

```powershell
python .\scripts\verify_minecraft_atlas.py
```

Exercise live Minecraft enemy removal and expansion through the same path as the ImGui slider:

```powershell
.\bin\x64\Release\DayScene.exe --api d3d12 --scene 6 --minecraftEnemyCount 0 --dump-frame 240
.\bin\x64\Release\DayScene.exe --api d3d12 --scene 6 --minecraftEnemyCount 8 --dump-frame 240
.\bin\x64\Release\DayScene.exe --api d3d12 --scene 6 --minecraftEnemySpeed 6 --dump-frame 240
```

Require the matching `Enemy count changed` or `Enemy speed changed` line, `Enemy population ready: active=N capacity=8`, a complete dump, and no path-unavailable, stuck, or rendering errors.

Expected: the audited `terrain.png` SHA-256, 20 block definitions, and all 120 face mappings pass.

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
