# Compute Flow and Rendering Pipeline Assessment

Reviewed and remediated: 2026-09-17. Status: C01-C06 implementation fixes applied;
local verification below is not a universal hardware portability sign-off.

## Scope and Baseline

The request was to assess the complete compute implementation: the BaseDriver
contract, every driver implementation, code organization, and rendering pipeline
correctness. This assessment covers the working tree rebased onto
`microsoft_webgpu_branch` at `91af57aaef3cad4787e90e1744ac7907d53d892f`, including
the browser integration and subsequent particle-depth/camera fixes. It is not
limited to the original compute PR's diff.

The earlier [branch review](compute-shader-branch-review.md) is historical. Its
claim that no defects remain is superseded by the findings below. Green build CI
does not close GPU correctness findings; a process exit of zero and absence of
ERROR-level messages do not establish validation cleanliness.

## Original Findings and Remediation

The finding descriptions below record the pre-fix behavior, not the current
implementation. Remediation and its verification follow each description.

### C01: Vulkan Storage Image Formats (High, Runtime Confirmed)

`Framework/src/video/vulkan/VulkanCompute.cpp` defines `T850_VULKAN` before
compiling HLSL. The explicit storage-image formats in `Assets/Shaders/CS_*.hlsl`
are guarded by `T850_SPIRV`, so Vulkan does not see them. glslang 16.2.0 emits
`Rgba32f` storage images for TorchParticles, GodRays, Blur, Bright and HDRComposite,
but the maintained graphs bind RGBA16F or RGBA8 render targets.

A Debug Vulkan DayScene compute run produced ten validation warnings stating
that the SPIR-V format does not match the image view and that image writes have
undefined values. The visible result happened to look correct on the RTX 4080
Laptop; that is not a guarantee on another driver or GPU. No formatless-write
feature workaround has been verified or proposed as the solution.

Vulkan now defines both `T850_VULKAN` and `T850_SPIRV`. Shared layout validation
and backend view checks reject storage-format mismatches before dispatch.
Independent glslang checks verify RGBA16F for TorchParticles and RGBA8 for the
five other maintained storage-image writers. Debug Vulkan scene and oracle runs
no longer report the format warnings. The oracle also rejects an RGBA16F target
bound to an RGBA8 writer on all five native APIs.

### C02: Vulkan Compute Synchronization (High, Source Finding)

`VulkanUtils.cpp` transitions transfer/color/depth writes to sampled layouts with
fragment-only destination stages. `VulkanCompute.cpp` does not add a barrier when
an input is already in shader-read layout. Also, compute-buffer initial data uses
`VulkanDriver::UploadBufferData`, whose post-copy barrier targets vertex/index
reads rather than compute shader access. Layout identity alone does not establish
the required memory visibility. The short validation run did not report a
synchronization hazard; distinguish this source finding from C01's reproduction.

Sampled-layout transitions now include compute and all graphics consumers;
upload barriers include shader reads/writes in the shared batched/standalone
recording path. Dispatch barriers order subsequent transfer, compute and graphics
access. `PopRT` explicitly transitions attachment writes before sampled use, so
already-sampled inputs retain producer visibility. Native oracles exercise initial
uploads and repeated producer/consumer writes; particle depth exercises graphics
clear-to-compute reads. Maintained scenes exercise compute-to-graphics use.

### C03: Buffer Usage and Binding Access (Medium)

`ComputeBufferAccess` conflates creation usage with per-dispatch access. D3D11
creates either an SRV or UAV. D3D12 and WebGPU reject a ReadWrite buffer bound as
ReadOnlyBuffer, whereas GL and Vulkan accept it. A normal producer/consumer chain
therefore cannot use the same binding contract on every backend.

Creation access now means maximum capability: ReadWrite permits either dispatch
access; ReadOnly never permits a write binding. D3D11 creates both SRV and UAV
for ReadWrite buffers. All five backends pass the producer/consumer oracle and
reject a write binding against a ReadOnly allocation. The `read-input` arithmetic
permutation is also registered in the offline browser shader manifest.

### C04: Readback Frame Contract (Medium)

`VulkanDriver::ReadComputeBuffer` records a copy using the current command buffer
without ensuring that it is recording or ending an active render pass. After
CompleteFrame, the frame index has advanced and that buffer is not necessarily
recording. D3D12 rejects readback without an active frame; WebGPU handles both
standalone and active-frame cases. Vulkan and D3D12 complete the current frame
with SubmitNoPresent, so arbitrary mid-frame use is not portable either.

Readback supports standalone, post-dispatch and post-submission calls, accepts
nonzero four-byte-aligned byte counts, and preserves an open frame and its target.
Vulkan and D3D12 submit/wait and resume recording without advancing the frame;
graphics binding caches are invalidated. Callers must rebind draw resources and
topology after this diagnostic synchronization boundary. Regression coverage on
all five APIs includes initial data, repeated dispatch, post-submission readback,
and readback between red/green target clears. This is not a claim that arbitrary
raw backend command state survives a command-buffer reset.

### C05: Pipeline Layout Validation (Medium)

D3D11/12 derive bindings from reflection but ignore ComputePipelineDesc.bindings.
GL/Vulkan validate declared uniqueness without matching the complete shader
interface. WebGPU validates reflected counts, kinds, uniform sizes and storage
formats. BaseDriver's layout lacks texture format/dimension/access details, and
the same malformed declaration can behave differently across drivers.

`ComputePipeline::SetValidatedLayout` and `ValidateBindings` enforce complete
interfaces, unique logical registers/portable indices, constant counts, access,
sampler pairing and no read/write aliases. D3D uses DXBC reflection; Vulkan and GL
use pinned, unmodified Khronos SPIRV-Reflect; WebGPU uses its existing reflection.
Storage formats and non-array, non-MS 2D texture interfaces are checked. Native
views are checked before dispatch, including Vulkan cubemap rejection.

On D3D, shader registers are the reflected identity. On portable APIs, the
reflected binding index is authoritative and the descriptor maps it to the
caller's logical register. Source authors must keep HLSL registers, explicit
SPIR-V indices and GLSL indices consistent; SPIR-V does not preserve the original
D3D register identity. Cross-API oracle coverage checks that maintained shaders
obey this mapping. Malformed-layout, missing-binding, invalid-access, alias and
format tests pass. A rejected dispatch followed by clear/readback verifies that
invalid calls leave the active target usable.

### C06: Resource Lifetime (Medium)

Vulkan compute buffer/pipeline destructors destroy native resources immediately.
D3D12 uses immediate COM ownership release without compute-specific retirement.
WebGPU buffers use completion-aware retirement. BaseDriver does not document an
explicit wait-before-destruction requirement or provide compute retirement hooks.
Current diagnostic tests wait through readback, and Minecraft's shadow target
recreation waits explicitly. RenderContainer::Resize does not wait itself; outer
callers must be audited before claiming a reproduced resize failure.

Vulkan defers compute buffer/pipeline destruction to frame-fence retirement.
D3D12 retains native pipelines, buffers and textures referenced by recorded
compute commands until the corresponding frame fence completes. Flush handles
pending recording before reclamation. RenderGraph flushes before destroying or
rebuilding its targets/pipelines, covering graph resize/reload ownership.
The driver must outlive its compute objects; owners of arbitrary textures must
still flush before destroying native views/descriptors. This is not a generic
texture-retirement API. The all-backend oracle destroys compute objects before
submission and verifies the result after submission/readback.

## Rendering Pipeline Assessment

The maintained graphs have coherent ordering: GBuffer/depth, deferred lighting,
transparent rendering, compute effects, luminance adaptation, bright/bloom,
tone mapping and final composition. Kernel constants, scoped camera selection,
reflected workgroup sizes and ceiling-divided dispatch extents are reasonable.
HLSL post-process math closely follows its raster counterparts. Raster remains
the default, and unsupported compute textures select authored draws or clear
fallbacks. Torch particles have no raster implementation.

The graph is an ordered pass executor with informational dependency edges, not
a dependency scheduler or barrier compiler. It now validates shader storage
formats, output extents, pixel-indexed input extents and sampler-to-texture pairing
by register, in addition to typed resources and read/write feedback. Tests include
swapped samplers between two valid inputs and mismatched particle depth extents.

A disabled pass performs no writes and leaves its target unchanged; disabling a
producer does not automatically disable consumers. The caller must disable those
consumers too when retained output is inappropriate. Failed compute dispatch uses
the authored graphics path, including its clear policy. TorchParticles has no
raster draws and clears its output to transparent black. Raster fallbacks and
camera/depth conventions remain unchanged.

TorchParticles loops over every particle for every screen pixel, recomputing
hashes, trigonometry and projection. At 1920x1080 this is about 49.8 million
particle iterations for 24 particles, or 132.7 million for 64. This is a scaling
concern, not a measured performance regression. Profile before choosing projected
bounds, tiled lists, or a different particle rendering strategy.

## Design Summary by Backend

| Backend | Assessment |
| --- | --- |
| D3D11 | Current kernels work; reflection-based runtime checks are useful. Per-dispatch immutable constant-buffer creation is an optimization candidate. |
| D3D12 | Reflected contracts, resumed readback recording and frame-fenced compute keep-alive are covered by the native oracle. |
| Vulkan | Storage formats, compute visibility, reflection, resumed readback and compute retirement are fixed and covered by focused native tests. |
| OpenGL | Reflected GLSL layouts and shared validation are enforced; separate HLSL/GLSL sources still require parity coverage. |
| WebGPU | Existing reflection and retirement are retained; read-only consumption of writable buffers and common validation are now supported. |

Keep the existing scene-values -> kernel registry -> render graph -> driver
separation. A wholesale rewrite is unnecessary. The
[implementation contract](compute-shader-implementation.md#api-neutral-framework-contract)
now specifies submission, lifetime, readback, buffer capabilities and supported
texture formats. `ReadWriteTexture` is retained for API compatibility but means
write-only storage in the portable contract.

## Evidence and Limits

### Strict Shader-Flow Follow-Up

The original native `auto` tests did not prove direct WGSL compute coverage.
The follow-up found that only the legacy SeparableBlur probe had a WGSL source;
the eight ComputeV1 families fell back to HLSL/SPIR-V. They now have maintained
WGSL siblings, including the arithmetic read-input permutation. `DawnComputeV1`
passes strict-flow reflection parity for all nine variants. Native WebGPU GPU
oracles pass explicitly under both `--shaderFlow wgsl` and `--shaderFlow spirv`.

Browser packages now have version-3 flow-specific identity and source provenance;
the browser applies `?shaderFlow=` before creating assets. Full export passed for
all seven scenes in raster and compute modes under all three shader flows, and
the Wasm build and shared tests passed. Chrome and Firefox each passed compute
oracles under strict `wgsl` and `spirv`; scenes 1 and 6 passed strict-flow browser
smoke checks in both flows. These checks use regenerated version-3 packages and
assert the selected compute flow. No HLSL/SPIR-V compiler runs in the browser:
that path is prepared offline and supplies Tint-generated WGSL to WebGPU.

Final publication checks passed the six-cell Windows matrix with shared tests,
Debug/Release `DawnComputeV1`, and all 11 browser unit tests. Clean Android Release
builds passed both ABIs before the final browser-only configuration guard change.
Exact-head CI remains the independent platform gate, especially for SteamRT,
which is unavailable locally. These checks establish build and focused runtime
health, not numerical equivalence of every rendered frame across shader flows.

### Assessment and Remediation Runs

Original assessment runs used x64 Debug, DayScene, Compute mode, 640x360, fixed
1/60-second steps and frame 3. Vulkan enabled synchronization validation and best
practices: exit 0, ten storage-format warnings, no reported sync hazards. D3D12
with --d3d12debug exited 0 with no captured errors. SDK glslang and the pinned
engine headers both report 16.2.0; an independent SPIR-V probe confirmed Rgba32f.

Evidence is external generated output under
`%LOCALAPPDATA%/T850Profiles/compute-review-20260917`, especially
`vulkan-scene1-debug.log` and `d3d12-scene1-debug.log`. No logs or private config
are required in Git.

Post-fix evidence is under
`%LOCALAPPDATA%/T850Profiles/pr-web-compute-20260917`:

- Clean Windows Debug/Release builds for Win32, x64 and ARM64 passed (six cells),
      including the matrix's Win32/x64 shared self-tests.
- Clean Android Release builds passed for arm64-v8a and x86_64, sequentially.
- Browser shader export/build and Wasm shared self-tests passed.
- All five native APIs passed compute positive/negative, lifetime and readback
      oracles. Chrome and Firefox passed the rebuilt browser compute oracle.
- Chrome passed touch/OnTop, camera controls/stability and BC/float-filtering
      feature-removal tests. Firefox passed raster/camera and desktop input checks.
- Local SteamRT is blocked: no Podman executable or installed WSL distribution.
      GitHub CI remains the independent SteamRT build gate.

Native GPU evidence is from an RTX 4080 Laptop. Browser feature removal is
emulation, not physical phone testing. Android device runtime, arbitrary backend
state continuation, and multi-vendor GPU coverage remain unverified. The particle
performance concern is unchanged and requires profiling, not a correctness fix.

The particle depth omission and FPS camera-grounding jitter were fixed before
this assessment. They should retain their regressions, not be reopened as pending.
The initial review was read-only; the subsequent requested remediation implements
C01-C06 before publication. No deployment or merge is part of this work.

## PR Review Follow-Up

The PR review correctly identified duplicate Pages manifest paths, an omitted
server test in the documented npm command, and deferred Vulkan pipeline releases
being enqueued after the graph flush. Manifest loading now rejects exact and
case-insensitive duplicates before modifying staging output. The npm command runs
both suites. Graph teardown retires pipelines before its existing flush, then
destroys target views; shared tests cover the ordering and repeated teardown.

Three proposed correctness fixes were not supported by the API contracts or
implementation:

- WebGPU texture destruction happens after queue submission. The
      [WebGPU specification](https://www.w3.org/TR/webgpu/#dom-gputexture-destroy)
      preserves previously submitted uses until completion; no extra completion
      queue is needed for destruction (unlike buffer pooling and reuse).
- D3D12 command-list reset does not require resetting the allocator first.
      [Microsoft's recording guidance](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles)
      explicitly permits recording multiple lists into an allocator before reset.
      The existing readback waits for submission and resets the closed list; the
      frame allocator is reclaimed through the normal frame lifecycle.
- The local server stores file paths and streams the file on every request,
      not cached bytes. Regression coverage now replaces same-sized assets and
      shader packages in place, reuses the server, and checks the updated contents.

The persistent-asset-cache suggestion is a separate optimization, not a broken
configured cache: `/persistent` is for generated runtime data. Authored assets
use session-local `/assets`; persisting them without version validation would
introduce stale data across rebuilds and deployments. That policy is unchanged.