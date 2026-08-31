---
name: t850-gpu-resource-lifetime
description: "Use when changing or debugging T850 D3D12/Vulkan/D3D11/OpenGL resource creation, uploads, staging buffers, descriptors, samplers, command submission, fences, frame completion, swapchains, buffer retirement, GPU memory growth, validation errors, device removal, or API-specific rendering lifetime bugs."
argument-hint: "State API, resource type, create/update/destroy path, frame lifecycle, validation message, and whether the issue is correctness, synchronization, or memory."
---

# T850 GPU Resource Lifetime

Use this workflow for renderer ownership and synchronization changes. Keep scene/framework code API-neutral; typed backend operations belong in the owning backend or strategy.

## 1. Establish the Lifecycle

Before editing, write the concrete lifetime:

```text
CPU owner
creation thread
GPU-visible allocation
upload/staging allocation
command list/buffer that references it
submission fence/frame index
last legal use
retirement owner
actual destruction point
```

If any item is unknown, read one owning backend and one caller before changing code.

Primary owners:

```text
Framework/include/video/BaseDriver.h
Framework/src/video/BaseDriver.cpp
Framework/include/video/<api>/
Framework/src/video/<api>/
Framework/include/scene/MutableMesh.h
Framework/src/scene/MutableMesh.cpp
Framework/src/debug/ProfilerGpuBackend.cpp
FrameworkImGui/src/ImGui*Backend.cpp
DayScene/Application.cpp
```

## 2. Frame Lifecycle Contract

Every rendered frame follows:

```text
BeginFrame
record rendering and overlays
EndFrame / profiler resolution
CompleteFrame(Present or SubmitNoPresent)
```

A rendered frame must be submitted exactly once. Skipping first-frame submission or presenting a command buffer twice desynchronizes:

- frame allocators;
- command lists/buffers;
- swapchain image indices;
- ImGui per-image resources;
- descriptor pools;
- timestamp queries;
- retired resources.

Do not fix a frame-index mismatch by adding an unexplained extra buffer slot. Prove the missing/duplicate lifecycle transition first.

## 3. Ownership Rules

- Resource registry owns managed textures; scenes do not destroy registry-owned textures.
- Cache entries are immutable after acquisition; create variants instead of mutating shared materials.
- Backend upload staging must remain alive until its submission fence signals.
- Replaced VB/IB resources retire after all referencing in-flight frames complete.
- Descriptor handles/ImGui texture IDs are backend-owned and released through that backend.
- CPU snapshots are optional only when no caller needs their geometry after upload; retain counts, bounds, sections, materials, and version explicitly.
- Never destroy a Vulkan/D3D12 resource merely because recording finished; GPU execution is asynchronous.

## 4. Upload Path

For a resource created with initial data:

1. allocate destination;
2. allocate/upload staging if backend requires it;
3. record copy and state transition;
4. submit on the owning queue;
5. associate staging with a fence;
6. allow destination use only after queue ordering guarantees it;
7. reclaim staging when the fence completes.

Batching rules:

- nested `BeginResourceUploadBatch()` / `EndResourceUploadBatch()` must flush only at depth zero;
- keep command allocator/list/buffer and staging allocations together;
- reclaim completed pending batches during long startup upload loops, not only on first `BeginFrame()`;
- do not call queue-idle per small upload unless correctness/debug explicitly requires it;
- do not run two vcpkg/build installs concurrently as a workaround for upload bugs.

## 5. Replacement and Retirement

For mutable geometry:

```text
validate new snapshot
create new VB and IB
if either fails, retire partial new resources and keep old pair
retire old pair through BaseDriver
publish new pair and metadata atomically
```

D3D11/GL may release immediately when commands are immediate and complete by contract. D3D12/Vulkan must delay destruction across in-flight frames/fences.

When optimizing memory, inventory all copies:

```text
source database arrays
MutableMeshSnapshot vertices/indices
Buffer::sysMemCpy
upload/staging resource
GPU destination
retired old destination
navigation/physics copy
```

Measure peak memory before and after. Do not infer savings from one container being cleared.

## 6. D3D12 Checks

Validate:

- command allocator reset only after its fence completes;
- one allocator/list per in-flight backbuffer or equivalent ownership;
- signal after command execution;
- `BeginFrame` waits only for the buffer being reused;
- DEFAULT-heap initial data uses staging and proper final state;
- descriptor heap offsets do not overlap persistent ranges;
- samplers and SRVs are bound at the root parameters expected by the shader;
- Present uses intended sync interval/tearing flags;
- waitable swapchain latency is not confused with same-frame serialization;
- completed upload batches and retired buffers are reclaimed regularly.

A large `D3D12_FenceWait` while GPU busy equals frame time usually means GPU backpressure, not an intrinsically expensive CPU function.

## 7. Vulkan Checks

Validate:

- command buffer reset occurs only after its fence signals;
- query pool reset is recorded before timestamp use;
- image acquisition index matches framebuffer/per-image resources;
- semaphores/fences are not reused while pending;
- descriptor sets/pools remain valid through submission;
- staging buffers are destroyed only after upload fence completion;
- `vkDestroyBuffer`/VMA destruction never occurs while a command buffer can reference it;
- swapchain recreation drains or retires old image-dependent resources safely;
- delayed descriptor binding is accounted for in trace/debug comparisons;
- all rendered frames call `CompleteFrame`.

Treat validation-layer messages as primary evidence. Record object type/handle, command buffer, queue, and VUID before editing.

## 8. API-Neutral Design

Shared code should ask capabilities or call virtual/strategy hooks:

```text
UsesGLSL
NeedsVFlip
SupportsDeferredRendering
SetPrePresentOverlayCallback
SetLatePresentSource
ProfilerGpuBackend
ImGuiRendererBackend
```

Do not add shared-code `if (api == ...)` or backend downcasts when the behavior belongs to a driver capability or strategy. API switches are acceptable at factories/composition roots.

## 9. Discriminating Tests

Choose the smallest check that distinguishes ownership hypotheses:

| Hypothesis | Check |
|---|---|
| resource destroyed while in use | validation layer/debug layer + fence/object handle log |
| first frame not submitted | frame-index/submission trace for frames 0–3 |
| wrong sampler/descriptor | texture-ID/root binding trace and single-texture probe |
| allocator reused early | fence value per allocator/backbuffer |
| upload staging retained too long | pending-batch count and process memory through startup |
| CPU geometry retained redundantly | inventory snapshot + `sysMemCpy` + staging sizes |
| present path limits FPS | PresentMon mode/sync/tearing plus engine present scope |
| backend visual mismatch | deterministic render-target dump, first divergent pass |

A reversible probe may log IDs, fence values, frame indices, queue depth, bytes, and ownership. Remove it after the check.

## 10. Validation Gates

### Focused build

```powershell
.\T850\scripts\build.ps1 -Config Release -Platform x64 -Action Build
```

### D3D12 debug/validation

Use Debug plus `--d3d12debug` when validation is relevant. Scan for device removal, live objects, resource-state, allocator, descriptor, and execute-command-list errors.

### Vulkan validation

Run the Vulkan validation-enabled configuration available in the repo. Require zero validation errors through startup, several frames, resize/recreation if touched, and teardown.

### Cross-API smoke

For shared buffer/texture/scene code, run sequential finite captures on:

```text
d3d11
d3d12
vulkan
gl
```

Require exit 0, expected dumps, and zero engine errors. Use `t850-api-frame-comparison` for pixel evidence.

### Stress relevant transitions

Depending on change:

- repeated create/replace/destroy;
- many startup uploads before first frame;
- live chunk expansion/shrink;
- resize/fullscreen/swapchain recreation;
- ImGui texture creation/pruning;
- first rendered frame and scene switch;
- offscreen submit without present;
- profiler on/off.

### Platform gate

D3D12/Vulkan Framework changes require Windows Release and SteamRT/Deck build when available. New Framework sources require Visual Studio, desktop CMake, and Android CMake registration.

## 11. Memory Acceptance

For scale changes, report:

- baseline and candidate peak working set/cgroup `MemoryPeak`;
- CPU snapshot bytes;
- buffer system-copy bytes;
- staging/pending bytes;
- GPU allocation estimate;
- retired-resource high-water mark;
- startup and steady-state values.

A lower steady state does not excuse an OOM startup peak. A finite frame-3 test is useful for startup pressure; a long live-transition test is required for retirement/reuse stability.

## 12. Common Wrong Fixes

- adding extra swapchain/ImGui images without proving index ownership;
- calling `WaitForGPU()` after every upload;
- clearing a CPU snapshot while `Ready()` still depends on `snapshot.Empty()`;
- freeing staging immediately after queue submit;
- mutating shared cached materials to attach textures;
- fixing a backend descriptor mismatch in scene code;
- disabling validation or swallowing device-loss errors;
- comparing APIs with different draw counts/cameras;
- treating exit 0 with a blank/uniform dump as success.

## 13. Completion Report

State:

- owner and lifetime before/after;
- exact fence/frame/queue invariant;
- resource copies retained and reclaimed;
- peak memory before/after;
- validation/debug-layer result;
- first-frame, replacement, resize, and teardown checks as applicable;
- D3D11/D3D12/Vulkan/GL smoke results;
- deterministic image result;
- unavailable platform gates and remaining risk.

Related skills:

- `t850-api-frame-comparison`
- `t850-deck-performance`
- `t850-crash-debugging`
- `t850-scene-runtime-controls`
- `t850-local-build-validation`

Related documentation:

- `documentation/architecture/platform-event-loop.md`
- `documentation/rendering/geometry-rendering-flow.md`
- `documentation/rendering/textures-and-ibl.md`
- `documentation/debug/diagnostics.md`
- `documentation/testing/verification.md`
