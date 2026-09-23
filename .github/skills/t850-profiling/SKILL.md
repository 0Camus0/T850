---
name: t850-profiling
description: "Use when adding, removing, or interpreting T850 CPU scopes, telemetry counters, upload/streaming instrumentation, GPU timestamps, shader preparation timings, or external PresentMon, PIX and ETW evidence."
argument-hint: "Describe the subsystem to instrument, the bottleneck suspected, or the measurement question."
---

# T850 Profiling

Four systems exist and they are not interchangeable.

| System | Files | Purpose |
|---|---|---|
| `Profiler` | `Framework/include/debug/Profiler.h`, `src/debug/Profiler.cpp` | API-neutral named CPU/GPU scopes, draw counts, log report. Enabled with `--profile`. |
| `ProfilerGpuBackend` | `src/debug/ProfilerGpuBackend.cpp` | Legacy profiler timestamp strategy for D3D11, D3D12, OpenGL, and Vulkan. |
| `GpuTimestampProfiler` | `include/debug/GpuTimestampProfiler.h`, `src/debug/GpuTimestampProfiler.cpp` | Opt-in, completion-qualified whole-frame and render-graph-pass timestamps for D3D12, Vulkan, and native Dawn/WebGPU. |
| `RuntimeTelemetry` | `include/debug/RuntimeTelemetry.h`, `src/debug/RuntimeTelemetry.cpp` | Sampled per-frame scopes and numeric counters, JSON output. `T8_TELEMETRY_SCOPE`, `AddCounter`, `SetCounter`. |

The layering is correct: `Profiler` is API-neutral and the GPU strategy is
API-specific. Preserve that split.

The CPU/telemetry implementation, matched CPU matrix, no-present throughput,
native GPU timestamps, pass matrix, and x64 shader-preparation matrix are
implemented and have accepted 2026-09-22 evidence. The remaining GPU timestamp
closure gates are external correlation, instrumentation perturbation, and
pending-callback/device-loss stress. Do not describe those gates as complete.
Use focused Framework/DayScene builds and the owning benchmark skill for reruns.

## The two rules

**1. A scope marks a phase, not a call.** If the question is "how many", use a
counter. If the question is "how long did this stage take", use a marker at the
stage boundary. Entries scale with phases, simulation ticks and render-graph
passes, not draws, meshes, entities or queries. Do not impose a fixed count.

**2. Remove hot-path bookkeeping and measure the remaining cost.** Use worker-local
fixed-slot accumulation instead of per-call strings, locks and map lookups.
Neither timestamp reads nor counters are free; establish overhead with tests
and repeated matched runs rather than assuming a particular nanosecond cost.

## Do not

- Do not add default `T8_TELEMETRY_SCOPE` or profiler scopes to per-draw,
  per-mesh, per-entity, per-query or per-upload paths. Event-scoped work and
  repeated simulation ticks are separate phases, not exceptions for call tracing.
- Do not build a scope name by concatenation or `std::string` construction on a
  hot path.
- Do not add built-in per-draw or per-dispatch GPU timing. External tools own
  that; see the tool assignment below.
- Use `T8_TELEMETRY_ADD` / `T8_TELEMETRY_SET` for literal counters. They cache
  registered IDs and accumulate in thread-owned fixed slots. String overloads
  still register through the slow path; do not use them in loops.
- Do not instrument Dawn entry points to measure Dawn overhead. That perturbs
  the cost being measured. Use a matched workload plus an ETW sampling profile.

## Tool assignment

| Question | Instrument |
|---|---|
| Frame pacing, present latency, GPU busy versus wait | PresentMon over ETW, out of process |
| Whole-frame or logical render-graph-pass GPU execution time | Opt-in `GpuTimestampProfiler` |
| Per-draw GPU execution time | PIX, RenderDoc or Nsight, out of process |
| CPU cost per frame phase | Built-in markers |
| Texture and geometry upload volume, stalls, spikes | Built-in per-frame aggregates |
| Where CPU time goes inside Dawn versus D3D12 | ETW sampling profile with module attribution |
| Cold per-stage native DXC or WebGPU source-preparation cost | `Capture-X64ShaderCompilationMatrix.ps1`; do not call the WebGPU value backend compiler time because pipeline creation is excluded |
| Did two runs do the same work | Counters |

Upload volume is the one case where built-in instrumentation beats an external
tool: a GPU capture shows the copies but not their aggregate cost across a
frame, and it cannot be left running.

## Marker set

Scopes are engine-wide and must use identical names on all five backends.

| Phase | Markers |
|---|---|
| Simulation | `game.update`, `game.events.dispatch`, `game.components.pre_physics`, `game.components.logic`, `game.state_machines`, `game.agents.steer`, `game.agents.groups`, `camera.update` |
| Physics | `physics.jolt.update_total`, `physics.jolt.simulate`, `physics.queries`, `character.fps_update` |
| Navigation | `navigation.update`, `navigation.find_paths_batch`, `navigation.build`, `navigation.rebuild` |
| Streaming | `terrain.voxel.stream_update`, `terrain.voxel.mesh_build`, `terrain.voxel.upload`, `terrain.heightmap.commit` |
| Animation | `animation.update_and_upload`, `animation.bone_texture_upload` |
| Render | `render.cull`, `render.pass.<name>` |
| Driver, same names where the phase exists | `gpu.encode`, `gpu.submit`, `gpu.present`, `gpu.gpu_wait`, `gpu.upload_batch` |
| Assets, event-scoped | `asset.gltf.parse`, `asset.gltf.image_decode`, `asset.gltf.draco` |

Anything not on this list needs justification before it is added.

`T8_CPU_WORK` is the explicit fixed-slot sum for fragmented culling, animation,
camera, query, agent, upload-batch and worker-decode CPU work. It is labelled
`accumulatedWork` in reports,
not presented as phase wall time. Do not reintroduce per-draw/query event timers.
`navigation.rebuild` is a full rebuild; there is no separate incremental tile
update API to claim coverage for. The glTF decode batch elapsed time is separate
from summed worker decode time.

## Upload instrumentation

Use existing upload entry points, including initial-data resource creation.
Carry source provenance from the owning job, batch or descriptor rather than
guessing it from the low-level resource type:

| Path | Entry point |
|---|---|
| Dynamic vertex, index, constant buffers | `Buffer::UpdateFromBuffer`, `include/video/BaseDriver.h` |
| Dynamic textures | `Texture::UpdateFloatData`, `BaseDriver.h` |
| Bulk upload phases | `BaseDriver::BeginResourceUploadBatch` / `EndResourceUploadBatch` |
| Uniform ring | `m_cbRingOffset`, `m_cbRingPeakUsage` in `D3D12Driver.cpp`, `VulkanDriver.cpp` |
| Buffer pool | `webgpu.buffer_reuses`, `buffer_allocations`, `buffer_evictions` in `WebGPUContext.cpp` |

R5 uses two axes: `UploadResource` (`Vertex`, `Index`, `Uniform`, `Texture`)
and `UploadSource` (`PerFrame`, `Streaming`, `AssetLoad`). Each logical upload
belongs to one matrix cell. Record logical and staging bytes separately, plus
calls, CPU nanoseconds, reallocations and largest logical upload. Publish via
R4's worker-safe frame protocol, retaining issue-frame identity for late records.
Use a startup epoch outside render frames and report unavailable metrics
explicitly. Never add a wait, flush or readback solely to obtain a metric.

Ring peak/capacity and overflow are recorded on D3D12 and Vulkan; WebGPU records
pool hits/misses/evictions. Budget monitoring runs on unsampled frames too.
Flagged budget-only frames are excluded from timing percentiles. Late unsampled
history is bounded to 64 frames; detailed storage to 8192. Drops are explicit.
The thread pool and shared asset wrappers preserve explicit streaming provenance.

### Interpreting upload data

| Symptom | Likely cause |
|---|---|
| Bytes spike, `gpu.gpu_wait` spikes, CPU encode flat | Bandwidth or in-flight stall; check renames and double buffering |
| Bytes spike, CPU encode spikes, GPU wait flat | CPU-side packing or format conversion before upload |
| Bytes flat, reallocations rising | Ring or pool sized wrong |
| Largest single upload spikes on one frame | One oversized upload; split or stream it |

## Known defects

Check these before trusting a number:

- R1 now uses an explicit active stack, monotonic query slots, tokenized guards
  and independent CPU/GPU sample counts. Reset generations reject stale GPU
  results. Four shared regression tests cover this behavior; final gate status
  remains in the remediation plan rather than being inferred from these fixes.
- Profiler parent/ID lookup is indexed. Dynamic render-pass IDs are cached at
  graph load. The first registration/worker initialization is cold and can
  allocate/lock; warmed-up handle recording does not.
- Deferred GPU query reset is exposed as `FlushDeferredQueryReset`; backend
  details remain in the strategy. Inclusive report nodes must not be summed
  into frame time.
- The legacy `ProfilerGpuBackend` Vulkan resolve uses
  `VK_QUERY_RESULT_WAIT_BIT`; the opt-in `GpuTimestampProfiler` uses completion
  serials and nonblocking query reads instead.
- `GpuTimestampProfiler` reads only completion-qualified batches. D3D12 uses
  fence completion, Vulkan uses submission completion and query availability,
  and Dawn uses submitted-work completion plus asynchronous mapping.
- Browser timestamps require both the `timestamp-query` feature and
  `GPUCommandEncoder.writeTimestamp`. Report unsupported cells as
  `capability-blocked`, never as zero or CPU-derived GPU time.

## Commands

```powershell
# Bounded built-in profiler check (not a CPU-only overhead measurement)
.\DayScene.exe --api d3d12 --scene 1 --profileCpuOnly --profileFrames 120

# Combined profiling and telemetry, with bounded exit
.\DayScene.exe --api d3d12 --scene 6 --profileCpuOnly --profileFrames 600 --telemetry

# External presentation metrics, no engine changes
& "$env:LOCALAPPDATA\T850Tools\PresentMon\v2.5.1\PresentMon-2.5.1-x64.exe" `
  -process_name DayScene.exe -output_file presentmon.csv -timed 60

# Completion-qualified GPU timestamps; requires /p:T850EnableGpuProfiling=1
.\DayScene.exe --api d3d12 --scene 1 --profileGpu `
  --profileGpuFrames 600 --profileGpuPasses render-graph `
  --benchmarkNoPresent --benchmarkHoldFrame 3000 --benchmarkFrames 660

# Cold x64 per-stage preparation matrix; native DXC and WebGPU boundaries differ
& ..\..\..\.github\skills\t850-arm64-gpu-benchmark\scripts\Capture-X64ShaderCompilationMatrix.ps1
```

Scene 6 is draw and streaming heavy and shows upload behavior most clearly.
Scene 3 exercises per-frame bone texture upload.

`--frames` is not a runtime limit. Telemetry-only runs need a verified stop
condition and a process timeout; do not leave a diagnostic process running.
For pure telemetry overhead measurements, do not enable GPU profiling just to
get its frame-count exit path. Use the R4/R7 measurement harness when available.

Measure compiled-out, runtime-disabled, unsampled and sampled instrumentation
separately; repeat matched runs in alternating order and report uncertainty.
R4's CPU-only mode must not enable GPU query waits. PresentMon presentation
metrics require presented runs and must not be mixed with submit-only results.

The bounded comparison command is `scripts/MeasureProfiling.ps1`; default
three alternating paired runs produce raw telemetry and phase percentiles with
adapter/work matching. A mismatch is inconclusive. MSBuild
`/p:T850EnableProfiling=0` or CMake `-DT850_ENABLE_PROFILING=OFF` compiles out
instrumentation. Do not claim the under-one-percent upload budget without a
matched, repeated overhead measurement. Every-enabled-frame upload warnings default
to 64 MB and can be set with `--telemetryUploadBudgetMB`.

Startup frames include loading/fade pump work and do not consume `profileFrames`.
Fixed-step runs also use fixed-step fades. First real frame milestones start at
telemetry initialization; completion means CPU submission/present return, not
GPU completion. Shader compilation/translation, cache provenance and pipeline
creation are reported separately from steady-state work. `T8_TELEMETRY_CALL`
is for these infrequent resource-creation operations, not per-draw API tracing.
Browser packages report load work, not the offline compiler's recorded time.
The harness observes cache state without clearing it or claiming a forced-cold run.

## Evidence

Generated captures, telemetry JSON and PresentMon CSV are local artifacts. Keep
them under `%LOCALAPPDATA%\T850Profiles\<topic>-<date>`; never commit them.

## Related

- `documentation/debug/diagnostics.md` — profiler, telemetry, dumps, tracing
- `documentation/rendering/gpu-performance-profiling-workflow.md` — canonical
  x64/ARM64 CPU/GPU capture, analysis, and reporting workflow
- `t850-arm64-gpu-benchmark` skill — deterministic Windows x64/ARM64 GPU and CPU matrices
- `documentation/rendering/webgpu-compute-remediation-plan.md` — R1 and R4-R7
  own profiling work; R8 owns the benchmark report
- `t850-deck-performance` skill — Steam Deck PresentMon loop
- `t850-gpu-resource-lifetime` skill — uploads, staging, fences, retirement
