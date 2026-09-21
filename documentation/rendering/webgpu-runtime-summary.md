# WebGPU Runtime Handoff

Status: Windows x64 runtime close-out, 2026-09-15, with the ARM64/CI update below.
This summarizes the local work
from dependency setup through compiler, renderer, real scenes and final validation.
It is not a claim that the entire [WebGPU proposal](proposal-webgpu.md) is complete.

## ARM64 and CI Update, 2026-09-19

Windows ARM64 now uses the same Dawn-over-D3D12 runtime implementation as x64.
The pinned Dawn/ImGui overlays support `arm64-windows-static`; setup generates a
separate `build/dawn-package-arm64` audit/link contract, and MSBuild stages the
matching DXC runtime and notices. Compile guards, driver factory routing, shader
compiler/package code, ImGui WebGPU integration and self-tests include `_M_ARM64`.

GitHub CI builds ARM64 natively on `windows-11-arm`, installs/audits the ARM64
Dawn package, builds deterministic package/shader probes and runs ARM64 gameplay
self-tests. The Emscripten bundle remains architecture-neutral: CI builds it once
on x64 and runs that same artifact in native x64 and ARM64 Edge processes. Hosted
browser correctness uses explicitly labeled SwiftShader software WebGPU because
standard hosted runners have no hardware GPU; it is not hardware/performance evidence.
The dated x64-only statements below remain historical evidence for their original
checkpoints and are superseded for current platform availability by this section.

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

## Immediate Presentation Investigation, 2026-09-18

The pinned Dawn D3D swapchain creates immediate-mode swapchains with
`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`, but its `PresentDXGISwapChain` called
`Present(0, 0)`. The missing per-present `DXGI_PRESENT_ALLOW_TEARING` flag allows
independent-flip presentation to throttle to refresh even though the engine logs
`presentation=immediate`. Native D3D12 already passes the flag.

Overlay revision `20260219.200501#7` adds an exact-match, idempotent source patch
through the existing CMake overlay hook. It supplies the flag only for Immediate
mode while not in exclusive fullscreen, retaining FIFO/mailbox behavior. The
Windows HWND path disables DXGI Alt+Enter; the fullscreen guard also protects
other D3D surfaces. This follows the
[DXGI present requirements](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-present).
Use `scripts/SetupDawn.ps1 -Mode Install` and relink the renderer; installed-library
or build-tree hand edits are not the fix.

Before-patch PresentMon evidence under
`%LOCALAPPDATA%/T850Profiles/minecraft-compute-regression-20260918` records WebGPU
at 16.67 ms with Hardware Composed Independent Flip versus native D3D12 at 2.87 ms
with Composed Flip. Both use the same Release executable and 1920x1080 compute
arguments. The capture pins the test window visible but did not obtain foreground
focus; client-size queries in the PowerShell host are DPI-virtualized. A separate
WebGPU composed run reached 4.70 ms, so the slow path is presentation-dependent,
not proof of a universal 30 FPS shader cost.

The rebuilt revision passes the package audit and Release Framework/DayScene
build. `presentation-fixed-webgpu.csv` records 4.12 ms (about 242 FPS) versus
native D3D12 at 3.09 ms (about 324 FPS). Both final windows were focused, with
1920x1080 client areas, Hardware Composed Independent Flip, sync interval zero,
`PresentFlags=512`, and `AllowsTearing=1` throughout the seven-second samples.
The refresh-rate cap is absent. Earlier captures did not obtain foreground
focus, so these are short diagnostic comparisons, not a controlled claim of a
precise speedup or a return to the historical 500 FPS.

The real-driver comparison fixture passes presentation, resize, recreation and
readback at unchanged tolerance. All 71 shared tests pass. The tiled particle
readbacks pass on D3D11, D3D12, Vulkan, GL and both native WebGPU shader flows;
browser shader packages were not rebuilt or retested in this Windows fix.

The separate tiled torch optimization retains all three emitters and their visual
parameters. A short submit-only CPU capture changed from 4.36 to 3.04 ms at 1080p;
this is diagnostic evidence, not a repeated performance acceptance result or a
claim of restoring the historical 500 FPS in every view.

## Offscreen Follow-Up, 2026-09-18

The later R1 verification exposed an independent overlay bug: surface-format
ImGui pipelines were being used on the shared RGBA8 offscreen targets. Vulkan
and WebGPU adapters now select compatible pipelines at GUI frame boundaries,
waiting for prior work only when the target mode/format changes. WebGPU renderer
reinitialization preserves the ImGui context and SDL platform backend; Vulkan
recreates only the main pipeline. Shared offscreen ring rotation does not cause
per-frame pipeline rebuilds.

`WebGPUDriver::CompleteFrame` suppresses presentation in configured offscreen
mode and calls `CompleteOffscreenFrame` after submission. This restores shared
target rotation and the post-overlay `--offscreenDebug` capture path.

The [offscreen overlay regression](../testing/verification.md#offscreen-overlays)
passes on all five desktop APIs in Debug, plus captured Vulkan/WebGPU Release
runs and both strict WebGPU shader flows. The real-driver comparison fixture
passes both flows at its unchanged tolerance 2. Full-scene Vulkan/WebGPU captures
are nonuniform with readable overlays but are not pixel-identical: the Debug
frame-340 comparison measured maximum channel delta 44, mean delta 1.5725 and
57,290 of 230,400 pixels outside tolerance 2. That is recorded variance, not a
passing whole-scene parity claim. No tolerance or baseline was changed.

Evidence: `%LOCALAPPDATA%/T850Profiles/offscreen-overlay-20260918`, including
`captures-final/frame340-comparison.json`, fixture reports, native build matrix,
WebAssembly build/tests and both Android compile logs. SteamRT remains locally
unavailable; no new CI validation or editor-parity claim is made.

## Implementation

### Capability validation follow-up, 2026-09-18

Shared render-target validation now runs before graph allocations. Unsupported
cube/depth/mip requests no longer reach WebGPU `Require` sites during target
creation: direct creation returns failure, and graph creation either reports a
named capability failure or takes the explicit single-level mip fallback.
Unknown formats are rejected rather than substituted. WebGPU reports its
device's color-attachment limit and supports explicit single-channel F32 color.

Reflected comparison samplers fail at shader load with backend, shader, stage
and key in the diagnostic. Tint depth-texture metadata is retained as a distinct
resource kind so the sampler requirement can be diagnosed before layout
creation. This does not add comparison/depth sampler rendering support, and it
does not implement R3's general device-loss or frame-loop error handling.

Focused descriptor, capability, shader-flow, package and graphics-fixture checks
pass. The broader scene matrix still exposes Vulkan sampler teardown errors and
Minecraft overlay attachment incompatibility on Vulkan/WebGPU; the earlier
DayScene overlay fix is not a claim of all-scene coverage. R2 remains blocked on
that gate and unavailable SteamRT verification. See the
[R2 evidence record](webgpu-compute-remediation-plan.md#r2-reconcile-strict-versus-lenient-backend-behavior).

### Dependencies and Build Integration

- Pinned vcpkg `77df67cfff9c12ccfdb52284e07c87c75092f723`, Dawn
  `20260219.200501#7`, ImGui `1.92.7#1`, glslang 16.2.0 and simplecpp 1.9.1.
- [SetupDawn.ps1](../../T850/scripts/SetupDawn.ps1) provides Plan/Install/Check,
  package audits, generated link properties and compiler identity metadata.
  Ordinary Windows x64 and ARM64 builds require architecture-matched audits;
  native Vulkan stays separate.
- Installed-package probes exercise exported Dawn/Tint headers and libraries,
  not accidental build-tree dependencies. DX compiler DLLs and dependency licenses
  are deployed through build integration.
- MSBuild remains authoritative; Framework, ImGui and platform source lists are
  registered in MSBuild/filters and kept in CMake parity. Android remains a
  preprocessor/build portability target, not a native WebGPU runtime port.
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
- Both launchers preserve ordinary scene/config/snapshot controls, enforce x64/ARM64
  prerequisites and audit Dawn during builds. They now advertise forward/deferred
  runtime support while retaining the EDITOR guard. The driver reports deferred
  support, with a matching fixture assertion.
- Portable Launcher packaging was regenerated and copied to the four existing
  x64/ARM64 output folders, then the source-root developer Launcher was regenerated.
  Packaged mouse-click workflows remain outside automated validation.

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
The browser foundation was committed as `e00ea824`; the completed follow-ups
through v0.1.7 were committed as `bf387f83`. Neither was pushed during this work.
Public Minecraft was v0.1.2 at the original checkpoint. The subsequent
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
| 5. Shared compute | Awaiting PR #40 and rebase | The current PR includes native WebGPU/Dawn as well as D3D11/D3D12/Vulkan/desktop GL compute. Browser integration and combined-tree acceptance remain. |
| 6. Full scene and editor coverage | Partial | Runtime scene coverage exists; T8ditor, hosted surfaces, missing renderer features and full workflow acceptance remain. |
| 7. Release and measurements | Partial | Packaging, CPU-side comparisons and substantial optimization exist. GPU timestamp profiling, complete stress/portability gates and browser CI remain. |

### Compute Handoff

The owner initially expected non-WebGPU compute from another PR and will perform
the rebase later. Inspection of PR #40 at `594d5cd1` supersedes that scope
assumption: it also includes native WebGPU/Dawn and desktop OpenGL 4.3+ compute.
Do not rebase now, duplicate those implementations, or invent a competing shared
compute abstraction. Emscripten support is not established by native Dawn tests.

After the owner rebases:

1. Inspect the incoming compute/resource/shader and render-graph contracts and
  their tests before editing WebGPU. Confirm which native backends and fallback
  behavior actually landed rather than assuming the entire original scope.
2. Reconcile the incoming WebGPU implementation with our browser shader packages,
  texture/buffer ownership and asynchronous submission model. Cover pipeline
  creation, bindings, dispatch, storage usages and graphics/compute ordering
  without scene-specific branches.
3. Run the same blur/reference tests across supported native backends and
  WebGPU, including odd extents, resize, resource recreation and hazards. Keep
  GL 3.3/ES graphics fallback distinct from the new desktop GL 4.3+ compute path.
4. Add completion-valid GPU timestamps and matched CPU/GPU measurements so the
  original native-D3D12-versus-Dawn overhead goal can be closed with evidence.

GPU profiling and editor work can be scoped separately while waiting, but no
new implementation is authorized merely by this saved continuation note.

### Post-Merge Particle Depth and Camera Fixes (2026-09-17)

The user found two regressions after integration. The torch kernel projected
particles without reading scene depth on any API. Its graph now binds
`GBuffer:DEPTH`; HLSL/GLSL use an unfiltered per-pixel reversed-Z comparison and
reject particles outside clip depth. The portable layout and Dawn reflection
probe include the added sampled texture. Browser shader packages were regenerated.

The merged FPS jump fix treated collision overclip's small positive vertical
velocity as upward jump motion, skipping the grounding probe and oscillating
camera height. A new idle assertion reproduced `idle FPS controller lost ground
contact` before the correction. The post-movement probe now uses pre-collision
ascent as well as resolved velocity, and confirmed ground contact zeros vertical
velocity. Jump ascent and landing tests remain green.

Validation: exact production-kernel particle depth readbacks (visible, hidden,
partial, near/far clip) passed all five native APIs plus Chrome/Firefox. Wasm
shared tests and x64/ARM64 Debug/Release builds passed, including native x64
self-tests. Full-scene 640x360 frame-61 compute captures passed all five native
APIs and were visually inspected. Chrome Compute measured zero idle-height drift
over 62 samples/600 frames; feature-removed Chrome touch measured zero over 60
samples, and Firefox Raster zero over 61. Touch/OnTop/capture recovery and camera
controls passed with actual BC and float32 filtering disabled.

Evidence: `%LOCALAPPDATA%/T850Profiles/particle-depth-camera-20260917`.
Early browser runs hit an old runtime/new graph mismatch and a stale server
catalog; rebuild and restart the local server after changed shader exports.
The first matrix launch used the wrong working directory for scene assets;
`windows-matrix-final.log` is the successful four-cell result. The first mobile
run's 360-pixel height cap conflicted with its OnTop assertion; the final
`chrome-mobile-viewport-final` report uses viewport-sized limits and passed.
Physical phones remain untested. These fixes are local and uncommitted; no push
or deployment occurred, and production remains v0.1.7.

### PR #40 Integration Completed (2026-09-17)

Rebased `microsoft_daniel_branch` onto the merged upstream
`91af57aaef3cad4787e90e1744ac7907d53d892f`. The two local web commits are now
`4712f86f` and `c3ece587`. All rebase conflicts are resolved. Follow-up browser
compute integration remains as working-tree changes; nothing was pushed or
deployed. Production remains v0.1.7. The local config was restored byte-for-byte
from its external backup; the pre-rebase stash is retained as a backup.

The final PR head `e63096be` passed the required upstream CI before merge,
including the corrected 56-DWORD TorchParticles ABI test. The earlier review
below concerns the older `594d5cd1` revision, not current CI status.

Integration preserves the context-owned uniform pages, epochs, cached graphics
bindings, completion-tracked buffer reuse, browser scheduling, 512-face no-BC
fallback, optional float filtering, touch/OnTop and View/InvertY controls.
Compute uses that same uniform/submission owner, including standalone dispatch.
Compute buffers now retire through the existing pool; sampled texture and sampler
layouts select nonfiltering variants when required by the actual bound resource.
Prepared shader package version 2 carries compute workgroups/storage formats;
native export and browser loading share source/entry/defines/layout identity.
Normal BuildWeb exports all seven scenes in both modes and the correctness kernels.
Both launchers forward Raster/Compute to the browser. Minecraft-only packaging
retains the authored skin without widening the model/scene allowlist.

Verified locally on the RTX 4080 Laptop development host:

- x64 and ARM64 Debug/Release builds: four cells passed; x64 shared tests passed.
  ARM64 is compile-only on this host.
- Native arithmetic and image correctness: D3D11, D3D12, Vulkan, GL and WebGPU
  passed. Chrome 152 and Firefox 156 passed the same GPU readback tests, including
  1x1, 7x5 and 257x129 textures.
- Wasm shared tests, compute package identity/metadata tests, 11 Node web tests,
  launcher WPF/config/routing tests in PowerShell 7 and 5.1 passed. Both launcher
  executables were regenerated.
- Chrome compute Minecraft passed touch, capture-error recovery, OnTop rotation,
  block edits and camera controls with actual BC and float32 filtering removal.
  Firefox compute mouse/camera/GUI/resize and default Raster input passed.
- Browser compute telemetry: Minecraft 1,800 dispatches and DayScene 3,000 over
  600 complete frames each, maximum three pending submissions; free-buffer pool
  remained below 32 MiB. This is bounded coverage, not a phone OOM guarantee.
- Native graphics fixture passed uniform rollover, readback, resize and renderer
  recreation comparisons. Matched 640x360 frame-61 DayScene raster/compute dumps
  were byte-identical for all 18 targets on each of D3D12 and WebGPU.
- Minecraft's GBuffer and shadows matched between modes; its compute-only torch
  particle layer changed the deferred/final result on both APIs (5,349 final
  pixels above tolerance 2). The graph intentionally has no raster particle draw.
  Cross-API compute final differences were 609 pixels/max delta 11 for Minecraft
  and 294 pixels/max delta 4 for DayScene; this is not pixel-exact cross-API parity.
- Restricted Pages preparation: 698 resources, 42 existing R2 routes, no pending
  uploads. Local Node SVG MIME handling was corrected and regression-tested.

Evidence: `%LOCALAPPDATA%/T850Profiles/compute-rebase-20260917`, including build
logs, browser reports/telemetry and `captures/` render-target HTML comparisons.
Physical phones, Android/SteamRT runtime and new hosted CI for this local tree
were not run. Historical native Voxel lighting differences remain unresolved;
this integration does not close that older comparison. No R2 objects/settings,
public deployment, or remote Git branches changed.

### Historical PR #40 Rebase Assessment (2026-09-17)

This is a source/conflict assessment, not a completed rebase or a runtime
validation of the combined code. No PR comments, approvals, remote writes,
branch switches, merges, stashes or rebases were performed. Git object-only
merge simulations left HEAD, index and the working tree unchanged.

| Checkpoint | SHA / State |
| --- | --- |
| Our local release commit | `bf387f8303867b76fc8fb1b886e1d3023bbe75b7` on `microsoft_daniel_branch` |
| Common ancestor | `8e31f4aab07b2b7a425890790e2bf1505b05def8` |
| Intended future upstream | `origin/microsoft_webgpu_branch`, `70327f39fcd7140b00d0445521bd22ddf432b683` |
| Incoming branch | `origin/microsoft_compute_shaders`, `594d5cd1e00baf605127bd44d22df240289bebf7` |
| PR | [#40: Add cross-backend compute shaders](https://github.com/0Camus0/T850/pull/40), open, mergeable, checks unstable at inspection |
| CI | [Build 35280243567](https://github.com/0Camus0/T850/actions/runs/35280243567): x64 Debug/Release failed; registration, Win32/ARM64 and both Android ABIs/Steam Deck passed; tag-only release skipped |

Re-fetch before acting: a newer PR revision or merge strategy may change every
conflict prediction below. The final rebase target is the real
`origin/microsoft_webgpu_branch` after PR #40 lands, not the temporary compute
branch. At this checkpoint our replay range contains exactly `e00ea824` followed
by `bf387f83`; upstream's PR #39 merge already contains their parent.

#### Findings to Resolve or Track Upstream

1. **High: current x64 CI is deterministically failing.**
   [ShaderProbe.cpp:508](https://github.com/0Camus0/T850/blob/594d5cd1e00baf605127bd44d22df240289bebf7/T850/cmake/dawn-package/ShaderProbe.cpp#L508)
   expects 28 DWORDs for TorchParticles, while
   [the HLSL constant block](https://github.com/0Camus0/T850/blob/594d5cd1e00baf605127bd44d22df240289bebf7/T850/Assets/Shaders/CS_TorchParticles.hlsl#L4)
   contains a float4x4 plus ten float4 values: 56 DWORDs / 224 bytes. Both failed
   jobs report `CS_TorchParticles.hlsl: unexpected ComputeV1 constant layout`.
   Correct the expectation/ABI consistently and rerun both checks; this is not
   a runner or dependency-download failure.
2. **High: storage allocation can prevent the promised raster fallback.**
   `RenderGraph::CreateRenderTargets` passes `rt.storage` directly to `CreateRT`
   before `CreateComputePipelines` checks mode/capability.
   [D3D11RT.cpp:103](https://github.com/0Camus0/T850/blob/594d5cd1e00baf605127bd44d22df240289bebf7/T850/Framework/src/video/d3d11/D3D11RT.cpp#L103)
   unconditionally adds the UAV bind flag for these targets, then exits on
   texture creation failure or returns false on UAV creation failure. Thus an
   unsupported storage format can fail startup even in raster mode. Gate storage
   allocation or retry a non-storage target consistently with graph selection;
   validate with missing typed-UAV format support, not only the development GPU.
3. **Medium: the compute self-test cannot validate non-Windows ports.**
   [App.cpp:129](https://github.com/0Camus0/T850/blob/594d5cd1e00baf605127bd44d22df240289bebf7/T850/DayScene/App.cpp#L129)
   rejects every non-Windows `--compute-selftest`, including explicit Vulkan.
   Preserve the useful native harness, but adapt host selection/lifecycle before
   counting Linux/Deck or browser compute execution as validated. Android/Deck
   CI compilation does not close this runtime coverage gap.
4. **Hardware risk: Vulkan queue selection changes without a present check.**
   [VulkanDriver.cpp:475](https://github.com/0Camus0/T850/blob/594d5cd1e00baf605127bd44d22df240289bebf7/T850/Framework/src/video/vulkan/VulkanDriver.cpp#L475)
   prefers a graphics+compute queue and assumes the same family can present.
   Verify surface support or preserve separate presentation ownership before
   claiming broader GPU compatibility. This was inspected, not reproduced on
   split-queue hardware during this assessment.

Do not apply automated review suggestions mechanically. For example, the claim
that all GL samplers default to texture unit zero is contradicted by the shipped
`CS_Bright.glsl` declarations at bindings 1/2 and `GLCompute` binding the textures
to those units. General independent sampler-state support is a separate question.
This review did not certify all 116 changed files or reproduce the PR author's
reported GPU matrix; it checked CI evidence and the integration-critical paths.

#### Predicted Conflicts

The branches change 102 and 116 files from their common ancestor, with 20
overlapping paths. The combined-tree simulation identifies six conflicted paths;
the exact first-commit replay identifies five (all except Minecraft's include).
The second replay still depends on how those first conflicts are resolved.

| Path | Resolution Direction |
| --- | --- |
| `T850/Framework/src/video/webgpu/WebGPUDriver.cpp` | Highest risk. Keep our `WebGPUContext` uniform pages, `UniformEpoch`, dynamic offsets, cached bind entries, bounded queue and completion-based retirement. Integrate incoming compute/storage methods and RT signature; do not choose the entire incoming file or retain two competing uniform allocators. Both `<tuple>` and `<type_traits>` are needed. |
| `T850/DayScene/App.cpp` | Combine the incoming compute-test AppBase selection/result with our `OS_WEB` framework branch, worker main loop and browser startup/error lifecycle. Do not drop the normal runtime or substitute the test app. |
| `T850/DayScene/MinecraftScene.cpp` | Combined conflict is the Emscripten include versus removal of an empty Vulkan conditional. Retain the browser include. Verify auto-merged torch/skin work alongside our View/InvertY/gamepad logic and scene state. |
| `T850/scripts/Launcher.ps1` | Retain both the browser selector/build/open logic and the new compute/raster selector plus scroll container. They collide at the same XAML insertion point. Check variable lookup, persistence and argument routing, not just XML validity. |
| `T850/scripts/Launcher_Release.ps1` | Same combined UI/routing treatment as the developer launcher, preserving portable browser discovery. |
| `T850/T850Launcher.exe` | Binary conflict: regenerate from the resolved launcher source after tests; never select an arbitrary side as the final artifact. |

Local generated evidence is under `T850/build/pr40-rebase-scope/`:
`merge-tree.txt` (combined tree `b7d199a011e19855c0c491e19ceae76dbbe71e73`) and
`first-replayed-commit.txt` (tree `d8acbfd29d5c1bd78181c58009f5d79624ca7831`).
These conflict-marked trees are diagnostic objects, not runnable source or refs.

#### Clean-Merge Integration Work

- **Browser shader compilation:** `WebGPUComputePipeline::Create` always calls
  `LoadShaderFiles`, whose implementation is Windows-x64-only. Unlike graphics,
  it has no `ReadShaderPackage` branch. The browser build has no Tint/glslang
  runtime. Add native compute-package export and Wasm package loading with the
  full `ComputeV1` identity, defines, entry point, workgroup size and storage
  formats. Regenerate all WebShaders with matching artifact hashes/schema; a
  raster-only setting does not remove compiled references to unavailable code.
- **Uniform and compute-buffer lifetime:** incoming compute dispatch allocates
  transient uniform buffers each time and bypasses our pooled upload path.
  Route constants and submission through the existing owner and audit compute
  buffer destruction/early failures. Preserve the rollover/reuse regression
  tests that fixed browser memory growth and stale per-draw constants.
- **Readback:** incoming `ReadComputeBuffer` uses direct queue submission and
  `MapAsync(WaitAnyOnly)` plus blocking `WaitAny`. Adapt it to our asynchronous
  browser completion path and submission/retirement bookkeeping. Do not add a
  blocking wait to the browser frame loop.
- **Optional filtering:** the incoming compute pipeline always declares sampled
  textures as `Float` and samplers as `Filtering`. Our no-float32-filterable
  depth/data path requires matching non-filtering layouts. Carry that distinction
  into compute (especially depth-consuming God Rays), and rerun the actual
  no-BC/no-float-feature tests rather than only checking adapter capabilities.
- **Asset allowlist:** the new authored `voxel_world.mob.skin_texture` is
  `herobrine_green.png`. Calling our selector with the incoming scene currently
  rejects `Textures/herobrine_green.png`. Add this declared dependency and its
  test while retaining the exclusion of unrelated models/scenes. The incoming
  code has an atlas fallback, but that is not the intended new skin appearance.
- **Configuration:** incoming default `postProcessMode` is raster. Explicitly
  route compute/raster through browser URLs, shell argument parsing and browser
  launcher construction before advertising the selector there. The current
  browser path has no forwarding for that option. Do not silently change mobile
  defaults or call a raster/clear fallback successful compute execution.
- **Self-tests and registrations:** retain both branches' project/CMake source
  additions and test cases. The Windows-only shader probe must test 56 torch
  DWORDs; browser tests need compute package and dispatch/readback coverage. The
  newly auto-merged `ShaderArtifact` fields require matching export/import data.
- **Mobile budget:** the new RGBA16F torch layer is an additional screen-sized
  allocation even when its fallback is selected. Measure startup peak, resize
  and OnTop at phone limits before deployment; our 512-face cubemap reduction
  does not budget the rest of the render graph or per-pixel particle workload.
- **Local state:** incoming PR changes tracked `T850/config.json`, while our
  working copy has separate personal settings. Back up/preserve them before the
  eventual rebase and review the intended repository defaults independently.
  Do not commit the local config, vcpkg working state, screenshot or videos.

#### Execution Order After Merge Approval

1. Re-fetch the actual target after PR #40 merges and record its SHA/status.
   Preserve dirty local settings without dropping files. Inspect the new replay
   range again; at this snapshot it is just `e00ea824`, then `bf387f83`.
2. Rebase onto `origin/microsoft_webgpu_branch`, not the compute feature branch.
   Resolve the driver/entry point first, then the paired launchers and the small
   scene conflict. Rebuild the launcher binary from the resolved sources.
3. Restore a compiling browser graphics path before enabling browser compute:
   package export/load, resource ownership and no-filterable-float layouts are
   prerequisites, not optional cleanup. Keep existing graphics tests intact.
4. Run registration and launcher PS5.1/PS7 routing/WPF tests; native x64
   Debug/Release compute tests at 1x1, 7x5 and 257x129; the corrected Dawn compute
   probe; shader precompilation and native API regression captures. Require fresh
   Windows/Android/SteamRT CI for the integrated SHA.
5. Run a clean Emscripten build/export, Wasm compute correctness/readback,
   compute/raster image comparisons on Minecraft and DayScene, and the existing
   touch/OnTop/View/InvertY/capture-error/no-BC tests. Measure frame-time and
   memory under repeated compute/graphics transitions, reload and resize.
6. Only after those gates pass, consider a separate authorized deployment.
   Current production v0.1.7 was not changed by this preparatory review.

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
- Hardware-GPU browser CI and fresh hardware validation of subsequent commits.
  The hosted x64/ARM64 browser jobs use explicitly labeled SwiftShader correctness
  coverage. Android and Linux/Steam Deck Dawn ports remain separate future work;
  existing native Vulkan validation is not a WebGPU port.

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