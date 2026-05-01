# T850 Render Architecture

This document describes the layered render architecture of T850: how
geometry, materials, entities, and per-frame draw work fit together,
and how subsystems like culling, picking, animation, physics, and ray
tracing plug in.

It mixes **current state** (what the code does today, late Phase A)
and **target state** (what the multi-phase refactor in
`session-state/.../plan.md` is moving toward). Both are labelled.

---

## 1. Layer overview

```
                                                                ┌───────────────────────────────────────────┐
                                                                │ Per-frame                                 │
                                                                │   ┌─────────────────────────────────────┐ │
                                                                │   │ RenderQueue<Pass>   (target)        │ │
                                                                │   │ ─ DrawItem[] sorted by 64-bit key   │ │
                                                                │   │ ─ Execute() tracks lastPSO/VB/IB    │ │
                                                                │   └─────────────────────────────────────┘ │
                                                                │                ▲                          │
                                                                │                │ extract                  │
                                                                └────────────────┼──────────────────────────┘
                                                                                 │
   ┌────────────────────────────────────┐    visit + frustum cull                │
   │  Scene                             │────────────────────────────────────────┘
   │   ─ list of RenderEntity           │
   └────────────────┬───────────────────┘
                    │ owns
                    ▼
       ┌───────────────────────────────────────┐
       │  RenderEntity   (target)              │
       │   ─ uint32_t  id                      │
       │   ─ MeshAsset*                        │ ─────────────┐
       │   ─ MaterialAsset* materials[N]       │ ──────┐      │
       │   ─ XMATRIX44    worldFromLocal       │       │      │
       │   ─ AABB         worldAABB (cached)   │       │      │
       │   ─ uint8_t      layerMask            │       │      │
       │   ─ bool         visible              │       │      │
       │   ─ SkeletonInstance? skin            │ ─┐    │      │
       │   ─ PhysicsBody?     body             │ ─┼───┐│      │
       │   ─ user data slots                   │  │   ││      │
       └───────────────────────────────────────┘  │   ││      │
                                                  │   ││      │
                                                  │   ││      ▼
                                                  │   ││  ┌──────────────────────────────────┐
                                                  │   ││  │ MaterialAsset  (Phase B — done)  │
                                                  │   ││  │   ─ contentHash (dedup key)      │
                                                  │   ││  │   ─ ShaderKey   featureKey       │
                                                  │   ││  │   ─ Texture*    textures[16]     │
                                                  │   ││  │   ─ int         textureIds[16]   │
                                                  │   ││  │   ─ MaterialParams params        │
                                                  │   ││  │       (alphaMode, doubleSided,   │
                                                  │   ││  │        unlit, bUseFresnel,       │
                                                  │   ││  │        IOR, normalScale, etc.,   │
                                                  │   ││  │        + 12 UV transforms)       │
                                                  │   ││  └──────────────────────────────────┘
                                                  │   ││
                                                  │   ││           shared by every entity that
                                                  │   ││           uses the same source path
                                                  │   ││                  ▼
                                                  │   │└─►┌──────────────────────────────────┐
                                                  │   │   │ MeshAsset       (Phase A — DONE) │
                                                  │   │   │   ─ sourcePath   (dedup key)     │
                                                  │   │   │   ─ vertexAttribMask             │
                                                  │   │   │   ─ vertexStride/Count/idxCount  │
                                                  │   │   │   ─ AABB        rootAABB         │
                                                  │   │   │   ─ Submesh[]   submeshes        │
                                                  │   │   │   (GPU memory in shared        │
                                                  │   │   │    VertexPool/IndexPool        │
                                                  │   │   │    on MeshAssetCache)          │
                                                  │   │   └──────────────────────────────────┘
                                                  │   │
                                                  │   │           ┌──────────────────────────┐
                                                  │   │           │ Submesh                  │
                                                  │   │           │   ─ vertexStart/Count    │
                                                  │   │           │   ─ indexStart           │
                                                  │   │           │   ─ triangleCount        │
                                                  │   │           │   ─ materialSlot         │
                                                  │   │           │   ─ AABB localAABB       │
                                                  │   │           │   ─ ShaderKey            │
                                                  │   │           │       vertexAttribKey    │
                                                  │   │           └──────────────────────────┘
                                                  │   │
                                                  │   ▼
                                                  │  ┌──────────────────────────────────┐
                                                  │  │ SkeletonAsset   (target)         │
                                                  │  │   ─ Bone[] hierarchy             │
                                                  │  │   ─ inverse-bind matrices        │
                                                  │  │   ─ optional Capsule[]           │
                                                  │  │       per bone (physics map)     │
                                                  │  └──────────────────────────────────┘
                                                  │
                                                  ▼
                                          ┌──────────────────────────────────┐
                                          │ SkeletonInstance  (per entity)   │
                                          │   ─ world bone matrices          │
                                          │   ─ bone texture (GPU)           │
                                          │   ─ AnimationController          │
                                          └──────────────────────────────────┘
```

Three caches hand out shared assets:

| Cache              | Key            | Phase                       |
|--------------------|----------------|-----------------------------|
| `MeshAssetCache`   | source path    | **A** — implemented         |
| `MaterialAssetCache` | content hash | B — done                    |
| `TextureCache`     | path + flags   | exists today (`g_pBaseDriver`) |

---

## 2. Today vs Target

| Concern                  | Today                                                   | Target (after full refactor)                          |
|--------------------------|---------------------------------------------------------|-------------------------------------------------------|
| One Porsche × N entities | N × VRAM, N × VB upload                                 | 1 × VRAM, 1 × upload (Phase A: ✅ for VB+IBs)          |
| Cross-entity sort        | Not possible (per-entity `Draw()`)                      | Single 64-bit `std::sort` per pass per camera (Phase C)|
| Material dedup           | None                                                    | Hash-based, automatic (Phase B)                        |
| CB upload                | Per-mesh CB stomped per-subset                          | Per-frame / per-material / per-instance ring buffer (Phase D) |
| Frustum cull             | Per-mesh AABB then per-subset AABB, per-entity loop     | Per-(entity, submesh) at extract, optionally GPU       |
| Picking                  | `RenderMesh::Info[].bounds` + ray-AABB                  | `RenderEntity` + `Submesh` localAABB transformed       |
| Skinning                 | `RenderSkinnedMesh` owns bones + textures               | `SkeletonInstance` on `RenderEntity`, `MeshAsset` shared |
| Physics                  | Not integrated                                          | `PhysicsBody` slot on `RenderEntity` ↔ pose source     |
| Ray tracing              | BLAS/TLAS via `BaseDriver::SupportsRayTracing` etc.      | BLAS lives in `MeshAsset`, TLAS rebuilt from entities  |

Cells that read **Phase A: ✅** are already in the codebase as of this
document. The rest are described below as if implemented, so subsystem
authors can design against the target shape.

---

## 3. Where current types live (Phase A code in detail)

### 3.1 `MeshAsset` (Phase A — done)

`Framework/include/scene/MeshAsset.h`. Owned by `MeshAssetCache`,
borrowed by everyone else.

```cpp
struct PoolAlloc {
  uint32_t poolId      = UINT32_MAX;
  uint32_t offsetElems;            // start vertex / start index (elements, not bytes)
  uint32_t count;                  // verts or indices
};

struct Submesh {
  uint32_t vertexStart, vertexCount;
  uint32_t indexStart, triangleCount;
  uint32_t materialSlot;
  bool     ib32Bit;
  AABB     localAABB;              // canonical t850::AABB (Picking.h)
  ShaderKey vertexAttribKey;       // bits 0..4 + 39..40 only

  PoolAlloc vbAlloc;               // → MeshAssetCache::m_vertexPools[poolId]
  PoolAlloc ibAlloc;               // → MeshAssetCache::m_indexPools[poolId]
  uint16_t  vbPoolId, ibPoolId;    // dense ids for sort-key packing (see §5)
};

struct MeshAsset {
  std::string             sourcePath;          // dedup key
  uint64_t                vertexAttribMask;
  uint32_t                vertexStride, vertexCount, indexCount;
  AABB                    rootAABB;
  std::vector<Submesh>    submeshes;           // flattened across all geometries
  uint32_t                refCount;
  // GPU memory lives in MeshAssetCache::m_vertexPools / m_indexPools,
  // not on this struct. Phase A.5.
};
```

### 3.2 `MeshAssetCache` (Phase A + A.5 — done)

`Framework/include/scene/MeshAssetCache.h`. Process-wide singleton,
mutex-protected, dedupes by lower-cased path. Refcount management:

```
   Acquire(path)            ──► first call: insert + populate, refs=1
                             ──► later call: return existing,    refs++
   Release(asset)           ──► refs--; if refs==0 destroy + remove

   GetOrCreateVertexPool(formatHash, stride)  ──► VertexPool*  (Tier 1)
   GetOrCreateIndexPool(ib32Bit)              ──► IndexPool*   (≤ 2)
   GetVertexPool(id) / GetIndexPool(id)       ──► O(1) lookup
```

GPU memory lifecycle:

- **Per-format `VertexPool`** holds one big GPU `VertexBuffer` + a CPU
  staging vector. Suballocate appends staging bytes; `EnsureUploaded`
  rebuilds the GPU buffer when staging changed. Created on demand by
  the first asset whose `(vertexAttribMask, stride)` produces a new
  key.
- **`IndexPool`** identical but at most two instances (16-bit and
  32-bit), keyed by IB width.
- Pool buffers outlive any individual asset and are torn down only at
  cache `Clear()`.

Lifecycle today:

- `RenderMesh::Load(filename)` calls `Acquire`, logs HIT/MISS.
- `RenderMesh::Create()` populates the asset on first acquisition
  (stride, submeshes) AND suballocates each geometry's VB + each
  subset's IB into the appropriate pool. Subsequent acquisitions skip
  the suballocate (asset already populated) and just copy the pool
  offsets from `m_asset->submeshes` into per-instance `MeshInfo` /
  `SubSetInfo`.
- `RenderMesh::Draw()` and `RenderSkinnedMesh::Draw()` bind the pool's
  `VertexBuffer`/`IndexBuffer` and pass per-submesh
  `vbAlloc.offsetElems` / `ibAlloc.offsetElems` /
  `ibAlloc.count` to `DrawIndexed`. The legacy per-asset `MeshInfo::VB`
  and `SubSetInfo::IB` pointers are nullptr after Phase A.5.3 and
  serve only as defensive fallbacks (currently unreached).
- `RenderMesh::Destroy()` releases the per-instance CB and calls
  `Release(asset)`; the cache decrements refs and (at 0) destroys the
  asset's submeshes vector — pools persist across asset lifetimes.

### 3.3 What `RenderMesh` is now

`RenderMesh` is **transitional**. It still owns:

- `transform` (per-instance world matrix) — moves to `RenderEntity` in
  Phase C.
- `Info[]` with `MeshInfo` and `SubSetInfo` — material data here moves
  to `MaterialAsset` in Phase B; the geometry pointers (`VB`, `IB`,
  `SubSetInfo::IB`) are already non-owning aliases into `m_asset`.
- The big per-mesh `CnstBuffer` — splits in Phase D.
- `Draw(t, vp)` — the legacy entry point. Phase C wraps it in a
  per-pass `RenderQueue` so multiple `RenderMesh`es batch together.

So today, "the entity" is `PrimitiveInst` + `RenderMesh` fused.
Tomorrow it is `RenderEntity` + shared `MeshAsset` + shared
`MaterialAsset`s.

---

## 4. How subsystems plug in

### 4.1 Frustum culling

**Today** (`RenderMesh::Draw`):
1. Extract 6 frustum planes once per draw.
2. For each `MeshInfo`: `AABBInsideFrustum(Info[i].bounds, transform, planes)`.
3. For each `SubSetInfo` of the surviving mesh: same check with
   `sub_info->bounds`. (Two-level cull.)

**Target** (Phase C):
```
Scene::Extract(pass, camera, queue):
  ExtractFrustumPlanes(camera.VP, planes)
  for each entity in scene.entities[camera.layerMask]:
    if !entity.visible: continue
    if !AABBInsideFrustum(entity.worldAABB, identity, planes): continue   // entity-level
    for submesh in entity.mesh->submeshes:
      AABB submeshWorld = entity.worldFromLocal * submesh.localAABB
      if !AABBInsideFrustum(submeshWorld, identity, planes): continue     // submesh-level
      queue.Push(BuildDrawItem(entity, submesh, ...))
```

The win:
- `entity.worldAABB` is computed only when `worldFromLocal` changes,
  not every frame.
- Visibility decision is per `(entity, submesh)`, not per asset.
- Result list is a flat array → easy to sort and execute.
- GPU-driven future (Phase F): the same per-(entity, submesh) AABB
  array goes to a compute shader that does the culling + writes
  `D3D12_DRAW_INDEXED_ARGUMENTS` for `ExecuteIndirect`.

### 4.2 Picking / selection

Already implemented at the asset-level using `RenderMesh::Info[].bounds`
in `EditorScene` etc. To migrate:

```
Pick(ray, scene) → (entity, submesh):
  candidates = []
  for each entity in scene.entities:
    AABB world = entity.worldAABB                              // entity test
    if RayIntersectsAABB(ray, world, t):
      for submesh in entity.mesh->submeshes:
        AABB sw = entity.worldFromLocal * submesh.localAABB    // submesh test
        if RayIntersectsAABB(ray, sw, t):
          candidates.push((entity.id, submesh.materialSlot, t))
  return min_by_t(candidates)
```

The existing `t850::AABB` and `RayIntersectsAABB` (in `utils/Picking.h`)
are reused; only the iteration shape changes from "walk one
RenderMesh" to "walk RenderEntity list".

### 4.3 Skeletal animation + physics (capsule-per-bone mapping)

The Porsche is rigid, but the engine has skeletal support
(`RenderSkinnedMesh`, `AnimationController`, `SkeletonAsset` —
inverse-bind matrices, max 128 bones). Adding physics that interacts
with bones needs three pieces. The architecture supports it cleanly:

**1. SkeletonAsset (shared, on the asset side)**
```cpp
struct Bone {
  std::string name;
  int          parent;          // -1 for root
  XMATRIX44    inverseBindLocal;
  XMATRIX44    bindPoseLocal;
  // Physics shape attached to this bone (target):
  PhysicsShape collider;        // enum { CAPSULE, BOX, SPHERE, NONE }
  float        capsuleRadius;
  float        capsuleHalfHeight;
  XMATRIX44    colliderLocal;   // shape transform relative to bone
  float        mass;
  uint16_t     bodyFlags;       // KINEMATIC / DYNAMIC / STATIC
};
struct SkeletonAsset {
  std::vector<Bone> bones;
  // … animation channels live with AnimationAsset, not here
};
```
The capsule data is **immutable, asset-side** — defined per character
type once, shared across instances. (Source: artist authoring tool, or
auto-generated from bone bounds with a default ratio.)

**2. SkeletonInstance (per entity)**
```cpp
struct SkeletonInstance {
  std::vector<XMATRIX44> worldBoneMatrices;   // updated by AnimationController
  Texture*               boneTexture;          // GPU upload (today's HAS_SKINNING_TEX path)
  AnimationState         anim;
  // Physics integration:
  std::vector<PhysicsBodyId> bodies;           // 1:1 with SkeletonAsset::bones
  RagdollMode            mode;                 // KINEMATIC | RAGDOLL | BLENDED
};
```
- In **KINEMATIC** mode, `AnimationController` writes
  `worldBoneMatrices`, then push to physics: `physics.SetBodyTransform(bodies[i], worldBoneMatrices[i] * collider.colliderLocal)`.
  The capsules follow the animation; physics bodies still report
  collisions to the gameplay layer but don't drive bone pose.
- In **RAGDOLL** mode, physics owns the simulation. Each frame:
  `worldBoneMatrices[i] = physics.GetBodyTransform(bodies[i]) * inverse(collider.colliderLocal)`.
  Animation is suspended; pose comes from the simulation.
- In **BLENDED** mode (hit reactions), per-bone weights interpolate
  between the kinematic and ragdoll matrices.

**3. Where it lives in the entity/asset graph**
```
RenderEntity (target)
   ├─ MeshAsset*         ───►  rigid VB/IB shared
   ├─ MaterialAsset*[]   ───►  shared
   └─ SkeletonInstance              (per entity, mutable)
        ├─ skeleton: SkeletonAsset*  ───►  bone hierarchy + capsule defs (shared)
        ├─ worldBoneMatrices[]                (mutable, fed to skinning shader)
        ├─ bodies: PhysicsBodyId[]            (one per bone with a collider)
        └─ AnimationController                (drives matrices in KINEMATIC mode)
```

**Frame loop (target):**
```
PhysicsWorld.Step(dt)
for each entity with skeleton:
  if entity.skin->mode == KINEMATIC:
    entity.skin->anim.Update(dt) → writes worldBoneMatrices
    for each bone i with collider:
      physics.SetKinematicTarget(skin.bodies[i],
                                 worldBoneMatrices[i] * skeleton.bones[i].colliderLocal)
  else if entity.skin->mode == RAGDOLL:
    for each bone i with collider:
      worldBoneMatrices[i] = physics.GetBodyTransform(skin.bodies[i])
                           * inverse(skeleton.bones[i].colliderLocal)

Scene::Extract(...) → RenderQueue
  // skinning still happens in shader; bone matrices uploaded into bone texture
```

The renderer is decoupled from the source of bone matrices — animation
or physics, the shader sees the same uniform / texture.

### 4.4 Physics on rigid bodies (no skeleton)

For a rigid mesh (e.g. a falling crate, a Porsche door panel as
debris):

```
RenderEntity
   ├─ MeshAsset*
   ├─ MaterialAsset*[]
   ├─ worldFromLocal
   └─ PhysicsBody body         (single rigid body)
        ├─ collisionShape: ConvexHull | Box | Sphere
        ├─ mass, friction, restitution
        └─ id in PhysicsWorld
```

The collision shape can be:
- **Authored**: stored on the `MeshAsset` as
  `std::optional<PhysicsShape> collisionShape`. Shared across instances.
- **Generated** at load time from `MeshAsset::rootAABB` (cheap fallback)
  or from a convex hull computed from vertex positions (more accurate).

Per-frame update is one-line: `entity.worldFromLocal = physics.GetBodyTransform(entity.body)`,
then the world AABB is invalidated and lazily recomputed before the next
extract.

### 4.5 Ray tracing (DXR / VK_KHR_ray_tracing)

T850 already has BLAS/TLAS infrastructure (see memory:
`BaseDriver::SupportsRayTracing/DispatchRays/...`). The asset/instance
split makes RT integration natural:

- **BLAS** is built once per `MeshAsset` (or per `Submesh` for finer
  granularity) and stored on the asset:
  ```cpp
  struct MeshAsset {
    ...
    BLASHandle blas;                     // built lazily, cached
    std::vector<BLASHandle> submeshBlas; // optional, finer
  };
  ```
- **TLAS** is rebuilt or refit per frame from the `RenderEntity` list:
  ```
  for each entity in scene.entities:
    tlas.AddInstance(entity.mesh->blas, entity.worldFromLocal,
                     entity.id /* hit record id */)
  tlas.Build(REFIT if only transforms changed, FAST_BUILD on entity add/remove)
  ```
- The shader binding table indexes into per-entity material data
  (already living in `MaterialAsset`).

### 4.6 Shadow casters / per-pass visibility

A `RenderEntity` carries `layerMask` (8 bits today). Each pass declares
which layers it sees:

```
RenderQueue<SHADOW_MAP>::Extract(scene, lightCamera):
  filter entities by `layerMask & SHADOW_CASTER`, then frustum cull
  against the light's frustum, then push.
```

This replaces the need for separate "shadow geometry" lists. The same
`MeshAsset` is shared between the gbuffer pass and shadow pass; only
the pass-specific `ShaderKey` differs (and pre-compiled PSOs already
exist per pass — see `RenderMesh.cpp` shader pre-compile loop).

### 4.7 Streaming / level swap

```
LevelLoad("scene.json"):
  for each entity_def:
    MeshAsset*      m = MeshAssetCache::Acquire(entity_def.meshPath)
    MaterialAsset*  mat = MaterialAssetCache::Acquire(entity_def.matHash)
    scene.entities.emplace(m, mat, entity_def.transform)

LevelUnload():
  for each entity in scene:
    MeshAssetCache::Release(entity.mesh)
    MaterialAssetCache::Release(entity.materials[*])
  scene.entities.clear()
```

Refcounts handle the case where two levels share a common asset
(`Common.glb` referenced by `LevelA` and `LevelB`): `Acquire` from
`LevelB` while `LevelA` is still loaded just bumps the refcount; nothing
re-uploads. `LevelA::Unload` decrements; the asset survives.

### 4.8 GPU-driven culling (Phase F)

Mostly transparent to the architecture, but worth showing how the
existing types feed into it:

```
Per frame:
  1. Upload entity AABB array (worldAABB[]) and submesh count[] to GPU.
  2. Compute shader walks them:
       - frustum cull with the camera's planes
       - depth-test against last frame's Hi-Z
       - write D3D12_DRAW_INDEXED_ARGUMENTS for survivors
  3. ExecuteIndirect draws using the pre-built MeshAsset VBs/IBs.
  4. Per-instance world matrix is read by shader from the same
     entity-indexed buffer (SV_InstanceID).
```

No CPU-side `RenderQueue` needed for this pass; the structure of
`MeshAsset` (shared VB/IB, identifiable submeshes) is the prerequisite.

---

## 5. Identity vs Sort key

T850 uses **two distinct keys** for distinct purposes. They are
related (one derives the other) but never conflated.

| Key | Purpose | Storage | Width | Layout |
|---|---|---|---|---|
| **`ShaderKey`** (identity) | Hashable fingerprint of "which shader/PSO am I". Drives shader compile + cache lookup. Already exists. | Per-Submesh (`vertexAttribKey` subset) and per-pass full key built at draw time | 64-bit, sparse | Bits scattered by feature category — see `Descriptors.h` |
| **Sort key** (per-DrawItem, target Phase C) | Drives `std::sort` per pass. Bit order mirrors driver state-change cost. | Computed per frame at extract; never stored | 64-bit, dense, packed | See below |

**Why two keys.** Sorting by the raw `ShaderKey` does not minimize
state changes — its bits are categorized for human readability
(vertex attribs at 0..4 + 39..40, pass at 20..25, material features
at 5..14 + 26..38), not by API cost. The sort key inverts that:
PSO-influencing bits go to the high end, depth-bucket goes to the low
end, so a single `std::sort` traverses items in the cheapest order.

**Why not split vertex attributes out of `ShaderKey`.** Tempting, but
they belong in identity because they determine (1) which VS variant to
compile, (2) which IA layout the PSO is built around, (3) which pool
the geometry can live in. All three derive from the same bits. The
only architecture where splitting would help is Tier 2 bindless vertex
pulling, which T850 is not pursuing.

### Cached derived indices

To pack the sort key cheaply at extract, `Submesh` carries a few
**dense `uint16_t` ids derived from `ShaderKey`** at load time:

```cpp
struct Submesh {
  // canonical (identity)
  ShaderKey vertexAttribKey;     // bits 0..4 + 39..40 only

  // derived dense indices for sort-key packing
  uint16_t  vbPoolId;            // → MeshAssetCache::m_vertexPools[id]
  uint16_t  ibPoolId;            // → MeshAssetCache::m_indexPools[id]   (≤ 2 today)
  // psoId is per-pass — lives on DrawItem (Phase C), not Submesh

  // GPU range info
  PoolAlloc vbAlloc;             // {poolId, offsetElems, count}
  PoolAlloc ibAlloc;
};
```

`vbPoolId`/`ibPoolId` are not "another table" in the conceptual sense
— they are arithmetic indices into existing arrays
(`MeshAssetCache::m_vertexPools`), computed once at `Acquire`/
`Suballocate` from `(vertexAttribKey, vertexStride)`. There is no
parallel hash to keep in sync with `ShaderKey`. The duplication with
`vbAlloc.poolId` is intentional: `PoolAlloc` is for GPU draw calls
(needs offset+count), `vbPoolId` is for sort-key packing (needs to fit
in a u16 slot).

### Sort key bit layout (target — Phase C)

**Opaque** (gbuffer, depth, shadow):
```
bit  63..58  pass         (6)   pass-segregation if multi-pass interleave
bit  57..54  layer        (4)   viewport / split-screen / RT slot
bit  53..38  psoId        (16)  ← MOST EXPENSIVE  (PSO + IA layout)
bit  37..22  materialId   (16)  ← descriptor-table change
bit  21..14  vbPoolId     (8)   ← VB binding (largely correlated w/ PSO)
bit  13      ibPoolId     (1)   ← 16/32-bit IB selection
bit  12.. 0  depthBucket  (13)  ← Hi-Z friendliness, front-to-back
```

**Transparent** (forward blend):
```
bit  63..58  pass
bit  57..54  layer
bit  53..30  depthBucket  (24)  ← back-to-front, correctness > perf
bit  29..14  psoId
bit  13.. 0  materialId low
```

`vbPoolId` is partially redundant with `psoId` (same vertex layout →
same PSO group) but kept as a tiebreak when two PSOs serve different
material features but share a vertex format. Cost of including it: 8
bits. Drop if profiling shows no benefit.

### Mapping to "how the pros do it"

- **UE**: `FCachedPSOInitializer` + `FCachedMeshDrawCommand` carry
  separately-cached indices for pipeline, vertex factory, etc., all
  derived from a canonical `FMeshMaterialShaderType` identity. Same
  pattern.
- **Unity DOTS**: `BatchMaterialID` + `BatchMeshID` + `BatchID` are
  derived dense ids. Same pattern.
- **Frostbite (DICE)**: `RenderRecipe` is the canonical key, with
  `PSOHandle` / `VertexLayoutHandle` derived. Same pattern.

---

## 6. Constants and conventions

- **AABB**: `t850::AABB` in `Framework/include/utils/Picking.h`. Fields
  are `vMin`/`vMax` (NOT `min`/`max`) to dodge `Windows.h` macros.
  `RenderMesh::AABB` is a legacy duplicate slated for removal.
- **ShaderKey**: 64-bit packed feature mask in
  `Framework/Descriptors.h`. Bits 0..4 + 39..40 are vertex-attribute
  bits, captured in the named constant `ShaderKey::VERTEX_ATTRIB_MASK`.
  Material features are bits 5..14 + 26..38; pass type is bits 20..25.
- **Backbones**: `Framework/include/video/BaseDriver.h` defines
  `VertexBuffer`, `IndexBuffer`, `Texture`, `Buffer` — virtual interface
  implemented per backend (D3D11/D3D12/GL/Vulkan). All four backends
  resolve vertex IA via shader reflection, so adding new vertex inputs
  is a shader-only change.
- **Logs**: cache hits/misses logged via `T8_LOG_INFO`. There is no
  WARNING level — use `T8_LOG_ERROR` for non-fatal recoverable issues.
- **Dump**: `MeshAssetCache::Get().DumpToLog()` prints all live assets
  with refcounts; useful from the editor's dev panel.

---

## 7. Files of interest

| File | Purpose |
|---|---|
| `Framework/include/scene/MeshAsset.h` | Submesh + MeshAsset + PoolAlloc |
| `Framework/include/scene/MeshPool.h` | VertexPool + IndexPool (Tier 1) |
| `Framework/src/scene/MeshPool.cpp` | Pool impl, lazy GPU upload |
| `Framework/include/scene/MeshAssetCache.h` | Singleton cache API + pool registry |
| `Framework/src/scene/MeshAssetCache.cpp` | Cache impl + GPU resource cleanup |
| `Framework/include/scene/RenderMesh.h` | Legacy entity+asset fusion (transitional) |
| `Framework/src/scene/RenderMesh.cpp` | `Load/Create/Draw/Destroy`, asset-cache hooks |
| `Framework/include/scene/RenderSkinnedMesh.h` | Adds skeleton + animation on top of RenderMesh |
| `Framework/include/scene/PrimitiveInstance.h` | Per-instance transform + textures (becomes RenderEntity facade) |
| `Framework/Descriptors.h` | `ShaderKey` bits + masks |
| `Framework/include/utils/Picking.h` | Canonical `AABB` and ray helpers |

---

## 8. Open evolution notes

- **`MaterialAsset`** is done (Phase B). `SubSetInfo` keeps a
  `MaterialAsset* matAsset` borrowed pointer; the bloated material
  fields on `SubSetInfo` are scratch storage for the parse loop and
  are no longer read at draw time.
- **`RenderEntity`** + per-pass `RenderQueue` (Phase C) is where the
  cross-entity sort actually appears. Until then, sort still happens
  per entity inside `RenderMesh::Draw`.
- **CB split** (Phase D) requires shader CB layout changes — not
  invisible. Will be done with FrameDumper PPM diff validation.
- **`SkeletonAsset` + capsule data** is a separate workstream; the
  architecture above describes the seam where it plugs in. Implementing
  it requires choosing a physics backend (Jolt / PhysX / Bullet) — out
  of scope for the current refactor sequence but the data layout above
  is intended to survive that choice.
