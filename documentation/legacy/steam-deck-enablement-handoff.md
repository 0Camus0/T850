# T850 Steam Deck Enablement Handoff

This document summarizes the Steam Deck enablement work on branch
`editor_spline_deck_enablement`, how the runtime is built, how the launcher is
installed, how the current validation was performed, and which follow-up areas
remain.

## Branch and scope

- Base branch: `editor_splines`
- Working branch: `editor_spline_deck_enablement`
- Runtime target: native Steam Deck Linux, Vulkan-only
- Build environment: Valve Steam Linux Runtime `sniper` SDK container
- Compiler/runtime: Clang 16 + libc++ from SteamRT, with bundled `libc++.so.1`,
  `libc++abi.so.1`, and `libunwind.so.1` beside `DayScene`
- Output binary: `T850/bin/SteamDeck/Release/DayScene`

The Steam Deck path is intentionally Vulkan-only. SDL3 owns windowing/input and
creates the Vulkan surface via `SDL_Vulkan_CreateSurface`, so the runtime works
under SteamOS Desktop Mode and Game Mode/gamescope without direct Wayland code.

## Major code changes

### Native Linux/Steam Deck CMake support

`T850/CMakeLists.txt` now supports x86_64 Linux as `SteamDeck`.

The Steam Deck CMake path:

- selects `T850_VS_PLATFORM=SteamDeck`
- uses `x64-linux` vcpkg triplet
- enables `T850_PLATFORM_STEAM_DECK`
- installs Linux dependencies through a generated vcpkg manifest
- generates a vcpkg overlay for `imgui` so its `sdl3-binding` depends on SDL3
  with Steam-friendly features only

The generated manifest/overlay are important because SDL3's Linux default
features pull DBus/IBus/libsystemd. That chain fails in the official SteamRT
SDK because the SDK has older headers. The overlay forces SDL3 to use:

- `vulkan`
- `wayland`
- `x11`

and avoids DBus/IBus.

### Linux framework

`T850/Framework/src/core/LinuxFramework.cpp` was replaced with an SDL3 + Vulkan
framework path. It now:

- initializes SDL video/gamepad
- creates an SDL Vulkan window
- feeds the SDL window into `VulkanDriver`
- opens SDL gamepads and populates `InputManager::Gamepad`
- detects Steam Deck/handheld data where possible
- keeps Back/View from closing the app on Linux
- sets the window position to `(0, 0)` after creation so the SteamOS window is
  not offset off screen

### Vulkan backend Linux enablement

The Vulkan backend headers/source are enabled for `OS_LINUX`.

Linux uses SDL's Vulkan surface creation path, just like Windows:

```cpp
SDL_Vulkan_CreateSurface(sdlWindow, instance, nullptr, &surface)
```

Vulkan suboptimal swapchain handling was also adjusted. SteamOS/gamescope can
return `VK_SUBOPTIMAL_KHR` while the Wayland surface extent is unchanged. The
old behavior recreated the swapchain every time. The Linux path now mirrors the
Android-safe logic: if the surface extent is unchanged, keep the existing
swapchain.

This avoids repeated swapchain rebuilds during loading/early frames.

### Loading screen event pumping

During loading-progress frames, the renderer previously only pumped events on
Windows:

```cpp
#if defined(OS_WINDOWS)
  m_framework->ProcessInput();
#endif
```

This now includes Linux:

```cpp
#if defined(OS_WINDOWS) || defined(OS_LINUX)
  m_framework->ProcessInput();
#endif
```

Without this, SteamOS/KDE could treat the window as non-responsive during heavy
asset loading even though the loading bar was being drawn.

### Runtime ImGui/gamepad behavior

Steam Deck uses the existing Ally X style model:

- menu button opens runtime ImGui
- L1/R1 switch runtime GUI panels
- D-pad / left stick / arrows navigate within the currently selected panel
- non-selected panels use `NoNavFocus | NoNavInputs`

The Linux path does **not** feed Start/Back/Guide into ImGui itself. That avoids
conflicts with the app-level menu toggle path.

`DevGuiContext` now has a static navigation focus panel guard:

- `SetNavigationFocusPanel(title)`
- `PanelAllowsNavigationFocus(title)`

This guard is applied to:

- `Scene Controls`
- `Sandbox Console`
- `DEBUG`
- debug preview windows

### Steam Deck launcher

Steam Deck launcher files live under `T850/steamdeck/`:

- `BuildSteamRuntime.sh`
- `PackageSteamDeckRelease.sh`
- `T850.sh`
- `T850DeckLauncher.sh`
- `T850DeckLauncher.py`
- `DownloadCloudAssets.py`
- `InstallSteamDeckLauncher.sh`
- `config_steamdeck.json`

`T850DeckLauncher.py` is a GTK3 single-window launcher that mirrors the Release
launcher layout:

- Build Configuration
- Graphics API
- Runtime Content
- Display
- Snapshot
- Logging
- Runtime Options
- command preview
- Run / Editor / Config / Close buttons

The launcher defaults are Deck-appropriate versions of `Launcher_Release.ps1`:

- target: Linux
- architecture: x64
- configuration: Release
- API: Vulkan
- resolution: 1280x800
- fullscreen enabled
- log level: error
- log to file off
- dump seconds: 5
- dump frame: 300
- telemetry frequency: 60
- profile frames: 300
- benchmark seconds: 90
- culling: full

The launcher also mirrors Release launcher scene-file logic:

- Quake3 Mock auto-selects `Scenes/Q3/q3dm6_mod_3.t8scene`
- Scene Template auto-selects `Scenes/Q3/q3dm6_mod_3_jolt.t8scene`
- Quake3/Template scene file controls are enabled while model controls are
  disabled
- Sandbox toggles between scene-file mode and model mode
- Ragdoll uses model mode
- Day Scene enables benchmark controls; other scenes disable them

For Quake3 Mock and Scene Template, the launcher passes:

```text
--sceneProfile pc/windows
```

This reproduces the Windows profile selection on Steam Deck. It is needed
because the scene profiles were previously fixed from `pc/x64` to
`pc/windows`, and Linux runtime profile auto-detection would otherwise select
`pc/x64`/Linux matching behavior instead of the Windows-authored profile.

### Desktop and Game Mode shortcuts

`InstallSteamDeckLauncher.sh` installs:

- `~/.local/share/applications/t850-steamdeck.desktop`
- `~/.local/share/applications/t850-steamdeck-launcher.desktop`
- `~/Desktop/T850.desktop`
- `~/Desktop/T850 Launcher.desktop`

It also updates Steam's `shortcuts.vdf` for the first Steam userdata profile it
finds, adding:

- `T850`
- `T850 Launcher`

If the Game Mode entries do not show immediately, restart Steam or switch out of
and back into Game Mode.

## Build commands

### Local/Deck SteamRT build

From the repo root on a machine with Podman:

```bash
T850/steamdeck/BuildSteamRuntime.sh --configuration Release
```

Clean build:

```bash
T850/steamdeck/BuildSteamRuntime.sh --configuration Release --clean
```

Package without rebuilding:

```bash
T850/steamdeck/PackageSteamDeckRelease.sh --configuration Release --skip-build
```

Package with build:

```bash
T850/steamdeck/PackageSteamDeckRelease.sh --configuration Release
```

Default package output:

```text
T850/steamdeck/package/T850-SteamDeck-Release.tar.gz
```

### Windows-to-Deck helper

From Windows:

```bat
LaunchSteamDeckSolution.bat --build
LaunchSteamDeckSolution.bat --install-launcher
```

Run Deck default:

```bat
LaunchSteamDeckSolution.bat --run-deck
```

Run Desktop Mode defaults:

```bat
LaunchSteamDeckSolution.bat --run-desktop
```

## CI/release integration

`.github/workflows/build.yml` now has a `steamdeck` job.

The job:

1. checks out the repo
2. installs Podman
3. clones vcpkg
4. validates Steam Deck launcher scripts
5. builds `DayScene` through `BuildSteamRuntime.sh`
6. verifies `DayScene` plus bundled libc++ runtime files exist
7. packages `T850-SteamDeck-Release.tar.gz`
8. uploads it as the `T850-SteamDeck-Release` artifact

The tagged-release job now depends on:

- Windows build matrix
- Android APK job
- Steam Deck job

The release job copies `T850-SteamDeck-*.tar.gz` directly into GitHub release
assets. Windows artifacts continue to be zipped by the existing packaging code.
Android APK artifacts continue to be copied as APKs.

## Deck validation performed

A local Steam Deck target configured through the ignored `deckConfig.json` file
was used for validation.

### DayScene rendering

Ran `DayScene` with frame dump:

```text
T850_EXIT_CODE=0
T850_DUMP_DIR=T850/dumps_vulkan_f30_20260618_195940
```

The dump included:

- `RT_Dump_BackBuffer.ppm`
- `RT_Dump_HDR_Final.ppm`
- GBuffer outputs
- shadow map/depth outputs
- `snapshot.json`

Image stats confirmed non-empty output:

- BackBuffer: `1278x721`, nonzero `100%`
- HDR_Final: `1278x721`, nonzero `100%`
- ShadowMap depth: `2048x2048`, nonzero about `80%`

### Quake3 Mock

Ran Quake3 Mock with:

```text
--scene 2
--sceneFile Scenes/Q3/q3dm6_mod_3.t8scene
--sceneProfile pc/windows
```

Validation showed:

- scene file loaded
- profile scope `Scenes/Q3/q3dm6_mod_3.t8scene`
- runtime profile `pc/windows`
- frame dump completed
- no error lines

### Runtime GUI / gamepad

Validated with virtual controller input:

- Start opens runtime GUI
- Back/View opens runtime GUI and no longer closes app
- Guide opens runtime GUI
- app stays running
- no coredump

Validated panel navigation:

- virtual D-pad down/right did **not** switch panels
- R1 switched to `Sandbox Console`
- L1 switched back to `Scene Controls`

### CI Steam Deck package

Downloaded the `T850-SteamDeck-Release` artifact from PR run `27964976433`
and extracted it cleanly on the Deck under `/tmp/t850-ci-package-test-latest`.

The packaged `steamdeck/T850.sh` entrypoint was then run with:

```text
--desktop --scene 1 --dump-frame 60
```

Validation showed:

- the package repaired runtime asset symlinks after extraction
- `DownloadCloudAssets.py` fetched all 55 cloud runtime assets on first run
- `DayScene` exited with code 0 after frame dump
- `dumps_vulkan_f60_20260622_085020` contained `RT_Dump_BackBuffer.ppm`,
  `RT_Dump_HDR_Final.ppm`, GBuffer dumps, shadow/depth dumps, and
  `snapshot.json`

The packaged `steamdeck/T850DeckLauncher.sh` was also smoke-tested on the Deck
display. It opened and stayed running until the validation timeout, with only
GTK/fontconfig warnings and no Python traceback.

## Known follow-ups

1. **Editor/T8ditor on Steam Deck**
   - The launcher has an Editor button, but Steam Deck editor support is not
     considered complete.
   - Build with `--with-editor` after editor Linux runtime support is ready.

2. **Physical Deck button behavior**
   - Virtual controller tests work, but physical Steam Deck buttons can be
     affected by Steam Input mappings.
   - If a physical button still closes the app or does not toggle GUI, inspect
     the Steam Input action set for the shortcut and whether Steam maps that
     button to Escape/system actions outside SDL.

3. **Steam Game Mode shortcut refresh**
   - `shortcuts.vdf` is updated by the installer, but Steam may need restart or
     Game Mode re-entry before entries appear.

4. **Release payload size**
   - The Steam Deck tarball includes tracked assets and the Steam Deck cloud
     downloader.
   - On first run, `T850.sh` runs `DownloadCloudAssets.py` unless
     `T850_SKIP_ASSET_DOWNLOAD=1` is set. This downloads runtime models and
     texture assets from the public cloud manifests into the extracted package.

5. **Warnings**
   - SteamRT/Clang currently reports warnings about some older code patterns
     (missing `override`, deprecated copy assignment, unused variables). These
     are not treated as errors.

## Files added for Steam Deck

- `.gitattributes`
- `LaunchSteamDeckSolution.bat`
- `T850/steamdeck/BuildSteamRuntime.sh`
- `T850/steamdeck/PackageSteamDeckRelease.sh`
- `T850/steamdeck/T850.sh`
- `T850/steamdeck/T850DeckLauncher.sh`
- `T850/steamdeck/T850DeckLauncher.py`
- `T850/steamdeck/InstallSteamDeckLauncher.sh`
- `T850/steamdeck/config_steamdeck.json`

## Important repository note

`T850/config.json` was already modified locally before the Steam Deck work. It
should not be treated as part of the Steam Deck enablement unless intentionally
reviewed and included.
