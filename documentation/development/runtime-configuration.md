# Runtime Configuration and CLI

Status: verified against `Config.h`, `ConfigRuntime.h/.cpp`, `DayScene/App.cpp`, and `Launcher.ps1` on 2026-09-14.

2026-09-22: `--shaderFlow auto` uses DXC/DXIL for native D3D12;
`--shaderFlow legacyHLSL` explicitly selects the previous D3DCompile/FXC/DXBC
path. WebGPU retains `auto|wgsl|spirv` source-flow selection.

## Configuration Precedence

DayScene starts from a fresh `Config` object every process. The effective order is:

1. hardcoded defaults from `Framework/include/core/Config.h`;
2. JSON loaded only when `--config <path>` is present;
3. nested JSON sections override same-purpose root JSON fields;
4. command-line arguments override JSON;
5. `ValidateConfig()` normalizes aliases, clamps invalid values, and disables inconsistent combinations.

`config.json` is not loaded automatically. The launcher reads/writes it as launcher state but launches DayScene with explicit CLI arguments. To use the file manually:

```powershell
.\DayScene.exe --config config.json --api d3d12 --width 1920 --height 1080
```

Unknown JSON keys are rejected with a source-position diagnostic. Use documented field names and treat a failed config load as an authoring error.

## Hardcoded Defaults

| Field | Default |
|---|---|
| API | `d3d11` |
| Shader flow | `auto` (DXC for native D3D12; WGSL first with HLSL/SPIR-V fallback for WebGPU) |
| Window | 1280x720, windowed |
| Scene | 0 (Sandbox) |
| Model | `Models/DamagedHelmet.glb` |
| Log level | 3 (`verbose`) |
| Culling | `full` |
| Post-process mode | `raster` |
| Profile frames | 300 |
| CPU-only profiler | false; `--profileCpuOnly` enables it with bounded profile exit |
| Captured-frame upload warning budget | 64 MB; `--telemetryUploadBudgetMB`, zero disables bytes threshold |
| Telemetry frequency | 60 frames |
| Telemetry output | `logs/perf_telemetry.json` |
| Benchmark/regression fixed delta | disabled (`0`) |

## Profiling Controls

Root JSON `profileCpuOnly` (also under `devTools`) selects CPU-only timing without
GPU queries. `telemetryUploadBudgetMB` is a root JSON field. `--benchmarkPaired`
enables the D3D12/WebGPU matrix subset at the requested resolution in submit-only
mode. Use [MeasureProfiling.ps1](../../T850/scripts/MeasureProfiling.ps1) for
finite alternating runs; its report distinguishes workload mismatch from a
timing conclusion. MSBuild `/p:T850EnableProfiling=0` and CMake
`-DT850_ENABLE_PROFILING=OFF` compile instrumentation out.

## WebGPU Shader Flow

From the selected Windows x64 executable directory:

```powershell
.\DayScene.exe --api webgpu --shaderFlow auto --scene 4 --sceneFile Scenes/ForwardScene.t8scene
.\DayScene.exe --api webgpu --shaderFlow wgsl --scene 4 --sceneFile Scenes/ForwardScene.t8scene
.\DayScene.exe --api webgpu --shaderFlow spirv --scene 4 --sceneFile Scenes/ForwardScene.t8scene
```

`auto` is the unchanged default. `wgsl` requires handwritten WGSL for engine
shader requests; `spirv` requires HLSL -> glslang/SPIR-V -> Tint/WGSL. Strict modes
never fall back to the other source language. The choice does not change the
graphics API, scene or render graph, and does not affect native backend compilers.
Dawn and upstream ImGui/internal renderer utility shaders keep their own shader
implementation; this option selects the engine shader-source flow.

The root JSON field `webgpuShaderFlow` supplies the same setting when `--config`
is used; CLI overrides it. Values are case-insensitive. Unknown, empty or missing
CLI values fail before asset loading, rather than silently choosing `auto`.
Invalid configured values also fail validation unless a valid CLI override replaces
them. Unlike older recoverable config fields, invalid flow selection is fatal.

The Windows host applies the setting on every WebGPU driver creation through
`ChangeAPI`, before `InitDriver()` and `CreateAssets()`. Startup logs
`[WebGPU] startup shaderFlow=... (before asset loading)`; per-shader logs report
the actual successful flow and cache state. Source-language cache identities stay
separate. Both Windows launchers show a **Shader Flow** selector only for WebGPU:
**WGSL preferred (auto)**, **WGSL only (strict)** or
**SPIR-V (HLSL translation)**. They persist
`webgpuShaderFlow` and pass it explicitly on the next RUN; no engine rebuild is
needed, and changing the selection does not switch an already-running process.
Older launcher configs without a supported selection default to `auto`.
The Compile Shaders action also runs all three WebGPU flows. Strict `wgsl`
intentionally fails if a selected runtime path requests anonymous HLSL without a
paired WGSL source. No fixture is injected, and T8ditor does not use this CLI
parser or support WebGPU rendering.

## WebGPU Device Recovery

The root `webgpuDeviceRecoveryAttempts` setting and
`--webgpuRecoveryAttempts <1..10>` select the consecutive full device/scene
recreation budget. The default is 3. One successful frame resets the count.
Recovery records `device.recovery.attempts`; exhausting the budget records
`device.recovery.exhausted` and shuts down cleanly.

`--webgpu-recovery-selftest` injects one loss. The developer-only
`--webgpu-recovery-stress-selftest` injects four consecutive losses and expects
three recreations followed by bounded exhaustion.

**Normal runtime selection works in `auto` and `spirv`; strict `wgsl` still has an anonymous-source limitation.**
The known DOF, CoC, shadow/SSAO, refraction and lightmap derivative-uniformity
failures were corrected on 2026-09-15. All 538 recorded/additional stages now pass
strict translation in both paths; all ten available runtime cases captured in
both `auto` and `spirv`. Native D3D12/Vulkan per-target before/after
checks are recorded in the shader notes. Strict `wgsl` still stops at anonymous
HLSL debug shaders without paired WGSL sources. Full rendering parity and broader
material coverage remain separate acceptance requirements. See
[shader flow and blockers](../rendering/shader-management.md#shader-flow-selection).

The initial strict-SPIR-V scene comparison exposed matrix translation and runtime
resource defects, including VoxelScene's missing environment binding. Those
defects are fixed; residual image differences are not silently treated as passes.
See the [runtime handoff](../rendering/webgpu-runtime-summary.md) for current
measurements, accepted exceptions, native checkpoint caveats and remaining work.

## Native D3D12 Shader Compiler Flow

Native D3D12 uses DXC and emits DXIL by default:

```powershell
.\DayScene.exe --api d3d12 --shaderFlow auto --scene 1
```

`auto` selects the highest Shader Model 6 profile reported by the adapter, up to
SM 6.6 in the current build. Graphics and compute use the same compiler helper,
DXC reflection, profile/version-qualified cache identity, `.dxil` bytecode, and
separate reflection artifacts. If the adapter does not expose Shader Model 6,
startup fails with guidance to choose the legacy path rather than silently
changing compilers.

The repository packages native DXC for x64 and ARM64. Win32 D3D12 requires
`--shaderFlow legacyHLSL`; validation rejects `auto` there with a named
diagnostic. D3D11 is unchanged.

The previous compiler remains available explicitly:

```powershell
.\DayScene.exe --api d3d12 --shaderFlow legacyHLSL --scene 1
```

`legacyHLSL` is case-insensitive, valid only with native D3D12, and uses
`D3DCompile`/FXC with `vs_5_0`, `ps_5_0`, and `cs_5_0` DXBC artifacts. DXC and
legacy cache identities cannot collide: default artifacts live under the
`d3d12` cache namespace and fallback artifacts under `d3d12-legacy`. Switching
flows does not evict the other flow's warm cache. Debug builds use
`d3d12-debug` and `d3d12-legacy-debug`, preventing `-Zi/-Od` artifacts from
colliding with optimized Release bytecode. Runtime logs identify the selected
flow, profile, and bytecode kind.

## JSON Shape

Root fields accepted by `RuntimeConfigJson` include:

```json
{
  "api": "d3d11",
  "webgpuShaderFlow": "auto",
  "webgpuDeviceRecoveryAttempts": 3,
  "width": 1280,
  "height": 720,
  "fullscreen": false,
  "scene": 0,
  "title": "T850 Project",
  "model": "Models/DamagedHelmet.glb",
  "sceneFile": "",
  "sceneProfile": "",
  "postProcessMode": "raster",
  "gui": false,
  "logLevel": "verbose",
  "logFile": "",
  "d3d12Debug": false,
  "profile": false,
  "profileFrames": 300,
  "debugFrames": false,
  "keepRunning": false,
  "dumpEnabled": false,
  "dumpByFrame": false,
  "dumpFrame": -1,
  "dumpSeconds": -1.0,
  "replaySnapshotPath": "",
  "cullingMode": "full",
  "runtimeTelemetry": false,
  "runtimeTelemetryFrequencyFrames": 60,
  "runtimeTelemetryOutputPath": "logs/perf_telemetry.json"
}
```

Preferred nested sections are also accepted:

```json
{
  "api": "vulkan",
  "display": {
    "width": 1920,
    "height": 1080,
    "fullscreen": false,
    "scene": 4,
    "sceneFile": "Scenes/DayScene.t8scene",
    "sceneProfile": ""
  },
  "dump": {
    "enabled": false,
    "trigger": "frame",
    "frame": -1,
    "seconds": -1.0
  },
  "replaySnapshot": {
    "enabled": false,
    "path": ""
  },
  "devTools": {
    "gui": false,
    "logLevel": "info",
    "logToFile": true,
    "logFile": "logs/T850.log",
    "d3d12Debug": false,
    "profile": false,
    "profileFrames": 300,
    "cullingMode": "full",
    "benchmark": false,
    "benchmarkMatrix": false,
    "offscreen": false,
    "offscreenDebug": false,
    "glOffscreenFlushMode": "frame"
  },
  "telemetry": {
    "enabled": true,
    "frequencyFrames": 60,
    "outputPath": "logs/perf_telemetry.json"
  }
}
```

Launcher-only fields such as `targetPlatform`, `configuration`, `architecture`, and `androidDeviceSerial` are ignored by DayScene but retained in launcher state.

If both `model` and `sceneFile` are supplied, a non-empty scene file wins and clears the model path. CLI `--model` clears the scene file; CLI `--sceneFile` clears the model.

## API, Logging, and Culling Aliases

Graphics API:

| Accepted | Normalized |
|---|---|
| `d3d11`, `dx11` | `d3d11` |
| `d3d12`, `dx12` | `d3d12` |
| `vulkan`, `vk` | `vulkan` |
| `gl`, `opengl` | `gl` |

Log levels: `error|0`, `info|1`, `debug|2`, `verbose|3`, `trace|4`.

Culling:

| Accepted | Mode |
|---|---|
| `enabled`, `enable`, `full`, `fullonload`, `on`, `1` | full on load |
| `lazy`, `deferred`, `2` | lazy |
| `disabled`, `disable`, `off`, `none`, `0` | disabled |

## DayScene CLI

Always prefer the binary's own help for the current list:

```powershell
.\DayScene.exe --help
```

### Renderer and Scene

```text
--config PATH
--api d3d11|d3d12|vulkan|gl
--width N --height N
--fullscreen
--scene N
--model PATH
--sceneFile PATH | --t8scene PATH
--sceneProfile NAME
--postProcessMode compute|raster
--orbitYaw RADIANS
--gui
```

Scene indices are 0 Sandbox, 1 Day, 2 Quake3Mock, 3 RagdollEditor, 4 SceneTemplate, and 5 VoxelScene.

`postProcessMode=compute` selects every `compute_if_supported` graph pass. `raster` selects the authored graphics draw or clear fallback, including Minecraft's transparent torch-particle target. `auto` is rejected. Desktop OpenGL enables compute only with an OpenGL 4.3 or newer context; older desktop GL and OpenGL ES use the retained fallbacks.

### Dumps, Replay, and Diagnostics

```text
--dump-frame N
--dumpFrame N
--dumpSnapshot-frame N
--dumpSnapshot-seconds SECONDS
--debugFrames
--replaySnapshot PATH
--keepRunning
--unattended
--dumpMatrices N
--culling full|lazy|disabled
--cullDisabled
--offscreen
--offscreenDebug
--glOffscreenFlushMode frame|wait|none
--dumpShaderPermutations
--recordShaderPermutations
--compileShaders
--shaderPermutationInput PATH
--shaderPermutationOutput PATH
```

`--debugFrames` enables manual dump requests. A normal dump exits unless `--keepRunning` is present.

`--regressionFixedDt SECONDS` uses a fixed update delta with real-time pacing and suppresses live input/random startup variation for unattended deterministic captures. Use it for regression tooling, not ordinary gameplay.

`--unattended` is for any automated run on a machine someone is using: the window is created without taking focus and the mouse is never captured (no relative mode, no cursor confinement). It changes nothing else, so timing and output are the same as a normal run.

D3D11 and D3D12 take their swap-chain window from the SDL window they are given, not from `GetActiveWindow()`, which is null for a window that never received focus.

### Benchmark

```text
--benchmark
--benchmarkMatrix
--benchmarkOutput PATH
--benchmarkReport PATH
--benchmarkFinalFrameDump
--benchmarkFinalFrameDir PATH
--benchmarkSeconds N
--benchmarkFrames N
--benchmarkFixedDt SECONDS
--benchmarkHoldFrame N
--benchmarkNoPresent
```

Benchmark matrix mode forces DayScene, D3D11 startup, 1920x1080, and onscreen start settings before the internal matrix runs.
`--benchmarkHoldFrame N` freezes simulation and physics at runtime frame `N` by
setting their effective delta to zero while rendering and presentation continue
without the fixed-delta wall-clock pacer. The runtime logs the held frame, uncapped
state and its QPC-backed timestamp once. Combine it with `--regressionFixedDt`
for deterministic external GPU measurements; it is a benchmark control, not an
ordinary gameplay pause.
`--benchmarkNoPresent` forces the offscreen submit path and suppresses progress
presentation entirely. The completion marker is emitted only after `WaitForGPU`,
so its throughput includes the final queue drain and excludes DWM/swapchain pacing.

### Backyard Water (scene 7) and the Fluid Solver

These are parsed by DayScene, not by `ConfigRuntime`, and every one of them implies `--scene 7` for
the Backyard ones. Every automated run also passes `--unattended`.

```text
--backyard-selftest                run the thirteen water gates, then exit
--backyard-selftest-frames N       frames per gate run
--backyard-no-ball                 run the gates without the scripted ball
--backyard-cell-size METRES        override the grid's derived cell size (debug)
--backyard-iterations N            run the gates at another platform's sweep count (60 = mobile/web/Deck)
--backyard-air-rise M_PER_S        run the gates at another air-rise speed (0 = old trapped air)
--backyard-substep-ceiling SECONDS longest substep for the gates (0.008333 = the old fixed 1/120 s)
--backyard-adaptive 0|1            force the fixed ceiling (0) or the measured CFL substeps (1);
                                   together with the ceiling this is how W-31's A/B was measured
--backyard-spawn N                 spawn N extra balls once the solver is ready
--backyard-spawn-seed N            fix the spawn and material RNG for a reproducible run
--backyard-pick-test               drive the pick/drag input path in-process, then exit
--backyard-pad-test                drive the gamepad grab (RT/LS/LT) and the pad quick actions
                                   in-process with a synthetic pad, then exit; needs --backyard-spawn 2+
--backyard-ball-probe N            log body 0 every N frames (see below)
--backyard-mass-audit N            log the W-03 mass audit every N frames
--fluid-selftest                   run the solver self-test, then exit
--fluid-selftest-frames N          frames for the solver self-test
--fluid-iterations N               override pressure iterations for one run
--fluid-fixed-dt SECONDS           fixed solver step for a deterministic run
--fluid-dump-volume FRAME          dump the alpha/velocity volume at a frame
```

**How many pressure sweeps run by default.** `WaterTunables::EffectivePressureIterations()` decides,
and it is the same call the headless gates use:

| Platform | Sweeps | Mechanism |
|---|---|---|
| Desktop Windows | the JSON value (120 by default) | — |
| Steam Deck | 60 | Run-time `utils::GetDeviceInfo().steamDeck` check (a Linux build, so a compile-time branch would also cap desktop Linux) |
| Android, Emscripten | 60 | Compile-time `kMobilePressureIterations` |

The Steam Deck OLED reports DMI `sys_vendor=Valve`, `product_name=Galileo`, `board_name=Galileo`; the
LCD reports `Jupiter`. `utils/DeviceInfo` owns that probe, and the input layer uses the same call for
its `Handheld detection:` line, so the two can no longer disagree. The scene logs the effective count
and the reason, e.g. `60 pressure iterations (Steam Deck cap)`.

`--backyard-ball-probe` is the only instrument that sees a *local* defect: the mass audit averages
the whole pool, where a 2 cm moat around a 0.25 m ball is 7 cells out of 82 917. It prints two lines
per sample, read from the same surface texture the renderer draws:

```text
[Backyard][ball] t=8.13 y=-0.059 vy=+0.096 level=-0.018 plane=-0.040 submerged=0.62 flow=(+0.054 +0.122) xz=(+0.098 -6.652) vxz=(+0.018 +0.018)
[Backyard][ball] far=+0.0012 ring-far=-0.0167 covered min-ring=-0.0133 rimPeak=+0.0543 moatMin=-0.1464 bands(cm): -2.4 -2.3 -2.4 -1.6 -1.3 -1.2 -1.2 -1.0
```

`level` is the sampled water line and `plane` the authored one, so the coupling is visible directly;
`ring-far` and `covered min-ring` are the float gate's two comparisons recomputed per sample;
`rimPeak`/`moatMin` are running extremes so a transient crest or crater between samples is not
lost; `bands` is the profile in eight 10 cm radial bands, which separates a *moat* (a dip in bands
0–3) from the *crater* of an impact (dip in band 0, crest in bands 1–3).

### Logging, Profiling, and Telemetry

```text
--logLevel error|info|debug|verbose|trace|0..4
--logFile PATH
--d3d12debug
--profile
--profileFrames N
--minecraftDrawDistance N
--telemetry | --runtimeTelemetry
--telemetryFrequencyFrames N
--runtimeTelemetryFrequencyFrames N
--telemetryOutput PATH
--runtimeTelemetryOutput PATH
--autoStartRagdoll
```

Telemetry output is working-directory relative unless an absolute path is supplied. It is written on normal framework shutdown. Direct `exit()` dump paths can bypass telemetry flush; use `--keepRunning`, then close the window normally when telemetry output is required.

### Early-Exit Tools

These run before renderer startup:

```powershell
.\DayScene.exe --game-selftest
.\DayScene.exe --validateGltf Models/DamagedHelmet.glb
.\DayScene.exe --dumpShaderPermutations --shaderPermutationOutput shader_permutations.json
```

`--game-selftest` returns 0 only when every gameplay test passes. `--validateGltf` returns nonzero when parsing or structural validation fails.

## Validation Rules

`ValidateConfig()` currently enforces:

- known graphics API;
- width/height in 1..16384;
- non-negative scene index;
- non-empty title;
- one valid model or scene file path string;
- log level 0..4;
- positive profile frame count;
- non-negative telemetry frequency and benchmark limits;
- finite non-negative benchmark fixed delta;
- finite regression fixed delta in 0..1;
- valid dump trigger/value combinations;
- `offscreenDebug` only when `offscreen` is enabled.

Adjusted values produce `[config] Adjusted ...` on stderr.

## T8ditor CLI

T8ditor has a separate minimal parser and defaults to D3D12:

```text
--api d3d11|d3d12|vulkan|gl
--width N --height N
--mesh PATH
--sceneFile PATH | --t8scene PATH
--dump-frame N | --dumpFrame N
--logFile PATH
--logLevel error|info|debug|verbose|trace|0..4
--d3d12debug
--postProcessMode compute|raster
```

The developer launcher maps D3D11/D3D12 editor launches to D3D12 on x64 and ARM64, but to D3D11 on Win32 because that ImGui triplet omits the D3D12 backend. GL/Vulkan selections map to Vulkan. Direct T8ditor invocation accepts all four Windows APIs when the selected backend is available in that build.

## Working Directory

Run from the executable output directory so copied DLLs, config, and asset junctions resolve:

```powershell
Set-Location F:\T850\T850\bin\x64\Release
.\DayScene.exe --scene 4 --sceneFile Scenes/DayScene.t8scene
```

Use resource-relative paths (`Models/...`, `Scenes/...`). Do not hardcode repository-absolute paths into runtime source or authored scenes.

## Related Documents

- [Windows setup, build, and run](windows-build-and-run.md)
- [Runtime hosts and scene selection](../runtime/runtime-hosts.md)
- [Diagnostics](../debug/diagnostics.md)
- [Visual regression](../debug/visual-regression.md)
