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
- configure culling, dumps/replay, logging, telemetry, D3D12 debug, and benchmark mode;
- download missing cloud assets;
- launch DayScene or T8ditor;
- install and deploy the Android app when Android is selected.

The launcher writes `config.json`. Runtime command-line arguments override values loaded from that file. Its Build/Rebuild buttons invoke `scripts\build.ps1`, the same entry point used by GitHub Actions. Windows output lookup uses `Win32`, `x64`, and `ARM64` exactly as MSBuild emits them.

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
