---
name: t850-arm64-gpu-benchmark
description: "Use when measuring T850 DayScene GPU performance on Windows x64 or ARM64, comparing native D3D12, native Dawn WGSL/SPIR-V, or Microsoft Edge WebGPU, comparing raster versus compute post-processing, collecting timestamp/PresentMon/WPR evidence, or measuring cold/warm shader compilation."
argument-hint: "State machine, architecture, GPU, scene, resolution, held frame, repetitions, shader flows, post-process modes, and requested trace/report outputs."
---

# T850 Windows GPU Benchmark

Use this workflow for matched GPU comparisons on Windows x64 or ARM64. It
measures a deterministic held scene state rather than averaging an animated
tour. The canonical end-to-end procedure is
[the Windows GPU profiling workflow](../../../documentation/rendering/gpu-performance-profiling-workflow.md).

## Fixed Contract

- Scene: DayScene, index 1, authored `Scenes/DayScene.t8scene`.
- Resolution: 1920x1080.
- Configuration: native Release matching the target OS architecture.
- Simulation: `--benchmarkFixedDt 0.0166666667` through runtime frame 3000.
- Hold: `--benchmarkHoldFrame 3000` sets simulation delta to zero, disables fixed-step wall-clock pacing, and keeps draw/present running uncapped.
- Primary GPU measurement: an explicit `T850_ENABLE_GPU_PROFILING=1` build,
	`--benchmarkNoPresent`, and timestamp-derived `gpu.frame` samples beginning at
	held frame 3000.
- Secondary throughput measurement: 600 identical held frames followed by
	`WaitForGPU`; this includes CPU recording/submission and is not GPU execution
	time.
- Repetitions: five independent launches per cell in alternating order.
- Trace measurement: one additional 6,000-frame no-present run per cell with WPR `GPU` and per-process GPU Engine counters.
- Browser measurement: Microsoft Edge WebGPU queue-drained no-present throughput;
	browser timestamp cells remain capability-gated and are never replaced by FPS.
- CPU measurement: three independent 1,800-frame compute-mode runs per native
	D3D12, native Dawn, and Edge backend; discard frames 0-299 and compare
	non-wait CPU time over frames 300-1799.

The six rendering cells are:

| API/source | Post-process |
|---|---|
| Native D3D12 | raster |
| Native D3D12 | compute |
| Dawn/D3D12 strict WGSL | raster |
| Dawn/D3D12 strict WGSL | compute |
| Dawn/D3D12 HLSL -> SPIR-V -> Tint/WGSL | raster |
| Dawn/D3D12 HLSL -> SPIR-V -> Tint/WGSL | compute |

Do not label `auto` as strict WGSL. Use `--shaderFlow wgsl` and `--shaderFlow spirv` explicitly. `--postProcessMode compute` switches the five authored `compute_if_supported` DayScene passes; it is not the Minecraft particle path.

## Prerequisites

1. Build `Framework;DayScene` Release with the matching x64 or ARM64 MSVC tools.
2. Stage the executable, runtime DLLs, authored shader sources, `shader_permutations.json`, DayScene scene files, Sponza/SkyBox GLBs, and environment textures.
3. Exclude `Shaders/.t8shadercache` from the package.
4. Use official PresentMon 2.6.0 x64 on x64. On ARM64 use native PR #666
	commit `503fc8ec5e867f07b94e12a667bc5e5430e79ffc`, SHA-256
	`F6A44EEBB392F32BF827E0578C67434CD1D9C3D59BE898F60E549F78BEC09093`;
	it is an unsigned developer collector and must be disclosed.
5. Require an interactive desktop, no DayScene/PresentMon process, no `T850-*` ETW session, and `WPR is not recording`.
6. Run captures sequentially in an elevated interactive scheduled task. Never capture cells concurrently.

## Capture

Deploy [Capture-Arm64GpuOfflineMatrix.ps1](./scripts/Capture-Arm64GpuOfflineMatrix.ps1) beside the staged runtime and invoke it from an elevated interactive scheduled task:

```powershell
.\Capture-Arm64GpuOfflineMatrix.ps1 -Root C:\T850-GPU-ARM64-YYYYMMDD
```

The harness:

1. verifies the runtime and an idle trace state;
2. attaches PresentMon to one D3D12 and one Dawn sanity run and requires zero process present rows;
3. runs five independent 600-frame, GPU-drained offline measurements per cell;
4. runs one 6,000-frame WPR GPU + GPU Engine counter trace per cell;
5. rejects missing completion markers, nonzero presents, wrong backend, renderer errors, or incomplete queue drain;
6. writes `result.json` last.

Run [Capture-Arm64ShaderCosts.ps1](./scripts/Capture-Arm64ShaderCosts.ps1)
separately for cold/warm compile-all and startup costs.

For timestamp-derived whole-frame GPU execution, deploy
[Capture-Arm64GpuTimestampMatrix.ps1](./scripts/Capture-Arm64GpuTimestampMatrix.ps1)
and run it from the same elevated interactive desktop:

```powershell
.\Capture-Arm64GpuTimestampMatrix.ps1 `
	-Root C:\T850-GPU-TIMESTAMP-YYYYMMDD `
	-Repetitions 5 -Samples 600 -HoldFrame 3000
```

The timestamp harness requires exact held-frame arming, positive finite samples,
zero drops/failures, correct Dawn/D3D12 identity, and active WebGPU
`TimestampQuery`. It requests `--profileGpuPasses render-graph`, records one
whole-frame pair plus one start/end query pair around every executed logical
render-graph node, and stores per-pass median/p95/min/max values. Exact zero is
valid for an executed empty node; negative or inconsistent region sets fail the
run. Its `gpu-profile.json` artifacts are canonical for GPU execution duration.

Pass rows cover scene work recorded through `RenderGraph::Execute` on the primary
frame command list/command buffer. Separate uploads, one-shot copies,
initialization, and standalone compute submissions are not named graph rows;
commands outside graph nodes remain visible only in the whole-frame interval.

Cold means the selected API cache under `Shaders/.t8shadercache` is absent before launch. Warm immediately repeats the same command without clearing it. Record wall time and telemetry shader compile/cache scopes separately.

For Microsoft Edge, first require both the adapter `timestamp-query` feature and
`GPUCommandEncoder.prototype.writeTimestamp`. If the method is absent, classify
browser timestamps as `capability-blocked` and stop those cells. Use
[Capture-EdgeGpuOfflineMatrix.mjs](./scripts/Capture-EdgeGpuOfflineMatrix.mjs)
for a separate five-by-600 browser throughput matrix. The script records the
server runtime identity, Edge/CDP version, adapter identity, feature string, and
actual method availability. Run remote Edge through an interactive desktop and
localhost SSH forwarding so the page remains a secure-context WebGPU origin.
Launch the owned Edge profile with background timer/renderer/native-occlusion
throttling disabled; the runner activates every CDP target and rejects any
engine render size other than 1920x1080.

An Edge benchmark canvas may remain black or stale by design: measured work is
submitted to offscreen targets with `SubmitNoPresent`. Use a separate normal
onscreen run for visual validation; do not add presentation to the measured
throughput path.

## Acceptance

For every primary run require:

- exit code 0;
- an offline start marker at simulation frame 3000;
- a completion marker with the exact frame count and `presents=0`;
- completion timing recorded only after `WaitForGPU`;
- PresentMon sanity runs with zero DayScene rows for D3D12 and Dawn;
- matching Dawn provider/backend for WebGPU;
- no engine error/device-loss/submit failure.

Compare useful work counters (`gpu.draws`, `gpu.indices`, `render.pass.count`) across source-flow peers. Raster and compute are intentionally different dispatch/draw strategies, but geometry work and scene state must match.

## Analysis

Use [Analyze-Arm64GpuOfflineMatrix.ps1](./scripts/Analyze-Arm64GpuOfflineMatrix.ps1) after copying GPU and shader evidence locally. Report per cell:

- median/min/max/CV of five GPU-drained completed-frame times and FPS;
- min/max and coefficient of variation;
- compute minus raster in milliseconds and percent within each API/source flow;
- WGSL and SPIR-V minus native D3D12 for the same post-process mode;
- 6,000-frame sustained completed throughput and GPU Engine utilization by engine type;
- compile-all cold/warm wall time and speedup;
- scene startup cold/warm first-runtime-frame time, shader compile scope, cache hit/miss counts.

Completed throughput includes CPU command recording/submission and GPU execution;
it is not a per-pass GPU timestamp. WPR/counters establish saturation and engine
assignment. Do not infer shader-stage execution cost from CPU telemetry.

For matched CPU overhead, run
[Capture-WindowsNativeCpuOverheadMatrix.ps1](./scripts/Capture-WindowsNativeCpuOverheadMatrix.ps1)
on each native host and
[Capture-EdgeCpuOverheadMatrix.mjs](./scripts/Capture-EdgeCpuOverheadMatrix.mjs)
for Edge. Analyze the two machine roots with
[Analyze-CrossMachineCpuOverhead.mjs](./scripts/Analyze-CrossMachineCpuOverhead.mjs).
Use compute post-processing, fixed `1/60` simulation, 1,800 profiled frames,
frames 300-1799 for analysis, and three independent repetitions per backend.
The non-wait CPU metric is `cpuFrameMs - gpu.gpu_wait -
webgpu.surface_acquire - gpu.present`. Require identical frame-by-frame work
hashes before comparing backends. Normalize each machine to its own native
D3D12 result; do not attribute absolute x64-versus-ARM64 differences to ISA.

If ETW must be recovered separately, invoke the offline collector with
`-TraceOnly` from an elevated interactive task and pass that result to the
analyzer as `-TraceEvidence`. Require the same executable SHA-256.
If xperf is unavailable on the capture host, add `-DeferEtlValidation`, copy all
ETLs to a WPT-equipped machine, and let the analyzer enforce zero lost
buffers/events. Use `Merge-WindowsGpuOfflineEvidence.ps1` only for a completed
throughput phase stopped solely by missing remote xperf; it strictly validates
phase shape and all machine, executable, and collector identities.

## Report

Generate a single-machine HTML with
[Generate-Arm64GpuOfflineReport.ps1](./scripts/Generate-Arm64GpuOfflineReport.ps1).
For x64/ARM64 comparison, use
[Analyze-CrossMachineGpuOverhead.ps1](./scripts/Analyze-CrossMachineGpuOverhead.ps1)
and [Generate-CrossMachineGpuReport.ps1](./scripts/Generate-CrossMachineGpuReport.ps1).
Normalize each machine to its own native D3D12 and each flow to its own raster
baseline. Do not treat absolute x64-versus-ARM64 time as CPU-ISA causality.
Retain raw JSON/ETL files outside Git and mark prior 60 Hz presented results as
superseded.

## Cleanup

Stop only owned processes/sessions/tasks. Verify zero DayScene/PresentMon processes, zero `T850-*` ETW sessions, and stopped WPR. Preserve unrelated Edge and user processes.
