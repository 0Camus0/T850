# WebGPU Backend Implementation

Status: implemented for DayScene on Windows x64, Windows ARM64, and Emscripten.
This file replaces the completed design proposal with the current architecture,
supported behavior, validation evidence, and remaining limitations. Historical
planning and time estimates remain available in Git history.

## Supported Scope

| Platform | Provider and backend | Status |
|---|---|---|
| Windows x64 | Native Dawn over D3D12 | Supported by DayScene |
| Windows ARM64 | Native Dawn over D3D12 | Supported by DayScene |
| Browser/Wasm | Emdawnwebgpu over browser WebGPU | Supported by the browser runtime |
| Win32 | None | Native D3D12 requires `legacyHLSL`; WebGPU is unavailable |
| Android | Native Vulkan | No Dawn/WebGPU backend |
| Linux/Steam Deck | Native Vulkan | No Dawn/WebGPU backend |
| T8ditor | Existing native backends | WebGPU remains unavailable |

WebGPU is selected through the normal API factory and runtime configuration. It
is not implemented as a wrapper around T850's native D3D12 driver. Native
D3D12, Vulkan, D3D11, and OpenGL remain independent peer backends.

## Runtime Architecture

The implementation is split across these owners:

| Area | Current owner |
|---|---|
| API-neutral frame/resource contract | `Framework/include/video/BaseDriver.h` |
| Windows Dawn instance, adapter, device, queue, surface, submission completion, and pooled resources | `Framework/include/video/webgpu/WebGPUContext.h`, `Framework/src/video/webgpu/WebGPUContext.cpp` |
| WebGPU buffers, textures, render targets, shader modules, pipelines, draw/dispatch, readback, and frame submission | `Framework/include/video/webgpu/WebGPUDriver.h`, `Framework/src/video/webgpu/WebGPUDriver.cpp` |
| Runtime shader preparation and reflection | `Framework/include/video/webgpu/WebGPUShaderCompiler.h`, `Framework/src/video/webgpu/WebGPUShaderCompiler.cpp` |
| Browser-prepared shader package | `Framework/src/video/webgpu/WebGPUShaderPackage.cpp` |
| ImGui renderer | `FrameworkImGui/src/ImGuiWebGPUBackend.cpp` |
| Browser host and asset/runtime shell | `Framework/src/core/WebFramework.cpp`, `web/` |

Dawn objects stay inside the WebGPU backend. Scene, render-graph, gameplay, and
editor-neutral code use the shared driver interfaces and capability queries.
The frame contract is `BeginFrame` -> rendering/compute -> `CompleteFrame`, with
`SubmitNoPresent` available for deterministic offscreen measurement.

## Dawn Integration

Windows uses pinned vcpkg overlay packages for Dawn and ImGui. The effective
Dawn native backend is D3D12 only. `scripts/SetupDawn.ps1` installs or audits the
package and generates architecture-specific MSBuild link metadata under:

- `build/dawn-package` for x64;
- `build/dawn-package-arm64` for ARM64.

`cmake/DawnPackage.targets` imports the generated link contract and stages
`dxcompiler.dll`, `dxil.dll`, and required notices. Missing or stale package
metadata fails the build with setup guidance. The native package and Emscripten
bundle are separate build products.

## Shader Translation and Caching Model

WebGPU supports three source-flow selections:

| Flow | Behavior |
|---|---|
| `auto` | Prefer the maintained WGSL stage; use the HLSL/SPIR-V path only when WGSL preparation fails |
| `wgsl` | Strict maintained WGSL; no HLSL translation fallback |
| `spirv` | Strict HLSL -> glslang SPIR-V -> Tint WGSL |

All maintained graphics and compute shader families have WGSL counterparts for
the supported runtime corpus. Runtime preprocessing, reflection, binding
validation, and cache identity include source language, stage, entry point,
defines, layout version, and compiler/toolchain signature. A missing cache entry
is regenerated from canonical source; a missing or invalid source fails with a
named diagnostic rather than silently rendering black.

Native D3D12 is separate from these WebGPU source flows. It defaults to DXC,
Shader Model 6, DXIL, and DXC reflection. `--shaderFlow legacyHLSL` explicitly
selects the prior FXC/SM5/DXBC path. Compiler and build configurations use
separate cache namespaces.

The x64 per-stage matrix measures different host-side boundaries:

- native D3D12: DXC compilation plus reflection;
- WebGPU: WGSL or HLSL/SPIR-V/Tint preparation and reflection plus synchronous
  `CreateShaderModule`;
- Dawn backend shader compilation during graphics/compute pipeline creation is
  outside the WebGPU per-stage metric.

Therefore cross-flow percentages describe preparation-path cost, not isolated
compiler speed. See the
[GPU performance profiling workflow](gpu-performance-profiling-workflow.md#profile-individual-shader-preparation-stages-on-x64).

## Resource and Render-Graph Support

The backend implements the resource and rendering behavior used by normal
DayScene scenes:

- indexed and non-indexed drawing;
- vertex, index, uniform, storage, and staging buffers;
- sampled textures, samplers, cubemaps, IBL resources, and dynamic texture data;
- mixed-format MRTs, depth-only targets, floating-point targets, readback, and
  offscreen capture;
- immutable pipeline and bind-group caching;
- completion-qualified resource retirement and bounded pooling;
- normal render-graph execution, overlays, resize, and API recreation;
- comparison-sampler validation and capability-aware texture layouts.

WebGPU reports no render-target mip-generation support, matching native D3D12
and Vulkan. Optional adapter features are requested only when the selected path
needs them. Missing optional BC compression and float filtering use the verified
fallback paths documented in [textures and IBL](textures-and-ibl.md).

## Cross-Backend Compute Design

The API-neutral compute contract is implemented by D3D11, D3D12, Vulkan,
Dawn/WebGPU, and desktop OpenGL 4.3 or newer. It includes:

- `ComputePipelineDesc` and explicit reflected binding layouts;
- storage/read-only buffers, constants, sampled textures, samplers, and storage
  textures;
- three-dimensional dispatch dimensions;
- graphics/compute resource transitions and render-graph execution;
- deterministic readback self-tests;
- raster fallback for targets without the required compute capability.

DayScene's `compute_if_supported` graph mode replaces five fullscreen raster
passes with compute implementations: God Rays, two ray blurs, Bright, and HDR
Composition. This changes draw/dispatch strategy while retaining matched scene
geometry and work counters.

## Native D3D12 Versus Dawn Experiment

The completed profiling study uses DayScene scene 1 at 1920x1080, fixed 1/60
simulation through frame 3000, held simulation afterward, and offscreen
`SubmitNoPresent` work followed by a final queue drain. It separates:

- completion-qualified GPU timestamps;
- queue-drained completed throughput;
- CPU non-wait overhead;
- GPU Engine and ETW evidence;
- cold/warm shader and startup cost;
- x64 per-stage shader preparation.

The final accepted GPU and CPU values, commands, hashes, limitations, and report
procedure are in
[GPU performance profiling workflow](gpu-performance-profiling-workflow.md).
Generated evidence stays outside Git.

Pass-level WebGPU timestamps close active physical passes at logical graph
boundaries. This changes Dawn command encoding. Whole-frame-only correlation and
profiling perturbation remain required before treating pass-profiled whole-frame
deltas as uninstrumented production overhead.

## Profiling and Diagnostics

Four separate facilities exist:

| Facility | Purpose |
|---|---|
| `Profiler` | Legacy named CPU/GPU scopes and draw statistics |
| `ProfilerGpuBackend` | Legacy D3D11/D3D12/OpenGL/Vulkan timestamp strategies |
| `RuntimeTelemetry` | Low-overhead CPU phase, counter, upload, and startup JSON |
| `GpuTimestampProfiler` | Opt-in completion-qualified D3D12/Vulkan/native Dawn whole-frame and logical-pass timestamps |

`T850_ENABLE_GPU_PROFILING` defaults OFF. The runtime requires `--profileGpu`
and rejects unsupported builds or adapters. The profiler uses D3D12 fences,
Vulkan submission completion/query availability, and Dawn submitted-work plus
asynchronous map completion. Browser timestamps remain capability-blocked where
`GPUCommandEncoder.writeTimestamp` is unavailable; browser completed throughput
is reported separately and is not substituted for GPU execution time.

See [GPU timestamp profiling](gpu-timestamp-profiling.md) for implementation
semantics and remaining validation gates.

## Build Integration

WebGPU sources are registered in MSBuild and CMake. Normal Windows x64 and ARM64
builds require their corresponding audited Dawn package. Win32 and non-Windows
native targets compile without Dawn. Browser builds use Emscripten's WebGPU port
and prepared shader packages.

The repository's build-registration validator covers framework source/header
registration across MSBuild, filters, CMake, and Android source lists. CI builds
Win32, x64, ARM64, Android, Steam Deck, and the configured browser/package gates.
GPU hardware runtime measurements remain local because hosted runners do not
provide the required physical adapters.

## Emscripten and Browser Milestone

The browser runtime is implemented, not future work. It includes:

- Emscripten/WebGPU rendering for the supported scene catalog;
- worker-based runtime and browser-owned event loop;
- packaged shader artifacts and cloud/local asset loading;
- resize, input, touch controls, persistence, diagnostics, and bounded telemetry;
- native x64 and ARM64 Edge validation using the same Wasm identity;
- capability-aware BC and float-filtering fallback paths.

Hosted CI uses explicitly identified software WebGPU for correctness, not
performance evidence. Browser queue-drained throughput was measured on native
x64 and ARM64 hardware separately.

## Required Release Acceptance

The implemented DayScene backend has passed the focused build, self-test,
compute, shader-corpus, scene-startup, browser, x64, and ARM64 gates recorded in
the linked documentation. The following are limitations rather than unfinished
parts of the original implementation plan:

- T8ditor does not expose WebGPU rendering.
- Android and Linux/Steam Deck continue to use native Vulkan.
- Universal pixel identity is not claimed; accepted differences and open visual
  cases are documented in [shader management](shader-management.md).
- Browser per-pass timestamps depend on browser API support and are currently
  capability-blocked on the tested Edge versions.
- GPU timestamp external correlation, instrumentation perturbation, and
  pending-callback/device-loss stress remain validation work.
- Full device-loss recovery and recovered-versus-fresh visual/leak equivalence
  are not part of the committed profiling implementation.

Do not describe these limits as completed, but do not use the superseded
proposal milestones as the current implementation status.

## Verification

Current evidence includes:

- Windows x64 and ARM64 Debug/Release builds;
- Win32 compatibility builds with explicit legacy D3D12 shader flow;
- Android and Steam Deck regression builds;
- gameplay/self-test suites and browser page tests;
- native D3D12, Vulkan, and Dawn timestamp runtime gates;
- six-cell x64 and ARM64 raster/compute GPU matrices;
- matched native and Edge CPU matrices;
- strict WGSL/SPIR-V shader corpus and startup captures;
- x64 per-stage shader-preparation matrix;
- visual captures and browser desktop/mobile report validation.

Exact commands and accepted result hashes are maintained in the workflow and
subsystem documents rather than duplicated here.

## Remaining Work

These are the current actionable items:

1. Measure whole-frame-only versus pass-level timestamp perturbation and use the
   whole-frame-only result for cross-backend headline comparisons.
2. Correlate timestamp boundaries with PIX/RenderDoc or equivalent ETW evidence,
   including a Vulkan run.
3. Complete pending-callback teardown, feature-negative, and device-loss stress
   for `GpuTimestampProfiler`.
4. Decide whether T8ditor will support WebGPU; it is currently excluded rather
   than partially supported.
5. Resolve or formally accept the remaining visual-difference cases.

## Related Documents

- [WebGPU runtime summary](webgpu-runtime-summary.md)
- [WebGPU compute remediation record](webgpu-compute-remediation-plan.md)
- [Shader management](shader-management.md)
- [Compute shader implementation](compute-shader-implementation.md)
- [GPU performance profiling workflow](gpu-performance-profiling-workflow.md)
- [GPU timestamp profiling](gpu-timestamp-profiling.md)
- [Browser platform](../platform/browser.md)
- [Windows build and run](../development/windows-build-and-run.md)
