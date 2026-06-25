# Glossary

This glossary captures engine terms used across the documentation.

## Core terms

| Term | Meaning |
|---|---|
| Framework | Core engine library containing rendering, resources, physics, navigation, scene setup, utilities, and API backends. |
| Scene | Runtime application/demo built on the Framework. Examples include DayScene, Quake3Mock, SandboxScene, RagdollEditor, and SceneTemplate. |
| T8ditor | The editor executable used to author `.t8scene` files. |
| `.t8scene` | JSON scene-authoring file saved by T8ditor and loaded by SceneTemplate. |
| SceneTemplate | Runtime scene intended to load and play editor-authored `.t8scene` content. |
| Dev layer | Developer/debug UI and runtime tooling layer exposed by scenes and editor code. |
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
| Static triangle mesh | Collision mesh generated from render geometry and used as static world collision. |
| Character | Jolt character controller path. |
| CharacterVirtual | Jolt virtual character controller path. |
| Ragdoll | Physics body/joint hierarchy bound to a skinned mesh skeleton. |
| Cook | Process of building optimized physics collision data. |

## Navigation terms

| Term | Meaning |
|---|---|
| Recast | Library used to build navigation meshes from source triangles. |
| Detour | Library used to query built navigation meshes and paths. |
| NavMesh | Recast/Detour navigation mesh used for pathfinding. |
| Off-mesh link | Authored or generated traversal connection such as jump/drop/jump pad. |
| Nav volume | Editor-authored helper volume used to include/exclude/area-mark/link-control NavMesh data. |
| `.t8nav` | Baked NavMesh asset format using the engine’s serialized Detour data. |

## Editor terms

| Term | Meaning |
|---|---|
| Hierarchy | Editor panel listing scene objects, cameras, lights, navigation, splines, and volumes. |
| Inspector / Properties | Editor panel for editing selected object data. |
| Gizmo | Viewport transform control using ImGuizmo/editor line rendering. |
| Play Scene | Editor workflow that exports a temporary `.t8scene` and runs it through SceneTemplate. |
| Mesh Editor | Editor-hosted mesh editing/inspection window. |
