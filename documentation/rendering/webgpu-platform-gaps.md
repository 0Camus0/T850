# WebGPU Platform Dependency Gaps

Status: remaining platform work, reviewed 2026-09-22. No Android, Linux or Steam
Deck Dawn package build or device probe has been validated. This document
extends the [WebGPU implementation record](proposal-webgpu.md) with
dependency-foundation gaps only; it does not claim renderer support.

## Scope and Backend Decision

The current [Windows foundation](../development/windows-build-and-run.md#dawn-dependency-foundation)
uses Dawn/D3D12 and is required for Windows x64 and ARM64. The pinned overlays,
architecture-specific static triplets, package audits and native CI cover both.
Android and native Linux/Steam
Deck cannot use that D3D12 path. Their proposed foundation would use **Dawn over
Vulkan**, requiring explicit approval to expand the current platform/backend
scope. Windows would remain Dawn/D3D12 only, not gain a Dawn/Vulkan option.

Once approved, the dependency would become part of normal setup/build/package
for each supported target, not a manual backend installation. Existing native
Vulkan remains a separate engine API with its own resources and allocator.
Dawn must own its own Vulkan device/resources; this work does not wrap or reuse
T850's `VulkanDriver` objects. If D3D12-only remains a global restriction, these
native platform ports cannot proceed.

## Shared Foundation Gaps

- Extend the pinned Dawn/ImGui overlays with platform-specific Vulkan features.
  The current overlays restrict WebGPU to Windows x64/ARM64 D3D12. Do not simply enable
  upstream defaults: audit the resolved feature set and prevent ImGui from
  pulling unintended Dawn backends. Preserve the existing Windows package gate.
- Separate portable pin/audit metadata from the Windows-only setup script.
  Reuse revision, feature, ABI and license checks, but link through CMake imported
  targets directly; these platforms do not need generated MSBuild properties.
- Build target libraries with the platform toolchain. Optional Tint command-line
  tools run on the build host, not on Android or an incompatible target sysroot.
  The future in-process translator needs target-built Tint/glslang libraries;
  its SPIR-V-reader/export and conversion work remains a separate shader gate.
- Adapt the package probe to request Vulkan explicitly, log the adapter and
  actual limits, create/destroy a device, and verify bounded queue completion
  and error handling. Never count software fallback as a hardware pass.
- Cache by platform, ABI, toolchain/sysroot, dependency pin, overlay content and
  configuration. Keep Windows, Android and Linux artifacts separate. A successful
  native Vulkan engine run does not prove Dawn's Vulkan feature/limit support.

## 1. Android

Current entry points: [SetupAndroidToolchain.bat](../../SetupAndroidToolchain.bat),
[AndroidBuild.cmake](../../T850/cmake/AndroidBuild.cmake), and the
[Android platform guide](../platform/android.md). The current build supports
`arm64-v8a` / `arm64-android` and `x86_64` / `x64-android`, with minimum API 28.
Its setup pins NDK `27.2.12479018`; compatibility with the selected Dawn revision
still needs to be proven.

| Gap | Work Needed |
|---|---|
| Cross-compiled packages | Add Vulkan-only Dawn and the ImGui WebGPU binding to Android provisioning. Build both ABIs with matching NDK, API level, C++ runtime, PIC and configuration; do not reuse Windows libraries. Verify Dawn's own Android prerequisites before accepting the pin. |
| CMake and native library | Add required package discovery/linkage to the separate Android CMake path and register the probe entry point in the APK's native code. Check transitive symbol resolution and compatible libc++ linkage across all packaged native libraries. |
| Runtime deployment | Resolve Vulkan through the Android platform loader/driver; do not ship a desktop loader or vendor driver. Audit additional `.so` dependencies, ABI placement, native-library/page-size alignment and license payloads using the existing APK packaging flow. |
| Probe execution | Run a small probe in an Android app context, using logcat for adapter/limits, validation messages and completion status. Test a physical arm64 device; label x86_64 emulator results separately and record its actual GPU backend. |
| Automation | Add per-ABI package/link checks and APK artifact checks, plus device tests where available. Preserve existing signing and native Vulkan regression workflows. |

**Foundation acceptance:** clean Debug/Release dependency and APK builds for each
claimed ABI; install/launch the probe without developer library paths; explicit
Dawn/Vulkan identity, no unexpected fallback, no validation errors, bounded
completion and repeated device teardown. Confirm native Vulkan still builds and
runs. Record device/driver, NDK/API level and package sizes; an emulator-only pass
does not establish physical-device compatibility.

**Not included:** a WebGPU `ANativeWindow` surface, NativeActivity window-loss and
resume handling, orientation/resize, render-graph support, shader conversion or
scene parity. Those need later lifecycle/rendering milestones before advertising
WebGPU in the Android launcher. Main risks are NDK/Dawn compatibility, mobile
driver limits, APK/runtime-library size and memory pressure.

## 2. Linux and Steam Deck

Current entry points: [CMakeLists.txt](../../T850/CMakeLists.txt),
[BuildSteamRuntime.sh](../../T850/steamdeck/BuildSteamRuntime.sh), and the
[Steam Deck platform guide](../platform/steam-deck.md). The native desktop path is
x86_64 Linux; its generated vcpkg manifest already has a Linux ImGui overlay that
selects SDL3 Vulkan, Wayland and X11 features.

| Gap | Work Needed |
|---|---|
| Package/overlay integration | Add Vulkan-only Dawn and ImGui WebGPU support to the existing Linux manifest. Merge with its SDL3 overlay rather than replacing it. Preserve native Vulkan and the existing editor dependencies; select X11/Wayland support explicitly for the intended runtime. |
| Toolchain and ABI | Build Dawn and its dependencies inside the same pinned SteamRT/container toolchain as the engine. Match compiler, libc++/libstdc++ choice, glibc baseline, PIC and configurations. A library built on a newer desktop distribution is not proof it can run on Deck. |
| CMake and probes | Use Dawn's imported CMake target directly and add a Linux version of the device/queue probe. Verify the pin's Vulkan and window-system prerequisites; no Windows SDK, DXC DLL staging or MSBuild bridge should enter this target. |
| Deployment | Extend runtime packaging for actual shared-library dependencies, relative search paths and notices. Use the runtime's supported Vulkan loader and host-provided GPU driver integration; do not bundle a vendor ICD or replace SteamOS system libraries. Audit the package without development paths. |
| Validation environments | Test a declared desktop Linux baseline and real Deck hardware separately. For Deck, run within the supported SteamRT environment and in desktop/game-mode launch contexts, recording driver/runtime identity rather than treating all Linux builds as interchangeable. |

**Foundation acceptance:** clean Debug/Release builds in the selected toolchain,
dependency inspection with no unresolved libraries, explicit hardware
Dawn/Vulkan device creation, bounded queue completion and repeated teardown with
zero validation errors. The packaged probe must run on the stated Linux baseline
and Deck without the build tree; native Vulkan runtime/editor smoke tests remain
passing. Report unsupported drivers or container GPU-access failures as blocked,
not as successful software-fallback runs.

**Not included:** SDL X11/Wayland WebGPU surfaces, Gamescope presentation,
suspend/resume and swapchain recovery, launcher API routing, shader translation,
or scene/editor parity. The current Deck wrapper forces native Vulkan; a probe
can be launched separately, but selectable WebGPU requires later wrapper and
platform-host work. Main risks are SteamRT compiler/runtime compatibility,
driver feature limits and distribution-specific loader/library resolution.

## Suggested Execution Gate

For each platform, first prove the pinned dependency build and target-device
probe, then integrate normal provisioning/linking/deployment and native Vulkan
regressions. Re-estimate subsequent surface and shader work from those results.
Do not transfer the Windows build result, hardware limits or implementation
time to these targets as evidence of compatibility or delivery effort.