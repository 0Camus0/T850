# Android Build and Deployment

Status: verified against local Debug APK builds and arm64-v8a/x86_64 PR CI on 2026-08-30.

T850 Android is a Vulkan NativeActivity application built through Gradle externalNativeBuild/CMake. The maintained ABIs are `arm64-v8a` and `x86_64`.

## Fixed Toolchain Versions

| Tool | Version/configuration |
|---|---|
| JDK | 17+ |
| compile/target SDK | 35 |
| minimum SDK | 28 |
| Android build tools | 35.0.0 |
| NDK | 27.2.12479018 |
| CMake | 3.22.1 |
| C++ | C++23, exceptions and RTTI, static libc++ |
| renderer | Vulkan |

The default SDK root is `%LOCALAPPDATA%\Android\Sdk`.

## One-Time Setup

From the repository root:

```powershell
.\SetupAndroidToolchain.bat
```

The script can install JDK 17, Ninja, and Vulkan SDK through winget; downloads Android command-line tools; installs SDK platforms/tools, NDK, and CMake; downloads VMA; and installs Android vcpkg dependencies.

Options:

```powershell
.\SetupAndroidToolchain.bat --sdk D:\Android\Sdk
.\SetupAndroidToolchain.bat --skip-winget
.\SetupAndroidToolchain.bat --with-emulator
.\SetupAndroidToolchain.bat --android-abis arm64-v8a,x86_64
```

For an emulator, setup with `--with-emulator`; this adds the Android 35 Google APIs x86_64 image and x86_64 vcpkg dependencies.

Expected setup checks include:

```text
platform-tools/adb.exe
platforms/android-35/android.jar
platforms/android-28/android.jar
build-tools/35.0.0/aapt2.exe
ndk/27.2.12479018/source.properties
cmake/3.22.1/bin/cmake.exe
```

Set or persist `JAVA_HOME`, `ANDROID_HOME`, `ANDROID_SDK_ROOT`, `ANDROID_NDK_HOME`, `ANDROID_NDK_ROOT`, and `VULKAN_SDK` when the setup output instructs you to do so.

## Full APK Build

The authoritative local build wrapper is:

```powershell
.\T850\scripts\android\BuildAndroid.bat Debug
.\T850\scripts\android\BuildAndroid.bat Release
```

Android uses the separate native source list in `T850/cmake/AndroidBuild.cmake` rather than `Framework/CMakeLists.txt`. It explicitly registers Framework sources plus `ImGuiRendererBackend` implementations and `ProfilerGpuBackend`. Before an APK build, validate source registration from the source root:

```powershell
.\scripts\ValidateBuildRegistration.ps1
```

The same registration gate runs before the Android GitHub Actions matrix.

Default behavior:

- Debug configuration;
- `arm64-v8a`;
- incremental build;
- `physics-demo` development asset profile;
- Vulkan validation disabled;
- no install/launch.

Useful commands:

```powershell
# Clean ARM64 Debug
.\T850\scripts\android\BuildAndroid.bat Debug --clean

# Build, install, and launch on connected device
.\T850\scripts\android\BuildAndroid.bat Debug --install --launch

# Emulator ABI
.\T850\scripts\android\BuildAndroid.bat Debug --emulator --install --launch

# Multiple ABIs
.\T850\scripts\android\BuildAndroid.bat Release --abi arm64-v8a,x86_64

# Custom SDK and validation layers
.\T850\scripts\android\BuildAndroid.bat Debug --sdk D:\Android\Sdk --vulkan-validation

# CI/compile-only unsigned Release
.\T850\scripts\android\BuildAndroid.bat Release --allow-unsigned-release
```

All options:

```text
Debug | Release
--configuration Debug|Release
--sdk PATH
--abi ABI[,ABI...] | --abis ...
--asset-profile physics-demo|doom-porsche|q3-sandbox|models-full|full
--emulator
--vulkan-validation
--allow-unsigned-release
--clean
--install
--launch
```

`--launch` implies `--install`, force-stops `com.t850.engine`, then launches `com.t850.engine/.LauncherActivity`.

## Native Window and UI Lifecycle

Android still selects Vulkan at the platform composition boundary, but shared framework code does not downcast the active driver. `AndroidFramework` calls virtual `BaseDriver::SuspendWindowSurface()` and `ResumeWindowSurface(nativeWindow,w,h)` hooks; `VulkanDriver` owns swapchain/surface teardown and recreation.

`ImGuiSystem` creates `ImGuiVulkanBackend`, which pairs `ImGui_ImplAndroid` with `ImGui_ImplVulkan`. The backend owns native-window rebinding, Android event/stylus forwarding, renderer frame hooks, draw submission, and descriptor cleanup. Runtime UI installs a virtual `BaseDriver::SetPrePresentOverlayCallback()` instead of casting to Vulkan.

Surface resume order is:

1. update `ANativeWindow` dimensions;
2. set the driver's tagged native window;
3. call `ResumeWindowSurface()`;
4. rebuild pipeline objects;
5. rebind the ImGui backend to the current native window.

Verified in PR CI: both `arm64-v8a` and `x86_64` Release APK jobs pass. Local development and production Debug APK builds also pass; install/launch and performance still require a connected device or emulator.

`ResizeSwapchain` must not trust `VkSurfaceCapabilitiesKHR::currentExtent` while the
surface is still settling: DeX and some Android compositors report `0x0` during
recreation, and a 0x0 swapchain answers `VK_ERROR_OUT_OF_DATE_KHR` forever, which
presents as a black window with a recreation loop. `ResolveSwapchainExtent`
clamps to the ANativeWindow size and refuses zero-area extents, recreation is
deferred until the surface reports a real size, and `CompleteFrame` only presents
when the frame actually acquired a swapchain image.


## Asset Profiles

Development APKs filter `Assets/` to control package size:

| Profile | Purpose |
|---|---|
| `physics-demo` | Default scenes plus DamageHelmet, SkyBox, Q3, Sponza, ragdoll metadata, Doomslayer, cyborg, Porsche |
| `doom-porsche` | Doomslayer/ragdoll metadata and Porsche showcase |
| `q3-sandbox` | Q3 scene/render assets and selected characters/models |
| `models-full` | Common runtime assets plus all supported model files and ragdoll metadata |
| `backyard` | Backyard scene (index 7): `Models/Backyard/*`, fluid pool layout, `Shaders/spirv/*`, ball PBR palette. Skips the unrelated sky cubemaps; ~80 MB APK |
| `full` | Entire source asset tree except generated caches/exclusions |

Production flavor uses tracked runtime scene/shader/texture assets and metadata; cloud/download strategy supplies heavyweight payloads where applicable.

Generated caches and build products are excluded from packaged assets.

## Shader Permutation Precompilation

Android has no runtime HLSL front end worth paying for at launch, so every
shader the scene can ask for is compiled to SPIR-V while the APK is assembled.

- `Assets/Shaders/shader_permutations.json` is the checked-in union of the
  graphics keys (`<name>.<KEY16>.spv`) and compute kernels
  (`<name>.<permutation>.spv`) used by the shipped scenes. Regenerate it with
  `DayScene --scene <n> --dumpShaderPermutations --shaderPermutationOutput <path>`
  and merge, never replace: dropping another scene's entries breaks that scene.
- `android/app/build.gradle` walks the union and emits
  `Assets/Shaders/spirv/*.spv` with `-DT850_VULKAN=1 -DT850_SPIRV=1`. The
  Backyard profile currently carries 286 graphics + 31 compute outputs.
- `VulkanShader` reads `<name>.<KEY16>.spv`, `VulkanCompute` reads
  `<name>.<permutation>.spv`; both fall back to glslang at runtime if an entry
  is missing, so a missing permutation costs startup time, not correctness.
- Shaders created without a permutation key (the `TextRenderer` HUD pair
  `VS_Text`/`FS_Text`) always compile from source and log at debug level.

Verify the cache is actually used by building with `LOG_LEVEL 2` and looking for
`[Vulkan][Compute] Precompiled SPIR-V hit` (one line per kernel) and zero
`Missing precompiled SPIR-V` lines.

## Fullscreen Behaviour

Both activities are declared `android:resizeableActivity="false"` and
`android:screenOrientation="landscape"`. On Samsung DeX a non-resizable activity
cannot be windowed, so the game task becomes `mode=fullscreen` and the surface
is the whole display (2960x1848 on the SM-X920 reference tablet) instead of a
freeform window. If a DeX desktop task is already minimized, launch is enough;
otherwise force the task out of the desktop with
`adb shell cmd statusbar click-tile com.sec.android.app.launcher/com.honeyspace.dexservice.DesktopModeTile`.


## Output APKs

The local wrapper builds the `development` flavor:

```text
T850/android/app/build/outputs/apk/development/debug/app-development-debug.apk
T850/android/app/build/outputs/apk/development/release/app-development-release.apk
```

An unsigned Release can be named `app-development-release-unsigned.apk`.

Gradle flavors:

| Flavor | Application ID | Label |
|---|---|---|
| `development` | `com.t850.engine` | T850 Vulkan |
| `production` | `com.t850.t8` | T8 |

Gradle's aggregate `assembleRelease` builds both flavors. CI stages the production Release output for publishing and can sign it from GitHub secrets.

## Release Signing

Local Release builds require signing unless `--allow-unsigned-release` is passed.

Accepted keys:

```text
T850_RELEASE_STORE_FILE or ANDROID_KEYSTORE_PATH
T850_RELEASE_STORE_PASSWORD or ANDROID_KEYSTORE_PASSWORD
T850_RELEASE_KEY_ALIAS or ANDROID_KEY_ALIAS
T850_RELEASE_KEY_PASSWORD or ANDROID_KEY_PASSWORD
```

Sources include Gradle properties, these ignored files, and environment variables:

```text
T850/android/signing.properties
T850/android/app/signing.properties
%USERPROFILE%/.android/t850-release-signing.properties
```

Example ignored local properties:

```properties
T850_RELEASE_STORE_FILE=C:/Users/me/.android/t850-release.keystore
T850_RELEASE_STORE_PASSWORD=replace-me
T850_RELEASE_KEY_ALIAS=t850-release
T850_RELEASE_KEY_PASSWORD=replace-me
```

`ConfigureAndroidReleaseSigning.ps1` is for generating/uploading GitHub Actions secrets through authenticated `gh`; it is not the normal local properties writer:

```powershell
.\ConfigureAndroidReleaseSigning.ps1 -Create
.\ConfigureAndroidReleaseSigning.ps1 -Create -GeneratePasswords
```

It uploads `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, and `ANDROID_KEY_PASSWORD`. Back up the keystore and passwords permanently; losing them prevents updates to existing installs.

## Fast Native Repack

`BuildAndroidFastApk.ps1` rebuilds/strips the native `.so`, copies an existing APK as a template, replaces `lib/<abi>/libT850Android.so`, zipaligns, and signs with `%USERPROFILE%\.android\debug.keystore`.

```powershell
.\BuildAndroidFastApk.ps1 Debug
.\BuildAndroidFastApk.ps1 Release --install --launch
```

Use this for rapid local development only. A fast `Release` output is debug-keystore signed and is not a production release artifact.

Important options:

```text
--sdk PATH
--abi ABI[,ABI...]
--emulator
--template APK
--out APK
--install
--launch
--skip-native-build
--vulkan-validation
```

It requires an existing template APK. Run a full build first or pass `--template`.

Default outputs:

```text
app/build/outputs/apk/<variant>/app-<variant>-fast-signed.apk
```

## Launcher Install and Deploy

Run the WPF launcher from the source root:

```powershell
.\T850\scripts\Launcher.ps1
```

Select Android target. The launcher forces Vulkan, disables desktop resolution/fullscreen controls, and changes buttons to:

- `Install`: `adb install -r <apk>`;
- `Deploy`: force-stop and launch an already-installed `com.t850.engine` app with selected scene/model/dump/telemetry extras.

Deploy does not install. If the package is missing, use Install first.

The launcher requires a device in adb state `device` and can target a selected serial.

## Manual Device Checks

```powershell
$adb = "$env:ANDROID_HOME\platform-tools\adb.exe"
& $adb devices
& $adb install -r .\T850\android\app\build\outputs\apk\development\debug\app-development-debug.apk
& $adb shell am force-stop com.t850.engine
& $adb shell am start -n com.t850.engine/.LauncherActivity
```

Use `adb logcat` for native crashes and Vulkan validation output.

### Reproducible auto-run

`LauncherActivity` deduplicates auto-run intents by the intent URI (or an
explicit `RUN_ID`) and remembers the consumed key in preferences. Repeating the
same `am start` after force-stopping the app launches the launcher UI instead of
the scene. Pass a fresh `RUN_ID` for every run:

```powershell
& $adb shell am start -n com.t850.engine/.LauncherActivity `
    --ez com.t850.engine.extra.AUTO_RUN true `
    --ei com.t850.engine.extra.SCENE 7 `
    --ei com.t850.engine.extra.LOG_LEVEL 2 `
    --es com.t850.engine.extra.RUN_ID "run-$(Get-Random)"
```


## CI Behavior

GitHub Actions builds arm64-v8a and x86_64 Release APKs. It allows unsigned Gradle output during compile, then:

- signs when release secrets are available;
- uploads unsigned artifacts for non-tag builds when secrets are absent;
- requires signing secrets for `v*` release tags.

The tagged release job then runs `scripts/ValidateReleasePackages.ps1` against
the assembled artifacts. It opens each APK, verifies `AndroidManifest.xml`, the
ABI-specific `libT850Android.so` and packaged assets, and rejects filenames that
identify an unsigned release. This structural check does not replace install,
launch, logcat and frame-capture evidence on equipped hardware.

## Stop Conditions and Failures

| Failure | Classification/action |
|---|---|
| Android SDK/NDK path missing | environment prerequisite; run setup or pass `--sdk` |
| JDK or `glslangValidator` missing | environment prerequisite; install JDK/Vulkan SDK |
| vcpkg package missing/link error | source/dependency registration; verify CMake and setup triplet |
| Release signing missing | configure signing or use unsigned only for compile validation |
| no connected device | build may still pass; install/deploy cannot run |
| app installed but Deploy fails | inspect adb state, package ID, and Activity launch output |
| shaders/assets absent in APK | inspect selected asset profile and Gradle generated asset directory |

## Related Documents

- [Windows setup and build](../development/windows-build-and-run.md)
- [Cloud assets](../development/cloud-assets.md)
- [Verification gates](../testing/verification.md)
- [Runtime configuration](../development/runtime-configuration.md)
