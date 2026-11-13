# Tag-time release verification — 2026-11-12

Manual reproduction of the `.github/workflows/build.yml` tag pipeline on the local
Windows and Linux hosts, from `master` at `b8d4ec2` ("Docs: update commit references
after the history rewrite").

**Result:** all seven release assets were built and `ValidateReleasePackages.ps1
-RequireSignedAndroid` passed. Windows x64/Win32, Android arm64, Steam Deck and the
browser bundle were executed. ARM64 Windows and x86_64 Android are built and
structurally verified but could not be executed on this hardware; see
[Known limitations](#known-limitations).

Nothing was pushed or committed.

## Hosts

| Host | Role | Details |
|---|---|---|
| Windows `10.0.0.232` | Windows, Android, Web, release assembly | i9-14900HX, 31.6 GB, RTX 4080 Laptop; VS 2022 Community v143; Android SDK/NDK 27.2; SM-X920 on ADB |
| Linux (this host) | Steam Deck | 32 cores, 123 GB, RTX PRO 6000; podman 5.7.0 |

Windows builds and file transfers run over SSH (session 0). Native WebGPU compute
self-tests and the emulator UI run in the interactive desktop session 1 through a
scheduled task, because D3D12/WebGPU offscreen readback fails in session 0.

A portable PowerShell 7.7.0 was installed at `D:\sandbox\pwsh\pwsh.exe` because
`PackageWebRelease.ps1` and `ValidateReleasePackages.ps1` use
`[IO.Path]::GetRelativePath`, which Windows PowerShell 5.1 does not have.

## Pipeline gate

| Gate | Result |
|---|---|
| `ValidateBuildRegistration.ps1` | PASS |
| `TestLauncherWebGPU.ps1 -Ui` | PASS (all launcher/layout checks) |
| `verify_minecraft_atlas.py` | PASS (20 blocks, 120 faces) |
| Dawn x64 package audit | PASS, D3D12-only |
| Dawn ARM64 package audit | PASS, D3D12-only |
| `check_gl_twins.py` / `check_fluid_constants.py` (Linux) | PASS |

## Windows builds and self-tests

| Configuration | Build | Self-test |
|---|---|---|
| x64 Release | PASS (248 s) | 78 PASS / 0 FAIL |
| x64 Debug | PASS (153 s) | 78 PASS / 0 FAIL |
| Win32 Release | PASS | 78 PASS / 0 FAIL |
| Win32 Debug | PASS | 78 PASS / 0 FAIL |
| ARM64 Release | PASS | not run (no ARM64 Windows host); PE `0xAA64` verified |
| ARM64 Debug | PASS | not run; PE `0xAA64` verified |

The x64 Release build also produced the WebAssembly bundle and passed
`T850WebSelfTests` (1/1 ctest).

## Release assets

`D:\sandbox\T850\release-assets`, validated with
`ValidateReleasePackages.ps1 -ReleaseDirectory release-assets -RequireSignedAndroid`.

| Asset | Size (bytes) | SHA-256 |
|---|---:|---|
| `T850-x64-Release.zip` | 1,355,835,749 | `37a57a923c4025ed6722f6d86caa06a5bdf89d4a93cde28594a408b47bfa91c6` |
| `T850-Win32-Release.zip` | 1,340,409,957 | `28064b7d1614b24331c041f42b6032f65c65d53cfd19e8149be2549575dd21f5` |
| `T850-ARM64-Release.zip` | 1,356,032,728 | `59bccace106fa98cca78a5414a2cb4255d2103bfe32321b63405bf2e6eda9ce6` |
| `T850-Web-Release.zip` | 21,221,078 | `b09ca16b87b58dc259a7e332ee986e25e0b8ce51ca8220a85d0067ef0a269b36` |
| `T850-Android-arm64-v8a.apk` | 518,818,547 | `fd2f68fc1a3cff29ff80e28f246f04e3d7dffe74c58d51ae123b0ec93d480f85` |
| `T850-Android-x86_64.apk` | 520,291,856 | `13a985732d737a53f44daa2c9198c61c4664cf7bd41f354ca716b70de074bc20` |
| `T850-SteamDeck-Release.tar.gz` | 1,318,194,574 | `153262d6079d2b2959cdbb36e5a53987999d2a2b1964b657a6502eb47c1fd700` |

`SHA256SUMS.txt` is written in the same directory.

### Packaged Windows ZIPs

Both ZIPs were extracted and self-tested **from the extracted package root**, not
from the build tree:

| Package | Required entries | Self-test |
|---|---|---|
| `T850-x64-Release.zip` | DayScene, T8ditor, T850Launcher, Shaders, Scenes, `web/server.mjs`, `web/site/DayScene.wasm` | 78 PASS / 0 FAIL |
| `T850-Win32-Release.zip` | same | 78 PASS / 0 FAIL |
| `T850-ARM64-Release.zip` | same; `DayScene.exe` and `T8ditor.exe` are `0xAA64` | not run |

The release launcher was built with `ps2exe 1.0.18` and is present in all three
ZIPs.

## WebAssembly and browser

| Check | Result |
|---|---|
| `BuildWeb.ps1 -AssetMode Cloud -Clean` | PASS (WASM build, ctest 1/1, 167 cloud routes) |
| `PackageWebRelease.ps1` + `ValidateWebReleasePackage.ps1` | PASS, 1,490 files |
| Edge 157.0.4341.46 (x64), `--software-webgpu --compute-selftest` | PASS (`report.passed=true`) |
| Edge 157.0.4341.46 (x64), `--device-loss-recovery` | PASS (`report.passed=true`) |

The native WebGPU compute self-test (used to export the volume-pattern shader
packages) passes in session 1. It fails in session 0 on this host, and that failure
left the Web bundle without `CS_VolumePatternWrite/Read` packages. Running the
self-test in session 1 generated both packages and the browser tests then passed.

## Android

Toolchain: Android SDK 35, NDK 27.2.12479018, JDK 17, Vulkan SDK 1.4.341.1; vcpkg
`arm64-android` and `x64-android` dependencies installed.

| ABI | Build | Signature | Runtime |
|---|---|---|---|
| `arm64-v8a` | PASS, 518,818,547 B | APK Signature Scheme v2; `CN=T850 Android Release` | Installed on SM-X920 (Android 16, arm64); scene 1 ran at ~55-70 fps; scene 8 runs with three on-screen controls and the Android profile at ~10 fps (screen space) / ~9 fps (particles), 2960x1848 |
| `x86_64` | PASS, 520,291,856 B | APK Signature Scheme v2; `CN=T850 Android Release` | Installed on Android 15 x86_64 emulator, then `SIGILL` at `t850::Config::Config()+122`; see limitations |

The production APK starts `AssetDownloadActivity`, which then opens
`LauncherActivity`; the development-flavor Release APK exports `LauncherActivity`
directly and was used for the emulator test.

## Scene 8 runtime

### Windows

The packaged x64 build was run at 960x540 with
`--scene 8 --backyard-start water --postProcessMode compute --dump-frame 300`.

| API | Result | Fluid pixels at frame 300 |
|---|---|---:|
| D3D12 | exit 0, 7 render targets, 0 `[ERROR]` lines | 233,134 |
| D3D11 | exit 0, 7 render targets, 0 `[ERROR]` lines | 236,466 |
| GL | exit 0, 7 render targets, 0 `[ERROR]` lines | 232,152 |

The captured fluid layer shows the pool, water surface, steps and caustics on all
three APIs.

### Android

The production arm64 APK was run through its normal path:
`AssetDownloadActivity` -> `LauncherActivity` -> `NativeActivity`, scene 8.

The first run exposed a packaging gap: the app had the Backyard `.glb` models in
external files but no `Models/Backyard/backyard_layout.json`, so `PbfFluid::
TryInitialize` failed. The pool stayed flat and every fluid pass fell back because
`BackyardWaterPositions`, `BackyardWaterAnisotropy` and `BackyardWaterCaustics`
were never created.

The release fix is:

- `android/app/build.gradle`: package `Models/Backyard/backyard_layout.json` in
  both the `physics-demo` and production asset lists.
- `android/app/src/main/java/com/t850/engine/LauncherActivity.java`: set
  `MAX_SCENE_INDEX = 8` and add "Backyard 8" to the scene list.
- `BackyardScene`/`Backyard8Scene`: replace the inherited Backyard 7 quick bar on
  Android with three scene-8 actions: **Restart still**, **Restart + falling
  ball**, and **Switch to particles / Switch to screen space**. The bar still
  records touch hits through `m_quickBarHits`, so it works without ImGui input.

With the layout in the APK and the launcher fix, the production APK logs:

```
[PBF] Pool basin X -4.50..4.50  Y -1.60..0.38  Z -9.00..-4.50 m ...
[PBF] Seeded 26632 pool particles up to y=-0.06, spacing 0.130 m
[PBF] Solver ready on vulkan: 26632 particles, 6 iteration(s), 2 substep(s)
[Backyard8] Particle fluid bound: 26632 particles ...
[Backyard8] Pool settled (3 s simulated) in 8404 ms
```

All 15 PBF compute pipelines are created. The new process has 0 `[ERROR]`
lines, 0 unresolved compute buffers and 0 unknown render targets. The captured
frame shows the pool with a moving water surface.

### Android controls and mobile profile

The Android scene-8 quick bar was exercised on the tablet:

| Action | Log / result |
|---|---|
| Restart still | `Quick action: restart still (lake)`, reseeded 26,632 particles, settled |
| Restart + falling ball | `Quick action: restart with falling ball`, 26,271 pool + 15,234 pending, appended to 41,505 total |
| Switch to particles | `Quick action: renderer -> particles`, then `Pass 'Fluid Resolve (surfels)' dispatched compute 185 x 116 x 1 for 1480x924x1` |
| Switch to screen space | `Quick action: renderer -> screen space` after a 41,505-particle drop; the button relabels to "Switch to particles" and the bar reads "renderer: screen space". 0 errors, 0 unresolved buffers across both switches. |

The final Android-only profile in `Backyard8Fluid.json` is:

```
"android": {
  "solverIterations": 3,
  "substeps": 1,
  "smoothingIterations": 2,
  "thicknessDownsample": 2,
  "maxStepsPerFrame": 1,
  "particleRadius": 0.16,
  "fixedStep": 0.0333333,
  "anisotropy": 1,
  "whitewater": 1
}
```

The shared `scene.fixedStep` / `scene.maxStepsPerFrame` values remain 1/60 and 3
for desktop and Steam Deck. Measured results on the SM-X920 at 2960x1848:

| Profile | FPS |
|---|---:|
| Original 2x6, 0.13 m, 6 smoothing, 2 steps/frame | ~3 |
| 1x3, 0.16 m, 2 smoothing, 2 steps/frame | ~9 |
| 1x3, 0.16 m, 2 smoothing, 1 step at 1/30 s | ~10 screen space / ~9 particles |

A 1920x1200 swapchain and disabling anisotropy/whitewater were tested and did not
improve the frame rate (9 and 8 fps respectively), so both were reverted to keep
the native resolution and visual quality. The remaining bottleneck is the PBF
compute/render path on the tablet, not fill rate or the solver iteration count.

The log also shows the fluid render-graph passes already run at half resolution:
`Pass 'Fluid Resolve' dispatched compute 185 x 116 x 1 for 1480x924x1 output`
(1480x924 is exactly half of 2960x1848). That partly explains why shrinking the
swapchain to 1920x1200 did not buy the expected 1.5x: the fluid targets were
already downscaled, so the win was limited to the scene/post passes.

These source changes are in the working tree but not committed. The APKs in
`release-assets` were rebuilt from them.

## Steam Deck

`BuildSteamRuntime.sh --configuration Release --with-editor` built inside the
`registry.gitlab.steamos.cloud/steamrt/sniper/sdk` container. DayScene, T8ditor and
`libc++.so.1` / `libc++abi.so.1` / `libunwind.so.1` are present. The packaged
tarball was extracted and self-tested from its package root:

```
exit=0 PASS=78 FAIL=0
```

Steam Deck hardware was not available; this is a host run against the packaged
Steam Runtime libc++.

## Known limitations

1. **x86_64 Android requires AVX2.** The x86_64 translation units inherit Jolt's
   public `INTERFACE_COMPILE_OPTIONS` (`-mavx2 -mbmi -mpopcnt -mlzcnt -mf16c -mfma
   -mfpmath=sse`), so the APK faults on emulator CPUs without AVX2. The default
   Android emulator CPU does not expose AVX2. The arm64 APK is unaffected. Fixing
   this needs the Jolt interface flags not to leak into the app target and Jolt
   itself built without AVX2, or a documented x86_64 AVX2 requirement.
2. **Android scene 8 is still ~10 fps at native resolution.** The mobile profile
   (1x3, 0.16 m spacing, 2 smoothing passes, one 1/30 s step) raises the SM-X920
   from 3 to 10 fps, but 2960x1848 remains heavy. A 1920x1200 swapchain and
   disabling anisotropy/whitewater were measured and did not help; further gains
   need engine-side PBF/render work, not only config changes.
3. **ARM64 Windows is build-only here.** The pipeline runs ARM64 self-tests on
   `windows-11-arm`; this host has no ARM64 Windows. Both ARM64 binaries are
   `0xAA64`.
4. **Steam Deck hardware is unavailable.** The packaged runtime passes its
   self-test on this Linux host with the bundled libc++.
5. **Tag signing.** The local release keystore at
   `C:\Users\camus\.android\t850-release-signing.properties` signed the APKs.
   The real tag workflow reads `ANDROID_KEYSTORE_BASE64` and related secrets; the
   signature certificate will differ if a different production key is used.
6. **No GitHub Release was created.** The pipeline's final `release` job publishes
   the assets; this run only assembled and validated them locally.
