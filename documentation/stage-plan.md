# Engine Documentation Stage Plan

This plan breaks the full engine documentation effort into focused stages. Each stage should be small enough for a local agent to inspect a narrow set of files and produce one high-quality Markdown document.

## Stage 0 — Documentation skeleton

Outputs:

- [README.md](README.md)
- [doc-conventions.md](doc-conventions.md)
- [glossary.md](glossary.md)
- `documentation/` folder structure.

Status: complete.

## Stage 1 — Main architecture

Target files:

- `T850/Framework/include/core/*`
- `T850/Framework/src/core/*`
- `T850/Framework/include/scene/SceneProp.h`
- `T850/Framework/src/scene/SceneProp.cpp`
- `T850/Framework/include/scene/Primitive*.h`
- `T850/Framework/src/scene/Primitive*.cpp`
- scene app entry points under `T850/DayScene/`

Output:

- [architecture/main-architecture.md](architecture/main-architecture.md)
- [architecture/platform-event-loop.md](architecture/platform-event-loop.md)

Status: draft complete.

Must cover:

- Engine layers.
- App lifecycle.
- Scene versus Framework responsibilities.
- Dev layer and debug UI.
- Event loop.
- Window ownership.
- API/platform switching.
- Windows, Android, Steam Deck differences.

## Stage 2 — Loading Geometry

Target files:

- `T850/Framework/src/utils/gltf/*`
- `T850/Framework/include/utils/gltf/*`
- `T850/Framework/src/utils/XDataBase.cpp`
- `T850/Framework/include/utils/XDataBase.h`
- `T850/Framework/src/scene/RenderMesh.cpp`
- `T850/Framework/include/scene/MeshAsset*.h`
- `T850/Framework/src/scene/MeshAssetCache.cpp`
- mesh pool and material cache files.

Output:

- [geometry/loading-geometry.md](geometry/loading-geometry.md)

Status: draft complete.

Must cover:

- glTF / GLB path.
- `.x` legacy path.
- `XDataBase` as intermediate.
- Vertex attributes.
- Index buffer selection.
- Material extraction.
- Texture loading.
- Static versus skinned detection.
- Mesh cache and GPU upload.

## Stage 3 — Shader management

Target files:

- `T850/Framework/Descriptors.h`
- shader key definitions.
- `T850/Framework/src/utils/ShaderDiskCache.cpp`
- `T850/Framework/src/utils/SPIRVReflection.cpp`
- API shader classes under `T850/Framework/src/video/*/*Shader.cpp`
- shader assets under `T850/Assets/Shaders/`

Output:

- [rendering/shader-management.md](rendering/shader-management.md)

Status: draft complete.

Must cover:

- `ShaderKey`.
- Defines/permutations.
- Pass keys.
- HLSL sharing between D3D/Vulkan.
- GLSL for GL.
- Offline compilation/cache.
- Reflection and resource binding.
- Vulkan/D3D12 PSO considerations.

## Stage 4 — Render graph

Target files:

- `T850/Framework/include/scene/RenderGraph*.h`
- `T850/Framework/src/scene/RenderGraph*.cpp`
- render graph JSON files in `T850/Assets/Scenes/`
- `RenderQuad` / fullscreen pass code.

Output:

- [rendering/render-graph.md](rendering/render-graph.md)

Status: draft complete.

Must cover:

- JSON schema.
- Render targets.
- Passes.
- Inputs/outputs.
- Push/pop RT.
- State overrides.
- Mesh versus quad draws.
- Post-processing.
- Final backbuffer pass.

## Stage 5 — Rendering geometry flow

Target files:

- `PrimitiveInstance.*`
- `RenderMesh.*`
- `RenderSkinnedMesh.*`
- `RenderQueue.*`
- `MeshPool.*`
- API buffer classes.

Output:

- [rendering/geometry-rendering-flow.md](rendering/geometry-rendering-flow.md)

Status: draft complete.

Must cover:

- `PrimitiveInst` to draw call.
- Transform composition.
- VB/IB binding.
- Material selection.
- Shader selection.
- State tracker.
- PSO resolution.
- DrawIndexed flow.
- Wireframe/debug geometry path.

## Stage 6 — Animation

Target files:

- `AnimationController.*`
- `RenderSkinnedMesh.*`
- glTF animation loading.
- shader skinning paths.

Output:

- [animation/animation-system.md](animation/animation-system.md)

Status: draft complete.

Must cover:

- Skeleton import.
- Animation clips.
- Interpolation.
- Pose update.
- Bone texture / constant data.
- GPU skinning.
- Snapshot/replay path.
- Limitations and debug workflows.

## Stage 7 — Physics

Target files:

- `T850/Framework/include/physics/*`
- `T850/Framework/src/physics/*`
- `RagdollEditorTool.*`
- editor ragdoll panels.
- SceneTemplate physics runtime sections.

Output:

- [physics/jolt-physics.md](physics/jolt-physics.md)

Status: draft complete.

Must cover:

- Jolt setup.
- Static triangle mesh cooking.
- Physics authoring metadata.
- Characters and CharacterVirtual.
- Ragdoll binding.
- Animation-to-ragdoll transition.
- Collision layers.
- Play Scene export.

## Stage 8 — NavMesh and Detour

Target files:

- `NavigationSystem.*`
- `NavigationDebugRenderer.*`
- editor NavMesh authoring code.
- SceneTemplate navigation runtime sections.

Output:

- [navigation/navmesh-detour.md](navigation/navmesh-detour.md)

Status: draft complete.

Must cover:

- Recast build.
- Detour query.
- Source geometry.
- Volumes/modifiers.
- Area costs.
- Link generation.
- `.t8nav` bake/load.
- Editor authoring workflow.

## Stage 9 — Editor

Target files:

- `T850/T8ditor/*`
- editor panel classes.
- editor gizmo/camera/grid/scene files.
- undo/redo.

Output:

- [editor/editor-overview.md](editor/editor-overview.md)

Status: draft complete.

Must cover:

- Editor app lifecycle.
- Panels.
- Hierarchy.
- Inspector.
- Rendering panel.
- Timeline.
- Play Scene.
- Mesh editor.
- Ragdoll editor.
- NavMesh authoring.
- Undo/redo expectations.

## Stage 10 — Scenes and formats

Target files:

- `EditorSceneFile.*`
- `SceneSetup.*`
- `SceneDescriptor.*`
- `SceneTemplate.cpp`
- `DayScene.cpp`
- `Quake3Mock.cpp`
- `.t8scene` examples.

Output:

- [scenes/scene-format-and-runtime.md](scenes/scene-format-and-runtime.md)

Status: draft complete.

Must cover:

- `.t8scene` schema.
- Editor save path.
- Runtime load path.
- SceneTemplate.
- DayScene differences.
- Quake3Mock/Jolt differences.
- Profiles and render graph references.
- Navigation/physics/animation/camera metadata.

## Stage 11 — Cross-link and dependency pass

Outputs:

- [dependency-map.md](dependency-map.md)
- Updated links in every document.
- Class dependency diagrams.
- End-to-end flows across subsystems.

Status: draft complete.

## Stage 12 — Review and gap pass

Outputs:

- [review-and-gaps.md](review-and-gaps.md)
- TODO/gap list.
- Troubleshooting index.
- Known limitations.
- Validation of class names and file paths.

Status: draft complete.

## Stage 13 — Debug and diagnostics

Target files:

- `T850/Framework/include/debug/LoadingProgress.h`
- `T850/Framework/include/debug/RuntimeTelemetry.h`
- `T850/Framework/include/debug/FrameDumper.h`
- `T850/Framework/include/debug/RenderTrace.h`
- `T850/Framework/include/debug/Profiler.h`
- `T850/Framework/src/debug/*`
- call sites in `DayScene`, `SceneTemplate`, `T8ditor`, render graph, shader, physics, and navigation code.

Output:

- [debug/diagnostics.md](debug/diagnostics.md)

Status: draft complete.

Must cover:

- Loading progress snapshots and loading-frame rendering.
- Runtime telemetry scopes, counters, output JSON, and benchmark usage.
- Frame dump/replay snapshot flow and render target dump entries.
- Render trace events for shaders, PSOs, buffers, render targets, textures, and draw calls.
- Profiler scopes, draw-call counting, and runtime diagnostics workflow.
- Debugging checklist for stalled loads, missing dumps, bad telemetry, or trace mismatch.

## Stage 14 — Resource lookup and cache paths

Target files:

- `T850/Framework/include/utils/ResourceLocator.h`
- `T850/Framework/src/utils/ResourceLocator.cpp`
- `T850/Framework/src/utils/ResourceManager.cpp`
- path users in shader cache, mesh preprocess cache, Jolt mesh cache, NavMesh cache, scene loading, texture loading, glTF buffers, and Android asset lookup.

Output:

- [architecture/resource-locator.md](architecture/resource-locator.md)

Status: draft complete.

Must cover:

- Resource path normalization and `Assets/` stripping.
- Base path, cache path, and resolved file path rules.
- Desktop filesystem lookup versus Android asset manager lookup.
- Case-insensitive Android packaged-asset fallback.
- `ReadText`, `ReadBinary`, `Exists`, `ResolveFilePath`, `ResolveCachePath`, and recursive fallback APIs.
- How cache-producing subsystems choose paths and invalidation keys.

## Stage 15 — Input, controllers, and camera profiles

Target files:

- `T850/Framework/include/utils/InputManager.h`
- platform input code under `T850/Framework/src/core/*`
- `T850/Framework/include/utils/CameraProfiles.h`
- `T850/Framework/src/utils/CameraProfiles.cpp`
- `T850/Framework/include/utils/HandheldControllerOverlay.h`
- scene/editor camera controller call sites in `SceneTemplate`, `DayScene`, `Quake3Mock`, and `T8ditor`.

Output:

- [input/camera-and-controls.md](input/camera-and-controls.md)

Status: draft complete.

Must cover:

- Keyboard, mouse, controller, touch, and gamepad state flow.
- Platform input translation into `InputManager`.
- Camera profile set: Orbit, Free Fly, Colliding Fly, Grounded FPS, COD FPS, Quake 3 FPS.
- Runtime profile selection and camera controller behavior.
- Handheld/controller overlay behavior.
- Editor versus runtime input routing, including hosted Play Scene/Mesh/Ragdoll windows.
- Android and handheld-specific differences.

## Stage 16 — FrameworkImGui runtime UI layer

Target files:

- `T850/FrameworkImGui/include/imgui/ImGuiSystem.h`
- `T850/FrameworkImGui/src/ImGuiSystem.cpp`
- `T850/FrameworkImGui/include/imgui/DevGuiContext.h`
- `T850/FrameworkImGui/src/DevGuiContext.cpp`
- T8ditor hosted viewport code and scene dev GUI call sites.

Output:

- [editor/imgui-system.md](editor/imgui-system.md)

Status: draft complete.

Must cover:

- ImGui platform/backend initialization per API and platform.
- Docking and viewport enablement.
- Android native window binding/rebinding.
- `DevGuiContext` and embedded panel helpers.
- Relationship between runtime debug UI and T8ditor-specific panels.
- Shutdown/reload hazards and hosted viewport integration.

## Stage 17 — Textures, samplers, and IBL resources

Target files:

- API texture implementations under `T850/Framework/src/video/*/*Texture.cpp`
- `T850/Framework/src/video/BaseDriver.cpp`
- `T850/Framework/include/scene/IBLResources.h`
- render graph environment slots in `T850/Framework/include/scene/RenderGraph.h`
- material texture slots in `MaterialAsset` / `RenderMesh`
- scene descriptor environment fields and editor cubemap/profile code.

Output:

- [rendering/textures-and-ibl.md](rendering/textures-and-ibl.md)

Status: draft complete.

Must cover:

- Texture creation/loading from files and memory.
- DDS/cubemap/float texture paths.
- API-specific texture resource and sampler handling.
- Mips, wrapping/filtering, SRV/sampler slots, and GL/D3D/Vulkan differences.
- IBL resource slots: diffuse/specular/BRDF/Charlie/sheen resources.
- Scene/profile/editor cubemap override workflow.
- Texture debugging and common failure modes.

## Stage 18 — SceneSetup and runtime control descriptors

Target files:

- `T850/Framework/include/scene/SceneDescriptor.h`
- `T850/Framework/src/scene/SceneDescriptor.cpp`
- `T850/Framework/include/scene/SceneSetup.h`
- `T850/Framework/src/scene/SceneSetup.cpp`
- JSON descriptor files under `T850/Assets/Scenes/*.json`
- call sites in `DayScene`, `Quake3Mock`, `SandboxScene`, `RagdollEditor`, `SceneTemplate`, and `T8ditor`.

Output:

- [scenes/scene-setup-descriptors.md](scenes/scene-setup-descriptors.md)

Status: draft complete.

Must cover:

- Exact mapping from `SceneDescriptor::quality` and `settings` to `SceneProps`.
- Runtime UI slider/checkbox/selector metadata and how panels consume it.
- Cameras, light cameras, lights, Gauss filters, splines, agents, environment maps, and IBL fields.
- How SceneTemplate combines `.t8scene` profiles with `SceneSetup`.
- How DayScene/Quake3Mock/Sandbox/RagdollEditor still depend on this older descriptor path.
- SaveState behavior and limitations.
