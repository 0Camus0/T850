# WebGPU Runtime Handoff

Status: Windows x64 runtime close-out, 2026-09-15. This summarizes the local work
from dependency setup through compiler, renderer, real scenes and final validation.
It is not a claim that the entire [WebGPU proposal](proposal-webgpu.md) is complete.

## Outcome

Normal DayScene forward and deferred rendering now runs on **Dawn over D3D12,
Windows x64**, through the existing scene, material and render-graph paths.
All ten available cases captured successfully with both default WGSL-first
`auto` and strict HLSL/SPIR-V/Tint `spirv` flow. No fixture substitution, scene
replacement or native-backend remapping occurs in Launcher RUN.

The demonstrated derivative-uniformity, matrix translation, mutable float texture,
environment binding and SSAO kernel defects were corrected. Large translated
geometry/lighting errors from the initial captures are gone. Residual image
differences remain: the two explicitly reviewed cases are visually accepted,
but neither universal pixel parity nor a fully passing native regression gate is
claimed. The additional native Voxel checkpoint difference below remains open.

T8ditor, shared engine compute, GPU timestamp profiling, performance acceptance
and new platform ports were not implemented as part of this close-out.

## Implementation

### Dependencies and Build Integration

- Pinned vcpkg `77df67cfff9c12ccfdb52284e07c87c75092f723`, Dawn
  `20260219.200501#6`, ImGui `1.92.7#1`, glslang 16.2.0 and simplecpp 1.9.1.
- [SetupDawn.ps1](../../T850/scripts/SetupDawn.ps1) provides Plan/Install/Check,
  package audits, generated link properties and compiler identity metadata.
  Ordinary Windows x64 builds require a valid audit; native Vulkan stays separate.
- Installed-package probes exercise exported Dawn/Tint headers and libraries,
  not accidental build-tree dependencies. DX compiler DLLs and dependency licenses
  are deployed through build integration.
- MSBuild remains authoritative; Framework, ImGui and platform source lists are
  registered in MSBuild/filters and kept in CMake parity. CPU preprocessor
  cross-builds were also exercised earlier on ARM64/Android, not WebGPU runtime ports.
- [TintInstall.cmake](../../T850/cmake/vcpkg-overlays/dawn/TintInstall.cmake)
  applies guarded, repeatable upstream fixes. Square row-major matrix accesses
  must update their loads even when the transposed type is unchanged; popping
  an IR operand must remove its usage record to avoid stale-use compiler assertions.

### Shader Sources and Compiler

- [WebGPUShaderCompiler.cpp](../../T850/Framework/src/video/webgpu/WebGPUShaderCompiler.cpp)
  supports in-process HLSL -> glslang/SPIR-V -> Tint/WGSL and direct WGSL paths.
  All 17 maintained HLSL stage families have WGSL counterparts.
- Direct WGSL uses the vendored 0BSD simplecpp preprocessor through an in-memory
  adapter. Production shader loading and cache paths use ResourceLocator.
- `auto` prefers WGSL and falls back only on source preparation failure. Strict
  `wgsl` and `spirv` never cross-fallback. This policy does not hide pipeline
  validation failures, device failures or incorrect rendered output.
- Reflection validates stage interfaces, resource types/dimensions, uniform
  sizes/top-level offsets and fragment outputs. Cache identities include source,
  language, defines, binding policy, shader key and compiler/dependency identity.
  Corruption recovery, invalidation, separate-process warm hits and restoration
  of preferred WGSL after fallback are tested.
- Translator and reflection enable only the required
  `unrestricted_pointer_parameters` feature for indexed matrix helpers; derivative
  validation is not globally suppressed.
- Shared HLSL/WGSL sampling corrections use LOD0 for appropriate single-mip render
  targets, compute SSAO/parallax gradients before divergent control flow, and
  place forward lightmap sampling before discard. Material mip selection remains.
- [ShaderNumerics.cpp](../../T850/cmake/dawn-package/ShaderNumerics.cpp) adds
  448 exact matrix cases covering non-symmetric data, multiplication directions,
  arrays and dynamically indexed matrix/vector/scalar accesses.

### Renderer and Resources

- [WebGPUDriver.cpp](../../T850/Framework/src/video/webgpu/WebGPUDriver.cpp) and
  [WebGPUContext.cpp](../../T850/Framework/src/video/webgpu/WebGPUContext.cpp)
  implement the existing Windows driver/factory contracts: indexed draws, textures,
  depth, state, pipelines, reflected bind groups, offscreen passes and presentation.
- Pending buffer updates preserve per-draw GPU snapshots. Texture uploads cover
  ordinary color, float and cube sources, supported BC compression and source mips;
  loaded-texture CPU mip generation reuses the native alpha-weighted algorithm.
- Mixed-format MRT supports the tested seven-output GBuffer, HDR/single-channel
  targets and depth-only passes, subject to requested adapter limits. Pipeline
  keys include attachment formats/order. All active attachment aliases are checked.
- Clears initialize alpha to zero; load continuations preserve contents. Viewport
  and scissor survive internal pass breaks. Format-aware captures/readback preserve
  float values internally; PPM visualization is an RGB/clamped representation.
- Filterable depth sampling uses a refreshed GPU float copy. Nearest/linear depth
  ramps and float texture updates are covered by the real-driver fixture.
- `WebGPUTexture::UpdateFloatData()` now uploads a fresh RGBA32F snapshot instead
  of inheriting a no-op, restoring tile light lists and point lighting.
- The shared `NO_ENVIRONMENT` shader variant omits unused environment/IBL bindings
  when the scene has none. It fixes Voxel's binding-6 failure without dummy
  environment textures or scene-specific driver behavior.
- SSAO now uploads all kernel samples, initializes a missing kernel and computes
  offsets in view-depth/projected space to avoid large world-position arithmetic.
  The last formulation did not improve the reviewed Ragdoll final-image metric;
  the displayed residual was accepted, not hidden by a wider threshold.
- Surface acquisition, explicit resize, zero-size suspension/resume and controlled
  failure paths are implemented. Driver recreation is tested native -> WebGPU ->
  native. This is not full live-scene API-switch or transparent device-loss recovery.

### Runtime and Launcher

- Normal `--api webgpu` and `--shaderFlow auto|wgsl|spirv` are parsed and validated
  before asset loading; CLI overrides the optional `webgpuShaderFlow` JSON field.
- Main-window runtime ImGui is integrated. Platform viewports remain disabled.
- Both launchers preserve ordinary scene/config/snapshot controls, enforce x64
  prerequisites and audit Dawn during builds. They now advertise forward/deferred
  runtime support while retaining the EDITOR guard. The driver reports deferred
  support, with a matching fixture assertion.
- Portable Launcher packaging was regenerated and copied to the four existing
  x64/ARM64 output folders, then the source-root developer Launcher was regenerated.
  This does not enable WebGPU on ARM64 or validate packaged mouse-click workflows.

## Final Verification

Final local evidence root:
[webgpu-runtime-closeout-20260915](../../T850/build/webgpu-runtime-closeout-20260915).
Generated build evidence is local and ignored by Git; archive it separately when
sharing this report. Commands completed locally, not on remote CI.

| Gate | Result |
|---|---|
| Dawn package audit | PASS, Dawn #6, x64 static, D3D12-only native backend |
| Windows builds | PASS: full x64 and ARM64 Debug + Release, four cells |
| Gameplay self-tests | PASS: 56/56 in x64 Debug and 56/56 in x64 Release |
| Dawn/shader CTests | PASS: 11/11 Debug and 11/11 Release, including both GPU tests |
| Recorded shader corpus | 538 stages through both flows; no-environment binding contracts |
| Vertex corpus | 1,027 variants with native/translated/direct layout and interface checks |
| GPU numerical tests | 448 exact matrix cases, 60 blur fixtures, 12,288 function cases; mutation rejection |
| Real-driver comparison | PASS in Debug and Release; eight image comparisons each, no channels outside tolerance 2 |
| Launcher tests | PASS in PowerShell 7 and Windows PowerShell 5.1: commands, preflight, WPF/config/routing |
| Strict capture matrix | 28 captures, five skips, zero failed captures |
| Default-flow captures | 10 captures, zero skips/failures |

The final build matrix is four cells, not the full six-cell Win32/x64/ARM64 CI
matrix. ARM64 is compile-only on this x64 host and does not include Dawn rendering.
Full CMake engine builds, remote CI, new APK builds and other-GPU testing were not
rerun for this close-out. See [logs](../../T850/build/webgpu-runtime-closeout-20260915/logs).

### Final Scene Metrics

All images are 1280x720. Eight cases replay the same original D3D12 snapshots;
Voxel/Minecraft use five-second fixed-1/60 timed captures because their earlier
replay controls timed out. Saved snapshots differ only by API across each current
D3D12/WebGPU pair, although snapshots are not complete streaming/gameplay state.

Below: final backbuffer pixels with any channel difference greater than 2, followed
by maximum channel delta. These are measurements, not all-passing assertions.

| Case | Strict SPIR-V vs D3D12 | Auto vs D3D12 |
|---|---:|---:|
| Sandbox | 18 / 6 | 18 / 6 |
| Day | 364 / 4 | 364 / 4 |
| Quake3 | 103 / 194 | 103 / 194 |
| RagdollEditor | 1,264 / 9 | 1,306 / 9 |
| SceneTemplate Q3 Jolt | 0 / 0 | 0 / 0 |
| SceneTemplate Q3 | 7 / 13 | 7 / 13 |
| SceneTemplate Day | 617 / 4 | 617 / 4 |
| SceneTemplate Forward | 0 / 1 | 0 / 1 |
| Voxel streaming | 230 / 13 | 230 / 13 |
| Minecraft | 1,931 / 9 | 1,931 / 9 |

Each flow versus D3D12 has **90/132 target images within tolerance and 42 outside**.
All target sets and dimensions match. Auto versus strict has 129/132 within
tolerance, 119 byte-identical. Its three differences are Ragdoll final (50 pixels,
max 6), Ragdoll ShadowAccum (1,348, max 8), and SceneTemplate Day ShadowAccum
(8, max 3). Nine of ten final images agree within tolerance across the two flows.

See [all-target JSON](../../T850/build/webgpu-runtime-closeout-20260915/all-target-comparison.json)
and the per-scene HTML/JSON reports under
[reports](../../T850/build/webgpu-runtime-closeout-20260915/reports).
The [flow/snapshot audit](../../T850/build/webgpu-runtime-closeout-20260915/flow-snapshot-audit.json)
confirms no WGSL selection in strict runs and no fallback events. Named shaders
use WGSL in auto; remaining translated stages are explicitly logged anonymous
HLSL helpers. Both manifests record the same Release executable SHA-256:
`D8913A49D9F53D807B0018FAFE5BE1D28C4D87A5564CB47B7D0C2A1005F6BC10`.

Skips: Nexus lacks `nexus_wars_terrain.glb` and `marine.glb` on all three APIs;
two authored Q3 Vulkan cases hit the existing 4-GiB capture-script guard. The
rounded reported memory is not proof that the GPU cannot run those scenes.

### Accepted Exceptions

- Ragdoll ShadowAccum: 11,739 pixels (1.27376%) outside tolerance 2, max 69;
  shadows disabled, SSAO enabled. User visually accepted the displayed pair.
  Its corresponding final image has 1,264 differing pixels, max 9.
- Quake3: one PBR silhouette pixel at zero-based `(926,221)`, max 255, spreading
  to 103 final pixels (0.01118%), max 194. ShadowAccum is byte-identical. The user
  accepted the displayed final image and isolated-pixel close-up.

The exact reviewed pairs and checkpoints remain documented under
[Accepted Visual Exceptions](shader-management.md#accepted-visual-exceptions).
They do not accept every target, future state, default-flow residual or unrelated
native change. No global tolerance was relaxed. CPU SSAO noise/kernel inputs were
verified identical, so different random seeds are not an established explanation.

### Native Controls and Remaining Caveat

The earlier isolated derivative-uniformity change had 228/232 native D3D12/Vulkan
target pairs byte-identical, 230 within tolerance; the only failures were one
D3D12 and two Vulkan Minecraft final pixels. Original references, source backup
and executable remain under
[uniformity-native-20260915](../../T850/build/uniformity-native-20260915).
That result applies to that change, not every subsequent shared shader edit.

Comparing the previous full native D3D12 checkpoint (`spirv-final-fixes-20260915`)
with this close-out gives 123/132 targets within tolerance, 102 byte-identical.
SSAO-related changes include Ragdoll final 2,807 pixels/max 9 and ShadowAccum
44,505/max 16; SceneTemplate Day final 38/max 3; Sandbox ShadowAccum 7/max 6.
Minecraft final changes at nine pixels/max 4. Saved snapshot properties match.

**Unaccepted native Voxel checkpoint delta:** the timed native backbuffer changes
at 309,535 pixels (33.5867%), max 245, with changes in Deferred and HDR_Final too.
The [before/after pair](../../T850/build/webgpu-runtime-closeout-20260915/reports/native-before-after/voxel-streaming/pair.png)
shows substantially changed lighting. Saved snapshots match, but the captures span
different build/shader checkpoints and do not establish the cause. This was not
causally isolated or approved. Do not report a clean native regression gate.
Current native/WebGPU agreement does not resolve this historical native difference.

Eight current Vulkan comparisons are retained as cross-API diagnostics:
52/100 targets within tolerance, 48 outside. Some final differences are large
(Day 25.27%, SceneTemplate Day 15.09%); these are not same-API before/after
regressions and have not been attributed to this work. Native Vulkan is not
Dawn's selected backend. No extra shader changes were made to force agreement.

## Reproduction

### PR Local CI Validation

Additional validation on 2026-09-15 reproduced the Windows and Android build
commands from [the GitHub workflow](../../.github/workflows/build.yml), using
the repository-recorded vcpkg revision. CI now checks out that revision on
Win32, ARM64, Android and Steam Deck too, instead of floating upstream HEAD;
x64 retains the explicit Dawn overlay pin. Native Windows install/clone failures
now stop their workflow steps explicitly.

| Additional PR gate | Result |
|---|---|
| Full Windows matrix | PASS: Win32, x64, ARM64, each Debug and Release; DayScene and T8ditor outputs verified |
| Local matrix self-tests | PASS: 56 each in Win32/x64 Debug/Release, 224 passing checks total |
| CPU-only shader CTests | PASS: 9/9 Debug and 9/9 Release, GPU tests excluded as on a fresh CI configuration |
| Registration, atlas, Launcher | PASS: source registration, 20 blocks/120 atlas face mappings, Windows PowerShell WPF/routing tests |
| Android Release | PASS: arm64-v8a and x86_64, development and production APKs; ABI contents and signatures verified |
| Workflow lint | PASS: checksum-verified actionlint 1.7.7; optional shellcheck/pyflakes integrations disabled |
| Steam Deck job | PASS on commit `851277e9`: hosted SteamRT build/package plus physical Deck runtime/editor smoke; details below |

The first local Win32 attempt failed because x86 vcpkg packages were absent.
Installing the workflow's exact package list resolved it; the subsequent complete
six-cell matrix passed without engine source fixes. Android used JDK 17, SDK 35,
build-tools 35.0.0, NDK 27.2.12479018, CMake 3.22.1 and Vulkan SDK 1.4.341.1.
Existing local signing was used without changing keys or publishing APKs.
No device install/run, CI-secret signing, artifact upload or tagged-release
packaging is implied. Builds reused installed dependencies and incremental output;
this is not a fresh hosted-runner VM simulation.

Local logs and ABI-separated APKs are in
`T850/build/webgpu-pr-local-ci-20260915`, excluded from the commit. Commands from
the source root after normal dependency/toolchain setup:

```powershell
.\scripts\ValidateBuildRegistration.ps1
python .\scripts\verify_minecraft_atlas.py
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File .\scripts\TestLauncherWebGPU.ps1 -Ui
.\scripts\RunWindowsBuildMatrix.ps1 -Action Build
ctest --test-dir .\build\dawn-package -C Debug -LE GPU --output-on-failure
ctest --test-dir .\build\dawn-package -C Release -LE GPU --output-on-failure
.\scripts\android\BuildAndroid.bat Release --allow-unsigned-release --abi arm64-v8a
.\scripts\android\BuildAndroid.bat Release --allow-unsigned-release --abi x86_64
```

These commands do not consume GitHub Actions minutes. GitHub currently documents
[free standard hosted runners for public repositories and free self-hosted runner usage](https://docs.github.com/en/billing/concepts/product-billing/github-actions).
This repository was public when checked. Larger runners and storage have separate
billing rules. A self-hosted runner can publish real Actions checks, but requires
deliberate setup and isolation from untrusted pull-request code; none was installed
or registered here. Local test results do not automatically satisfy required
GitHub check statuses. Windows MSBuild jobs are not reproduced by Linux-only `act`.

### Hosted CI and Steam Deck

The [hosted Build run for commit 851277e9](https://github.com/0Camus0/T850/actions/runs/35031344666)
completed successfully: registration, all six Windows cells, both Android ABIs,
and Steam Deck. The tag-only release job was skipped for the PR, as expected.

The actual uploaded Steam Deck runtime/editor package was tested on physical
SteamOS 3.8.10 hardware with RADV VANGOGH. Both executables resolved their shared
libraries, all 56 runtime self-tests passed, and native Vulkan ForwardScene and
editor captures rendered successfully. Required external scene assets were
supplied separately. The earlier environment-blocked row is superseded by this
evidence, not by a claim that the duplicate cold on-Deck source build completed;
that redundant build was deliberately cancelled. See the
[artifact hashes and bounded hardware evidence](https://github.com/0Camus0/T850/pull/38#issuecomment-5689162782).
This is native Vulkan validation, not WebGPU on Linux.

### Launcher and Shader Cache Follow-Up

Both launchers now offer a WebGPU-only source-flow selector (`auto` or `spirv`)
and a **Compile Shaders** button. The button uses the shipped Windows runtime,
not an external developer compiler, to populate the normal caches for D3D11,
D3D12, Vulkan, OpenGL and both WebGPU flows on x64. It preserves per-job logs and
results and supports cooperative cancellation between permutations. Non-x64
runtimes omit WebGPU; Android retains its offline APK shader task. The compile
mode explicitly rejects non-Windows hosts before renderer startup.

`--recordShaderPermutations` and the capture script's `-PermutationOutput` option
allow a bounded scene sweep to collect requests after startup, including streamed
shaders. The refreshed D3D12 sweep captured ten cases, skipped missing Nexus
models, and verified ten recorder flushes. It retained 254 old keys and added 27,
for **281 recorded permutations**. This is observed suite coverage, not every
possible feature-bit combination or unnamed runtime helper.

Follow-up validation on 2026-09-15:

- All six API/flow compiler jobs passed for the refreshed manifest through the
  real developer and portable WPF dialogs; cooperative cancellation was verified.
- The six-cell Windows build matrix and 224 Win32/x64 self-test checks passed.
- Android's offline shader task generated **576 SPIR-V outputs** successfully.
  This follow-up did not rebuild APK packages or repeat Android device testing.
- Launcher command/config/WPF/queue tests passed in PowerShell 7 and Windows
  PowerShell 5.1, including a final-review regression test for delayed completion
  output. Both launcher executables were rebuilt.
- Build registration, pinned Dawn audit, manifest checks, and documentation links
  passed. Missing manifests were rejected in Debug and Release.

Evidence is under `T850/build/shader-prewarm-20260915` and the runtime
`logs/shader-compile-*` directories. The hosted run above and Deck hardware test
belong to the earlier commit; new commits require their own hosted checks.
The native Voxel historical difference below remains unresolved and unaccepted.

### Runtime Commands

From the source root containing the solution:

```powershell
.\scripts\SetupDawn.ps1 -Mode Check
.\scripts\RunWindowsBuildMatrix.ps1 -Platforms x64,ARM64 -Configurations Debug,Release -Action Build
.\scripts\TestLauncherWebGPU.ps1 -Ui
cmake --build .\build\dawn-package --config Release
ctest --test-dir .\build\dawn-package -C Release --output-on-failure
Push-Location .\bin\x64\Release
.\DayScene.exe --game-selftest
.\DayScene.exe --api webgpu --shaderFlow auto --scene 1 --gui
.\DayScene.exe --api webgpu --shaderFlow spirv --scene 4 --sceneFile Scenes/ForwardScene.t8scene --gui
.\DayScene.exe --graphics-fixture --compare --shaderFlow spirv --output ..\..\..\build\webgpu-fixture-new
Pop-Location
```

Repeat the probe/fixture in Debug when validating compiler or driver changes.
The two interactive scene commands run until closed. The configured local probe
build enables GPU CTests; default CI configuration is CPU-only. Use the
[build guide](../development/windows-build-and-run.md#shader-compiler-probe) for
initial CMake/setup options. For new captures, use a fresh output root and the
[strict snapshot comparison workflow](../development/windows-build-and-run.md#strict-spir-v-visual-comparison).
Never overwrite accepted reference evidence to obtain a passing comparison.

## Continuation Status (2026-09-17)

This checkpoint reconciles the original seven-step plan with the later browser
work. Earlier dated sections describe their own checkpoints, not today's backlog.
The latest local implementation commit is `e00ea824`; it was not pushed during
that work. Public Minecraft was v0.1.2 at this checkpoint. The subsequent
v0.1.4 OnTop/isolation-guidance, v0.1.5 pointer-capture, v0.1.6 reduced-memory
cubemap and v0.1.7 camera-control deployments are recorded in the
[browser release notes](../platform/browser.md#published-wssi-demo); they do not
change the stage or incoming compute-PR handoff below.

| Original Step | Status | Evidence and Remaining Boundary |
| --- | --- | --- |
| 1. Dependency foundation | Complete | Pinned Dawn/D3D12, required Windows x64 build integration and package audits. |
| 2. Shader feasibility | Complete | Native in-process translation, reflection, cold/warm cache tests, and the later maintained WGSL path. This is not exhaustive future-material coverage. |
| 3. Driver lifecycle | Complete for milestone | Device/surface, submission, resize and bounded teardown/recreation tests. Full live-scene stress and device-loss recovery remain. |
| 4. Minimal graphics integration | Complete and exceeded | Indexed/textured/depth fixture, readback and reuse tests; normal forward/deferred runtime scenes also work. |
| 5. Shared compute | Awaiting incoming PR and rebase | Non-WebGPU compute is being implemented in a separate PR. WebGPU compute remains to be integrated against that work. |
| 6. Full scene and editor coverage | Partial | Runtime scene coverage exists; T8ditor, hosted surfaces, missing renderer features and full workflow acceptance remain. |
| 7. Release and measurements | Partial | Packaging, CPU-side comparisons and substantial optimization exist. GPU timestamp profiling, complete stress/portability gates and browser CI remain. |

### Compute Handoff

The owner confirmed on 2026-09-17 that non-WebGPU compute support is arriving in
another PR and will perform the rebase later. Do not rebase now, duplicate those
native implementations, or invent a competing shared compute abstraction.

After the owner rebases:

1. Inspect the incoming compute/resource/shader and render-graph contracts and
  their tests before editing WebGPU. Confirm which native backends and fallback
  behavior actually landed rather than assuming the entire original scope.
2. Add the WebGPU implementation using the incoming abstractions and existing
  WebGPU texture/buffer ownership. Cover pipeline creation, bindings, dispatch,
  storage usages and graphics/compute ordering without scene-specific branches.
3. Run the same blur/reference tests across supported native backends and
  WebGPU, including odd extents, resize, resource recreation and hazards. Keep
  GL's graphics fallback or explicit unsupported path; do not label it compute.
4. Add completion-valid GPU timestamps and matched CPU/GPU measurements so the
  original native-D3D12-versus-Dawn overhead goal can be closed with evidence.

GPU profiling and editor work can be scoped separately while waiting, but no
new implementation is authorized merely by this saved continuation note.

### Open Acceptance Work

- T8ditor API parsing/rendering, hosted/multiple surfaces, platform viewports,
  previews and editor workflows. The EDITOR guard remains enabled.
- Cube render targets, comparison samplers, true border-color sampling and
  render-target mip generation; loaded texture mip support already exists.
- Anonymous debug/helper strict-WGSL coverage and broader material/animation
  states. Resolve or explicitly review the historical native Voxel checkpoint
  lighting difference; later same-API cleanup passes do not close that older delta.
- WebGPU GPU timestamps, asynchronous telemetry and controlled GPU-overhead
  measurements. CPU-side comparison, uniform upload pooling and browser
  scheduling optimization are implemented, not future work.
- Full live-scene API switching/reload, device-loss recovery, save/reload,
  long-running memory/frame-time behavior, missing Nexus assets, guarded Vulkan
  cases, other GPUs and physical mobile-device coverage.
- Automated Emscripten build/browser CI and fresh hosted validation of subsequent
  commits. Native Windows ARM64, Android and Linux/Steam Deck Dawn ports remain
  separate future work; existing native Vulkan validation is not a WebGPU port.

### Browser Work Already Delivered

The Emscripten build, browser selection in both launchers, prepared shader
packages, local seven-scene smoke coverage, responsive Minecraft welcome,
touch controls, block-edit tests, BC/float-filtering fallbacks, restricted
Cloudflare demo and reusable JSON-configured deploy script are implemented.
See [browser implementation and evidence](../platform/browser.md). These were
later additions beyond the original plan, not unfinished prerequisites.

Remaining browser-specific work includes lower-limit GBuffer compatibility,
physical-device testing, full authoring/gameplay workflows and the deferred
public multi-scene launcher. The reported digging slowdown and `file:///`
security warning were not reliably reproduced or fixed; their diagnostic
probes remain available. Embedded-browser cross-origin isolation failures are
a separate capability/access issue, not evidence of the same runtime bug.

The PR commit includes implementation, maintained shader sources, pinned dependency
recipes, simplecpp sources/license, Launcher and documentation. Generated dependency
trees, build outputs/captures, recordings and personal runtime configuration are
excluded. The existing branch is retained; unrelated working-tree changes are
preserved. The tracked Launcher executable follows the repository's existing
packaging convention.