# Windows Setup, Build, and Run

Status: verified against source and scripts on 2026-08-19.

This is the primary local development workflow. T850 uses Visual Studio/MSBuild on Windows; CMake is maintained for Android and Steam Deck and is not the normal Windows build entry point.

## Roots

Commands in this document distinguish two directories:

- repository root: `F:\T850` in the current workspace; contains `LaunchSolution.bat`, `documentation/`, and Android/Steam Deck entry points;
- source root: `F:\T850\T850`; contains `T850.sln`, projects, `Assets/`, `scripts/`, and build outputs.

When a command says "from the source root", first run:

```powershell
Set-Location F:\T850\T850
```

## Prerequisites

Required for Windows builds:

- Visual Studio 2022 Community, Professional, Enterprise, or Build Tools;
- Desktop development with C++ workload;
- MSVC v143 toolset, including Host x64 to ARM64 tools for ARM64 builds;
- Windows SDK;
- Git;
- Windows PowerShell 5+ or PowerShell 7.
- CMake 3.21 or newer on PATH for the required x64 Dawn package probe/link metadata.

Internet access is required for first-time vcpkg setup and cloud assets. The setup script pins vcpkg to Visual Studio 2022 so dependencies use the same v143 ABI as the projects.

## First-Time Setup

From the repository root:

```powershell
.\LaunchSolution.bat --setup-only
```

This command:

1. finds Visual Studio 2022 C++ tools;
2. sets `VCPKG_VISUAL_STUDIO_PATH` to that VS 2022 installation;
3. clones/bootstrap vcpkg under `T850\Librerias\vcpkg` when needed;
4. installs x64 dependencies and ImGui backends;
5. downloads the runtime model and texture sets;
6. exits without opening Visual Studio.

Useful variants:

```powershell
.\LaunchSolution.bat                 # setup x64, download assets, open T850.sln
.\LaunchSolution.bat --x86           # also provision x86 dependencies
.\LaunchSolution.bat --arm64         # also provision ARM64 dependencies
.\LaunchSolution.bat --all           # provision x64, x86, and ARM64
.\LaunchSolution.bat --skip          # skip vcpkg, download assets, open solution
.\LaunchSolution.bat --skip-assets   # skip cloud asset download
.\LaunchSolution.bat --assets-only   # download runtime assets and exit
.\LaunchSolution.bat --all-models --assets-only
```

Stop if the script reports that Visual Studio 2022 C++ tools are missing. Do not let vcpkg silently select a newer Visual Studio toolset.

## Dawn Dependency Foundation

Windows x64 setup and builds now require the pinned Dawn/D3D12 package. Dependency,
shader compilation, graphics fixtures and normal forward/deferred DayScene runtime
rendering with `--api webgpu` are implemented. T8ditor is not supported yet.
Win32 and ARM64 builds retain their existing dependencies; native Vulkan is
unchanged. The standard setup command calls [SetupDawn.ps1](../../T850/scripts/SetupDawn.ps1)
automatically for x64.

From the source root:

```powershell
.\scripts\SetupDawn.ps1 -Mode Plan   # inspect the package plan without installing
.\scripts\SetupDawn.ps1              # install/audit packages and generate link properties
.\scripts\SetupDawn.ps1 -Mode Check  # read-only build prerequisite check
```

The tracked [overlays](../../T850/cmake/vcpkg-overlays/RequirePinnedVcpkg.cmake)
pin vcpkg to `77df67cfff9c12ccfdb52284e07c87c75092f723`, Dawn to
`20260219.200501#6` and ImGui to `1.92.7#1`. The Dawn overlay includes guarded
Tint matrix-transpose and operand-usage fixes. ImGui's WebGPU dependency disables
Dawn's default features and requests D3D12 explicitly. The Dawn overlay does not
offer D3D11, Vulkan, GL or Metal features. Upstream's internal null/test backend
remains compiled; the probe requests D3D12 and rejects software adapters, so it
is not a fallback renderer.

The Dawn overlay enables Tint's SPIR-V reader and WGSL reader/writer, installs
the linkable translator and its headers, fixes upstream's duplicated Tint header
directory, and installs Dawn's shared utility headers required by Tint. glslang
is an explicit setup dependency. The translator links through `dawn::webgpu_dawn`;
the optional Tint command-line tool is not required.

`Bootstrap` clones and checks out the pin only when creating a new vcpkg tree.
An existing different revision or modified upstream Dawn/ImGui port stops setup
without overwriting it. Use a separate fresh checkout or deliberately reconcile
that dependency revision; the script does not silently upgrade other packages.
Do not run concurrent installs in the same vcpkg root.

### Link Contract

The [package probe project](../../T850/cmake/dawn-package/CMakeLists.txt) uses
`find_package(Dawn CONFIG REQUIRED)` and `dawn::webgpu_dawn`. CMake resolves all
transitive libraries and generator expressions. Setup reads the resolved
Debug/Release link inputs through the documented
[CMake File API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html),
then writes local MSBuild property files under `build/dawn-package`. It does not
parse CMake expressions or maintain a separate Abseil library list.

[DawnPackage.targets](../../T850/cmake/DawnPackage.targets) is imported by Framework,
FrameworkImGui, DayScene and the editor host targets. It validates x64 prerequisites,
consumes the generated link properties, and stages `dxcompiler.dll`, `dxil.dll`
and license notices for executable outputs. These DLLs remain required even
with the static Dawn triplet. The audit records the revision, package versions,
features, ABI metadata hashes, recipe hashes, checkout location and generated
link-property hashes; rerun setup after those inputs change or the checkout moves.
Setup also generates/audits `T850DawnShaderConfig.h` from Dawn/glslang package ABIs
and translator sources. Rerun setup after changing the translator to refresh its
cache identity. This does not rebuild unchanged dependency packages.
Generated properties, audit files and downloaded package content are not committed.

The CMake engine build uses the same pinned package and exported target directly.
Turning off automatic dependency installation does not disable Dawn; configuration
still requires the installed package audit to pass. CI provisions it for x64,
builds the probe without claiming hardware execution, and retains runtime DLLs
and licenses in release artifacts.

All Windows DayScene and editor-host builds also stage the architecture-matched
`vulkan-1.dll` and `licenses/vulkan-loader.txt` from vcpkg. The Vulkan loader is
dynamic even in `*-windows-static` triplets and is imported at process startup,
including for CPU-only `--game-selftest` runs. Do not rely on a GPU driver or SDK
installation to supply it. PR CI verifies the staged files and runs the gameplay
and terrain self-tests for Win32/x64 Debug and Release, reporting captured output
and the native process exit code; ARM64 is cross-build-only on the x64 runner.

### Probe and Validation

```powershell
cmake --build .\build\dawn-package --config Debug --target DawnPackageProbe DawnMSBuildProbe --parallel 4
cmake --build .\build\dawn-package --config Release --target DawnPackageProbe DawnMSBuildProbe --parallel 4
Push-Location .\build\dawn-package\Release
.\DawnPackageProbe.exe
.\DawnMSBuildProbe.exe
Pop-Location
```

The first target links directly through CMake; the second uses only the generated
MSBuild properties. Both compile the same probe source. Each explicitly requests
a hardware D3D12 adapter, prints adapter limits, creates a device, waits for an
empty queue submission with a bounded timeout, checks callbacks and tears down.
It does not create a surface, render a frame or translate an engine shader.

Local 2026-09-14 evidence: both probes passed in Debug and Release on an NVIDIA
RTX 4080 Laptop GPU. Adapter limits included 8 color attachments, 128 attachment
bytes/sample, 48 sampled textures/stage, 16 samplers/stage and 256-byte uniform
offset alignment. These are adapter capabilities, not full-scene parity or proof
that an engine device has requested those limits.

The normal x64 and ARM64 Debug/Release solution builds passed. Release self-tests
and sequential D3D11/D3D12/Vulkan/GL DayScene smoke captures passed. A forced
missing-metadata test failed x64 with setup guidance; Win32/ARM64 correctly skipped
that prerequisite. Full Win32 execution/build and remote CI were not validated in
this step; CMake engine parity was configured, while executable probe builds used
its VS2022 generator. Those are step-one results; the shader gate is described below.

### Shader Compiler Probe

From the source root, after setup:

```powershell
cmake --build .\build\dawn-package --config Debug --target DawnShaderProbe ShaderPreprocessorProbe --parallel 4
ctest --test-dir .\build\dawn-package -C Debug --output-on-failure
cmake --build .\build\dawn-package --config Release --target DawnShaderProbe ShaderPreprocessorProbe --parallel 4
ctest --test-dir .\build\dawn-package -C Release --output-on-failure
.\build\dawn-package\Release\DawnShaderProbe.exe .\Assets\Shaders .\build\dawn-package\hardware-cache gpu
```

Without a test-mode argument, the probe now uses the same WGSL-first/fallback
policy as `LoadShaderFiles`. Explicit flow selection is available without rebuilding:

```powershell
$probe = ".\build\dawn-package\Release\DawnShaderProbe.exe"
& $probe .\Assets\Shaders .\build\dawn-package\flow-cache
& $probe .\Assets\Shaders .\build\dawn-package\flow-cache flow auto
& $probe .\Assets\Shaders .\build\dawn-package\flow-cache flow wgsl VS_tri
& $probe .\Assets\Shaders .\build\dawn-package\flow-cache flow spirv VS_tri
```

The optional final argument limits the run to one stage; otherwise the 17 default
stages are loaded. A stem, `.wgsl` or `.hlsl` filename is accepted. `auto` tries
WGSL then HLSL/SPIR-V on a load/preparation failure. `wgsl` and `spirv` are strict
and never switch to the other path. Unknown modes fail instead of silently using
a default. Any unhandled stage failure makes the command exit nonzero; the output
retains each stage's actual flow, fallback status/reason and cache/timing data.
The existing `test`, `cold`, `warm`, `gpu`, `permutations` and `corpus` modes remain
explicit regression modes that check both implementations, not flow benchmarks.

Use the forced modes for comparisons. A fresh dedicated cache root gives a cold
load per language; rerunning the same command measures a warm load. Cache entries
remain language-qualified, and timings report failed WGSL work before successful
fallback. These commands measure CPU shader preparation, not frame performance.
Both flows still require valid sources and supported shader constructs. The known
derivative-uniformity failures are fixed as of 2026-09-15: default stages and the
recorded corpus now pass strict HLSL/SPIR-V/Tint and WGSL validation. See
[corrections and native per-target image checks](../rendering/shader-management.md#corrections-and-native-image-checks).
Normal DayScene accepts `--shaderFlow auto|wgsl|spirv` before
loading; see [runtime selection](runtime-configuration.md#webgpu-shader-flow).
T8ditor WebGPU support is still pending. Selecting a strict flow does not bypass
shader compatibility failures.

By default CTest runs nine CPU-only checks, including all 17 HLSL/WGSL stage
counterparts, cache corruption/invalidation, fresh-process warm loads, 1,027 vertex
feature combinations, 257 recorded/additional graphics permutations, default-flow
loading and isolated fallback/override tests. Native
HLSL and WGSL resource/constant/output contracts are compared without rendering.
CI builds/runs these tests for x64 Debug/Release; remote CI was not executed locally.
The reset fixture touches only its generated test cache. `test` and `gpu` use
isolated cache subfolders; do not pass a production cache as the probe's cache root.

Enable the hardware-only CTests locally (shader numerics and surface lifecycle):

```powershell
cmake -S .\cmake\dawn-package -B .\build\dawn-package -DT850_SHADER_GPU_TESTS=ON
cmake --build .\build\dawn-package --config Release --target DawnShaderProbe DawnSurfaceProbe --parallel 4
ctest --test-dir .\build\dawn-package -C Release -L GPU --output-on-failure
```

This runs native HLSL/D3D11 and direct WGSL/Dawn/D3D12 on Dawn's exact DXGI adapter:
60 blur fixtures against each other and a CPU reference, an intentional-regression
mutation, and 12,288 production-function input/mode cases across mesh/fullscreen
shaders. No window or screenshots are required. The option is OFF by default so
GPU-less CI does not claim a hardware pass. See
[coverage and limitations](../rendering/shader-management.md#automated-hlslwgsl-tests).

The vendored 0BSD simplecpp preprocessor is included in normal MSBuild, desktop
CMake and Android source lists. A standalone test target is available without Dawn:

```powershell
cmake -S .\cmake\dawn-package -B .\build\preprocessor-arm64 -G "Visual Studio 17 2022" -A ARM64 -DT850_PREPROCESSOR_ONLY=ON
cmake --build .\build\preprocessor-arm64 --config Release --target ShaderPreprocessorProbe
cmake -S .\cmake\dawn-package -B .\build\preprocessor-android -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$env:LOCALAPPDATA/Android/Sdk/ndk/27.2.12479018/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DT850_PREPROCESSOR_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .\build\preprocessor-android --target ShaderPreprocessorProbe
```

Both standalone ARM64 cross-builds passed locally. No Android device was attached,
so Android execution and a full APK build were not validated for this detour.
Windows ARM64 execution was not tested on this x64 host. This portability result
does not expand the Windows x64/D3D12-only Dawn runtime scope.

Stage-two direct-WGSL detour, 2026-09-14: all eight CTest checks passed in Debug
and Release, including hardware numerics. The earlier translator/package results
and sizes below are historical measurements before this detour, not new direct-WGSL
size or performance measurements.

Flow-selection follow-up on 2026-09-14: the seven focused selection/cache/GPU
checks passed in Debug and Release, along with normal x64 and ARM64 Debug/Release
builds and the Release self-tests. The unchanged preprocessor and large permutation
sweeps were not rerun for this policy-only change. No shader sources were modified.

Step-two installed-package evidence on 2026-09-14: revision 4 setup and its
read-only audit passed. All three probes built with installed headers/libraries
only in Debug/Release, and all four CTest checks passed in each configuration.
Triangle/text graphics pipelines and the blur compute pipeline were accepted by
Dawn/D3D12 on the RTX 4080 Laptop GPU in both configurations; native SM5 and WGSL
constants matched at offsets 0/4/8/12. Both device-link probes also passed.
Normal x64 Debug/Release solution builds and all 54 Release self-tests passed.
ARM64 Debug/Release Framework/DayScene builds passed with the platform-guarded
source registered. Remote CI, full Win32 validation and a full CMake engine build
were not rerun for this slice; the executable compiler probes use CMake and the
normal Windows engine builds use MSBuild.
At that earlier gate the WebGPU driver, full shader inventory and compute readback
were still pending. The detour now adds the 17 WGSL sources and the bounded
numerical/readback checks above; full rendering and scene parity remain pending.

Release file sizes from that local build (bytes):

| Output | Step-one Baseline | Step-two Build |
|---|---:|---:|
| DayScene.exe | 11,109,376 | 11,109,888 |
| T8ditor.exe | 10,789,376 | 10,789,888 |
| DawnPackageProbe.exe | 6,237,696 | 9,062,400 |
| DawnShaderProbe.exe | Not present | 11,200,000 |

The device probe grew by 2,824,704 bytes across package revisions. The shader
probe adds 2,137,600 bytes over the current device probe, but also includes
glslang, cache/reflection utilities and test code: neither delta isolates the
translator's marginal cost in the final engine. The compiler is registered in
Framework but not called by the renderer yet, so the 512-byte engine executable
changes do not demonstrate negligible translator cost. The installed Release
`webgpu_dawn.lib` is 2,566,962,230 bytes; this is a build archive, not a shipped
runtime payload. The required DXC DLL staging remains unchanged. Full runtime
memory and final integrated package overhead remain unmeasured.

## Integrated WebGPU Fixture

The first stage-three graphics milestone uses
[WebGPUDriver](../../T850/Framework/src/video/webgpu/WebGPUDriver.cpp), not a
backend-private drawing sample. The shared
[Windows driver factory](../../T850/Framework/src/video/WindowsDriverFactory.cpp)
is used by Win32Framework and by the
[graphics fixture](../../T850/Framework/src/debug/GraphicsFixture.cpp). The fixture
creates resources and draws through BaseDriver, Device, DeviceContext, ShaderBase,
VertexBuffer, IndexBuffer, ConstantBuffer, Texture and BaseRT.

After a normal x64 build, run from the output directory:

```powershell
Push-Location .\bin\x64\Release
.\DayScene.exe --graphics-fixture --api webgpu
.\DayScene.exe --graphics-fixture --api webgpu --shaderFlow spirv
.\DayScene.exe --graphics-fixture --compare
Pop-Location
```

`--shaderFlow auto|wgsl|spirv` selects the same policy as the shader compiler;
`auto` is the default. The driver logs the requested/actual source flow and cache
state. Select it before creating shader objects. `--compare` runs native D3D12,
WebGPU, then native D3D12 in the same process, fully destroying/recreating the
drivers and fixture resources between runs. It requires identical adapter LUIDs.
This is fixture-level recreation through the shared factory, not yet an ordinary
scene `Win32Framework::ChangeAPI` or benchmark-matrix transition.

Each run draws a near red indexed triangle and a larger, farther blue checker
triangle to an RGBA8/Depth32Float target. It checks reverse-depth rejection,
per-draw buffer updates, texture sampling, render-to-texture presentation, resize
from 320x240 to 257x193, and an offscreen submission without presentation. PPM
readback checks the center and expected nonblank regions. Paired captures must
have zero RGB channels differing by more than 2/255. Output defaults to a unique
`graphics-fixture/<run>` directory under the working directory; `--output PATH`
selects an explicit location and also holds the isolated shader cache.

The separate `DawnSurfaceProbe` tests hardware D3D12 device/surface creation,
clear/present, resize, zero-size suspend/resume, offscreen submission and repeated
teardown. Callback and queue waits are bounded to 30 seconds; driver/device errors
are reported rather than substituting another API. GPU tests require a local
interactive Windows session and suitable hardware. CI compiles the surface probe
but does not enable hardware execution by default.

Local 2026-09-14 evidence: x64 Debug/Release and ARM64 Debug/Release builds passed;
the paired fixture passed in both x64 configurations on the RTX 4080 Laptop GPU
(LUID 91966), with zero channels outside tolerance at both sizes. Forced SPIR-V
rendering passed in Release. Surface lifecycle and shader numeric tests passed in
Debug/Release. All 54 Release self-tests and sequential D3D11/D3D12/Vulkan/GL
ordinary-scene smoke captures passed. ARM64 does not include the Dawn driver.
Remote CI, full Win32, Android APK, and full CMake-engine execution were not tested
for this slice.

The original fixture's resource subset has been extended for the normal forward
scene and mixed-format MRT tests described below. Multiple color attachments,
HDR/single-channel targets and depth-only targets are implemented. The driver
still conservatively reports no complete deferred support because scene parity
is not closed. Render-target mip generation and cube depth targets remain
unsupported. Buffer updates allocate GPU snapshots;
ring allocation and steady-state performance work remain pending. FIFO presentation
and fixture waits make these correctness tests, not an overhead benchmark.

The fixture remains an explicit developer test only; neither Windows launcher
substitutes it for normal startup. Full scene/editor parity, device-loss recovery
and shared graphics/compute graph integration remain pending. No full-scene or
three-day integrated-compute acceptance gate is claimed.

### First Normal WebGPU Scene

Step 4 now runs an authored scene through the normal Windows application loop,
SceneTemplate, PrimitiveManager/RenderMesh, material binding and RenderGraph:

```powershell
Push-Location .\bin\x64\Release
.\DayScene.exe --api webgpu --scene 4 --sceneFile Scenes/ForwardScene.t8scene --width 1280 --height 720 --gui
Pop-Location
```

In the Launcher, select Windows x64, WebGPU, SceneTemplate and
`Scenes/ForwardScene.t8scene`, then use ordinary RUN. The launcher does not select
or rewrite the scene automatically. EDITOR remains guarded and full deferred
scenes such as `DayScene.t8scene` are not supported by this driver yet.

[ForwardScene.t8scene](../../T850/Assets/Scenes/ForwardScene.t8scene) loads the
existing DamagedHelmet glTF model and an authored camera. Its
[shared graph](../../T850/Assets/Scenes/ForwardScene_RenderGraph.json) draws the
material into a single RGBA8/depth target and presents it with the existing
fullscreen pass. `DEFAULT_PASS` explicitly names the existing mesh pass zero,
which includes opaque materials; the existing `FORWARD_PASS` transparent-subset
filter is unchanged. The same scene and graph run on native D3D12 without
scene-level WebGPU branches or an alternative mesh renderer.

Integration added named and anonymous shader-call support, 2D/cube texture
uploads with source mips, RGBA16F/RGBA32F IBL uploads, BC1/2/3 upload support,
single-channel font textures, and native-equivalent material mips/samplers.
D3D12 and WebGPU share the existing alpha-weighted CPU mip algorithm. Optional
float32 filtering and BC features are requested when supported; attempting to use
those resources without the required feature fails explicitly. Absent optional
scene-depth/color inputs at slots 7/9 receive zero-valued bindings; missing required
material textures and active-attachment aliasing still fail.

The upstream ImGui WebGPU backend now drives the main runtime window, including
loading frames, fonts and runtime controls. This does not implement T8ditor,
multi-window ImGui, depth/cube previews or complete editor preview behavior.
Normal Clear starts a frame as on native D3D12. FrameDumper can capture the
backbuffer during a frame, submit pending work, and continue drawing afterward.
Anonymous HLSL debug shaders use the existing in-memory SPIR-V translator and log
that choice; strict WGSL mode rejects them because no paired WGSL source exists.

Local validation on 2026-09-14, NVIDIA RTX 4080 Laptop GPU:

- Full x64 Debug/Release and ARM64 Framework/DayScene Debug/Release builds passed.
- All 55 Release self-tests passed, including alpha-weighted, odd-sized,
	single-column and six-face mip regression cases.
- The normal forward scene's native D3D12 and WebGPU backbuffer captures were
	byte-identical at 640x480 in Release/Debug and at 257x193 in Release. Native
	output was also byte-identical before/after extracting the mip helper.
- The existing visual gate reported two captures, no skips and no failures per
	paired run. Existing SceneTemplate/DayScene content passed capture gates on
	D3D11, D3D12, Vulkan and GL.
- The main runtime UI completed 60 frames and explicit teardown without errors.
	A separate normal-window-close run passed after repeated in-frame captures.
- The driver comparison passed native D3D12 -> WebGPU -> native D3D12 recreation,
	RT resize, offscreen submission and in-frame screenshot followed by more draws.

The current Windows host only updates input state on OS window-resize events;
changing the outer window size did not resize the renderer in the live test.
Do not count that as live scene/swapchain resize validation. Driver-level resize
is tested separately. Full scene reload/API switching, other GPUs, remote CI and
full CMake-engine/Android execution were not validated for this step.

Repeat the normal scene capture gate through the existing workflow:

```powershell
.\scripts\CaptureVisualBaselines.ps1 -RunSet candidate -Cases scene-template-forward -Apis webgpu,d3d12 -Width 640 -Height 480 -DumpSeconds 0.1 -OutputRoot .\build\webgpu-scene-validation -Force
```

These results validate one static normal-mapped material/IBL scene, not every
shader permutation or full renderer parity. `WGPU-SHADER-01` remains open; no
shader source or diagnostic-severity setting was changed in this step.
WebGPU benchmark-matrix requests fail explicitly rather than selecting D3D11.

### Mixed-Format MRT Follow-Up

Windows x64 WebGPU now implements the existing per-attachment format contract:
`RGBA8` (`RGB8` expanded to RGBA8), `RGBA16F`, `RGBA32F`, `R8` and `F16`, with
optional F32 depth or a depth-only target. Attachments may have different formats
in the same pass. Every attachment participates in render-pass setup, pipeline
cache identity, zero-RGBA clearing, load preservation and alias checks. Device
attachment limits are queried and explicitly requested; the tested adapter reports
eight color attachments and 128 attachment bytes per sample. Dawn validates the
format combination against those limits; no graph formats are silently replaced
to fit a smaller budget.

HDR and depth readbacks preserve float values internally. `ReadRTColorFloat`
returns the first texel as in native D3D12, without clamping HDR or negative values.
PPM captures alone clamp to the displayable range and replicate single channels
into RGB. All color attachments and F32 depth can be captured through FrameDumper.

Depth sampling has an API-specific constraint: `Depth32Float` cannot use a
filterable-float binding. Nearest reads use an unfilterable-float binding and
non-filtering sampler. Filtered reads use a lazily created R32Float sampling copy,
filled on the GPU with `textureLoad` from depth and refreshed after depth clears
or writes. This requires `float32-filterable`, preserves the authored filter
choice, and costs an additional texture and copy draw when dirty. Viewport and
scissor state survive this internal pass break. This is a correctness
implementation, not a measured performance result.

Depth textures retain the native sampler flag convention; border mode takes
precedence over nearest filtering. White-border addressing itself is not yet
emulated: WebGPU uses clamp-to-edge. That remaining difference is recorded in
`WGPU-RENDER-02` rather than silently claimed as full sampler parity.

Local 2026-09-14 validation:

- Full x64 Debug/Release and ARM64 Framework/DayScene Debug/Release builds passed;
	all 55 Release self-tests passed.
- Existing driver comparison tests passed in Debug/Release with seven distinct
	MRT outputs, reversed format ordering, unclamped HDR/single-channel readbacks,
	zero-alpha clears, load preservation and nonzero-attachment alias rejection.
- Depth-only draws, nearest/filtered depth sampling, refresh after clearing and
	scissor preservation passed against native D3D12. The new inline-HLSL tests run
	in `auto`/`spirv` test modes and explicitly skip in strict `wgsl`; normal Sandbox
	captures separately exercise the direct WGSL shaders.
- The unchanged Sandbox scene and graph now complete with no runtime validation
	errors and produce captures: Release 640x480 and Debug 257x193. The previously
	accepted forward scene remains byte-identical to native D3D12 at 257x193.

**Sandbox visual parity is still open.** At Release 640x480, the final image has
15,641/307,200 pixels (5.09%) outside a 2/255 channel tolerance. Depth captures
match exactly; the GBuffer has at most one pixel outside tolerance per attachment.
The larger differences begin in shadow accumulation and deferred lighting, then
propagate into post-processing. The capture gate succeeding means nonblank output,
normal exit and no validation errors, not a passing image comparison. Track the
remaining work as `WGPU-RENDER-02` in
[Shader Management](../rendering/shader-management.md#open-follow-up-sandbox-deferred-parity).
This does not close full scene/editor support or `WGPU-SHADER-01`.

```powershell
# Normal scene, unchanged graph and production shaders:
.\scripts\CaptureVisualBaselines.ps1 -RunSet candidate -Cases sandbox -Apis webgpu,d3d12 -Width 640 -Height 480 -DumpSeconds 0.1 -OutputRoot .\build\webgpu-mrt-sandbox-validation -KeepRawDumps -Force
# Driver contract regression only, never a Launcher substitute:
Push-Location .\bin\x64\Release
.\DayScene.exe --graphics-fixture --compare --output ..\..\..\build\webgpu-mrt-depth-regression
Pop-Location
```

At this historical MRT checkpoint neither the Launcher nor the selected
scene/graph was modified. The later runtime close-out updates the Launcher to
advertise forward/deferred runtime support, while keeping the editor unavailable.
Remote CI, other GPUs, full CMake engine execution and Android execution were not
run for this follow-up; ARM64 validation is compile-only without Dawn.

### Strict SPIR-V Visual Comparison

The capture workflow accepts `-ShaderFlow auto|wgsl|spirv` (default `auto`) and
passes the choice only to WebGPU runs. `-ReplayApi d3d12` selects a native snapshot
as the replay source for every requested API; it requires `-ReplayFromRunSet`.
Actual arguments, source flow and snapshot path are recorded for successful
captures. `-KeepRawDumps` now also copies all target PPMs into each case directory.

Use a fresh output root and compare native and WebGPU replays of the same snapshot:

```powershell
.\scripts\CaptureVisualBaselines.ps1 -RunSet reference -Cases sandbox -Apis d3d12 -OutputRoot .\build\spirv-parity -KeepRawDumps
.\scripts\CaptureVisualBaselines.ps1 -RunSet candidate -Cases sandbox -Apis d3d12,webgpu -ShaderFlow spirv -ReplayFromRunSet reference -ReplayApi d3d12 -OutputRoot .\build\spirv-parity -KeepRawDumps
python .\scripts\compare_dumps.py .\build\spirv-parity\candidate\sandbox\d3d12 .\build\spirv-parity\candidate\sandbox\webgpu --tolerance 2 --report .\build\spirv-parity\reports\sandbox
```

Initial 2026-09-15 matrix result (before compiler/renderer fixes): nine strict-SPIR-V cases rendered, but all nine final
images failed comparison; 95/127 individual targets differed beyond tolerance.
VoxelScene failed at required texture binding 6, and Nexus assets were unavailable.
Forward/Sandbox repeated SPIR-V captures are deterministic and visibly different
from native, while direct-WGSL controls are much closer. Details and the local
paired-image index are recorded in
[WGPU-RENDER-03](../rendering/shader-management.md#open-follow-up-translated-shader-rendering).
These results do not undo the passing native HLSL before/after correctness tests;
they reveal translated-shader integration problems those tests did not cover.

## Command-Line Builds

Use `scripts\build.ps1` from the source root. It selects the correct solution platform, builds the full solution, and returns MSBuild's exit code. `-Action` accepts `Build` or `Rebuild` and defaults to `Rebuild`.

```powershell
.\scripts\build.ps1 -Config Debug   -Platform x64
.\scripts\build.ps1 -Config Release -Platform x64
.\scripts\build.ps1 -Config Debug   -Platform ARM64
.\scripts\build.ps1 -Config Release -Platform ARM64
.\scripts\build.ps1 -Config Debug   -Platform x86
```

Use the same incremental action as GitHub Actions:

```powershell
.\scripts\build.ps1 -Config Debug -Platform Win32 -Action Build
```

Run the exact local Windows PR/CI matrix, including source-registration validation, all six configuration/platform cells, both executables, and Win32/x64 self-tests:

```powershell
.\scripts\RunWindowsBuildMatrix.ps1
```

Platform mapping:

| Script value | Solution platform | Executable output |
|---|---|---|
| `x64` | `x64` | `bin\x64\<Config>\` |
| `x86` | `Win32` | `bin\Win32\<Config>\` |
| `ARM64` | `ARM64` | `bin\ARM64\<Config>\` |

The script defaults to logical processor count minus one. Override it when memory pressure or CI limits require fewer workers:

```powershell
$env:T850_BUILD_WORKERS = '6'
.\scripts\build.ps1 -Config Release -Platform x64
```

A successful full solution build produces:

```text
Lib/<Config>/<Platform>/Framework.lib
Lib/<Config>/<Platform>/FrameworkImGui.lib
bin/<Platform>/<Config>/DayScene.exe
bin/<Platform>/<Config>/T8ditor.exe
```

Post-build steps create output-directory junctions for `Shaders`, `Models`, `Fonts`, `Textures`, `Scenes`, and `Layouts`, and copy required DLLs/configuration. Run executables with their output directory as the working directory.

## Visual Studio and Launcher

Open the solution after setup:

```powershell
.\LaunchSolution.bat --skip --skip-assets
```

Run the developer WPF launcher from the source root:

```powershell
.\scripts\Launcher.ps1
```

The launcher can:

- select Windows or Android target;
- select architecture/configuration;
- build or rebuild;
- select graphics API, scene, model or `.t8scene`, resolution, and fullscreen;
- select raster or compute render-graph execution;
- configure culling, dumps/replay, logging, telemetry, D3D12 debug, and benchmark mode;
- download missing cloud assets;
- launch DayScene or T8ditor;
- install and deploy the Android app when Android is selected.

The launcher writes `config.json`. Runtime command-line arguments override values loaded from that file. Its Build/Rebuild buttons invoke `scripts\build.ps1`, the same entry point used by GitHub Actions. Windows output lookup uses `Win32`, `x64`, and `ARM64` exactly as MSBuild emits them.

### WebGPU Launcher Selection

Both Windows launchers also have a **Compile Shaders** button in the Graphics API
section. It compiles every entry in `Shaders/shader_permutations.json` through
D3D11, D3D12, Vulkan and OpenGL, plus WebGPU `auto` and `spirv` on x64. It uses the
selected architecture/configuration in the developer launcher and the adjacent
DayScene executable in the portable launcher. No scene assets or development
compiler tools are required beyond the shipped shaders, manifest and runtime.
The engine itself must include the `--compileShaders` mode. This mode currently
requires Windows; non-Windows runtimes reject it before renderer startup.

The modal progress window runs one API/flow job at a time, supports cancellation,
and retains per-job output/error logs and a completion `summary.json` under the
runtime's `logs/shader-compile-<timestamp>` directory. A failed API is reported as
a failure; cancellation finishes the current shader before exiting so it does
not interrupt a cache write. Cancellation stops the remaining jobs. Otherwise,
a compilation failure does not stop the remaining jobs. WebGPU is omitted on non-x64
runtimes. The button is disabled for the Android target; APK builds already run
their own offline SPIR-V compilation task using the same manifest.

This prepares the normal local shader caches, reusing valid existing entries.
It does not enumerate every possible 64-bit feature combination, compile unnamed
runtime helpers, or produce portable GPU-specific pipeline binaries. New shader
requests not represented in the manifest retain their normal runtime fallback.
See [refreshing the permutation list](../rendering/shader-management.md#refreshing-and-compiling-permutations).

Both the developer and portable Windows launchers offer **WebGPU (Dawn/D3D12)**
for Windows x64 and persist the selected API. RUN and EDITOR use their normal
argument builders with `--api webgpu`; the launcher does not remap that selection
to native D3D12, inject `--graphics-fixture`, or launch an alternative workload.
EDITOR remains explicitly unavailable until its CLI and renderer support WebGPU;
the existing editor parser would otherwise ignore that API and use native D3D12.
Scene, snapshot, logging and telemetry controls retain their regular behavior,
including the normal executable and asset prerequisites. The portable launcher
checks the adjacent DayScene executable's PE architecture, not the host's bitness.

**WebGPU supports forward and deferred runtime scenes on Windows x64; editor
support is unavailable.** The Launcher displays that scope and retains normal
controls and startup arguments. Existing native API routing is unchanged. See
the [runtime handoff](../rendering/webgpu-runtime-summary.md) for measured image
differences, accepted exceptions and remaining coverage gaps.

Selecting WebGPU reveals the **Shader Flow** dropdown in both launchers:

- **WGSL preferred (auto)**: the default, preferring named WGSL sources with HLSL
	translation available for missing sources and anonymous helpers.
- **SPIR-V (HLSL translation)**: strict HLSL -> glslang/SPIR-V -> Tint/WGSL.

The choice is saved as `webgpuShaderFlow` and appears as `--shaderFlow` in the
runtime command preview. It takes effect on the next RUN without rebuilding the
engine. Switching away from WebGPU hides the control and omits the argument while
retaining the choice. Unsupported targets disable the selector. Old configs
without a supported value default to `auto`. Strict `wgsl` is CLI-only until
anonymous HLSL helpers have WGSL counterparts.

Automatic fixture capture directories and completion UI remain removed; the
selector uses normal scene startup. Explicit command-line developer tests remain
available. See
[Shader Flow Selection](../rendering/shader-management.md#shader-flow-selection).

Both launchers also expose **Post-process Mode** for every desktop runtime API.
It defaults to **Raster** and is saved as `postProcessMode`; every RUN command
includes `--postProcessMode raster|compute`. Select **Compute** to enable each
supported render-graph compute pass, including Minecraft torch particles.

Developer Build/Rebuild preflight now checks CMake availability and runs
`SetupDawn.ps1 -Mode Check` for every Windows x64 build, regardless of selected API.
Missing or stale Dawn packages/metadata offer the existing logged setup workflow
through `SetupDawn.ps1 -Mode Install`. Non-x64 builds skip the Dawn audit.

Hardware-free command, config, architecture, prerequisite, mocked Dawn-preflight
and normal WPF control tests run in Windows CI:

```powershell
.\scripts\TestLauncherWebGPU.ps1 -Ui
```

The tests passed locally on 2026-09-15 under PowerShell 7 and Windows PowerShell
5.1. They reject fixture substitution and preserve normal scene arguments and
controls, verify both shader choices, live preview changes, visibility and config
round trips, and keep config writes in temporary files. They do not establish
WebGPU scene rendering or automate packaged-EXE mouse clicks.

Both launchers also offer **WebGPU + Browser (Emscripten)** with an installed
browser dropdown, hidden for native APIs. The developer launcher exposes
BUILD WEB / REBUILD WEB and honors Debug/Release; the portable launcher runs a
prepared bundle. See [Browser Runtime](../platform/browser.md#build-and-run) for
prerequisites, clean-build behavior, browser discovery, and the opt-in launcher
build integration test. Windows PowerShell 5.1 launcher command/WPF tests and
fresh Release plus Debug/Release configuration-switch builds passed on 2026-09-16.

## Run DayScene

Run from the chosen output directory:

```powershell
Set-Location .\bin\x64\Release
.\DayScene.exe --api d3d11 --scene 0 --model Models/DamagedHelmet.glb
```

Graphics API values are `d3d11`, `d3d12`, `gl`, and `vulkan`.

Scene indices:

| Index | Host |
|---:|---|
| 0 | Sandbox model or scene viewer |
| 1 | DayScene runtime demo |
| 2 | Quake3Mock |
| 3 | RagdollEditor |
| 4 | SceneTemplate authored `.t8scene` runtime |
| 5 | VoxelScene generated mutable terrain runtime |

Examples:

```powershell
.\DayScene.exe --api d3d12 --scene 1 --width 1920 --height 1080
.\DayScene.exe --api gl --scene 3 --model Models/Tyrant.glb
.\DayScene.exe --api d3d11 --scene 4 --sceneFile Scenes/DayScene.t8scene
.\DayScene.exe --api d3d11 --scene 4 --sceneFile Scenes/Q3/q3dm6_mod_3_jolt.t8scene --gui
.\DayScene.exe --api d3d12 --scene 5 --width 1280 --height 720
```

Print the authoritative runtime option list from the built binary:

```powershell
.\DayScene.exe --help
```

## Run T8ditor

T8ditor defaults to D3D12 but accepts all four Windows backends:

```powershell
Set-Location .\bin\x64\Release
.\T8ditor.exe --api d3d12 --width 1920 --height 1080
.\T8ditor.exe --api d3d11 --sceneFile Scenes/Q3/q3dm6_mod_3_jolt.t8scene
.\T8ditor.exe --api vulkan --mesh Models/DamagedHelmet.glb
```

Supported editor arguments:

```text
--api d3d11|d3d12|vulkan|gl
--width N --height N
--mesh PATH
--sceneFile PATH | --t8scene PATH
--dump-frame N | --dumpFrame N
--logLevel error|info|debug|verbose|trace|0..4
--logFile PATH
--d3d12debug
```

## Windows Deployment and CI Artifacts

There is no separate local Windows deployment script. A runnable local output is the corresponding `bin\<Platform>\<Config>` directory plus its copied DLLs, `config.json`, and the asset junction targets.

The tag-triggered GitHub Actions release job is the authoritative distributable packaging path. It:

- downloads Release artifacts;
- builds `T850Launcher.exe` with `scripts\build_launcher_release.ps1`;
- stages executables/DLLs, launcher, tracked lightweight assets, cloud downloader support, manifest, and config;
- emits one ZIP per Windows Release artifact;
- includes Android APKs and the Steam Deck tarball;
- publishes all files on a `v*` Git tag.

Do not hand-copy only `DayScene.exe`; missing DLLs or assets will make the package incomplete.

## Build-System Parity Rule

Windows uses `.vcxproj`; Android and Steam Deck use CMake. Every new Framework source must be added to all of:

```text
Framework/Framework.vcxproj
Framework/Framework.vcxproj.filters
Framework/CMakeLists.txt
```

Android has a separate native source list in `cmake/AndroidBuild.cmake`; DayScene additions must also be present in `DayScene/CMakeLists.txt` and that Android list. Enforce the maintained gameplay/terrain/mutable-mesh contracts with:

```powershell
.\scripts\ValidateBuildRegistration.ps1
```

Never edit CMake merely to hide a Windows linker error. Find the missing project entry or dependency.

## Common Failures

| Symptom | Check |
|---|---|
| vcpkg libraries reference a newer MSVC runtime | Re-run `LaunchSolution.bat`; confirm it prints a VS 2022 v143 path |
| ARM64 MSBuild not found | Install Host x64 to ARM64 compiler tools in VS 2022 |
| executable starts but assets are missing | Build through the project so post-build junctions run; check output `Models`, `Scenes`, and `Shaders` |
| model/scene path fails | Use resource-relative paths such as `Models/Foo.glb` and `Scenes/Foo.t8scene` |
| launcher says assets are missing | Click Download Assets or run the cloud download scripts |
| a new source links on Windows but not Android/Steam Deck | Add it to `Framework/CMakeLists.txt` |
| a new source is ignored by Visual Studio | Add it to the owning `.vcxproj`; filters affect organization only |

## Related Documents

- [Runtime configuration and CLI](runtime-configuration.md)
- [Cloud asset workflow](cloud-assets.md)
- [Verification and test matrix](../testing/verification.md)
- [Visual dumps and regression comparison](../debug/visual-regression.md)
- [Android build and deployment](../platform/android.md)
- [Steam Deck build and deployment](../platform/steam-deck.md)
