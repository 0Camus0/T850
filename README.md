# T850

[![Build](https://github.com/0Camus0/T850/actions/workflows/build.yml/badge.svg)](https://github.com/0Camus0/T850/actions/workflows/build.yml)

A cross-platform 3D rendering engine written in C++ with a deferred rendering pipeline, PBR materials, four graphics backends, and a built-in scene editor. Originally authored by **Daniel Enriquez** (2017) and actively developed since.

<p align="center">
  <img src="screenshots/Sponza1.png" alt="T850 — Sponza Atrium with deferred shading, HDR bloom, and volumetric lighting" width="100%">
</p>

## What Is This?

T850 started as a learning project to understand how real-time rendering works under the hood — from raw vertex buffers to full deferred pipelines with post-processing. Over the years it has grown into a multi-API engine that runs the same scene on **D3D11, D3D12, OpenGL, and Vulkan**, loads **glTF 2.0** models with PBR materials, and includes a **built-in editor** for tweaking scenes in real time. It's not trying to be Unity or Unreal — it's a playground for graphics programming where every line of the rendering code is yours to read, break, and learn from.

## Screenshots

<p align="center">
  <img src="screenshots/Sponza2.png" alt="SSAO and shadow mapping in the Sponza atrium" width="100%">
  <br><em>SSAO, PCF shadow mapping, and deferred lighting in the Sponza atrium</em>
</p>

<p align="center">
  <img src="screenshots/Editor.png" alt="T850 Editor with scene hierarchy and gizmos" width="100%">
  <br><em>Built-in scene editor (T8ditor) with ImGui, gizmos, grid, and real-time parameter tuning</em>
</p>

<p align="center">
  <img src="screenshots/PBR1.png" alt="PBR metallic-roughness rendering with IBL" width="48%" style="display:inline-block">
  <img src="screenshots/PBR2.png" alt="PBR model with environment reflections" width="48%" style="display:inline-block">
  <br><em>PBR metallic-roughness workflow with image-based lighting — Sandbox scene with orbit camera and cubemap selector</em>
</p>

## Features

### Graphics Backends

| Backend | Status | Notes |
|---------|--------|-------|
| **Direct3D 11** | Stable | Full HLSL pipeline, runtime API switching |
| **Direct3D 12** | Stable | Triple-buffered, PSO cache, ring buffer CB allocator, debug layer |
| **Vulkan** | Stable | HLSL→SPIR-V via glslang, VMA memory management, triple-buffered |
| **OpenGL** (GLEW) | Stable | Desktop GL 3.3+, GLSL shaders with automatic attribute parsing |

All four backends share the same shader logic, scene code, and render graph — switch between them at launch with a single flag.

### Rendering Pipeline

- **Deferred shading** — G-Buffer with 5 color attachments (albedo, normals, PBR data, geometric normals, depth) plus shadow accumulation
- **PBR materials** — Metallic-roughness workflow with GGX normal distribution, Schlick-GGX geometry, Fresnel-Schlick; separate skybox and IBL intensity controls
- **Image-Based Lighting** — Specular reflections from cubemap mip chain, approximate diffuse irradiance from high-mip env sampling
- **Shadow mapping** — Directional light with configurable PCF kernel (radius, samples, scale)
- **SSAO** — Screen-space ambient occlusion with hemisphere sampling and noise texture
- **HDR pipeline** — Luminance map → adaptive exposure → Reinhard tone mapping → bloom extraction → Gaussian blur → composite
- **Bloom** — Bright-pass threshold with configurable factor and white level
- **Depth of Field** — Circle-of-Confusion based near/far blur with auto-focus option
- **Volumetric lighting / God rays** — Screen-space ray marching with Mie scattering
- **Parallax mapping** — Configurable sample count, height scale, self-shadowing with soft shadows
- **Gaussian blur** — Separable blur with runtime-configurable kernel size, radius, and sigma
- **Lens flare** and **vignette** post-processing
- **Fade transitions** between scenes
- **Render graph** — JSON-driven multi-pass pipeline definition (render targets, attachments, blend/depth/cull states, texture bindings)

### glTF 2.0 Loader

Built from scratch with no third-party glTF library:

- **Formats**: `.gltf` (JSON + external bins) and `.glb` (binary container)
- **Draco decompression** — `KHR_draco_mesh_compression` with parallel decode
- **MikkTSpace tangents** — Generated at load time when tangents are missing
- **Parallel image decode** — stb_image on a thread pool for fast texture loading
- **PBR material extraction** — Base color, metallic-roughness, normal, occlusion, emissive maps
- **Static meshes** — Full vertex attribute support (position, normal, tangent, texcoord, color)

### Scene Editor (T8ditor)

- ImGui-based editor with gizmos (translate, rotate, scale)
- Real-time parameter tuning via GUI sliders, checkboxes, and selectors
- Grid overlay and line renderer for debugging
- Scene serialization to `.t8scene` JSON format
- LDR deferred pass with depth-discard for clean viewport

### Engine Systems

- **AABB frustum culling** — Per-subset bounding boxes with frustum plane extraction; per-frame culling stats
- **GUI system** — Atlas-backed slider bars, checkboxes, selectors, and popup text editing with layout serialization and snap-to-grid editing mode
- **Orbit camera** — Sandbox scene with left-drag rotate, right-drag zoom, auto-framing to model bounding sphere
- **Cubemap selector** — Runtime environment map switching (DDS RGBA16F HDR cubemaps + DXT compressed sky cubemaps)
- **Spline system** — Catmull-Rom splines with agents, per-control-point velocity, and camera attachment for cinematic fly-throughs
- **Snapshot / replay** — Frame dumper with RT-level BMP/PPM export, matrix replay for deterministic camera positioning
- **Profiler** — GPU + CPU per-scope timing, draw call and triangle counts (opt-in via `--profile`)
- **Logging** — Thread-safe, multi-backend (console with ANSI color, VS Output window, file), 5 severity levels, timestamps with PID:TID and RAM usage
- **Scene descriptors** — JSON-defined scenes with cameras, lights, splines, meshes, render settings, and slider/checkbox/selector GUI definitions
- **GUI atlas generator** — Packs individual GUI textures into a single atlas with edge extrusion and configurable max sprite size
- **Text rendering** — stb_truetype with screen-space text, used for FPS counter and debug overlays

### Build & Tooling

- **CI pipeline** — GitHub Actions building x86/x64/ARM64 × Debug/Release on Windows via MSBuild + vcpkg
- **WPF Launcher** — GUI launcher for selecting API, scene, model, resolution, snapshot settings, and log level; dev version includes build button and editor launch
- **LaunchSolution.bat** — One-click setup: clones vcpkg, installs dependencies (GLEW, SDL3, Draco, MikkTSpace, ImGui), opens VS solution
- **Dual shader languages** — HLSL for D3D11/D3D12/Vulkan, GLSL for OpenGL; technique XML files define shader profiles with per-profile preprocessor defines

## Demo Scenes

| Scene | Description |
|-------|-------------|
| **Sandbox** | glTF model viewer with orbit camera, PBR rendering, cubemap selector, and full HDR pipeline. Load any `.glb`/`.gltf` from the Models dropdown. |
| **Day** | Sponza atrium lit by directional sun + warm point lights; shadows, bloom, DOF, god rays, SSAO, parallax mapping, lens flare; spline camera fly-through |
| **Night** | Night variant with omnidirectional shadow mapping (cubemap depth), moving light agent on a spline path |
| **Tech** | Technical showcase scene |

## Project Structure

```
T850/
├── screenshots/                # README screenshots
├── .github/workflows/          # CI pipeline (build.yml)
├── LaunchSolution.bat          # One-click dev setup
├── ARCHITECTURE.md             # Detailed architecture documentation
├── T850/
│   ├── Assets/
│   │   ├── Fonts/              # TrueType fonts
│   │   ├── Layouts/            # GUI layout + atlas JSON
│   │   ├── Models/             # .X and .glb/.gltf models
│   │   ├── Scenes/             # Scene descriptors + render graphs (JSON)
│   │   ├── Shaders/            # GLSL + HLSL shader pairs
│   │   ├── Techniques/         # XML shader technique definitions
│   │   └── Textures/           # DDS cubemaps, PBR textures
│   ├── DayScene/               # Application + scene implementations
│   │   ├── App.cpp             # main(), CLI parsing, framework bootstrap
│   │   ├── Application.cpp/h   # App lifecycle, fade, scene management
│   │   ├── SC_SandBox.cpp/h    # Sandbox (glTF viewer + orbit camera)
│   │   ├── SC_Day.cpp/h        # Day scene (Sponza)
│   │   ├── SC_Night.cpp/h      # Night scene
│   │   └── SC_Tech.cpp/h       # Tech scene
│   ├── Framework/              # Engine library
│   │   ├── Config.h            # Compile-time configuration
│   │   ├── include/
│   │   │   ├── core/           # Platform abstraction (Win32, Linux)
│   │   │   ├── scene/          # Primitives, render graph, GUI, text, splines
│   │   │   ├── utils/          # Math, camera, timer, input, glTF loader, profiler, log
│   │   │   └── video/          # Base driver + D3D11, D3D12, Vulkan, GL backends
│   │   └── src/                # Implementation (mirrors include/)
│   ├── Librerias/              # Third-party libraries (vcpkg, GLEW, SDL3, stb, etc.)
│   ├── scripts/                # Build scripts, launchers, analysis tools
│   └── bin/                    # Build outputs (x86, x64, ARM64)
```

## Building from Source

### Quick Start (Windows)

1. **Clone:**
   ```bash
   git clone https://github.com/0Camus0/T850.git
   cd T850
   ```

2. **Run the setup script** (clones vcpkg, installs dependencies, opens VS):
   ```
   LaunchSolution.bat
   ```

3. **Build** in Visual Studio (Release / x64) or via MSBuild:
   ```powershell
   msbuild T850/T850.sln /p:Configuration=Release /p:Platform=x64 /m
   ```

4. **Run:**
   ```
   T850/bin/x64/Release/DayScene.exe --api d3d12
   ```
   Or use the **Launcher** (`T850/T850Launcher.exe`) for a GUI.

### Command-Line Options

```
--api <d3d11|d3d12|vulkan|gl>    Graphics backend (default: d3d11)
--scene <0|1>                     Scene index (0=Sandbox, 1=Day)
--model <path>                    glTF model for Sandbox (default: Models/DamagedHelmet.glb)
--width <W> --height <H>          Window resolution
--fullscreen                      Launch in fullscreen
--logLevel <error|info|debug|verbose|trace>
--d3d12debug                      Enable D3D12 debug/validation layer
--profile                         Enable GPU+CPU profiler
--debugFrames                     Spacebar pauses, dumps RTs, and exits
```

### Linux (CMake)

```bash
cd T850/build
cmake .. -DHEADLESS=OFF
make
```

Headless mode (`-DHEADLESS=ON`) enables EGL/GBM offscreen rendering for CI.

## Controls

| Key | Action |
|-----|--------|
| `W` / `A` / `S` / `D` | Move camera (FPS mode) |
| `Q` / `E` | Move up / down |
| Mouse drag | Orbit (Sandbox) or look around (Day/Night) |
| `G` | Toggle GUI overlay |
| `Tab` | Save current scene settings to JSON |
| `C` | Toggle scene / light camera |
| `K` | Print camera position |
| `Space` | Dump RT snapshots (when `--debugFrames`) |

## Third-Party Libraries

| Library | Purpose |
|---------|---------|
| **SDL3** | Window management, input, GL context |
| **GLEW 2.0** | OpenGL extension loading |
| **glaze** | Fast JSON parsing (scene descriptors) |
| **stb** | Image loading (`stb_image`), font rendering (`stb_truetype`) |
| **MikkTSpace** | Tangent generation for glTF models |
| **Draco** | Google's mesh compression (glTF extension) |
| **tinyxml2** | XML parsing for shader techniques |
| **VMA** | Vulkan Memory Allocator |
| **glslang** | HLSL → SPIR-V compilation for Vulkan |
| **ImGui / ImGuizmo** | Editor UI and 3D gizmos |
| **vcpkg** | Package manager (bundled in repo) |

## License

This project is released under [The Unlicense](https://unlicense.org) — dedicated to the public domain. See [LICENSE.md](LICENSE.md) for details.
# T850

[![Build](https://github.com/0Camus0/T850/actions/workflows/build.yml/badge.svg)](https://github.com/0Camus0/T850/actions/workflows/build.yml)

A cross-platform 3D rendering engine written in C++ featuring a deferred rendering pipeline with multiple post-processing effects. Originally authored by **Daniel Enriquez** (2017).

## Overview

T850 is a lightweight 3D graphics framework built around a multi-pass deferred rendering architecture. It supports runtime switching between graphics APIs, loads DirectX `.X` model files, and ships with three demo scenes (Day, Night, and Tech) that showcase its rendering capabilities including shadow mapping, bloom, SSAO, depth of field, volumetric lighting, and more.

## Features

### Rendering Pipeline
- **Deferred shading** with G-Buffer (4 color attachments + depth)
- **Shadow mapping** with configurable PCF (Percentage Closer Filtering)
- **Gaussian blur** with configurable kernel size, radius, and sigma
- **Bloom / HDR** tone mapping with bright pass extraction
- **SSAO** (Screen Space Ambient Occlusion) with noise texture
- **Depth of Field** (Circle of Confusion based, near/far)
- **Volumetric lighting / God rays** via ray marching
- **Parallax mapping** (configurable low/high samples and height)
- **Lens flare**
- **Vignette** post-process
- **Environment / Cube maps** (DDS format, DXT1/DXT3/DXT5)
- **Fade transitions** between scenes
- **Three quality presets**: High, Medium, Low

### Lighting
- **Blinn-Phong** specular model (Phong also available)
- Ambient, Diffuse, Specular, and Fresnel components
- Multiple point lights (up to 127)
- Omnidirectional shadow support (cube map depth)
- PBR-ready material maps: Diffuse, Specular, Gloss, Normal, Height

### Engine Architecture
- **Scene management**: Base class (`SceneBase`) with virtual lifecycle (Init, Load, Update, Draw, Input, Destroy)
- **Primitive system**: Managed primitives (Meshes, Quads, Cubes, Triangles, Splines) with instancing
- **Spline system**: Catmull-Rom splines with agents, velocity per control point, and camera attachment
- **Text rendering** via stb_truetype (TrueType font loading, screen-space text)
- **XML-based shader techniques**: Define shader profiles (GLSL/HLSL) with per-profile defines
- **GLSL parser**: Automatic attribute/uniform/varying extraction from shader source
- **Resource manager**: Centralized loading of `.X` model databases
- **Custom image loader** (CIL): Supports DDS (DXT1/3/5, cubemaps), PVR (PVRTC), ETC1/ETC2, and raw image formats via stb_image
- **Input manager**: Keyboard and mouse input abstraction
- **Camera system**: Perspective and orthographic projections, FPS-style controls, spline-attached camera animation
- **Configurable at compile time**: Debug flags, texture quality, driver selection, window manager
- **Headless rendering mode**: EGL/GBM-based offscreen rendering for CI and automated screenshot capture
- **Runtime API switching**: Press `1` for D3D11, `2` for OpenGL (Windows)

## Graphics Backends

| Backend | Platforms | Notes |
|---------|-----------|-------|
| **OpenGL** (via GLEW) | Windows, Linux | Desktop OpenGL |
| **OpenGL ES 2.0** | Windows, Linux | Via EGL + PowerVR emulation (Windows) |
| **OpenGL ES 3.0** | Windows, Linux | Default driver selection |
| **OpenGL ES 3.1** | Linux | Forced on Linux builds |
| **Direct3D 11** | Windows | Full D3D11 device/context, HLSL shaders |

Configured in `Framework/Config.h` via `GL_DRIVER_SELECTED`:
```c
#define OGLES20 2
#define OGLES30 3
#define OGLES31 4
#define OGL     5

#define GL_DRIVER_SELECTED OGLES30
```

## Shader Languages

Shaders are provided in dual formats under `Assets/Shaders/`:

- **GLSL** (`.glsl`) — Used by all OpenGL / OpenGL ES backends
- **HLSL** (`.hlsl`) — Used by the Direct3D 11 backend

Shader pairs include: `VS_Mesh`/`FS_Mesh`, `VS_Quad`/`FS_Quad`, `VS_Text`/`FS_Text`, `VS_W`/`FS_W`, and others. The technique system (`Assets/Techniques/*.xml`) allows defining shader profiles with per-profile preprocessor defines.

## 3D Model Format

The engine loads **DirectX `.X` files** via a custom parser (`XDataBase`), supporting:
- Mesh geometry with normals, tangents, binormals
- Texture coordinates (multiple sets)
- Materials with effect instances
- Skeletal animation (skin weights, animation sets with rotation/scale/position keys)
- Hierarchical frame transforms

## Demo Scenes

| Scene | Description |
|-------|-------------|
| **SC_Day** | Sponza atrium lit by directional sun light + warm point lights; full deferred pipeline with shadows, bloom, DOF, god rays, SSAO, lens flare; spline camera fly-through |
| **SC_Night** | Night variant with omnidirectional shadow mapping (cube map), moving light agent on a spline path |
| **SC_Tech** | Technical showcase scene |

Scenes auto-transition after a set period (150 seconds for Day → Night).

## Project Structure

```
T850/
├── LICENSE.md                  # The Unlicense (public domain)
├── Assets/
│   ├── Fonts/                  # TrueType fonts
│   ├── Models/                 # .X model files (Sponza, SkyBox, etc.)
│   ├── Shaders/                # GLSL + HLSL shader pairs
│   ├── Techniques/             # XML technique definitions
│   └── Textures/               # DDS cube maps and textures
├── DayScene/                   # Application entry point + scene implementations
│   ├── App.cpp                 # main(), framework bootstrap
│   ├── Application.cpp/.h      # App class (lifecycle, fade, text, scene mgmt)
│   ├── SC_Day.cpp/.h           # Day scene
│   ├── SC_Night.cpp/.h         # Night scene
│   └── SC_Tech.cpp/.h          # Tech scene
├── Framework/                  # Engine library
│   ├── Config.h                # Compile-time configuration
│   ├── T8_descriptors.h        # Enums, signatures, buffer descriptors
│   ├── include/
│   │   ├── core/               # Framework base (Win32, Linux)
│   │   ├── scene/              # Primitives, meshes, quads, lens flare, text, splines
│   │   ├── utils/              # Math, camera, timer, splines, XDataBase, techniques, input
│   │   └── video/              # Base driver, GL driver, D3D11 driver, render targets, textures, shaders
│   └── src/                    # Implementation files (mirrors include/ layout)
├── Librerias/                  # Third-party libraries
└── bin/                        # Build outputs (x86, x64, ARM64)
```

## Third-Party Libraries

| Library | Purpose |
|---------|---------|
| **SDL3** | Window management, input, OpenGL context (Windows) |
| **SDL 1.2.14** | Legacy window management (Linux) |
| **freeglut** | Alternative window management (Linux) |
| **GLEW 2.0.0** | OpenGL extension loading |
| **PowerVR OGLES2 SDK** | OpenGL ES emulation on desktop |
| **stb** | `stb_truetype` (font rendering), `stb_image` (image loading) |
| **tinyxml2** | XML parsing for shader technique files |
| **vcpkg** | Package manager (bundled) |

## Building from Source

### Prerequisites (Windows)

- **Visual Studio 2022** (or Build Tools) with the **Desktop development with C++** workload
- **Git** (for cloning vcpkg)

### Steps

1. **Clone the repository:**
   ```bash
   git clone https://github.com/0Camus0/T850.git
   cd T850
   ```

2. **Set up vcpkg and install dependencies:**
   ```powershell
   cd T850/Librerias/vcpkg
   ./bootstrap-vcpkg.bat -disableMetrics

   # For x64 builds:
   ./vcpkg install glew:x64-windows-static angle:x64-windows

   # For x86 builds:
   ./vcpkg install glew:x86-windows-static angle:x86-windows

   # For ARM64 builds:
   ./vcpkg install glew:arm64-windows-static angle:arm64-windows
   ```

3. **Build with MSBuild:**
   ```powershell
   cd ../../..   # back to T850/ root
   msbuild T850/T850.sln /p:Configuration=Release /p:Platform=x64 /m
   ```
   Or open `T850/T850.sln` in Visual Studio and build from there.

4. **Run:**
   ```
   T850/bin/x64/Release/DayScene.exe
   ```

### Build Configurations

| Platform | Configuration | Output |
|----------|--------------|--------|
| x86 | Debug / Release | `bin/x86/Debug/` or `bin/x86/Release/` |
| x64 | Debug / Release | `bin/x64/Debug/` or `bin/x64/Release/` |
| ARM64 | Debug / Release | `bin/arm64/Debug/` or `bin/arm64/Release/` |

The build automatically creates junction links from the output directory to `Assets/` subdirectories and copies the required DLLs (SDL3, libEGL, libGLESv2, zlib1).

### Linux (CMake + Make)
- **Window managers**: freeglut, Wayland (native EGL)
- **Headless mode**: EGL/GBM offscreen rendering (compile with `-DHEADLESS=ON`)
- **Standard**: C++11

```bash
cd T850/build
cmake .. -DHEADLESS=OFF
make

# Headless build (CI/testing)
cmake .. -DHEADLESS=ON
make
```

## Controls

| Key | Action |
|-----|--------|
| `W` / `A` / `S` / `D` | Move camera forward / left / backward / right |
| `Q` / `E` | Move camera up / down |
| Mouse | Look around (yaw / pitch) |
| `C` | Toggle between scene camera and light camera |
| `K` | Print camera position and orientation |
| `B` / `N` | Cycle through rendering settings |
| `+` / `-` (numpad) | Adjust selected setting value |
| `1` | Switch to Direct3D 11 |
| `2` | Switch to OpenGL |

Adjustable settings include: exposure, bloom factor, number of lights, Gaussian kernel parameters, PCF radius/samples, SSAO kernel size/radius, DOF aperture/focal length/CoC, parallax samples/height, and light volume steps.

## License

This project is released under [The Unlicense](https://unlicense.org) — dedicated to the public domain. See [LICENSE.md](LICENSE.md) for details.
