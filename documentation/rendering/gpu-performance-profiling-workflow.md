# Windows GPU Performance Profiling Workflow

Status: verified against the timestamp, offline throughput, WPR/ETW, PresentMon,
and shader-cost scripts on 2026-09-22. ARM64 execution is verified on
`WSSICCLROM04`; x64 execution is verified on `CAMUSSTRIX`.

## Purpose

This workflow reproduces the DayScene GPU investigation on either Windows x64
or Windows ARM64. It produces five different classes of evidence:

1. timestamp-derived whole-frame and render-graph pass GPU durations;
2. queue-drained completed throughput with no presentation;
3. WPR/ETW GPU scheduling traces and per-process GPU Engine utilization;
4. cold/warm shader compilation and scene startup costs;
5. Microsoft Edge WebGPU queue-drained throughput and capability evidence;
6. matched native and browser non-wait CPU overhead.

These metrics answer different questions and must not be substituted for each
other.

| Metric | Measures | Does not measure |
|---|---|---|
| GPU timestamp | Elapsed GPU queue time between query writes | CPU recording, startup, display latency |
| Completed throughput | CPU recording/submission plus GPU completion | Isolated GPU execution |
| GPU Engine counter | Scheduled engine occupancy/utilization | Per-pass elapsed duration |
| PresentMon | Presented-frame timing and GPU busy attribution | A no-present workload |
| Shader/startup telemetry | Compiler, translation, PSO, and startup wall time | Steady-state frame execution |
| Browser completed throughput | Browser, Dawn, CPU recording/submission, and final GPU drain | Isolated browser GPU execution or per-pass duration |
| Non-wait CPU | Frame CPU time minus GPU wait, surface acquire, and present scopes | GPU execution or cross-machine ISA causality |

## Authoritative files

- `.github/skills/t850-arm64-gpu-benchmark/scripts/Capture-Arm64GpuTimestampMatrix.ps1`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Capture-Arm64GpuOfflineMatrix.ps1`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Capture-Arm64ShaderCosts.ps1`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Capture-X64ShaderCompilationMatrix.ps1`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Run-WindowsGpuProfileSuite.ps1`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Analyze-Arm64GpuOfflineMatrix.ps1`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Generate-Arm64GpuOfflineReport.ps1`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Capture-EdgeGpuOfflineMatrix.mjs`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Capture-WindowsNativeCpuOverheadMatrix.ps1`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Capture-EdgeCpuOverheadMatrix.mjs`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Merge-WindowsGpuOfflineEvidence.ps1`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Analyze-CrossMachineGpuOverhead.ps1`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Analyze-CrossMachineCpuOverhead.mjs`
- `.github/skills/t850-arm64-gpu-benchmark/scripts/Generate-CrossMachineGpuReport.ps1`
- `T850/Framework/src/debug/GpuTimestampProfiler.cpp`
- `T850/Framework/src/scene/RenderGraph.cpp`

The script names retain `Arm64` for compatibility, but their capture logic is
architecture-neutral. Architecture and executable hashes are recorded in every
new result.

## Source change inventory

The implementation behind this workflow is deliberately opt-in and grouped by
ownership:

| Area | Source changes |
|---|---|
| Profiling core | `GpuTimestampProfiler.h/.cpp` adds bounded completion batches, region pairs, backend adapters, validation, and JSON output. |
| Driver integration | D3D12 uses fence-qualified readback; Vulkan uses submission serials, query availability, timestamp periods, and valid-bit masking; Dawn uses `TimestampQuery`, resolve/staging buffers, submitted-work completion, asynchronous mapping, and event processing. |
| Render graph | `RenderGraph::Execute` brackets every executed logical pass while preserving one whole-frame interval. |
| Benchmark control | DayScene adds held deterministic simulation, `SubmitNoPresent`, exact frame limits, final `WaitForGPU`, and browser result publication. |
| Runtime configuration | `--profileGpu*`, `--benchmarkHoldFrame`, `--benchmarkNoPresent`, and `--shaderFlow legacyHLSL` are parsed and validated for supported targets. |
| D3D12 shaders | Graphics and compute default to dynamically loaded DXC, SM6 DXIL, DXC reflection, and compiler/build-specific caches; explicit `legacyHLSL` retains FXC SM5 DXBC. |
| Build and tests | MSBuild/CMake expose default-OFF `T850_ENABLE_GPU_PROFILING`; self-tests cover configuration and completion-batch behavior; browser tests cover forwarding and capability rejection. |
| Reproduction | The benchmark skill contains native/Edge capture, ETW validation, shader-cost, CPU-overhead, merge, analysis, and HTML report tools. |

Normal builds and frame paths remain unchanged unless GPU profiling or the
no-present benchmark is explicitly selected.

## Shader compiler paths

Native D3D12 and Dawn now both use DXC/DXIL by default, but they do not compile
identical source or necessarily produce identical bytecode.

| Path | Current compiler | Output |
|---|---|---|
| Native D3D12 default | Direct engine HLSL through staged DXC | DXIL / highest supported SM6 profile up to 6.6 |
| Native D3D12 `legacyHLSL` | `D3DCompile`, `vs_5_0` / `ps_5_0` / `cs_5_0` | DXBC / Shader Model 5 |
| Dawn D3D12 | Tint WGSL-to-HLSL plus Dawn built DXC | DXIL / Shader Model 6 family |
| Strict SPIR-V source flow | glslang HLSL-to-SPIR-V, Tint SPIR-V-to-WGSL, then Dawn/Tint/DXC | DXIL |

The Dawn package is built with `DAWN_USE_BUILT_DXC`. In the pinned Dawn source,
`DawnPlatform::IsFeatureEnabled(kWebGPUUseDXC)` therefore returns true;
`PhysicalDeviceD3D12` defaults the `UseDXC` toggle on when Shader Model 6 is
supported; `ShaderModuleD3D12` then sends Tint-generated HLSL to
`IDxcCompiler3::Compile`. Dawn retains an FXC SM5.1 fallback for hardware below
Shader Model 6 or an explicitly disabled DXC toggle.

`D3DCompile` is the legacy FXC API even when `d3dcompiler_47.dll` comes from a
current Windows installation. It does not become DXC and remains available only
through `--shaderFlow legacyHLSL` for native D3D12.

A byte-for-byte native-versus-Dawn comparison still requires the same generated
HLSL, shader model, compiler options, entry points, binding layout, and
specialization. Native D3D12 compiles authored engine HLSL directly with
column-major packing; Dawn first lowers WGSL through Tint to generated HLSL and
uses its own remapping/layout options. Sharing DXC does not imply identical DXIL.

Local x64 acceptance after the switch:

- all 291 registered graphics/compute permutations compile under DXC SM 6.6;
- all 291 also compile under explicit `legacyHLSL` SM5;
- cold graphics, warm DXIL/reflection cache restore, standalone compute, and all
  five production compute-selected passes exit cleanly;
- repeated DXC captures are byte-identical;
- DXC versus legacy final images differ at 16 of 230,400 pixels, maximum channel
  delta 4; tolerance 2 leaves six pixels. This is a stable compiler-codegen
  difference, not exact visual parity, and is retained in local evidence.

Native ARM64 acceptance on `.193` after local validation:

- ARM64 Release `Framework;DayScene` build passed;
- default DXC compute self-test passed at SM 6.6/DXIL;
- real DayScene graphics plus five compute-selected passes completed with zero
  renderer/device errors;
- all 291 registered permutations compiled successfully under ARM64 DXC;
- explicit `legacyHLSL` compute fallback passed at `cs_5_0`/DXBC.

Accepted evidence is under
`%LOCALAPPDATA%\T850Profiles\dxc-arm64-20260922\accepted`; result SHA-256 is
`01D0EF837EBFD12AE88E21068AF3678711E077B746E79AE174BE7F75EA4DDA36`.

Verified x64 host compiler fingerprints:

| Runtime | Version | SHA-256 |
|---|---|---|
| `C:\Windows\System32\d3dcompiler_47.dll` | `10.0.26100.8875` | `4A9B93F6ED20CCF294310FD73B47ECAC3DAE5938FD013A8D7B076F3BFB236236` |
| staged `dxcompiler.dll` | `1.9.2602.17 (21d28f727)` | `B86A738ECE4C05DBE2D9BBB29668A2CCB28A0740773C9B027CDC61E8D07B4D75` |
| staged `dxil.dll` | `1.9.2602.17 (21d28f727)` | `058F2F52A680C38B223A5615B7DF969DEF21F77E577562E5099D971DEDE992DE` |

Record these values again on another machine; do not assume Windows servicing
or the staged Dawn package is identical.

## Profile individual shader preparation stages on x64

The aggregate cold/warm shader-cost capture answers startup and cache questions,
but it cannot identify the slowest shader or compare compute and pixel stages.
For that question, build x64 Release and run:

```powershell
& .\.github\skills\t850-arm64-gpu-benchmark\scripts\Capture-X64ShaderCompilationMatrix.ps1 `
  -RuntimeRoot .\T850\bin\x64\Release `
  -ExpectedExecutableSha256 $expectedExecutableSha256 `
  -Repetitions 5
```

The harness removes `Shaders/.t8shadercache` before every launch and alternates
the three flow orders. Every run must compile the same 281 vertex, 281 pixel,
and 9 compute stages from `shader_permutations.json`. Intra-launch WebGPU cache
hits are retained as diagnostics but excluded from compiler statistics.

The structured event boundary is:

| Flow | Per-stage duration |
|---|---|
| Native D3D12 | DXC compile plus DXC reflection |
| WebGPU strict WGSL | WGSL preparation/reflection and synchronous `CreateShaderModule` |
| WebGPU strict SPIR-V | HLSL -> SPIR-V -> Tint/WGSL preparation/reflection and synchronous `CreateShaderModule` |

For WebGPU, Dawn backend compilation performed during graphics/compute pipeline
creation is outside this metric. Cross-flow percentages compare host-side shader
preparation paths, not compiler speed. Pipeline creation, source loading,
disk-cache writes, process startup, and whole-corpus wall time are reported
separately or excluded. Report arithmetic mean, median, p95, maximum, and exact
maximum shader/key for each flow/stage.
Also report compute versus pixel mean and median, with the explicit caveat that
the compute corpus has only nine distinct samples and different shader source;
the ratio is descriptive, not a controlled causal stage comparison.

Accepted evidence must use a clean source tree and pass
`-ExpectedExecutableSha256`. The result records the source revision and dirty
state. `-AllowDirtySource` exists only for diagnostic runs whose values will not
be published.

The retained 2026-09-22 matrix predates the clean-source enforcement and was
captured from an explicitly hashed local binary. Keep it as historical report
evidence, but rerun from a clean committed worktree before using its percentages
as a release gate. Its WebGPU values also include the earlier broader attempt
interval; future captures use preparation time that excludes cache writes.

## Fixed workload

The canonical six cells are:

| Cell | API | Shader source flow | Post-processing |
|---|---|---|---|
| `d3d12-raster` | Native D3D12 | Native HLSL | Raster |
| `d3d12-compute` | Native D3D12 | Native HLSL | Compute alternatives |
| `wgsl-raster` | Dawn/D3D12 | Strict authored WGSL | Raster |
| `wgsl-compute` | Dawn/D3D12 | Strict authored WGSL | Compute alternatives |
| `spirv-raster` | Dawn/D3D12 | HLSL -> SPIR-V -> WGSL | Raster |
| `spirv-compute` | Dawn/D3D12 | HLSL -> SPIR-V -> WGSL | Compute alternatives |

Fixed inputs:

- DayScene scene 1;
- 1920x1080;
- full culling/load mode;
- fixed simulation delta `0.0166666667` until frame 3000;
- simulation and physics held at frame 3000;
- offscreen rendering with `SubmitNoPresent`;
- no DWM or swapchain presentation in measured runs.

Raster has 248 draws, 642,171 indices, and 24 graph passes. Compute has 243
draws, 642,141 indices, and 24 graph passes because five fullscreen draws become
compute dispatches. Compare source-flow peers only when these counters match.

## Roots and prerequisites

Repository root:

```text
D:\Code\QwenFlashT850\T850
```

Source/build root:

```text
D:\Code\QwenFlashT850\T850\T850
```

Required software:

- Visual Studio 2022 with the target MSVC architecture;
- PowerShell 5.1 or newer;
- Windows Performance Recorder (`wpr.exe`);
- Windows Performance Toolkit (`xperf.exe`);
- PresentMon matching the operating-system architecture;
- Microsoft Edge and Node.js 22 or newer for the dependency-free CDP browser capture;
- an interactive desktop session for native GPU execution.

Architecture-specific build rules:

- x64: Build Tools or Community MSBuild can build `Framework;DayScene`.
- ARM64: use an MSBuild installation containing the ARM64 MSVC target. On the
  verified development machine this is Visual Studio Community.
- The executable architecture must match the target operating system. Do not
  compare an emulated x64 executable against a native ARM64 executable.

Collectors used by the verified runs:

| Architecture | Collector | SHA-256 |
|---|---|---|
| x64 | Official PresentMon 2.6.0 x64 | `B2A706BC6AD475749E3B7E3409263AA1E6906D45BDCF993F6DBC0F660188F1AF` |
| ARM64 | Native developer build from PresentMon PR #666, commit `503fc8ec` | `F6A44EEBB392F32BF827E0578C67434CD1D9C3D59BE898F60E549F78BEC09093` |

The ARM64 collector is unsigned and must be disclosed. The signed x64 collector
previously produced no rows for native ARM64 applications.

## Build the profiling executable

From the source root:

```powershell
Set-Location D:\Code\QwenFlashT850\T850\T850

$msbuildX64 = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuildX64 .\T850.sln `
  '/t:Framework;DayScene' `
  '/p:Configuration=Release' `
  '/p:Platform=x64' `
  '/p:T850EnableGpuProfiling=1' `
  '/m:8' '/v:minimal'
```

ARM64 variant:

```powershell
$msbuildArm64 = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
& $msbuildArm64 .\T850.sln `
  '/t:Framework;DayScene' `
  '/p:Configuration=Release' `
  '/p:Platform=ARM64' `
  '/p:T850EnableGpuProfiling=1' `
  '/m:8' '/v:minimal'
```

Require exit code 0 and record the SHA-256 of `DayScene.exe`.

Normal production builds leave `T850EnableGpuProfiling=0`. A default-OFF binary
must reject `--profileGpu` before asset loading with a diagnostic naming
`T850_ENABLE_GPU_PROFILING=1`.

## Stage one evidence root

All three capture scripts expect this layout:

```text
<root>/
  runtime/DayScene.exe
  runtime/Shaders/
  runtime/Models/
  runtime/Scenes/
  runtime/Textures/
  runtime/Fonts/
  runtime/Layouts/
```

For a local run, a junction to the build output is sufficient:

```powershell
$stamp = Get-Date -Format yyyyMMdd-HHmmss
$root = Join-Path $env:LOCALAPPDATA "T850Profiles\gpu-suite-$stamp"
New-Item $root -ItemType Directory | Out-Null
New-Item (Join-Path $root runtime) -ItemType Junction `
  -Target D:\Code\QwenFlashT850\T850\T850\bin\x64\Release | Out-Null
```

The build output already contains asset junctions. Cold shader tests delete
`runtime/Shaders/.t8shadercache`; with this local junction that is the source
asset cache. Do not run another engine process concurrently.

For a remote run, copy files rather than copying junction objects. Include the
runtime DLLs, all authored shader source files and `shader_permutations.json`,
DayScene scene/graph files, Sponza and SkyBox models, environment/LUT/lens
textures, fonts, and layouts. Exclude PDBs, editor binaries, unrelated scenes,
and `Shaders/.t8shadercache`.

After copying, compare the local and remote `DayScene.exe` SHA-256 values.

## Download the x64 PresentMon collector

```powershell
$collector = Join-Path $env:LOCALAPPDATA 'T850Profiles\tools\PresentMon-2.6.0-x64.exe'
New-Item (Split-Path $collector) -ItemType Directory -Force | Out-Null
Invoke-WebRequest `
  'https://github.com/GameTechDev/PresentMon/releases/download/v2.6.0/PresentMon-2.6.0-x64.exe' `
  -OutFile $collector

if ((Get-FileHash $collector).Hash -ne
    'B2A706BC6AD475749E3B7E3409263AA1E6906D45BDCF993F6DBC0F660188F1AF') {
  throw 'PresentMon hash mismatch'
}
```

Do not silently replace the collector with a different version. Record its path,
version, hash, and architecture in the evidence.

## Preflight

Before any suite:

```powershell
if (Get-Process DayScene, PresentMon -ErrorAction SilentlyContinue) {
  throw 'Renderer or collector is already active'
}
if (@(logman query -ets 2>&1 | Where-Object { $_ -match '^T850-' }).Count) {
  throw 'A T850 ETW session is already active'
}
if ((wpr -status 2>&1 | Out-String) -notmatch 'WPR is not recording') {
  throw 'WPR is already recording'
}
Get-Command wpr.exe, xperf.exe -ErrorAction Stop
```

Also record:

```powershell
Get-CimInstance Win32_VideoController |
  Select-Object Name, DriverVersion, AdapterRAM
$env:PROCESSOR_ARCHITECTURE
$env:COMPUTERNAME
powercfg /getactivescheme
Get-CimInstance Win32_Battery |
  Select-Object BatteryStatus, EstimatedChargeRemaining
```

Do not change the user's power plan implicitly. Laptop GPU clocks, mux mode,
thermal policy, and AC/battery state can change both absolute time and backend
deltas; record them and rerun under another policy only with explicit approval.

Stop rather than weakening acceptance when the requested API, timestamp feature,
or target architecture is unavailable.

## Run the complete suite with one command

For a fresh root, the canonical wrapper runs all phases sequentially and writes
`suite-result.json` only after timestamp, throughput/trace, and shader/startup
results exist:

```powershell
$suite = '.\.github\skills\t850-arm64-gpu-benchmark\scripts\Run-WindowsGpuProfileSuite.ps1'
& $suite `
  -Root $root `
  -CollectorPath $collector `
  -ExpectedCollectorSha256 'B2A706BC6AD475749E3B7E3409263AA1E6906D45BDCF993F6DBC0F660188F1AF' `
  -TimestampRepetitions 1 `
  -TimestampSamples 120 `
  -ThroughputRepetitions 5 `
  -MeasuredFrames 600 `
  -TraceFrames 6000 `
  -HoldFrame 3000 `
  -Width 1920 `
  -Height 1080
```

Use the ARM64 collector path/hash on ARM64. Add `-SkipWpr` only after a WPR
preflight is explicitly classified as privilege-blocked; the manifest then
records `skipWpr=true`.

The following sections document each phase independently for reruns and failure
diagnosis.

## Run the timestamp pass matrix

From the repository root:

```powershell
$skillScripts = '.\.github\skills\t850-arm64-gpu-benchmark\scripts'
& "$skillScripts\Capture-Arm64GpuTimestampMatrix.ps1" `
  -Root $root `
  -Repetitions 1 `
  -Samples 120 `
  -HoldFrame 3000 `
  -Width 1920 `
  -Height 1080
```

For a final statistical study, use `-Repetitions 5 -Samples 600` and alternate
cell order, but preserve the 1x120 matrix when comparing with the 2026-09-22
ARM64 pass result.

The command adds `--profileGpuPasses render-graph`. Each measured frame contains:

- one `gpu.frame` start/end timestamp pair;
- one start/end pair around every executed `RenderGraph::ExecutePass` node;
- asynchronous, completion-qualified query resolution.

The named pass scope is the primary scene frame command list/command buffer.
Separate initialization, upload, standalone compute, and one-shot copy
submissions are not named graph rows. Commands recorded outside a graph node but
inside the submitted frame appear only in `gpu.frame`.

Dawn requires an active render pass to close before encoder-level
`WriteTimestamp`. Pass-profile mode therefore changes Dawn physical pass
boundaries. Compare Dawn cells within the same instrumented mode; do not combine
pass-profile samples with uninstrumented production timings.

Acceptance:

- exactly six cells;
- exactly the requested frame count per cell;
- identical region set on every frame in one cell;
- one frame region plus every executed graph node;
- finite, nonnegative samples;
- zero dropped and failed frame batches;
- `timestamp-query requested=1 active=1` for Dawn;
- no validation, device-loss, or queue errors.

An exact zero is valid for an executed graph node with no GPU commands, such as
an empty transparent pass. Negative, missing, or inconsistent regions fail.

Output:

```text
<root>/gpu-timestamp-evidence/result.json
<root>/gpu-timestamp-evidence/<cell>-r<repeat>/gpu-profile.json
```

## Run no-present throughput and ETW traces

```powershell
& "$skillScripts\Capture-Arm64GpuOfflineMatrix.ps1" `
  -Root $root `
  -Repetitions 5 `
  -MeasuredFrames 600 `
  -TraceFrames 6000 `
  -CollectorPath $collector `
  -ExpectedCollectorSha256 'B2A706BC6AD475749E3B7E3409263AA1E6906D45BDCF993F6DBC0F660188F1AF'
```

For native ARM64, pass the PR #666 collector and its ARM64 hash instead.

The script performs three jobs.

### Superseded presented-frame experiment

The first ARM64 experiment used PresentMon GPU-busy fields around presented
frames. It was rejected as an uncapped comparison because
`--regressionFixedDt 1/60` slept before the hold-state check and paced the run
near 60 Hz. The engine was already presenting with sync interval zero and
tearing enabled; presentation flags were not the root cause.

After hold pacing was corrected, the accepted methodology removed presentation
entirely with `--benchmarkNoPresent`. PresentMon cannot provide rows for a true
no-present workload, so it is now used only to prove zero presents. Historical
PresentMon GPU-busy numbers are not canonical GPU execution measurements and
must not be mixed with timestamp results.

### PresentMon zero-present sanity

It attaches PresentMon to one D3D12 and one Dawn process while each runs
`SubmitNoPresent`. It requires zero rows for the DayScene PID. PresentMon is not
used as the performance metric for these runs because no presentation occurs.
Its role is to prove that the supposedly offscreen path did not reach the
swapchain. A valid no-present session may produce no CSV at all, so the harness
requires the collector process to remain alive until explicit session
termination; a collector that exits during startup or before stop fails.

To rerun only this proof after collector-hardening or packaging changes, use a
fresh evidence directory without repeating throughput or traces:

```powershell
& "$skillScripts\Capture-Arm64GpuOfflineMatrix.ps1" `
  -Root $root `
  -EvidenceDirectory (Join-Path $root 'zero-present-verification') `
  -ZeroPresentOnly `
  -CollectorPath $collector `
  -ExpectedCollectorSha256 '<architecture-specific hash>'
```

### Five repeated completed-throughput runs

For each cell, five independent launches render 600 held frames. Timing starts
at the held state and ends only after `WaitForGPU`. The metric therefore includes
CPU command recording, submission, queueing, and GPU completion. It is not pure
GPU execution time.

Odd repetitions use forward cell order and even repetitions reverse it. This
reduces monotonic temperature and clock-order bias.

### WPR GPU/ETW traces and busyness

For each cell, one 6,000-frame held run executes with:

```powershell
wpr.exe -start GPU -filemode
# Run held no-present workload and sample counters.
wpr.exe -stop <cell>\gpu.etl 'T850 offline GPU trace'
```

The built-in `GPU` profile captures Windows GPU scheduler, context, packet, and
engine activity. It does not provide the logical render-pass names; timestamp
queries provide those.

The ETLs were retained for WPA/xperf inspection of queue scheduling, engine
assignment, process/context activity, and trace integrity. The report's numeric
3D-engine busyness values were not derived by post-processing ETW events; they
came from the per-process `GPU Engine` performance counters sampled concurrently
with each WPR run. Keep those evidence sources distinct.

While the process is active, the script samples:

```powershell
Get-Counter '\GPU Engine(*)\Utilization Percentage' -MaxSamples 1
```

It keeps instances containing the DayScene PID and groups `engtype_3D` samples
by timestamp. Values for that PID and engine type are summed across GPU nodes,
then mean/p95/min/max are calculated. This is scheduled 3D-engine utilization,
not per-pass duration and not display FPS.

On the Qualcomm target, compute dispatches ran on the unified 3D engine and the
separate compute engine remained at zero. Never assume another adapter has the
same engine topology; inspect all instance names first.

Every ETL is checked with:

```powershell
xperf.exe -i <cell>\gpu.etl -a tracestats
```

Require both parsed lost-buffer and lost-event counts to be zero. A lossy ETL is
rejected, not averaged with accepted traces.

If WPR is privilege-blocked, `-SkipWpr` retains the same 6,000-frame runs and
per-process GPU Engine counters but marks ETL unavailable. This is a degraded
suite, not full ETW acceptance. Rerun without `-SkipWpr` from an already elevated
interactive shell; do not trigger a UAC prompt from automation.

The verified non-elevated x64 shell failed `wpr -start GPU -filemode` with
`0xc5585011`, `Failed to enable the policy to profile system performance`.
Classify that result as environment-blocked ETW, not a renderer failure and not
a passing trace gate.

To recover ETW from a separate elevated interactive task without repeating the
five-run throughput matrix, use a fresh root containing the same runtime and:

```powershell
& "$skillScripts\Capture-Arm64GpuOfflineMatrix.ps1" `
  -Root $traceRoot `
  -TraceOnly `
  -TraceFrames 6000 `
  -CollectorPath $collector `
  -ExpectedCollectorSha256 '<architecture-specific hash>'
```

`-TraceOnly` runs the six sustained counter/WPR cells and skips PresentMon
sanity and repeated throughput. Pass its `offline-evidence` directory as
`-TraceEvidence` when analyzing the primary result. The executable hashes in
both roots must match.

If the capture host has WPR but no `xperf.exe`, add `-DeferEtlValidation` to the
trace-only run. Copy every ETL to a machine with the Windows Performance
Toolkit; `Analyze-Arm64GpuOfflineMatrix.ps1` then runs `xperf tracestats`
locally and rejects any missing or nonzero loss counters. Do not label the raw
deferred capture accepted before this analysis succeeds.

When a completed throughput phase was stopped only because remote xperf was
missing after WPR wrote its first ETL, preserve that raw result and recapture
traces separately. Use `Merge-WindowsGpuOfflineEvidence.ps1` to combine them;
the merger requires the expected xperf-only failure, all 30 throughput runs,
six deferred trace cells, matching machine/architecture/executable/collector
identity, exact frame counts, and zero presents.

Outputs:

```text
<root>/offline-evidence/result.json
<root>/offline-evidence/runs/<cell>-r<repeat>/
<root>/offline-evidence/traces/<cell>/gpu.etl
<root>/offline-evidence/traces/<cell>/gpu-engine-counters.json
<root>/offline-evidence/traces/<cell>/xperf-tracestats.txt
```

## Run shader compilation and startup costs

```powershell
& "$skillScripts\Capture-Arm64ShaderCosts.ps1" -Root $root
```

Compile-all cases:

- native D3D12 HLSL;
- strict WGSL;
- HLSL -> SPIR-V -> WGSL.

For each case:

1. cold removes `Shaders/.t8shadercache`;
2. the selected path compiles every registered permutation;
3. warm repeats immediately without clearing the cache.

Startup repeats cold/warm for all six raster/compute cells and captures the first
runtime frame with CPU-only telemetry. It records source preparation,
translation, graphics/compute pipeline creation, cache counters, and wall time.

Do not run shader-cost capture concurrently with another process using the same
runtime cache.

Output:

```text
<root>/shader-cost-evidence/result.json
```

## Run Microsoft Edge WebGPU throughput

Browser per-pass timestamps are a capability-gated cell. The verified Edge
build advertises the adapter feature `timestamp-query`, but
`GPUCommandEncoder.prototype.writeTimestamp` is absent. Emscripten fails on the
first query write if the feature string alone is trusted. The shell therefore
rejects browser GPU timestamp profiling before startup. Mark browser timestamp
cells `capability-blocked`; do not substitute CPU time or FPS in the timestamp
table.

Browser completed throughput remains a separate valid metric. Build the Wasm
DayScene after native captures finish, serve it with `web/server.mjs`, and start
Edge with a fresh user-data directory and a CDP port. Include
`--disable-background-timer-throttling`, `--disable-renderer-backgrounding`,
`--disable-backgrounding-occluded-windows`, and
`--disable-features=CalculateNativeWinOcclusion`; the capture script also
activates each CDP target. Without these controls, a CDP-created tab can warm up
at only a few frames per second. Then run:

The browser canvas is expected to remain black or stale during these runs.
Measured frames render into offscreen targets and complete with
`SubmitNoPresent`; no final image is copied to or presented on the visible
canvas. This is the same zero-present contract validated by the result manifest,
not evidence that GPU work was skipped. Perform visual validation in a separate
normal run without `benchmarkNoPresent` or `profileGpu`.

```powershell
node "$skillScripts\Capture-EdgeGpuOfflineMatrix.mjs" `
  --endpoint http://127.0.0.1:9222 `
  --base-url http://127.0.0.1:8876/ `
  --output (Join-Path $root 'browser-offline-evidence\result.json') `
  --machine $env:COMPUTERNAME `
  --architecture $env:PROCESSOR_ARCHITECTURE `
  --repetitions 5 `
  --frames 600 `
  --hold-frame 3000 `
  --width 1920 `
  --height 1080
```

The script uses dependency-free CDP, creates a fresh browser context for every
run, alternates raster/compute order, and probes Edge version, adapter info, the
advertised timestamp feature, and the actual encoder method once on a separate
lightweight page. CDP device metrics include the shell's fixed 36-pixel header;
every result is rejected unless the engine publishes an exact 1920x1080 render
size. Timing starts at held frame 3000 and ends after the engine's final queue
drain. It is browser/Dawn end-to-end throughput, not pure GPU execution.

For a remote interactive Edge, keep the page on remote localhost so WebGPU has
a secure-context exception. Use SSH reverse forwarding from remote
`127.0.0.1:8876` to the local server and local forwarding from a spare local
port to remote CDP. Do not load the page from a plain LAN HTTP origin.

## Analyze and generate a report

```powershell
$analysis = Join-Path $root 'analysis.json'
& "$skillScripts\Analyze-Arm64GpuOfflineMatrix.ps1" `
  -GpuEvidence (Join-Path $root 'offline-evidence') `
  -ShaderEvidence (Join-Path $root 'shader-cost-evidence') `
  -TraceEvidence (Join-Path $traceRoot 'offline-evidence') `
  -OutputPath $analysis

& "$skillScripts\Generate-Arm64GpuOfflineReport.ps1" `
  -AnalysisPath $analysis `
  -TimestampPath (Join-Path $root 'gpu-timestamp-evidence\result.json') `
  -ReportPath (Join-Path $root 'report.html') `
  -ChartDirectory (Join-Path $root 'charts')
```

Omit `-TraceEvidence` when the primary offline result already contains accepted
ETLs. To normalize x64 and ARM64 independently and include optional Edge rows:

```powershell
& "$skillScripts\Analyze-CrossMachineGpuOverhead.ps1" `
  -X64TimestampPath $x64Timestamp `
  -Arm64TimestampPath $arm64Timestamp `
  -X64OfflineAnalysisPath $x64Analysis `
  -Arm64OfflineAnalysisPath $arm64Analysis `
  -X64BrowserPath $x64Browser `
  -Arm64BrowserPath $arm64Browser `
  -OutputPath $crossAnalysis

& "$skillScripts\Generate-CrossMachineGpuReport.ps1" `
  -AnalysisPath $crossAnalysis `
  -CpuAnalysisPath $crossCpuAnalysis `
  -ShaderCompilationPath $x64ShaderCompilation `
  -ReportPath $crossReport
```

The cross-machine output reports Dawn overhead relative to that machine's
native D3D12, compute overhead relative to that flow's raster result, matched
CPU overhead when supplied, and the x64 per-stage compilation matrix when
supplied. It does not treat absolute x64-versus-ARM64 time as CPU-architecture
causality.

The generator name and current report title retain ARM64 history. The JSON
artifacts contain the authoritative machine and architecture identity; do not
relabel one architecture's result as another.

`Run-WindowsGpuProfileSuite.ps1` writes `suite-result.json` with the machine,
architecture, executable SHA-256, collector SHA-256, WPR availability, and the
SHA-256 of each phase result. Require the executable hash in timestamp,
throughput, shader, and suite manifests to match before comparing or packaging
the evidence.

For cross-machine comparisons, compare:

- frame and pass medians within the same profiling granularity;
- compute-minus-raster deltas within one API/source flow;
- Dawn-minus-native deltas for the same mode;
- matched work counters;
- GPU engine topology and utilization;
- compiler and driver identity;
- run spread and repetition count.

Absolute times across different GPUs are expected to differ. The useful
behavioral comparison is which passes dominate, whether compute/raster direction
matches, whether source flows remain close, and whether one backend-specific
pass changes rank.

An x64-versus-ARM64 machine comparison is not a causal CPU-ISA experiment when
the GPU, driver, thermal envelope, memory system, and operating environment also
change. Report it as a machine/platform behavior delta. Isolating CPU
architecture would require the same GPU/driver and matched power/clock controls;
isolating GPU backend behavior requires repeated runs on one machine.

## Remote execution

SSH-launched graphics processes can run outside the interactive desktop and are
not accepted. Deploy the staged root, then register an interactive scheduled
task for the currently logged-in desktop user.

Discover the session:

```powershell
quser
Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" |
  ForEach-Object {
    $owner = Invoke-CimMethod -InputObject $_ -MethodName GetOwner
    [pscustomobject]@{ Session=$_.SessionId; User="$($owner.Domain)\$($owner.User)" }
  }
```

Create the task from an elevated administrative session:

```powershell
$desktopUser = 'DOMAIN\user'
$action = New-ScheduledTaskAction `
  -Execute 'powershell.exe' `
  -Argument '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "C:\capture\Run-WindowsGpuProfileSuite.ps1" -Root "C:\capture" -CollectorPath "C:\capture\tools\PresentMon.exe" -ExpectedCollectorSha256 "<SHA256>"' `
  -WorkingDirectory 'C:\capture\runtime'
$principal = New-ScheduledTaskPrincipal `
  -UserId $desktopUser -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet `
  -ExecutionTimeLimit (New-TimeSpan -Hours 2) `
  -MultipleInstances IgnoreNew `
  -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName 'T850-GPU-Suite' `
  -Action $action -Principal $principal -Settings $settings
Start-ScheduledTask -TaskName 'T850-GPU-Suite'
```

The task script should run the three canonical capture commands sequentially and
write `result.json` last for each phase. Copy evidence locally and verify hashes
before deleting the remote root.

## Interpretation rules

1. Timestamp JSON is canonical for GPU execution duration.
2. Completed throughput is end-to-end backend throughput, not GPU time.
3. PresentMon is a presentation oracle and zero-present sanity check here.
4. GPU Engine utilization establishes scheduling/saturation, not pass cost.
5. Pass medians do not sum exactly to the whole-frame median because medians are
   independent and frame time includes barriers, resolves, and commands outside
   graph nodes.
6. A faster Dawn result does not mean WebGPU is generally faster than D3D12.
   Dawn itself uses D3D12 on Windows; compiler, barriers, pass construction,
   bindings, and engine backend quality differ.
7. A 1-3% cross-run delta requires repeated alternating runs before a causal
   conclusion. Clock, thermal, and DVFS state can be that large.

## Success criteria

The complete suite passes only when:

- every process exits 0;
- all six cells are present in every phase;
- executable, collector, architecture, and machine identity are recorded;
- frame 3000 is the timestamp/throughput workload state;
- Dawn reports provider Dawn and backend D3D12;
- work counters match source-flow peers;
- timestamp batches have zero drops/failures and stable pass sets;
- no-present completion markers report `presents=0`;
- PresentMon zero-present sanity has zero process rows;
- Microsoft Edge browser throughput has five raster and five compute runs, exact
  1920x1080 engine render size, 600 frames per run, and `presents=0`;
- browser timestamp status records both the adapter feature and the actual
  `GPUCommandEncoder.writeTimestamp` method;
- all six ETLs have zero lost buffers/events;
- logs contain no renderer, validation, device-loss, or queue errors;
- cold/warm shader runs use the intended cache sequence;
- evidence is copied and hash-verified before cleanup.

## Stop conditions

Stop and classify the run instead of weakening acceptance when:

- no interactive desktop is active;
- the executable or collector architecture is wrong;
- `TimestampQuery` is unavailable;
- an unrelated WPR or owned ETW session is active;
- a trace loses events or buffers;
- work counters differ unexpectedly;
- the renderer reaches presentation during a no-present run;
- a log reports validation, device loss, submission failure, or missing assets;
- query results are negative, missing, inconsistent, dropped, or failed;
- a cache cannot be cleared exclusively for a cold run.

## Cleanup

After evidence has been copied and verified:

```powershell
Get-ScheduledTask -TaskName 'T850-GPU-Suite' -ErrorAction SilentlyContinue |
  Unregister-ScheduledTask -Confirm:$false
Get-Process DayScene, PresentMon -ErrorAction SilentlyContinue
logman query -ets | Select-String '^T850-'
wpr -status
```

Delete only the disposable evidence runtime/root. Preserve unrelated browser,
user, and system processes. A local runtime junction can be removed without
removing its target:

```powershell
(Get-Item (Join-Path $root runtime)).Delete()
```

Retain raw JSON, ETL, counter, trace-stat, log, executable hash, collector hash,
and generated report artifacts outside Git unless a small summary is explicitly
intended for source control.

## Accepted post-DXC cross-machine record: 2026-09-22

The final matrix used five independent runs with 600 held samples/frames per
cell at 1920x1080 and frame 3000. Normalize each host to its own native D3D12
baseline; these machines have different GPUs, drivers, CPUs, and power behavior.
These timestamps were captured in render-graph pass mode. Dawn closes physical
passes at logical timestamp boundaries in that mode, so the table is accepted
pass-profile evidence but not yet an uninstrumented backend-overhead result.
Whole-frame-only and pass-mode perturbation measurements remain the closeout
gate for cross-backend headline claims.

Timestamp-derived whole-frame GPU medians:

| Host | D3D12 raster | WGSL raster | WGSL overhead | SPIR-V raster | SPIR-V overhead |
|---|---:|---:|---:|---:|---:|
| x64 RTX 4080 Laptop | 3.040 ms | 3.979 ms | +30.89% | 3.993 ms | +31.35% |
| ARM64 Adreno X1-85 | 14.536 ms | 15.532 ms | +6.85% | 15.925 ms | +9.56% |

Compute versus raster GPU time is +1.28% / -2.02% / +1.99% on x64 for
D3D12/WGSL/SPIR-V, and +6.18% / +9.70% / +7.41% on ARM64. The report contains
all 24 named pass rows. Largest WGSL-minus-native raster contributors include
Shadow Accumulation and GBuffer on x64, and DOF plus Shadow Accumulation on
ARM64.

Queue-drained completed throughput remains a different metric. Native WGSL
raster is +2.27% versus D3D12 on x64 and -3.72% on ARM64. Microsoft Edge WGSL
raster is +30.07% on x64 (6.572 ms/frame) and -7.11% on ARM64
(15.198 ms/frame). Browser compute versus raster is -7.89% on x64 and +5.02%
on ARM64. Every accepted browser run has 600 frames, exact 1920x1080 engine
render size, and zero presents.

Edge 153 x64/NVIDIA Lovelace and Edge 154 ARM64/Qualcomm Adreno both advertise
`timestamp-query` but lack `GPUCommandEncoder.writeTimestamp`; browser per-pass
timestamp cells are capability-blocked, not replaced with throughput. Both
hosts loaded browser runtime identity
`d34275873195b3bd801ab770003b72e4fb0202c13e19f78fbffef982d66787a5`.

The Saturday CPU-overhead methodology was rerun with the accepted DXC native
binaries and the same current Wasm runtime: compute post-processing, fixed
1/60 simulation step, 1,800 profiled frames, frames 300-1799 analyzed, and three
independent repetitions per backend. Non-wait CPU wall time subtracts explicit
GPU wait, surface-acquire, and present scopes from `cpuFrameMs`.

| Host | Native D3D12 | Native Dawn WGSL | Dawn overhead | Edge WebGPU | Edge overhead |
|---|---:|---:|---:|---:|---:|
| x64 | 0.855 ms | 1.470 ms | +72.02% | 3.215 ms | +276.13% |
| ARM64 | 2.053 ms | 2.581 ms | +25.68% | 3.586 ms | +74.65% |

All nine runs per host share the same frame-by-frame work hash, and both hosts
produce the same work hash
`ac00583c5ebe423af9dad9e48f631802354fc3407524119d7031a8e4aef8f3df`.
The principal native Dawn CPU tax is submission: +0.409 ms over D3D12 on x64
and +0.733 ms on ARM64. Dawn render-graph CPU adds 0.190 ms on x64 but is
0.086 ms lower on ARM64, so the ARM Dawn CPU delta is primarily submission.
Edge render-graph CPU adds 2.244 ms on x64 and 1.742 ms on ARM64.

CPU evidence manifests: x64 SHA-256
`15F7C0ADB2284EC667FC3D553927A58472B91166349F66F925F092836EA6B60E`;
ARM64 SHA-256
`34211D5B7E1BB369331B0C0A27E181DABA609F721D0C08256118310CDF7B3F69`.

ARM64 retained six WPR GPU ETLs, all validated with local x64 xperf at zero lost
buffers/events. x64 WPR remained privilege-blocked after direct WPR and
highest-run-level task attempts; its six sustained GPU Engine counter runs are
retained as the explicit degraded trace phase.

Final outputs:

- `D:\Code\QwenFlashT850\DayScene-x64-vs-ARM64-GPU-Report.html`
- `D:\Code\QwenFlashT850\DayScene-x64-vs-ARM64-GPU-Comparison.json`
- `D:\Code\QwenFlashT850\DayScene-x64-vs-ARM64-CPU-Comparison.json`
- `D:\Code\QwenFlashT850\DayScene-x64-Shader-Compilation-Matrix.json`
- `D:\Code\QwenFlashT850\DayScene-DXC-Cross-Machine-GPU-Report.zip`
- `D:\Code\QwenFlashT850\DayScene-DXC-Cross-Machine-GPU-Manifest.json`
- x64 manifest SHA-256 `E5AE8405578E16283E2A039EC8068A7AB7EC0F061DC7D63760B63FD0BBA4154C`;
- ARM64 manifest SHA-256 `E0B192909FD36B09DB2246A9269C8E9C8074C691E60F4BBC271B4C93A7C2EA1F`.

## Related documents

- [GPU timestamp profiling](gpu-timestamp-profiling.md)
- [Diagnostics and telemetry](../debug/diagnostics.md)
- [Render graph](render-graph.md)
- [Shader management](shader-management.md)
- [Verification](../testing/verification.md)
- [Windows build and run](../development/windows-build-and-run.md)
