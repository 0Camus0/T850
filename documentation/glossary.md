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

## Rendering terms

| Term | Meaning |
|---|---|
| `RenderMesh` | Static mesh renderer for loaded geometry. Owns mesh draw setup and material/shader binding. |
| `RenderSkinnedMesh` | Skinned mesh renderer extending mesh rendering with animation/bone upload support. |
| `PrimitiveInst` | Per-instance transform/texture/key wrapper used to draw primitive render objects. |
| `MeshAsset` | Shared geometry/material asset data cached by path. |
| `MaterialAsset` | Deduplicated material parameters and texture references. |
| `ShaderKey` | Bitfield describing shader permutation features and pass type. |
| PSO | Pipeline State Object, especially relevant for D3D12/Vulkan where shader/state/topology combine into a pipeline. |
| VB | Vertex Buffer. |
| IB | Index Buffer. |
| RT | Render Target. |
| GBuffer | Deferred rendering target set containing surface properties, depth, etc. |
| IBL | Image-based lighting resources. |

## Geometry and asset terms

| Term | Meaning |
|---|---|
| glTF / GLB | Modern mesh/scene asset format loaded by the glTF loader. |
| `.x` | Legacy DirectX mesh format supported by the X loader. |
| `XDataBase` | Internal mesh database format used as an intermediate after loading `.x` or converted glTF data. |
| Subset | Material/draw subset inside a mesh geometry. |
| Mesh pool | Shared GPU buffer allocation path for mesh VB/IB data. |

## Animation terms

| Term | Meaning |
|---|---|
| Skeleton | Bone hierarchy used by skinned meshes. |
| Animation set / clip | Authored animation sequence. |
| Bone texture | GPU texture path used to pass bone transforms to shaders for skinning. |
| Snapshot pose | Captured pose data used for replay, ragdoll, or editor workflows. |

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

