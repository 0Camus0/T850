# T850

[![Build](https://github.com/0Camus0/T850/actions/workflows/build.yml/badge.svg)](https://github.com/0Camus0/T850/actions/workflows/build.yml)

A cross-platform 3D rendering engine written in C++ with a deferred rendering pipeline, PBR materials, four graphics backends, and a built-in scene editor.

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
