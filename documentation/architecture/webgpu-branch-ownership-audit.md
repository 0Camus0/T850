# WebGPU Branch Ownership Audit

Scope: the nine commits in `microsoft_daniel_branch` after shared base
`c9bb6a75b0b75314673d95ef0e2702c575ac4d0d`, plus the runtime scene callers affected
by their contracts. PR #39 remains unmerged. This is not a claim that all legacy
scene architecture has been migrated into Framework.

## Commit Assessment

| Commit | Change | Assessment and disposition |
| --- | --- | --- |
| `95293d19` | WebGPU proposal | Design documentation, not runtime behavior. Preserve historical proposal versus implemented-status distinctions. |
| `b67f4f05` | Shadows and event loop | Shadow sampling, SSAO initialization and frame completion belong in Framework. Retain those shared fixes; validate all native APIs as well as WebGPU. |
| `9e5119a6` | Mouse capture | Scene-class constant overrides were inappropriate policy. Replaced by authored `mouse_capture`, applied by Framework `SceneSetup` and queried through `SceneBase`. |
| `66e1139a` | Proposal update | Documentation only. Does not authorize scene-specific renderer workarounds. |
| `851277e9` | WebGPU runtime and shader flows | Driver/compiler/reflection and mipmap utilities have appropriate Framework owners. Removed backend guesses for absent scene texture slots. Shader-flow configuration now occurs in the Windows driver factory, not via a platform-host downcast. The app's scene-transition GPU drain is now Framework lifecycle behavior. |
| `21556f9b` | Launcher cache compilation | Manifest parsing, source selection, compilation, cancellation and progress were incorrectly implemented in the executable entry point. Moved them into Framework shader services and a Framework-owned tool host. CLI parsing/help/validation live in `ConfigRuntime`; recording exit hooks live with the recorder. |
| `ae02906a` | Restore CI self-tests | Keep enabled; tests exit before graphics startup. No scene ownership exception needed. |
| `563c985b` | Stage Vulkan loader | Correct packaging responsibility in build targets. Preserve architecture-correct runtime staging for self-tests and executables. |
| `cac79fbf` | Shared-branch PR validation | Keep branch routing and PR validation; no engine behavior. |

The branch changes 108 files before this cleanup. These include shader assets,
compiler/backend code, test fixtures, vendored simplecpp, dependency overlays,
launchers, build registrations and documentation. ABI binding numbers, rendering
algorithms, dependency versions and isolated test geometry are not scene data.
They should not be moved into `.t8scene` merely because they contain constants.

## Corrected Ownership

- `PrecompileShaders(BaseDriver&, request)` owns manifest validation, resource
  loading and compilation. Its caller supplies cancellation/progress callbacks;
  it does not depend on a scene or the global driver.
- `RunShaderPrecompileCommand` owns the Windows tool application and driver
  lifecycle. Android's existing offline APK compilation remains separate.
- `SceneBase::AllowsMouseCapture` reads `SceneProps`. DayScene and Sandbox author
  their previous false policy in scene files; other document-based callers apply
  explicit scene overrides through `SceneSetup`. Absent policy preserves the
  previous true default.
- `RootFramework::UnloadScene` drains GPU work before scene destruction. App and
  DevLayer transitions use this operation.
- All removed fixed GBuffer bindings in DayScene, Sandbox, Quake3, Minecraft and
  SceneTemplate are supplied by the authored render graph.
- `DEFAULT_PASS` opaque mesh rendering no longer requires prior scene depth or
  scene color. `FORWARD_PASS` explicitly enables those compositing operations in
  HLSL, GLSL and WGSL. The recorded offline manifest carries the same define.
  WebGPU rejects missing required resources instead of inventing black textures.
- VoxelScene now loads `VoxelScene.t8scene`: camera/light setup, filters, graph,
  disabled passes, palette, atlas pixels, chunk dimensions, streaming budgets,
  layered terrain parameters, edit path, camera mode and reach are authored data.
  Framework builds and validates the palette, generates layered chunks, performs
  collision sweeps and resets streaming dimensions. Palette order remains
  stone/dirt/grass to preserve existing saved block IDs.
- DayScene loads model paths/transforms/visibility and graph selection from its
  existing authored scene rather than repeating Sponza/SkyBox content in C++.
  Framework applies object transforms. Static physics sources and cook settings
  come from named authored physics entities, not a first-object assumption.
- New source files are registered in MSBuild, filters, desktop CMake and Android
  CMake. The enlarged scene serializer uses MSVC `/bigobj` in both build systems.

## Scene Sweep

| Runtime | Cleanup coverage | Legacy behavior still present |
| --- | --- | --- |
| Sandbox | Authored capture policy; graph-owned GBuffer inputs | Mesh viewer, profile, navigation and editor orchestration remain in the scene. |
| DayScene | Authored input/content/graph/physics; graph-owned inputs | Benchmark matrix, spectator fallback and first-object diagnostics remain specialized legacy code. |
| Quake3Mock | Authored input override; graph-owned inputs | Q3 clip-path conventions, navigation and profile orchestration remain. |
| RagdollEditor | Framework input-policy application | Hosted editor orchestration and default graph selection remain. |
| SceneTemplate | Authored input override; no fixed GBuffer binding | Large legacy entity/physics/navigation integration and default-profile fallback remain. |
| VoxelScene | Authored setup/palette/terrain/budgets; shared collision/generator | Input mapping, chunk render/physics integration and dump-target selection remain in the reference app. |
| Minecraft | Authored input override; graph-owned inputs | Existing authored voxel-world integration, player/mob/gameplay and diagnostic orchestration remain. |

These residuals are not declared resolved by the new tests. In particular, a
complete "no engine-specific code in any scene" migration still requires moving
shared lifecycle, input, diagnostic and scene-authoring integration out of the
large legacy scene implementations. Merely relocating entire scene classes into
Framework would hide rather than fix their coupling.

The shader permutation recorder now uses Glaze to validate existing manifests
before merging and ResourceLocator for atomic replacement. Malformed, unreadable,
unsupported or inconsistent input is rejected without truncation. Recording
remains a single-process operation: simultaneous writers to the same manifest
are not supported. The launcher serializes recording/compilation jobs.

## Validation

Evidence is saved locally under `T850/build/architecture-cleanup/` (ignored).

- Final six-cell Windows matrix passed, including 59 self-tests in each of the
  Win32/x64 Debug/Release cells (236 checks). ARM64 was compiled, not executed.
- Gameplay/terrain suite includes new ownership, precompiler and voxel-authoring
  tests: input round-trip/override, GPU-drain ordering, failure/cancellation,
  palette identity, malformed data, negative coordinates and streaming reset.
  Recorder tests cover merging, JSON escaping, malformed-input preservation and
  recovery after a rejected write.
- Shader precompilation through the extracted service compiled 281 entries each
  on D3D11, D3D12, OpenGL, Vulkan, WebGPU-auto and WebGPU-SPIR-V: 1,686 successes,
  zero failures. Conflicting modes and missing manifests were rejected.
- All 11 CTests passed in both Debug and Release, including GPU tests and the
  recorded WGSL permutation corpus.
- Full same-API visual comparison: 48 native/WebGPU-auto captures and 10
  WebGPU-SPIR-V captures, zero capture failures, zero final-image comparisons
  outside tolerance 2. Covers all seven runtime scene types and authored variants.
- Eight skips: Nexus assets missing in six API/flow cases; two Q3 Vulkan cases
  excluded by the existing VRAM guard. Skips are not passes.
- Baseline executable came from successful pre-cleanup run `35040646021`.
  References were captured before shader changes and retained. Comparisons are
  before/after within each API, not claims of universal cross-API parity.
- Final DayScene authored-content/physics captures were byte-identical on all
  four native APIs and both WebGPU flows.
- Full intermediate-target comparison also passed: 760 targets across the 58
  captures at tolerance 2, with no missing or extra targets. This includes the
  final DayScene captures above, not the earlier DayScene implementation.

The earlier cross-checkpoint native D3D12 Voxel lighting discrepancy documented
in the WebGPU runtime summary remains a separate historical open issue; the
current same-API comparison does not retroactively resolve it. Hosted Android
and Steam Deck source checks and final PR status must be reported separately
from local Windows rendering. No merge is authorized by this cleanup.