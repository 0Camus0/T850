# T850

Current engine/README audit: 2026-10-06. Dated evidence sections retain
their original benchmark or release revision and are not silently rebased onto
the current implementation.

<div align="center">

[![Build](https://github.com/0Camus0/T850/actions/workflows/build.yml/badge.svg)](https://github.com/0Camus0/T850/actions/workflows/build.yml)
&nbsp;&nbsp;![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Browser%20%7C%20Android%20%7C%20Steam%20Deck-blue)
&nbsp;&nbsp;![APIs](https://img.shields.io/badge/APIs-D3D11%20%7C%20D3D12%20%7C%20Vulkan%20%7C%20OpenGL%20%7C%20WebGPU-green)
&nbsp;&nbsp;![License](https://img.shields.io/badge/license-MIT-yellow)

**A C++23 rendering/game engine with five native Windows graphics backends, a cloud-backed Emscripten/WebGPU release, JSON render graphs, glTF/PBR, animation, Jolt physics, Recast/Detour navigation, gameplay simulation, and a built-in scene editor.**

<img src="T850/Resources/Screens/Sponza1.png" alt="T850 Sponza deferred renderer" width="100%">

</div>

## What Is Implemented

### Rendering

- D3D11, D3D12, OpenGL, Vulkan, and native Dawn/WebGPU peer DayScene backends on supported Windows targets; T8ditor intentionally remains on the first four.
- Vulkan runtime on Android and Steam Deck/Linux.
- Architecture-neutral Emscripten/WebGPU runtime with a lightweight seven-scene browser launcher, prepared shaders, loopback COOP/COEP server, and allowlisted cloud asset routes.
- JSON-driven render graph with deferred shading and post processing.
- PBR metallic/roughness materials, IBL, shadows, SSAO, HDR/tone mapping, bloom, depth of field, God Rays, parallax/self-shadowing, lens flare, and vignette.
- Cross-backend compute on D3D11, D3D12, Vulkan, Dawn/WebGPU, and desktop OpenGL 4.3+, including structured-buffer and storage-image kernels plus compute/raster render-graph alternatives.
- Shader permutations, strict manifest-backed compute kernels, disk caches, reflection, backend pipeline caches, D3D12 ShaderCacheSession/PipelineLibrary persistence, and native Dawn BlobCache persistence.
- Deterministic render-target/frame dumps and same-API visual regression comparison.
- Nested CPU telemetry and completion-qualified D3D12/Vulkan/WebGPU GPU timestamps, pass-level reports, upload/compile counters, and no-present profiling modes.

### Assets and Animation

- `.gltf` and `.glb`, including Draco compression and supported material extensions.
- Legacy `.x` loading remains available in the engine.
- Parallel image/geometry work, MikkTSpace tangents, mesh/material pools and caches.
- Managed texture atlases with stable IDs, immutable rectangular mappings, half-texel UVs, explicit pixelation, and the file-backed Minecraft `terrain.png` atlas.
- Skeletal animation with GPU RGBA32F bone textures, snapshots, wireframe, and skeleton visualization.
- Environment/IBL loading and generated caches.
- Manifest-driven cloud models/textures with size/SHA-256 validation and atomic local replacement; release packages keep heavyweight payloads out of GitHub artifacts.

### Simulation and Gameplay

- Jolt static/dynamic/kinematic bodies, triangle mesh cooking/cache, gameplay collision layers, filtered casts/overlap, characters, and ragdolls.
- Recast NavMesh build/cache/bake, Detour path queries, volumes, area costs, and authored/generated traversal links.
- Scene-owned fixed-tick gameplay system with stable IDs, components, controllers, movement intents, queued events, state machines, physics/navigation facades, path following, groups/formations/flocking, and RTS/FPS examples.
- Mutable voxel terrain with streamed chunks, atlas-aware meshing, collision, persistence and block edits; Minecraft adds an authored world, voxel A* navigation, enemies, health/HUD and browser controls.
- 78 CLI self-tests for schema, validation, lifecycle, events, tick semantics, state machines, physics, render-graph lifetimes, mutable meshes, voxel terrain, navigation cancellation, and fallback behavior.

### T8ditor

- Multi-object scene authoring, cameras, lights, transforms, splines, physics, navigation, ragdolls, profiles, and render controls.
- Game entity/group hierarchy and inspectors for identity, control, links, components, behavior, formation/flock settings, and simulation settings.
- Validation panel with jump-to-entity/group and validation before Play.
- Editor overlays for game labels/state, health, sensor/combat radii, and groups.
- Whole-scene undo/redo plus transform/group commands.
- Fidelity Play exports a temporary `.t8scene` and loads it through the real SceneTemplate serializer/runtime path.
- Native 16-bit heightmap import, sculpt/material brushes, terrain LOD and persistence.
- Tagged scene regions plus square-grid placement with occupancy, flatness checks, colored blockouts, optional fitted GLB visuals, per-part visibility and animation selection.
- Direct CLI supports D3D11, D3D12, OpenGL, and Vulkan on Windows. The WPF launcher maps editor launches to D3D12/Vulkan on x64 and ARM64, and D3D11/Vulkan on Win32.

## Quick Start: Windows

Prerequisites:

- Visual Studio 2022 with Desktop development with C++ and v143;
- Windows SDK;
- Git and PowerShell;
- internet access for first-time vcpkg and cloud assets.

From the repository root:

```powershell
.\LaunchSolution.bat --setup-only
Set-Location .\T850
.\scripts\build.ps1 -Config Release -Platform x64
```

Run DayScene:

```powershell
Set-Location .\bin\x64\Release
.\DayScene.exe --api d3d11 --scene 1
```

Run an authored scene:

```powershell
.\DayScene.exe --api d3d12 --scene 4 --sceneFile Scenes/DayScene.t8scene
```

Run T8ditor:

```powershell
.\T8ditor.exe --api d3d12 --sceneFile Scenes/DayScene.t8scene
```

Open the developer launcher:

```powershell
Set-Location ..\..\..
.\scripts\Launcher.ps1
```

Full guide: [Windows setup, build, and run](documentation/development/windows-build-and-run.md).

## Custom Editors

For a private/custom editor, reuse T8ditorCore through the
[static editor SDK](documentation/editor/editor-sdk.md). The first slice provides
external panels, commands, component inspectors/validators, undoable gameplay edits,
and shared component registration in default Play. See the
[external consumer sample](examples/EditorExtension/main.cpp); no editor source copy
is required. Replaceable Play sessions and full-world transactions remain follow-up work.

## Runtime Hosts

`DayScene.exe` contains seven selectable hosts:

| Index | Host | Typical use |
|---:|---|---|
| 0 | SandboxScene | isolated model/material/animation inspection |
| 1 | DayScene | Sponza demo, full renderer, benchmarks |
| 2 | Quake3Mock | authored `q3dm6_mod_3.t8scene`, Q3 collision/navigation experiments |
| 3 | RagdollEditor | animated model and runtime ragdoll work |
| 4 | SceneTemplate | authored `.t8scene`, gameplay, physics, navigation |
| 5 | VoxelScene | generated mutable chunks, grounded FPS, streaming and block edits |
| 6 | MinecraftScene | authored atlas-backed block world, enemies, voxel navigation/collision and browser demo |

Use `DayScene.exe --help` for the current CLI. See [Runtime hosts](documentation/runtime/runtime-hosts.md) and [Runtime configuration](documentation/development/runtime-configuration.md).

### Scene Gallery

Deterministic D3D12 captures from the seven current runtime hosts at 1280x720:

<table>
  <tr>
    <td width="50%"><img src="T850/Resources/Screens/Scenes/Sandbox.png" alt="SandboxScene inspecting a PBR helmet"><br><b>0 · SandboxScene</b> — focused model, material and animation inspection.</td>
    <td width="50%"><img src="T850/Resources/Screens/Scenes/DayScene.png" alt="DayScene Sponza renderer"><br><b>1 · DayScene</b> — Sponza, the full render graph and benchmark path.</td>
  </tr>
  <tr>
    <td width="50%"><img src="T850/Resources/Screens/Scenes/Quake3Mock.png" alt="Quake3Mock authored q3dm6 scene"><br><b>2 · Quake3Mock</b> — authored q3dm6 scene with Q3 runtime behavior.</td>
    <td width="50%"><img src="T850/Resources/Screens/Scenes/RagdollEditor.png" alt="RagdollEditor animated Doom Slayer"><br><b>3 · RagdollEditor</b> — skinned animation and runtime ragdoll tooling.</td>
  </tr>
  <tr>
    <td width="50%"><img src="T850/Resources/Screens/Scenes/SceneTemplate.png" alt="SceneTemplate authored Q3 Jolt scene"><br><b>4 · SceneTemplate</b> — authored scene runtime, Jolt, navigation and gameplay.</td>
    <td width="50%"><img src="T850/Resources/Screens/Scenes/VoxelScene.png" alt="VoxelScene streamed mutable terrain"><br><b>5 · VoxelScene</b> — generated streamed chunks and mutable block terrain.</td>
  </tr>
  <tr>
    <td width="50%"><img src="T850/Resources/Screens/Scenes/MinecraftScene.png" alt="MinecraftScene authored block world"><br><b>6 · MinecraftScene</b> — authored atlas-backed world, enemies and browser controls.</td>
    <td width="50%">The same host catalog is available from the native launcher and the packaged browser launcher. SceneTemplate remains the primary authored `.t8scene` runtime.</td>
  </tr>
</table>

## Build Matrix

Primary Windows commands, from `T850/`:

```powershell
.\scripts\build.ps1 -Config Debug   -Platform x64
.\scripts\build.ps1 -Config Release -Platform x64
.\scripts\build.ps1 -Config Debug   -Platform ARM64
.\scripts\build.ps1 -Config Release -Platform ARM64
```

Run the exact local Windows PR/CI matrix, including full-solution builds and all runnable self-tests:

```powershell
.\scripts\RunWindowsBuildMatrix.ps1
```

Outputs:

```text
T850/Lib/<Config>/<Platform>/Framework.lib
T850/Lib/<Config>/<Platform>/FrameworkImGui.lib
T850/bin/<Platform>/<Config>/DayScene.exe
T850/bin/<Platform>/<Config>/T8ditor.exe
```

MSBuild/`.vcxproj` is primary on Windows. Android and Steam Deck use CMake; new Framework sources must be registered in both systems.

## Tests and Visual Regression

Gameplay self-tests:

```powershell
Set-Location T850
& .\bin\x64\Release\DayScene.exe --game-selftest
```

Expected: 78 `PASS` lines and exit code 0.

Cross-backend compute/readback smoke test:

```powershell
& .\bin\x64\Debug\DayScene.exe --compute-selftest --api d3d12 --d3d12debug
```

Equivalent implementations exist for `d3d11`, `vulkan`, `webgpu`, and desktop `gl` 4.3+. The browser CI separately runs WebGPU compute and device-loss recovery in native x64 and ARM64 Edge.

Full deterministic candidate capture and comparison:

```powershell
.\scripts\CaptureVisualBaselines.ps1 -RunSet candidate -Force -ContinueOnError
.\scripts\CompareVisualBaselines.ps1 -Tolerance 2 -OutputPath .\VisualBaselines\final-comparison.json
```

The accepted 2026-08-19 matrix has 26 comparable 1280x720 captures and zero failures. Hardware/missing-asset skips remain explicit in manifests.

See [Verification gates](documentation/testing/verification.md) and [Visual regression](documentation/debug/visual-regression.md).

## Android

One-time setup from the repository root:

```powershell
.\SetupAndroidToolchain.bat
```

Build/install/launch ARM64 Debug:

```powershell
.\T850\scripts\android\BuildAndroid.bat Debug --abi arm64-v8a --install --launch
```

Local Release builds require signing by default. The fast APK script repacks a previously built APK and debug-keystore signs it; it is a development path, not production publishing.

See [Android build and deployment](documentation/platform/android.md).

## Browser / Emscripten

Build the architecture-neutral browser runtime with heavyweight assets left in the public cloud stores:

```powershell
Set-Location .\T850
.\scripts\BuildWeb.ps1 -Configuration Release -AssetMode Cloud -Clean
node .\web\server.mjs --open --launcher
```

The local server binds only to `127.0.0.1`, supplies the isolation headers required by threaded Wasm, serves tracked scene/shader metadata, and proxies allowlisted model/texture/IBL requests. Opening the HTML directly with `file://` is unsupported.

Tagged releases include a standalone `T850-Web-Release.zip` with `Start-T850-Web.cmd` and `Start-T850-Web.sh`. The same validated `web/` tree is embedded in every Windows ZIP, so **WebGPU + Browser (Emscripten)** works directly from `T850Launcher.exe`. Node.js 20+ and a WebGPU-capable browser are required.

See [Browser runtime and packaging](documentation/platform/browser.md).

## Steam Deck

On a Podman-capable Linux/Deck host:

```bash
cd T850
./steamdeck/BuildSteamRuntime.sh --configuration Release
./steamdeck/T850.sh --game-mode --scene 1
./steamdeck/PackageSteamDeckRelease.sh --configuration Release --skip-build
```

The official build uses Valve SteamRT `sniper`, Clang 16, and libc++. A Windows SSH orchestrator can prepare/build/deploy/run on a remote Deck.

See [Steam Deck build and deployment](documentation/platform/steam-deck.md).

For a local Windows-to-Deck development update, configure ignored `deckConfig.json` and run `./UpdateSteamDeck.ps1`. Add `-Run` to launch the updated Minecraft scene after the SteamRT build.

## Cloud Assets

Download runtime assets:

```powershell
.\LaunchSolution.bat --assets-only
```

Download all cloud models:

```powershell
.\LaunchSolution.bat --all-models --assets-only
```

Downloads validate size/SHA-256 when present and atomically replace missing/invalid files. See [Cloud asset workflow](documentation/development/cloud-assets.md).

## CI and Releases

`.github/workflows/build.yml` builds:

- Windows Win32/x64/ARM64 crossed with Debug/Release;
- Android arm64-v8a and x86_64 Release APKs;
- Steam Deck SteamRT Release and tarball package;
- Emscripten Release/Wasm, prepared WebGPU shaders, cloud catalog and portable web ZIP;
- WebGPU compute and device-loss recovery in native x64 and ARM64 Edge.

A `v*` tag creates a GitHub Release containing Windows ZIPs, Android APKs, the
Steam Deck tarball, standalone browser ZIP, compiled launcher, and `SHA256SUMS.txt`.
Every Windows ZIP also contains the browser runtime. Before
publication, `ValidateReleasePackages.ps1` opens every archive, verifies required
executables/native libraries/assets/browser entries, validates credential-free HTTPS
cloud routes, rejects bundled heavyweight web payloads, and rejects unsigned tagged APKs.

## Known Limits

- T8ditor does not use WebGPU; it supports D3D11, D3D12, OpenGL and Vulkan.
- Browser packages require HTTP serving, Node.js 20+, WebGPU, and internet access for cloud-hosted heavyweight assets.
- Android and Steam Deck builds/package checks run in CI; hardware install, graphics and performance acceptance still require equipped devices.
- Fast in-memory Play, gameplay hot reload, visual state-machine graph authoring, and cross-scene persistent gameplay entities are not implemented.
- Minecraft water currently uses a static atlas frame; fluid simulation, transparent sorting and voxel lighting propagation remain future work.
- Heavy Q3 Vulkan captures can exceed a 2 GiB audit GPU; hardware-limit skips are not counted as passes.

## Repository Layout

```text
documentation/                 Current authoritative documentation
T850/
  T850.sln                     Windows solution
  Framework/                   Core rendering/game/physics/navigation library
  FrameworkImGui/              Reusable ImGui layer
  DayScene/                    Runtime executable and scene hosts
  T8ditor/                     Editor executable
  Assets/                      Authored and downloaded runtime resources
  scripts/                     Build, launcher, asset, dump, comparison tools
  web/                         Browser launcher, loopback server and deployment tools
  android/                     Gradle/NativeActivity project
  steamdeck/                   SteamRT build/run/package/launcher scripts
```

## Documentation

Start with [Documentation index](documentation/README.md) and [Current status](documentation/current-status-and-roadmap.md). For local agents, the workspace skills route build/run, visual regression, and platform deployment into focused procedures.

## License

T850 is licensed under the [MIT License](LICENSE). Third-party dependencies retain
their own licenses and notices.
