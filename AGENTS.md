# T850 Engine — Agent Instructions

## How to work with this codebase

This is a **3D engine** split into three main executables built on shared Framework libraries. Code lives under `T850/` (subdirectory of the git root). Documentation under `documentation/` is authoritative and up-to-date.

---

## Project layout (`T850/`)

| Directory | Purpose |
|---|---|
| `Framework/src/` | Core engine: rendering, resources, physics (Jolt), navmesh (Recast/Detour), scenes, shaders, animation, input |
| `FrameworkImGui/` | ImGui wrapper — Dear ImGui context init, platform/renderer backends (D3D11/12, GL, Vulkan) |
| `DayScene/` | Runtime demo scene — main entry point for shipping builds |
| `T8ditor/` | Editor executable — authors `.t8scene` files, hosted viewports, undo/redo |
| `Shaders/` | HLSL/GLSL shader sources; compiled to `.t8shadercache` |
| `Assets/`, `Models/`, `Textures/` | Source assets (GLTF/X/DDS, textures, IBL) |
| `Navigation/` | Baked navmesh assets (`.t8nav`) |

---

## 12 key subsystems

1. **Architecture** — Platform event loop, RootFramework/AppBase lifecycle, ResourceLocator (path + cache abstraction)
2. **Geometry loading** — glTF/.x → XDataBase → RenderMesh/XFinalGeometry → PrimitiveManager → GPU meshes
3. **Shader management** — ShaderKey bitfield → feature permutation compilation → `.t8shadercache` disk cache
4. **Render graph** — JSON-driven render passes (render targets, inputs, draws). No hardcoded pass order.
5. **Geometry rendering flow** — RenderMesh/RenderSkinnedMesh draw, MeshDrawStateTracker skip-redundant-binds, PSO management
6. **Textures & IBL** — CIL loader, Texture slots 10-15 (IBL), slots 16-25 (material maps), EnvironmentMapSet
7. **Animation** — AnimationController → bone matrices → bone texture (t24) → skinned draw; ragdoll bridge to Jolt
8. **Physics (Jolt)** — JoltPhysicsSystem wrapper, static triangle meshes from render geometry, ragdolls, `.t8jolt` caches
9. **Navigation** — Recast build + Detour query, NavMeshGeometry, off-mesh links, `.t8nav` baked assets
10. **Editor (T8ditor)** — EditorWorld model, Hierarchy/Inspector panels, Gizmo transforms, Play Scene, UndoStack
11. **ImGui system** — ImGuiSystem owns context/backends; DevGuiContext for runtime UI; platform windows for hosted viewports
12. **Scenes** — `.t8scene` JSON (authored) → EditorSceneFile schema → SceneTemplate runtime loads + plays

---

## Building the project

**Build system: Visual Studio Solution + MSBuild.** The CMake build exists but is secondary; the primary and supported build path is the `.sln` / `.vcxproj` route via MSBuild.

### Prerequisites

- **Visual Studio 2022** (Community / Professional / Enterprise / Build Tools) with C++ workload
- **Windows SDK** (auto-included with VS)
- **PowerShell 5+** (ships with Windows)

### Build commands (PowerShell)

From `T850/T850/` (the directory that contains `T850.sln`):

```powershell
# Build all targets (x64 Release)
.\scripts\build.ps1 -Config Release -Platform x64

# Build x64 Debug
.\scripts\build.ps1 -Config Debug -Platform x64

# Rebuild (clean + build)
.\scripts\build.ps1 -Config Release -Platform x64   # script always does /t:Rebuild
```

The build script (`scripts/build.ps1`) auto-discovers MSBuild, sets parallel workers to `cores - 1`, and colors errors/warnings in the output. Override worker count with `$env:T850_BUILD_WORKERS`.

### GUI launcher

```powershell
.\scripts\Launcher.ps1
```

WPF-based launcher that lets you pick target (Windows/Android), architecture, configuration, graphics API, scene, model, and more. Buttons: **BUILD**, **REBUILD**, **RUN** (DayScene), **EDITOR** (T8ditor). The launcher calls MSBuild under the hood with the same arguments as `build.ps1`.

### Solution structure (`T850.sln`)

| Project | vcxproj | Output |
|---|---|---|
| Framework | `Framework/Framework.vcxproj` | `Lib/<config>/<arch>/Framework.lib` |
| FrameworkImGui | `FrameworkImGui/FrameworkImGui.vcxproj` | `Lib/<config>/<arch>/FrameworkImGui.lib` |
| DayScene | `DayScene/DayScene.vcxproj` | `bin/<arch>/<config>/DayScene.exe` |
| T8ditor | `T8ditor/T8ditor.vcxproj` | `bin/<arch>/<config>/T8ditor.exe` |

Platform mapping in MSBuild: `x64` → `x64`, `x86` → `Win32`, `ARM64` → `ARM64`.

### Running the editor

```powershell
# After building, the editor is at:
.\bin\x64\Release\T8ditor.exe

# Or launch via the Launcher.ps1 "EDITOR" button
```

### Important

- **Never modify CMakeLists.txt files as a hack to fix linker errors.** The `.vcxproj` files are the source of truth. If something doesn't link, check the vcxproj file references and link settings.
- Post-build steps in the vcxproj files create **directory junctions** (symlinks) from the output directory to `Assets/` subdirectories (Shaders, Models, Fonts, Textures, Scenes, Layouts), so the running executable can find assets via relative paths.

---

## Workflow rules

- **Read docs before code.** When a user asks about a feature, first search `documentation/` for that subsystem doc (`grep` or MCP `search_docs`).
- **Use the dependency map** (`documentation/dependency-map.md`) to find what else changes. Every row tells you "read first" and "then read".
- **Use ResourceLocator**, never raw filesystem paths (must work on Android packaged assets).
- **AGENTS.md is your cheat sheet.** Full docs are in `documentation/` — use MCP tools or targeted reads when you need detail.
