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

- `T850/Framework/include/Descriptors.h`
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

- Updated links in every document.
- Class dependency diagrams.
- End-to-end flows across subsystems.

## Stage 12 — Review and gap pass

Outputs:

- TODO/gap list.
- Troubleshooting index.
- Known limitations.
- Validation of class names and file paths.
