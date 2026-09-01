# Runtime Configuration and CLI

Status: verified against `Config.h`, `ConfigRuntime.h/.cpp`, `DayScene/App.cpp`, and `Launcher.ps1` on 2026-08-19.

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

Unknown JSON keys are ignored. A typo can therefore be silent; use documented field names and inspect `[config]` warnings.

## Hardcoded Defaults

| Field | Default |
|---|---|
| API | `d3d11` |
| Window | 1280x720, windowed |
| Scene | 0 (Sandbox) |
| Model | `Models/DamagedHelmet.glb` |
| Log level | 3 (`verbose`) |
| Culling | `full` |
| Profile frames | 300 |
| Telemetry frequency | 60 frames |
| Telemetry output | `logs/perf_telemetry.json` |
| Benchmark/regression fixed delta | disabled (`0`) |

## JSON Shape

Root fields accepted by `RuntimeConfigJson` include:

```json
{
  "api": "d3d11",
  "width": 1280,
  "height": 720,
  "fullscreen": false,
  "scene": 0,
  "title": "T850 Project",
  "model": "Models/DamagedHelmet.glb",
  "sceneFile": "",
  "sceneProfile": "",
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
--orbitYaw RADIANS
--gui
```

Scene indices are 0 Sandbox, 1 Day, 2 Quake3Mock, 3 RagdollEditor, 4 SceneTemplate, and 5 VoxelScene.

### Dumps, Replay, and Diagnostics

```text
--dump-frame N
--dumpFrame N
--dumpSnapshot-frame N
--dumpSnapshot-seconds SECONDS
--debugFrames
--replaySnapshot PATH
--keepRunning
--dumpMatrices N
--culling full|lazy|disabled
--cullDisabled
--offscreen
--offscreenDebug
--glOffscreenFlushMode frame|wait|none
--dumpShaderPermutations
--shaderPermutationOutput PATH
```

`--debugFrames` enables manual dump requests. A normal dump exits unless `--keepRunning` is present.

`--regressionFixedDt SECONDS` uses a fixed update delta with real-time pacing and suppresses live input/random startup variation for unattended deterministic captures. Use it for regression tooling, not ordinary gameplay.

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
```

Benchmark matrix mode forces DayScene, D3D11 startup, 1920x1080, and onscreen start settings before the internal matrix runs.

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
