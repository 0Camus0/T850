# Review and Gap Pass

Status: Stage 12 draft.

This document is the final documentation review pass for Stages 0-11. It records validation performed, coverage that looks solid, high-confidence gaps that remain, a troubleshooting index, and cross-subsystem limitations to keep visible for future agents.

## Validation performed

Validation completed in this pass:

- Re-read current documentation map and stage plan.
- Ran a Markdown relative-link audit across `documentation/**/*.md`; all relative links resolve.
- Ran a code/file path reference audit for inline paths; corrected stale path references and re-ran successfully.
- Reviewed completed subsystem docs against source inventories for architecture, geometry, shaders, render graph, geometry rendering, animation, physics, navigation, editor, scenes, and cross-system dependency map.
- Ran two independent source-coverage review agents:
  - one focused on documentation coverage;
  - one focused on runtime subsystems that might be missing.
- Directly checked flagged gap areas in source:
  - `Framework/include/debug/*`
  - `Framework/src/debug/*`
  - `Framework/src/utils/ResourceLocator.cpp`
  - `Framework/include/utils/InputManager.h`
  - `Framework/include/utils/CameraProfiles.h`
  - `Framework/include/utils/HandheldControllerOverlay.h`
  - `FrameworkImGui/src/ImGuiSystem.cpp`
  - `FrameworkImGui/src/DevGuiContext.cpp`

## Coverage that looks strong

These areas have enough detail for future agents to continue work confidently:

| Area | Coverage |
|---|---|
| Architecture/platform lifecycle | [Main architecture](architecture/main-architecture.md), [Platform event loop](architecture/platform-event-loop.md) |
| Geometry import to render-ready data | [Loading geometry](geometry/loading-geometry.md) |
| Shader keys, shader cache, reflection, API-specific compilation | [Shader management](rendering/shader-management.md) |
| Render graph schema and execution | [Render graph](rendering/render-graph.md) |
| `PrimitiveInst`/mesh draw flow | [Geometry rendering flow](rendering/geometry-rendering-flow.md) |
| Animation/skinning/bone texture | [Animation system](animation/animation-system.md) |
| Jolt physics, ragdolls, static collision, character sweep controller | [Jolt physics](physics/jolt-physics.md) |
| Recast/Detour navigation, volumes, links, `.t8nav` | [NavMesh and Detour](navigation/navmesh-detour.md) |
| T8ditor authoring workflow | [Editor overview](editor/editor-overview.md) |
| `.t8scene`, SceneTemplate, profiles, scene variants | [Scene format and runtime](scenes/scene-format-and-runtime.md) |
| Cross-subsystem flows | [Dependency map](dependency-map.md) |
| Resource lookup, Android packaged assets, generated cache paths | [Resource locator and cache paths](architecture/resource-locator.md) |
| Input, controllers, camera profiles, and hosted viewport routing | [Input, camera, and controls](input/camera-and-controls.md) |
| FrameworkImGui backend, runtime UI, and hosted scene panels | [FrameworkImGui runtime UI](editor/imgui-system.md) |
| Texture loading, sampler state, IBL resources, and cubemap profiles | [Textures, samplers, and IBL](rendering/textures-and-ibl.md) |
| SceneSetup descriptors, runtime control metadata, and `SceneProps` mapping | [SceneSetup descriptors](scenes/scene-setup-descriptors.md) |

## High-confidence documentation gaps

These are real gaps discovered by reviewing source coverage. They do not invalidate Stages 1-11, but they should become future documentation stages or appendix sections.

### 1. Debug and diagnostics subsystem

Addressed by [Debug and diagnostics](debug/diagnostics.md) in Stage 13. The notes below are retained as review history and as a checklist for future diagnostics changes.

No dedicated document currently covers the diagnostic stack.

Relevant code:

- `Framework/include/debug/LoadingProgress.h`
- `Framework/include/debug/RuntimeTelemetry.h`
- `Framework/include/debug/FrameDumper.h`
- `Framework/include/debug/RenderTrace.h`
- `Framework/include/debug/Profiler.h`
- `Framework/src/debug/RuntimeTelemetry.cpp`
- `Framework/src/debug/FrameDumper.cpp`
- `Framework/src/debug/FrameDumperIO.cpp`
- `Framework/src/debug/RenderTrace.cpp`
- `Framework/src/debug/Profiler.cpp`

What to document:

- loading progress snapshots and render callback,
- runtime telemetry counters/timers/output JSON,
- frame dump and replay snapshot format,
- render trace events and PSO/shader/buffer/RT capture,
- profiler scopes and draw-call counting,
- how these systems are used from DayScene, SceneTemplate, T8ditor, render graph, physics, navigation, and shaders.

Suggested future file:

- `documentation/debug/diagnostics.md`

### 2. Resource lookup and path resolution

Addressed by [Resource locator and cache paths](architecture/resource-locator.md) in Stage 14. The notes below are retained as review history and as a checklist for future path/cache changes.

Geometry docs mention `ResourceManager`, but there is no focused explanation of resource path normalization, lookup roots, Android packaged assets, cache path resolution, and recursive fallback behavior.

Relevant code:

- `Framework/include/utils/ResourceLocator.h`
- `Framework/src/utils/ResourceLocator.cpp`
- `Framework/src/utils/ResourceManager.cpp`
- `Framework/src/scene/EditorSceneFile.cpp`

What to document:

- `Assets/` stripping and path normalization,
- base path and cache path resolution,
- desktop file lookup vs Android asset manager lookup,
- case-insensitive Android asset fallback,
- `ReadText`, `ReadBinary`, `Exists`, `ResolveFilePath`, `ResolveCachePath`,
- how `.t8scene`, shader cache, mesh preprocess cache, `.t8jolt`, `.t8nav`, textures, and glTF buffers depend on this layer.

Suggested future section/file:

- add a "Resource lookup and cache paths" section to [Main architecture](architecture/main-architecture.md), or create `documentation/architecture/resource-locator.md`.

### 3. Input, controller mapping, and camera profiles

Addressed by [Input, camera, and controls](input/camera-and-controls.md) in Stage 15. The notes below are retained as review history and as a checklist for future input/camera changes.

Input is mentioned in platform docs, but camera profile behavior and controller/handheld overlay paths are underdocumented.

Relevant code:

- `Framework/include/utils/InputManager.h`
- `Framework/include/utils/CameraProfiles.h`
- `Framework/src/utils/CameraProfiles.cpp`
- `Framework/include/utils/HandheldControllerOverlay.h`
- framework platform input paths in Windows/Linux/Android framework files.

What to document:

- keyboard/mouse/gamepad state model in `InputManager`,
- active camera profiles: Orbit, Free Fly, Colliding Fly, Grounded FPS, COD FPS, Quake 3 FPS,
- how SceneTemplate and editor select/use camera profiles,
- handheld/controller overlay behavior,
- Android/touch/controller differences.

Suggested future file:

- `documentation/input/camera-and-controls.md`

### 4. FrameworkImGui runtime UI layer

Addressed by [FrameworkImGui runtime UI](editor/imgui-system.md) in Stage 16. The notes below are retained as review history and as a checklist for future ImGui/backend changes.

Stage 9 covers T8ditor's editor UI, but the reusable FrameworkImGui layer deserves explicit coverage.

Relevant code:

- `FrameworkImGui/include/imgui/ImGuiSystem.h`
- `FrameworkImGui/src/ImGuiSystem.cpp`
- `FrameworkImGui/include/imgui/DevGuiContext.h`
- `FrameworkImGui/src/DevGuiContext.cpp`
- editor hosted viewport code in `T8ditor/HostedViewportPanel.*`

What to document:

- SDL/Android platform backend setup,
- renderer backend setup for D3D11/D3D12/GL/Vulkan,
- docking and viewport enablement,
- Android native window rebind,
- `DevGuiContext`,
- relationship between runtime debug UI and T8ditor panels.

Suggested future file:

- `documentation/editor/imgui-system.md` or `documentation/architecture/imgui-runtime.md`

### 5. Texture, IBL, and material asset loading beyond mesh materials

Addressed by [Textures, samplers, and IBL](rendering/textures-and-ibl.md) in Stage 17. The notes below are retained as review history and as a checklist for future texture/IBL changes.

The geometry/shader/render docs cover material slots and texture binding, but not the broader texture and IBL loading path as a standalone topic.

Relevant code:

- API texture implementations under `Framework/src/video/*/*Texture.cpp`
- texture creation in `BaseDriver.cpp`
- IBL/environment slots in `RenderGraph.h`
- SceneSetup environment map fields in `SceneDescriptor.h`
- editor cubemap/profile selection in `EditorApp.cpp`

What to document:

- texture file formats and loader behavior,
- cubemap/float texture paths,
- IBL diffuse/specular/BRDF/Charlie/sheen LUT slots,
- API-specific sampler behavior,
- editor/runtime cubemap profile overrides.

Suggested future section:

- extend [Shader management](rendering/shader-management.md) or create `documentation/rendering/textures-and-ibl.md`.

### 6. SceneSetup bridge details

Addressed by [SceneSetup descriptors](scenes/scene-setup-descriptors.md) in Stage 18. The notes below are retained as review history and as a checklist for future descriptor changes.

Stage 10 covers `SceneDescriptor` and `SceneSetup`, but future readers would benefit from a short deeper appendix because this is the old runtime descriptor path still used by DayScene/Quake3Mock/T8ditor render controls.

Relevant code:

- `Framework/include/scene/SceneDescriptor.h`
- `Framework/src/scene/SceneDescriptor.cpp`
- `Framework/include/scene/SceneSetup.h`
- `Framework/src/scene/SceneSetup.cpp`

What to add:

- exact mapping from descriptor quality/settings to `SceneProps`,
- how runtime UI sliders/checkboxes/selectors are defined by descriptor JSON,
- how SceneTemplate combines `.t8scene` profiles with `SceneSetup`.

Suggested location:

- add a deeper appendix to [Scene format and runtime](scenes/scene-format-and-runtime.md).

## Things checked that are not missing

- No engine-owned audio subsystem was found under `Framework` or `DayScene`; only vendored/SDL audio references appear. There is no meaningful T850 audio system to document right now.
- The physics documentation correctly states that current character movement is T850's kinematic sweep controller, not a direct Jolt `CharacterVirtual` wrapper.
- The render docs correctly separate shader compilation/cache from D3D12/Vulkan PSO creation.
- The navigation docs correctly distinguish baked `.t8nav` assets from generated cache files.

## Troubleshooting index

| Symptom | Start here |
|---|---|
| App does not create a window, input stalls, resize breaks | [Platform event loop](architecture/platform-event-loop.md) |
| Mesh fails to load or has no geometry | [Loading geometry](geometry/loading-geometry.md) |
| Mesh renders with wrong material/shader | [Shader management](rendering/shader-management.md), [Geometry rendering flow](rendering/geometry-rendering-flow.md) |
| Render target or post-process pass is wrong | [Render graph](rendering/render-graph.md) |
| Draw call exists but nothing appears | [Geometry rendering flow](rendering/geometry-rendering-flow.md) |
| Skinned mesh does not animate | [Animation system](animation/animation-system.md) |
| Ragdoll/physics body is missing | [Jolt physics](physics/jolt-physics.md) |
| Character movement collides incorrectly | [Jolt physics](physics/jolt-physics.md), [Input, camera, and controls](input/camera-and-controls.md) |
| NavMesh has no polygons or links | [NavMesh and Detour](navigation/navmesh-detour.md) |
| T8ditor panel/action behaves incorrectly | [Editor overview](editor/editor-overview.md) |
| Play Scene differs from editor | [Scene format and runtime](scenes/scene-format-and-runtime.md), [Editor overview](editor/editor-overview.md) |
| Asset loads on desktop but not Android, or generated cache paths are confusing | [Resource locator and cache paths](architecture/resource-locator.md) |
| Keyboard/gamepad/touch/camera profile behavior is wrong | [Input, camera, and controls](input/camera-and-controls.md), [Platform event loop](architecture/platform-event-loop.md) |
| Runtime/editor ImGui, docking, platform-window, or hosted panel behavior is wrong | [FrameworkImGui runtime UI](editor/imgui-system.md), [Editor overview](editor/editor-overview.md) |
| Texture, sampler, cubemap, or IBL output differs by backend/platform | [Textures, samplers, and IBL](rendering/textures-and-ibl.md), [Render graph](rendering/render-graph.md) |
| Runtime UI descriptor control or profile override is ignored | [SceneSetup descriptors](scenes/scene-setup-descriptors.md), [Scene format and runtime](scenes/scene-format-and-runtime.md) |
| Cross-subsystem change touches many files | [Dependency map](dependency-map.md) |

## Cross-subsystem limitations summary

These limitations appear in subsystem docs, but are worth keeping together:

- `RenderQueue` is not the active mesh executor yet; `RenderMesh::Draw` still owns the draw walk.
- D3D12/Vulkan PSO creation is draw-state dependent even when shader keys match.
- Physics has no fixed-step accumulator/substep scheduler yet.
- Physics layers are static/non-moving vs moving only.
- Current character movement is a kinematic sweep controller, not direct Jolt `CharacterVirtual`.
- Skinned mesh culling is conservative/limited because bind-pose bounds are not animation-aware.
- Recast build is whole-mesh, not tiled streaming.
- `.t8scene` unknown keys are ignored, so schema typos can be silent.
- Hosted editor windows share some API resources and must be audited carefully for render-state isolation.
- Resource path behavior differs between desktop filesystem lookup and Android packaged assets.

## Recommended follow-up documentation stages

1. **Stage 13** — Debug and diagnostics: loading progress, telemetry, frame dump/replay, render trace, profiler. Completed as [debug/diagnostics.md](debug/diagnostics.md).
2. **Stage 14** — Resource locator and cache path architecture. Completed as [architecture/resource-locator.md](architecture/resource-locator.md).
3. **Stage 15** — Input, controllers, handheld overlay, and camera profiles. Completed as [input/camera-and-controls.md](input/camera-and-controls.md).
4. **Stage 16** — FrameworkImGui runtime UI and platform backend behavior. Completed as [editor/imgui-system.md](editor/imgui-system.md).
5. **Stage 17** — Texture/IBL loading and sampler behavior. Completed as [rendering/textures-and-ibl.md](rendering/textures-and-ibl.md).
6. **Stage 18** — SceneSetup/SceneDescriptor appendix with exact `SceneProps` mappings. Completed as [scenes/scene-setup-descriptors.md](scenes/scene-setup-descriptors.md).
