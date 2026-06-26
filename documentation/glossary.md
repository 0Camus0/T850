# Glossary

This glossary captures engine terms used across the documentation.

## Core terms

| Term | Meaning |
|---|---|
| Framework | Core engine library containing rendering, resources, physics, navigation, scene setup, utilities, and API backends. |
| Dependency map | Cross-subsystem documentation map linking class dependencies and end-to-end runtime/editor flows. |
| Review and gap pass | Final documentation audit listing validation results, missing subsystem docs, troubleshooting links, and cross-system limitations. |
| Scene | Runtime application/demo built on the Framework. Examples include DayScene, Quake3Mock, SandboxScene, RagdollEditor, and SceneTemplate. |
| T8ditor | The editor executable used to author `.t8scene` files. |
| `.t8scene` | JSON scene-authoring file saved by T8ditor and loaded by SceneTemplate. |
| `EditorSceneFile` | Framework schema struct for `.t8scene`, including objects, physics, navigation, ragdolls, cameras, lights, profiles, and editor state. |
| `SceneDescriptor` | Runtime JSON descriptor for scene controls, cameras, lights, quality settings, UI metadata, and profiles. |
| `SceneSetup` | Loader/applier that converts `SceneDescriptor` JSON into cameras, lights, filters, splines, and `SceneProps`. |
| Runtime control descriptor | `SceneDescriptor` slider/checkbox/selector metadata consumed by runtime scenes and T8ditor rendering panels through name-based mapping tables. |
| `QualityDesc` | `SceneDescriptor` block mapped by `SceneSetup::ApplyQualityAndSettings` into render-quality fields on `SceneProps`. |
| `SceneSettingsDesc` | `SceneDescriptor` block mapped by `SceneSetup::ApplyQualityAndSettings` into exposure, lighting, material, and feature-toggle fields on `SceneProps`. |
| SceneTemplate | Runtime scene intended to load and play editor-authored `.t8scene` content. |
| Scene profile | `SandboxProfileDesc` override set selected by model/platform/GPU targets for render, camera, light, animation, and UI state. |
| Dev layer | Developer/debug UI and runtime tooling layer exposed by scenes and editor code. |
| LoadingProgress | Global loading progress state with snapshots, scoped weighted steps, and an optional frame callback for loading UI. |
| RuntimeTelemetry | Runtime frame sampler for named scopes and counters, written to JSON on shutdown when enabled. |
| FrameDumper | Diagnostic system that dumps backbuffer/render targets and writes/replays `snapshot.json` frame state. |
| RenderTrace | Optional compile-time render instrumentation system that writes API resource/state/bind/draw events for cross-backend diffing. |
| Profiler | CPU/GPU scope profiler with draw-call counting and API-specific timestamp backends. |
| `ResourceLocator` | Process-wide path abstraction for normalizing resource paths, reading desktop or Android packaged assets, listing assets, resolving filesystem files, and choosing cache paths. |
| Resource path | Portable engine-relative asset path, usually stored without a leading slash or top-level `Assets/` prefix, such as `Models/Robot.glb`. |
| Base path | `ResourceLocator` readable root used as one candidate for resolving relative desktop assets. |
| Cache path | `ResourceLocator` writable root used by generated caches and relative writes when the requested path is not an existing file. |
| Android asset manager | Native `AAssetManager` pointer used by `ResourceLocator` to read packaged APK assets that are not regular filesystem files. |
| `.t8cache` | Source-local generated cache directory used by mesh preprocess and Jolt cooked triangle mesh caches. |
| `InputManager` | App-owned shared keyboard, mouse, text, touch-cursor, and gamepad state consumed by scenes, editor tools, and runtime UI. |
| `GamepadInputState` | Normalized SDL gamepad state with connected/enabled flags, handheld metadata, axes, triggers, held buttons, and pressed-edge flags. |
| `CameraController` | Runtime camera profile owner that attaches to a `Camera`, forwards `CameraInputState`, and updates the active profile. |
| Camera profile | Runtime camera behavior mode such as Orbit, Free Fly, Colliding Fly, Grounded FPS, COD FPS, or Quake 3 FPS. |
| `CameraInputState` | Scene/editor camera intent structure containing movement, jump/crouch/sprint, mouse look, orbit flags, deltas, and scroll. |
| `EditorCamera` | T8ditor-specific orbit/pan/zoom camera used by the main editor viewport. |
| Handheld overlay | ImGui gamepad navigation and controller help/footer UI drawn for handheld/controller workflows. |
| `ImGuiSystem` | FrameworkImGui wrapper that owns Dear ImGui context setup, platform/renderer backend init, frame lifecycle, draw-data rendering, loading frames, and Android native-window rebinding. |
| `DevGuiContext` | Shared scene/runtime debug UI facade used for panels, descriptor controls, embedded panels, hosted viewport docking, and gamepad navigation focus. |
| Platform windows | Dear ImGui multi-viewport/native-window feature used by T8ditor hosted windows on desktop. |
| Hosted scene panel | T8ditor Play Scene or Mesh Edit panel that pins a runtime scene's `DrawDevGui` output into a hosted ImGui viewport/dockspace. |
| Primitive | Engine render object abstraction. Common path: `PrimitiveInst` references a `PrimitiveBase` implementation such as `RenderMesh`. |
| RenderGraph | Data-driven render pipeline loaded from JSON, with passes, render targets, inputs, draw commands, and state changes. |
| Render graph pass | One JSON-declared render step that can bind a render target, set state, bind inputs, and issue mesh or quad draws. |
| Render graph edge | Runtime dependency record from a prior RT writer pass to a later pass that samples that RT attachment. |

## Rendering terms

| Term | Meaning |
|---|---|
| `RenderMesh` | Static mesh renderer for loaded geometry. Owns mesh draw setup and material/shader binding. |
| `RenderSkinnedMesh` | Skinned mesh renderer extending mesh rendering with animation/bone upload support. |
| `PrimitiveInst` | Per-instance transform/texture/key wrapper used to draw primitive render objects. |
| `PrimitiveBase` | Abstract render primitive interface implemented by mesh, skinned mesh, quad, and debug primitives. |
| `MeshAsset` | Shared geometry/material asset data cached by path. |
| `MaterialAsset` | Deduplicated material parameters and texture references. |
| `MeshDrawStateTracker` | Pass-scoped state tracker that skips redundant shader-dependent texture, constant-buffer, topology, VB, and IB binds. |
| `RenderQueue` | Future flat draw-list abstraction with sortable `DrawItem` entries; current mesh rendering still walks `RenderMesh::Draw`. |
| `DrawIndexed` | Backend draw call using index count, start index, and base vertex to draw a submesh or cluster. |
| `ShaderKey` | Bitfield describing shader permutation features and pass type. |
| `PassType` | Six-bit render pass selector stored inside `ShaderKey`, used to compile pass-specific shader variants. |
| Shader disk cache | API-specific compiled shader artifact cache stored under `Shaders/.t8shadercache`. |
| SPIR-V reflection | Vulkan helper that parses SPIR-V modules for descriptor bindings and vertex input locations. |
| PSO | Pipeline State Object, especially relevant for D3D12/Vulkan where shader/state/topology combine into a pipeline. |
| PSO cache | D3D12/Vulkan runtime cache keyed by shader pointer plus render state, topology, and render target formats/render pass. |
| VB | Vertex Buffer. |
| IB | Index Buffer. |
| RT | Render Target. |
| Fullscreen quad | `RenderQuad` primitive used by render graph post-process, deferred, copy, and final-output passes. |
| GBuffer | Deferred rendering target set containing surface properties, depth, etc. |
| IBL | Image-based lighting resources. |
| `Texture` | API-neutral base texture object containing source path, CIL flags, sampler params, dimensions, mip count, and backend bind/update functions. |
| CIL | Common image loader used by `Texture::LoadTexture` for DDS/cubemap/compressed/half-float texture data. |
| `EnvironmentMapSet` | Texture-index bundle for sky, diffuse/specular IBL, BRDF LUT, Charlie IBL/LUT, and sheen E LUT. |
| `IBLResources` | Framework helpers that load explicit IBL assets, generate/cache filtered IBL cubemaps/LUTs, and update `SceneProps` IBL settings. |
| `EnvironmentTextureSlot` | Reserved render texture slots 10-15 for diffuse/specular/BRDF/Charlie/sheen IBL resources. |
| `MaterialTextureSlot` | Reserved render texture slots 16-25 for extended material maps such as sheen, clearcoat, occlusion, specular, transmission, and lightmap. |
| Generated IBL cache | Derived `.t8ibl` float cache files stored under `Textures/GeneratedIBLCache`. |

## Geometry and asset terms

| Term | Meaning |
|---|---|
| glTF / GLB | Modern mesh/scene asset format loaded by the glTF loader. |
| `.x` | Legacy DirectX mesh format supported by the X loader. |
| `XDataBase` | Internal mesh database format used as an intermediate after loading `.x` or converted glTF data. |
| `xMeshGeometry` | Source-style geometry arrays inside `XDataBase`: attributes, triangle indices, material list, and optional skin metadata. |
| `xFinalGeometry` | Interleaved render-ready vertex data and subset metadata generated from `xMeshGeometry`. |
| Subset | Material/draw subset inside a mesh geometry. |
| `Submesh` | Shared mesh-cache metadata for one drawable subset, including index range, material slot, bounds, and mesh-pool allocations. |
| Mesh pool | Shared GPU buffer allocation path for mesh VB/IB data. |
| Mesh preprocess cache | Disk cache for mesh bounds/culling cluster metadata keyed by source signature and clustering settings. |

## Animation terms

| Term | Meaning |
|---|---|
| Skeleton | Bone hierarchy used by skinned meshes. |
| `AnimationController` | Per-renderer runtime controller that owns mutable clip playback state, animated skeleton state, and final shader bone matrices. |
| Skinning | Deforming mesh vertices by blending bone transforms using imported joint indices and weights. |
| Bind pose | Rest pose used to compute inverse bind matrices and baseline skeleton transforms. |
| Inverse bind matrix | Matrix that transforms from mesh/bind space into a bone's local bind-space reference; used before animated combined bone transform. |
| Animation set / clip | Authored animation sequence. |
| Bone texture | GPU texture path used to pass bone transforms to shaders for skinning. |
| Snapshot pose | Captured pose data used for replay, ragdoll, or editor workflows. |
| Keyframe mode | Animation inspection mode that snaps to discrete keyframes instead of interpolating continuously. |

## Physics terms

| Term | Meaning |
|---|---|
| Jolt | Physics library integrated into the Framework. |
| `JoltPhysicsSystem` | Framework wrapper around Jolt for lifecycle, body creation, casts, ragdolls, simulation update, and debug body queries. |
| `PhysicsBodyHandle` | Runtime handle to a Jolt body slot stored on `PrimitiveInst` for authored physics bodies. |
| `PhysicsRagdollHandle` | Runtime handle to a Jolt ragdoll slot containing multiple body handles and constraints. |
| Static triangle mesh | Collision mesh generated from render geometry and used as static world collision. |
| `.t8jolt` | Cached cooked Jolt triangle mesh shape stored under a source asset `.t8cache` directory. |
| Character | Jolt character controller path. |
| CharacterVirtual | Jolt virtual character controller path. |
| Kinematic character controller | T850 movement controller that uses capsule/box sweeps against the physics world instead of directly wrapping Jolt CharacterVirtual. |
| Ragdoll | Physics body/joint hierarchy bound to a skinned mesh skeleton. |
| Ragdoll authoring asset | JSON file under `Models/RagdollEdits` storing edited ragdoll body, joint, frozen, and controlled-bone metadata. |
| Cook | Process of building optimized physics collision data. |

## Navigation terms

| Term | Meaning |
|---|---|
| Recast | Library used to build navigation meshes from source triangles. |
| Detour | Library used to query built navigation meshes and paths. |
| NavMesh | Recast/Detour navigation mesh used for pathfinding. |
| `NavMeshGeometry` | Runtime build input containing source vertices/indices, off-mesh links, volume modifiers, area costs, and optional validators. |
| `NavMeshBuildSettings` | Recast build and T850 traversal-link settings, including cell size, agent dimensions, query extents, and auto link options. |
| Off-mesh link | Authored or generated traversal connection such as jump/drop/jump pad. |
| Nav volume | Editor-authored helper volume used to include/exclude/area-mark/link-control NavMesh data. |
| `.t8nav` | Baked NavMesh asset format using the engine’s serialized Detour data. |
| `NavMeshDebugRenderer` | Line-renderer based debug view for navmesh geometry, nodes, graph edges, and off-mesh links. |

## Editor terms

| Term | Meaning |
|---|---|
| Hierarchy | Editor panel listing scene objects, cameras, lights, navigation, splines, and volumes. |
| Inspector / Properties | Editor panel for editing selected object data. |
| Gizmo | Viewport transform control using ImGuizmo/editor line rendering. |
| Play Scene | Editor workflow that exports a temporary `.t8scene` and runs it through SceneTemplate. |
| Mesh Editor | Editor-hosted mesh editing/inspection window. |
| Ragdoll Editor | Editor-hosted window for authoring and simulating ragdoll bodies, joints, controlled bones, and debug overlays. |
| `EditorWorld` | Shared T8ditor authoring data model containing objects, cameras, lights, physics entities, selection, groups, undo, and loaded scene state. |
| Hosted viewport | Editor-hosted render target and ImGui image/input rectangle used by Mesh Edit, Ragdoll Edit, and Play Scene windows. |
| `UndoStack` | T8ditor command stack supporting undo/redo through transform commands and scene-state snapshots. |
